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

class HttpClient;

#include "json.hpp"
using Json = nlohmann::json;

using namespace Utils;

namespace Calaos
{

class RemoteUIProvisioningHandler
{
private:
    HttpClient *httpClient;

public:
    RemoteUIProvisioningHandler(HttpClient *client);
    ~RemoteUIProvisioningHandler();

    bool canHandleRequest(const string &uri, const string &method) const;
    void processRequest(const string &uri, const string &method,
                       const string &data, const Params &paramsGET);

private:
    void handleProvisionRequest(const string &data);
    void handleRemoteUIList();
    void handleRemoteUIStatus(const string &remote_ui_id);

    void sendJsonResponse(const Json &response, int status_code = 200);
    void sendErrorResponse(const string &error, int status_code = 400);

    string getClientIP() const;
    bool validateProvisioningRequest(const Json &request) const;
    DeviceInfo parseDeviceInfo(const Json &device_info_json) const;
};

}

#endif