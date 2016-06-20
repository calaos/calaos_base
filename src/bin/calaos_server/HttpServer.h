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
#ifndef S_HttpServer_H
#define S_HttpServer_H

#include "Calaos.h"
#include "WebSocket.h"
#include "HttpClient.h"
#include <Ecore_Con.h>

namespace Calaos {

class HttpServer
{
private:
    int port;
    Ecore_Con_Server *tcp_server;
    Ecore_Event_Handler *event_handler_client_add;
    Ecore_Event_Handler *event_handler_client_del;
    Ecore_Event_Handler *event_handler_data_get;
    Ecore_Event_Handler *event_handler_client_write;

    std::map<Ecore_Con_Client *, WebSocket *> connections;

    HttpServer(int port); //port to listen

public:
    static HttpServer &Instance(int port = 0)
    {
        static HttpServer server(port);

        return server;
    }
    ~HttpServer();

    void disconnectAll();

    /* Internal stuff used by ecore-con */
    void addConnection(Ecore_Con_Client *client);
    void delConnection(Ecore_Con_Client *client);
    void getDataConnection(Ecore_Con_Client *client, void *data, int size);
    void dataWritten(Ecore_Con_Client *client, int size);
};

}

#endif
