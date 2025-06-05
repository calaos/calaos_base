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
#include "IOBase.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "DataLogger.h"
#include "Utils.h"
#include "NotifManager.h"
#include "CalaosConfig.h"

using namespace Calaos;

//Default timer value before value is considered bad (4 hours)
double const IOBase::TimerChangedWarning = 60 * 60 * 4;

IOBase::IOBase(Params &p, int iotype):
    param(p),
    auto_sc_mark(false),
    io_type(iotype)
{
    ioDoc = new IODoc();
    ioDoc->paramAdd("id", _("Unique ID identifying the Input/Output in calaos-server"), IODoc::TYPE_STRING, true, string(), true);
    ioDoc->paramAdd("name", _("Name of Input/Output."), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("visible", _("Display the Input/Output on all user interfaces if set. Default to true"), IODoc::TYPE_BOOL, false, "true");
    ioDoc->paramAdd("enabled", _("Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration."), IODoc::TYPE_BOOL, false, "true");
    ioDoc->paramAdd("gui_type", _("Internal graphical type for all calaos objects. Set automatically, read-only parameter."), IODoc::TYPE_STRING, false, string(), true);
    ioDoc->paramAdd("io_type", _("IO type, can be \"input\", \"output\", \"inout\""), IODoc::TYPE_STRING, true, string(), true);
    ioDoc->paramAdd("log_history", _("If enabled, write an entry in the history event log for this IO"), IODoc::TYPE_BOOL, false, "false");
    ioDoc->paramAdd("logged", _("If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO"), IODoc::TYPE_BOOL, false, "false");

    if (!param.Exists("enabled"))
        param.Add("enabled", "true");

    param.Add("io_type", io_type == IO_INPUT?"input":io_type == IO_OUTPUT?"output":"inout");

    ListeRoom::Instance().addIOHash(this);
}

IOBase::~IOBase()
{
    ListeRoom::Instance().delIOHash(this);
    delete ioDoc;
}

void IOBase::EmitSignalIO()
{
    cDebugDom("iobase") << get_param("id");
    ListeRule::Instance().ExecuteRuleSignal(get_param("id"));
    DataLogger::Instance().log(this);
}

bool IOBase::LoadFromXml(TiXmlElement *node)
{
    VAR_UNUSED(node);
    return true;
}

bool IOBase::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *cnode;
    if (isInput())
        cnode = new TiXmlElement("calaos:input");
    else
        cnode = new TiXmlElement("calaos:output");
    node->LinkEndChild(cnode);

    for (int i = 0;i < get_params().size();i++)
    {
        string key, value;
        get_params().get_item(i, key, value);
        cnode->SetAttribute(key, value);
    }

    return true;
}

void IOBase::setStatusInfo(StatusType type, double value)
{
    switch (type)
    {
        case StatusType::BatteryLevel:
        {
            status_info.battery_level = value;

            // If battery level is less than 30%, we send a notification if enabled

            //get last time the notification was sent
            string id = get_param("id") + "_" + get_param("type");
            Params cachedParams;
            Config::Instance().ReadValueParams(id, cachedParams);

            time_t current_time = time(nullptr);
            time_t last_notif_time = 0;
            string last_notif_str = cachedParams["last_battery_notif_time"];

            if (!last_notif_str.empty())
            {
                try
                {
                    last_notif_time = std::stoll(last_notif_str);
                }
                catch (const std::exception&)
                {
                    last_notif_time = 0;
                }
            }

            // Check if 24 hours (86400 seconds) have passed
            bool enough_time_passed = (current_time - last_notif_time) >= 86400;

            //global notification settings
            bool g_notif_mail_enabled = Utils::get_config_option("notif/battery_mail_enabled") == "true";
            bool g_notif_push_enabled = Utils::get_config_option("notif/battery_push_enabled") == "true";

            //IO specific notification settings
            bool io_notif_enabled = get_param("notif_battery") == "true";

            if (value < 30.0 && io_notif_enabled && enough_time_passed)
            {
                if (g_notif_mail_enabled)
                {
                    cDebugDom("iobase") << "Sending battery low notification via email for IO: " << get_param("id");
                    NotifManager::Instance().sendMailNotification(
                        "Battery Low",
                        "The battery level of " + get_param("name") + " is low (" + Utils::to_string(value) + "%)."
                    );
                }
                if (g_notif_push_enabled)
                {
                    cDebugDom("iobase") << "Sending battery low notification via push for IO: " << get_param("id");
                    NotifManager::Instance().sendPushNotification(
                        "The battery level of " + get_param("name") + " is low (" + Utils::to_string(value) + "%)."
                    );
                }

                cachedParams["last_battery_notif_time"] = Utils::to_string(current_time);
                Config::Instance().SaveValueParams(id, cachedParams, false);
            }

            break;
        }
        case StatusType::WirelessSignal:
            status_info.wireless_signal = value;
            break;
        default:
            cWarningDom("iobase") << "setStatusInfo: Unsupported status type for double value: " << static_cast<int>(type);
            break;
    }
}

void IOBase::setStatusInfo(StatusType type, const string &value)
{
    switch (type)
    {
        case StatusType::IpAddress:
            status_info.ip_address = value;
            break;
        case StatusType::WifiSSID:
            status_info.wifi_ssid = value;
            break;
        default:
            cWarningDom("iobase") << "setStatusInfo: Unsupported status type for string value: " << static_cast<int>(type);
            break;
    }
}

void IOBase::setStatusInfo(StatusType type, StatusConnected value)
{
    switch (type)
    {
        case StatusType::Connected:
        {
            status_info.connected = value;

            //global notification settings
            bool g_notif_mail_enabled = Utils::get_config_option("notif/io_connected_mail_enabled") == "true";
            bool g_notif_push_enabled = Utils::get_config_option("notif/io_connected_push_enabled") == "true";

            //IO specific notification settings
            bool io_notif_enabled = get_param("notif_connected") == "true";

            if (value != StatusConnected::STATUS_NONE && io_notif_enabled)
            {
                if (g_notif_mail_enabled)
                {
                    cDebugDom("iobase") << "Sending connected notification via email for IO: " << get_param("id");
                    NotifManager::Instance().sendMailNotification(
                        "Connected status changed",
                        "The connected status of " + get_param("name") + " has changed to: " +
                        (value == StatusConnected::STATUS_CONNECTED ? "connected" : "disconnected") + "."
                    );
                }
                if (g_notif_push_enabled)
                {
                    cDebugDom("iobase") << "Sending connected notification via push for IO: " << get_param("id");
                    NotifManager::Instance().sendPushNotification(
                        "The connected status of " + get_param("name") + " has changed to: " +
                        (value == StatusConnected::STATUS_CONNECTED ? "connected" : "disconnected") + "."
                    );
                }
            }

            break;
        }
        default:
            cWarningDom("iobase") << "setStatusInfo: Unsupported status type for bool value: " << static_cast<int>(type);
            break;
    }
}

void IOBase::setStatusInfo(StatusType type, uint64_t value)
{
    switch (type)
    {
        case StatusType::Uptime:
            status_info.uptime = value;
            break;
        default:
            cWarningDom("iobase") << "setStatusInfo: Unsupported status type for uint64_t value: " << static_cast<int>(type);
            break;
    }
}

Params IOBase::getStatusInfo() const
{
    Params status;
    if (status_info.battery_level > 0.0) status.Add("battery_level", Utils::to_string(status_info.battery_level));
    if (status_info.connected != StatusConnected::STATUS_NONE)
        status.Add("connected", status_info.connected == StatusConnected::STATUS_CONNECTED ? "true" : "false");
    if (status_info.wireless_signal > 0.0) status.Add("wireless_signal", Utils::to_string(status_info.wireless_signal));
    if (status_info.uptime > 0) status.Add("uptime", Utils::to_string(status_info.uptime));
    if (!status_info.ip_address.empty()) status.Add("ip_address", status_info.ip_address);
    if (!status_info.wifi_ssid.empty()) status.Add("wifi_ssid", status_info.wifi_ssid);
    return status;
}
