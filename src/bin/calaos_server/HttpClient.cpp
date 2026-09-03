/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos.
 **
 **  Calaos is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
#include "RemoteUIProvisioningHandler.h"
#include "OtaHttpHandler.h"
#include "HttpClient.h"
#include "HttpServer.h"
#include "hef_uri_syntax.h"
#include "Prefix.h"
#include "CalaosConfig.h"
#include "HttpCodes.h"
#include "libuvw.h"

using namespace Calaos;

//Only used here, to refuse a request body bigger than
//TransportLimits::maxHttpBodySize()
#define HTTP_413 "HTTP/1.0 413 Payload Too Large"
#define HTTP_413_BODY "<html><head>" \
    "<title>413 Payload Too Large</title>" \
    "</head>" \
    "<body>" \
    "<h1>Calaos Server - Payload Too Large</h1>" \
    "<p>The request body is bigger than what the server accepts.</p>" \
    "</body>" \
    "</html>"

//Only used here, to refuse a request with more than
//TransportLimits::MaxHeadersSize bytes of headers
#define HTTP_431 "HTTP/1.0 431 Request Header Fields Too Large"
#define HTTP_431_BODY "<html><head>" \
    "<title>431 Request Header Fields Too Large</title>" \
    "</head>" \
    "<body>" \
    "<h1>Calaos Server - Request Header Fields Too Large</h1>" \
    "<p>The request headers are bigger than what the server accepts.</p>" \
    "</body>" \
    "</html>"

#define HTTP_429_BODY "<html><head>" \
    "<title>429 Too Many Requests</title>" \
    "</head>" \
    "<body>" \
    "<h1>Calaos Server - Too Many Requests</h1>" \
    "<p>Too many simultaneous connections from this address.</p>" \
    "</body>" \
    "</html>"

//--- TransportLimits: config overrides -------------------------------------
//Each limit is read from local_config.xml once, on first use, and clamped
//back to its default when unset or broken. The literal keys below are
//declared in the option registry (src/lib/ConfigOptions.cpp) and checked by
//tests/check-config-options.sh.
uint64_t TransportLimits::maxHttpBodySize()
{
    static const uint64_t v = parseLimit(
        Utils::get_config_option("max_http_body_size"),
        DefaultMaxHttpBodySize, 4096, 1024 * 1024 * 1024);
    return v;
}

uint64_t TransportLimits::maxWebsocketMessageSize()
{
    static const uint64_t v = parseLimit(
        Utils::get_config_option("max_websocket_message_size"),
        DefaultMaxWebsocketMessageSize, 4096, 1024 * 1024 * 1024);
    return v;
}

std::size_t TransportLimits::maxConnections()
{
    static const std::size_t v = parseLimit(
        Utils::get_config_option("max_connections"),
        DefaultMaxConnections, 1, 10000);
    return v;
}

std::size_t TransportLimits::maxConnectionsPerIp()
{
    static const std::size_t v = parseLimit(
        Utils::get_config_option("max_connections_per_ip"),
        DefaultMaxConnectionsPerIp, 1, 10000);
    return v;
}

double TransportLimits::requestReadTimeout()
{
    static const double v = (double)parseLimit(
        Utils::get_config_option("request_read_timeout"),
        (uint64_t)DefaultRequestReadTimeout, 1, 600);
    return v;
}

HttpClient::HttpClient(const std::shared_ptr<uvw::TcpHandle> &client):
    client_conn(client)
{
    //The llhttp callbacks live header-inline in HttpParsing (HttpClient.h)
    //and only ever see the RequestState part of this object: per-request
    //state, wiped by _parser_begin on every request of the connection
    HttpParsing::initParserSettings(parser_settings);

    maxBodySize = TransportLimits::maxHttpBodySize();

    parser = (llhttp_t *)calloc(1, sizeof(llhttp_t));
    reinitParser();

    cDebugDom("network") << this;

    client_conn->once<uvw::ErrorEvent>([this, token = std::weak_ptr<bool>(alive)](const auto &, auto &)
    {
        if (token.expired()) return;

        cCriticalDom("network")
                << "Error sending data ! Closing connection.";

        this->CloseConnection();
    });

    //A client that opens a connection and then says nothing (or sends its
    //headers one byte at a time) holds a slot forever, and slots are capped.
    //It is cancelled as soon as a complete request has been read.
    readTimeout = new Timer(TransportLimits::requestReadTimeout(), [this]()
    {
        cWarningDom("network")
                << "No complete request after "
                << TransportLimits::requestReadTimeout()
                << "s, closing connection";

        this->CloseConnection();
    });
}

HttpClient::~HttpClient()
{
    if (ipTracked)
        HttpServer::Instance().releaseClientIp(trackedIp);

    delete jsonApi;
    delete remoteUIHandler;
    delete otaHandler;
    free(parser);
    DELETE_NULL(closeTimer);
    DELETE_NULL(readTimeout);

    cDebugDom("network") << this;
}

void HttpClient::cancelReadTimeout()
{
    DELETE_NULL(readTimeout);
}

void HttpClient::sendRequestTooLarge()
{
    Params headers;
    headers.Add("Connection", "close");
    headers.Add("Content-Type", "text/html");
    string res = buildHttpResponse(HTTP_413, headers, HTTP_413_BODY);
    sendToClient(res);
}

void HttpClient::sendRequestHeadersTooLarge()
{
    Params headers;
    headers.Add("Connection", "close");
    headers.Add("Content-Type", "text/html");
    string res = buildHttpResponse(HTTP_431, headers, HTTP_431_BODY);
    sendToClient(res);
}

bool HttpClient::trackPerIpCap()
{
    if (ipTracked)
        return true;

    string ip = getEffectiveClientIp();

    if (!HttpServer::Instance().trackClientIp(ip))
    {
        cWarningDom("network")
                << "Client " << ip << " already has "
                << TransportLimits::maxConnectionsPerIp()
                << " connections opened, refusing this one";
        return false;
    }

    ipTracked = true;
    trackedIp = ip;
    return true;
}

int HttpClient::processHeaders(const string &request)
{
    enum llhttp_errno err = llhttp_execute(parser, request.c_str(), request.size());

    if (headersTooLarge)
    {
        headersTooLarge = false;

        cWarningDom("network") << "Request headers are bigger than "
                << TransportLimits::MaxHeadersSize << " bytes, rejecting it";

        cancelReadTimeout();
        sendRequestHeadersTooLarge();

        return HTTP_PROCESS_DONE;
    }

    if (bodyTooLarge)
    {
        bodyTooLarge = false;

        cWarningDom("network") << "Request body is bigger than "
                << TransportLimits::maxHttpBodySize() << " bytes, rejecting it";

        cancelReadTimeout();
        sendRequestTooLarge();

        return HTTP_PROCESS_DONE;
    }

    if (err != HPE_OK &&
        err != HPE_PAUSED &&
        err != HPE_PAUSED_UPGRADE &&
        err != HPE_PAUSED_H2_UPGRADE)
    {
        /* Handle error. Usually just close the connection. */
        CloseConnection();

        cDebugDom("network") << "Error parsing HTTP request: " << llhttp_errno_name(err)
                << " (" << parser->reason << ")";

        return HTTP_PROCESS_DONE;
    }

    if (!parse_done)
        return HTTP_PROCESS_MOREDATA;

    cancelReadTimeout();

    //Per-source connection cap, enforced on the first parsed request rather
    //than at accept time: the client identity may come from X-Forwarded-For,
    //which does not exist before the headers are read. The global cap at accept
    //time (HttpServer::addConnection) still bounds what an unparsed connection
    //can hold.
    if (!trackPerIpCap())
    {
        Params headers;
        headers.Add("Connection", "close");
        headers.Add("Content-Type", "text/html");
        string res = buildHttpResponse(HTTP_429, headers, HTTP_429_BODY);
        sendToClient(res);

        return HTTP_PROCESS_DONE;
    }

    //Finally parsing of request is done, we can search for
    //a response for the requested path

    cDebugDom("network") << "Client headers: HTTP/" << Utils::to_string(parser->http_major) << "." << Utils::to_string(parser->http_minor) << " " << parse_url;
    for (auto it = request_headers.begin();it!= request_headers.end();++it)
        cDebugDom("network") << it->first << ": " << it->second;

    //Handle CORS here
    if (request_headers.find("origin") != request_headers.end())
    {
        resHeaders.Add("Access-Control-Allow-Origin", request_headers["origin"]);
        resHeaders.Add("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");
    }

    if (request_method == HTTP_OPTIONS)
    {
        if (request_headers.find("access-control-request-method") != request_headers.end())
            resHeaders.Add("Access-Control-Allow-Methods", "GET, POST, OPTIONS");

        if (request_headers.find("access-control-request-headers") != request_headers.end())
            resHeaders.Add("Access-Control-Allow-Headers", "{" + request_headers["access-control-request-headers"] + "}");

        Params headers;
        headers.Add("Connection", "Close");
        headers.Add("Cache-Control", "no-cache, must-revalidate");
        headers.Add("Expires", "Mon, 26 Jul 1997 05:00:00 GMT");
        headers.Add("Content-Type", "text/html");
        string res = buildHttpResponse(HTTP_200, headers, "");
        sendToClient(res);

        return HTTP_PROCESS_DONE;
    }

    //If client asks for websocket just return and let websocket class handle connection
    if (Utils::strContains(request_headers["connection"], "upgrade", Utils::CaseInsensitive) &&
        Utils::str_to_lower(request_headers["upgrade"]) == "websocket")
    {
        cDebugDom("websocket") << "Upgrading connection to WebSocket";
        isWebsocket = true;
        return HTTP_PROCESS_WEBSOCKET;
    }

    if (parser->upgrade)
    {
        /* handle new protocol */
        cDebugDom("network") << "Protocol Upgrade not supported, closing connection.";
        CloseConnection();

        return HTTP_PROCESS_DONE;
    }

    hef::HfURISyntax req_url("http://0.0.0.0" + parse_url);

    //decode GET parameters
    paramsGET.clear();
    vector<string> pars;
    Utils::split(req_url.getQuery(), pars, "&");

    for (const string &s: pars)
    {
        vector<string> p;
        Utils::split(s, p, "=", 2);
        paramsGET.Add(p[0], p[1]);
    }

    if (req_url.getPath() == "/debug" ||
        req_url.getPath() == "/debug/")
    {
        Params headers;
        headers.Add("Connection", "close");
        headers.Add("Content-Type", "text/html");
        headers.Add("Location", "/debug/index.html");
        string res = buildHttpResponse(HTTP_301, headers, string());
        sendToClient(res);

        return HTTP_PROCESS_DONE;
    }

    //Only enable debug http access if enabled explicitely in config
    bool isDebugEnabled = Utils::get_config_option("debug_enabled") == "true";

    if (Utils::strStartsWith(req_url.getPath(), "/debug/", Utils::CaseInsensitive) &&
        isDebugEnabled)
    {
        cDebugDom("network") << "Sending debug pages";

        string path = req_url.getPath();
        path.erase(0, 7);

        string wwwroot = Utils::get_config_option("debug_wwwroot");
        if (!FileUtils::isDir(wwwroot))
            wwwroot = Prefix::Instance().dataDirectoryGet() + "/debug";

        cDebugDom("network") << "Using www root: " << wwwroot;

        string fileName;
        if (!FileUtils::resolveSafePath(wwwroot, path, fileName) ||
            FileUtils::isDir(fileName))
        {
            cWarningDom("network") << "Rejected debug path (traversal or not found): " << path;

            Params headers;
            headers.Add("Connection", "close");
            headers.Add("Content-Type", "text/html");
            string res = buildHttpResponse(HTTP_404, headers, HTTP_404_BODY);
            sendToClient(res);

            return HTTP_PROCESS_DONE;
        }

        string filext = str_to_lower(path.substr(path.find_last_of(".") + 1));

        Params headers;
        headers.Add("Connection", "Close");
        headers.Add("Content-Type", getMimeType(filext));
        cDebug() << "send file " << fileName << "to Client";
        string res = buildHttpResponseFromFile(HTTP_200, headers, fileName);
        sendToClient(res);

        return HTTP_PROCESS_DONE;
    }

    if (req_url.getPath() == "/" ||
        req_url.getPath() == "/app" ||
        req_url.getPath() == "/app/")
    {
        Params headers;
        headers.Add("Connection", "close");
        headers.Add("Content-Type", "text/html");
        headers.Add("Location", "/app/index.html");
        string res = buildHttpResponse(HTTP_301, headers, string());
        sendToClient(res);

        return HTTP_PROCESS_DONE;
    }

    if (Utils::strStartsWith(req_url.getPath(), "/app/", Utils::CaseInsensitive))
    {
        cDebugDom("network") << "Sending webapp pages";

        string path = req_url.getPath();
        path.erase(0, 5);

        string wwwroot = Utils::get_config_option("wwwroot");
        if (!FileUtils::isDir(wwwroot))
            wwwroot = Prefix::Instance().dataDirectoryGet() + "/app";

        string fileName;
        if (!FileUtils::resolveSafePath(wwwroot, path, fileName) ||
            FileUtils::isDir(fileName))
        {
            cWarningDom("network") << "Rejected webapp path (traversal or not found): " << path;

            Params headers;
            headers.Add("Connection", "close");
            headers.Add("Content-Type", "text/html");
            string res = buildHttpResponse(HTTP_404, headers, HTTP_404_BODY);
            sendToClient(res);

            return HTTP_PROCESS_DONE;
        }

        string filext = str_to_lower(path.substr(path.find_last_of(".") + 1));

        Params headers;
        headers.Add("Connection", "Close");
        headers.Add("Content-Type", getMimeType(filext));
        cDebug() << "send file " << fileName << "to Client";
        string res = buildHttpResponseFromFile(HTTP_200, headers, fileName);
        sendToClient(res);

        return HTTP_PROCESS_DONE;
    }

    if (req_url.getPath() != "/api" &&
        req_url.getPath() != "/api.php" &&
        req_url.getPath() != "/api/v2" &&
        !Utils::strStartsWith(req_url.getPath(), "/api/v3"))
    {
        Params headers;
        headers.Add("Connection", "close");
        headers.Add("Content-Type", "text/html");
        string res = buildHttpResponse(HTTP_404, headers, HTTP_404_BODY);
        sendToClient(res);

        return HTTP_PROCESS_DONE;
    }

    proto_ver = APINONE;
    if (req_url.getPath() == "/api.php" ||
        req_url.getPath() == "/api")
        proto_ver = API_HTTP;

    return HTTP_PROCESS_HTTP;
}

void HttpClient::DataWritten(int size)
{
    data_size -= size;

    cDebugDom("network") << size << " bytes has been written, " << data_size << " bytes remaining";

    if (data_size <= 0 && need_restart)
    {
        cDebugDom("network")
                << "All config files written, restarting calaos_server";
        uvw::Loop::getDefault()->stop();
    }

    if (conn_close && data_size <= 0)
    {
        cDebugDom("network")
                << "All data sent, close connection";

        //Close connection in 500ms if not closed by client. This forces the closing and
        //has to be done because lighttpd mod_proxy keeps connection open regardless of the Connection: close header
        if (!closeTimer)
            closeTimer = new Timer(0.5, sigc::mem_fun(this, &HttpClient::CloseConnection));
        else
            closeTimer->Reset(0.5);
    }
}

void HttpClient::CloseConnection()
{
    if (isClosing)
        return; //already closing...
    isClosing = true;

    DELETE_NULL(closeTimer);

    cDebugDom("network") << "Closing connection...";
    client_conn->once<uvw::ErrorEvent>([](const uvw::ErrorEvent &ev, auto &h)
    {
        cDebugDom("network") << "Shutdown failed: " << ev.what() << ". Closing.";
        h.close();
    });
    client_conn->on<uvw::ShutdownEvent>([](const uvw::ShutdownEvent &, auto &h)
    {
        //After shutdown close handle. Closing through the handle itself, not
        //through this->client_conn: the HttpClient may be gone by now (the
        //peer can close first, EndEvent -> CloseEvent -> delete).
        cDebugDom("network") << "Shutdown done. Closing.";
        h.close();
    });
    client_conn->shutdown();
}

string HttpClient::buildHttpResponseFromFile(string code, Params &headers, string fileName)
{
    ifstream file(fileName);
    string body((std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());

    return buildHttpResponse(code, headers, body);
}

string HttpClient::buildHttpResponse(string code, Params &headers, string body)
{
    stringstream res;

    //HTTP code
    res << code << "\r\n";

    for (int i = 0;i < headers.size();i++)
    {
        string key, value;
        headers.get_item(i, key, value);
        resHeaders.Add(key, value);
    }

    if (!resHeaders.Exists("Content-Length"))
        resHeaders.Add("Content-Length", Utils::to_string(body.length()));

    if (Utils::str_to_lower(request_headers["connection"]) == "close")
    {
        resHeaders.Add("Connection", "Close");
        cDebugDom("network")
                << "Client requested Connection: Close";
    }

    if (Utils::str_to_lower(resHeaders["Connection"]) == "close")
        conn_close = true;
    else
        conn_close = false;

    //headers
    for (int i = 0;i < resHeaders.size();i++)
    {
        string key, value;
        resHeaders.get_item(i, key, value);
        res << key << ": " << value << "\r\n";
    }

    res << "\r\n";

    //body
    res << body;

    return res.str();
}

void HttpClient::sendToClient(string res)
{
    int dataSize = res.length();
    data_size += dataSize;

    cDebugDom("network") << "Sending " << dataSize << " bytes, data_size = " << data_size;

    if (client_conn->closing() || isClosing)
        return;

    auto dataWrite = std::unique_ptr<char[]>(new char[dataSize]);
    std::copy(res.begin(), res.end(), dataWrite.get());
    client_conn->write(std::move(dataWrite), dataSize);

    //The uvw handle outlives this HttpClient: if the client is deleted with
    //writes still in flight, libuv delivers their completion afterwards, and
    //a raw `this` would dangle. The alive token makes the stale callback a
    //no-op instead (wave-4 finding, was relying on libuv delivery order).
    client_conn->once<uvw::WriteEvent>([this, dataSize, token = std::weak_ptr<bool>(alive)](const auto &, auto &)
    {
        if (token.expired()) return;
        this->DataWritten(dataSize);
    });
}

void HttpClient::handleJsonRequest(uint8_t parser_method)
{
    // Check if this is a RemoteUI API request first
    if (!remoteUIHandler)
    {
        remoteUIHandler = new RemoteUIProvisioningHandler(this);
    }

    // Extract HTTP method and URL path
    string method;
    switch (parser_method)
    {
        case HTTP_GET: method = "GET"; break;
        case HTTP_POST: method = "POST"; break;
        case HTTP_PUT: method = "PUT"; break;
        case HTTP_DELETE: method = "DELETE"; break;
        default: method = "UNKNOWN"; break;
    }

    hef::HfURISyntax req_url("http://0.0.0.0" + parse_url);
    string uri = req_url.getPath();

    // Route to RemoteUI handler if URL matches
    if (remoteUIHandler->canHandleRequest(uri, method))
    {
        remoteUIHandler->processRequest(uri, method, bodymessage, paramsGET);
        return;
    }

    // Route to OTA handler if URL matches
    if (!otaHandler)
        otaHandler = new Calaos::OtaHttpHandler(this);

    if (otaHandler->canHandleRequest(uri, method))
    {
        otaHandler->processRequest(uri, method, bodymessage, paramsGET);
        return;
    }

    // Fall back to standard JSON API handler
    if (!jsonApi)
    {
        if (proto_ver == API_HTTP)
            jsonApi = new JsonApiHandlerHttp(this);
        else
        {
            cWarningDom("network") << "API version not implemented";
            return;
        }

        jsonApi->sendData.connect([=](const string &data)
        {
            sendToClient(data);
        });
        jsonApi->closeConnection.connect([=](int c, const string &r)
        {
            VAR_UNUSED(c);
            VAR_UNUSED(r);
            CloseConnection();
        });
    }

    jsonApi->processApi(bodymessage, paramsGET);
}

string HttpClient::getMimeType(const string &file_ext)
{
    if (file_ext == "js")
        return "application/javascript";
    else if (file_ext == "css")
        return "text/css";
    else if (file_ext == "jpg" || file_ext == "jpeg")
        return "image/jpeg";
    else if (file_ext == "png")
        return "image/png";
    else if (file_ext == "gif")
        return "image/gif";
    else if (file_ext == "tiff")
        return "image/tiff";
    else if (file_ext == "svg")
        return "image/svg+xml";
    else if (file_ext == "pdf")
        return "application/pdf";
    else if (file_ext == "html" || file_ext == "htm")
        return "text/html";
    else if (file_ext == "xml")
        return "application/xml";
    else if (file_ext == "json")
        return "application/json";
    else if (file_ext == "ttf")
        return "application/octet-stream";
    else if (file_ext == "woff")
        return "application/font-woff";
    else if (file_ext == "eot")
        return "application/vnd.ms-fontobject";

    return "text/plain";
}

string HttpClient::getClientIp() const
{
    if (!client_conn)
        return "unknown";

    try
    {
        auto addr = client_conn->peer<uvw::IPv4>();
        if (!addr.ip.empty())
            return addr.ip;

        // Try IPv6 if IPv4 failed
        auto addr6 = client_conn->peer<uvw::IPv6>();
        if (!addr6.ip.empty())
            return addr6.ip;
    }
    catch (...)
    {
        // If both fail, return unknown
    }

    return "unknown";
}
