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
#include "OutputLightDimmer.h"

using namespace Calaos;

OutputLightDimmer::OutputLightDimmer(Params &p):
    IOBase(p, IOBase::IO_OUTPUT),
    value(0),
    old_value(100),
    press_detected(false),
    press_sens(false),
    stop_after_press(false)
{
    ioDoc->descriptionBaseSet(_("Light with dimming control. Light intensity can be changed for this light."));
    ioDoc->conditionAdd("changed", _("Event on any change of value"));
    ioDoc->conditionAdd("value", _("Event when light is at this value"));
    ioDoc->actionAdd("true", _("Switch the light on"));
    ioDoc->actionAdd("false", _("Switch the light off"));
    ioDoc->actionAdd("toggle", _("Invert the light state"));
    ioDoc->actionAdd("impulse 200", _("Do an impulse on light state. Set to true for X ms then reset to false"));
    ioDoc->actionAdd("impulse 500 200 500 200", _("Do an impulse on light state with a pattern.<br>"
                                                  "Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>"
                                                  "Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>"
                                                  "Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts)"));
    ioDoc->actionAdd("set_state true", _("Update internal light state without starting real action. This is useful when having updating the light state from an external source."));
    ioDoc->actionAdd("set_state false", _("Update internal light state without starting real action. This is useful when having updating the light state from an external source."));
    ioDoc->actionAdd("set_state 50", _("Update internal light state without starting real action. This is useful when having updating the light state from an external source."));

    ioDoc->actionAdd("set off 50", _("Set light value without switching on. This will be the light intensity for the next ON"));
    ioDoc->actionAdd("set 50", _("Set light intensity and swith on if light is off"));
    ioDoc->actionAdd("up 5", _("Increase intensity by X percent"));
    ioDoc->actionAdd("down 5", _("Decrease intensity by X percent"));
    ioDoc->actionAdd("hold press", _("Dynamically change light intensity when holding a switch (press action)"));
    ioDoc->actionAdd("hold stop", _("Dynamically change light intensity when holding a switch (stop action)"));

    set_param("gui_type", "light_dimmer");

    if (!get_params().Exists("visible")) set_param("visible", "true");

    cInfoDom("output") << get_param("id") << ": Ok";
}

OutputLightDimmer::~OutputLightDimmer()
{
    DELETE_NULL(hold_timer);
    DELETE_NULL(impulseTimer);
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
    if (!isEnabled()) return true;

    bool ret = true;

    cInfoDom("output") << get_param("id") << ": got action, " << val;

    // Setting a new value will also stop any running impulse actions
    DELETE_NULL(impulseTimer);

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
        hold_timer = new Timer(0.5, (sigc::slot<void>)sigc::mem_fun(*this, &OutputLightDimmer::HoldPress_cb));
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
    else if (Utils::strStartsWith(val, "set_state "))
    {
        val.erase(0, 10);

        if (val == "true")
        {
            value = old_value;
            cmd_state = "on";
        }
        else if (val == "false")
        {
            old_value = value;
            value = 0;
            cmd_state = "off";
        }
        else if (is_of_type<int>(val))
        {
            int percent;
            Utils::from_string(val, percent);
            if (percent < 0) percent = 0;
            if (percent > 100) percent = 100;

            cmd_state = "set " + Utils::to_string(percent);
            value = percent;
        }
    }
    else
        return false;

    if (!useRealState)
    {
        EmitSignalIO();
        emitChange();
    }

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
    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", get_value_string() } });
}

void OutputLightDimmer::HoldPress_cb()
{
    // press is detect the first timer with 500ms
    if (!press_detected)
    {
        press_detected = true;
        delete hold_timer;
        hold_timer = new Timer(1.0, (sigc::slot<void>)sigc::mem_fun(*this, &OutputLightDimmer::HoldPress_cb));
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
    cInfoDom("output") << get_param("id")
                       << ": got impulse action, staying true for "
                       << _time << "ms";

    set_value(true);

    DELETE_NULL(impulseTimer);
    impulseTimer = new Timer((double)_time / 1000.,
                                  (sigc::slot<void>)sigc::mem_fun(*this, &OutputLightDimmer::TimerImpulse) );
}

void OutputLightDimmer::TimerImpulse()
{
    set_value(false);

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

    cInfoDom("output") << get_param("id")
                       << ": got extended impulse action, parsing blinking pattern...";

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

            cDebugDom("output") << get_param("id")
                                << ": Add blink step " << ((binfo.state)?"True":"False")
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

            cDebugDom("output") << get_param("id")
                                << ": Add blink step " << ((binfo.state)?"True":"False");
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
        set_value(blinks[current_blink].state);

        impulseTimer = new Timer((double)blinks[current_blink].duration / 1000.,
                                      (sigc::slot<void>)sigc::mem_fun(*this, &OutputLightDimmer::TimerImpulseExtended) );
    }
}

void OutputLightDimmer::TimerImpulseExtended()
{
    //Stop timer
    DELETE_NULL(impulseTimer);

    cDebugDom("output") << "Next impulse from pattern.";

    //safety checks
    if (current_blink < 0 || current_blink >= (int)blinks.size())
        return; //Stops blinking

    current_blink = blinks[current_blink].next;

    //safety checks for new value
    if (current_blink < 0 || current_blink >= (int)blinks.size())
        return; //Stops blinking

    cDebugDom("output") << "Next state is: " << (blinks[current_blink].state?"true":"false");

    //Set new output state
    set_value(blinks[current_blink].state);

    //restart timer
    impulseTimer = new Timer((double)blinks[current_blink].duration / 1000.,
                                  (sigc::slot<void>)sigc::mem_fun(*this, &OutputLightDimmer::TimerImpulseExtended) );
}

bool OutputLightDimmer::check_condition_value(std::string cvalue, bool equal)
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
