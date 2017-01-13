/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include "OutputLight.h"

using namespace Calaos;

OutputLight::OutputLight(Params &p):
    IOBase(p, IOBase::IO_OUTPUT),
    value(false)
{
    ioDoc->descriptionBaseSet(_("Basic light. This light have only 2 states, ON or OFF. Can also be used to control simple relays output"));
    ioDoc->conditionAdd("changed", _("Event on any change of value"));
    ioDoc->conditionAdd("true", _("Event when light is on"));
    ioDoc->conditionAdd("false", _("Event when light is off"));
    ioDoc->actionAdd("true", _("Switch the light on"));
    ioDoc->actionAdd("false", _("Switch the light off"));
    ioDoc->actionAdd("toggle", _("Invert light state"));
    ioDoc->actionAdd("impulse 200", _("Do an impulse on light state. Set to true for X ms then reset to false"));
    ioDoc->actionAdd("impulse 500 200 500 200", _("Do an impulse on light state with a pattern.<br>"
                                                  "Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>"
                                                  "Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>"
                                                  "Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts)"));
    ioDoc->actionAdd("set_state true", _("Update internal light state without starting real action. This is useful when having updating the light state from an external source."));
    ioDoc->actionAdd("set_state false", _("Update internal light state without starting real action. This is useful when having updating the light state from an external source."));

    if (!get_params().Exists("visible")) set_param("visible", "true");

    set_param("gui_type", "light");
}

OutputLight::~OutputLight()
{
    DELETE_NULL(timer);
}

void OutputLight::emitChange()
{   
    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", value?"true":"false" } });
}

bool OutputLight::set_value(bool val)
{
    if (!isEnabled()) return true;

    // Setting a new value will also stop any running impulse actions
    DELETE_NULL(timer);

    return _set_value(val);
}

bool OutputLight::_set_value(bool val)
{
    cInfoDom("output") << get_param("id") << ": got action, " << ((val)?"True":"False");

    if (set_value_real(val))
    {
        if (!useRealState)
        {
            value = val;

            EmitSignalIO();

            emitChange();
        }
        return true;
    }

    return false;
}

bool OutputLight::set_value(string val)
{
    if (!isEnabled()) return true;

    if (val.compare(0, 8,"impulse ") == 0)
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

        return true;
    }
    else if (val == "toggle")
    {
        return set_value(!value);
    }
    else if (val == "true" || val == "on")
    {
        return set_value(true);
    }
    else if (val == "false" || val == "off")
    {
        return set_value(false);
    }
    else if (val == "set_state true")
    {
        value = true;
        EmitSignalIO();
        emitChange();
        return true;
    }
    else if (val == "set_state false")
    {
        value = false;
        EmitSignalIO();
        emitChange();
        return true;
    }

    return false;
}

void OutputLight::impulse(int time)
{
    cInfoDom("output") << get_param("id") << ": got impulse action, staying true for "
                       << time << "ms";

    set_value(true);

    if (timer) delete timer;
    timer = new Timer((double)time / 1000.,
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

    cInfoDom("output") << get_param("id") << ": got extended impulse action, parsing blinking pattern...";

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

            cDebugDom("output") << get_param("id") << ": Add blink step " << ((binfo.state)?"True":"False")
                                << " for " << binfo.duration << "ms";

            state = !state;
        }
        else if (tokens[i] == "loop" && loop < 0)
        {
            //set loop mode to the next item
            loop = blinks.size();

            cDebugDom("output") << get_param("id") << ": Loop all next steps.";
        }
        else if (tokens[i] == "old")
        {
            BlinkInfo binfo;
            binfo.state = get_value_bool();
            binfo.duration = 0;
            binfo.next = blinks.size() + 1;

            blinks.push_back(binfo);

            cDebugDom("output") <<  get_param("id") << ": Add blink step " << ((binfo.state)?"True":"False");
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

        timer = new Timer((double)blinks[current_blink].duration / 1000.,
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
    timer = new Timer((double)blinks[current_blink].duration / 1000.,
                           (sigc::slot<void>)sigc::mem_fun(*this, &OutputLight::TimerImpulseExtended) );
}

