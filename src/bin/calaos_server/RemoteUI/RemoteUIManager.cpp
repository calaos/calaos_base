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
#include "RemoteUIConfig.h"
#include "CalaosConfig.h"
#include "FileUtils.h"
#include "Timer.h"
#include "RemoteUIWebSocketHandler.h"
#include "ListeRoom.h"
#include "EventManager.h"

using namespace Calaos;

RemoteUIManager *RemoteUIManager::_instance = nullptr;

RemoteUIManager::RemoteUIManager()
{
    setupTimers();
    
    event_connection = EventManager::Instance().newEvent.connect(
        sigc::mem_fun(*this, &RemoteUIManager::handleIOEvent)
    );

    cInfo() << "RemoteUIManager: Initialized";
}

RemoteUIManager::~RemoteUIManager()
{
    if (nonce_cleanup_timer)
        nonce_cleanup_timer->stop();
    if (rate_limit_cleanup_timer)
        rate_limit_cleanup_timer->stop();
    
    event_connection.disconnect();
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

void RemoteUIManager::addRemoteUI(std::shared_ptr<RemoteUI> remote_ui)
{
    if (!remote_ui)
        return;

    remote_uis[remote_ui->getId()] = remote_ui;
    updateIndices(remote_ui);

    cInfo() << "RemoteUIManager: Added RemoteUI " << remote_ui->getId();
}

void RemoteUIManager::removeRemoteUI(const string &id)
{
    auto it = remote_uis.find(id);
    if (it != remote_uis.end())
    {
        removeFromIndices(it->second);
        remote_uis.erase(it);
        cInfo() << "RemoteUIManager: Removed RemoteUI " << id;
    }
}

std::shared_ptr<RemoteUI> RemoteUIManager::getRemoteUI(const string &id)
{
    auto it = remote_uis.find(id);
    return (it != remote_uis.end()) ? it->second : nullptr;
}

std::shared_ptr<RemoteUI> RemoteUIManager::getRemoteUIByCode(const string &code)
{
    auto it = remote_uis_by_code.find(code);
    return (it != remote_uis_by_code.end()) ? it->second : nullptr;
}

std::shared_ptr<RemoteUI> RemoteUIManager::getRemoteUIByToken(const string &token)
{
    auto it = remote_uis_by_token.find(token);
    return (it != remote_uis_by_token.end()) ? it->second : nullptr;
}

void RemoteUIManager::updateIndices(std::shared_ptr<RemoteUI> remote_ui)
{
    if (!remote_ui)
        return;

    remote_uis_by_code[remote_ui->getProvisioningCode()] = remote_ui;
    remote_uis_by_token[remote_ui->getAuthToken()] = remote_ui;
}

void RemoteUIManager::removeFromIndices(std::shared_ptr<RemoteUI> remote_ui)
{
    if (!remote_ui)
        return;

    remote_uis_by_code.erase(remote_ui->getProvisioningCode());
    remote_uis_by_token.erase(remote_ui->getAuthToken());
}

Json RemoteUIManager::processProvisioningRequest(const string &code, const DeviceInfo &device_info)
{
    auto remote_ui = getRemoteUIByCode(code);
    if (!remote_ui)
    {
        Json error_response;
        error_response["status"] = "unknown";
        error_response["error"] = "Code not found";
        return error_response;
    }

    // Update device info and mark as provisioned
    remote_ui->setDeviceInfo(device_info);

    cInfo() << "RemoteUIManager: Provisioned RemoteUI " << remote_ui->getId()
            << " with device " << device_info.mac_address;

    return remote_ui->getProvisioningResponse();
}

bool RemoteUIManager::validateAuthentication(const string &token, const string &timestamp,
                                           const string &nonce, const string &hmac,
                                           const string &ip_address)
{
    // Check rate limiting
    if (!checkRateLimit(ip_address))
    {
        cWarning() << "RemoteUIManager: Rate limit exceeded for IP " << ip_address;
        return false;
    }

    // Check if nonce was already used
    if (isNonceUsed(nonce))
    {
        cWarning() << "RemoteUIManager: Nonce reuse attempt from IP " << ip_address;
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
            cWarning() << "RemoteUIManager: Timestamp too old/new from IP " << ip_address;
            return false;
        }
    }
    catch (const std::exception &e)
    {
        cWarning() << "RemoteUIManager: Invalid timestamp format from IP " << ip_address;
        return false;
    }

    // Get RemoteUI and validate HMAC
    auto remote_ui = getRemoteUIByToken(token);
    if (!remote_ui)
    {
        cWarning() << "RemoteUIManager: Unknown token from IP " << ip_address;
        return false;
    }

    if (!remote_ui->validateHMAC(token, timestamp, nonce, hmac))
    {
        cWarning() << "RemoteUIManager: HMAC validation failed for " << remote_ui->getId();
        return false;
    }

    // Add nonce to prevent replay
    addNonce(nonce, ip_address);

    cDebug() << "RemoteUIManager: Authentication successful for " << remote_ui->getId();
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

    cDebug() << "RemoteUIManager: Cleaned up expired nonces, " << nonce_cache.size() << " remaining";
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

void RemoteUIManager::notifyIOStateChange(const string &io_id, const Json &io_data)
{
    Json message;
    message["msg"] = "io_state";
    message["data"]["io_id"] = io_id;
    message["data"]["state"] = io_data;

    for (const auto &pair : remote_uis)
    {
        auto remote_ui = pair.second;
        if (remote_ui->isOnline() && remote_ui->hasReferencedIO(io_id))
        {
            auto handler_it = connected_handlers.find(remote_ui->getId());
            if (handler_it != connected_handlers.end())
            {
                handler_it->second->sendMessage(message);
                cDebug() << "RemoteUIManager: Notified " << remote_ui->getId()
                        << " of IO state change for " << io_id;
            }
        }
    }
}

void RemoteUIManager::notifyAllIOStates()
{
    for (const auto &handler_pair : connected_handlers)
    {
        const string &remote_ui_id = handler_pair.first;
        RemoteUIWebSocketHandler *handler = handler_pair.second;
        
        auto remote_ui = getRemoteUI(remote_ui_id);
        if (remote_ui && remote_ui->isOnline())
        {
            handler->sendInitialIOStates();
            cDebug() << "RemoteUIManager: Sent all IO states to " << remote_ui_id;
        }
    }
}

size_t RemoteUIManager::getOnlineCount() const
{
    size_t count = 0;
    for (const auto &pair : remote_uis)
    {
        if (pair.second->isOnline())
            count++;
    }
    return count;
}

size_t RemoteUIManager::getTotalCount() const
{
    return remote_uis.size();
}

void RemoteUIManager::loadFromConfig()
{
    // Load RemoteUIs from XML configuration file
    string file = Utils::getConfigFile(IO_CONFIG);
    if (!Utils::fileExists(file))
    {
        cInfo() << "RemoteUIManager: Config file " << file << " does not exist";
        return;
    }

    TiXmlDocument document(file);
    if (!document.LoadFile())
    {
        cError() << "RemoteUIManager: Unable to load XML file " << file;
        return;
    }

    auto loaded_remote_uis = RemoteUIConfigParser::parseRemoteUIsFromConfig(&document);

    for (auto remote_ui : loaded_remote_uis)
    {
        addRemoteUI(remote_ui);
    }

    cInfo() << "RemoteUIManager: Loaded " << loaded_remote_uis.size() << " RemoteUIs from config";
}

void RemoteUIManager::saveToConfig() const
{
    string file = Utils::getConfigFile(IO_CONFIG);

    TiXmlDocument document(file);
    if (!document.LoadFile())
    {
        cError() << "RemoteUIManager: Unable to load XML file " << file << " for saving";
        return;
    }

    // Convert map to vector for saving
    std::vector<std::shared_ptr<RemoteUI>> remote_ui_vector;
    for (const auto &pair : remote_uis)
    {
        remote_ui_vector.push_back(pair.second);
    }

    if (!RemoteUIConfigParser::saveRemoteUIsToConfig(&document, remote_ui_vector))
    {
        cError() << "RemoteUIManager: Failed to save RemoteUIs to config";
        return;
    }

    if (!document.SaveFile())
    {
        cError() << "RemoteUIManager: Unable to save XML file " << file;
        return;
    }

    cInfo() << "RemoteUIManager: Saved " << remote_ui_vector.size() << " RemoteUIs to config";
}

void RemoteUIManager::addWebSocketHandler(const string &remote_ui_id, RemoteUIWebSocketHandler *handler)
{
    connected_handlers[remote_ui_id] = handler;
    auto remote_ui = getRemoteUI(remote_ui_id);
    if (remote_ui)
    {
        remote_ui->setOnline(true);
        cInfo() << "RemoteUIManager: WebSocket handler connected for " << remote_ui_id;
    }
}

void RemoteUIManager::removeWebSocketHandler(const string &remote_ui_id)
{
    connected_handlers.erase(remote_ui_id);
    auto remote_ui = getRemoteUI(remote_ui_id);
    if (remote_ui)
    {
        remote_ui->setOnline(false);
        cInfo() << "RemoteUIManager: WebSocket handler disconnected for " << remote_ui_id;
    }
}

void RemoteUIManager::handleIOEvent(const CalaosEvent &event)
{
    if (event.getType() == CalaosEvent::EventIOChanged)
    {
        string io_id = event.getParam().get_param_const("id");
        string state = event.getParam().get_param_const("state");
        
        Json io_data;
        io_data["state"] = state;
        
        notifyIOStateChange(io_id, io_data);
    }
}