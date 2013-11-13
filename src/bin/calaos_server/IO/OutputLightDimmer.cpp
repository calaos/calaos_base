/******************************************************************************
**  Copyright (c) 2007-2013, Calaos. All Rights Reserved.
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
#include <OutputLightDimmer.h>
#include <IPC.h>

using namespace Calaos;

OutputLightDimmer::OutputLightDimmer(Params &_p):
                Output(_p),
                value(0),
                old_value(100),
                hold_timer(NULL),
                press_detected(false),
                press_sens(false),
                stop_after_press(false)
{
        set_param("gui_type", "light_dimmer");

        if (!get_params().Exists("visible")) set_param("visible", "true");

        Utils::logger("output") << Priority::INFO << "OutputLightDimmer::OutputLightDimmer(" << get_param("id") << "): Ok" << log4cpp::eol;
}

OutputLightDimmer::~OutputLightDimmer()
{
        Utils::logger("output") << Priority::INFO << "OutputLightDimmer::OutputLightDimmer(): Ok" << log4cpp::eol;
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
bool OutputLightDimmer::set_value(std::string val)
{
        bool ret = true;

        Utils::logger("output") << Priority::INFO << "OutputLightDimmer(" << get_param("id") << "): got action, " << val << log4cpp::eol;

        if (val == "on" || val == "true")
        {
                //switch the light on only if value == 0
                if (value == 0)
                {
                        set_on_real();

                        cmd_state = "on";
                }
        }
        else if (val == "off" || val == "false")
        {
                //switch the light off only if value > 0
                if (value > 0)
                {
                        set_off_real();

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

                if (value > 0)
                {
                        set_value_real(percent);
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

                set_value_real(percent);
                value = percent;
        }
        else if (val.compare(0, 3, "up ") == 0)
        {
                val.erase(0, 3);
                int percent;
                Utils::from_string(val, percent);

                set_dim_up_real(percent);

                cmd_state = "up " + val;
        }
        else if (val.compare(0, 5, "down ") == 0)
        {
                val.erase(0, 5);
                int percent;
                Utils::from_string(val, percent);

                cmd_state = "down " + val;

                set_dim_down_real(percent);
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
                hold_timer = new EcoreTimer(0.5, (sigc::slot<void>)sigc::mem_fun(*this, &OutputLightDimmer::HoldPress_cb));
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

        emitChange();

        return ret;
}

bool OutputLightDimmer::set_dim_up_real(int percent)
{
        percent = value + percent;
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;

        set_value_real(percent);

        value = percent;

        return true;
}

bool OutputLightDimmer::set_dim_down_real(int percent)
{
        percent = value - percent;
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;

        set_value_real(percent);

        value = percent;

        return true;
}

bool OutputLightDimmer::set_on_real()
{
        value = old_value;
        set_value_real(value);

        return true;
}

bool OutputLightDimmer::set_off_real()
{
        old_value = value;
        value = 0;
        set_value_real(value);

        return true;
}

void OutputLightDimmer::emitChange()
{
        string sig = "output ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode(string("state:") + get_value_string());
        IPC::Instance().SendEvent("events", sig);
}

void OutputLightDimmer::HoldPress_cb()
{
        // press is detect the first timer with 500ms
        if (!press_detected)
        {
                press_detected = true;
                delete hold_timer;
                hold_timer = new EcoreTimer(1.0, (sigc::slot<void>)sigc::mem_fun(*this, &OutputLightDimmer::HoldPress_cb));
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
        cmd += Utils::to_string(v);
        set_value(cmd);
}
