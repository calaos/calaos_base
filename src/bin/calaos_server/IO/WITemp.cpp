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
#include <WITemp.h>
#include <ListeRule.h>
#include <IPC.h>

using namespace Calaos;

WITemp::WITemp(Params &p):
                Input(p),
                port(502),
                value(0.0),
                timer(0.0),
                requestInProgress(false)
{
        host = get_param("host");
        Utils::from_string(get_param("offset"), offset);

        Utils::from_string(get_param("var"), address);
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        if (!get_params().Exists("visible")) set_param("visible", "true");

        ListeRule::Instance().Add(this); //add this specific input to the EventLoop

        requestInProgress = true;
        WagoMap::Instance(host, port).read_words((UWord)address, 1, sigc::mem_fun(*this, &WITemp::WagoReadCallback));

        Calaos::StartReadRules::Instance().addIO();

        Utils::logger("input") << Priority::INFO << "WITemp::WITemp(" << get_param("id") << "): Ok" << log4cpp::eol;
}

WITemp::~WITemp()
{
        Utils::logger("input") << Priority::INFO << "WITemp::~WITemp(): Ok" << log4cpp::eol;
}

void WITemp::WagoReadCallback(bool status, UWord address, int count, vector<UWord> &values)
{
        requestInProgress = false;

        if (!status)
        {
                Utils::logger("input") << Priority::ERROR << "WITemp(" << get_param("id") << "): Failed to read value" << log4cpp::eol;
                Calaos::StartReadRules::Instance().ioRead();

                return;
        }

        double val = value;

        if (!values.empty())
                val = (short int)values[0] / 10.0;

        if (val != value)
        {
                Utils::logger("input") << Priority::INFO << "WITemp:changed(" << get_param("id") << ") : " << get_value_double() << " °C" << log4cpp::eol;

                value = val;
                EmitSignalInput();

                string sig = "input ";
                sig += get_param("id") + " ";
                sig += Utils::url_encode(string("state:") + to_string(get_value_double()));
                IPC::Instance().SendEvent("events", sig);
        }

        Calaos::StartReadRules::Instance().ioRead();
}

void WITemp::hasChanged()
{
        host = get_param("host");
        Utils::from_string(get_param("var"), address);
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        double sec = ecore_time_get() - timer;
        if (sec >= 15)
        {
                timer = ecore_time_get();

                if (!requestInProgress)
                {
                        requestInProgress = true;
                        WagoMap::Instance(host, port).read_words((UWord)address, 1, sigc::mem_fun(*this, &WITemp::WagoReadCallback));
                }
        }
}

double WITemp::get_value_double()
{
        double v;

        if (get_params().Exists("offset"))
        {
                Utils::from_string(get_param("offset"), offset);

                if (offset < 100 && offset > -100)
                        v = value - offset;
                else
                        v = value;
        }
        else
        {
                v = value;
        }

        return v;
}

void WITemp::force_input_double(double v)
{
        value = v;
        EmitSignalInput();

        string sig = "input ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode(string("state:") + to_string(get_value_double()));
        IPC::Instance().SendEvent("events", sig);
}
