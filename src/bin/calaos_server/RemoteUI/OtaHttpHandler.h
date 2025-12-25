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
#ifndef OTAHTTPHANDLER_H
#define OTAHTTPHANDLER_H

#include "Utils.h"
#include "json.hpp"

class HttpClient;

using Json = nlohmann::json;
using namespace Utils;

namespace Calaos
{

class RemoteUI;

class OtaHttpHandler
{
private:
    HttpClient *httpClient;

public:
    OtaHttpHandler(HttpClient *client);
    ~OtaHttpHandler();

    bool canHandleRequest(const string &uri, const string &method) const;
    void processRequest(const string &uri, const string &method,
                       const string &data, const Params &paramsGET);

private:
    void handleFirmwareDownload(const string &hardwareId);
    void handleRescan();

    void sendJsonResponse(const Json &response, int status_code = 200);
    void sendErrorResponse(const string &code, const string &message, int status_code = 400);
    void sendFirmwareFile(const string &filePath, const string &checksum, const string &filename);

    string getClientIP() const;
    bool isLocalhost() const;
    bool authenticateRequest();
};

}

#endif
