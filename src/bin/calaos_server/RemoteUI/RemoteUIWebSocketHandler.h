/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#ifndef REMOTEUIWEBSOCKETHANDLER_H
#define REMOTEUIWEBSOCKETHANDLER_H

#include "JsonApi.h"
#include "RemoteUIManager.h"
#include "EventManager.h"
#include "ListeRoom.h"
#include "json.hpp"
#include <memory>

namespace Calaos
{

class RemoteUI;

class RemoteUIWebSocketHandler: public JsonApi
{
private:
    RemoteUI *authenticated_remote_ui;
    bool is_authenticated;
    sigc::connection event_connection;

public:
    RemoteUIWebSocketHandler(HttpClient *client);
    virtual ~RemoteUIWebSocketHandler();

    // Override JsonApi methods
    virtual void processApi(const string &data, const Params &paramsGET) override;

    // Authentication
    bool authenticateConnection(const std::map<string, string> &headers);
    bool isAuthenticated() const { return is_authenticated; }

    // Message handlers
    void handleMessage(const Json &message);
    void handleSetState(const Json &data);
    void handleGetConfig();
    void handlePing();

    // Send messages to RemoteUI
    void sendInitialIOStates();
    void sendIOStateUpdate(const string &io_id, const Json &io_state);
    void sendConfigUpdate();
    void sendMessage(const Json &message);

    // Event handling
    void handleIOEvent(const CalaosEvent &event);

private:
    void connectToEventManager();
    void disconnectFromEventManager();
    Json createErrorMessage(const string &error) const;
    Json getIOStateJson(IOBase *io) const;
};

}

#endif