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
#include "OtaHttpHandler.h"
#include "HttpClient.h"
#include "OtaFirmwareManager.h"
#include "HMACAuthenticator.h"
#include "IO/RemoteUI/RemoteUI.h"
#include "FileUtils.h"
#include <fstream>

static const char *TAG = "ota";

using namespace Calaos;

OtaHttpHandler::OtaHttpHandler(HttpClient *client):
    httpClient(client)
{
}

OtaHttpHandler::~OtaHttpHandler()
{
}

bool OtaHttpHandler::canHandleRequest(const string &uri, const string &method) const
{
    // Handle OTA endpoints
    if (uri.find("/api/v3/ota/") == 0)
        return true;

    return false;
}

void OtaHttpHandler::processRequest(const string &uri, const string &method,
                                    const string &data, const Params &paramsGET)
{
    cDebugDom(TAG) << "OtaHttpHandler: Processing " << method << " " << uri;

    // Check if OTA is enabled
    if (!OtaFirmwareManager::Instance().isEnabled())
    {
        sendErrorResponse("OTA_DISABLED", "OTA firmware updates are disabled", 503);
        return;
    }

    try
    {
        // POST /api/v3/ota/rescan - localhost only
        if (uri == "/api/v3/ota/rescan" && method == "POST")
        {
            handleRescan();
            return;
        }

        // GET /api/v3/ota/firmware/{hardware_id}/download - requires HMAC auth
        if (uri.find("/api/v3/ota/firmware/") == 0 &&
            uri.find("/download") != string::npos &&
            method == "GET")
        {
            // Extract hardware_id from URL
            // URL format: /api/v3/ota/firmware/{hardware_id}/download
            string prefix = "/api/v3/ota/firmware/";
            string suffix = "/download";

            size_t startPos = prefix.length();
            size_t endPos = uri.find(suffix);

            if (endPos == string::npos || endPos <= startPos)
            {
                sendErrorResponse("INVALID_URL", "Invalid firmware download URL", 400);
                return;
            }

            string hardwareId = uri.substr(startPos, endPos - startPos);

            // Validate hardware_id (prevent path traversal)
            if (hardwareId.find('/') != string::npos ||
                hardwareId.find("..") != string::npos ||
                hardwareId.empty())
            {
                sendErrorResponse("INVALID_HARDWARE_ID", "Invalid hardware ID", 400);
                return;
            }

            handleFirmwareDownload(hardwareId);
            return;
        }

        sendErrorResponse("NOT_FOUND", "Endpoint not found", 404);
    }
    catch (const std::exception &e)
    {
        cErrorDom(TAG) << "OtaHttpHandler: Exception processing request: " << e.what();
        sendErrorResponse("INTERNAL_ERROR", "Internal server error", 500);
    }
}

void OtaHttpHandler::handleFirmwareDownload(const string &hardwareId)
{
    // Authenticate request using HMAC
    if (!authenticateRequest())
    {
        sendErrorResponse("UNAUTHORIZED", "Authentication failed", 401);
        return;
    }

    // Get firmware from cache
    const FirmwareManifest *firmware = OtaFirmwareManager::Instance().getFirmware(hardwareId);
    if (!firmware)
    {
        sendErrorResponse("FIRMWARE_NOT_FOUND",
                         "No firmware found for hardware ID: " + hardwareId, 404);
        return;
    }

    string firmwarePath = firmware->getFirmwarePath();
    if (!FileUtils::exists(firmwarePath))
    {
        cErrorDom(TAG) << "Firmware file missing: " << firmwarePath;
        sendErrorResponse("FIRMWARE_FILE_MISSING",
                         "Firmware file not found on server", 500);
        return;
    }

    cInfoDom(TAG) << "Serving firmware download: " << hardwareId
                  << " v" << firmware->getVersion()
                  << " to " << getClientIP();

    sendFirmwareFile(firmwarePath, firmware->getChecksumSha256(), firmware->getFirmwareFile());
}

void OtaHttpHandler::handleRescan()
{
    // Rescan is only allowed from localhost
    if (!isLocalhost())
    {
        cWarningDom(TAG) << "Rescan request rejected from non-localhost: " << getClientIP();
        sendErrorResponse("FORBIDDEN", "Rescan only allowed from localhost", 403);
        return;
    }

    cInfoDom(TAG) << "Firmware rescan triggered via HTTP from " << getClientIP();

    OtaFirmwareManager::Instance().rescan();

    Json response;
    response["success"] = true;
    response["message"] = "Firmware rescan completed";
    response["firmware_count"] = OtaFirmwareManager::Instance().getAllFirmwares().size();

    sendJsonResponse(response);
}

void OtaHttpHandler::sendJsonResponse(const Json &response, int status_code)
{
    string response_str = response.dump();

    string statusText;
    switch (status_code)
    {
        case 200: statusText = "OK"; break;
        case 400: statusText = "Bad Request"; break;
        case 401: statusText = "Unauthorized"; break;
        case 403: statusText = "Forbidden"; break;
        case 404: statusText = "Not Found"; break;
        case 500: statusText = "Internal Server Error"; break;
        case 503: statusText = "Service Unavailable"; break;
        default: statusText = "Error"; break;
    }

    string http_response = "HTTP/1.1 " + Utils::to_string(status_code) + " " + statusText + "\r\n" +
                          "Content-Type: application/json\r\n" +
                          "Content-Length: " + Utils::to_string(response_str.length()) + "\r\n" +
                          "Connection: close\r\n\r\n" + response_str;
    httpClient->sendToClient(http_response);

    cDebugDom(TAG) << "OtaHttpHandler: Sent JSON response (" << response_str.length() << " bytes)";
}

void OtaHttpHandler::sendErrorResponse(const string &code, const string &message, int status_code)
{
    Json response;
    response["success"] = false;
    response["error"]["code"] = code;
    response["error"]["message"] = message;

    sendJsonResponse(response, status_code);
}

void OtaHttpHandler::sendFirmwareFile(const string &filePath, const string &checksum, const string &filename)
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        sendErrorResponse("FILE_READ_ERROR", "Failed to read firmware file", 500);
        return;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Read file content
    std::vector<char> buffer(fileSize);
    if (!file.read(buffer.data(), fileSize))
    {
        sendErrorResponse("FILE_READ_ERROR", "Failed to read firmware file", 500);
        return;
    }
    file.close();

    // Build HTTP response headers
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: application/octet-stream\r\n";
    response << "Content-Length: " << fileSize << "\r\n";
    response << "Content-Disposition: attachment; filename=\"" << filename << "\"\r\n";
    response << "X-Checksum-SHA256: " << checksum << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";

    string headers = response.str();

    // Send headers + body
    string fullResponse = headers + string(buffer.begin(), buffer.end());
    httpClient->sendToClient(fullResponse);

    cInfoDom(TAG) << "OtaHttpHandler: Sent firmware file (" << fileSize << " bytes)";
}

string OtaHttpHandler::getClientIP() const
{
    return httpClient->getClientIp();
}

bool OtaHttpHandler::isLocalhost() const
{
    string ip = getClientIP();
    return (ip == "127.0.0.1" ||
            ip == "::1" ||
            ip == "localhost" ||
            ip.find("127.0.0.") == 0);
}

bool OtaHttpHandler::authenticateRequest()
{
    // Get request headers from HttpClient
    const auto &headers = httpClient->getRequestHeaders();

    // Convert to map<string, string> for HMACAuthenticator
    std::map<string, string> headerMap(headers.begin(), headers.end());

    RemoteUI *authenticated_remote_ui = nullptr;
    string client_ip = getClientIP();

    if (!HMACAuthenticator::authenticateHttpRequest(headerMap, client_ip, authenticated_remote_ui))
    {
        cWarningDom(TAG) << "OtaHttpHandler: HMAC authentication failed from " << client_ip;
        return false;
    }

    cDebugDom(TAG) << "OtaHttpHandler: HMAC authentication successful for "
                   << (authenticated_remote_ui ? authenticated_remote_ui->get_param("id") : "unknown");
    return true;
}
