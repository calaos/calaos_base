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
#include "RemoteUI.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <iomanip>
#include <sstream>
#include <random>

using namespace Calaos;

Json DeviceInfo::toJson() const
{
    Json j;
    j["type"] = type;
    j["manufacturer"] = manufacturer;
    j["model"] = model;
    j["firmware"] = firmware;
    j["mac_address"] = mac_address;
    j["capabilities"] = capabilities;
    return j;
}

void DeviceInfo::fromJson(const Json &j)
{
    type = j.value("type", "");
    manufacturer = j.value("manufacturer", "");
    model = j.value("model", "");
    firmware = j.value("firmware", "");
    mac_address = j.value("mac_address", "");
    if (j.contains("capabilities"))
        capabilities = j["capabilities"];
}

Json RemoteUIConfig::toJson() const
{
    Json j;
    j["name"] = name;
    j["room"] = room;
    j["theme"] = theme;
    j["brightness"] = brightness;
    j["timeout"] = timeout;
    j["pages"] = pages;
    return j;
}

void RemoteUIConfig::fromJson(const Json &j)
{
    name = j.value("name", "");
    room = j.value("room", "");
    theme = j.value("theme", "dark");
    brightness = j.value("brightness", 80);
    timeout = j.value("timeout", 300);
    if (j.contains("pages"))
        pages = j["pages"];
}

RemoteUI::RemoteUI():
    is_online(false),
    is_provisioned(false)
{
}

RemoteUI::RemoteUI(const string &provision_code, const DeviceInfo &info):
    provisioning_code(provision_code),
    device_info(info),
    is_online(false),
    is_provisioned(false)
{
    id = "remote_ui_" + provision_code;
    generateAuthToken();
    generateDeviceSecret();
    mac_address = info.mac_address;
    provisioned_at = std::chrono::system_clock::now();
}

RemoteUI::~RemoteUI()
{
}

void RemoteUI::setConfig(const RemoteUIConfig &cfg)
{
    config = cfg;
    extractReferencedIOs();
}

void RemoteUI::setOnline(bool online)
{
    is_online = online;
    if (online)
        updateLastSeen();
}

void RemoteUI::setDeviceInfo(const DeviceInfo &info)
{
    device_info = info;
    if (mac_address.empty())
        mac_address = info.mac_address;
}

void RemoteUI::generateAuthToken()
{
    auth_token = id;
}

string RemoteUI::generateRandomSecret(size_t length) const
{
    const size_t MAX_SECRET_LENGTH = 1024;
    if (length > MAX_SECRET_LENGTH)
    {
        cError() << "Requested secret length too large: " << length;
        return "";
    }
    
    std::vector<unsigned char> buffer(length);
    if (RAND_bytes(buffer.data(), length) != 1)
    {
        cError() << "Failed to generate random bytes for device secret";
        return "";
    }

    std::ostringstream oss;
    for (size_t i = 0; i < length; ++i)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[i]);
    }

    return oss.str();
}

void RemoteUI::generateDeviceSecret()
{
    device_secret = generateRandomSecret(32);
}

bool RemoteUI::validateHMAC(const string &token, const string &timestamp, const string &nonce, const string &hmac) const
{
    if (device_secret.empty())
        return false;

    string message = token + ":" + timestamp + ":" + nonce;

    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int result_len;

    HMAC(EVP_sha256(),
         device_secret.c_str(), device_secret.length(),
         reinterpret_cast<const unsigned char*>(message.c_str()), message.length(),
         result, &result_len);

    std::ostringstream oss;
    for (unsigned int i = 0; i < result_len; ++i)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(result[i]);
    }

    return oss.str() == hmac;
}

void RemoteUI::extractReferencedIOs()
{
    referenced_ios.clear();

    if (config.pages.is_array())
    {
        for (const auto &page : config.pages)
        {
            if (page.contains("widgets") && page["widgets"].is_array())
            {
                for (const auto &widget : page["widgets"])
                {
                    if (widget.contains("io") && widget["io"].is_string())
                    {
                        referenced_ios.insert(widget["io"]);
                    }
                }
            }
        }
    }
}

bool RemoteUI::hasReferencedIO(const string &io_id) const
{
    return referenced_ios.find(io_id) != referenced_ios.end();
}

Json RemoteUI::toJson() const
{
    Json j;
    j["id"] = id;
    j["provisioning_code"] = provisioning_code;
    j["auth_token"] = auth_token;
    j["device_secret"] = device_secret;
    j["mac_address"] = mac_address;
    j["device_info"] = device_info.toJson();
    j["config"] = config.toJson();
    j["is_online"] = is_online;
    j["is_provisioned"] = is_provisioned;

    auto time_t_provisioned = std::chrono::system_clock::to_time_t(provisioned_at);
    auto time_t_last_seen = std::chrono::system_clock::to_time_t(last_seen);

    j["provisioned_at"] = std::to_string(time_t_provisioned);
    j["last_seen"] = std::to_string(time_t_last_seen);

    return j;
}

void RemoteUI::fromJson(const Json &j)
{
    id = j.value("id", "");
    provisioning_code = j.value("provisioning_code", "");
    auth_token = j.value("auth_token", "");
    device_secret = j.value("device_secret", "");
    mac_address = j.value("mac_address", "");
    is_online = j.value("is_online", false);
    is_provisioned = j.value("is_provisioned", false);

    if (j.contains("device_info"))
        device_info.fromJson(j["device_info"]);

    if (j.contains("config"))
    {
        config.fromJson(j["config"]);
        extractReferencedIOs();
    }

    if (j.contains("provisioned_at"))
    {
        string time_str = j["provisioned_at"];
        auto time_t_val = std::stoull(time_str);
        provisioned_at = std::chrono::system_clock::from_time_t(time_t_val);
    }

    if (j.contains("last_seen"))
    {
        string time_str = j["last_seen"];
        auto time_t_val = std::stoull(time_str);
        last_seen = std::chrono::system_clock::from_time_t(time_t_val);
    }
}

Json RemoteUI::getProvisioningResponse() const
{
    Json response;
    response["status"] = "accepted";
    response["device_id"] = id;
    response["auth_token"] = auth_token;
    response["device_secret"] = device_secret;

    Json server_config;
    server_config["websocket_url"] = "ws://localhost:5454/api/v1/remote_ui/ws";
    server_config["sync_interval"] = 1000;
    response["server_config"] = server_config;

    response["remote_ui_config"] = config.toJson();

    return response;
}

Json RemoteUI::getRemoteUIIOStatesMessage(const std::map<string, Json> &io_states) const
{
    Json message;
    message["msg"] = "remote_ui_io_states";

    Json data = Json::array();
    for (const string &io_id : referenced_ios)
    {
        auto it = io_states.find(io_id);
        if (it != io_states.end())
        {
            data.push_back(it->second);
        }
    }

    message["data"] = data;
    return message;
}

Json RemoteUI::getRemoteUIConfigMessage() const
{
    Json message;
    message["msg"] = "remote_ui_config";
    message["data"] = config.toJson();
    return message;
}

void RemoteUI::updateLastSeen()
{
    last_seen = std::chrono::system_clock::now();
}