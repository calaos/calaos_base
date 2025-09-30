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
#ifndef HMACAUTHENTICATOR_H
#define HMACAUTHENTICATOR_H

#include "Utils.h"
#include "RemoteUIManager.h"
#include <map>

using namespace Utils;

namespace Calaos
{

class RemoteUI;

struct WebSocketHeaders
{
    string authorization;
    string auth_timestamp;
    string auth_nonce;
    string auth_hmac;
    string user_agent;
    string origin;

    bool parse(const std::map<string, string> &headers);
    bool isValid() const;
};

class HMACAuthenticator
{
public:
    static bool authenticateWebSocketConnection(const WebSocketHeaders &headers,
                                              const string &client_ip,
                                              RemoteUI* &authenticated_remote_ui);

    static bool authenticateHttpRequest(const std::map<string, string> &headers,
                                      const string &client_ip,
                                      RemoteUI* &authenticated_remote_ui);

    static string extractTokenFromBearer(const string &authorization);
    static bool validateTimestamp(const string &timestamp);
    static string generateNonce();

private:
    static const int TIMESTAMP_TOLERANCE_SECONDS = 60;
};

}

#endif