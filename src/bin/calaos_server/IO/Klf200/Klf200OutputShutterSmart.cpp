/******************************************************************************
 **  Copyright (c) 2026, Calaos. All Rights Reserved.
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
#include <algorithm>

#include "Klf200OutputShutterSmart.h"
#include "Klf200Wire.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(Klf200OutputShutterSmart)

static const char *TAG = "klf200";

Klf200OutputShutterSmart::Klf200OutputShutterSmart(Params &p):
    OutputShutterSmart(p)
{
    ioDoc->friendlyNameSet("Klf200OutputShutterSmart");
    ioDoc->descriptionSet(_("VELUX window or roller shutter controlled through a KLF 200 gateway. The position is the one reported by the gateway."));
    ioDoc->linkAdd("VELUX KLF 200", "https://www.klf200.com/klf200");

    ioDoc->paramAdd("host", _("IP address or hostname of the KLF 200 gateway"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("password", _("Password of the KLF 200 gateway (the Wi-Fi password printed on the back of the unit)"), IODoc::TYPE_STRING, true);
    ioDoc->paramAddInt("node_id", _("Node ID of the window or shutter on the gateway (run calaos_klf200 --list to find it)"), 0, 255, true);

    //Not used by this driver: the gateway reports the real position.
    //Re-declared here as optional so the installer does not require them.
    ioDoc->paramAddInt("time_up", _("Not used: the KLF 200 reports the real position"), 0, 9999, false, 100, true);
    ioDoc->paramAddInt("time_down", _("Not used: the KLF 200 reports the real position"), 0, 9999, false, 100, true);
    ioDoc->paramAddInt("impulse_time", _("Not used by this driver"), 0, 999999, false, 0, true);
    ioDoc->paramAdd("stop_both", _("Not used by this driver"), IODoc::TYPE_BOOL, false, string(), true);

    //The gateway owns the motion: set_value() refuses these
    ioDoc->actionRemove("impulse up 200");
    ioDoc->actionRemove("impulse down 200");
    ioDoc->actionRemove("calibrate");

    //Pin the time base to 100 so that position == percentage
    readConfig();

    //The cached direction is stale: only the gateway knows if it moves
    sens = SHUTTER_STOP;

    if (!get_params().Exists("host") || get_param("host").empty())
    {
        cErrorDom(TAG) << "Klf200OutputShutterSmart(" << get_param("id") << "): host parameter is required";
        return;
    }

    if (!get_params().Exists("password") || get_param("password").empty())
    {
        cErrorDom(TAG) << "Klf200OutputShutterSmart(" << get_param("id") << "): password parameter is required";
        return;
    }

    if (!get_params().Exists("node_id") || !Utils::is_of_type<int>(get_param("node_id")))
    {
        cErrorDom(TAG) << "Klf200OutputShutterSmart(" << get_param("id") << "): node_id parameter is required";
        return;
    }

    host = get_param("host");
    Utils::from_string(get_param("node_id"), nodeId);

    ctrl = &Klf200Ctrl::Instance();

    registrationId = ctrl->registerNode(Klf200Types::Host(host),
                                        Klf200Types::Password(get_param("password")),
                                        Klf200Types::NodeId(nodeId),
                                        false,
                                        [this, weakAlive = std::weak_ptr<bool>(alive)](const Params &event)
                                        {
        if (weakAlive.expired())
            return; //IO deleted, stale callback
        nodeEventReceived(event);
    });

    cInfoDom(TAG) << "Klf200OutputShutterSmart(" << get_param("id") << "): node " << nodeId << " on " << host;
}

Klf200OutputShutterSmart::~Klf200OutputShutterSmart()
{
    if (ctrl && registrationId != Klf200Registry::INVALID_ID)
        ctrl->unregisterNode(registrationId);
}

void Klf200OutputShutterSmart::readConfig()
{
    if (!get_params().Exists("visible")) set_param("visible", "true");

    //The gateway reports percentages: a 100-unit time base makes every
    //conversion of the base class (position <-> percent) an identity.
    total_time = 100;
    time_up = 100;
    time_down = 100;
    impulse_time = -1;
}

bool Klf200OutputShutterSmart::sendPosition(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    return ctrl->setPosition(Klf200Types::Host(host), Klf200Types::NodeId(nodeId), percent);
}

bool Klf200OutputShutterSmart::sendStop()
{
    return ctrl->stop(Klf200Types::Host(host), Klf200Types::NodeId(nodeId));
}

/* Grammar of OutputShutterSmart minus impulse and calibrate. No timer: the
 * motion comes back through nodeEventReceived(); only cmd_state is published
 * here, and nothing at all when the order could not be sent. */
bool Klf200OutputShutterSmart::set_value(std::string val)
{
    if (!isEnabled()) return true;

    if (!ctrl)
    {
        cErrorDom(TAG) << "Klf200OutputShutterSmart(" << get_param("id")
                       << "): not configured (host, password and node_id are required), action " << val << " refused";
        return false;
    }

    cInfoDom(TAG) << "Klf200OutputShutterSmart(" << get_param("id") << "): got action, " << val;

    const int current = (int)get_value_double();

    const string previousCmdState = cmd_state;
    bool sent = true;

    if (val == "up")
    {
        cmd_state = "up";
        sent = sendPosition(0);
    }
    else if (val == "down")
    {
        cmd_state = "down";
        sent = sendPosition(100);
    }
    else if (val == "stop")
    {
        cmd_state = "stop";
        sent = sendStop();
    }
    else if (val == "toggle")
    {
        if (sens != SHUTTER_STOP)
        {
            cmd_state = "stop";
            sent = sendStop();
        }
        else if (old_sens == SHUTTER_UP)
        {
            cmd_state = "down";
            sent = sendPosition(100);
        }
        else
        {
            cmd_state = "up";
            sent = sendPosition(0);
        }
    }
    else if (Utils::strStartsWith(val, "set "))
    {
        val.erase(0, 4);
        if (!Utils::is_of_type<int>(val))
            return false;
        int percent;
        Utils::from_string(val, percent);
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
        cmd_state = "set " + Utils::to_string(percent);
        sent = sendPosition(percent);
    }
    else if (Utils::strStartsWith(val, "up "))
    {
        val.erase(0, 3);
        if (!Utils::is_of_type<int>(val))
            return false;
        int percent;
        Utils::from_string(val, percent);
        percent = std::clamp(percent, 0, 100);
        cmd_state = "up " + Utils::to_string(percent);
        sent = sendPosition(current - percent);
    }
    else if (Utils::strStartsWith(val, "down "))
    {
        val.erase(0, 5);
        if (!Utils::is_of_type<int>(val))
            return false;
        int percent;
        Utils::from_string(val, percent);
        percent = std::clamp(percent, 0, 100);
        cmd_state = "down " + Utils::to_string(percent);
        sent = sendPosition(current + percent);
    }
    else if (Utils::strStartsWith(val, "set_state "))
    {
        val.erase(0, 10);
        if (!Utils::is_of_type<int>(val))
            return false;
        int percent;
        Utils::from_string(val, percent);
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
        cmd_state = "set " + Utils::to_string(percent);
        writePosition(percent);
        sens = SHUTTER_STOP;
    }
    else
    {
        cWarningDom(TAG) << "Klf200OutputShutterSmart(" << get_param("id") << "): unsupported action " << val;
        return false;
    }

    if (!sent)
    {
        cmd_state = previousCmdState;
        cErrorDom(TAG) << "Klf200OutputShutterSmart(" << get_param("id") << "): action " << val
                       << " not sent, the KLF 200 is not reachable";
        return false;
    }

    updateCache();
    EmitSignalIO();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", get_value_string() } },
                         false);

    return true;
}

void Klf200OutputShutterSmart::nodeEventReceived(const Params &event)
{
    if (event["event"] != "node")
        return; //rain events are for Klf200InputSwitch

    /* Only a window gets a style, and only when none was chosen: a roller
     * shutter keeps no io_style, like every other shutter. */
    if (Klf200Wire::styleOf(event) == "window" && get_param("io_style").empty())
        set_param("io_style", "window");

    const int reported = Klf200Wire::positionOf(event);
    const string moving = event["moving"];

    int newSens = SHUTTER_STOP;
    if (moving == "up") newSens = SHUTTER_UP;
    else if (moving == "down") newSens = SHUTTER_DOWN;

    /* Nothing new. Compared with what the IO shows, not with the last report:
     * after a set_state, the next report must restore the real position. */
    if ((reported < 0 || reported == (int)get_value_double()) && newSens == sens)
        return;

    const bool wasMoving = sens != SHUTTER_STOP;

    if (reported >= 0)
        writePosition(reported);

    if (wasMoving && newSens == SHUTTER_STOP)
        old_sens = sens;
    else if (newSens != SHUTTER_STOP)
        old_sens = newSens;
    sens = newSens;

    cDebugDom(TAG) << "Klf200OutputShutterSmart(" << get_param("id") << "): position " << reported
                   << "% moving=" << moving;

    updateCache();
    EmitSignalIO();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", get_value_string() } },
                         newSens == SHUTTER_STOP);
}
