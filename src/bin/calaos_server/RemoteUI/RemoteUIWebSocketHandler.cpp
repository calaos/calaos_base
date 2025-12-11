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
#include "RemoteUIWebSocketHandler.h"
#include "IO/RemoteUI/RemoteUI.h"
#include "IOBase.h"
#include "HttpClient.h"
#include "RemoteUIManager.h"
#include "HMACAuthenticator.h"

static const char *TAG = "remote_ui";

using namespace Calaos;

RemoteUIWebSocketHandler::RemoteUIWebSocketHandler(HttpClient *client):
    JsonApi(client),
    authenticated_remote_ui(nullptr),
    is_authenticated(false)
{
    cDebugDom(TAG) << "RemoteUIWebSocketHandler: Created for client " << httpClient->getClientIp();
}

RemoteUIWebSocketHandler::~RemoteUIWebSocketHandler()
{
    disconnectFromEventManager();

    if (authenticated_remote_ui)
    {
        // Unregister this handler from RemoteUIManager
        RemoteUIManager::Instance().removeWebSocketHandler(authenticated_remote_ui->get_param("id"));
        cInfoDom(TAG) << "RemoteUIWebSocketHandler: Disconnected RemoteUI " << authenticated_remote_ui->get_param("id");
    }
}

bool RemoteUIWebSocketHandler::authenticateConnection(const std::map<string, string> &headers)
{
    WebSocketHeaders ws_headers;
    if (!ws_headers.parse(headers))
    {
        cWarningDom(TAG) << "RemoteUIWebSocketHandler: Failed to parse WebSocket headers";
        return false;
    }

    string client_ip = httpClient->getClientIp();
    if (HMACAuthenticator::authenticateWebSocketConnection(ws_headers, client_ip, authenticated_remote_ui))
    {
        is_authenticated = true;
        connectToEventManager();

        // Register this handler with RemoteUIManager
        RemoteUIManager::Instance().addWebSocketHandler(authenticated_remote_ui->get_param("id"), this);

        cInfoDom(TAG) << "RemoteUIWebSocketHandler: Successfully authenticated RemoteUI "
               << authenticated_remote_ui->get_param("id");

        // Send config first, then initial IO states
        Timer::singleShot(0.1, [this]()
        {
            sendConfigUpdate();
            sendInitialIOStates();
        });

        return true;
    }

    return false;
}

void RemoteUIWebSocketHandler::processApi(const string &data, const Params &paramsGET)
{
    if (!is_authenticated)
    {
        cWarningDom(TAG) << "RemoteUIWebSocketHandler: Received message from unauthenticated client";
        sendMessage(createErrorMessage("Not authenticated"));
        return;
    }

    if (data.empty())
        return;

    try
    {
        Json message = Json::parse(data);
        handleMessage(message);
    }
    catch (const std::exception &e)
    {
        cWarningDom(TAG) << "RemoteUIWebSocketHandler: Invalid JSON received: " << e.what();
        sendMessage(createErrorMessage("Invalid JSON"));
    }
}

void RemoteUIWebSocketHandler::handleMessage(const Json &message)
{
    if (!message.contains("msg") || !message["msg"].is_string())
    {
        sendMessage(createErrorMessage("Missing or invalid 'msg' field"));
        return;
    }

    string msg_type = message["msg"];

    cDebugDom(TAG) << "RemoteUIWebSocketHandler: Handling message type: " << msg_type;

    if (msg_type == "set_state")
    {
        if (message.contains("data"))
            handleSetState(message["data"]);
        else
            sendMessage(createErrorMessage("Missing 'data' field for set_state"));
    }
    else if (msg_type == "remote_ui_get_config")
    {
        handleGetConfig();
    }
    else if (msg_type == "ping")
    {
        handlePing();
    }
    else
    {
        sendMessage(createErrorMessage("Unknown message type: " + msg_type));
    }
}

void RemoteUIWebSocketHandler::handleSetState(const Json &data)
{
    if (!data.contains("id") || !data["id"].is_string())
    {
        sendMessage(createErrorMessage("Missing or invalid 'id' field"));
        return;
    }

    string io_id = data["id"];

    // Verify this RemoteUI has access to this IO
    if (!authenticated_remote_ui->hasReferencedIO(io_id))
    {
        cWarningDom(TAG) << "RemoteUIWebSocketHandler: Unauthorized access attempt to IO "
                   << io_id << " by RemoteUI " << authenticated_remote_ui->get_param("id");
        sendMessage(createErrorMessage("Unauthorized access to IO: " + io_id));
        return;
    }

    // Find the IO and set its state
    IOBase *io = ListeRoom::Instance().get_io(io_id);
    if (!io)
    {
        sendMessage(createErrorMessage("IO not found: " + io_id));
        return;
    }

    if (data.contains("value"))
    {
        if (data["value"].is_boolean())
        {
            io->set_value(data["value"].get<bool>());
        }
        else if (data["value"].is_number())
        {
            io->set_value(data["value"].get<double>());
        }
        else if (data["value"].is_string())
        {
            io->set_value(data["value"].get<string>());
        }

        cDebugDom(TAG) << "RemoteUIWebSocketHandler: Set state of " << io_id
                 << " to " << data["value"].dump();
    }
}

void RemoteUIWebSocketHandler::handleGetConfig()
{
    if (!authenticated_remote_ui)
        return;

    Json message = authenticated_remote_ui->getRemoteUIConfigMessage();
    sendMessage(message);
}

void RemoteUIWebSocketHandler::handlePing()
{
    Json pong_message;
    pong_message["msg"] = "pong";
    pong_message["data"] = nullptr;
    sendMessage(pong_message);
}

void RemoteUIWebSocketHandler::sendInitialIOStates()
{
    if (!authenticated_remote_ui)
        return;

    std::map<string, Json> io_states;
    const auto &referenced_ios = authenticated_remote_ui->getReferencedIOs();

    for (const string &io_id : referenced_ios)
    {
        IOBase *io = ListeRoom::Instance().get_io(io_id);
        if (io)
        {
            io_states[io_id] = getIOStateJson(io);
        }
    }

    Json message = authenticated_remote_ui->getRemoteUIIOStatesMessage(io_states);
    sendMessage(message);

    cDebugDom(TAG) << "RemoteUIWebSocketHandler: Sent initial IO states ("
             << io_states.size() << " IOs) to " << authenticated_remote_ui->get_param("id");
}

void RemoteUIWebSocketHandler::sendIOStateUpdate(const string &io_id, const Json &io_state)
{
    Json message;
    message["msg"] = "io_state";
    message["data"] = io_state;
    sendMessage(message);
}

void RemoteUIWebSocketHandler::sendConfigUpdate()
{
    if (!authenticated_remote_ui)
        return;

    Json message;
    message["msg"] = "remote_ui_config_update";

    Json data;
    data["version"] = 1; // TODO: Implement versioning
    data["changes"] = Json::array({"pages", "theme"});
    data["action"] = "reload";

    message["data"] = data;
    sendMessage(message);
}

void RemoteUIWebSocketHandler::sendMessage(const Json &message)
{
    string msg_str = message.dump();
    sendData.emit(msg_str);

    cDebugDom(TAG) << "RemoteUIWebSocketHandler: Sent message (" << msg_str.length() << " bytes)";
}

void RemoteUIWebSocketHandler::connectToEventManager()
{
    event_connection = EventManager::Instance().newEvent.connect(
        sigc::mem_fun(*this, &RemoteUIWebSocketHandler::handleIOEvent)
    );
}

void RemoteUIWebSocketHandler::disconnectFromEventManager()
{
    event_connection.disconnect();
}

void RemoteUIWebSocketHandler::handleIOEvent(const CalaosEvent &event)
{
    if (!authenticated_remote_ui || event.getType() != CalaosEvent::EventIOChanged)
        return;

    string io_id = event.getParam().get_param_const("id");

    // Only send updates for IOs this RemoteUI cares about
    if (!authenticated_remote_ui->hasReferencedIO(io_id))
        return;

    IOBase *io = ListeRoom::Instance().get_io(io_id);
    if (io)
    {
        Json io_state = getIOStateJson(io);
        sendIOStateUpdate(io_id, io_state);

        cDebugDom(TAG) << "RemoteUIWebSocketHandler: Sent IO state update for " << io_id;
    }
}

Json RemoteUIWebSocketHandler::createErrorMessage(const string &error) const
{
    Json message;
    message["msg"] = "error";

    Json data;
    data["error"] = error;
    message["data"] = data;

    return message;
}

Json RemoteUIWebSocketHandler::getIOStateJson(IOBase *io) const
{
    if (!io)
        return Json{};

    Json io_json;
    io_json["io_id"] = io->get_param("id");
    io_json["type"] = io->get_param("type");

    // Get state/value based on IO type
    if (io->isInput())
    {
        if (io->get_param("gui_type") == "temp")
        {
            double value = 0.0;
            if (Utils::from_string(io->get_value_string(), value))
                io_json["value"] = value;
            else
                io_json["value"] = 0.0;
        }
        else if (io->get_param("gui_type") == "analog_in")
        {
            double value = 0.0;
            if (Utils::from_string(io->get_value_string(), value))
                io_json["value"] = value;
            else
                io_json["value"] = 0.0;
        }
        else
        {
            io_json["state"] = io->get_value_bool();
        }
    }
    else if (io->isOutput())
    {
        if (io->get_param("gui_type") == "light_dimmer")
        {
            io_json["state"] = io->get_value_bool();
            int value = 0;
            if (Utils::from_string(io->get_value_string(), value))
                io_json["value"] = value;
            else
                io_json["value"] = 0;
        }
        else if (io->get_param("gui_type") == "analog_out")
        {
            double value = 0.0;
            if (Utils::from_string(io->get_value_string(), value))
                io_json["value"] = value;
            else
                io_json["value"] = 0.0;
        }
        else if (io->get_param("gui_type") == "shutter" || io->get_param("gui_type") == "shutter_smart")
        {
            int state = 0;
            if (Utils::from_string(io->get_value_string(), state))
                io_json["state"] = state;
            else
                io_json["state"] = 0;
        }
        else
        {
            io_json["state"] = io->get_value_bool();
        }
    }

    return io_json;
}