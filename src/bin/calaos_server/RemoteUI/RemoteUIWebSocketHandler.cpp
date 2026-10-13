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
#include "IO/RemoteUI/RemoteUIOutputRelay.h"
#include "IOBase.h"
#include "HttpClient.h"
#include "RemoteUIManager.h"
#include "HMACAuthenticator.h"
#include "OtaFirmwareManager.h"
#include "IO/Device.h"
#include "ListeRoom.h"

static const char *TAG = "remote_ui";

using namespace Calaos;

RemoteUIWebSocketHandler::RemoteUIWebSocketHandler(HttpClient *client):
    JsonApiHandlerWS(client),
    authenticated_remote_ui(nullptr),
    last_auth_failure(AuthFailureReason::Success)
{
    cDebugDom(TAG) << "RemoteUIWebSocketHandler: Created for client " << httpClient->getClientIp();
}

RemoteUIWebSocketHandler::~RemoteUIWebSocketHandler()
{
    //Invalidate the alive token: async callbacks (post-auth timer,
    //buildJsonState completion) captured a weak_ptr on it and will no-op
    *handlerAlive = false;

    if (authenticated_remote_ui)
    {
        // Unregister this handler from RemoteUIManager
        // Pass 'this' so the manager only removes if we are the current handler
        // (avoids a stale connection removing a newer handler for the same device)
        RemoteUIManager::Instance().removeWebSocketHandler(authenticated_remote_ui->get_param("id"), this);
        cInfoDom(TAG) << "RemoteUIWebSocketHandler: Disconnected RemoteUI " << authenticated_remote_ui->get_param("id");
    }
}

bool RemoteUIWebSocketHandler::authenticateConnection(const std::map<string, string> &headers)
{
    WebSocketHeaders ws_headers;
    if (!ws_headers.parse(headers))
    {
        cWarningDom(TAG) << "RemoteUIWebSocketHandler: Failed to parse WebSocket headers";
        last_auth_failure = AuthFailureReason::MissingHeaders;
        return false;
    }

    string client_ip = httpClient->getClientIp();
    if (HMACAuthenticator::authenticateWebSocketConnection(ws_headers, client_ip, authenticated_remote_ui, last_auth_failure))
    {
        // Use parent's authentication mechanism
        setAuthenticated(true);

        // Update device info from headers if provided
        if (!ws_headers.device_version.empty())
        {
            authenticated_remote_ui->set_param("device_version", ws_headers.device_version);
            cDebugDom(TAG) << "Updated device_version from WS header: " << ws_headers.device_version;
        }
        if (!ws_headers.device_hardware_id.empty())
        {
            authenticated_remote_ui->set_param("device_type", ws_headers.device_hardware_id);
            cDebugDom(TAG) << "Updated device_type from WS header: " << ws_headers.device_hardware_id;
        }

        // Register this handler with RemoteUIManager
        RemoteUIManager::Instance().addWebSocketHandler(authenticated_remote_ui->get_param("id"), this);

        cInfoDom(TAG) << "RemoteUIWebSocketHandler: Successfully authenticated RemoteUI "
               << authenticated_remote_ui->get_param("id");

        // Send config first, then initial IO states, then check for OTA updates
        // Timer::singleShot fires unconditionally: guard with the alive token,
        // the handler may be deleted (device disconnect) before the timer fires
        Timer::singleShot(0.1, [this, alive = std::weak_ptr<bool>(handlerAlive)]()
        {
            auto token = alive.lock();
            if (!token || !*token)
                return; //handler was destroyed in the meantime

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

    // last_auth_failure is already set by authenticateWebSocketConnection
    return false;
}

void RemoteUIWebSocketHandler::processApi(const string &data, const Params &paramsGET)
{
    if (data.empty())
        return;

    /* This parse is the handler's own and nothing reaches the parent before
     * it, so the nesting ceiling the parent enforces does not cover it: gate
     * it here or a document above the ceiling is built once at full price.
     */
    if (requestNestingWithinLimit(data))
    {
        /* The parse and the handling are guarded apart because they fail for
         * unrelated reasons and a reader acts on the name: a handler that
         * throws on a well formed frame used to be logged as a parse error,
         * which sends the next reader into the parser. The handling guard is
         * the wide one - it covers the state writes a relay frame triggers and
         * the serialisation of the answer - so what it catches is only ever
         * named as unhandled.
         */
        Json message;
        bool parsed = false;

        // Check for RemoteUI-specific messages first
        try
        {
            message = Json::parse(data);
            parsed = true;
        }
        catch (const std::exception &e)
        {
            cWarningDom(TAG) << "RemoteUIWebSocketHandler: JSON parse error: "
                             << Utils::jsonErrorForLog(e, data.size());
        }

        if (parsed && message.contains("msg") && message["msg"].is_string())
        {
            string msg_type = message["msg"];

            /* A panel is not a user session: it is served the three messages
             * its firmware sends and nothing else of the JSON API, and
             * set_state reaches the parent only for an IO a widget names.
             */
            if (msg_type == "set_state" && panelMayWrite(message))
            {
                JsonApiHandlerWS::processApi(data, paramsGET);
                return;
            }

            if (msg_type != "remote_ui_get_config" && msg_type != "remote_ui_relay_state")
            {
                refuse(msg_type, message);
                return;
            }

            try
            {
                // Handle RemoteUI-specific messages
                if (msg_type == "remote_ui_get_config")
                {
                    cDebugDom(TAG) << "RemoteUIWebSocketHandler: Handling remote_ui_get_config";
                    handleGetConfig();
                    return;
                }

                if (msg_type == "remote_ui_relay_state")
                {
                    cDebugDom(TAG) << "RemoteUIWebSocketHandler: Handling remote_ui_relay_state";
                    if (message.contains("data"))
                        handleRelayState(message["data"]);
                    return;
                }
            }
            catch (const std::exception &e)
            {
                cWarningDom(TAG) << "RemoteUIWebSocketHandler: unhandled failure while serving "
                                 << msg_type << ": " << e.what();
            }
        }
        else if (parsed)
            cWarningDom(TAG) << "RemoteUIWebSocketHandler: frame without a message type, refused";
    }
    else
        cWarningDom(TAG) << "RemoteUIWebSocketHandler: request nesting deeper than "
                         << MaxRequestNestingDepth << " levels, refused";
}

bool RemoteUIWebSocketHandler::panelMayWrite(const Json &message)
{
    if (!authenticated_remote_ui)
        return false;

    const string id = setStateId(message);
    if (id.empty())
        return false;

    ensureScope();
    return authenticated_remote_ui->hasWritableIO(id);
}

void RemoteUIWebSocketHandler::refuse(const string &msg_type, const Json &message)
{
    const string panel = authenticated_remote_ui ? authenticated_remote_ui->get_param("id") : "?";
    const bool setState = msg_type == "set_state";

    //The type and the id come from the panel: tagged, never printed.
    if (setState)
        cWarningDom(TAG) << "RemoteUI " << panel << ": set_state refused on IO #"
                         << Utils::logTag(setStateId(message)) << ", no widget of this panel names it";
    else
        cWarningDom(TAG) << "RemoteUI " << panel << ": message #" << Utils::logTag(msg_type)
                         << " refused, a panel is only served what its firmware sends";

    Json answer = { { "error", "forbidden" } };
    if (setState)
        answer["success"] = "false";

    string msg_id;
    const auto it = message.find("msg_id");
    if (it != message.end() && it->is_string())
        msg_id = it->get<string>();

    sendJson(msg_type, answer, msg_id);
}

string RemoteUIWebSocketHandler::setStateId(const Json &message)
{
    const auto data = message.find("data");
    if (data == message.end() || !data->is_object())
        return string();
    const auto id = data->find("id");
    if (id == data->end() || !id->is_string())
        return string();
    return id->get<string>();
}

void RemoteUIWebSocketHandler::refreshScope()
{
    if (authenticated_remote_ui)
        authenticated_remote_ui->extractReferencedIOs();
    scopeFresh = true;
}

void RemoteUIWebSocketHandler::ensureScope()
{
    if (!scopeFresh)
        refreshScope();
}

bool RemoteUIWebSocketHandler::forwardsEvent(const CalaosEvent &event)
{
    switch (event.getType())
    {
    case CalaosEvent::EventIOAdded:
    case CalaosEvent::EventIODeleted:
        scopeFresh = false;
        break;
    case CalaosEvent::EventIOChanged:
    case CalaosEvent::EventIOPropertyDelete:
    case CalaosEvent::EventIOStatusChanged:
        break;
    default:
        //Rooms, scenarios, audio, pushes: nothing a panel widget shows.
        return false;
    }

    if (!authenticated_remote_ui)
        return false;

    ensureScope();
    return authenticated_remote_ui->hasReferencedIO(event.getParam()["id"]);
}

void RemoteUIWebSocketHandler::handleGetConfig()
{
    if (!authenticated_remote_ui)
        return;

    Json configData = authenticated_remote_ui->getRemoteUIConfigMessage();
    sendJson("remote_ui_config", configData);
}

void RemoteUIWebSocketHandler::handleRelayState(const Json &data)
{
    if (!authenticated_remote_ui)
        return;

    if (!data.contains("relay") || !data.contains("state"))
    {
        cWarningDom(TAG) << "RemoteUIWebSocketHandler: remote_ui_relay_state missing relay or state field";
        return;
    }

    int relay_num;
    bool state;

    try
    {
        relay_num = data["relay"].get<int>();
        state = data["state"].get<bool>();
    }
    catch (const std::exception &e)
    {
        cWarningDom(TAG) << "RemoteUIWebSocketHandler: Invalid relay state data: " << e.what();
        return;
    }

    string remote_ui_id = authenticated_remote_ui->get_param("id");
    string relay_num_str = Utils::to_string(relay_num);

    for (int i = 0; i < ListeRoom::Instance().size(); i++)
    {
        Room *room = ListeRoom::Instance()[i];
        for (int j = 0; j < room->get_size(); j++)
        {
            IOBase *io = room->get_io(j);
            if (io->get_param("type") == "RemoteUIOutputRelay" &&
                io->get_param("remote_ui_id") == remote_ui_id &&
                io->get_param("relay_num") == relay_num_str)
            {
                RemoteUIOutputRelay *relay = dynamic_cast<RemoteUIOutputRelay*>(io);
                if (relay)
                {
                    relay->updateStateFromDevice(state);
                    cDebugDom(TAG) << "RemoteUIWebSocketHandler: Updated relay " << relay_num
                                   << " state to " << state << " on " << remote_ui_id;
                }
                return;
            }
        }
    }

    cWarningDom(TAG) << "RemoteUIWebSocketHandler: No relay IO found for relay "
                     << relay_num << " on " << remote_ui_id;
}

void RemoteUIWebSocketHandler::sendInitialIOStates()
{
    if (!authenticated_remote_ui)
        return;

    refreshScope();
    const auto &referenced_ios = authenticated_remote_ui->getReferencedIOs();

    // Convert set to vector for buildJsonState
    vector<string> iolist(referenced_ios.begin(), referenced_ios.end());

    /* buildJsonState may defer the callback through async audio-player
     * queries: guard with the alive token, the handler may be deleted
     * (device disconnect) before the result comes back.
     *
     * E4.1n. THIS IS WHAT THE TICKET WAS FOR. Six lines used to sit here:
     *
     *      char *json_str = json_dumps(jret, JSON_COMPACT);
     *      json_decref(jret);
     *      if (json_str) { Json data = Json::parse(json_str); free(json_str);
     *
     * a full serialization plus a full reparse whose only purpose was to cross
     * the border between the two libraries, on a path that runs at every device
     * connection. buildJsonState() answers a Json now, so the bridge is one
     * parameter.
     *
     * That Json::parse was also the ONLY throwing form of this file with no try
     * above it (the other two, :128 and :185, are inside one), on a callback
     * reached from the event loop - i.e. std::terminate on a live server if it
     * ever threw. It could not, today, because jansson validated the UTF-8 that
     * json_dumps re-emitted; it is deleted rather than left to become live when
     * that validation goes.
     *
     * THE LIFE GUARD BELOW IS NOT PART OF THE CLEANUP. The decrefs disappear
     * because ownership is by value; the weak_ptr protects `this`, not the
     * document, and there are in fact TWO guards on this path - JsonApi's own
     * apiAlive stops the chain before the lambda is even entered, and this one
     * is the belt that also covers the post-auth timer, which is not inside
     * JsonApi. Pinned by RemoteUiStateBridgeTest.
     */
    buildJsonState(iolist, [this, iolist, alive = std::weak_ptr<bool>(handlerAlive)](Json jret)
    {
        auto token = alive.lock();
        if (!token || !*token)
            return;

        //A device travels whole, as an object entry; every other IO keeps its
        //bare string, which shipped firmwares and the goldens depend on.
        if (jret.is_object())
        {
            for (auto &entry: jret.items())
            {
                Device *dev = ListeRoom::Instance().findIOAs<Device>(entry.key());
                if (!dev)
                    continue;
                entry.value() = Json{ { "state", entry.value() },
                                      { "gui_type", dev->get_param("gui_type") },
                                      { "device", dev->deviceObject() } };
            }
        }

        sendJson("remote_ui_io_states", jret);

        cDebugDom(TAG) << "RemoteUIWebSocketHandler: Sent initial IO states ("
                 << iolist.size() << " IOs) to " << authenticated_remote_ui->get_param("id");
    });
}

void RemoteUIWebSocketHandler::sendConfigUpdate()
{
    if (!authenticated_remote_ui)
        return;

    static const char *const screensaverParams[] = {
        "screensaver_timeout", "screensaver_dimming", "screensaver_mode",
        "screensaver_clock_timezone", "screensaver_clock_format",
        "screensaver_clock_show_date", "screensaver_clock_date_format",
        "screensaver_clock_seconds"
    };

    Json data = Json::object();
    //See RemoteUI::putIfSet(): an unset param must leave NO key behind.
    RemoteUI::putIfSet(data, "name", authenticated_remote_ui->get_param("name"));
    authenticated_remote_ui->putBrightnessIfSet(data);
    //Non-throwing parse: a malformed device-supplied value must not throw
    //out of the timer callback into the event loop (default grid is 3x3)
    data["grid_height"] = parseGridDimension(authenticated_remote_ui->get_param("grid_h"), 3);
    data["grid_width"] = parseGridDimension(authenticated_remote_ui->get_param("grid_w"), 3);
    for (const char *const param: screensaverParams)
        RemoteUI::putIfSet(data, param, authenticated_remote_ui->get_param(param));
    data["pages"] = authenticated_remote_ui->getPages();

    // Add room information
    Room *room = ListeRoom::Instance().getRoomByIO(authenticated_remote_ui);
    if (room)
    {
        Json room_json = Json::object();
        RemoteUI::putIfSet(room_json, "name", room->get_name());
        RemoteUI::putIfSet(room_json, "type", room->get_type());
        room_json["hits"] = room->get_hits();
        data["room"] = room_json;
    }

    // Add IO items
    refreshScope();
    const auto &referenced_ios = authenticated_remote_ui->getReferencedIOs();
    Json io_items = Json::array();
    for (const string &io_id : referenced_ios)
    {
        IOBase *io = ListeRoom::Instance().get_io(io_id);
        if (io)
        {
            Json io_json;
            buildJsonIO(io, io_json, IoProjection::DeviceConfig);
            io_items.push_back(io_json);
        }
    }
    data["io_items"] = io_items;

    sendJson("remote_ui_config_update", data);
}
