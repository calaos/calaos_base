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
#ifndef RemoteUI_H
#define RemoteUI_H

#include "Calaos.h"
#include "IOBase.h"
#include "json.hpp"
#include <set>
#include <chrono>

using Json = nlohmann::json;

namespace Calaos
{

class RemoteUI : public IOBase
{
private:
    bool is_online;
    bool is_provisioned;
    std::chrono::system_clock::time_point provisioned_at;
    std::chrono::system_clock::time_point last_seen;

    std::set<string> referenced_ios;

    Json pages;
    Json device_info;

    string cmd_state;

    void readConfig();
    void emitChange();
    void extractReferencedIOs();
    string generateRandomSecret(size_t length = 32) const;

public:
    RemoteUI(Params &p);
    virtual ~RemoteUI();

    virtual bool LoadFromXml(TiXmlElement *node) override;
    virtual bool SaveToXml(TiXmlElement *node) override;

    // State management
    bool isOnline() const { return is_online; }
    void setOnline(bool online);
    bool isProvisioned() const { return is_provisioned; }
    void updateLastSeen();

    // Device secret management
    void generateDeviceSecret();
    bool validateHMAC(const string &token, const string &timestamp, const string &nonce, const string &hmac);

    // IO reference management
    const std::set<string> &getReferencedIOs() const { return referenced_ios; }
    bool hasReferencedIO(const string &io_id) const;

    // Configuration access
    const Json &getPages() const { return pages; }
    const Json &getDeviceInfo() const { return device_info; }

    // Provisioning response for API
    Json getProvisioningResponse();

    // RemoteUI state messages for WebSocket
    Json getRemoteUIIOStatesMessage(const std::map<string, Json> &io_states);
    Json getRemoteUIConfigMessage();

    // Actions for rules
    bool setBrightness(int brightness);
    int getBrightness();
    bool setPage(const string &page_id);
    bool showNotification(const string &message);

    virtual DATA_TYPE get_type() override { return TSTRING; }
    virtual bool set_value(string val) override;
    virtual string get_command_string() override { return cmd_state; }
};

}

#endif