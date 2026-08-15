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
#ifndef S_HttpClient_H
#define S_HttpClient_H

#include "Calaos.h"
#include "llhttp.h"
#include <unordered_map>
#include "JsonApiHandlerHttp.h"
#include "JsonApiHandlerWS.h"
#include "Timer.h"

using namespace Calaos;

namespace uvw {
//Forward declare classes here to prevent long build time
//because of uvw.hpp being header only
class TcpHandle;
}

namespace Calaos {
class RemoteUIProvisioningHandler;
class OtaHttpHandler;
}

/* Transport limits of the http/websocket port, shared by HttpClient,
 * WebSocket and HttpServer.
 *
 * The biggest legitimate payload a Calaos client sends is ~215 KiB
 * (calaos_installer pushing io.xml and rules.xml in one request), every other
 * client stays far below that. The caps here keep a x20 margin and stop an
 * unauthenticated client from making the server allocate until it dies.
 *
 * The websocket frame cap is WebSocketFrame::MAX_FRAME_SIZE_IN_BYTES and has
 * the same value: it lives in src/lib, which cannot include a calaos_server
 * header.
 *
 * Every limit but the header cap can be overridden from local_config.xml
 * (keys of the same name in snake_case, see docs/16_config_options.md). The
 * accessors read the config once, on first use, and fall back to the default
 * on an empty, non numeric or out of range value: a broken config must never
 * turn a hardening limit off.
 */
namespace TransportLimits
{
//Defaults, unchanged from when the limits were hard wired.
static constexpr uint64_t DefaultMaxHttpBodySize = 4 * 1024 * 1024;
static constexpr uint64_t DefaultMaxWebsocketMessageSize = 4 * 1024 * 1024;
static constexpr uint64_t DefaultMaxConnections = 100;
static constexpr uint64_t DefaultMaxConnectionsPerIp = 50;
static constexpr double DefaultRequestReadTimeout = 30.0;

//Total bytes accepted for the request line + headers of one request. Refused
//with a 431. Not configurable on purpose: haproxy sits in front of
//calaos_server in calaos-os and cannot forward more than tune.bufsize
//(16 KiB by default) of headers anyway, so 32 KiB never rejects a request
//that went through it.
static constexpr std::size_t MaxHeadersSize = 32 * 1024;

//Biggest http request body accepted. Checked on Content-Length as soon as the
//headers are parsed, and again on every body chunk for the requests that
//announce no length (chunked). Refused with a 413.
uint64_t maxHttpBodySize();

//Biggest websocket message accepted, fragments included. Refused with a 1009
//close frame. A value above WebSocketFrame::MAX_FRAME_SIZE_IN_BYTES only
//takes effect on fragmented messages: a single frame stays capped at 4 MiB.
uint64_t maxWebsocketMessageSize();

//Simultaneous connections accepted on the port. Above that, a connection is
//answered 503 and closed right away.
std::size_t maxConnections();

//Simultaneous connections accepted from one client. Above that, the request
//is answered 429 and the connection closed, so that one client cannot occupy
//every maxConnections() slot and evict everybody else. The client is the
//address haproxy saw (X-Forwarded-For), not the TCP peer: every connection
//shares the proxy address, see effectiveClientIp() below.
std::size_t maxConnectionsPerIp();

//Delay a connection is given to send a complete request. It only covers the
//time before the first request is parsed, so it never applies to an opened
//websocket (which has its own ping keepalive), nor to a long poll or a mjpeg
//stream (their request is parsed long before the delay expires), only to a
//client that connects and then sends nothing or dribbles headers.
double requestReadTimeout();

//Parses a config override for one of the limits above. def is returned when
//value is empty (key not set), not a plain positive number, or outside
//[minValue, maxValue]. Pure, unit tested in tests/TransportHardening_test.cpp.
inline uint64_t parseLimit(const std::string &value, uint64_t def,
                           uint64_t minValue, uint64_t maxValue)
{
    if (value.empty())
        return def;

    uint64_t v = 0;
    std::size_t pos = 0;
    try
    {
        if (value.find_first_not_of("0123456789") != std::string::npos)
            return def;
        v = std::stoull(value, &pos);
    }
    catch (...)
    {
        return def;
    }

    if (pos != value.size() || v < minValue || v > maxValue)
        return def;

    return v;
}

//The client identity of a proxied connection. calaos_server always sits
//behind haproxy in calaos-os, so the TCP peer is the proxy: without this,
//every client collapses into one per-IP bucket. haproxy APPENDS its own
//X-Forwarded-For header line after any client supplied one; the header map
//keeps the last parsed line, and this helper takes the last comma entry of
//that line: the address the trusted proxy hop saw. Everything before it is
//client supplied and can be rotated at will, so it is ignored (same rule as
//the MCP sidecar throttle, T1.8). Falls back to the TCP peer address when the
//header is absent or empty (direct connection, no proxy).
//Pure, unit tested in tests/TransportHardening_test.cpp.
inline std::string effectiveClientIp(const std::string &xffLastLine,
                                     const std::string &peerIp)
{
    std::string::size_type pos = xffLastLine.rfind(',');
    std::string last = (pos == std::string::npos)?
                       xffLastLine:
                       xffLastLine.substr(pos + 1);

    std::string::size_type b = last.find_first_not_of(" \t");
    if (b == std::string::npos)
        return peerIp;
    std::string::size_type e = last.find_last_not_of(" \t");
    last = last.substr(b, e - b + 1);

    return last.empty()? peerIp:last;
}
}

class HttpClient: public sigc::trackable
{
protected:

    std::shared_ptr<uvw::TcpHandle> client_conn;

    llhttp_settings_t parser_settings;
    llhttp_t *parser;

    bool parse_done = false;
    unsigned char request_method;
    unordered_map<string, string> request_headers;

    int proto_ver;

    void CloseConnection();

    //for parsing purposes
    bool has_field = false, has_value = false;
    string hfield, hvalue;
    string bodymessage;
    string parse_url;

    //headers to send back
    Params resHeaders;

    //set to true if connection need to be closed after data has been sent
    bool conn_close = false; //by default we keep-alive connection unless client asks us to close it
    int data_size = 0; //data count remaining

    //special case when config was written, we need to restart calaos_server
    bool need_restart = false;

    //timer to close the connection after data has been written
    Timer *closeTimer = nullptr;

    //timer closing a connection that never sends a complete request
    Timer *readTimeout = nullptr;

    //set when a request body goes over maxHttpBodySize(), the request is then
    //refused with a 413 instead of being buffered
    bool bodyTooLarge = false;

    //bytes of request line + headers accumulated for the request being
    //parsed, reset on every new request of a keep-alive connection
    std::size_t headersSize = 0;

    //set when headersSize goes over TransportLimits::MaxHeadersSize, the
    //request is then refused with a 431 instead of being accumulated
    bool headersTooLarge = false;

    //client identity counted in HttpServer's per-IP connection map. Counted
    //once per connection, on its first parsed request, released by the
    //destructor.
    bool ipTracked = false;
    std::string trackedIp;

    //Lifetime token for the uvw callbacks of this connection (WriteEvent,
    //ErrorEvent, DataEvent...). uvw handles outlive the HttpClient that fed
    //them, so a callback must never touch a raw `this` without first checking
    //this token: the destructor releases it, expiring every weak_ptr taken
    //from it. Same pattern as McpProxyHandler/Rule.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    bool isClosing = false;

    bool isWebsocket = false;

    JsonApi *jsonApi = nullptr;
    RemoteUIProvisioningHandler *remoteUIHandler = nullptr;
    OtaHttpHandler *otaHandler = nullptr;

    Params paramsGET;

    enum
    {
        HTTP_PROCESS_MOREDATA = 0,  //need more data for headers
        HTTP_PROCESS_HTTP,          //normal http request that should be processed
        HTTP_PROCESS_WEBSOCKET,     //websocket request
        HTTP_PROCESS_DONE,          //request already processed (for HTTP OPTIONS, or debug.html)
    };
    int processHeaders(const string &request);

    void handleJsonRequest(uint8_t method);

    //stops the request read timeout: the connection has said what it wants
    void cancelReadTimeout();

    void sendRequestTooLarge();
    void sendRequestHeadersTooLarge();

    //Counts this connection in the per-IP cap on its first parsed request.
    //Returns false when the client is over maxConnectionsPerIp(): the caller
    //answers 429 and closes.
    bool trackPerIpCap();

    string getMimeType(const string &file_ext);

    friend int _parser_begin(llhttp_t *parser);
    friend int _parser_header_field(llhttp_t *parser, const char *at, size_t length);
    friend int _parser_header_value(llhttp_t *parser, const char *at, size_t length);
    friend int _parser_headers_complete(llhttp_t *parser);
    friend int _parser_message_complete(llhttp_t *parser);
    friend int _parser_url(llhttp_t *parser, const char *at, size_t length);
    friend int _parser_body_complete(llhttp_t* parser, const char *at, size_t length);
    friend int _check_headers_size(HttpClient *client, size_t length);

public:
    HttpClient(const std::shared_ptr<uvw::TcpHandle> &client);
    virtual ~HttpClient();

    enum { APINONE = 0, API_HTTP, API_WEBSOCKET, API_REMOTE_UI_WEBSOCKET };

    /* Called by JsonApiServer whenever data has been written to client */
    virtual void DataWritten(int size);

    string buildHttpResponse(string code, Params &headers, string body);
    string buildHttpResponseFromFile(string code, Params &headers, string fileName);

    string getClientIp() const;
    void sendToClient(string res);
    void setNeedRestart(bool e) { need_restart = e; }

    // Expose request headers for handlers that need authentication
    const unordered_map<string, string> &getRequestHeaders() const { return request_headers; }

    //Lifetime token for async callbacks capturing this connection: check
    //expired() before touching the object (see the `alive` member).
    std::weak_ptr<bool> aliveToken() const { return alive; }
};

#endif
