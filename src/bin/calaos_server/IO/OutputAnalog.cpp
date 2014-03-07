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
#include <OutputAnalog.h>
#include <IPC.h>

using namespace Calaos;
using namespace Utils;

OutputAnalog::OutputAnalog(Params &p):
                Output(p),
                value(-1),
                real_value_max(0.0),
                wago_value_max(0.0)
{
        set_param("gui_type", "analog_out");

        readConfig();

        Calaos::StartReadRules::Instance().addIO();

        cInfoDom("output") << "OutputAnalog::OutputAnalog(" << get_param("id") << "): Ok";
}

OutputAnalog::~OutputAnalog()
{
        cInfoDom("output") << "OutputAnalog::~OutputAnalog(): Ok";
}

void OutputAnalog::readConfig()
{
        if (get_params().Exists("real_max")) Utils::from_string(get_param("real_max"), real_value_max);
        if (get_params().Exists("wago_max")) Utils::from_string(get_param("wago_max"), wago_value_max);
        if (!get_params().Exists("visible")) set_param("visible", "true");
}

double OutputAnalog::get_value_double()
{
        readConfig();

        if (wago_value_max > 0 && real_value_max > 0)
                return Utils::roundValue(value * real_value_max / wago_value_max);
        else
                return Utils::roundValue(value);
}

void OutputAnalog::emitChange()
{
        string sig = "output ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode(string("state:") + Utils::to_string(value));
        IPC::Instance().SendEvent("events", sig);
}

bool OutputAnalog::set_value(double val)
{
        UWord v;

        readConfig();

        if (wago_value_max > 0 && real_value_max > 0)
                v = (UWord)(val * wago_value_max / real_value_max);
        else
                v = (UWord)(val);

        set_value_real(v);

        value = val;
        EmitSignalOutput();
        emitChange();

        return true;
}

bool OutputAnalog::set_value(string val)
{
        if (val == "inc")
        {
                double step = 1.0;
                if (is_of_type<double>(get_param("step")))
                        from_string(get_param("step"), step);

                set_value(value + step);
        }
        else if (val == "dec")
        {
                double step = 1.0;
                if (is_of_type<double>(get_param("step")))
                        from_string(get_param("step"), step);

                set_value(value - step);
        }
        else if (val.compare(0, 4, "inc ") == 0)
        {
                string t = val;
                t.erase(0, 4);

                double step = 1.0;
                if (is_of_type<double>(t))
                        from_string(t, step);

                set_value(value + step);
        }
        else if (val.compare(0, 4, "dec ") == 0)
        {
                string t = val;
                t.erase(0, 4);

                double step = 1.0;
                if (is_of_type<double>(t))
                        from_string(t, step);

                set_value(value - step);
        }

        return true;
}

