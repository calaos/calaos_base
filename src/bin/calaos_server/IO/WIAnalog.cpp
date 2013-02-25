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
#include <WIAnalog.h>
#include <ListeRule.h>
#include <IPC.h>

using namespace Calaos;

WIAnalog::WIAnalog(Params &p):
                Input(p),
                real_value_max(0.0),
                wago_value_max(0.0),
                port(502),
                value(0.0),
                timer(0.0),
                frequency(15.0), // 15 sec. between each read
                requestInProgress(false),
                start(true)
{
        readConfig();

        ListeRule::Instance().Add(this); //add this specific input to the EventLoop

        WagoMap::Instance(host, port).read_words((UWord)address, 1, sigc::mem_fun(*this, &WIAnalog::WagoReadCallback));
        requestInProgress = true;

        Calaos::StartReadRules::Instance().addIO();

        Utils::logger("input") << Priority::INFO << "WIAnalog::WIAnalog(" << get_param("id") << "): Ok" << log4cpp::eol;
}

WIAnalog::~WIAnalog()
{
        Utils::logger("input") << Priority::INFO << "WIAnalog::~WIAnalog(): Ok" << log4cpp::eol;
}

void WIAnalog::readConfig()
{
        host = get_param("host");
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        Utils::from_string(get_param("var"), address);

        if (!get_params().Exists("visible")) set_param("visible", "true");

        if (get_params().Exists("real_max")) Utils::from_string(get_param("real_max"), real_value_max);
        if (get_params().Exists("wago_max")) Utils::from_string(get_param("wago_max"), wago_value_max);

        if (get_params().Exists("frequency")) Utils::from_string(get_param("frequency"), frequency);
}

void WIAnalog::WagoReadCallback(bool status, UWord address, int count, vector<UWord> &values)
{
        requestInProgress = false;

        if (!status)
        {
                Utils::logger("input") << Priority::ERROR << "WIAnalog(" << get_param("id") << "): Failed to read value" << log4cpp::eol;
                Calaos::StartReadRules::Instance().ioRead();

                return;
        }

        double val = value;

        if (!values.empty())
        {
                val = values[0];
        }

        if (val != value)
        {
                value = val;
                EmitSignalInput();

                string sig = "input ";
                sig += get_param("id") + " ";
                sig += Utils::url_encode(string("state:") + to_string(get_value_double()));
                IPC::Instance().SendEvent("events", sig);

                Utils::logger("input") << Priority::INFO << "WIAnalog:changed(" << get_param("id") << ") : " << get_value_double() << log4cpp::eol;
        }

        if (start)
        {
            Calaos::StartReadRules::Instance().ioRead();
            start = false;
        }
}

void WIAnalog::hasChanged()
{
        readConfig();

        double sec = ecore_time_get() - timer;
        if (sec >= frequency)
        {
                timer = ecore_time_get();

                if (!requestInProgress)
                {
                        requestInProgress = true;
                        WagoMap::Instance(host, port).read_words((UWord)address, 1, sigc::mem_fun(*this, &WIAnalog::WagoReadCallback));
                }
        }
}

double WIAnalog::get_value_double()
{
        readConfig();

        Utils::logger("input") << Priority::DEBUG << "WIAnalog::get_value_double(" << get_param("id") << "): "
                                << value << " * " << real_value_max << " / " << wago_value_max << log4cpp::eol;

        if (wago_value_max > 0 && real_value_max > 0)
                return Utils::roundValue(value * real_value_max / wago_value_max);
        else
                return Utils::roundValue(value);
}

void WIAnalog::force_input_double(double v)
{
        if (wago_value_max > 0 && real_value_max > 0)
                value = v * wago_value_max / real_value_max;
        else
                value = v;

        EmitSignalInput();

        string sig = "input ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode(string("state:") + to_string(v));
        IPC::Instance().SendEvent("events", sig);
}
