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

//An X-Forwarded-For line is worth exactly as much as the hop that wrote it,
//and the only hop we can recognise is the reverse proxy beside us: it reaches
//calaos_server over the loopback (haproxy backend `server calaos-server
//127.0.0.1:5454`). Any other peer is a client speaking about itself.
//All three spellings are accepted - 127.0.0.0/8 ENTIRE, ::1, ::ffff:127.x -
//or haproxy itself loses its trust on a dual-stack host, silently. Only the
//first two can reach here from a TCP peer, because tcpPeerAddress() unmaps an
//IPv4-mapped one; the third is kept for a caller that would read an address
//from somewhere else, and no such caller exists today.
//PREFIX, never containment: "10.127.0.5" is an ordinary LAN address.
inline bool isTrustedProxyPeer(const std::string &peerIp)
{
    if (peerIp == "::1")
        return true;

    static const std::string mapped("::ffff:");
    const std::string v4 = (peerIp.compare(0, mapped.size(), mapped) == 0)?
                           peerIp.substr(mapped.size()):
                           peerIp;

    return v4.compare(0, 4, "127.") == 0;
}

//The client identity of a proxied connection. calaos_server sits behind
//haproxy in calaos-os, so the TCP peer is the proxy: without this, every
//client collapses into one per-IP bucket. haproxy APPENDS its own
//X-Forwarded-For header line after any client supplied one; the header map
//keeps the last parsed line, and this helper takes the last comma entry of
//that line: the address the trusted proxy hop saw. Everything before it is
//client supplied and can be rotated at will, so it is ignored (same rule as
//the MCP sidecar throttle, T1.8). Falls back to the TCP peer address when the
//header is absent or empty.
//
//The header is read ONLY when the TCP peer is the loopback: port 5454 also
//answers directly from the LAN by default, and a direct client would otherwise
//name its own identity here - see the trust note on getEffectiveClientIp().
//Pure, unit tested in tests/TransportHardening_test.cpp.
inline std::string effectiveClientIp(const std::string &xffLastLine,
                                     const std::string &peerIp)
{
    if (!isTrustedProxyPeer(peerIp))
        return peerIp;

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

/* Per-request parsing state of one http connection, and the llhttp callbacks
 * filling it. Everything here describes THE REQUEST BEING PARSED, never the
 * connection: beginNewRequest() wipes all of it on every on_message_begin, so
 * nothing of request N can leak into request N+1 of a keep-alive connection
 * (T2.16: a stale Origin kept triggering CORS, a stale X-Forwarded-For
 * polluted the client identity of the per-IP cap).
 *
 * Connection-scoped state (conn_close, data_size, isWebsocket, the ip
 * tracking...) stays in HttpClient. The split is header-inline so
 * tests/TransportHardening_test.cpp can drive the exact production callbacks
 * without linking the calaos_server binary.
 */
namespace HttpParsing
{
struct RequestState
{
    //request line + headers, filled by the callbacks below
    bool parse_done = false;
    unsigned char request_method = 0;
    unordered_map<string, string> request_headers;

    //parsing scratch state
    bool has_field = false, has_value = false;
    string hfield, hvalue;
    string bodymessage;
    string parse_url;

    //bytes of request line + headers accumulated for the request being
    //parsed
    std::size_t headersSize = 0;

    //set when headersSize goes over TransportLimits::MaxHeadersSize, the
    //request is then refused with a 431 instead of being accumulated
    bool headersTooLarge = false;

    //set when the request body goes over maxBodySize, the request is then
    //refused with a 413 instead of being buffered
    bool bodyTooLarge = false;

    //body cap checked by the callbacks. Set once from
    //TransportLimits::maxHttpBodySize() by HttpClient (kept as a plain value
    //here so the config accessor does not have to be linked in tests).
    uint64_t maxBodySize = TransportLimits::DefaultMaxHttpBodySize;

    //headers of the response to the request being parsed (CORS echoes of
    //this request's Origin land here)
    Params resHeaders;

    //Forgets everything about the previous request of the connection.
    //Called on llhttp's on_message_begin, i.e. once per request of a
    //keep-alive connection.
    void beginNewRequest()
    {
        parse_done = false;
        request_method = 0;
        request_headers.clear();
        has_field = false;
        has_value = false;
        hfield.clear();
        hvalue.clear();
        bodymessage.clear();
        parse_url.clear();
        headersSize = 0;
        headersTooLarge = false;
        bodyTooLarge = false;
        resHeaders.clear();
    }
};

inline int _parser_begin(llhttp_t *parser)
{
    RequestState *state = static_cast<RequestState *>(parser->data);

    //reset per-request state to parse another request on the same connection
    state->beginNewRequest();

    return 0;
}

//Accounts length more bytes of request line/headers. llhttp itself puts no
//bound on them, so without this a client dribbling an endless header would
//make hvalue grow until the read timeout fires (30 s of free allocation).
//Refused with a 431 as soon as the cap is crossed, nothing more accumulated.
inline int _check_headers_size(RequestState *state, size_t length)
{
    state->headersSize += length;
    if (state->headersSize > TransportLimits::MaxHeadersSize)
    {
        state->headersTooLarge = true;
        return HPE_USER;
    }
    return 0;
}

inline int _parser_header_field(llhttp_t *parser, const char *at, size_t length)
{
    RequestState *state = static_cast<RequestState *>(parser->data);

    if (int err = _check_headers_size(state, length))
        return err;

    if (state->has_field && state->has_value)
    {
        state->request_headers[Utils::str_to_lower(state->hfield)] = state->hvalue;
        state->has_field = false;
        state->has_value = false;
        state->hfield.clear();
        state->hvalue.clear();
    }

    if (!state->has_field)
        state->has_field = true;

    state->hfield.append(at, length);

    return 0;
}

inline int _parser_header_value(llhttp_t *parser, const char *at, size_t length)
{
    RequestState *state = static_cast<RequestState *>(parser->data);

    if (int err = _check_headers_size(state, length))
        return err;

    if (!state->has_value)
        state->has_value = true;

    state->hvalue.append(at, length);

    return 0;
}

inline int _parser_headers_complete(llhttp_t *parser)
{
    RequestState *state = static_cast<RequestState *>(parser->data);

    if (state->has_field && state->has_value)
    {
        state->request_headers[Utils::str_to_lower(state->hfield)] = state->hvalue;
        state->has_field = false;
        state->has_value = false;
        state->hfield.clear();
        state->hvalue.clear();
    }

    //An announced body over the limit is refused here, before a single byte of
    //it has been read from the socket
    if (parser->content_length > state->maxBodySize)
    {
        state->bodyTooLarge = true;
        return -1;
    }

    return 0;
}

inline int _parser_url(llhttp_t *parser, const char *at, size_t length)
{
    RequestState *state = static_cast<RequestState *>(parser->data);

    if (int err = _check_headers_size(state, length))
        return err;

    state->parse_url.append(at, length);

    return 0;
}

inline int _parser_message_complete(llhttp_t *parser)
{
    RequestState *state = static_cast<RequestState *>(parser->data);

    state->parse_done = true;
    state->request_method = parser->method;

    return 0;
}

inline int _parser_body_complete(llhttp_t *parser, const char *at, size_t length)
{
    RequestState *state = static_cast<RequestState *>(parser->data);

    //A chunked body announces no length, so accumulation is what has to be
    //stopped here
    if (state->bodymessage.size() + length > state->maxBodySize)
    {
        state->bodyTooLarge = true;
        return HPE_USER;
    }

    state->bodymessage.append(at, length);

    return 0;
}

//The exact callback wiring HttpClient uses in production, shared with the
//tests so they cannot drift apart.
inline void initParserSettings(llhttp_settings_t &settings)
{
    llhttp_settings_init(&settings);

    settings.on_message_begin = _parser_begin;
    settings.on_url = _parser_url;
    settings.on_header_field = _parser_header_field;
    settings.on_header_value = _parser_header_value;
    settings.on_headers_complete = _parser_headers_complete;
    settings.on_body = _parser_body_complete;
    settings.on_message_complete = _parser_message_complete;
}

//THE one place that binds a parser to the RequestState its callbacks fill.
//
//CONTRACT: parser->data must hold the RequestState SUBOBJECT pointer, never
//the owning object, because every callback above static_casts parser->data
//straight back to RequestState*. RequestState is NOT at offset 0 inside
//HttpClient (a vptr and the sigc::trackable base sit in front of it), so a
//raw `this` stored in parser->data would put every callback read/write 16
//bytes off, silently corrupting the object. Taking the state by reference
//here makes the call site perform the derived-to-base adjustment implicitly:
//never assign parser->data by hand, always go through this helper (or
//HttpClient::reinitParser()).
inline void bindParser(llhttp_t *parser, llhttp_settings_t &settings,
                       RequestState &state)
{
    llhttp_init(parser, HTTP_REQUEST, &settings);
    parser->data = &state;
}
}

class HttpClient: public sigc::trackable, protected HttpParsing::RequestState
{
protected:

    std::shared_ptr<uvw::TcpHandle> client_conn;

    llhttp_settings_t parser_settings;
    llhttp_t *parser;

    //(Re)initializes the llhttp parser for the next request of this
    //connection. The parser->data contract (RequestState subobject pointer,
    //see HttpParsing::bindParser) lives there and only there: used by the
    //constructor and by WebSocket after every handled http request, never
    //hand-rolled at a call site.
    void reinitParser()
    {
        HttpParsing::bindParser(parser, parser_settings, *this);
    }

    int proto_ver;

    void CloseConnection();

    //set to true if connection need to be closed after data has been sent
    bool conn_close = false; //by default we keep-alive connection unless client asks us to close it
    int data_size = 0; //data count remaining

    //special case when config was written, we need to restart calaos_server
    bool need_restart = false;

    //timer to close the connection after data has been written
    Timer *closeTimer = nullptr;

    //timer closing a connection that never sends a complete request
    Timer *readTimeout = nullptr;

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

public:
    HttpClient(const std::shared_ptr<uvw::TcpHandle> &client);
    virtual ~HttpClient();

    enum { APINONE = 0, API_HTTP, API_WEBSOCKET, API_REMOTE_UI_WEBSOCKET };

    /* Called by JsonApiServer whenever data has been written to client */
    virtual void DataWritten(int size);

    string buildHttpResponse(string code, Params &headers, string body);
    string buildHttpResponseFromFile(string code, Params &headers, string fileName);

    //The TCP peer as a bare literal: no brackets, no port, no zone, and an
    //IPv4-mapped peer unmapped, so one client keys one bucket whether the
    //server was bound to an IPv4 address or to a dual-stack one. "unknown"
    //when the handle has no peer to read.
    //Virtual so that a test can inject a peer address: the real one comes from
    //a connected socket. No production subclass overrides it.
    virtual string getClientIp() const;

    /* Client identity used by every per-client security decision of this
     * connection: the per-IP connection cap (trackPerIpCap) and the login
     * throttle of both JSON API transports (JsonApiHandlerWS::clientIp,
     * JsonApiHandlerHttp::clientIp).
     *
     * NOT the same thing as getClientIp(), and the difference is the whole
     * point: getClientIp() answers the TCP peer, which behind haproxy - the
     * deployment of calaos-os - is the PROXY, identical for every user. Keying
     * anything per-client on it collapses everybody into one bucket.
     *
     * This answers TransportLimits::effectiveClientIp() of the X-Forwarded-For
     * line of the request being served: the LAST comma entry, the one the
     * trusted proxy hop wrote. Everything before it is client supplied and
     * ignored. Falls back to the TCP peer when the header is absent.
     *
     * TRUST MODEL - READ THIS BEFORE ADDING A CALLER. The X-Forwarded-For
     * line is believed ONLY when the TCP peer is the loopback, because that is
     * where the proxy is (haproxy backend: 127.0.0.1:5454); port 5454 also
     * answers directly from the LAN by default, and such a client speaks only
     * about itself, so its header is dropped and the peer address answers.
     * Two limits follow: code running on this machine is loopback and can
     * still choose its bucket, and a reverse proxy on ANOTHER machine has its
     * header ignored - supporting that needs a `trusted_proxies` option.
     */
    string getEffectiveClientIp() const
    {
        const auto it = request_headers.find("x-forwarded-for");
        return TransportLimits::effectiveClientIp(
            it != request_headers.end()? it->second: string(),
            getClientIp());
    }

    void sendToClient(string res);
    void setNeedRestart(bool e) { need_restart = e; }

    // Expose request headers for handlers that need authentication
    const unordered_map<string, string> &getRequestHeaders() const { return request_headers; }

    //Lifetime token for async callbacks capturing this connection: check
    //expired() before touching the object (see the `alive` member).
    std::weak_ptr<bool> aliveToken() const { return alive; }
};

#endif
