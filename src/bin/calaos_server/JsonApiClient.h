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
#ifndef S_JsonApiClient_H
#define S_JsonApiClient_H

#include <Calaos.h>
#include <Ecore_Con.h>
#include <ListeRoom.h>
#include <Room.h>
#include <AudioManager.h>
#include <AudioPlayer.h>
#include <CamManager.h>
#include <IPCam.h>
#include <InPlageHoraire.h>
#include <jansson.h>
#include <http_parser.h>
#include <unordered_map>

using namespace Calaos;

class JsonApiClient: public sigc::trackable
{
protected:

    Ecore_Con_Client *client_conn;
    Ecore_Event_Handler *exe_handler;

    Ecore_Exe *exe_thumb;
    string tempfname;

    http_parser_settings parser_settings;
    http_parser *parser;

    bool parse_done = false;
    unsigned char request_method;
    unordered_map<string, string> request_headers;

    Params jsonParam;

    int player_count;

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
    EcoreTimer *closeTimer = nullptr;

    void handleRequest();
    void sendToClient(string res);
    string buildHttpResponse(string code, Params &headers, string body);
    string buildHttpResponseFromFile(string code, Params &headers, string fileName);
    void sendJson(json_t *json);

    //processing functions
    void processGetHome();
    void processGetState(json_t *jroot);
    void processSetState();
    void processGetPlaylist();
    void processPolling();
    void processGetCover();
    void processGetCameraPic();
    void processConfig(json_t *jroot);

    json_t *buildJsonHome();
    json_t *buildJsonCameras();
    json_t *buildJsonAudio();
    template<typename T> json_t *buildJsonRoomIO(Room *room);
    void getNextPlaylistItem(AudioPlayer *player, json_t *jplayer, json_t *jplaylist, int it_current, int it_count);

    void exeFinished(Ecore_Exe *exe, int exit_code);

    friend int _parser_begin(http_parser *parser);
    friend int _parser_header_field(http_parser *parser, const char *at, size_t length);
    friend int _parser_header_value(http_parser *parser, const char *at, size_t length);
    friend int _parser_headers_complete(http_parser *parser);
    friend int _parser_message_complete(http_parser *parser);
    friend int _parser_url(http_parser *parser, const char *at, size_t length);
    friend int _parser_body_complete(http_parser* parser, const char *at, size_t length);
    friend Eina_Bool _ecore_exe_finished(void *data, int type, void *event);

public:
    JsonApiClient(Ecore_Con_Client *cl);
    ~JsonApiClient();

    enum { APIV1 = 0, APIV1_5, APIV2 };

    /* Called by JsonApiServer whenever data comes in */
    void ProcessData(string data);

    /* Called by JsonApiServer whenever data has been written to client */
    void DataWritten(int size);
};

#endif
