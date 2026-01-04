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
#include "RemoteUIProvisioningHandler.h"
#include "HttpClient.h"
#include "IO/RemoteUI/RemoteUI.h"
#include "ListeRoom.h"
#include "CalaosConfig.h"

static const char *TAG = "remote_ui";

using namespace Calaos;

RemoteUIProvisioningHandler::RemoteUIProvisioningHandler(HttpClient *client):
    httpClient(client)
{
}

RemoteUIProvisioningHandler::~RemoteUIProvisioningHandler()
{
}

bool RemoteUIProvisioningHandler::canHandleRequest(const string &uri, const string &method) const
{
    // Handle provisioning endpoints
    if (uri.find("/api/v3/provision/") == 0)
        return true;

    return false;
}

void RemoteUIProvisioningHandler::processRequest(const string &uri, const string &method,
                                               const string &data, const Params &paramsGET)
{
    cDebugDom(TAG) << "RemoteUIProvisioningHandler: Processing " << method << " " << uri;

    try
    {
        if (uri == "/api/v3/provision/request" && method == "POST")
        {
            handleProvisionRequest(data);
        }
        else if (uri == "/api/v3/provision/verify" && method == "POST")
        {
            handleProvisionVerify(data);
        }
        else
        {
            sendErrorResponse("Endpoint not found", 404);
        }
    }
    catch (const std::exception &e)
    {
        cErrorDom(TAG) << "RemoteUIProvisioningHandler: Exception processing request: " << e.what();
        sendErrorResponse("Internal server error", 500);
    }
}

void RemoteUIProvisioningHandler::handleProvisionRequest(const string &data)
{
    if (data.empty())
    {
        sendErrorResponse("Empty request body", 400);
        return;
    }

    Json request;
    try
    {
        request = Json::parse(data);
    }
    catch (const std::exception &e)
    {
        sendErrorResponse("Invalid JSON", 400);
        return;
    }

    if (!validateProvisioningRequest(request))
    {
        sendErrorResponse("Invalid provisioning request", 400);
        return;
    }

    string code = request["code"];
    Json device_info = parseDeviceInfo(request["device_info"]);
    string mac_address = device_info.value("mac_address", "");

    cInfoDom(TAG) << "RemoteUIProvisioningHandler: Provisioning request for code " << code
            << " from device " << mac_address;

    // Search for existing RemoteUI IO with this provisioning code
    RemoteUI *remote_ui = nullptr;
    for (int i = 0; i < ListeRoom::Instance().size(); i++)
    {
        Room *room = ListeRoom::Instance()[i];
        for (int j = 0; j < room->get_size(); j++)
        {
            IOBase *io = room->get_io(j);
            if ((io->get_param("type") == "RemoteUI" || io->get_param("type") == "remote_ui") &&
                io->get_param("provisioning_code") == code)
            {
                remote_ui = dynamic_cast<RemoteUI*>(io);
                break;
            }
        }
        if (remote_ui)
            break;
    }

    if (!remote_ui)
    {
        cErrorDom(TAG) << "RemoteUIProvisioningHandler: No RemoteUI found with provisioning code " << code;
        sendErrorResponse("Invalid provisioning code", 404);
        return;
    }

    // Update device info
    if (device_info.contains("type"))
        remote_ui->set_param("device_type", device_info["type"]);
    if (device_info.contains("manufacturer"))
        remote_ui->set_param("device_manufacturer", device_info["manufacturer"]);
    if (device_info.contains("model"))
        remote_ui->set_param("device_model", device_info["model"]);
    if (device_info.contains("version"))
        remote_ui->set_param("device_version", device_info["version"]);
    if (device_info.contains("mac_address"))
        remote_ui->set_param("mac_address", device_info["mac_address"]);

    // Generate secrets if not already provisioned
    if (remote_ui->get_param("device_secret").empty())
    {
        remote_ui->set_param("auth_token", "remote_ui_" + code);
        remote_ui->generateDeviceSecret();
    }

    // Save configuration
    Config::Instance().SaveConfigIO();

    // Send provisioning response
    Json response = remote_ui->getProvisioningResponse();
    sendJsonResponse(response);

    cInfoDom(TAG) << "RemoteUIManager: Provisioned RemoteUI " << remote_ui->get_param("id");
}

void RemoteUIProvisioningHandler::handleProvisionVerify(const string &data)
{
    if (data.empty())
    {
        sendErrorResponse("Empty request body", 400);
        return;
    }

    Json request;
    try
    {
        request = Json::parse(data);
    }
    catch (const std::exception &e)
    {
        sendErrorResponse("Invalid JSON", 400);
        return;
    }

    // Validate required fields
    if (!request.contains("device_id") || !request["device_id"].is_string())
    {
        sendErrorResponse("Missing device_id", 400);
        return;
    }
    if (!request.contains("auth_token") || !request["auth_token"].is_string())
    {
        sendErrorResponse("Missing auth_token", 400);
        return;
    }

    string device_id = request["device_id"];
    string auth_token = request["auth_token"];

    cDebugDom(TAG) << "RemoteUIProvisioningHandler: Verify request for device " << device_id;

    // Find RemoteUI by device_id
    RemoteUI *remote_ui = RemoteUIManager::Instance().getRemoteUI(device_id);
    if (!remote_ui)
    {
        cWarningDom(TAG) << "RemoteUIProvisioningHandler: Unknown device " << device_id;
        Json response;
        response["status"] = "invalid";
        response["reason"] = "unknown_device";
        sendJsonResponse(response, 401);
        return;
    }

    // Verify auth_token matches
    if (remote_ui->get_param("auth_token") != auth_token)
    {
        cWarningDom(TAG) << "RemoteUIProvisioningHandler: Invalid auth_token for device " << device_id;
        Json response;
        response["status"] = "invalid";
        response["reason"] = "invalid_token";
        sendJsonResponse(response, 401);
        return;
    }

    // Optionally update device_info if provided
    if (request.contains("device_info") && request["device_info"].is_object())
    {
        Json device_info = request["device_info"];

        if (device_info.contains("type"))
            remote_ui->set_param("device_type", device_info["type"]);
        if (device_info.contains("manufacturer"))
            remote_ui->set_param("device_manufacturer", device_info["manufacturer"]);
        if (device_info.contains("model"))
            remote_ui->set_param("device_model", device_info["model"]);
        if (device_info.contains("version"))
            remote_ui->set_param("device_version", device_info["version"]);
        if (device_info.contains("mac_address"))
            remote_ui->set_param("mac_address", device_info["mac_address"]);

        // Save configuration if device_info was updated
        Config::Instance().SaveConfigIO();
    }

    cInfoDom(TAG) << "RemoteUIProvisioningHandler: Provisioning verified for device " << device_id;

    Json response;
    response["status"] = "valid";
    sendJsonResponse(response);
}

void RemoteUIProvisioningHandler::sendJsonResponse(const Json &response, int status_code)
{
    string response_str = response.dump();

    string http_response = "HTTP/1.1 " + Utils::to_string(status_code) +
                          (status_code == 200 ? " OK" : " Error") + "\r\n" +
                          "Content-Type: application/json\r\n" +
                          "Content-Length: " + Utils::to_string(response_str.length()) + "\r\n" +
                          "Connection: close\r\n\r\n" + response_str;
    httpClient->sendToClient(http_response);

    cDebugDom(TAG) << "RemoteUIProvisioningHandler: Sent JSON response (" << response_str.length() << " bytes)";
}

void RemoteUIProvisioningHandler::sendErrorResponse(const string &error, int status_code)
{
    Json response;
    response["status"] = "error";
    response["error"] = error;

    sendJsonResponse(response, status_code);
}

string RemoteUIProvisioningHandler::getClientIP() const
{
    return httpClient->getClientIp();
}

bool RemoteUIProvisioningHandler::validateProvisioningRequest(const Json &request) const
{
    if (!request.contains("code") || !request["code"].is_string())
        return false;

    if (!request.contains("device_info") || !request["device_info"].is_object())
        return false;

    const Json &device_info = request["device_info"];

    if (!device_info.contains("type") || !device_info["type"].is_string())
        return false;

    if (!device_info.contains("mac_address") || !device_info["mac_address"].is_string())
        return false;

    return true;
}

Json RemoteUIProvisioningHandler::parseDeviceInfo(const Json &device_info_json) const
{
    // Just return the device info JSON as is
    return device_info_json;
}