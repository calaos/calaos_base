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
#ifndef WebSocketClient_H
#define WebSocketClient_H

#include <Ecore_Con.h>
#include "Utils.h"
#include "WebSocketFrame.h"
#include "EcoreTimer.h"
#include "http_parser.h"

class WebSocketClient: public sigc::trackable
{
public:
    WebSocketClient();
    virtual ~WebSocketClient();

    sigc::signal<void, const string &> textMessageReceived;
    sigc::signal<void, const string &> binaryMessageReceived;
    sigc::signal<void> websocketDisconnected;
    sigc::signal<void> websocketConnected;

    void openConnection(string url);

    void sendPing(const string &data);
    void sendCloseFrame(uint16_t code = WebSocketFrame::CloseCodeNormal, const string &reason = string(), bool forceClose = false);

    void sendTextMessage(const string &data);
    void sendBinaryMessage(const string &data);

private:
    enum { WSConnecting, WSOpened, WSClosing, WSClosed };
    int status = WSClosed;

    string wsUrl;

    string recv_buffer;
    WebSocketFrame currentFrame;

    string currentData;
    int currentOpcode;

    bool isfragmented = false;

    double ping_time = 0.0;
    EcoreTimer *closeTimeout = nullptr;
    bool closeReceived = false;

    int data_size = 0; //data count remaining

    void reset(); //reset state machine

    bool checkHandshakeRequest();
    void processHandshake();
    void processControlFrame();

    bool checkCloseStatusCode(uint16_t code);

    void sendFrameData(const string &data, bool isbinary);

    Ecore_Con_Server *ecoreServer = nullptr;

    bool gotNewDataHandshake(const string &data);
    void gotNewData(const string &data);

    void CloseConnection();
    void sendToServer(string res);

    Ecore_Event_Handler *handler_add;
    Ecore_Event_Handler *handler_data;
    Ecore_Event_Handler *handler_del;
    Ecore_Event_Handler *handler_error;
    Ecore_Event_Handler *handler_written;

    friend Eina_Bool WebSocketClient_con_add(void *data, int type, void *event);
    friend Eina_Bool WebSocketClient_con_del(void *data, int type, void *event);
    friend Eina_Bool WebSocketClient_con_data(void *data, int type, void *event);
    friend Eina_Bool WebSocketClient_con_error(void *data, int type, void *event);
    friend Eina_Bool WebSocketClient_con_data_written(void *data, int type, void *event);

    string handshakeHeader;
    string sec_key;

    http_parser_settings parser_settings;
    http_parser *parser;

    bool parse_done = false;
    unordered_map<string, string> request_headers;

    //for parsing purposes
    bool has_field = false, has_value = false;
    string hfield, hvalue;
    string bodymessage;
    string parse_url;

    friend int _parser_begin(http_parser *parser);
    friend int _parser_header_field(http_parser *parser, const char *at, size_t length);
    friend int _parser_header_value(http_parser *parser, const char *at, size_t length);
    friend int _parser_headers_complete(http_parser *parser);
    friend int _parser_message_complete(http_parser *parser);
    friend int _parser_url(http_parser *parser, const char *at, size_t length);
    friend int _parser_body_complete(http_parser* parser, const char *at, size_t length);
};

#endif // WebSocketClient_H
