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
#ifndef REMOTEUIPROVISIONINGHANDLER_H
#define REMOTEUIPROVISIONINGHANDLER_H

#include "Utils.h"
#include "RemoteUIManager.h"
#include "RemoteUISecurityLimits.h"
#include <set>
#include <map>
#include <ctime>

class HttpClient;

#include "json.hpp"
using Json = nlohmann::json;

using namespace Utils;

namespace Calaos
{

// Structure to track provisioning attempts per IP
struct ProvisioningIPTracking
{
    time_t last_request_time = 0;          // Last provisioning request timestamp
    std::set<string> codes_tried;          // Unique codes tried in the last hour
    time_t codes_window_start = 0;         // Start of the 1-hour tracking window
    time_t blacklist_until = 0;            // Blacklist expiration timestamp
};

class RemoteUIProvisioningHandler
{
private:
    HttpClient *httpClient;

    // IP tracking for rate limiting and brute force detection
    static std::map<string, ProvisioningIPTracking> ip_tracking;

public:
    RemoteUIProvisioningHandler(HttpClient *client);
    ~RemoteUIProvisioningHandler();

    bool canHandleRequest(const string &uri, const string &method) const;
    void processRequest(const string &uri, const string &method,
                       const string &data, const Params &paramsGET);

private:
    void handleProvisionRequest(const string &data);

    void sendJsonResponse(const Json &response, int status_code = 200);
    void sendErrorResponse(const string &error, int status_code = 400);

    string getClientIP() const;
    bool validateProvisioningRequest(const Json &request) const;
    Json parseDeviceInfo(const Json &device_info_json) const;

    // Input validation
    bool validateRequestSize(const string &data) const;
    bool validateDeviceInfo(const Json &device_info) const;

    // Authentication token generation
    string generateAuthToken() const;

    // Rate limiting and brute force protection
    bool checkRateLimitAndBlacklist(const string &client_ip, const string &code);
    void trackProvisioningAttempt(const string &client_ip, const string &code);
    void cleanupExpiredTracking();
};

}

#endif