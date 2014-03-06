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
        DELETE_NULL(hold_timer);
        DELETE_NULL(impulseTimer);
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
**  impulse <params>
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
        else if (val.compare(0, 8, "impulse ") == 0)
        {
                string tmp = val;
                tmp.erase(0, 8);
                // classic impulse, WODigital goes false after <time> miliseconds
                if (is_of_type<int>(tmp))
                {
                        int t;
                        Utils::from_string(tmp, t);
                        impulse(t);
                }
                else
                {
                        // extended impulse using pattern
                        impulse_extended(tmp);
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

void OutputLightDimmer::impulse(int _time)
{
        Utils::logger("output") << Priority::INFO << "OutputLightDimmer(" << get_param("id")
                        << "): got impulse action, staying true for "
                        << _time << "ms" << log4cpp::eol;

        set_value("true");

        DELETE_NULL(impulseTimer);
        impulseTimer = new EcoreTimer((double)_time / 1000.,
                        (sigc::slot<void>)sigc::mem_fun(*this, &OutputLightDimmer::TimerImpulse) );
}

void OutputLightDimmer::TimerImpulse()
{
        set_value("false");

        DELETE_NULL(impulseTimer);
}

void OutputLightDimmer::impulse_extended(string pattern)
{
        /* Extended impulse to do blinking.
         * It uses a pattern like this one:
         * - "<on_time> <off_time>"
         * - "loop <on_time> <off_time>"
         * - "old" (switch to the old value)
         * they can be combined together to create different blinking effects
         */

        DELETE_NULL(impulseTimer);
        blinks.clear();

        Utils::logger("output") << Priority::INFO << "OutputLightDimmer(" << get_param("id")
                        << "): got extended impulse action, parsing blinking pattern..." << log4cpp::eol;

        //Parse the string
        vector<string> tokens;
        split(pattern, tokens);

        bool state = true;
        int loop = -1;
        for (uint i = 0;i < tokens.size();i++)
        {
                if (is_of_type<int>(tokens[i]))
                {
                        int blinktime;
                        from_string(tokens[i], blinktime);

                        BlinkInfo binfo;
                        binfo.state = state;
                        binfo.duration = blinktime;
                        binfo.next = blinks.size() + 1;

                        blinks.push_back(binfo);

                        Utils::logger("output") << Priority::DEBUG << "OutputLightDimmer(" << get_param("id")
                                                << ")::Parse : Add blink step " << ((binfo.state)?"True":"False")
                                                << " for " << binfo.duration << "ms" << log4cpp::eol;

                        state = !state;
                }
                else if (tokens[i] == "loop" && loop < 0)
                {
                        //set loop mode to the next item
                        loop = blinks.size();

                        Utils::logger("output") << Priority::DEBUG << "OutputLightDimmer("
                                                << get_param("id") << ")::Parse : Loop all next steps." << log4cpp::eol;
                }
                else if (tokens[i] == "old")
                {
                        BlinkInfo binfo;
                        binfo.state = get_value_bool();
                        binfo.duration = 0;
                        binfo.next = blinks.size() + 1;

                        blinks.push_back(binfo);

                        Utils::logger("output") << Priority::DEBUG << "OutputLightDimmer(" << get_param("id")
                                                << ")::Parse : Add blink step " << ((binfo.state)?"True":"False")
                                                << log4cpp::eol;
                }
        }

        if (loop >= 0)
        {
                //tell the last item to loop
                if (blinks.size() > (uint)loop)
                        blinks[blinks.size() - 1].next = loop;
        }

        current_blink = 0;

        if (blinks.size() > 0)
        {
                set_value(blinks[current_blink].state?"true":"false");

                impulseTimer = new EcoreTimer((double)blinks[current_blink].duration / 1000.,
                                (sigc::slot<void>)sigc::mem_fun(*this, &OutputLightDimmer::TimerImpulseExtended) );
        }
}

void OutputLightDimmer::TimerImpulseExtended()
{
        //Stop timer
        DELETE_NULL(impulseTimer);

        //safety checks
        if (current_blink < 0 || current_blink >= (int)blinks.size())
                return; //Stops blinking

        current_blink = blinks[current_blink].next;

        //safety checks for new value
        if (current_blink < 0 || current_blink >= (int)blinks.size())
                return; //Stops blinking

        //Set new output state
        set_value(blinks[current_blink].state?"true":"false");

        //restart timer
        impulseTimer = new EcoreTimer((double)blinks[current_blink].duration / 1000.,
                                (sigc::slot<void>)sigc::mem_fun(*this, &OutputLightDimmer::TimerImpulseExtended) );
}

