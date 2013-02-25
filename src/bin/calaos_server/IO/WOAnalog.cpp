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
#include <WOAnalog.h>
#include <IPC.h>

using namespace Calaos;
using namespace Utils;

WOAnalog::WOAnalog(Params &p):
                Output(p),
                port(502),
                value(-1),
                real_value_max(0.0),
                wago_value_max(0.0)
{
        readConfig();

        WagoMap::Instance(host, port).read_words((UWord)address + 0x200, 1, sigc::mem_fun(*this, &WOAnalog::WagoReadCallback));

        Calaos::StartReadRules::Instance().addIO();

        Utils::logger("output") << Priority::INFO << "WOAnalog::WOAnalog(" << get_param("id") << "): Ok" << log4cpp::eol;
}

WOAnalog::~WOAnalog()
{
        Utils::logger("output") << Priority::INFO << "WOAnalog::~WOAnalog(): Ok" << log4cpp::eol;
}

void WOAnalog::readConfig()
{
        host = get_param("host");
        Utils::from_string(get_param("var"), address);
        if (get_params().Exists("real_max")) Utils::from_string(get_param("real_max"), real_value_max);
        if (get_params().Exists("wago_max")) Utils::from_string(get_param("wago_max"), wago_value_max);

        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        if (!get_params().Exists("visible")) set_param("visible", "true");
}

double WOAnalog::get_value_double()
{
        readConfig();

        if (wago_value_max > 0 && real_value_max > 0)
                return Utils::roundValue(value * real_value_max / wago_value_max);
        else
                return Utils::roundValue(value);
}

void WOAnalog::WagoReadCallback(bool status, UWord address, int count, vector<UWord> &values)
{
        if (!status)
        {
                Utils::logger("output") << Priority::ERROR << "WOAnalog(" << get_param("id") << "): Failed to read value" << log4cpp::eol;
                Calaos::StartReadRules::Instance().ioRead();

                return;
        }

        if (!values.empty()) value = values[0];

        string sig = "output ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode(string("state:") + Utils::to_string(value));
        IPC::Instance().SendEvent("events", sig);

        Calaos::StartReadRules::Instance().ioRead();
}

void WOAnalog::WagoWriteCallback(bool status, UWord address, UWord _value)
{
        if (!status)
        {
                Utils::logger("output") << Priority::ERROR << "WOAnalog(" << get_param("id") << "): Failed to write value" << log4cpp::eol;
                return;
        }

        value = _value;

        UWord v;

        readConfig();

        if (wago_value_max > 0 && real_value_max > 0)
                v = (UWord)(value * wago_value_max / real_value_max);
        else
                v = (UWord)(value);

        Utils::logger("output") << Priority::INFO << "WOAnalog(" << get_param("id") << "), executed action " << value << " (" << v << ")" << log4cpp::eol;
}

bool WOAnalog::set_value(double val)
{
        UWord v;

        readConfig();

        if (wago_value_max > 0 && real_value_max > 0)
                v = (UWord)(val * wago_value_max / real_value_max);
        else
                v = (UWord)(val);

        WagoMap::Instance(host, port).write_single_word((UWord)address, v, sigc::mem_fun(*this, &WOAnalog::WagoWriteCallback));

        value = val;

        EmitSignalOutput();

        string sig = "output ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode(string("state:") + Utils::to_string(value));
        IPC::Instance().SendEvent("events", sig);

        return true;
}
