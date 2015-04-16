/******************************************************************************
 **  Copyright (c) 2007-2015, Calaos. All Rights Reserved.
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
#include "WebSocketClient.h"
#include "hef_uri_syntax.h"
#include "SHA1.h"

#define WEBSOCKET_HANDSHAKE \
    "GET %1 HTTP/1.1\r\n" \
    "Host: %2:%3\r\n" \
    "Upgrade: websocket\r\n" \
    "Connection: Upgrade\r\n" \
    "Sec-WebSocket-Key: %4\r\n" \
    "Sec-WebSocket-Version: 13\r\n" \
    "\r\n"

const uint64_t MAX_MESSAGE_SIZE_IN_BYTES = INT_MAX - 1;
const uint64_t FRAME_SIZE_IN_BYTES = 512 * 512 * 2;

int _parser_begin(http_parser *parser)
{
    WebSocketClient *client = reinterpret_cast<WebSocketClient *>(parser->data);

    //reset status flags to parse another request on the same connection
    client->parse_done = false;
    client->has_field = false;
    client->has_value = false;
    client->hfield.clear();
    client->hvalue.clear();
    client->bodymessage.clear();
    client->parse_url.clear();
    client->request_headers.clear();

    return 0;
}

int _parser_header_field(http_parser *parser, const char *at, size_t length)
{
    WebSocketClient *client = reinterpret_cast<WebSocketClient *>(parser->data);

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
    WebSocketClient *client = reinterpret_cast<WebSocketClient *>(parser->data);

    if (!client->has_value)
        client->has_value = true;

    client->hvalue.append(at, length);

    return 0;
}

int _parser_headers_complete(http_parser *parser)
{
    WebSocketClient *client = reinterpret_cast<WebSocketClient *>(parser->data);

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
    WebSocketClient *client = reinterpret_cast<WebSocketClient *>(parser->data);

    client->parse_url.append(at, length);

    return 0;
}

int _parser_message_complete(http_parser *parser)
{
    WebSocketClient *client = reinterpret_cast<WebSocketClient *>(parser->data);

    client->parse_done = true;

    return 0;
}

int _parser_body_complete(http_parser* parser, const char *at, size_t length)
{
    WebSocketClient *client = reinterpret_cast<WebSocketClient *>(parser->data);

    client->bodymessage.append(at, length);

    return 0;
}

Eina_Bool WebSocketClient_con_add(void *data, int type, void *event)
{
    VAR_UNUSED(type);
    WebSocketClient *w = reinterpret_cast<WebSocketClient *>(data);
    Ecore_Con_Event_Server_Add *ev = reinterpret_cast<Ecore_Con_Event_Server_Add *>(event);

    if (!ev || !w ||
        w->ecoreServer != ev->server)
        return ECORE_CALLBACK_PASS_ON;

    ecore_con_server_send(w->ecoreServer, w->handshakeHeader.c_str(), w->handshakeHeader.size());
    w->data_size += w->handshakeHeader.size();

    return ECORE_CALLBACK_DONE;
}

Eina_Bool WebSocketClient_con_del(void *data, int type, void *event)
{
    VAR_UNUSED(type);
    WebSocketClient *w = reinterpret_cast<WebSocketClient *>(data);
    Ecore_Con_Event_Server_Del *ev = reinterpret_cast<Ecore_Con_Event_Server_Del *>(event);

    if (!ev || !w ||
        w->ecoreServer != ev->server)
        return ECORE_CALLBACK_PASS_ON;

    w->ecoreServer = nullptr; //invalidate ptr
    w->status = WebSocketClient::WSClosed;
    w->data_size = 0;
    w->reset();
    w->websocketDisconnected.emit();

    return ECORE_CALLBACK_DONE;
}

Eina_Bool WebSocketClient_con_data(void *data, int type, void *event)
{
    VAR_UNUSED(type);
    WebSocketClient *w = reinterpret_cast<WebSocketClient *>(data);
    Ecore_Con_Event_Server_Data *ev = reinterpret_cast<Ecore_Con_Event_Server_Data *>(event);

    if (!ev || !w ||
        w->ecoreServer != ev->server)
        return ECORE_CALLBACK_PASS_ON;

    string d((char *)ev->data, ev->size);
    if (w->status == WebSocketClient::WSConnecting)
    {
        if (!w->gotNewDataHandshake(d))
            w->CloseConnection();
    }
    else if (w->status == WebSocketClient::WSOpened ||
             w->status == WebSocketClient::WSClosing)
        w->gotNewData(d);


    return ECORE_CALLBACK_DONE;
}

Eina_Bool WebSocketClient_con_error(void *data, int type, void *event)
{
    VAR_UNUSED(type);
    WebSocketClient *w = reinterpret_cast<WebSocketClient *>(data);
    Ecore_Con_Event_Server_Error *ev = reinterpret_cast<Ecore_Con_Event_Server_Error *>(event);

    if (!ev || !w ||
        w->ecoreServer != ev->server)
        return ECORE_CALLBACK_PASS_ON;

    cErrorDom("websocket") << ev->error;

    //ecore server is deleted, invalidate the pointer
    w->ecoreServer = nullptr;
    w->status = WebSocketClient::WSClosed;
    w->data_size = 0;
    w->reset();
    w->websocketDisconnected.emit();

    return ECORE_CALLBACK_DONE;
}

Eina_Bool WebSocketClient_con_data_written(void *data, int type, void *event)
{
    VAR_UNUSED(type);
    WebSocketClient *w = reinterpret_cast<WebSocketClient *>(data);
    Ecore_Con_Event_Server_Write *ev = reinterpret_cast<Ecore_Con_Event_Server_Write *>(event);

    if (!ev || !w ||
        w->ecoreServer != ev->server)
        return ECORE_CALLBACK_PASS_ON;

    w->data_size -= ev->size;
    cDebugDom("websocket") << ev->size << " bytes has been written, " << w->data_size << " bytes remaining";

    return ECORE_CALLBACK_DONE;
}

WebSocketClient::WebSocketClient()
{
    handler_add = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_ADD, WebSocketClient_con_add, this);
    handler_del = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DEL, WebSocketClient_con_del, this);
    handler_data = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DATA, WebSocketClient_con_data, this);
    handler_error = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_ERROR, WebSocketClient_con_error, this);
    handler_written = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_WRITE, WebSocketClient_con_data_written, this);

    //set up callbacks for the parser
    parser_settings.on_message_begin = _parser_begin;
    parser_settings.on_url = _parser_url;
    parser_settings.on_header_field = _parser_header_field;
    parser_settings.on_header_value = _parser_header_value;
    parser_settings.on_headers_complete = _parser_headers_complete;
    parser_settings.on_body = _parser_body_complete;
    parser_settings.on_message_complete = _parser_message_complete;

    parser = (http_parser *)calloc(1, sizeof(http_parser));
    http_parser_init(parser, HTTP_RESPONSE);
    parser->data = this;
}

WebSocketClient::~WebSocketClient()
{
    delete closeTimeout;

    ecore_event_handler_del(handler_add);
    ecore_event_handler_del(handler_del);
    ecore_event_handler_del(handler_data);
    ecore_event_handler_del(handler_error);

    DELETE_NULL_FUNC(ecore_con_server_del, ecoreServer);
}

void WebSocketClient::openConnection(string url)
{
    if (status != WSClosed)
        return;

    status = WSConnecting;
    wsUrl = url;

    hef::HfURISyntax req_url(url);

    cDebugDom("websocket") << "Connecting to " << req_url.getHost() << " port:" << req_url.getPort();

    ecoreServer = ecore_con_server_connect(ECORE_CON_REMOTE_TCP, req_url.getHost().c_str(), req_url.getPort(), this);
    ecore_con_server_data_set(ecoreServer, this);

    sec_key.clear();

    //generate key
    srand(time(NULL));
    for (int i = 0;i < 4; i++)
    {
        const u_int32_t tmp = u_int32_t((double(rand()) / RAND_MAX) * std::numeric_limits<u_int32_t>::max());
        sec_key.append(static_cast<const char *>(static_cast<const void *>(&tmp)), sizeof(u_int32_t));
    }

    sec_key = Utils::Base64_encode(sec_key);

    handshakeHeader = WEBSOCKET_HANDSHAKE;
    Utils::replace_str(handshakeHeader, "%1", req_url.getPathAndQuery());
    Utils::replace_str(handshakeHeader, "%2", req_url.getHost());
    Utils::replace_str(handshakeHeader, "%3", req_url.getPortAsString());
    Utils::replace_str(handshakeHeader, "%4", sec_key);
}

bool WebSocketClient::gotNewDataHandshake(const string &request)
{
    size_t nparsed;

    nparsed = http_parser_execute(parser, &parser_settings, request.c_str(), request.size());

    if (parser->upgrade)
    {
        if (nparsed != request.size())
        {
            bodymessage = request;
            bodymessage.erase(0, nparsed);
        }
    }
    else if (nparsed != request.size())
    {
        /* Handle error. Usually just close the connection. */
        cWarningDom("websocket") << "nparsed != request.size()  " << nparsed << " !=" << request.size();
        return false;
    }

    if (!parse_done)
        return true; //need more data

    //Check handshake response
    //check HTTP 1.1 version at least
    if (parser->http_major < 1 || (parser->http_major == 1 && parser->http_minor < 1))
    {
        cWarningDom("websocket") << "wrong HTTP version";
        return false;
    }

    //Status code should be 101 (Switching protocol)
    if (parser->status_code != 101)
    {
        cWarningDom("websocket") << "Websocket upgrade failes, code: " << Utils::to_string(parser->status_code);
        return false;
    }

    //3. upgrade header must contains websocket
    if (request_headers.find("upgrade") == request_headers.end() ||
        Utils::str_to_lower(request_headers["upgrade"]) != "websocket")
    {
        cWarningDom("websocket") << "wrong upgrade field";
        return false;
    }

    //4. connection header must contains upgrade
    if (request_headers.find("connection") == request_headers.end() ||
        !Utils::strContains(request_headers["connection"], "upgrade", Utils::CaseInsensitive))
    {
        cWarningDom("websocket") << "wrong connection field";
        return false;
    }

    //5. check sec-websocket-key
    string reqkey = Utils::Base64_decode(request_headers["sec-websocket-accept"]);
    if (reqkey.size() != 20)
    {
        cWarningDom("websocket") << "wrong Sec-Websocket-Accept: " << reqkey.size();
        return false;
    }

    if (request_headers["sec-websocket-version"] != "13" &&
        request_headers["sec-websocket-version"] != "")
    {
        cWarningDom("websocket") << "wrong Sec-Websocket-Version: " << request_headers["sec-websocket-version"];
        return false;
    }

    //calculate key
    string key = sec_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    CSHA1 sha1;
    sha1.Update((const unsigned char *)key.c_str(), key.length());
    sha1.Final();

    string s;
    sha1.ReportHashStl(s);
    uint8_t message_digest[20];
    sha1.GetHash(message_digest);

    cDebugDom("websocket") << "sec_key : " << sec_key;
    cDebugDom("websocket") << "key : " << key;
    cDebugDom("websocket") << "SHA1 : " << s;

    string encoded_key = Utils::Base64_encode(message_digest, 20);
    cDebugDom("websocket") << "Sec-Websocket-Accept : " << encoded_key;

    if (request_headers["sec-websocket-accept"] != encoded_key)
    {
        cWarningDom("websocket") << "wrong Sec-Websocket-Accept, does not match client key";
        return false;
    }

    status = WSOpened;
    currentFrame.clear();

    websocketConnected.emit();

    cInfoDom("websocket") << "websocket opened successfully to " << wsUrl;

    if (bodymessage.size() > 0)
        gotNewData(bodymessage);

    return true;
}

void WebSocketClient::reset()
{
    currentFrame.clear();
    isfragmented = false;
    currentData.clear();
    currentOpcode = 0;
}

void WebSocketClient::gotNewData(const string &data)
{
    cDebugDom("websocket") << "Processing frame data " << data.size();

    recv_buffer += data;

    while (currentFrame.processFrameData(recv_buffer))
    {
        if (currentFrame.isValid())
        {
            cDebugDom("websocket") << "Got a new frame: " << currentFrame.toString();

            if (currentFrame.isControlFrame())
                processControlFrame();
            else
            {
                if (currentFrame.isContinuationFrame() && !isfragmented)
                {
                    reset();
                    string err = "Received a continuation frame but no fragmented start frame";
                    cWarningDom("websocket") << err;

                    //Send close frame and close connection
                    sendCloseFrame(WebSocketFrame::CloseCodeProtocolError, err);
                }
                if (isfragmented && currentFrame.isDataFrame() && !currentFrame.isContinuationFrame())
                {
                    reset();
                    string err = "When fragmented, all data frames must have continuation opcode";
                    cWarningDom("websocket") << err;

                    //Send close frame and close connection
                    sendCloseFrame(WebSocketFrame::CloseCodeProtocolError, err);
                }

                if (!currentFrame.isContinuationFrame())
                {
                    currentOpcode = currentFrame.getOpcode();
                    isfragmented = !currentFrame.isFinalFrame();
                }

                if (currentData.size() + currentFrame.getPayload().size() > MAX_MESSAGE_SIZE_IN_BYTES)
                {
                    reset();
                    stringstream err;
                    err << "Message exceeds size of " << MAX_MESSAGE_SIZE_IN_BYTES << " bytes";
                    cWarningDom("websocket") << err.str();

                    //Send close frame and close connection
                    sendCloseFrame(WebSocketFrame::CloseCodeTooMuchData, err.str());
                }

                currentData.append(currentFrame.getPayload());

                if (currentFrame.isFinalFrame())
                {
                    cDebugDom("websocket") << "Received " << (currentOpcode == WebSocketFrame::OpCodeText?"text":"binary") << " message of size " << currentData.size();

                    if (currentOpcode == WebSocketFrame::OpCodeText)
                        textMessageReceived.emit(currentData);
                    else
                        binaryMessageReceived.emit(currentData);

                    if (currentData.size() == 0)
                    {
                        string err = "A frame with 0 length payload is not accepted";
                        cWarningDom("websocket") << err;

                        ecore_con_server_flush(ecoreServer);

                        //Send close frame and close connection
                        sendCloseFrame(WebSocketFrame::CloseCodeNormal, err);
                    }

                    reset();
                }
            }

            currentFrame.clear();
        }
        else if (currentFrame.getCloseCode() != WebSocketFrame::CloseCodeNormal)
        {
            cWarningDom("websocket") << "Error in websocket handling: " << currentFrame.getCloseReason();

            //handle wrong close frames errors, test 7.3.6
            if (currentFrame.isCloseFrame())
                closeReceived = true; //make the connection to force close and don't wait for close handshake

            //Send close frame and close connection
            sendCloseFrame(currentFrame.getCloseCode(), currentFrame.getCloseReason());

            return;
        }
    }
}

void WebSocketClient::sendCloseFrame(uint16_t code, const string &reason, bool forceClose)
{
    //Already in closing state, do not send another close frame
    if (status == WSClosing) return;

    cInfoDom("websocket") << "Initiate close: " << code << " - " << reason;

    string payload;
    payload.push_back(static_cast<char>(code >> 8));
    payload.push_back(static_cast<char>(code));
    payload.append(reason);

    uint32_t maskingKey = u_int32_t((double(rand()) / RAND_MAX) * std::numeric_limits<u_int32_t>::max());

    string frame = WebSocketFrame::makeFrame(WebSocketFrame::OpCodeClose,
                                             payload,
                                             true,
                                             maskingKey);

    cDebugDom("network") << "Sending " << frame.length() << " bytes, data_size = " << data_size;

    data_size += frame.size();
    if (!ecoreServer || ecore_con_server_send(ecoreServer, frame.c_str(), frame.size()) == 0)
        cCriticalDom("network") << "Error sending data !";
    else
        ecore_con_server_flush(ecoreServer);

    //start a timeout to wait for a close frame from the client
    if (!closeReceived && !forceClose)
    {
        closeTimeout = new EcoreTimer(10.0, [=]()
        {
            cDebugDom("websocket") << "Waiting too long for the close frame from the client, aborting.";
            CloseConnection();
            DELETE_NULL(closeTimeout);
        });

        status = WSClosing;
    }
    else
    {
        //Client initiated closing
        CloseConnection();
    }

}

void WebSocketClient::processControlFrame()
{
    if (currentFrame.isPingFrame())
    {
        if (status == WSClosing) return;

        cDebugDom("websocket") << "Received a PING, sending PONG back";

        uint32_t maskingKey = u_int32_t((double(rand()) / RAND_MAX) * std::numeric_limits<u_int32_t>::max());

        //Send back a pong frame
        string frame = WebSocketFrame::makeFrame(WebSocketFrame::OpCodePong,
                                                 currentFrame.getPayload(),
                                                 true,
                                                 maskingKey);
        sendToServer(frame);
    }
    else if (currentFrame.isPongFrame())
    {
        if (status == WSClosing) return;

        double elapsed = ecore_time_get() - ping_time;
        cInfoDom("websocket") << "Received a PONG back in " << Utils::time2string_digit(elapsed, elapsed * 1000.);
    }
    else if (currentFrame.isCloseFrame())
    {
        //Read close code and close reason from payload
        uint16_t code;
        string close_reason;
        currentFrame.parseCloseCodeReason(code, close_reason);

        cInfoDom("websocket") << "Close frame received, code:" << code << " reason: " << close_reason;

        DELETE_NULL(closeTimeout);
        closeReceived = true;
        if (currentFrame.hasError()) //in case Close frame has errors, testsuite 7.3.1
        {
            code = WebSocketFrame::CloseCodeNormal;
            close_reason = "malformed close frame";
        }

        if (!checkCloseStatusCode(code))
        {
            code = WebSocketFrame::CloseCodeProtocolError;
            close_reason = "Wrong close status code";
        }

        if (status == WSOpened)
            sendCloseFrame(code, close_reason);
        else
            CloseConnection();
    }
}

void WebSocketClient::sendPing(const string &data)
{
    if (!status == WSOpened) return;
    ping_time = ecore_time_get();

    uint32_t maskingKey = u_int32_t((double(rand()) / RAND_MAX) * std::numeric_limits<u_int32_t>::max());

    string frame = WebSocketFrame::makeFrame(WebSocketFrame::OpCodePing,
                                             data,
                                             true,
                                             maskingKey);
    sendToServer(frame);
}

void WebSocketClient::sendTextMessage(const string &data)
{
    sendFrameData(data, false);
}

void WebSocketClient::sendBinaryMessage(const string &data)
{
    sendFrameData(data, true);
}

void WebSocketClient::sendFrameData(const string &data, bool isbinary)
{
    if (status != WSOpened)
    {
        cErrorDom("websocket") << "Can't send data, websocket is not connected";
        return;
    }

    cDebugDom("websocket") << "Sending " << (isbinary?"binary":"text") << " frame, payload size: " << data.size();

    int numframes = data.size() / FRAME_SIZE_IN_BYTES;
    if (data.size() % FRAME_SIZE_IN_BYTES || numframes == 0)
        numframes++;

    uint64_t current = 0;
    uint64_t byteswritten = 0;
    uint64_t bytesleft = data.size();
    int header_size = 0;

    cDebugDom("websocket") << "Sending " << numframes << " frames";

    for (int i = 0;i < numframes;i++)
    {
        uint32_t maskingKey = u_int32_t((double(rand()) / RAND_MAX) * std::numeric_limits<u_int32_t>::max());

        bool lastframe = (i == (numframes - 1));
        bool firstframe = (i == 0);

        uint64_t sz = bytesleft < FRAME_SIZE_IN_BYTES?bytesleft:FRAME_SIZE_IN_BYTES;

        string frame;

        if (sz > 0)
        {
            frame = WebSocketFrame::makeFrame(firstframe?
                                                  isbinary?WebSocketFrame::OpCodeBinary:
                                                           WebSocketFrame::OpCodeText
                                                         :WebSocketFrame::OpCodeContinue,
                                              data.substr(current, sz),
                                              lastframe,
                                              maskingKey);
            header_size += frame.size() - sz;
        }
        else
        {
            frame = WebSocketFrame::makeFrame(firstframe?
                                                  isbinary?WebSocketFrame::OpCodeBinary:
                                                           WebSocketFrame::OpCodeText
                                                         :WebSocketFrame::OpCodeContinue,
                                              string(),
                                              lastframe,
                                              maskingKey);
            header_size += frame.size();
        }

        //send frame
        data_size += frame.size();
        uint n;
        if (!ecoreServer ||
            (n = ecore_con_server_send(ecoreServer, frame.c_str(), frame.size())) == 0)
        {
            cCriticalDom("network") << "Error sending data !";
            CloseConnection();
            return;
        }
        if (n != frame.size())
            cWarningDom("websocket") << "All data not sent! framesize: " << frame.size() << " sent: " << n;
        byteswritten += n;
        cDebugDom("websocket") << "Data written: " << n << " byteswritten: " << byteswritten;

        current += sz;
        bytesleft -= sz;
    }

    if (byteswritten != (data.size() + header_size))
    {
        cErrorDom("websocket") << "Error, bytes written " << byteswritten << " != " << (data.size() + header_size);
        CloseConnection();
    }
}

bool WebSocketClient::checkCloseStatusCode(uint16_t code)
{
    //range 0-999 is not used
    if (code < WebSocketFrame::CloseCodeNormal)
        return false;

    //range 1000-2999 reserved for valid websocket codes
    if (code <= 2999)
    {
        if (code == WebSocketFrame::CloseCodeReserved1004 ||
            code == WebSocketFrame::CloseCodeMissingStatusCode ||
            code == WebSocketFrame::CloseCodeAbnormalDisconnection ||
            code > WebSocketFrame::CloseCodeBadOperation)
            return false;

        //CloseCodeTlsHandshakeFailed should never be used and is an error
    }

    //range 3000-3999 reserved for libraries, framwork ...
    //range 4000-4999 reserved for private use

    return true;
}

void WebSocketClient::CloseConnection()
{
    cDebugDom("websocket") << "Closing connection";
    status = WSClosed;
    if (ecoreServer)
    {
        ecore_con_server_del(ecoreServer);
        ecoreServer = nullptr;
        websocketDisconnected.emit();
    }
}

void WebSocketClient::sendToServer(string res)
{
    data_size += res.length();

    cDebugDom("websocket") << "Sending " << res.length() << " bytes, data_size = " << data_size;

    if (!ecoreServer || ecore_con_server_send(ecoreServer, res.c_str(), res.length()) == 0)
    {
        cCriticalDom("websocket")
                << "Error sending data ! Closing connection.";

        CloseConnection();
    }
}
