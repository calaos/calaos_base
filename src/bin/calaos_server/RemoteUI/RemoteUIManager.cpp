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
#include "RemoteUIManager.h"
#include "RemoteUIWebSocketHandler.h"
#include "IO/RemoteUI/RemoteUI.h"
#include "ListeRoom.h"
#include "EventManager.h"

static const char *TAG = "remote_ui";

using namespace Calaos;

RemoteUIManager *RemoteUIManager::_instance = nullptr;

RemoteUIManager::RemoteUIManager()
{
    setupTimers();

    cInfoDom(TAG) << "RemoteUIManager: Initialized";
}

RemoteUIManager::~RemoteUIManager()
{
    if (nonce_cleanup_timer)
        nonce_cleanup_timer->stop();
    if (rate_limit_cleanup_timer)
        rate_limit_cleanup_timer->stop();
}

void RemoteUIManager::setupTimers()
{
    auto loop = uvw::Loop::getDefault();

    // Nonce cleanup timer - every 5 minutes
    nonce_cleanup_timer = loop->resource<uvw::TimerHandle>();
    nonce_cleanup_timer->on<uvw::TimerEvent>([this](const uvw::TimerEvent &, uvw::TimerHandle &)
    {
        cleanupExpiredNonces();
    });
    nonce_cleanup_timer->start(uvw::TimerHandle::Time{300000}, uvw::TimerHandle::Time{300000});

    // Rate limit cleanup timer - every minute
    rate_limit_cleanup_timer = loop->resource<uvw::TimerHandle>();
    rate_limit_cleanup_timer->on<uvw::TimerEvent>([this](const uvw::TimerEvent &, uvw::TimerHandle &)
    {
        cleanupRateLimits();
    });
    rate_limit_cleanup_timer->start(uvw::TimerHandle::Time{60000}, uvw::TimerHandle::Time{60000});
}

RemoteUI *RemoteUIManager::getRemoteUI(const string &id)
{
    IOBase *io = ListeRoom::Instance().get_io(id);
    if (!io)
        return nullptr;

    return dynamic_cast<RemoteUI*>(io);
}

RemoteUI *RemoteUIManager::getRemoteUIByToken(const string &token)
{
    // Search all IOs for RemoteUI with matching auth_token
    for (int i = 0; i < ListeRoom::Instance().size(); i++)
    {
        Room *room = ListeRoom::Instance()[i];
        for (int j = 0; j < room->get_size(); j++)
        {
            IOBase *io = room->get_io(j);
            if (io->get_param("type") == "RemoteUI" ||
                io->get_param("type") == "remote_ui_output")
            {
                if (io->get_param("auth_token") == token)
                {
                    return dynamic_cast<RemoteUI*>(io);
                }
            }
        }
    }

    return nullptr;
}

std::vector<RemoteUI*> RemoteUIManager::getAllRemoteUIs()
{
    std::vector<RemoteUI*> outputs;

    for (int i = 0; i < ListeRoom::Instance().size(); i++)
    {
        Room *room = ListeRoom::Instance()[i];
        for (int j = 0; j < room->get_size(); j++)
        {
            IOBase *io = room->get_io(j);
            if (io->get_param("type") == "RemoteUI" ||
                io->get_param("type") == "remote_ui_output")
            {
                RemoteUI *remoteui_output = dynamic_cast<RemoteUI*>(io);
                if (remoteui_output)
                    outputs.push_back(remoteui_output);
            }
        }
    }

    return outputs;
}

bool RemoteUIManager::validateAuthentication(const string &token, const string &timestamp,
                                           const string &nonce, const string &hmac,
                                           const string &ip_address)
{
    // Check rate limiting
    if (!checkRateLimit(ip_address))
    {
        cWarningDom(TAG) << "RemoteUIManager: Rate limit exceeded for IP " << ip_address;
        return false;
    }

    // Check if nonce was already used
    if (isNonceUsed(nonce))
    {
        cWarningDom(TAG) << "RemoteUIManager: Nonce reuse attempt from IP " << ip_address;
        return false;
    }

    auto now = std::chrono::system_clock::now();

    try
    {
        auto timestamp_val = std::stoull(timestamp);
        auto request_time = std::chrono::system_clock::from_time_t(timestamp_val);
        auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(now - request_time).count();

        if (std::abs(time_diff) > TIMESTAMP_TOLERANCE_SECONDS)
        {
            cWarningDom(TAG) << "RemoteUIManager: Timestamp too old/new from IP " << ip_address;
            return false;
        }
    }
    catch (const std::exception &e)
    {
        cWarningDom(TAG) << "RemoteUIManager: Invalid timestamp format from IP " << ip_address;
        return false;
    }

    // Get RemoteUI and validate HMAC
    RemoteUI *remote_ui = getRemoteUIByToken(token);
    if (!remote_ui)
    {
        cWarningDom(TAG) << "RemoteUIManager: Unknown token from IP " << ip_address;
        return false;
    }

    if (!remote_ui->validateHMAC(token, timestamp, nonce, hmac))
    {
        cWarningDom(TAG) << "RemoteUIManager: HMAC validation failed for " << remote_ui->get_param("id");
        return false;
    }

    // Add nonce to prevent replay
    addNonce(nonce, ip_address);

    cDebugDom(TAG) << "RemoteUIManager: Authentication successful for " << remote_ui->get_param("id");
    return true;
}

bool RemoteUIManager::checkRateLimit(const string &ip_address)
{
    auto now = std::chrono::system_clock::now();
    auto it = rate_limit_map.find(ip_address);

    if (it == rate_limit_map.end())
    {
        rate_limit_map[ip_address] = RateLimitEntry();
        return true;
    }

    auto &entry = it->second;
    auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(now - entry.window_start).count();

    if (time_diff >= 60) // New minute window
    {
        entry.window_start = now;
        entry.attempts = 1;
        return true;
    }

    if (entry.attempts >= MAX_ATTEMPTS_PER_MINUTE)
        return false;

    entry.attempts++;
    return true;
}

void RemoteUIManager::addNonce(const string &nonce, const string &ip_address)
{
    nonce_cache[nonce] = NonceEntry(ip_address);
}

bool RemoteUIManager::isNonceUsed(const string &nonce) const
{
    return nonce_cache.find(nonce) != nonce_cache.end();
}

void RemoteUIManager::cleanupExpiredNonces()
{
    auto now = std::chrono::system_clock::now();
    auto it = nonce_cache.begin();

    while (it != nonce_cache.end())
    {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.created_at).count();
        if (age > NONCE_EXPIRY_SECONDS)
        {
            it = nonce_cache.erase(it);
        }
        else
        {
            ++it;
        }
    }

    cDebugDom(TAG) << "RemoteUIManager: Cleaned up expired nonces, " << nonce_cache.size() << " remaining";
}

void RemoteUIManager::cleanupRateLimits()
{
    auto now = std::chrono::system_clock::now();
    auto it = rate_limit_map.begin();

    while (it != rate_limit_map.end())
    {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.window_start).count();
        if (age > 120) // Keep for 2 minutes after window
        {
            it = rate_limit_map.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void RemoteUIManager::notifyAllIOStates()
{
    for (const auto &handler_pair : connected_handlers)
    {
        const string &remote_ui_id = handler_pair.first;
        RemoteUIWebSocketHandler *handler = handler_pair.second;

        RemoteUI *remote_ui = getRemoteUI(remote_ui_id);
        if (remote_ui && remote_ui->isOnline())
        {
            handler->sendInitialIOStates();
            cDebugDom(TAG) << "RemoteUIManager: Sent all IO states to " << remote_ui_id;
        }
    }
}

size_t RemoteUIManager::getOnlineCount() const
{
    size_t count = 0;
    auto remote_uis = const_cast<RemoteUIManager*>(this)->getAllRemoteUIs();
    for (RemoteUI *remote_ui : remote_uis)
    {
        if (remote_ui->isOnline())
            count++;
    }
    return count;
}

size_t RemoteUIManager::getTotalCount() const
{
    return const_cast<RemoteUIManager*>(this)->getAllRemoteUIs().size();
}

void RemoteUIManager::addWebSocketHandler(const string &remote_ui_id, RemoteUIWebSocketHandler *handler)
{
    connected_handlers[remote_ui_id] = handler;
    RemoteUI *remote_ui = getRemoteUI(remote_ui_id);
    if (remote_ui)
    {
        remote_ui->setOnline(true);
        cInfoDom(TAG) << "RemoteUIManager: WebSocket handler connected for " << remote_ui_id;
    }
}

void RemoteUIManager::removeWebSocketHandler(const string &remote_ui_id)
{
    connected_handlers.erase(remote_ui_id);
    RemoteUI *remote_ui = getRemoteUI(remote_ui_id);
    if (remote_ui)
    {
        remote_ui->setOnline(false);
        cInfoDom(TAG) << "RemoteUIManager: WebSocket handler disconnected for " << remote_ui_id;
    }
}
