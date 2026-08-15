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

    /* Alive token for asynchronous callbacks kept outside of this object
     * (post-auth timer, buildJsonState completion): they capture a weak_ptr
     * on it and bail out when the handler has been destroyed in the
     * meantime (device dropping the connection right after authenticating).
     * Same pattern as JsonApiHandlerHttp::handlerAlive.
     */
    std::shared_ptr<bool> handlerAlive { std::make_shared<bool>(true) };

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
    void handleRelayState(const Json &data);

    // Send RemoteUI-specific messages
    void sendInitialIOStates();
    void sendConfigUpdate();

    /* Non-throwing parse of a client-supplied grid dimension.
     * std::stoi would throw out of timer callbacks into the event loop on
     * malformed values; this returns fallback instead when the value is not
     * a plain integer in [1, 1000].
     * Header-inline so it can be unit-tested without linking server objects.
     */
    static int parseGridDimension(const std::string &value, int fallback)
    {
        int v = 0;
        if (!Utils::from_string(value, v))
            return fallback;
        if (v < 1 || v > 1000)
            return fallback;
        return v;
    }

    // Expose sendJson for RemoteUIManager to send notifications
    using JsonApiHandlerWS::sendJson;
};

}

#endif