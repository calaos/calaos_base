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
#include <WOVoletSmart.h>
#include <IPC.h>

using namespace Calaos;

WOVoletSmart::WOVoletSmart(Params &p):
                Output(p),
                port(502),
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
        host = get_param("host");
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

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

        if (get_params().Exists("var_save"))
        {
                Utils::logger("output") << Priority::INFO << "WOVoletSmart::WOVoletSmart(" << get_param("id") << "): Reading initial value" << log4cpp::eol;

                string cmd = "WAGO_INFO_VOLET_GET " + get_param("var_save");
                WagoMap::Instance(host, port).SendUDPCommand(cmd, sigc::mem_fun(*this, &WOVoletSmart::WagoUDPCommand_cb));

                Calaos::StartReadRules::Instance().addIO();
        }

        if (!get_params().Exists("visible")) set_param("visible", "true");

        Utils::logger("output") << Priority::INFO << "WOVoletSmart::WOVoletSmart(" << get_param("id") << "): Ok" << log4cpp::eol;
}

WOVoletSmart::~WOVoletSmart()
{
        if (timer_end) delete timer_end;
        if (timer_update) delete timer_update;
        if (timer_impulse) delete timer_impulse;
        if (timer_up) delete timer_up;
        if (timer_down) delete timer_down;

        Utils::logger("output") << Priority::INFO << "WOVoletSmart::~WOVoletSmart(): Ok" << log4cpp::eol;
}

void WOVoletSmart::WagoUDPCommand_cb(bool status, string command, string result)
{
        if (!status)
        {
                Utils::logger("output") << Priority::INFO << "WODali::WagoUdpCommand(): Error with request " << command << log4cpp::eol;
                Calaos::StartReadRules::Instance().ioRead();

                return;
        }

        if (command.find("WAGO_INFO_VOLET_GET") != string::npos)
        {
                vector<string> tokens;
                split(result, tokens);
                if (tokens.size() >= 3)
                {
                        int _position;
                        from_string(tokens[2], _position);
                        writePosition((double)_position / 1000000.);

                        if (position >= total_time) old_sens = VDOWN;
                }

                string sig = "output ";
                sig += get_param("id") + " ";
                sig += Utils::url_encode(string("state:") + get_value_string());
                IPC::Instance().SendEvent("events", sig);
        }

        Calaos::StartReadRules::Instance().ioRead();
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
bool WOVoletSmart::set_value(std::string val)
{
        host = get_param("host");
        Utils::from_string(get_param("var_up"), up_address);
        Utils::from_string(get_param("var_down"), down_address);
        Utils::from_string(get_param("time"), total_time);

        Utils::logger("output") << Priority::INFO << "WOVoletSmart(" << get_param("id") << "): got action, " << val << log4cpp::eol;

        //handle knx and 841/849
        if (get_param("knx") == "true")
        {
                up_address += WAGO_KNX_START_ADDRESS;
                down_address += WAGO_KNX_START_ADDRESS;
        }
        if (get_param("wago_841") == "true" && get_param("knx") != "true")
        {
                up_address += WAGO_841_START_ADDRESS;
                down_address += WAGO_841_START_ADDRESS;
        }

        if (!get_params().Exists("time_up"))
                time_up = total_time;
        else
                Utils::from_string(get_param("time_up"), time_up);

        if (!get_params().Exists("time_down"))
                time_down = total_time - 2;
        else
                Utils::from_string(get_param("time_down"), time_down);

        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);
        if (get_params().Exists("impulse_time"))
                Utils::from_string(get_param("impulse_time"), impulse_time);
        else
                impulse_time = -1;

        if (calibrate) return false;

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
        else if (val == "stop")
        {
                Stop();
        }
        else if (val.compare(0, 11, "impulse up ") == 0)
        {
                is_impulse_action = true;
                val.erase(0, 11);
                from_string(val, impulse_action_time);
                UpWait();
                cmd_state = "impulse up " + to_string(impulse_action_time);
        }
        else if (val.compare(0, 13, "impulse down ") == 0)
        {
                is_impulse_action = true;
                val.erase(0, 13);
                from_string(val, impulse_action_time);
                DownWait();
                cmd_state = "impulse down " + to_string(impulse_action_time);
        }
        else if (val.compare(0, 4, "set ") == 0)
        {
                val.erase(0, 4);
                int percent;
                double new_position;
                from_string(val, percent);

                cmd_state = "set " + to_string(percent);

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

                cmd_state = "up " + to_string(percent);

                new_position = (double)percent * (double)time_up / 100.;

                Up(readPosition() - new_position);
        }
        else if (val.compare(0, 5, "down ") == 0)
        {
                val.erase(0, 5);
                int percent;
                double new_position;
                from_string(val, percent);

                cmd_state = "down " + to_string(percent);

                new_position = (double)percent * (double)time_up / 100.;

                Down(readPosition() + new_position);
        }
        else if (val == "calibrate")
        {
                calibrate = true;

                WagoMap::Instance(host, port).write_single_bit((UWord)up_address, true, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));
                WagoMap::Instance(host, port).write_single_bit((UWord)down_address, false, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));

                if (impulse_time >= 0)
                {
                        if (timer_impulse) delete timer_impulse;
                        timer_impulse = new EcoreTimer((double)impulse_time / 1000.,
                                (sigc::slot<void>)sigc::mem_fun(*this, &WOVoletSmart::TimerImpulse) );
                }

                timer_calib = new EcoreTimer((double)total_time,
                        (sigc::slot<void>)sigc::mem_fun(*this, &WOVoletSmart::TimerCalibrate) );
        }

        EmitSignalOutput();

        string sig = "output ";
        sig += get_param("id") + " ";
        sig += string("state:") + url_encode(get_value_string());
        IPC::Instance().SendEvent("events", sig);

        return true;
}

void WOVoletSmart::Up(double new_value)
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

        WagoMap::Instance(host, port).write_single_bit((UWord)up_address, true, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));
        WagoMap::Instance(host, port).write_single_bit((UWord)down_address, false, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));
        sens = VUP;

        start_time = ecore_time_get();
        start_position = pos;

        if (new_value < 0)
        {
                timer_end = new EcoreTimer(pos,
                        (sigc::slot<void>)sigc::mem_fun(*this, &WOVoletSmart::TimerEnd) );
        }
        else
        {
                timer_end = new EcoreTimer(pos - new_value,
                        (sigc::slot<void>)sigc::mem_fun(*this, &WOVoletSmart::TimerEnd) );
        }
        timer_update = new EcoreTimer((1. * (double)total_time / 100.),
                (sigc::slot<void>)sigc::mem_fun(*this, &WOVoletSmart::TimerUpdate) );

        if (impulse_time > 0)
        {
                if (timer_impulse) delete timer_impulse;
                timer_impulse = new EcoreTimer((double)impulse_time / 1000.,
                        (sigc::slot<void>)sigc::mem_fun(*this, &WOVoletSmart::TimerImpulse) );
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
                        EcoreTimer::singleShot(_timer, sigc::mem_fun(*this, &WOVoletSmart::Stop));
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

void WOVoletSmart::Down(double new_value)
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

        WagoMap::Instance(host, port).write_single_bit((UWord)up_address, false, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));
        WagoMap::Instance(host, port).write_single_bit((UWord)down_address, true, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));
        sens = VDOWN;

        start_time = ecore_time_get();
        start_position = pos;

        if (new_value < 0)
        {
                timer_end = new EcoreTimer((double)total_time - pos,
                        (sigc::slot<void>)sigc::mem_fun(*this, &WOVoletSmart::TimerEnd) );
        }
        else
        {
                timer_end = new EcoreTimer(new_value - pos,
                        (sigc::slot<void>)sigc::mem_fun(*this, &WOVoletSmart::TimerEnd) );
        }
        timer_update = new EcoreTimer((1. * (double)total_time / 100.),
                (sigc::slot<void>)sigc::mem_fun(*this, &WOVoletSmart::TimerUpdate) );

        if (impulse_time > 0)
        {
                if (timer_impulse) delete timer_impulse;
                timer_impulse = new EcoreTimer((double)impulse_time / 1000.,
                        (sigc::slot<void>)sigc::mem_fun(*this, &WOVoletSmart::TimerImpulse) );
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
                        EcoreTimer::singleShot(_timer, sigc::mem_fun(*this, &WOVoletSmart::Stop));
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

void WOVoletSmart::DownWait()
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
                                        (sigc::slot<void>)sigc::bind(sigc::mem_fun(*this, &WOVoletSmart::Down), -1) );
                }

                return;
        }

        Down();
}

void WOVoletSmart::UpWait()
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
                                        (sigc::slot<void>)sigc::bind(sigc::mem_fun(*this, &WOVoletSmart::Up), -1) );
                }

                return;
        }

        Up();
}

void WOVoletSmart::Stop()
{
        if (calibrate) return;

        cmd_state = "stop";
        if (sens == VSTOP) return;

        if (impulse_time > 0)
        {
                if (get_param("stop_both") != "false")
                {
                        //It seems that most shutter will stop with impulsion on both up and down
                        WagoMap::Instance(host, port).write_single_bit((UWord)up_address, true, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));
                        WagoMap::Instance(host, port).write_single_bit((UWord)down_address, true, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));
                }
                else
                {
                        if (sens == VUP)
                                WagoMap::Instance(host, port).write_single_bit((UWord)up_address, true, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));
                        else
                                WagoMap::Instance(host, port).write_single_bit((UWord)down_address, true, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));
                }

                if (timer_impulse) delete timer_impulse;
                timer_impulse = new EcoreTimer((double)impulse_time / 1000.,
                        (sigc::slot<void>)sigc::mem_fun(*this, &WOVoletSmart::TimerImpulse) );
        }
        else
        {
                WagoMap::Instance(host, port).write_single_bit((UWord)up_address, false, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));
                WagoMap::Instance(host, port).write_single_bit((UWord)down_address, false, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));
        }

        if (timer_end) delete timer_end;
        timer_end = NULL;
        if (timer_update) delete timer_update;
        timer_update = NULL;

        TimerUpdate();

        sens = VSTOP;
}

void WOVoletSmart::TimerUpdate()
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

void WOVoletSmart::TimerEnd()
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

void WOVoletSmart::TimerImpulse()
{
        WagoMap::Instance(host, port).write_single_bit((UWord)up_address, false, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));
        WagoMap::Instance(host, port).write_single_bit((UWord)down_address, false, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));

        if (timer_impulse)
        {
                delete timer_impulse;
                timer_impulse = NULL;
        }
}

void WOVoletSmart::TimerCalibrate()
{
        WagoMap::Instance(host, port).write_single_bit((UWord)up_address, false, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));
        WagoMap::Instance(host, port).write_single_bit((UWord)down_address, false, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));

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

double WOVoletSmart::readPosition()
{
        return position;
}

void WOVoletSmart::writePosition(double p)
{
        position = p;

        if (position < 0) position = 0;
        if (position > time_up) position = time_up;

        if (get_params().Exists("var_save"))
        {
                string cmd = "WAGO_INFO_VOLET_SET " + get_param("var_save") + " " + to_string((int)(position * 1000000));
                WagoMap::Instance(host, port).SendUDPCommand(cmd);
        }
}

std::string WOVoletSmart::get_value_string()
{
        if (calibrate)
                return "calibration";

        if (sens == VUP)
                return "up " + to_string(get_value_double());

        if (sens == VDOWN)
                return "down " + to_string(get_value_double());

        if (sens == VSTOP)
                return "stop " + to_string(get_value_double());

        return " " + to_string(get_value_double());
}

void WOVoletSmart::WagoWriteCallback(bool status, UWord address, bool value)
{
        if (!status)
        {
                Utils::logger("output") << Priority::ERROR << "WOVoletSmart(" << get_param("id") << "): Failed to write value" << log4cpp::eol;
                return;
        }
}
