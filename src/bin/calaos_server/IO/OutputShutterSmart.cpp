/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
#include "OutputShutterSmart.h"
#include "CalaosConfig.h"

using namespace Calaos;

OutputShutterSmart::OutputShutterSmart(Params &p):
    IOBase(p, IOBase::IO_OUTPUT),
    total_time(0),
    time_up(0),
    time_down(0),
    sens(SHUTTER_STOP),
    old_sens(SHUTTER_UP),
    position(0.0),
    timer_end(NULL),
    timer_update(NULL),
    timer_impulse(NULL),
    timer_up(NULL),
    timer_down(NULL),
    timer_calib(NULL),
    calibrate(false)
{
    ioDoc->descriptionBaseSet(_("Smart shutter. This shutter calculates the position of the shutter based on the time it takes to open and close. It then allows to set directly the shutter at a specified position."));
    ioDoc->paramAddInt("time_up", _("Time in sec for shutter to be fully open. The more accurate, the better it will work"), 0, 9999, true, 60);
    ioDoc->paramAddInt("time_down", _("Time in sec for shutter to fully closed. The more accurate, the better it will work"), 0, 9999, true, 60);
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
    ioDoc->actionAdd("set 50", _("Set shutter at position X in percent"));
    ioDoc->actionAdd("up 5", _("Open the shutter by X percent"));
    ioDoc->actionAdd("down 5", _("Close the shutter by X percent"));
    ioDoc->actionAdd("calibrate", _("Start calibration on shutter. This opens fully the shutter and resets all internal position values. Use this if shutter sync is lost."));
    ioDoc->actionAdd("set_state true", _("Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source."));
    ioDoc->actionAdd("set_state false", _("Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source."));

    set_param("gui_type", "shutter_smart");

    if (!get_params().Exists("visible")) set_param("visible", "true");
    if (!get_params().Exists("log_history")) set_param("log_history", "true");

    readConfig();

    cInfoDom("output") << "OutputShutterSmart::OutputShutterSmart(" << get_param("id") << "): Reading initial value";

    //read values from cache
    string id = get_param("id") + "_" + get_param("type");
    Params cache;
    if (Config::Instance().ReadValueParams(id, cache))
    {
        Utils::from_string(cache["total_time"], total_time);
        Utils::from_string(cache["time_up"], time_up);
        Utils::from_string(cache["time_down"], time_down);
        Utils::from_string(cache["sens"], sens);
        Utils::from_string(cache["old_sens"], old_sens);
        Utils::from_string(cache["position"], position);
        Utils::from_string(cache["start_time"], start_time);
        Utils::from_string(cache["start_position"], start_position);
        cmd_state = cache["cmd_state"];
    }
}

OutputShutterSmart::~OutputShutterSmart()
{
    if (timer_end) delete timer_end;
    if (timer_update) delete timer_update;
    if (timer_impulse) delete timer_impulse;
    if (timer_up) delete timer_up;
    if (timer_down) delete timer_down;
}

void OutputShutterSmart::readConfig()
{
    if (!get_params().Exists("visible")) set_param("visible", "true");
    Utils::from_string(get_param("time"), total_time);

    if (!get_params().Exists("time_up"))
        time_up = total_time;
    else
        Utils::from_string(get_param("time_up"), time_up);

    if (!get_params().Exists("time_down"))
        time_down = total_time - 2;
    else
        Utils::from_string(get_param("time_down"), time_down);

    if (total_time < time_up)
        total_time = time_up;

    if (get_params().Exists("impulse_time"))
        Utils::from_string(get_param("impulse_time"), impulse_time);
    else
        impulse_time = -1;
}

/* List of actions where value is in percent
**  up
**  down
**  stop
**  set <position>
**  up <value>
**  down <value>
**  toggle
**  impulse up
**  impulse down
**  calibrate
*/
bool OutputShutterSmart::set_value(std::string val)
{
    if (!isEnabled()) return true;

    cInfoDom("output") << "OutputShutterSmart(" << get_param("id") << "): got action, " << val;

    if (calibrate) return false;

    readConfig();

    is_impulse_action = false;

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
    else if (val.compare(0, 4, "set ") == 0)
    {
        val.erase(0, 4);
        int percent;
        double new_position;
        from_string(val, percent);

        cmd_state = "set " + Utils::to_string(percent);

        new_position = (double)percent * (double)time_up / 100.;

        if (new_position < readPosition())
            Up(new_position);
        else if (new_position > readPosition())
            Down(new_position);
    }
    else if (val.compare(0, 3, "up ") == 0)
    {
        val.erase(0, 3);
        int percent;
        double new_position;
        from_string(val, percent);

        cmd_state = "up " + Utils::to_string(percent);

        new_position = (double)percent * (double)time_up / 100.;

        Up(readPosition() - new_position);
    }
    else if (val.compare(0, 5, "down ") == 0)
    {
        val.erase(0, 5);
        int percent;
        double new_position;
        from_string(val, percent);

        cmd_state = "down " + Utils::to_string(percent);

        new_position = (double)percent * (double)time_up / 100.;

        Down(readPosition() + new_position);
    }
    else if (val == "calibrate")
    {
        calibrate = true;

        setOutputUp(true);
        setOutputDown(false);

        if (impulse_time >= 0)
        {
            if (timer_impulse) delete timer_impulse;
            timer_impulse = new Timer((double)impulse_time / 1000.,
                                           (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerImpulse) );
        }

        timer_calib = new Timer((double)total_time,
                                     (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerCalibrate) );
    }
    else if (Utils::strStartsWith(val, "set_state "))
    {
        val.erase(0, 10);

        if (Utils::is_of_type<int>(val))
        {
            int percent;
            Utils::from_string(val, percent);
            if (percent < 0) percent = 0;
            if (percent > 100) percent = 100;
            cmd_state = "set " + Utils::to_string(percent);

            double new_position = (double)percent * (double)time_up / 100.;
            writePosition(new_position);
            sens = SHUTTER_STOP;
        }

        updateCache();
    }
    else
        return false;

    EmitSignalIO();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", get_value_string() } });

    return true;
}

void OutputShutterSmart::Up(double new_value)
{
    if (calibrate) return;

    if (sens != SHUTTER_STOP)
    {
        Stop();
        return;
    }

    //calculate position and total_time for up_time range
    double pos = readPosition();
    total_time = time_up;

    if (pos <= 0)
    {
        updateCache();
        return;
    }

    if (timer_end) delete timer_end;
    if (timer_update) delete timer_update;

    setOutputUp(true);
    setOutputDown(false);
    sens = SHUTTER_UP;

    start_time = Utils::getMainLoopTime();
    start_position = pos;

    if (new_value < 0)
    {
        timer_end = new Timer(pos,
                                   (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerEnd) );
    }
    else
    {
        timer_end = new Timer(pos - new_value,
                                   (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerEnd) );
    }
    timer_update = new Timer((1. * (double)total_time / 100.),
                                  (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerUpdate) );

    if (impulse_time > 0)
    {
        if (timer_impulse) delete timer_impulse;
        timer_impulse = new Timer((double)impulse_time / 1000.,
                                       (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerImpulse) );
    }

    if (is_impulse_action)
    {
        //We want to do an impulse action here
        //We check if down time is bigger than impulse_action_time before firing
        //the timer.

        double _t = pos;
        if (new_value > 0) _t -= new_value;
        if (impulse_action_time + impulse_time < _t * 1000)
        {
            double _timer = (double)(impulse_action_time + impulse_time) / 1000.;
            Timer::singleShot(_timer, sigc::mem_fun(*this, &OutputShutterSmart::Stop));
        }
    }
    else
    {
        //Only change cmd_state if are not in impulse_action mode
        cmd_state = "up";
    }

    if (timer_up) delete timer_up;
    timer_up = NULL;

    updateCache();
}

void OutputShutterSmart::Down(double new_value)
{
    if (calibrate) return;

    if (sens != SHUTTER_STOP)
    {
        Stop();
        return;
    }

    //calculate position and total_time for down_time range
    double pos = (readPosition() * (double)time_down) / (double) time_up;
    total_time = time_down;
    new_value = (new_value * (double)time_down) / (double) time_up;

    if (pos >= total_time)
    {
        updateCache();
        return;
    }

    if (timer_end) delete timer_end;
    if (timer_update) delete timer_update;

    setOutputUp(false);
    setOutputDown(true);
    sens = SHUTTER_DOWN;

    start_time = Utils::getMainLoopTime();
    start_position = pos;

    if (new_value < 0)
    {
        timer_end = new Timer((double)total_time - pos,
                                   (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerEnd) );
    }
    else
    {
        timer_end = new Timer(new_value - pos,
                                   (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerEnd) );
    }
    timer_update = new Timer((1. * (double)total_time / 100.),
                                  (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerUpdate) );

    if (impulse_time > 0)
    {
        if (timer_impulse) delete timer_impulse;
        timer_impulse = new Timer((double)impulse_time / 1000.,
                                       (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerImpulse) );
    }

    if (is_impulse_action)
    {
        //We want to do an impulse action here
        //We check if down time is bigger than impulse_action_time before firing
        //the timer.

        double _t = (double)total_time - pos;
        if (new_value > 0) _t -= new_value;
        if (impulse_action_time + impulse_time < _t * 1000)
        {
            double _timer = (double)(impulse_action_time + impulse_time) / 1000.;
            Timer::singleShot(_timer, sigc::mem_fun(*this, &OutputShutterSmart::Stop));
        }
    }
    else
    {
        //Only change cmd_state if are not in impulse_action mode
        cmd_state = "down";
    }

    if (timer_down) delete timer_down;
    timer_down = NULL;

    updateCache();
}

void OutputShutterSmart::DownWait()
{
    if (calibrate) return;

    if (sens != SHUTTER_STOP)
    {
        if (sens == SHUTTER_DOWN)
            Stop();
        else
        {
            Stop();

            cmd_state = "down";

            double _t = 200;
            if (impulse_time >= 0) _t += impulse_time;
            timer_down = new Timer(_t / 1000.,
                                        (sigc::slot<void>)sigc::bind(sigc::mem_fun(*this, &OutputShutterSmart::Down), -1) );
        }

        updateCache();
        return;
    }

    Down();
}

void OutputShutterSmart::UpWait()
{
    if (calibrate) return;

    if (sens != SHUTTER_STOP)
    {
        if (sens == SHUTTER_UP)
            Stop();
        else
        {
            Stop();

            cmd_state = "up";

            double _t = 200;
            if (impulse_time >= 0) _t += impulse_time;
            timer_up = new Timer(_t / 1000.,
                                      (sigc::slot<void>)sigc::bind(sigc::mem_fun(*this, &OutputShutterSmart::Up), -1) );
        }

        updateCache();
        return;
    }

    Up();
}

void OutputShutterSmart::Stop()
{
    if (calibrate) return;

    cmd_state = "stop";
    if (sens == SHUTTER_STOP) return;

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
            if (sens == SHUTTER_UP)
                setOutputUp(true);
            else
                setOutputDown(true);
        }

        if (timer_impulse) delete timer_impulse;
        timer_impulse = new Timer((double)impulse_time / 1000.,
                                       (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerImpulse) );
    }
    else
    {
        setOutputUp(false);
        setOutputDown(false);
    }

    if (timer_end) delete timer_end;
    timer_end = NULL;
    if (timer_update) delete timer_update;
    timer_update = NULL;

    TimerUpdate();

    sens = SHUTTER_STOP;

    updateCache();
}

void OutputShutterSmart::ImpulseUp(int ms)
{
    is_impulse_action = true;
    impulse_action_time = ms;
    UpWait();
    cmd_state = "impulse up " + Utils::to_string(impulse_action_time);

    updateCache();
}

void OutputShutterSmart::ImpulseDown(int ms)
{
    is_impulse_action = true;
    impulse_action_time = ms;
    DownWait();
    cmd_state = "impulse down " + Utils::to_string(impulse_action_time);

    updateCache();
}

void OutputShutterSmart::Toggle()
{
    if (sens == SHUTTER_UP)
    {
        old_sens = SHUTTER_UP;
        Down();
    }
    else if (sens == SHUTTER_DOWN)
    {
        old_sens = SHUTTER_DOWN;
        Up();
    }
    else
    {
        if (old_sens == SHUTTER_UP)
            Down();
        else
            Up();
    }

    updateCache();
}

void OutputShutterSmart::TimerUpdate()
{
    if (sens == SHUTTER_UP)
    {
        //set new position
        double _t = start_position;
        _t -= (Utils::getMainLoopTime() - start_time);
        writePosition(_t);
    }
    else if (sens == SHUTTER_DOWN)
    {
        //set new position and conver it to time_up range
        double _t = start_position;
        _t += (Utils::getMainLoopTime() - start_time);
        _t = (_t * (double)time_up) / (double)time_down;
        writePosition(_t);
    }

    updateCache();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", get_value_string() } });
}

void OutputShutterSmart::TimerEnd()
{
    if (sens == SHUTTER_UP)
        old_sens = SHUTTER_UP;
    else if (sens == SHUTTER_DOWN)
        old_sens = SHUTTER_DOWN;

    string t = cmd_state;
    Stop();
    cmd_state = t;

    updateCache();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", get_value_string() } });
}

void OutputShutterSmart::TimerImpulse()
{
    setOutputUp(false);
    setOutputDown(false);

    if (timer_impulse)
    {
        delete timer_impulse;
        timer_impulse = NULL;
    }
}

void OutputShutterSmart::TimerCalibrate()
{
    setOutputUp(false);
    setOutputDown(false);

    if (timer_calib)
    {
        delete timer_calib;
        timer_calib = NULL;
    }

    writePosition(0.);

    calibrate = false;

    updateCache();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", get_value_string() } });
}

double OutputShutterSmart::readPosition()
{
    return position;
}

void OutputShutterSmart::writePosition(double p)
{
    position = p;

    if (position < 0) position = 0;
    if (position > time_up) position = time_up;

    Config::Instance().SaveValueIO(get_param("id"), Utils::to_string(position), false);
}

std::string OutputShutterSmart::get_value_string()
{
    if (calibrate)
        return "calibration";

    if (sens == SHUTTER_UP)
        return "up " + Utils::to_string(get_value_double());

    if (sens == SHUTTER_DOWN)
        return "down " + Utils::to_string(get_value_double());

    if (sens == SHUTTER_STOP)
        return "stop " + Utils::to_string(get_value_double());

    return " " + Utils::to_string(get_value_double());
}

bool OutputShutterSmart::check_condition_value(std::string cvalue, bool equal)
{
    if (cvalue == "open" || cvalue == "true")
    {
        if ((equal && get_value_double() < 100) ||
            (!equal && get_value_double() == 100))
            return true;
    }
    else if (cvalue == "closed" || cvalue == "false")
    {
        if ((equal && get_value_double() == 100) ||
            (!equal && get_value_double() < 100))
            return true;
    }
    else if (cvalue == "stop" || cvalue == "stopped")
    {
        if ((equal && sens == SHUTTER_STOP) ||
            (!equal && sens != SHUTTER_STOP))
            return true;
    }
    else if (is_of_type<int>(cvalue))
    {
        int v;
        Utils::from_string(cvalue, v);
        if ((equal && get_value_double() == v) ||
            (!equal && get_value_double() != v))
            return true;
    }

    return false;
}

void OutputShutterSmart::updateCache()
{
    Params p = {{ "total_time", Utils::to_string(total_time) },
                { "time_up", Utils::to_string(time_up) },
                { "time_down", Utils::to_string(time_down) },
                { "sens", Utils::to_string(sens) },
                { "old_sens", Utils::to_string(old_sens) },
                { "position", Utils::to_string(position) },
                { "start_time", Utils::to_string(start_time) },
                { "start_position", Utils::to_string(start_position) },
                { "cmd_state", cmd_state }};

    string id = get_param("id") + "_" + get_param("type");
    Config::Instance().SaveValueParams(id, p, false);
}
