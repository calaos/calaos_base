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
#include <OutputLightRGB.h>
#include <IPC.h>

using namespace Calaos;

OutputLightRGB::OutputLightRGB(Params &_p):
                Output(_p),
                value(0),
                old_value(100),
                timer_auto(NULL)
{
        set_param("gui_type", "light_rgb");
        if (!get_params().Exists("visible")) set_param("visible", "true");

        Utils::logger("output") << Priority::INFO << "OutputLightRGB::OutputLightRGB(" << get_param("id") << "): Ok" << log4cpp::eol;
}

OutputLightRGB::~OutputLightRGB()
{
        Utils::logger("output") << Priority::INFO << "OutputLightRGB::~OutputLightRGB(): Ok" << log4cpp::eol;
}

/* List of actions where value is in percent
**  set <value>
**  on
**  off
**  toggle
*/
bool OutputLightRGB::set_value(std::string val)
{
        bool ret = true;

        Utils::logger("output") << Priority::INFO << "OutputLightRGB(" << get_param("id") << "): got action, " << val << log4cpp::eol;

        if (val == "on" || val == "true")
        {
                //switch the light on only if value == 0
                if (value == 0)
                {
                        if (red == 0 && green == 0 && blue == 0)
                        {
                                red = 255; green = 255; blue = 255;
                        }

                        setColor();
                        value = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;
                        cmd_state = "on";
                }

                DELETE_NULL(timer_auto);
        }
        else if (val == "off" || val == "false")
        {
                //switch the light off only if value > 0
                if (value > 0)
                {
                        setColorReal(0, 0, 0);

                        old_value = value;
                        value = 0;

                        cmd_state = "off";
                }

                DELETE_NULL(timer_auto);
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

                DELETE_NULL(timer_auto);

                cmd_state = "set off " + Utils::to_string(percent);

                if (value > 0)
                {
                        setColor();
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

                setColor();
                DELETE_NULL(timer_auto);
        }
        else if (val.compare(0, 8, "set_red ") == 0)
        {
                val.erase(0, 8);
                int percent;
                from_string(val, percent);
                red = percent;
                if (red < 0) red = 0;
                if (red > 255) red = 255;

                setColor();
                DELETE_NULL(timer_auto);
        }
        else if (val.compare(0, 10, "set_green ") == 0)
        {
                val.erase(0, 10);
                int percent;
                from_string(val, percent);
                green = percent;
                if (green < 0) green = 0;
                if (green > 255) green = 255;

                setColor();
                DELETE_NULL(timer_auto);
        }
        else if (val.compare(0, 9, "set_blue ") == 0)
        {
                val.erase(0, 9);
                int percent;
                from_string(val, percent);
                blue = percent;
                if (blue < 0) blue = 0;
                if (blue > 255) blue = 255;

                setColor();
                DELETE_NULL(timer_auto);
        }
        else if (val.compare(0, 7, "up_red ") == 0)
        {
                val.erase(0, 7);
                int percent;
                from_string(val, percent);
                red += percent;
                if (red < 0) red = 0;
                if (red > 255) red = 255;

                setColor();
                DELETE_NULL(timer_auto);
        }
        else if (val.compare(0, 9, "down_red ") == 0)
        {
                val.erase(0, 9);
                int percent;
                from_string(val, percent);
                red -= percent;
                if (red < 0) red = 0;
                if (red > 255) red = 255;

                setColor();
                DELETE_NULL(timer_auto);
        }
        else if (val.compare(0, 9, "up_green ") == 0)
        {
                val.erase(0, 9);
                int percent;
                from_string(val, percent);
                green += percent;
                if (green < 0) green = 0;
                if (green > 255) green = 255;

                setColor();
                DELETE_NULL(timer_auto);
        }
        else if (val.compare(0, 11, "down_green ") == 0)
        {
                val.erase(0, 11);
                int percent;
                from_string(val, percent);
                green -= percent;
                if (green < 0) green = 0;
                if (green > 255) green = 255;

                setColor();
                DELETE_NULL(timer_auto);
        }
        else if (val.compare(0, 8, "up_blue ") == 0)
        {
                val.erase(0, 8);
                int percent;
                from_string(val, percent);
                blue += percent;
                if (blue < 0) blue = 0;
                if (blue > 255) blue = 255;

                setColor();
                DELETE_NULL(timer_auto);
        }
        else if (val.compare(0, 10, "down_blue ") == 0)
        {
                val.erase(0, 10);
                int percent;
                from_string(val, percent);
                blue -= percent;
                if (blue < 0) blue = 0;
                if (blue > 255) blue = 255;

                setColor();
                DELETE_NULL(timer_auto);
        }
        else if (val.compare(0, 12, "auto_change ") == 0)
        {
                val.erase(0, 12);
                int timems;
                from_string(val, timems);

                DELETE_NULL(timer_auto);
                timer_auto = new EcoreTimer((double)timems / 1000.,
                                            (sigc::slot<void>)sigc::mem_fun(*this, &OutputLightRGB::TimerAutoChange) );
        }

        EmitSignalOutput();

        emitChange();

        return ret;
}

void OutputLightRGB::emitChange()
{
        string sig = "output ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode(string("state:") + get_value_string());
        IPC::Instance().SendEvent("events", sig);
}

void OutputLightRGB::setColor()
{
        int v = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;
        cmd_state = "set " + Utils::to_string(v);

        setColorReal(red, green, blue);

        value = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;

        EmitSignalOutput();

        emitChange();
}

void OutputLightRGB::TimerAutoChange()
{
        //choose a random color
        red = rand() % 255;
        blue = rand() % 255;
        green = rand() % 255;

        setColor();
}

void OutputLightRGB::stateUpdated(int r, int g, int b)
{
        red = r;
        green = g;
        blue = b;

        int v = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;
        cmd_state = "set " + Utils::to_string(v);

        value = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;

        EmitSignalOutput();
        emitChange();
}

bool OutputLightRGB::check_condition_value(std::string cvalue, bool equal)
{
        if (cvalue == "on" || cvalue == "true")
        {
                if ((equal && value > 0) ||
                    (!equal && value == 0))
                        return true;
        }
        else if (cvalue == "off" || cvalue == "false")
        {
                if ((!equal && value > 0) ||
                    (equal && value == 0))
                        return true;
        }
        else if (is_of_type<int>(cvalue))
        {
                int v;
                Utils::from_string(cvalue, v);
                if ((equal && value == v) ||
                    (!equal && value != v))
                        return true;
        }

        return false;
}
