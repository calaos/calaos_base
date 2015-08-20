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
#include "OutputShutter.h"

using namespace Calaos;

OutputShutter::OutputShutter(Params &p):
    IOBase(p, IOBase::IO_OUTPUT),
    sens(VSTOP),
    old_sens(VUP),
    timer_end(NULL),
    timer_impulse(NULL),
    timer_up(NULL),
    timer_down(NULL),
    state_volet("true")
{
    ioDoc->descriptionBaseSet(_("Simple shutter. This shutter supports open/close states, as well as impulse shutters."));
    ioDoc->paramAddInt("time", _("Time in sec for shutter to open or close"), 0, 9999, true, 60);
    ioDoc->paramAddInt("impulse_time", _("Impulse time for shutter that needs impulse instead of holding up/down relays. If set to 0 impulse shutter is disabled. Time is in ms. Default to 0"), 0, 999999, false, 0);
    ioDoc->paramAdd("stop_both", _("If in impulse mode, some shutters needs to activate both up dans down relays when stopping the shutter"), IODoc::TYPE_BOOL, false);
    ioDoc->conditionAdd("changed", _("Event on any change of shutter state"));
    ioDoc->conditionAdd("true", _("Event when shutter is open"));
    ioDoc->conditionAdd("false", _("Event when shutter is closed"));
    ioDoc->actionAdd("up", _("Open the shutter"));
    ioDoc->actionAdd("down", _("Close the shutter"));
    ioDoc->actionAdd("stop", _("Stop the shutter"));
    ioDoc->actionAdd("toggle", _("Invert shutter state"));
    ioDoc->actionAdd("impulse up 200", _("Open shutter for X ms"));
    ioDoc->actionAdd("impulse down 200", _("Close shutter for X ms"));
    ioDoc->actionAdd("set_state true", _("Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source."));
    ioDoc->actionAdd("set_state false", _("Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source."));

    set_param("gui_type", "shutter");

    if (!get_params().Exists("visible")) set_param("visible", "true");

    cInfoDom("output") << "OutputShutter::OutputShutter(" << get_param("id") << "): Ok";
}

OutputShutter::~OutputShutter()
{
    if (timer_end) delete timer_end;
    if (timer_impulse) delete timer_impulse;
    if (timer_up) delete timer_up;
    if (timer_down) delete timer_down;

    cInfoDom("output") << "OutputShutter::~OutputShutter(): Ok";
}

bool OutputShutter::set_value(std::string val)
{
    if (!isEnabled()) return true;

    cInfoDom("output") << "OutputShutter(" << get_param("id") << "): got action, " << val;

    Utils::from_string(get_param("time"), time);

    is_impulse_action = false;

    if (get_params().Exists("impulse_time"))
        Utils::from_string(get_param("impulse_time"), impulse_time);
    else
        impulse_time = -1;

    if (val == "up")
    {
        UpWait();
    }
    else if (val == "down")
    {
        DownWait();
    }
    else if (val == "toggle")
    {
        Toggle();
    }
    else if (val == "stop")
    {
        Stop();
    }
    else if (val.compare(0, 11, "impulse up ") == 0)
    {
        val.erase(0, 11);
        int v;
        from_string(val, v);
        ImpulseUp(v);
    }
    else if (val.compare(0, 13, "impulse down ") == 0)
    {
        val.erase(0, 11);
        int v;
        from_string(val, v);
        ImpulseDown(v);
    }
    else if (Utils::strStartsWith(val, "set_state "))
    {
        val.erase(0, 10);

        if (val == "up")
        {
            state_volet = "true";
            cmd_state = "up";
        }
        else if (val == "down")
        {
            state_volet = "false";
            cmd_state = "down";
        }
    }
    else
        return false;

    EmitSignalIO();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", get_value_string() } });

    return true;
}

void OutputShutter::ImpulseUp(int ms)
{
    is_impulse_action = true;
    impulse_action_time = ms;
    UpWait();
    cmd_state = "impulse up " + Utils::to_string(impulse_action_time);
}

void OutputShutter::ImpulseDown(int ms)
{
    is_impulse_action = true;
    impulse_action_time = ms;
    DownWait();
    cmd_state = "impulse down " + Utils::to_string(impulse_action_time);
}

void OutputShutter::Toggle()
{
    if (sens == VUP)
    {
        old_sens = VUP;
        Down();
    }
    else if (sens == VDOWN)
    {
        old_sens = VDOWN;
        Up();
    }
    else if (sens == VSTOP)
    {
        if (old_sens == VUP)
            Down();
        else
            Up();
    }
}

void OutputShutter::Up()
{
    if (sens != VSTOP)
    {
        Stop();
        return;
    }

    setOutputUp(true);
    setOutputDown(false);
    sens = VUP;

    if (impulse_time > 0)
    {
        if (timer_impulse) delete timer_impulse;
        timer_impulse = new EcoreTimer((double)impulse_time / 1000.,
                                       (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutter::TimerImpulse) );
    }

    if (is_impulse_action)
    {
        //We want to do an impulse action here
        //We check if down time is bigger than impulse_action_time before firing
        //the timer.

        if (impulse_action_time + impulse_time < time * 1000)
        {
            double _t = (double)(impulse_action_time + impulse_time) / 1000.;
            EcoreTimer::singleShot(_t, sigc::mem_fun(*this, &OutputShutter::Stop));
        }
    }
    else
    {
        //Only change cmd_state if are not in impulse_action mode
        cmd_state = "up";
    }

    if (timer_up) delete timer_up;
    timer_up = NULL;

    if (timer_end) delete timer_end;
    timer_end = new EcoreTimer((double)time,
                               (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutter::TimerEnd) );
}

void OutputShutter::Down()
{
    if (sens != VSTOP)
    {
        Stop();
        return;
    }

    setOutputUp(false);
    setOutputDown(true);
    sens = VDOWN;

    if (impulse_time > 0)
    {
        if (timer_impulse) delete timer_impulse;
        timer_impulse = new EcoreTimer((double)impulse_time / 1000.,
                                       (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutter::TimerImpulse) );
    }

    if (is_impulse_action)
    {
        //We want to do an impulse action here
        //We check if down time is bigger than impulse_action_time before firing
        //the timer.

        if (impulse_action_time + impulse_time < time * 1000)
        {
            double _t = (double)(impulse_action_time + impulse_time) / 1000.;
            EcoreTimer::singleShot(_t, sigc::mem_fun(*this, &OutputShutter::Stop));
        }
    }
    else
    {
        //Only change cmd_state if are not in impulse_action mode
        cmd_state = "down";
    }

    if (timer_down) delete timer_down;
    timer_down = NULL;

    if (timer_end) delete timer_end;
    timer_end = new EcoreTimer((double)time,
                               (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutter::TimerEnd) );
}

void OutputShutter::DownWait()
{
    if (sens != VSTOP)
    {
        if (sens == VDOWN)
        {
            Stop();
            return;
        }
        else
        {
            Stop();

            cmd_state = "down";

            double _t = 200;
            if (impulse_time >= 0) _t += impulse_time;
            timer_down = new EcoreTimer(_t / 1000.,
                                        (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutter::Down) );
        }

        return;
    }

    Down();
}

void OutputShutter::UpWait()
{
    if (sens != VSTOP)
    {
        if (sens == VUP)
        {
            Stop();
            return;
        }
        else
        {
            Stop();

            cmd_state = "up";

            double _t = 200;
            if (impulse_time >= 0) _t += impulse_time;
            timer_up = new EcoreTimer(_t / 1000.,
                                      (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutter::Up) );
        }

        return;
    }

    Up();
}

void OutputShutter::Stop()
{
    cmd_state = "stop";
    if (sens == VSTOP) return;

    if (impulse_time > 0)
    {
        if (get_param("stop_both") != "false")
        {
            //It seems that most shutter will stop with impulsion on both up and down
            setOutputUp(true);
            setOutputDown(true);
        }
        else
        {
            if (sens == VUP)
                setOutputUp(true);
            else
                setOutputDown(true);
        }

        if (timer_impulse) delete timer_impulse;
        timer_impulse = new EcoreTimer((double)impulse_time / 1000.,
                                       (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutter::TimerImpulse) );
    }
    else
    {
        setOutputUp(false);
        setOutputDown(false);
    }

    sens = VSTOP;

    if (timer_end)
    {
        delete timer_end;
        timer_end = NULL;
    }
}

void OutputShutter::TimerEnd()
{
    if (sens == VUP)
    {
        old_sens = VUP;
        state_volet = "true";
    }
    else if (sens == VDOWN)
    {
        old_sens = VDOWN;
        state_volet = "false";
    }

    string t = cmd_state;
    Stop();
    cmd_state = t;

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", get_value_string() } });
}

void OutputShutter::TimerImpulse()
{
    setOutputUp(false);
    setOutputDown(false);

    if (timer_impulse)
    {
        delete timer_impulse;
        timer_impulse = NULL;
    }
}

bool OutputShutter::check_condition_value(std::string cvalue, bool equal)
{
    if (cvalue == "open" || cvalue == "true")
    {
        if ((equal && get_value_string() == "true") ||
            (!equal && get_value_string() == "false"))
            return true;
    }
    else if (cvalue == "closed" || cvalue == "false")
    {
        if ((equal && get_value_string() == "false") ||
            (!equal && get_value_string() == "true"))
            return true;
    }
    else if (cvalue == "stop" || cvalue == "stopped")
    {
        if ((equal && sens == VSTOP) ||
            (!equal && sens != VSTOP))
            return true;
    }

    return false;
}
