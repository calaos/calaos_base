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
#include <WODaliRVB.h>
#include <IPC.h>

using namespace Calaos;

WODaliRVB::WODaliRVB(Params &_p):
                Output(_p),
                value(0),
                old_value(100),
                port(502)
{
        host = get_param("host");
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        if (!get_params().Exists("visible")) set_param("visible", "true");

        string cmd;
        cmd = "WAGO_DALI_GET " + get_param("rline") + " " + get_param("raddress");
        WagoMap::Instance(host, port).SendUDPCommand(cmd, sigc::mem_fun(*this, &WODaliRVB::WagoUDPCommandRed_cb));
        cmd = "WAGO_DALI_GET " + get_param("gline") + " " + get_param("gaddress");
        WagoMap::Instance(host, port).SendUDPCommand(cmd, sigc::mem_fun(*this, &WODaliRVB::WagoUDPCommandGreen_cb));
        cmd = "WAGO_DALI_GET " + get_param("bline") + " " + get_param("baddress");
        WagoMap::Instance(host, port).SendUDPCommand(cmd, sigc::mem_fun(*this, &WODaliRVB::WagoUDPCommandBlue_cb));

        value = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;

        Calaos::StartReadRules::Instance().addIO();
        Calaos::StartReadRules::Instance().addIO();
        Calaos::StartReadRules::Instance().addIO();

        Utils::logger("output") << Priority::INFO << "WODaliRVB::WODaliRVB(" << get_param("id") << "): Ok" << log4cpp::eol;
}

WODaliRVB::~WODaliRVB()
{
        Utils::logger("output") << Priority::INFO << "WODaliRVB::~WODaliRVB(): Ok" << log4cpp::eol;
}

void WODaliRVB::WagoUDPCommandRed_cb(bool status, string command, string result)
{
        if (!status)
        {
                Utils::logger("output") << Priority::INFO << "WODaliRVB::WagoUdpCommandRed(): Error with request " << command << log4cpp::eol;
                Calaos::StartReadRules::Instance().ioRead();

                return;
        }

        vector<string> tokens;
        split(result, tokens);
        if (tokens.size() >= 3)
        {
                from_string(tokens[2], red);
                red = (red * 255) / 100;

                value = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;

                string sig = "output ";
                sig += get_param("id") + " ";
                sig += Utils::url_encode(string("state:") + get_value_string());
                IPC::Instance().SendEvent("events", sig);
        }

        Calaos::StartReadRules::Instance().ioRead();
}

void WODaliRVB::WagoUDPCommandGreen_cb(bool status, string command, string result)
{
        if (!status)
        {
                Utils::logger("output") << Priority::INFO << "WODaliRVB::WagoUdpCommandGreen(): Error with request " << command << log4cpp::eol;
                Calaos::StartReadRules::Instance().ioRead();

                return;
        }

        vector<string> tokens;
        split(result, tokens);
        if (tokens.size() >= 3)
        {
                from_string(tokens[2], green);
                green = (green * 255) / 100;

                value = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;

                string sig = "output ";
                sig += get_param("id") + " ";
                sig += Utils::url_encode(string("state:") + get_value_string());
                IPC::Instance().SendEvent("events", sig);
        }

        Calaos::StartReadRules::Instance().ioRead();
}

void WODaliRVB::WagoUDPCommandBlue_cb(bool status, string command, string result)
{
        if (!status)
        {
                Utils::logger("output") << Priority::INFO << "WODaliRVB::WagoUdpCommandBlue(): Error with request " << command << log4cpp::eol;
                Calaos::StartReadRules::Instance().ioRead();

                return;
        }

        vector<string> tokens;
        split(result, tokens);
        if (tokens.size() >= 3)
        {
                from_string(tokens[2], blue);
                blue = (blue * 255) / 100;

                value = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;

                string sig = "output ";
                sig += get_param("id") + " ";
                sig += Utils::url_encode(string("state:") + get_value_string());
                IPC::Instance().SendEvent("events", sig);
        }

        Calaos::StartReadRules::Instance().ioRead();
}

void WODaliRVB::WagoUDPCommand_cb(bool status, string command, string result)
{
        if (!status)
        {
                Utils::logger("output") << Priority::INFO << "WODaliRVB::WagoUdpCommand(): Error with request " << command << log4cpp::eol;

                return;
        }
}

/* List of actions where value is in percent
**  set <value>
**  on
**  off
**  toggle
*/
bool WODaliRVB::set_value(std::string val)
{
        bool ret = true;

        Utils::logger("output") << Priority::INFO << "WODaliRVB(" << get_param("id") << "): got action, " << val << log4cpp::eol;

        host = get_param("host");
        from_string(get_param("port"), port);

        struct timespec t;
        t.tv_sec = 0;
        t.tv_nsec = 50 * 1000000; //50ms

        if (val == "on" || val == "true")
        {
                //switch the light on only if value == 0
                if (value == 0)
                {
                        if (red == 0 && green == 0 && blue == 0)
                        {
                                red = 255; green = 255; blue = 255;
                        }

                        //red
                        string cmd = "WAGO_DALI_SET " + get_param("rline") + " " + get_param("rgroup") +
                                     " " + get_param("raddress") + " " + to_string((red * 100) / 255) +
                                     " " + get_param("rfade_time");
                        WagoMap::Instance(host, port).SendUDPCommand(cmd);

                        //green
                        cmd = "WAGO_DALI_SET " + get_param("gline") + " " + get_param("ggroup") +
                                     " " + get_param("gaddress") + " " + to_string((green * 100) / 255) +
                                     " " + get_param("gfade_time");
                        WagoMap::Instance(host, port).SendUDPCommand(cmd);

                        //blue
                        cmd = "WAGO_DALI_SET " + get_param("bline") + " " + get_param("bgroup") +
                                     " " + get_param("baddress") + " " + to_string((blue * 100) / 255) +
                                     " " + get_param("bfade_time");
                        WagoMap::Instance(host, port).SendUDPCommand(cmd);

                        value = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;

                        cmd_state = "on";
                }
        }
        else if (val == "off" || val == "false")
        {
                //switch the light off only if value > 0
                if (value > 0)
                {
                        //red
                        string cmd = "WAGO_DALI_SET " + get_param("rline") + " " + get_param("rgroup") +
                                     " " + get_param("raddress") + " 0 " +
                                     get_param("rfade_time");
                        WagoMap::Instance(host, port).SendUDPCommand(cmd);

                        //green
                        cmd = "WAGO_DALI_SET " + get_param("gline") + " " + get_param("ggroup") +
                                     " " + get_param("gaddress") + " 0 " +
                                     get_param("gfade_time");
                        WagoMap::Instance(host, port).SendUDPCommand(cmd);

                        //blue
                        cmd = "WAGO_DALI_SET " + get_param("bline") + " " + get_param("bgroup") +
                                     " " + get_param("baddress") + " 0 " +
                                     get_param("bfade_time");
                        WagoMap::Instance(host, port).SendUDPCommand(cmd);

                        old_value = value;
                        value = 0;

                        cmd_state = "off";
                }
        }
        else if (val.compare(0, 8, "set off ") == 0)
        {
                val.erase(0, 4);
                int percent;
                from_string(val, percent);
                red = percent >> 16;
                green = (percent >> 8) & 0x0000FF;
                blue = percent & 0x0000FF;
                if (red < 0) red = 0;
                if (red > 255) red = 255;
                if (green < 0) green = 0;
                if (green > 255) green = 255;
                if (blue < 0) blue = 0;
                if (blue > 255) blue = 255;

                cmd_state = "set off " + Utils::to_string(percent);

                if (value > 0)
                {
                        string cmd = "WAGO_DALI_SET " + get_param("rline") + " " + get_param("rgroup") +
                                " " + get_param("raddress") + " " + to_string((red * 100) / 255) +
                                " " + get_param("rfade_time");
                        WagoMap::Instance(host, port).SendUDPCommand(cmd);

                        cmd = "WAGO_DALI_SET " + get_param("gline") + " " + get_param("ggroup") +
                                " " + get_param("gaddress") + " " + to_string((green * 100) / 255) +
                                " " + get_param("gfade_time");
                        WagoMap::Instance(host, port).SendUDPCommand(cmd);

                        cmd = "WAGO_DALI_SET " + get_param("bline") + " " + get_param("bgroup") +
                                " " + get_param("baddress") + " " + to_string((blue * 100) / 255) +
                                " " + get_param("bfade_time");
                        WagoMap::Instance(host, port).SendUDPCommand(cmd);

                        value = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;
                }
                else
                {
                        old_value = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;
                }
        }
        else if (val == "toggle")
        {
                if (value == 0)
                        set_value(true);
                else
                        set_value(false);
        }
        else if (val.compare(0, 4, "set ") == 0)
        {
                val.erase(0, 4);
                int percent;
                from_string(val, percent);
                red = percent >> 16;
                green = (percent >> 8) & 0x0000FF;
                blue = percent & 0x0000FF;
                if (red < 0) red = 0;
                if (red > 255) red = 255;
                if (green < 0) green = 0;
                if (green > 255) green = 255;
                if (blue < 0) blue = 0;
                if (blue > 255) blue = 255;

                cmd_state = "set " + Utils::to_string(percent);

                string cmd = "WAGO_DALI_SET " + get_param("rline") + " " + get_param("rgroup") +
                             " " + get_param("raddress") + " " + to_string((red * 100) / 255) +
                             " " + get_param("rfade_time");
                WagoMap::Instance(host, port).SendUDPCommand(cmd);

                cmd = "WAGO_DALI_SET " + get_param("gline") + " " + get_param("ggroup") +
                             " " + get_param("gaddress") + " " + to_string((green * 100) / 255) +
                             " " + get_param("gfade_time");
                WagoMap::Instance(host, port).SendUDPCommand(cmd);

                cmd = "WAGO_DALI_SET " + get_param("bline") + " " + get_param("bgroup") +
                             " " + get_param("baddress") + " " + to_string((blue * 100) / 255) +
                             " " + get_param("bfade_time");
                WagoMap::Instance(host, port).SendUDPCommand(cmd);

                value = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;
        }
        else if (val.compare(0, 8, "set_red ") == 0)
        {
                val.erase(0, 8);
                int percent;
                from_string(val, percent);
                red = percent;
                if (red < 0) red = 0;
                if (red > 255) red = 255;

                int v = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;
                set_value("set " + to_string(v));
        }
        else if (val.compare(0, 10, "set_green ") == 0)
        {
                val.erase(0, 10);
                int percent;
                from_string(val, percent);
                green = percent;
                if (green < 0) green = 0;
                if (green > 255) green = 255;

                int v = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;
                set_value("set " + to_string(v));
        }
        else if (val.compare(0, 9, "set_blue ") == 0)
        {
                val.erase(0, 9);
                int percent;
                from_string(val, percent);
                blue = percent;
                if (blue < 0) blue = 0;
                if (blue > 255) blue = 255;

                int v = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;
                set_value("set " + to_string(v));
        }
        else if (val.compare(0, 7, "up_red ") == 0)
        {
                val.erase(0, 7);
                int percent;
                from_string(val, percent);
                red += percent;
                if (red < 0) red = 0;
                if (red > 255) red = 255;

                int v = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;
                set_value("set " + to_string(v));
        }
        else if (val.compare(0, 9, "down_red ") == 0)
        {
                val.erase(0, 9);
                int percent;
                from_string(val, percent);
                red -= percent;
                if (red < 0) red = 0;
                if (red > 255) red = 255;

                int v = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;
                set_value("set " + to_string(v));
        }
        else if (val.compare(0, 9, "up_green ") == 0)
        {
                val.erase(0, 9);
                int percent;
                from_string(val, percent);
                green += percent;
                if (green < 0) green = 0;
                if (green > 255) green = 255;

                int v = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;
                set_value("set " + to_string(v));
        }
        else if (val.compare(0, 11, "down_green ") == 0)
        {
                val.erase(0, 11);
                int percent;
                from_string(val, percent);
                green -= percent;
                if (green < 0) green = 0;
                if (green > 255) green = 255;

                int v = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;
                set_value("set " + to_string(v));
        }
        else if (val.compare(0, 8, "up_blue ") == 0)
        {
                val.erase(0, 8);
                int percent;
                from_string(val, percent);
                blue += percent;
                if (blue < 0) blue = 0;
                if (blue > 255) blue = 255;

                int v = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;
                set_value("set " + to_string(v));
        }
        else if (val.compare(0, 10, "down_blue ") == 0)
        {
                val.erase(0, 10);
                int percent;
                from_string(val, percent);
                blue -= percent;
                if (blue < 0) blue = 0;
                if (blue > 255) blue = 255;

                int v = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;
                set_value("set " + to_string(v));
        }

        EmitSignalOutput();

        string sig = "output ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode(string("state:") + get_value_string());
        IPC::Instance().SendEvent("events", sig);

        return ret;
}
