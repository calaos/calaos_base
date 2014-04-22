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
#ifndef S_JsonApiServer_H
#define S_JsonApiServer_H

#include <Calaos.h>
#include <JsonApiClient.h>
#include <Ecore_Con.h>

using namespace std;

class JsonApiServer
{
private:
    int port;
    Ecore_Con_Server *tcp_server;
    Ecore_Event_Handler *event_handler_client_add;
    Ecore_Event_Handler *event_handler_client_del;
    Ecore_Event_Handler *event_handler_data_get;
    Ecore_Event_Handler *event_handler_client_write;

    map<Ecore_Con_Client *, JsonApiClient *> connections;

public:
    JsonApiServer(int port); //port to listen
    static JsonApiServer &Instance(int port)
    {
        static JsonApiServer server(port);

        return server;
    }
    ~JsonApiServer();


    /* Internal stuff used by ecore-con */
    void addConnection(Ecore_Con_Client *client);
    void delConnection(Ecore_Con_Client *client);
    void getDataConnection(Ecore_Con_Client *client, void *data, int size);
    void dataWritten(Ecore_Con_Client *client, int size);
};
#endif
