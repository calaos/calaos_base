/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
#include "HttpClient.h"
#include "HttpServer.h"
#include "hef_uri_syntax.h"
#include "Prefix.h"
#include "CalaosConfig.h"
#include "HttpCodes.h"
#include "uvw/src/uvw.hpp"

using namespace Calaos;

#ifndef json_array_foreach
#define json_array_foreach(array, index, value) \
    for(index = 0; \
    index < json_array_size(array) && (value = json_array_get(array, index)); \
    index++)
#endif

int _parser_begin(http_parser *parser)
{
    HttpClient *client = reinterpret_cast<HttpClient *>(parser->data);

    //reset status flags to parse another request on the same connection
    client->parse_done = false;
    client->has_field = false;
    client->has_value = false;
    client->hfield.clear();
    client->hvalue.clear();
    client->bodymessage.clear();
    client->parse_url.clear();

    return 0;
}

int _parser_header_field(http_parser *parser, const char *at, size_t length)
{
    HttpClient *client = reinterpret_cast<HttpClient *>(parser->data);

    if (client->has_field && client->has_value)
    {
        client->request_headers[Utils::str_to_lower(client->hfield)] = client->hvalue;
        client->has_field = false;
        client->has_value = false;
        client->hfield.clear();
        client->hvalue.clear();
    }

    if (!client->has_field)
        client->has_field = true;

    client->hfield.append(at, length);

    return 0;
}

int _parser_header_value(http_parser *parser, const char *at, size_t length)
{
    HttpClient *client = reinterpret_cast<HttpClient *>(parser->data);

    if (!client->has_value)
        client->has_value = true;

    client->hvalue.append(at, length);

    return 0;
}

int _parser_headers_complete(http_parser *parser)
{
    HttpClient *client = reinterpret_cast<HttpClient *>(parser->data);

    if (client->has_field && client->has_value)
    {
        client->request_headers[Utils::str_to_lower(client->hfield)] = client->hvalue;
        client->has_field = false;
        client->has_value = false;
        client->hfield.clear();
        client->hvalue.clear();
    }

    return 0;
}

int _parser_url(http_parser *parser, const char *at, size_t length)
{
    HttpClient *client = reinterpret_cast<HttpClient *>(parser->data);

    client->parse_url.append(at, length);

    return 0;
}

int _parser_message_complete(http_parser *parser)
{
    HttpClient *client = reinterpret_cast<HttpClient *>(parser->data);

    client->parse_done = true;
    client->request_method = parser->method;

    return 0;
}

int _parser_body_complete(http_parser* parser, const char *at, size_t length)
{
    HttpClient *client = reinterpret_cast<HttpClient *>(parser->data);

    client->bodymessage.append(at, length);

    return 0;
}

HttpClient::HttpClient(const std::shared_ptr<uvw::TcpHandle> &client):
    client_conn(client)
{
    //set up callbacks for the parser
    parser_settings.on_message_begin = _parser_begin;
    parser_settings.on_url = _parser_url;
    parser_settings.on_header_field = _parser_header_field;
    parser_settings.on_header_value = _parser_header_value;
    parser_settings.on_headers_complete = _parser_headers_complete;
    parser_settings.on_body = _parser_body_complete;
    parser_settings.on_message_complete = _parser_message_complete;

    parser = (http_parser *)calloc(1, sizeof(http_parser));
    http_parser_init(parser, HTTP_REQUEST);
    parser->data = this;

    cDebugDom("network") << this;

    client_conn->once<uvw::ErrorEvent>([this](const auto &, auto &)
    {
        cCriticalDom("network")
                << "Error sending data ! Closing connection.";

        this->CloseConnection();
    });
}

HttpClient::~HttpClient()
{
    delete jsonApi;
    free(parser);
    DELETE_NULL(closeTimer);

    cDebugDom("network") << this;
}

int HttpClient::processHeaders(const string &request)
{
    size_t nparsed;

    nparsed = http_parser_execute(parser, &parser_settings, request.c_str(), request.size());

    if (nparsed != request.size())
    {
        /* Handle error. Usually just close the connection. */
        CloseConnection();

        return HTTP_PROCESS_DONE;
    }

    if (!parse_done)
        return HTTP_PROCESS_MOREDATA;

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

    for (const string s: pars)
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
        headers.Add("Location", "debug/index.html");
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

        string fileName = wwwroot +
                          (wwwroot[wwwroot.length() - 1] == '/'?"":"/") + path;

        if (!FileUtils::exists(fileName))
        {
            cDebugDom("network") << "fileName not found: " << fileName;

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
        headers.Add("Location", "app/index.html");
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

        string fileName = wwwroot +
                          (wwwroot[wwwroot.length() - 1] == '/'?"":"/") + path;

        if (!FileUtils::exists(fileName) || FileUtils::isDir(fileName))
        {
            cDebugDom("network") << "Filename not found: " << fileName;

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
        req_url.getPath() != "/api/v2")
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

    //TODO: get url parameters here?
    //for example get username/password as a url parameter

    //Handle CORS here
    if (request_headers.find("Origin") != request_headers.end())
    {
        resHeaders.Add("Access-Control-Allow-Origin", request_headers["Origin"]);
        resHeaders.Add("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");
    }

    if (request_method == HTTP_OPTIONS)
    {
        if (request_headers.find("Access-Control-Request-Method") != request_headers.end())
            resHeaders.Add("Access-Control-Allow-Methods", "GET, POST, OPTIONS");

        if (request_headers.find("Access-Control-Request-Headers") != request_headers.end())
            resHeaders.Add("Access-Control-Allow-Headers", request_headers["Access-Control-Request-Headers"]);

        resHeaders.Add("Connection", "Close");
        resHeaders.Add("Cache-Control", "no-cache, must-revalidate");
        resHeaders.Add("Expires", "Mon, 26 Jul 1997 05:00:00 GMT");
        resHeaders.Add("Content-Type", "text/html");
        string res = buildHttpResponse(HTTP_200, resHeaders, "");
        sendToClient(res);

        return HTTP_PROCESS_DONE;
    }

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

    cDebugDom("network") << "Closing connection";
    client_conn->shutdown();
    client_conn->on<uvw::ShutdownEvent>([this](const uvw::ShutdownEvent &, auto &)
    {
        //After shutdown close handle
        client_conn->close();
    });
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

    client_conn->write((char *)res.c_str(), dataSize);
    client_conn->once<uvw::WriteEvent>([this, dataSize](const auto &, auto &)
    {
        this->DataWritten(dataSize);
    });
}

void HttpClient::handleJsonRequest()
{
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
