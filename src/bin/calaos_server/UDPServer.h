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
#ifndef S_UDPServer_H
#define S_UDPServer_H

#include <Calaos.h>
#include <tcpsocket.h>
#include <Ecore_Con.h>
#include <WagoMap.h>

namespace Calaos {

class UDPServer
{
protected:
    int port;

    Ecore_Con_Server *udp_server;
    Ecore_Event_Handler *event_handler_data_get;

    Ecore_Con_Server *udp_broadcast, *udp_sender;


public:
    UDPServer(int port); //port to listen
    ~UDPServer();

    /* Internal stuff used by ecore-con */
    void ProcessRequest(Ecore_Con_Client *client, std::string request);
};

}

#endif
