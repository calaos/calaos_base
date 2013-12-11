/******************************************************************************
**  Copyright (c) 2007-2013, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
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
#include <http_parser.h>
#include <unordered_map>

using namespace Calaos;

        class JsonApiClient: public sigc::trackable
        {
                protected:

                        Ecore_Con_Client *client_conn;

                        http_parser_settings parser_settings;
                        http_parser *parser;

                        bool parse_done = false;
                        unsigned char request_method;
                        unordered_map<string, string> request_headers;

                        void CloseConnection();

                        friend int _parser_header_field(http_parser *parser, const char *at, size_t length);
                        friend int _parser_header_value(http_parser *parser, const char *at, size_t length);
                        friend int _parser_headers_complete(http_parser *parser);
                        friend int _parser_message_complete(http_parser *parser);
                        friend int _parser_url(http_parser *parser, const char *at, size_t length);
                        friend int _parser_body_complete(http_parser* parser, const char *at, size_t length);

                public:
                        JsonApiClient(Ecore_Con_Client *cl);
                        ~JsonApiClient();

                        /* Called by TCPServer whenever data comes in */
                        void ProcessData(string data);
        };

#endif
