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
#ifndef REMOTEUIMANAGER_H
#define REMOTEUIMANAGER_H

#include "Utils.h"
#include "libuvw.h"
#include <map>
#include <memory>
#include <set>
#include <chrono>

#include "EventManager.h"

namespace Calaos {
class RemoteUIWebSocketHandler;
class RemoteUI;
}

#include "json.hpp"
using Json = nlohmann::json;

using namespace Utils;

namespace Calaos
{

struct NonceEntry
{
    std::chrono::system_clock::time_point created_at;
    string ip_address;

    NonceEntry() : created_at(std::chrono::system_clock::now()), ip_address("") {}
    NonceEntry(const string &ip) :
        created_at(std::chrono::system_clock::now()),
        ip_address(ip) {}
};

struct RateLimitEntry
{
    std::chrono::system_clock::time_point window_start;
    int attempts;

    RateLimitEntry() :
        window_start(std::chrono::system_clock::now()),
        attempts(1) {}
};

class RemoteUIManager
{
private:
    static RemoteUIManager *_instance;

    std::map<string, RemoteUIWebSocketHandler*> connected_handlers;

    // Security
    std::map<string, NonceEntry> nonce_cache;
    std::map<string, RateLimitEntry> rate_limit_map;

    // Timers for cleanup
    std::shared_ptr<uvw::TimerHandle> nonce_cleanup_timer;
    std::shared_ptr<uvw::TimerHandle> rate_limit_cleanup_timer;

    // EventManager connection
    sigc::connection event_connection;

    RemoteUIManager();

public:
    static RemoteUIManager &Instance()
    {
        if (!_instance)
            _instance = new RemoteUIManager();
        return *_instance;
    }

    ~RemoteUIManager();

    // RemoteUI IO discovery (via ListeRoom)
    RemoteUI *getRemoteUI(const string &id);
    RemoteUI *getRemoteUIByToken(const string &token);
    std::vector<RemoteUI*> getAllRemoteUIs();

    // Authentication
    bool validateAuthentication(const string &token, const string &timestamp,
                              const string &nonce, const string &hmac,
                              const string &ip_address);

    // IO state notifications
    void notifyIOStateChange(const string &io_id, const Json &io_data);
    void notifyAllIOStates();

    // Security
    bool checkRateLimit(const string &ip_address);
    void addNonce(const string &nonce, const string &ip_address);
    bool isNonceUsed(const string &nonce) const;

    // WebSocket handler management
    void addWebSocketHandler(const string &remote_ui_id, RemoteUIWebSocketHandler *handler);
    void removeWebSocketHandler(const string &remote_ui_id);

    // Statistics
    size_t getOnlineCount() const;
    size_t getTotalCount() const;

private:
    void setupTimers();
    void cleanupExpiredNonces();
    void cleanupRateLimits();
    void handleIOEvent(const CalaosEvent &event);

    static const int MAX_ATTEMPTS_PER_MINUTE = 3;
    static const int NONCE_EXPIRY_SECONDS = 300; // 5 minutes
    static const int TIMESTAMP_TOLERANCE_SECONDS = 60;
};

}

#endif