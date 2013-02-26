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
#include <WODigital.h>
#include <IPC.h>

using namespace Calaos;

WODigital::WODigital(Params &p):
                Output(p),
                value(false),
                port(502),
                timer(NULL)
{
        host = get_param("host");

        from_string(get_param("var"), address);

        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);
        if (!get_params().Exists("visible")) set_param("visible", "true");

        if (get_param("knx") == "true")
                address += WAGO_KNX_START_ADDRESS;

        //Do this before translating address to 841/849
        WagoMap::Instance(host, port).read_output_bits((UWord)address, 1, sigc::mem_fun(*this, &WODigital::WagoReadCallback));

        if (get_param("wago_841") == "true" && get_param("knx") != "true")
                address += WAGO_841_START_ADDRESS;

        Calaos::StartReadRules::Instance().addIO();

        Utils::logger("output") << Priority::INFO << "WODigital::WODigital(" << get_param("id") << "): Ok" << log4cpp::eol;
}

WODigital::~WODigital()
{
        if (timer)
        {
                delete timer;
                timer = NULL;
        }

        Utils::logger("output") << Priority::INFO << "WODigital::~WODigital(): Ok" << log4cpp::eol;
}

void WODigital::WagoReadCallback(bool status, UWord address, int count, vector<bool> &values)
{
        if (!status)
        {
                Utils::logger("output") << Priority::ERROR << "WODigital(" << get_param("id") << "): Failed to read value" << log4cpp::eol;
                Calaos::StartReadRules::Instance().ioRead();

                return;
        }

        if (!values.empty())
                value = values[0];

        Utils::logger("output") << Priority::INFO << "WODigital(" << get_param("id") << "): Reading initial value: " << (value?"true":"false") << log4cpp::eol;

        string sig = "output ";
        sig += get_param("id") + " ";
        if (value)
                sig += Utils::url_encode("state:true");
        else
                sig += Utils::url_encode("state:false");
        IPC::Instance().SendEvent("events", sig);

        Calaos::StartReadRules::Instance().ioRead();
}

void WODigital::WagoWriteCallback(bool status, UWord address, bool _value)
{
        if (!status)
        {
                Utils::logger("output") << Priority::ERROR << "WODigital(" << get_param("id") << "): Failed to write value" << log4cpp::eol;
                return;
        }
}

bool WODigital::set_value(bool val)
{
        // Setting a new value will also stop any running impulse actions
        if (timer)
        {
                delete timer;
                timer = NULL;
        }

        return _set_value(val);
}

bool WODigital::_set_value(bool val)
{
        Utils::logger("output") << Priority::INFO << "WODigital(" << get_param("id")
                        << "): got action, " << ((val)?"True":"False") << log4cpp::eol;

        host = get_param("host");
        Utils::from_string(get_param("var"), address);
        if (get_param("knx") == "true")
                address += WAGO_KNX_START_ADDRESS;
        if (get_param("wago_841") == "true" && get_param("knx") != "true")
                address += WAGO_841_START_ADDRESS;
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        WagoMap::Instance(host, port).write_single_bit((UWord)address, val, sigc::mem_fun(*this, &WODigital::WagoWriteCallback));

        value = val;

        EmitSignalOutput();

        string sig = "output ";
        sig += get_param("id") + " ";
        if (value)
                sig += Utils::url_encode("state:true");
        else
                sig += Utils::url_encode("state:false");
        IPC::Instance().SendEvent("events", sig);

        return true;
}

void WODigital::impulse(int _time)
{
        Utils::logger("output") << Priority::INFO << "WODigital(" << get_param("id")
                        << "): got impulse action, staying true for "
                        << _time << "ms" << log4cpp::eol;

        time = _time;
        set_value(true);

        if (timer) delete timer;
        timer = new EcoreTimer((double)time / 1000.,
                        (sigc::slot<void>)sigc::mem_fun(*this, &WODigital::TimerImpulse) );
}

void WODigital::TimerImpulse()
{
        _set_value(false);

        if (timer)
        {
                delete timer;
                timer = NULL;
        }
}

void WODigital::impulse_extended(string pattern)
{
        /* Extended impulse to do blinking.
         * It uses a pattern like this one:
         * - "<on_time> <off_time>"
         * - "loop <on_time> <off_time>"
         * - "old" (switch to the old value)
         * they can be combined together to create different blinking effects
         */

        if (timer)
        {
                delete timer;
                timer = NULL;
        }
        blinks.clear();

        Utils::logger("output") << Priority::INFO << "WODigital(" << get_param("id")
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

                        Utils::logger("output") << Priority::DEBUG << "WODigital(" << get_param("id")
                                                << ")::Parse : Add blink step " << ((binfo.state)?"True":"False")
                                                << " for " << binfo.duration << "ms" << log4cpp::eol;

                        state = !state;
                }
                else if (tokens[i] == "loop" && loop < 0)
                {
                        //set loop mode to the next item
                        loop = blinks.size();

                        Utils::logger("output") << Priority::DEBUG << "WODigital("
                                                << get_param("id") << ")::Parse : Loop all next steps." << log4cpp::eol;
                }
                else if (tokens[i] == "old")
                {
                        BlinkInfo binfo;
                        binfo.state = get_value_bool();
                        binfo.duration = 0;
                        binfo.next = blinks.size() + 1;

                        blinks.push_back(binfo);

                        Utils::logger("output") << Priority::DEBUG << "WODigital(" << get_param("id")
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
                                (sigc::slot<void>)sigc::mem_fun(*this, &WODigital::TimerImpulseExtended) );
        }
}

void WODigital::TimerImpulseExtended()
{
        //Stop timer
        if (timer)
        {
                delete timer;
                timer = NULL;
        }

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
                                (sigc::slot<void>)sigc::mem_fun(*this, &WODigital::TimerImpulseExtended) );
}
