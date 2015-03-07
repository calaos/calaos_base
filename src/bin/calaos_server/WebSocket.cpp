/******************************************************************************
 **  Copyright (c) 2006-2015, Calaos. All Rights Reserved.
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
#include "WebSocket.h"
#include "CalaosConfig.h"
#include <Ecore.h>
#include "HttpCodes.h"
#include "SHA1.h"
#include "hef_uri_syntax.h"

using namespace Calaos;

const uint64_t MAX_MESSAGE_SIZE_IN_BYTES = INT_MAX - 1;
const uint64_t FRAME_SIZE_IN_BYTES = 512 * 512 * 2;

WebSocket::WebSocket(Ecore_Con_Client *cl):
    HttpClient(cl)
{
    reset();
}

WebSocket::~WebSocket()
{
    delete closeTimeout;
    cDebugDom("websocket") << this;
}

void WebSocket::ProcessData(string data)
{
    cDebugDom("websocket") << "Process new data " << data.size();

    int http_status = HTTP_PROCESS_WEBSOCKET;
    if (!isWebsocket)
        http_status = processHeaders(data);

    switch (http_status)
    {
    case HTTP_PROCESS_HTTP:
    {
        if (parse_done)
        {
            //init parser again
            http_parser_init(parser, HTTP_REQUEST);
            parser->data = this;

            handleJsonRequest();
        }
        break;
    }
    case HTTP_PROCESS_WEBSOCKET:
    {
        //Now we handle websocket here
        if (status == WSConnecting)
            processHandshake();
        else if (status == WSOpened ||
            status == WSClosing) //Waiting for the closing handshake
        processFrame(data);
    }
    case HTTP_PROCESS_MOREDATA:
    case HTTP_PROCESS_DONE:
    default: break;
    }
}

void WebSocket::processHandshake()
{
    if (!checkHandshakeRequest())
    {
        cWarningDom("websocket") << "Websocket Handshake is not formated correctly, aborting.";
        Params headers;
        headers.Add("Connection", "close");
        headers.Add("Content-Type", "text/html");
        string res = buildHttpResponse(HTTP_400, headers, HTTP_400_BODY);
        sendToClient(res);
    }

    status = WSOpened;
    currentFrame.clear();

    cDebugDom("websocket") << "Connection switched to websocket";
}

bool WebSocket::checkHandshakeRequest()
{
    //Check client headers, if it has all required by RFC
    //http://tools.ietf.org/html/rfc6455#section-4.2.1

    //1. check HTTP 1.1 version at least
    if (parser->http_major < 1 || (parser->http_major == 1 && parser->http_minor < 1))
    {
        cWarningDom("websocket") << "wrong HTTP version";
        return false;
    }

    //1. Request should be GET
    if (request_method != HTTP_GET)
    {
        cWarningDom("websocket") << "not a GET HTTP";
        return false;
    }

    //Check if path is our API
    hef::HfURISyntax req_url("http://0.0.0.0" + parse_url);
    if (req_url.getPath() != "/api/v3" &&
        req_url.getPath() != "/echo")
    {
        cWarningDom("websocket") << "wrong path: " << req_url.getPath();
        return false;
    }

    echoMode = req_url.getPath() == "/echo";

    proto_ver = APINONE;
    if (req_url.getPath() == "/api/v3") proto_ver = APIV3;

    if (!jsonApi)
    {
        if (proto_ver == APIV3)
            jsonApi = new JsonApiV3(this);
        else
        {
            cWarningDom("network") << "API version not implemented";
            return false;
        }

        jsonApi->sendData.connect([=](const string &data)
        {
            sendTextMessage(data);
        });
        jsonApi->closeConnection.connect([=](int c, const string &r)
        {
            sendCloseFrame(static_cast<uint16_t>(c), r);
        });
    }

    //2. Must contains non empty Host
    if (request_headers.find("host") == request_headers.end() ||
        request_headers["host"].empty())
    {
        cWarningDom("websocket") << "malformed host";
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
        Utils::str_to_lower(request_headers["connection"]) != "upgrade")
    {
        cWarningDom("websocket") << "wrong connection field";
        return false;
    }

    //5. check sec-websocket-key
    string reqkey= Utils::Base64_decode(request_headers["sec-websocket-key"]);
    if (reqkey.size() != 16)
    {
        cWarningDom("websocket") << "wrong Sec-Websocket-Key";
        return false;
    }

    if (request_headers["sec-websocket-version"] != "13")
    {
        cWarningDom("websocket") << "wrong Sec-Websocket-Version";
        return false;
    }

    //The handshake is correct, build our response
    Params headers;
    headers.Add("Upgrade", "websocket");
    headers.Add("Connection", "Upgrade");
    headers.Add("Access-Control-Allow-Credentials", "false");
    headers.Add("Access-Control-Allow-Methods", "GET");
    headers.Add("Access-Control-Allow-Headers", "content-type");
    headers.Add("Access-Control-Allow-Origin", "*");

    //calculate key
    string key = request_headers["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    CSHA1 sha1;
    sha1.Update((const unsigned char *)key.c_str(), key.length());
    sha1.Final();

    string s;
    sha1.ReportHashStl(s);
    uint8_t message_digest[20];
    sha1.GetHash(message_digest);

    cDebugDom("websocket") << "key : " << key;
    cDebugDom("websocket") << "SHA1 : " << s;

    string encoded_key = Utils::Base64_encode(message_digest, 20);
    cDebugDom("websocket") << "Sec-Websocket-Accept : " << encoded_key;
    headers.Add("Sec-Websocket-Accept", encoded_key);

    //build response
    stringstream res;
    //HTTP code
    res << HTTP_WS_HANDSHAKE << "\r\n";

    //headers
    for (int i = 0;i < headers.size();i++)
    {
        string _key, _value;
        headers.get_item(i, _key, _value);
        res << _key << ": " << _value << "\r\n";
    }
    res << "\r\n";

    sendToClient(res.str());

    return true;
}

void WebSocket::reset()
{
    currentFrame.clear();
    isfragmented = false;
    currentData.clear();
    currentOpcode = 0;
}

void WebSocket::processFrame(const string &data)
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
                    sendCloseFrame(CloseCodeProtocolError, err);
                }
                if (isfragmented && currentFrame.isDataFrame() && !currentFrame.isContinuationFrame())
                {
                    reset();
                    string err = "When fragmented, all data frames must have continuation opcode";
                    cWarningDom("websocket") << err;

                    //Send close frame and close connection
                    sendCloseFrame(CloseCodeProtocolError, err);
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
                    sendCloseFrame(CloseCodeTooMuchData, err.str());
                }

                currentData.append(currentFrame.getPayload());

                if (currentFrame.isFinalFrame())
                {
                    cDebugDom("websocket") << "Received " << (currentOpcode == WebSocketFrame::OpCodeText?"text":"binary") << " message of size " << currentData.size();

                    if (currentOpcode == WebSocketFrame::OpCodeText)
                        textMessageReceived.emit(currentData);
                    else
                        binaryMessageReceived.emit(currentData);

                    if (!echoMode && proto_ver == APIV3 && jsonApi)
                        jsonApi->processApi(currentData);

                    if (echoMode)
                    {
                        if (currentOpcode == WebSocketFrame::OpCodeText)
                            sendTextMessage(currentData);
                        else
                            sendBinaryMessage(currentData);
                    }

                    if (currentData.size() == 0)
                    {
                        string err = "A frame with 0 length payload is not accepted";
                        cWarningDom("websocket") << err;

                        //Send close frame and close connection
                        sendCloseFrame(CloseCodeNormal, err);
                    }

                    reset();
                }
            }

            currentFrame.clear();
        }
        else if (currentFrame.getCloseCode() != CloseCodeNormal)
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

void WebSocket::sendCloseFrame(uint16_t code, const string &reason, bool forceClose)
{
    if (!isWebsocket) return;

    //Already in closing state, do not send another close frame
    if (status == WSClosing) return;

    cInfoDom("websocket") << "Initiate close: " << code << " - " << reason;

    string payload;
    payload.push_back(static_cast<char>(code >> 8));
    payload.push_back(static_cast<char>(code));
    payload.append(reason);

    string frame = WebSocketFrame::makeFrame(WebSocketFrame::OpCodeClose,
                                             payload,
                                             true);

    cDebugDom("network") << "Sending " << frame.length() << " bytes, data_size = " << data_size;

    data_size += frame.size();
    if (!client_conn || ecore_con_client_send(client_conn, frame.c_str(), frame.size()) == 0)
        cCriticalDom("network") << "Error sending data !";
    else
        ecore_con_client_flush(client_conn);

    //start a timeout to wait for a close frame from the client
    if (!closeReceived && !forceClose)
    {
        closeTimeout = new EcoreTimer(10.0, [=]()
        {
            cDebugDom("websocket") << "Waiting too long for the close frame from the client, aborting.";
            status = WSClosed;
            CloseConnection();
            DELETE_NULL(closeTimeout);
        });

        status = WSClosing;
    }
    else
    {
        //Client initiated closing
        status = WSClosed;
        CloseConnection();
    }

    websocketDisconnected.emit();
}

void WebSocket::processControlFrame()
{
    if (currentFrame.isPingFrame())
    {
        if (status == WSClosing) return;

        cDebugDom("websocket") << "Received a PING, sending PONG back";

        //Send back a pong frame
        string frame = WebSocketFrame::makeFrame(WebSocketFrame::OpCodePong,
                                                 currentFrame.getPayload(),
                                                 true);
        sendToClient(frame);
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
            code = CloseCodeNormal;
            close_reason = "malformed close frame";
        }

        if (!checkCloseStatusCode(code))
        {
            code = CloseCodeProtocolError;
            close_reason = "Wrong close status code";
        }

        if (status == WSOpened)
            sendCloseFrame(code, close_reason);
        else
        {
            CloseConnection();
            status = WSClosed;
        }
    }
}

void WebSocket::sendPing(const string &data)
{
    if (!isWebsocket) return;

    ping_time = ecore_time_get();

    string frame = WebSocketFrame::makeFrame(WebSocketFrame::OpCodePing,
                                             data,
                                             true);
    sendToClient(frame);
}

void WebSocket::sendTextMessage(const string &data)
{
    if (!isWebsocket) return;
    sendFrameData(data, false);
}

void WebSocket::sendBinaryMessage(const string &data)
{
    if (!isWebsocket) return;
    sendFrameData(data, true);
}

void WebSocket::sendFrameData(const string &data, bool isbinary)
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
                                              lastframe);
            header_size += frame.size() - sz;
        }
        else
        {
            frame = WebSocketFrame::makeFrame(firstframe?
                                                  isbinary?WebSocketFrame::OpCodeBinary:
                                                           WebSocketFrame::OpCodeText
                                                         :WebSocketFrame::OpCodeContinue,
                                              string(),
                                              lastframe);
            header_size += frame.size();
        }

        //send frame
        data_size += frame.size();
        uint n;
        if (!client_conn ||
            (n = ecore_con_client_send(client_conn, frame.c_str(), frame.size())) == 0)
        {
            cCriticalDom("network") << "Error sending data !";
            CloseConnection();
            status = WSClosed;
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
        status = WSClosed;
    }
}

bool WebSocket::checkCloseStatusCode(uint16_t code)
{
    //range 0-999 is not used
    if (code < CloseCodeNormal)
        return false;

    //range 1000-2999 reserved for valid websocket codes
    if (code <= 2999)
    {
        if (code == CloseCodeReserved1004 ||
            code == CloseCodeMissingStatusCode ||
            code == CloseCodeAbnormalDisconnection ||
            code > CloseCodeBadOperation)
            return false;

        //CloseCodeTlsHandshakeFailed should never be used and is an error
    }

    //range 3000-3999 reserved for libraries, framwork ...
    //range 4000-4999 reserved for private use

    return true;
}
