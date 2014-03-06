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
#include <OutputShutterSmart.h>
#include "CalaosConfig.h"
#include <IPC.h>

using namespace Calaos;

OutputShutterSmart::OutputShutterSmart(Params &p):
                Output(p),
                total_time(0),
                time_up(0),
                time_down(0),
                sens(VSTOP),
                old_sens(VUP),
                position(0.0),
                timer_end(NULL),
                timer_update(NULL),
                timer_impulse(NULL),
                timer_up(NULL),
                timer_down(NULL),
                timer_calib(NULL),
                calibrate(false)
{
        set_param("gui_type", "shutter_smart");

        readConfig();

        Utils::logger("output") << Priority::INFO << "OutputShutterSmart::OutputShutterSmart(" << get_param("id") << "): Reading initial value" << log4cpp::eol;

        string initpos;
        if (!Config::Instance().ReadValueIO(get_param("id"), initpos))
                Utils::logger("output") << Priority::ERROR << "OutputShutterSmart::OutputShutterSmart(" << get_param("id") << "): Reading initial value failed!" << log4cpp::eol;
        else
                from_string(initpos, position);

        if (position >= total_time) old_sens = VDOWN;
}

OutputShutterSmart::~OutputShutterSmart()
{
        if (timer_end) delete timer_end;
        if (timer_update) delete timer_update;
        if (timer_impulse) delete timer_impulse;
        if (timer_up) delete timer_up;
        if (timer_down) delete timer_down;

        Utils::logger("output") << Priority::INFO << "OutputShutterSmart::~OutputShutterSmart(): Ok" << log4cpp::eol;
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
        Utils::logger("output") << Priority::INFO << "OutputShutterSmart(" << get_param("id") << "): got action, " << val << log4cpp::eol;

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
                        timer_impulse = new EcoreTimer((double)impulse_time / 1000.,
                                (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerImpulse) );
                }

                timer_calib = new EcoreTimer((double)total_time,
                        (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerCalibrate) );
        }

        EmitSignalOutput();

        string sig = "output ";
        sig += get_param("id") + " ";
        sig += string("state:") + url_encode(get_value_string());
        IPC::Instance().SendEvent("events", sig);

        return true;
}

void OutputShutterSmart::Up(double new_value)
{
        if (calibrate) return;

        if (sens != VSTOP)
        {
                Stop();
                return;
        }

        //calculate position and total_time for up_time range
        double pos = readPosition();
        total_time = time_up;

        if (pos <= 0) return;

        if (timer_end) delete timer_end;
        if (timer_update) delete timer_update;

        setOutputUp(true);
        setOutputDown(false);
        sens = VUP;

        start_time = ecore_time_get();
        start_position = pos;

        if (new_value < 0)
        {
                timer_end = new EcoreTimer(pos,
                        (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerEnd) );
        }
        else
        {
                timer_end = new EcoreTimer(pos - new_value,
                        (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerEnd) );
        }
        timer_update = new EcoreTimer((1. * (double)total_time / 100.),
                (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerUpdate) );

        if (impulse_time > 0)
        {
                if (timer_impulse) delete timer_impulse;
                timer_impulse = new EcoreTimer((double)impulse_time / 1000.,
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
                        EcoreTimer::singleShot(_timer, sigc::mem_fun(*this, &OutputShutterSmart::Stop));
                }
        }
        else
        {
                //Only change cmd_state if are not in impulse_action mode
                cmd_state = "up";
        }

        if (timer_up) delete timer_up;
        timer_up = NULL;
}

void OutputShutterSmart::Down(double new_value)
{
        if (calibrate) return;

        if (sens != VSTOP)
        {
                Stop();
                return;
        }

        //calculate position and total_time for down_time range
        double pos = (readPosition() * (double)time_down) / (double) time_up;
        total_time = time_down;
        new_value = (new_value * (double)time_down) / (double) time_up;

        if (pos >= total_time) return;

        if (timer_end) delete timer_end;
        if (timer_update) delete timer_update;

        setOutputUp(false);
        setOutputDown(true);
        sens = VDOWN;

        start_time = ecore_time_get();
        start_position = pos;

        if (new_value < 0)
        {
                timer_end = new EcoreTimer((double)total_time - pos,
                        (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerEnd) );
        }
        else
        {
                timer_end = new EcoreTimer(new_value - pos,
                        (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerEnd) );
        }
        timer_update = new EcoreTimer((1. * (double)total_time / 100.),
                (sigc::slot<void>)sigc::mem_fun(*this, &OutputShutterSmart::TimerUpdate) );

        if (impulse_time > 0)
        {
                if (timer_impulse) delete timer_impulse;
                timer_impulse = new EcoreTimer((double)impulse_time / 1000.,
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
                        EcoreTimer::singleShot(_timer, sigc::mem_fun(*this, &OutputShutterSmart::Stop));
                }
        }
        else
        {
                //Only change cmd_state if are not in impulse_action mode
                cmd_state = "down";
        }

        if (timer_down) delete timer_down;
        timer_down = NULL;
}

void OutputShutterSmart::DownWait()
{
        if (calibrate) return;

        if (sens != VSTOP)
        {
                if (sens == VDOWN)
                        Stop();
                else
                {
                        Stop();

                        cmd_state = "down";

                        double _t = 200;
                        if (impulse_time >= 0) _t += impulse_time;
                        timer_down = new EcoreTimer(_t / 1000.,
                                        (sigc::slot<void>)sigc::bind(sigc::mem_fun(*this, &OutputShutterSmart::Down), -1) );
                }

                return;
        }

        Down();
}

void OutputShutterSmart::UpWait()
{
        if (calibrate) return;

        if (sens != VSTOP)
        {
                if (sens == VUP)
                        Stop();
                else
                {
                        Stop();

                        cmd_state = "up";

                        double _t = 200;
                        if (impulse_time >= 0) _t += impulse_time;
                        timer_up = new EcoreTimer(_t / 1000.,
                                        (sigc::slot<void>)sigc::bind(sigc::mem_fun(*this, &OutputShutterSmart::Up), -1) );
                }

                return;
        }

        Up();
}

void OutputShutterSmart::Stop()
{
        if (calibrate) return;

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

        sens = VSTOP;
}

void OutputShutterSmart::ImpulseUp(int ms)
{
        is_impulse_action = true;
        impulse_action_time = ms;
        UpWait();
        cmd_state = "impulse up " + Utils::to_string(impulse_action_time);
}

void OutputShutterSmart::ImpulseDown(int ms)
{
        is_impulse_action = true;
        impulse_action_time = ms;
        DownWait();
        cmd_state = "impulse down " + Utils::to_string(impulse_action_time);
}

void OutputShutterSmart::Toggle()
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

void OutputShutterSmart::TimerUpdate()
{
        if (sens == VUP)
        {
                //set new position
                double _t = start_position;
                _t -= (ecore_time_get() - start_time);
                writePosition(_t);
        }
        else if (sens == VDOWN)
        {
                //set new position and conver it to time_up range
                double _t = start_position;
                _t += (ecore_time_get() - start_time);
                _t = (_t * (double)time_up) / (double)time_down;
                writePosition(_t);
        }

        string sig = "output ";
        sig += get_param("id") + " ";
        sig += string("state:") + url_encode(get_value_string());
        IPC::Instance().SendEvent("events", sig);
}

void OutputShutterSmart::TimerEnd()
{
        if (sens == VUP)
                old_sens = VUP;
        else if (sens == VDOWN)
                old_sens = VDOWN;

        string t = cmd_state;
        Stop();
        cmd_state = t;

        string sig = "output ";
        sig += get_param("id") + " ";
        sig += string("state:") + url_encode(get_value_string());
        IPC::Instance().SendEvent("events", sig);
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

        string sig = "output ";
        sig += get_param("id") + " ";
        sig += string("state:") + url_encode(get_value_string());
        IPC::Instance().SendEvent("events", sig);
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

        if (sens == VUP)
                return "up " + Utils::to_string(get_value_double());

        if (sens == VDOWN)
                return "down " + Utils::to_string(get_value_double());

        if (sens == VSTOP)
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
                if ((equal && sens == VSTOP) ||
                    (!equal && sens != VSTOP))
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
