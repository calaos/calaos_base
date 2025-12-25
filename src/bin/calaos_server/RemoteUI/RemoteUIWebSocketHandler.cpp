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
#include "OtaFirmwareManager.h"

static const char *TAG = "remote_ui";

using namespace Calaos;

RemoteUIWebSocketHandler::RemoteUIWebSocketHandler(HttpClient *client):
    JsonApiHandlerWS(client),
    authenticated_remote_ui(nullptr)
{
    cDebugDom(TAG) << "RemoteUIWebSocketHandler: Created for client " << httpClient->getClientIp();
}

RemoteUIWebSocketHandler::~RemoteUIWebSocketHandler()
{
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
        // Use parent's authentication mechanism
        setAuthenticated(true);

        // Register this handler with RemoteUIManager
        RemoteUIManager::Instance().addWebSocketHandler(authenticated_remote_ui->get_param("id"), this);

        cInfoDom(TAG) << "RemoteUIWebSocketHandler: Successfully authenticated RemoteUI "
               << authenticated_remote_ui->get_param("id");

        // Send config first, then initial IO states, then check for OTA updates
        Timer::singleShot(0.1, [this]()
        {
            sendConfigUpdate();
            sendInitialIOStates();

            // Check for firmware updates
            if (authenticated_remote_ui && OtaFirmwareManager::Instance().isEnabled())
            {
                string hardwareId = authenticated_remote_ui->get_param("device_type");
                string currentVersion = authenticated_remote_ui->get_param("device_version");
                OtaFirmwareManager::Instance().checkDeviceForUpdate(this, hardwareId, currentVersion);
            }
        });

        return true;
    }

    return false;
}

void RemoteUIWebSocketHandler::processApi(const string &data, const Params &paramsGET)
{
    if (data.empty())
        return;

    // Check for RemoteUI-specific messages first
    try
    {
        Json message = Json::parse(data);

        if (message.contains("msg") && message["msg"].is_string())
        {
            string msg_type = message["msg"];

            // Handle RemoteUI-specific messages
            if (msg_type == "remote_ui_get_config")
            {
                cDebugDom(TAG) << "RemoteUIWebSocketHandler: Handling remote_ui_get_config";
                handleGetConfig();
                return;
            }
        }
    }
    catch (const std::exception &e)
    {
        cWarningDom(TAG) << "RemoteUIWebSocketHandler: JSON parse error: " << e.what();
    }

    // Delegate all other messages to parent class (JsonApiHandlerWS)
    JsonApiHandlerWS::processApi(data, paramsGET);
}

void RemoteUIWebSocketHandler::handleGetConfig()
{
    if (!authenticated_remote_ui)
        return;

    Json configData = authenticated_remote_ui->getRemoteUIConfigMessage();
    sendJson("remote_ui_config", configData);
}

void RemoteUIWebSocketHandler::sendInitialIOStates()
{
    if (!authenticated_remote_ui)
        return;

    const auto &referenced_ios = authenticated_remote_ui->getReferencedIOs();

    // Convert set to vector for buildJsonState
    vector<string> iolist(referenced_ios.begin(), referenced_ios.end());

    buildJsonState(iolist, [this, iolist](json_t *jret)
    {
        // Convert jansson json_t to nlohmann::json
        char *json_str = json_dumps(jret, JSON_COMPACT);
        if (json_str)
        {
            Json data = Json::parse(json_str);
            free(json_str);
            sendJson("remote_ui_io_states", data);

            cDebugDom(TAG) << "RemoteUIWebSocketHandler: Sent initial IO states ("
                     << iolist.size() << " IOs) to " << authenticated_remote_ui->get_param("id");
        }
    });
}

void RemoteUIWebSocketHandler::sendConfigUpdate()
{
    if (!authenticated_remote_ui)
        return;

    Json data;
    data["name"] = authenticated_remote_ui->get_param("name");
    data["brightness"] = authenticated_remote_ui->getBrightness();
    data["grid_height"] = std::stoi(authenticated_remote_ui->get_param("grid_h"));
    data["grid_width"] = std::stoi(authenticated_remote_ui->get_param("grid_w"));
    data["pages"] = authenticated_remote_ui->getPages();

    // Add room information
    Room *room = ListeRoom::Instance().getRoomByIO(authenticated_remote_ui);
    if (room)
    {
        Json room_json;
        room_json["name"] = room->get_name();
        room_json["type"] = room->get_type();
        room_json["hits"] = room->get_hits();
        data["room"] = room_json;
    }

    // Add IO items
    const auto &referenced_ios = authenticated_remote_ui->getReferencedIOs();
    Json io_items = Json::array();
    for (const string &io_id : referenced_ios)
    {
        IOBase *io = ListeRoom::Instance().get_io(io_id);
        if (io)
        {
            Json io_json;
            vector<string> params = { "id", "name", "type", "hits", "var_type", "visible", "chauffage_id", "rw", "unit", "gui_type", "state", "auto_scenario", "step", "io_type", "io_style", "value_warning" };
            for (const string &param : params)
            {
                string val = io->get_param(param);
                if (!val.empty())
                    io_json[param] = val;
            }
            io_items.push_back(io_json);
        }
    }
    data["io_items"] = io_items;

    sendJson("remote_ui_config_update", data);
}
