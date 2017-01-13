/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#include <Ecore_Con.h>
#include "http_parser.h"
#include <unordered_map>
#include "JsonApiHandlerHttp.h"
#include "JsonApiHandlerWS.h"
#include "Timer.h"

using namespace Calaos;

class HttpClient: public sigc::trackable
{
protected:

    Ecore_Con_Client *client_conn;

    http_parser_settings parser_settings;
    http_parser *parser;

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

    bool isWebsocket = false;

    JsonApi *jsonApi = nullptr;

    Params paramsGET;

    enum
    {
        HTTP_PROCESS_MOREDATA = 0,  //need more data for headers
        HTTP_PROCESS_HTTP,          //normal http request that should be processed
        HTTP_PROCESS_WEBSOCKET,     //websocket request
        HTTP_PROCESS_DONE,          //request already processed (for HTTP OPTIONS, or debug.html)
    };
    int processHeaders(const string &request);

    void handleJsonRequest();

    void sendToClient(string res);

    string getMimeType(const string &file_ext);

    friend int _parser_begin(http_parser *parser);
    friend int _parser_header_field(http_parser *parser, const char *at, size_t length);
    friend int _parser_header_value(http_parser *parser, const char *at, size_t length);
    friend int _parser_headers_complete(http_parser *parser);
    friend int _parser_message_complete(http_parser *parser);
    friend int _parser_url(http_parser *parser, const char *at, size_t length);
    friend int _parser_body_complete(http_parser* parser, const char *at, size_t length);

public:
    HttpClient(Ecore_Con_Client *cl);
    virtual ~HttpClient();

    enum { APINONE = 0, API_HTTP, API_WEBSOCKET };

    /* Called by JsonApiServer whenever data has been written to client */
    virtual void DataWritten(int size);

    string buildHttpResponse(string code, Params &headers, string body);
    string buildHttpResponseFromFile(string code, Params &headers, string fileName);

    void setNeedRestart(bool e) { need_restart = e; }
};

#endif
