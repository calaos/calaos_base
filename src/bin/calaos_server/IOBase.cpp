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

bool IOBase::docGenerationMode = false;

IOBase::ScopedDocGen::ScopedDocGen()
{
    IOBase::docGenerationMode = true;
}

IOBase::ScopedDocGen::~ScopedDocGen()
{
    IOBase::docGenerationMode = false;
}

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

    //Documentation throwaway IOs (IOFactory::genDocIO()) must not enter the
    //live io_table: they all share id="doc" and are deleted right away.
    if (!docGenerationMode)
    {
        ListeRoom::Instance().addIOHash(this);
        hashRegistered = true;
    }
}

IOBase::~IOBase()
{
    if (hashRegistered)
        ListeRoom::Instance().delIOHash(this);
    delete ioDoc;
}

void IOBase::set_param(std::string opt, std::string val)
{
    if (opt == "id")
    {
        if (param.Exists("id"))
        {
            if (param["id"] == val)
                return; //no-op, not an error

            //Refuse: io_table is keyed on "id", changing it here would
            //leave a stale hash entry (lookups by the new id fail, and the
            //destructor could no longer unregister -> dangling pointer).
            //This is reachable from the JSON API "setparam" call.
            cErrorDom("iobase") << "set_param(): refusing to change id '"
                                << param["id"] << "' to '" << val
                                << "', the IO id is immutable once created "
                                << "(use renameId() to rename an IO)";
            return;
        }

        //id set for the first time after construction (unusual: all the
        //normal creation paths put "id" in the constructor Params). Go
        //through renameId() so the io_table entry follows the key change.
        renameId(val);
        return;
    }

    param.Add(opt, val);
}

void IOBase::del_param(std::string opt)
{
    if (opt == "id")
    {
        cErrorDom("iobase") << "del_param(): refusing to delete the id '"
                            << param["id"] << "', the IO id is immutable "
                            << "(io_table is keyed on it)";
        return;
    }

    param.Delete(opt);
}

bool IOBase::renameId(const std::string &newId)
{
    string oldId = param["id"];

    if (newId == oldId)
        return true; //no-op

    if (newId.empty())
    {
        cErrorDom("iobase") << "renameId(): refusing to rename '" << oldId
                            << "' to an empty id";
        return false;
    }

    if (hashRegistered && ListeRoom::Instance().get_io(newId))
    {
        cErrorDom("iobase") << "renameId(): refusing to rename '" << oldId
                            << "' to '" << newId
                            << "', an IO with this id already exists";
        return false;
    }

    if (hashRegistered)
        ListeRoom::Instance().delIOHash(this);

    param.Add("id", newId);

    if (hashRegistered)
        ListeRoom::Instance().addIOHash(this);

    return true;
}

void IOBase::EmitSignalIO()
{
    cDebugDom("iobase") << get_param("id");
    ListeRule::Instance().ExecuteRuleSignal(get_param("id"));
    DataLogger::Instance().log(this);
}

bool IOBase::LoadFromXml(pugi::xml_node node)
{
    VAR_UNUSED(node);
    return true;
}

bool IOBase::SaveToXml(pugi::xml_node node)
{
    pugi::xml_node cnode = node.append_child(isInput()?"calaos:input":"calaos:output");

    for (int i = 0;i < get_params().size();i++)
    {
        string key, value;
        get_params().get_item(i, key, value);
        XmlUtils::setAttribute(cnode, key, value);
    }

    return true;
}

void IOBase::setStatusInfo(StatusType type, double value)
{
    switch (type)
    {
        case StatusType::BatteryLevel:
        {
            //0% is a valid reading: record it and report it (getStatusInfo()
            //keys on battery_level_set, not on the value being non-zero).
            status_info.battery_level = value;
            status_info.battery_level_set = true;

            // If battery level is less than 30%, we send a notification if enabled

            //global notification settings
            bool g_notif_mail_enabled = Utils::get_config_option("notif/battery_mail_enabled") == "true";
            bool g_notif_push_enabled = Utils::get_config_option("notif/battery_push_enabled") == "true";

            //IO specific notification settings
            bool io_notif_enabled = get_param("notif_battery") == "true";

            //Nothing to do unless the battery is low, this IO opted in, and
            //at least one channel can actually send. In particular the
            //throttle timestamp below must never be recorded when no
            //notification is sent, otherwise enabling a channel later
            //swallows the first real notification for up to 24h.
            if (value >= 30.0 || !io_notif_enabled ||
                (!g_notif_mail_enabled && !g_notif_push_enabled))
                break;

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

            if (enough_time_passed)
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

                //A channel sent (or at least attempted): record the time.
                //Params::operator[] is read-only (returns by value), so the
                //previous `cachedParams[...] = ...` assigned into a
                //temporary and the throttle timestamp was NEVER stored:
                //Add() is the correct way, and makes the 24h throttle work.
                cachedParams.Add("last_battery_notif_time", Utils::to_string(current_time));
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
    //battery_level_set and not a >0.0 check: 0% is a valid reading and must
    //be reported (consistent with the low-battery notification, which fires
    //for any reading below 30%).
    if (status_info.battery_level_set) status.Add("battery_level", Utils::to_string(status_info.battery_level));
    if (status_info.connected != StatusConnected::STATUS_NONE)
        status.Add("connected", status_info.connected == StatusConnected::STATUS_CONNECTED ? "true" : "false");
    if (status_info.wireless_signal > 0.0) status.Add("wireless_signal", Utils::to_string(status_info.wireless_signal));
    if (status_info.uptime > 0) status.Add("uptime", Utils::to_string(status_info.uptime));
    if (!status_info.ip_address.empty()) status.Add("ip_address", status_info.ip_address);
    if (!status_info.wifi_ssid.empty()) status.Add("wifi_ssid", status_info.wifi_ssid);
    return status;
}
