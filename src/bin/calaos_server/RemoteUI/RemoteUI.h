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
#ifndef REMOTEUI_H
#define REMOTEUI_H

#include "Utils.h"
#include "libuvw.h"
#include <memory>
#include <chrono>
#include <set>
#include <map>

#include "json.hpp"
using Json = nlohmann::json;

using namespace Utils;

namespace Calaos
{

struct DeviceInfo
{
    string type;
    string manufacturer;
    string model;
    string firmware;
    string mac_address;
    Json capabilities;

    Json toJson() const;
    void fromJson(const Json &j);
};

struct RemoteUIConfig
{
    string name;
    string room;
    string theme;
    int brightness;
    int timeout;
    Json pages;

    Json toJson() const;
    void fromJson(const Json &j);
};

class RemoteUI
{
private:
    string id;
    string provisioning_code;
    string auth_token;
    string device_secret;
    string mac_address;

    DeviceInfo device_info;
    RemoteUIConfig config;

    bool is_online;
    bool is_provisioned;
    std::chrono::system_clock::time_point provisioned_at;
    std::chrono::system_clock::time_point last_seen;

    std::set<string> referenced_ios;

public:
    RemoteUI();
    RemoteUI(const string &provision_code, const DeviceInfo &info);
    ~RemoteUI();

    // Getters
    const string &getId() const { return id; }
    const string &getProvisioningCode() const { return provisioning_code; }
    const string &getAuthToken() const { return auth_token; }
    const string &getDeviceSecret() const { return device_secret; }
    const string &getMacAddress() const { return mac_address; }
    const DeviceInfo &getDeviceInfo() const { return device_info; }
    const RemoteUIConfig &getConfig() const { return config; }

    bool isOnline() const { return is_online; }
    bool isProvisioned() const { return is_provisioned; }

    // Setters
    void setConfig(const RemoteUIConfig &cfg);
    void setOnline(bool online);
    void setDeviceInfo(const DeviceInfo &info);

    // Device secret management
    void generateDeviceSecret();
    bool validateHMAC(const string &token, const string &timestamp, const string &nonce, const string &hmac) const;

    // IO reference management
    void extractReferencedIOs();
    const std::set<string> &getReferencedIOs() const { return referenced_ios; }
    bool hasReferencedIO(const string &io_id) const;

    // Serialization
    Json toJson() const;
    void fromJson(const Json &j);

    // Config response for provisioning
    Json getProvisioningResponse() const;

    // RemoteUI state messages
    Json getRemoteUIIOStatesMessage(const std::map<string, Json> &io_states) const;
    Json getRemoteUIConfigMessage() const;

    void updateLastSeen();

private:
    void generateAuthToken();
    string generateRandomSecret(size_t length = 32) const;
};

}

#endif