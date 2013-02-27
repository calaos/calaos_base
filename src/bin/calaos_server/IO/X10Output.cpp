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
#include <X10Output.h>
#include <Ecore.h>

using namespace Calaos;

X10Output::X10Output(Params &p):
                Output(p),
                value(-1),
                state_value(false),
                old_value(-1)
{
        housecode = get_param("code");
        state_value = X10Command("onstate");
        int _val;
        X10Command("dimstate", &_val);
        _val = 22 - _val + 1;
        if (state_value)
        {
                value = (int)((double)(_val * 101.) / 22.);
                value--;
        }
        else
        {
                old_value = (int)((double)(_val * 101.) / 22.);
                old_value--;
        }

        if (!get_params().Exists("visible")) set_param("visible", "true");

        Utils::logger("output") << Priority::INFO << "X10Output::X10Output(" << get_param("id") << "): Ok" << log4cpp::eol;
}

X10Output::~X10Output()
{
        Utils::logger("output") << Priority::INFO << "X10Output::~X10Output(): Ok" << log4cpp::eol;
}

/* List of actions where value is in percent
**  set <value>
**  up <value>
**  down <value>
**  on
**  off
**  toggle
*/
bool X10Output::set_value(std::string val)
{
        bool ret = false;
        housecode = get_param("code");

        Utils::logger("output") << Priority::INFO << "X10Output(" << get_param("id") << "): got action, " << val << log4cpp::eol;

        if (val == "on" || val == "true")
        {
                ret = X10Command("on");
                state_value = true;
                if (ret && state_value)
                {
                        if (old_value == -1)
                                value = 0;
                        else
                                value = old_value;
                }

                cmd_state = "on";
        }
        else if (val == "off" || val == "false")
        {
                ret = X10Command("off");
                state_value = false;

                cmd_state = "off";
        }
        else if (val == "toggle")
        {
                if (state_value) state_value = false; else state_value = true;
                if (state_value)
                {
                        ret = X10Command("on");
                        cmd_state = "on";
                }
                else
                {
                        ret = X10Command("off");
                        cmd_state = "off";
                }
                if (ret && state_value)
                {
                        if (old_value == -1)
                                value = 0;
                        else
                                value = old_value;
                }
        }
        else if (val.compare(0, 4, "set ") == 0)
        {
                val.erase(0, 4);
                int percent = atoi(val.c_str());
                if (percent < 0) percent = 0;
                if (percent > 100) percent = 100;

                cmd_state = "set " + Utils::to_string(percent);

                int v = (int)(((double)(percent + 1.) * 22.) / 101.);
                v = 22 - v + 1;
                if (v < 1) v = 1; if (v > 22) v = 22;
                ret = X10Command("dimb", &v);
                if (ret) value = percent;
        }
        else if (val.compare(0, 3, "up ") == 0)
        {
                val.erase(0, 3);
                int percent = atoi(val.c_str());
                if (percent < 0) percent = 0;
                if (percent > 100) percent = 100;

                cmd_state = "up " + Utils::to_string(percent);

                int v = (int)(((double)(percent + 1.) * 22.) / 101.);
                v = 22 - v + 1;
                if (v < 1) v = 1; if (v > 22) v = 22;
                ret = X10Command("bright", &v);
                if (ret) value = percent;
        }
        else if (val.compare(0, 5, "down ") == 0)
        {
                val.erase(0, 5);
                int percent = atoi(val.c_str());
                if (percent < 0) percent = 0;
                if (percent > 100) percent = 100;

                cmd_state = "down " + Utils::to_string(percent);

                int v = (int)(((double)(percent + 1.) * 22.) / 101.);
                v = 22 - v + 1;
                if (v < 1) v = 1; if (v > 22) v = 22;
                ret = X10Command("dim", &v);
                if (ret) value = percent;
        }

        if (state_value == false)
        {
                old_value = value;
                value = -1;
        }
        else if (value == -1)
                value = old_value;

        return ret;
}

bool X10Output::X10Command(std::string cmd, int *dval)
{
        vector<string> argv;

        if (cmd == "on")
        {
                string cmd_line = "heyu on " + housecode;
                ecore_exe_run(cmd_line.c_str(), NULL);
        }
        else if (cmd == "off")
        {
                string cmd_line = "heyu off " + housecode;
                ecore_exe_run(cmd_line.c_str(), NULL);
        }
        else if (cmd == "dimb")
        {
                string cmd_line = "heyu dimb " + housecode + " " + Utils::to_string(*dval);
                ecore_exe_run(cmd_line.c_str(), NULL);
        }
        else if (cmd == "bright")
        {
                string cmd_line = "heyu bright " + housecode + " " + Utils::to_string(*dval);
                ecore_exe_run(cmd_line.c_str(), NULL);
        }
        else if (cmd == "dim")
        {
                string cmd_line = "heyu dim " + housecode + " " + Utils::to_string(*dval);
                ecore_exe_run(cmd_line.c_str(), NULL);
        }
        else if (cmd == "onstate")
        {
                //setup params for heyu
                string _cmd = "heyu onstate " + housecode;
                string std_out;
                int _ret = -1;

                //TO FIX: Need to fix that using ecore_exe...
                //Glib::spawn_command_line_sync(_cmd, &std_out, NULL, &_ret);

                if (_ret != 0)
                {
                        //reload the heyu engine
                        string cmd_line = "heyu engine";
                        ecore_exe_run(cmd_line.c_str(), NULL);

                        //then try again
                  //      Glib::spawn_command_line_sync(_cmd, &std_out, NULL, &_ret);
                }

                if (std_out.empty()) return false;
                if (std_out.compare(0, 1, "1") == 0)
                        return true;
                else
                        return false;
        }
        else if (cmd == "dimstate")
        {
                //setup params for heyu
                string _cmd = "heyu dimstate " + housecode;
                string std_out;
                int _ret = -1;

                //TO FIX: Need to fix that using ecore_exe...
                //Glib::spawn_command_line_sync(_cmd, &std_out, NULL, &_ret);

                if (_ret != 0)
                {
                        //reload the heyu engine
                        string cmd_line = "heyu engine";
                        ecore_exe_run(cmd_line.c_str(), NULL);

                        //then try again
                        //Glib::spawn_command_line_sync(_cmd, &std_out, NULL, &_ret);
                }

                *dval = 22;
                if (std_out.empty()) return false;
                *dval = atoi(std_out.c_str());
        }

        return true;
}
