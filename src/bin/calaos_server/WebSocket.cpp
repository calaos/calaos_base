/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include "JsonApiCodes.h"
#include "SHA1.h"
#include "hef_uri_syntax.h"

using namespace Calaos;

WebSocket::WebSocket(Ecore_Con_Client *cl):
    JsonApiClient(cl)
{
}

WebSocket::~WebSocket()
{
}

void WebSocket::ProcessData(string data)
{
    JsonApiClient::ProcessData(data);

    //handle HTTP request now
    if (!isWebsocket)
    {
        if (parse_done)
        {
            //init parser again
            http_parser_init(parser, HTTP_REQUEST);
            parser->data = this;
        }

        handleRequest();
        return;
    }

    //Now we handle websocket here
    if (status == WSConnecting)
        processHandshake();
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
    if (req_url.getPath() != "/api/v2")
    {
        cWarningDom("websocket") << "wrong path: " << req_url.getPath();
        return false;
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
