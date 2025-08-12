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
#include "HMACAuthenticator.h"
#include <openssl/rand.h>
#include <iomanip>
#include <sstream>
#include <chrono>

using namespace Calaos;

bool WebSocketHeaders::parse(const std::map<string, string> &headers)
{
    auto findHeader = [&headers](const string &key) -> string
    {
        // Try different case variations
        auto it = headers.find(key);
        if (it != headers.end())
            return it->second;
            
        string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
        it = headers.find(lower_key);
        if (it != headers.end())
            return it->second;
            
        return "";
    };
    
    authorization = findHeader("Authorization");
    auth_timestamp = findHeader("X-Auth-Timestamp");
    auth_nonce = findHeader("X-Auth-Nonce");
    auth_hmac = findHeader("X-Auth-HMAC");
    user_agent = findHeader("User-Agent");
    origin = findHeader("Origin");
    
    return isValid();
}

bool WebSocketHeaders::isValid() const
{
    return !authorization.empty() && 
           !auth_timestamp.empty() && 
           !auth_nonce.empty() && 
           !auth_hmac.empty();
}

bool HMACAuthenticator::authenticateWebSocketConnection(const WebSocketHeaders &headers,
                                                      const string &client_ip,
                                                      std::shared_ptr<RemoteUI> &authenticated_remote_ui)
{
    if (!headers.isValid())
    {
        cWarning() << "HMACAuthenticator: Invalid headers in WebSocket auth";
        return false;
    }
    
    string token = extractTokenFromBearer(headers.authorization);
    if (token.empty())
    {
        cWarning() << "HMACAuthenticator: Invalid Authorization header";
        return false;
    }
    
    if (!validateTimestamp(headers.auth_timestamp))
    {
        cWarning() << "HMACAuthenticator: Invalid timestamp";
        return false;
    }
    
    bool auth_result = RemoteUIManager::Instance().validateAuthentication(
        token, headers.auth_timestamp, headers.auth_nonce, headers.auth_hmac, client_ip
    );
    
    if (auth_result)
    {
        authenticated_remote_ui = RemoteUIManager::Instance().getRemoteUIByToken(token);
        if (authenticated_remote_ui)
        {
            authenticated_remote_ui->setOnline(true);
            cInfo() << "HMACAuthenticator: WebSocket authentication successful for " 
                   << authenticated_remote_ui->getId();
        }
    }
    
    return auth_result;
}

bool HMACAuthenticator::authenticateHttpRequest(const std::map<string, string> &headers,
                                              const string &client_ip,
                                              std::shared_ptr<RemoteUI> &authenticated_remote_ui)
{
    auto findHeader = [&headers](const string &key) -> string
    {
        auto it = headers.find(key);
        if (it != headers.end())
            return it->second;
        
        string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
        it = headers.find(lower_key);
        if (it != headers.end())
            return it->second;
            
        return "";
    };
    
    string authorization = findHeader("Authorization");
    string timestamp = findHeader("X-Auth-Timestamp");
    string nonce = findHeader("X-Auth-Nonce");
    string hmac = findHeader("X-Auth-HMAC");
    
    if (authorization.empty() || timestamp.empty() || nonce.empty() || hmac.empty())
    {
        cWarning() << "HMACAuthenticator: Missing required headers in HTTP auth";
        return false;
    }
    
    string token = extractTokenFromBearer(authorization);
    if (token.empty())
    {
        cWarning() << "HMACAuthenticator: Invalid Authorization header in HTTP auth";
        return false;
    }
    
    if (!validateTimestamp(timestamp))
    {
        cWarning() << "HMACAuthenticator: Invalid timestamp in HTTP auth";
        return false;
    }
    
    bool auth_result = RemoteUIManager::Instance().validateAuthentication(
        token, timestamp, nonce, hmac, client_ip
    );
    
    if (auth_result)
    {
        authenticated_remote_ui = RemoteUIManager::Instance().getRemoteUIByToken(token);
        if (authenticated_remote_ui)
        {
            authenticated_remote_ui->updateLastSeen();
            cDebug() << "HMACAuthenticator: HTTP authentication successful for " 
                    << authenticated_remote_ui->getId();
        }
    }
    
    return auth_result;
}

string HMACAuthenticator::extractTokenFromBearer(const string &authorization)
{
    const string bearer_prefix = "Bearer ";
    if (authorization.length() > bearer_prefix.length() && 
        authorization.substr(0, bearer_prefix.length()) == bearer_prefix)
    {
        return authorization.substr(bearer_prefix.length());
    }
    
    return "";
}

bool HMACAuthenticator::validateTimestamp(const string &timestamp)
{
    try
    {
        auto timestamp_val = std::stoull(timestamp);
        auto request_time = std::chrono::system_clock::from_time_t(timestamp_val);
        auto now = std::chrono::system_clock::now();
        auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(now - request_time).count();
        
        return std::abs(time_diff) <= TIMESTAMP_TOLERANCE_SECONDS;
    }
    catch (const std::exception &e)
    {
        cWarning() << "HMACAuthenticator: Invalid timestamp format: " << e.what();
        return false;
    }
}

string HMACAuthenticator::generateNonce()
{
    unsigned char buffer[8];
    if (RAND_bytes(buffer, sizeof(buffer)) != 1)
    {
        cError() << "HMACAuthenticator: Failed to generate random nonce";
        return "";
    }
    
    std::ostringstream oss;
    for (size_t i = 0; i < sizeof(buffer); ++i)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[i]);
    }
    
    return oss.str();
}