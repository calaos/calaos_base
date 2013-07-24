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
#include <WOVolet.h>
#include <IPC.h>

using namespace Calaos;

WOVolet::WOVolet(Params &p):
                Output(p),
                sens(VSTOP),
                old_sens(VUP),
                port(502),
                timer_end(NULL),
                timer_impulse(NULL),
                timer_up(NULL),
                timer_down(NULL),
                state_volet("true")
{
        host = get_param("host");
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        if (!get_params().Exists("visible")) set_param("visible", "true");

        Utils::logger("output") << Priority::INFO << "WOVolet::WOVolet(" << get_param("id") << "): Ok" << log4cpp::eol;
}

WOVolet::~WOVolet()
{
        if (timer_end) delete timer_end;
        if (timer_impulse) delete timer_impulse;
        if (timer_up) delete timer_up;
        if (timer_down) delete timer_down;

        Utils::logger("output") << Priority::INFO << "WOVolet::~WOVolet(): Ok" << log4cpp::eol;
}

/* List of actions where value is in percent
**  up
**  down
**  stop
**  toggle
**  impulse up
**  impulse down
*/
bool WOVolet::set_value(std::string val)
{
        Utils::logger("output") << Priority::INFO << "WOVolet(" << get_param("id") << "): got action, " << val << log4cpp::eol;

        Utils::from_string(get_param("var_up"), up_address);
        Utils::from_string(get_param("var_down"), down_address);
        Utils::from_string(get_param("time"), time);

        is_impulse_action = false;

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
                Utils::from_string(val, impulse_action_time);
                UpWait();
                cmd_state = "impulse up " + to_string(impulse_action_time);
        }
        else if (val.compare(0, 13, "impulse down ") == 0)
        {
                is_impulse_action = true;
                val.erase(0, 13);
                Utils::from_string(val, impulse_action_time);
                DownWait();
                cmd_state = "impulse down " + to_string(impulse_action_time);
        }

        EmitSignalOutput();

        string sig = "output ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode(string("state:") + get_value_string());
        IPC::Instance().SendEvent("events", sig);

        return true;
}

void WOVolet::Up()
{
        if (sens != VSTOP)
        {
                Stop();
                return;
        }

        WagoMap::Instance(host, port).write_single_bit((UWord)up_address, true, sigc::mem_fun(*this, &WOVolet::WagoWriteCallback));
        WagoMap::Instance(host, port).write_single_bit((UWord)down_address, false, sigc::mem_fun(*this, &WOVolet::WagoWriteCallback));
        sens = VUP;

        if (impulse_time > 0)
        {
                if (timer_impulse) delete timer_impulse;
                timer_impulse = new EcoreTimer((double)impulse_time / 1000.,
                        (sigc::slot<void>)sigc::mem_fun(*this, &WOVolet::TimerImpulse) );
        }

        if (is_impulse_action)
        {
                //We want to do an impulse action here
                //We check if down time is bigger than impulse_action_time before firing
                //the timer.

                if (impulse_action_time + impulse_time < time * 1000)
                {
                        double _t = (double)(impulse_action_time + impulse_time) / 1000.;
                        EcoreTimer::singleShot(_t, sigc::mem_fun(*this, &WOVolet::Stop));
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
                        (sigc::slot<void>)sigc::mem_fun(*this, &WOVolet::TimerEnd) );
}

void WOVolet::Down()
{
        if (sens != VSTOP)
        {
                Stop();
                return;
        }

        WagoMap::Instance(host, port).write_single_bit((UWord)up_address, false, sigc::mem_fun(*this, &WOVolet::WagoWriteCallback));
        WagoMap::Instance(host, port).write_single_bit((UWord)down_address, true, sigc::mem_fun(*this, &WOVolet::WagoWriteCallback));
        sens = VDOWN;

        if (impulse_time > 0)
        {
                if (timer_impulse) delete timer_impulse;
                timer_impulse = new EcoreTimer((double)impulse_time / 1000.,
                        (sigc::slot<void>)sigc::mem_fun(*this, &WOVolet::TimerImpulse) );
        }

        if (is_impulse_action)
        {
                //We want to do an impulse action here
                //We check if down time is bigger than impulse_action_time before firing
                //the timer.

                if (impulse_action_time + impulse_time < time * 1000)
                {
                        double _t = (double)(impulse_action_time + impulse_time) / 1000.;
                        EcoreTimer::singleShot(_t, sigc::mem_fun(*this, &WOVolet::Stop));
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
                        (sigc::slot<void>)sigc::mem_fun(*this, &WOVolet::TimerEnd) );
}

void WOVolet::DownWait()
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
                                        (sigc::slot<void>)sigc::mem_fun(*this, &WOVolet::Down) );
                }

                return;
        }

        Down();
}

void WOVolet::UpWait()
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
                                        (sigc::slot<void>)sigc::mem_fun(*this, &WOVolet::Up) );
                }

                return;
        }

        Up();
}

void WOVolet::Stop()
{
        cmd_state = "stop";
        if (sens == VSTOP) return;

        if (impulse_time > 0)
        {
                if (get_param("stop_both") != "false")
                {
                        //It seems that most shutter will stop with impulsion on both up and down
                        WagoMap::Instance(host, port).write_single_bit((UWord)up_address, true, sigc::mem_fun(*this, &WOVolet::WagoWriteCallback));
                        WagoMap::Instance(host, port).write_single_bit((UWord)down_address, true, sigc::mem_fun(*this, &WOVolet::WagoWriteCallback));
                }
                else
                {
                        if (sens == VUP)
                                WagoMap::Instance(host, port).write_single_bit((UWord)up_address, true, sigc::mem_fun(*this, &WOVolet::WagoWriteCallback));
                        else
                                WagoMap::Instance(host, port).write_single_bit((UWord)down_address, true, sigc::mem_fun(*this, &WOVolet::WagoWriteCallback));
                }

                if (timer_impulse) delete timer_impulse;
                timer_impulse = new EcoreTimer((double)impulse_time / 1000.,
                        (sigc::slot<void>)sigc::mem_fun(*this, &WOVolet::TimerImpulse) );
        }
        else
        {
                WagoMap::Instance(host, port).write_single_bit((UWord)up_address, false, sigc::mem_fun(*this, &WOVolet::WagoWriteCallback));
                WagoMap::Instance(host, port).write_single_bit((UWord)down_address, false, sigc::mem_fun(*this, &WOVolet::WagoWriteCallback));
        }

        sens = VSTOP;

        if (timer_end)
        {
                delete timer_end;
                timer_end = NULL;
        }
}

void WOVolet::TimerEnd()
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

        string sig = "output ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode(string("state:") + get_value_string());
        IPC::Instance().SendEvent("events", sig);
}

void WOVolet::TimerImpulse()
{
        WagoMap::Instance(host, port).write_single_bit((UWord)up_address, false, sigc::mem_fun(*this, &WOVolet::WagoWriteCallback));
        WagoMap::Instance(host, port).write_single_bit((UWord)down_address, false, sigc::mem_fun(*this, &WOVolet::WagoWriteCallback));

        if (timer_impulse)
        {
                delete timer_impulse;
                timer_impulse = NULL;
        }
}

void WOVolet::WagoWriteCallback(bool status, UWord address, bool value)
{
        if (!status)
        {
                Utils::logger("output") << Priority::ERROR << "WOVolet(" << get_param("id") << "): Failed to write value" << log4cpp::eol;
                return;
        }
}
