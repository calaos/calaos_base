/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include <WODali.h>
#include <IPC.h>

using namespace Calaos;

WODali::WODali(Params &_p):
                Output(_p),
                value(0),
                old_value(100),
                port(502),
                hold_timer(NULL),
                press_detected(false),
                press_sens(false),
                stop_after_press(false)
{
        host = get_param("host");
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        if (!get_params().Exists("visible")) set_param("visible", "true");

        string cmd = "WAGO_DALI_GET " + get_param("line") + " " + get_param("address");
        WagoMap::Instance(host, port).SendUDPCommand(cmd, sigc::mem_fun(*this, &WODali::WagoUDPCommand_cb));

        Calaos::StartReadRules::Instance().addIO();
        Utils::logger("output") << Priority::INFO << "WODali::WODali(" << get_param("id") << "): Ok" << log4cpp::eol;
}

WODali::~WODali()
{
        Utils::logger("output") << Priority::INFO << "WODali::WODali(): Ok" << log4cpp::eol;
}

void WODali::WagoUDPCommand_cb(bool status, string command, string result)
{
        if (!status)
        {
                Utils::logger("output") << Priority::INFO << "WODali::WagoUdpCommand(): Error with request " << command << log4cpp::eol;
                Calaos::StartReadRules::Instance().ioRead();

                return;
        }

        if (command.find("WAGO_DALI_GET") != string::npos)
        {
                vector<string> tokens;
                split(result, tokens);
                if (tokens.size() >= 3)
                {
                        //get the status of the ballast
                        if (tokens[1] == "0")
                        {
                                value = 0;
                                old_value = 100;
                        }
                        else
                        {
                                value = 100;
                                old_value = 0;
                        }
                }

                string sig = "output ";
                sig += get_param("id") + " ";
                sig += Utils::url_encode(string("state:") + get_value_string());
                IPC::Instance().SendEvent("events", sig);
        }

        Calaos::StartReadRules::Instance().ioRead();
}

/* List of actions where value is in percent
**  set <value>
**  set off <value>
**  up <value>
**  down <value>
**  on
**  off
**  toggle
**  hold press
**  hold stop
*/
bool WODali::set_value(std::string val)
{
        bool ret = true;

        Utils::logger("output") << Priority::INFO << "WODali(" << get_param("id") << "): got action, " << val << log4cpp::eol;

        host = get_param("host");
        from_string(get_param("port"), port);

        if (val == "on" || val == "true")
        {
                //switch the light on only if value == 0
                if (value == 0)
                {
                        string cmd = "WAGO_DALI_SET " + get_param("line") + " " + get_param("group") +
                                     " " + get_param("address") + " " + Utils::to_string(old_value) +
                                     " " + get_param("fade_time");
                        WagoMap::Instance(host, port).SendUDPCommand(cmd);
                        value = old_value;

                        cmd_state = "on";
                }
        }
        else if (val == "off" || val == "false")
        {
                //switch the light off only if value > 0
                if (value > 0)
                {
                        string cmd = "WAGO_DALI_SET " + get_param("line") + " " + get_param("group") +
                                     " " + get_param("address") + " 0 " + get_param("fade_time");
                        WagoMap::Instance(host, port).SendUDPCommand(cmd);
                        old_value = value;
                        value = 0;

                        cmd_state = "off";
                }
        }
        else if (val == "toggle")
        {
                if (value == 0)
                        set_value(true);
                else
                        set_value(false);
        }
        else if (val.compare(0, 8, "set off ") == 0)
        {
                val.erase(0, 8);
                int percent;
                Utils::from_string(val, percent);
                if (percent < 0) percent = 0;
                if (percent > 100) percent = 100;

                cmd_state = "set off " + Utils::to_string(percent);

                string cmd = "WAGO_DALI_SET " + get_param("line") + " " + get_param("group") +
                             " " + get_param("address") + " " + Utils::to_string(percent) +
                             " " + get_param("fade_time");
                if (value > 0)
                {
                        WagoMap::Instance(host, port).SendUDPCommand(cmd);
                        value = percent;
                }
                else
                {
                        old_value = percent;
                }
        }
        else if (val.compare(0, 4, "set ") == 0)
        {
                val.erase(0, 4);
                int percent;
                Utils::from_string(val, percent);
                if (percent < 0) percent = 0;
                if (percent > 100) percent = 100;

                cmd_state = "set " + Utils::to_string(percent);

                string cmd = "WAGO_DALI_SET " + get_param("line") + " " + get_param("group") +
                             " " + get_param("address") + " " + Utils::to_string(percent) +
                             " " + get_param("fade_time");
                WagoMap::Instance(host, port).SendUDPCommand(cmd);
                value = percent;
        }
        else if (val.compare(0, 3, "up ") == 0)
        {
                val.erase(0, 3);
                int percent;
                Utils::from_string(val, percent);
                percent += value;
                if (percent < 0) percent = 0;
                if (percent > 100) percent = 100;

                cmd_state = "up " + val;

                string cmd = "WAGO_DALI_SET " + get_param("line") + " " + get_param("group") +
                             " " + get_param("address") + " " + Utils::to_string(percent) +
                             " " + get_param("fade_time");
                WagoMap::Instance(host, port).SendUDPCommand(cmd);
                value = percent;
        }
        else if (val.compare(0, 5, "down ") == 0)
        {
                val.erase(0, 5);
                int percent;
                Utils::from_string(val, percent);
                percent = value - percent;
                if (percent < 0) percent = 0;
                if (percent > 100) percent = 100;

                cmd_state = "down " + val;

                string cmd = "WAGO_DALI_SET " + get_param("line") + " " + get_param("group") +
                             " " + get_param("address") + " " + Utils::to_string(percent) +
                             " " + get_param("fade_time");
                WagoMap::Instance(host, port).SendUDPCommand(cmd);
                value = percent;
        }
        else if (val == "hold press")
        {
                if (hold_timer)
                {
                        //reset hold detection
                        delete hold_timer;
                }

                press_detected = false;
                stop_after_press = true;
                hold_timer = new EcoreTimer(0.5, (sigc::slot<void>)sigc::mem_fun(*this, &WODali::HoldPress_cb));
        }
        else if (val == "hold stop")
        {
                //only toggle after a press and if long press is not detected
                if (!press_detected && stop_after_press)
                {
                        ret = set_value(string("toggle"));
                }

                stop_after_press = false;

                if (hold_timer)
                {
                        delete hold_timer;
                        hold_timer = NULL;
                        press_detected = false;
                }
        }

        EmitSignalOutput();

        string sig = "output ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode(string("state:") + get_value_string());
        IPC::Instance().SendEvent("events", sig);

        return ret;
}

void WODali::HoldPress_cb()
{
        // press is detect the first timer with 500ms
        if (!press_detected)
        {
                press_detected = true;
                delete hold_timer;
                hold_timer = new EcoreTimer(1.0, (sigc::slot<void>)sigc::mem_fun(*this, &WODali::HoldPress_cb));
        }

        int v = 0;

        if (press_sens)
        {
                v = value - 10;
                if (v < 0)
                {
                        press_sens = false;
                        v = 0;
                }
        }
        else
        {
                v = value + 10;
                if (v > 100)
                {
                        press_sens = true;
                        v = 100;
                }
        }

        string cmd = "set ";
        cmd += to_string(v);
        set_value(cmd);
}
