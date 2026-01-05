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

#include "JsonApiHandlerWS.h"
#include "RemoteUIManager.h"
#include "AuthFailureReason.h"
#include "ListeRoom.h"
#include "json.hpp"
#include <memory>

namespace Calaos
{

class RemoteUI;

class RemoteUIWebSocketHandler: public JsonApiHandlerWS
{
private:
    RemoteUI *authenticated_remote_ui;
    AuthFailureReason last_auth_failure;

public:
    RemoteUIWebSocketHandler(HttpClient *client);
    virtual ~RemoteUIWebSocketHandler();

    // Override processApi to handle RemoteUI-specific messages first
    virtual void processApi(const string &data, const Params &paramsGET) override;

    // Authentication via HMAC headers
    // Returns true on success, false on failure
    // Use getLastAuthFailure() to get the reason for failure
    bool authenticateConnection(const std::map<string, string> &headers);

    // Get the last authentication failure reason (for HTTP error response)
    AuthFailureReason getLastAuthFailure() const { return last_auth_failure; }

    // RemoteUI-specific handlers
    void handleGetConfig();

    // Send RemoteUI-specific messages
    void sendInitialIOStates();
    void sendConfigUpdate();

    // Expose sendJson for RemoteUIManager to send notifications
    using JsonApiHandlerWS::sendJson;
};

}

#endif