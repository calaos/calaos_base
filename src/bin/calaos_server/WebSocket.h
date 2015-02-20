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
#ifndef S_WebSocketServer_H
#define S_WebSocketServer_H

#include "Calaos.h"
#include <Ecore_Con.h>
#include <unordered_map>
#include "JsonApiClient.h"

using namespace Calaos;

class WebSocket: public JsonApiClient
{
public:
    WebSocket(Ecore_Con_Client *cl);
    virtual ~WebSocket();

    sigc::signal<void, const string &> textMessageReceived;
    sigc::signal<void, const vector<u_int8_t> &> binaryMessageReceived;
    sigc::signal<void> websocketDisconnected;

    /* Called by JsonApiServer whenever data comes in */
    virtual void ProcessData(string data);

private:

    enum { WSConnecting, WSOpened, WSClosed };
    int status = WSConnecting;

    bool checkHandshakeRequest();
    void processHandshake();
};

#endif
