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
#include "RemoteUISecurityLimits.h"
#include <openssl/rand.h>  // For RAND_bytes()
#include <iomanip>         // For std::setw, std::setfill
#include <sstream>         // For std::ostringstream

static const char *TAG = "remote_ui";

using namespace Calaos;

// Static member definition
std::map<string, ProvisioningIPTracking> RemoteUIProvisioningHandler::ip_tracking;

// Rate limiting and security constants
static const int RATE_LIMIT_SECONDS = 10;           // Min 10s between requests per IP
static const int MAX_CODES_PER_IP = 10;             // Max different codes per IP per hour
static const int TRACKING_WINDOW_SECONDS = 3600;    // 1 hour tracking window
static const int BLACKLIST_DURATION_SECONDS = 3600; // 1 hour blacklist

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

    // Validate request size BEFORE parsing
    if (!validateRequestSize(data))
    {
        sendErrorResponse("Request body too large", 413);
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
    string client_ip = getClientIP();
    Json device_info = parseDeviceInfo(request["device_info"]);

    // Validate device_info fields
    if (!validateDeviceInfo(device_info))
    {
        sendErrorResponse("Device info fields too long", 413);
        return;
    }

    string mac_address = device_info.value("mac_address", "");

    // Check rate limiting and blacklist BEFORE processing
    if (!checkRateLimitAndBlacklist(client_ip, code))
    {
        sendErrorResponse("Too many requests or blacklisted", 429);
        return;
    }

    // Track this provisioning attempt (for code switching detection)
    trackProvisioningAttempt(client_ip, code);

    cInfoDom(TAG) << "RemoteUIProvisioningHandler: Provisioning request for code " << code
            << " from device " << mac_address << " (IP: " << client_ip << ")";

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

    // hardware_id is the real device type identifier
    // it must be selected by the user in Calaos Installer to create the RemoteUI, but the device will update it with the real hardware_id during provisioning
    if (device_info.contains("hardware_id"))
        remote_ui->set_param("device_type", device_info["hardware_id"]);

    if (device_info.contains("manufacturer"))
        remote_ui->set_param("device_manufacturer", device_info["manufacturer"]);

    if (device_info.contains("platform"))
        remote_ui->set_param("device_platform", device_info["platform"]);

    if (device_info.contains("version"))
        remote_ui->set_param("device_version", device_info["version"]);
    if (device_info.contains("mac_address"))
        remote_ui->set_param("mac_address", device_info["mac_address"]);

    // Generate secrets if not already provisioned
    if (remote_ui->get_param("device_secret").empty())
    {
        // Generate cryptographically random auth token (32 bytes = 256 bits)
        string auth_token = generateAuthToken();
        if (auth_token.empty())
        {
            cErrorDom(TAG) << "RemoteUIProvisioningHandler: Failed to generate auth token";
            sendErrorResponse("Internal server error - token generation failed", 500);
            return;
        }

        remote_ui->set_param("auth_token", auth_token);
        remote_ui->generateDeviceSecret();

        cInfoDom(TAG) << "RemoteUIProvisioningHandler: Generated secure auth token for device "
                      << remote_ui->get_param("id");
    }

    // Save configuration
    Config::Instance().SaveConfigIO();

    // Send provisioning response
    Json response = remote_ui->getProvisioningResponse();
    sendJsonResponse(response);

    cInfoDom(TAG) << "RemoteUIManager: Provisioned RemoteUI " << remote_ui->get_param("id");
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

bool RemoteUIProvisioningHandler::checkRateLimitAndBlacklist(const string &client_ip, const string &code)
{
    time_t now = time(nullptr);

    // Cleanup expired tracking data periodically
    cleanupExpiredTracking();

    auto &tracking = ip_tracking[client_ip];

    // Check if IP is blacklisted
    if (tracking.blacklist_until > 0 && now < tracking.blacklist_until)
    {
        int remaining = tracking.blacklist_until - now;
        cWarningDom(TAG) << "RemoteUIProvisioningHandler: Blocked blacklisted IP " << client_ip
                        << " (remaining: " << remaining << "s)";
        return false;
    }

    // Clear blacklist if expired
    if (tracking.blacklist_until > 0 && now >= tracking.blacklist_until)
    {
        cInfoDom(TAG) << "RemoteUIProvisioningHandler: Blacklist expired for IP " << client_ip;
        tracking.blacklist_until = 0;
        tracking.codes_tried.clear();
        tracking.codes_window_start = now;
    }

    // Check rate limiting (10 seconds between requests)
    if (tracking.last_request_time > 0)
    {
        int time_since_last = now - tracking.last_request_time;
        if (time_since_last < RATE_LIMIT_SECONDS)
        {
            cWarningDom(TAG) << "RemoteUIProvisioningHandler: Rate limit exceeded for IP " << client_ip
                            << " (last request " << time_since_last << "s ago, min " << RATE_LIMIT_SECONDS << "s)";
            return false;
        }
    }

    return true;
}

void RemoteUIProvisioningHandler::trackProvisioningAttempt(const string &client_ip, const string &code)
{
    time_t now = time(nullptr);
    auto &tracking = ip_tracking[client_ip];

    // Update last request time
    tracking.last_request_time = now;

    // Initialize tracking window if needed
    if (tracking.codes_window_start == 0)
        tracking.codes_window_start = now;

    // Reset tracking window if it's been more than 1 hour
    if (now - tracking.codes_window_start > TRACKING_WINDOW_SECONDS)
    {
        cDebugDom(TAG) << "RemoteUIProvisioningHandler: Resetting tracking window for IP " << client_ip;
        tracking.codes_tried.clear();
        tracking.codes_window_start = now;
    }

    // Track this code
    size_t previous_size = tracking.codes_tried.size();
    tracking.codes_tried.insert(code);

    // Check if this is a new code (code switching detection)
    if (tracking.codes_tried.size() > previous_size)
    {
        cDebugDom(TAG) << "RemoteUIProvisioningHandler: IP " << client_ip << " tried code '"
                      << code << "' (" << tracking.codes_tried.size() << " unique codes in window)";

        // Detect brute force attack (too many different codes)
        if (tracking.codes_tried.size() > MAX_CODES_PER_IP)
        {
            tracking.blacklist_until = now + BLACKLIST_DURATION_SECONDS;
            cWarningDom(TAG) << "RemoteUIProvisioningHandler: SECURITY ALERT - Blacklisting IP " << client_ip
                            << " for " << (BLACKLIST_DURATION_SECONDS / 60) << " minutes"
                            << " (tried " << tracking.codes_tried.size() << " different codes)";
        }
    }
}

void RemoteUIProvisioningHandler::cleanupExpiredTracking()
{
    time_t now = time(nullptr);
    auto it = ip_tracking.begin();

    while (it != ip_tracking.end())
    {
        auto &tracking = it->second;
        bool is_expired = false;

        // Remove tracking if:
        // 1. Blacklist expired AND tracking window expired AND no recent requests
        if (tracking.blacklist_until == 0 &&
            now - tracking.codes_window_start > TRACKING_WINDOW_SECONDS &&
            now - tracking.last_request_time > TRACKING_WINDOW_SECONDS)
        {
            is_expired = true;
        }

        if (is_expired)
        {
            cDebugDom(TAG) << "RemoteUIProvisioningHandler: Cleaning up expired tracking for IP " << it->first;
            it = ip_tracking.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

string RemoteUIProvisioningHandler::generateAuthToken() const
{
    // Generate 32 bytes (256 bits) of cryptographically secure random data
    const size_t TOKEN_BYTES = 32;
    const size_t MAX_TOKEN_BYTES = 1024;

    if (TOKEN_BYTES > MAX_TOKEN_BYTES)
    {
        cErrorDom(TAG) << "RemoteUIProvisioningHandler: Token size exceeds maximum";
        return "";
    }

    std::vector<unsigned char> buffer(TOKEN_BYTES);
    if (RAND_bytes(buffer.data(), TOKEN_BYTES) != 1)
    {
        cErrorDom(TAG) << "RemoteUIProvisioningHandler: Failed to generate random bytes for auth token";
        return "";
    }

    // Convert to hexadecimal string (64 characters for 32 bytes)
    std::ostringstream oss;
    for (size_t i = 0; i < TOKEN_BYTES; ++i)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[i]);
    }

    return oss.str();
}

bool RemoteUIProvisioningHandler::validateRequestSize(const string &data) const
{
    // Check request body size
    if (data.size() > RemoteUISecurityLimits::MAX_REQUEST_BODY_SIZE)
    {
        cWarningDom(TAG) << "RemoteUIProvisioningHandler: Request body too large ("
                        << data.size() << " bytes, max "
                        << RemoteUISecurityLimits::MAX_REQUEST_BODY_SIZE << ")";
        return false;
    }
    return true;
}

bool RemoteUIProvisioningHandler::validateDeviceInfo(const Json &device_info) const
{
    // Check string field lengths
    const std::vector<string> string_fields = {
        "mac_address", "type", "name", "model", "firmware_version",
        "hardware_version", "manufacturer"
    };

    for (const auto &field : string_fields)
    {
        if (device_info.contains(field) && device_info[field].is_string())
        {
            string value = device_info[field];
            if (value.length() > RemoteUISecurityLimits::MAX_STRING_LENGTH)
            {
                cWarningDom(TAG) << "RemoteUIProvisioningHandler: Device info field '"
                                << field << "' too long ("
                                << value.length() << " chars, max "
                                << RemoteUISecurityLimits::MAX_STRING_LENGTH << ")";
                return false;
            }
        }
    }

    return true;
}