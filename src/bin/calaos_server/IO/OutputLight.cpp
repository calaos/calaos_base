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
#include <OutputLight.h>
#include <IPC.h>

using namespace Calaos;

OutputLight::OutputLight(Params &p):
                Output(p),
                timer(NULL),
                value(false)
{
        if (!get_params().Exists("visible")) set_param("visible", "true");

        set_param("gui_type", "light");
}

OutputLight::~OutputLight()
{
        DELETE_NULL(timer);
}

void OutputLight::emitChange()
{
        string sig = "output ";
        sig += get_param("id") + " ";
        if (value)
                sig += Utils::url_encode("state:true");
        else
                sig += Utils::url_encode("state:false");
        IPC::Instance().SendEvent("events", sig);
}

bool OutputLight::set_value(bool val)
{
        // Setting a new value will also stop any running impulse actions
        DELETE_NULL(timer);

        return _set_value(val);
}

bool OutputLight::_set_value(bool val)
{
        Utils::logger("output") << Priority::INFO << "OutputLight(" << get_param("id")
                        << "): got action, " << ((val)?"True":"False") << log4cpp::eol;

        if (set_value_real(val))
        {
                value = val;

                EmitSignalOutput();

                emitChange();
                return true;
        }

        return false;
}

bool OutputLight::set_value(string val)
{
        if (val.compare(0, 8,"impulse ") == 0)
        {
                string tmp = val;
                val.erase(0, 8);
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

                return true;
        }
        else if (val == "toggle")
        {
                return set_value(!value);
        }

        return false;
}

void OutputLight::impulse(int _time)
{
        Utils::logger("output") << Priority::INFO << "OutputLight(" << get_param("id")
                        << "): got impulse action, staying true for "
                        << _time << "ms" << log4cpp::eol;

        time = _time;
        set_value(true);

        if (timer) delete timer;
        timer = new EcoreTimer((double)time / 1000.,
                        (sigc::slot<void>)sigc::mem_fun(*this, &OutputLight::TimerImpulse) );
}

void OutputLight::TimerImpulse()
{
        _set_value(false);

        DELETE_NULL(timer);
}

void OutputLight::impulse_extended(string pattern)
{
        /* Extended impulse to do blinking.
         * It uses a pattern like this one:
         * - "<on_time> <off_time>"
         * - "loop <on_time> <off_time>"
         * - "old" (switch to the old value)
         * they can be combined together to create different blinking effects
         */

        DELETE_NULL(timer);
        blinks.clear();

        Utils::logger("output") << Priority::INFO << "OutputLight(" << get_param("id")
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

                        Utils::logger("output") << Priority::DEBUG << "OutputLight(" << get_param("id")
                                                << ")::Parse : Add blink step " << ((binfo.state)?"True":"False")
                                                << " for " << binfo.duration << "ms" << log4cpp::eol;

                        state = !state;
                }
                else if (tokens[i] == "loop" && loop < 0)
                {
                        //set loop mode to the next item
                        loop = blinks.size();

                        Utils::logger("output") << Priority::DEBUG << "OutputLight("
                                                << get_param("id") << ")::Parse : Loop all next steps." << log4cpp::eol;
                }
                else if (tokens[i] == "old")
                {
                        BlinkInfo binfo;
                        binfo.state = get_value_bool();
                        binfo.duration = 0;
                        binfo.next = blinks.size() + 1;

                        blinks.push_back(binfo);

                        Utils::logger("output") << Priority::DEBUG << "OutputLight(" << get_param("id")
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
                _set_value(blinks[current_blink].state);

                timer = new EcoreTimer((double)blinks[current_blink].duration / 1000.,
                                (sigc::slot<void>)sigc::mem_fun(*this, &OutputLight::TimerImpulseExtended) );
        }
}

void OutputLight::TimerImpulseExtended()
{
        //Stop timer
        DELETE_NULL(timer);

        //safety checks
        if (current_blink < 0 || current_blink >= (int)blinks.size())
                return; //Stops blinking

        current_blink = blinks[current_blink].next;

        //safety checks for new value
        if (current_blink < 0 || current_blink >= (int)blinks.size())
                return; //Stops blinking

        //Set new output state
        _set_value(blinks[current_blink].state);

        //restart timer
        timer = new EcoreTimer((double)blinks[current_blink].duration / 1000.,
                                (sigc::slot<void>)sigc::mem_fun(*this, &OutputLight::TimerImpulseExtended) );
}

