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
#include <InputAnalog.h>
#include <ListeRule.h>
#include <IPC.h>
#include <Ecore.h>

using namespace Calaos;

InputAnalog::InputAnalog(Params &p):
                Input(p),
                real_value_max(0.0),
                wago_value_max(0.0),
                value(0.0),
                timer(0.0),
                frequency(15.0) // 15 sec. between each read
{
        readConfig();

        ListeRule::Instance().Add(this); //add this specific input to the EventLoop

        Calaos::StartReadRules::Instance().addIO();

        Utils::logger("input") << Priority::INFO << "InputAnalog::InputAnalog(" << get_param("id") << "): Ok" << log4cpp::eol;
}

InputAnalog::~InputAnalog()
{
        Utils::logger("input") << Priority::INFO << "InputAnalog::~InputAnalog(): Ok" << log4cpp::eol;
}

void InputAnalog::readConfig()
{
        if (!get_params().Exists("visible")) set_param("visible", "true");
        if (get_params().Exists("real_max")) Utils::from_string(get_param("real_max"), real_value_max);
        if (get_params().Exists("wago_max")) Utils::from_string(get_param("wago_max"), wago_value_max);
        if (get_params().Exists("frequency")) Utils::from_string(get_param("frequency"), frequency);
}

void InputAnalog::emitChange()
{
        EmitSignalInput();

        string sig = "input ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode(string("state:") + to_string(get_value_double()));
        IPC::Instance().SendEvent("events", sig);

        Utils::logger("input") << Priority::INFO << "InputAnalog:changed(" << get_param("id") << ") : " << get_value_double() << log4cpp::eol;
}

void InputAnalog::hasChanged()
{
        readConfig();

        double sec = ecore_time_get() - timer;
        if (sec >= frequency)
        {
                timer = ecore_time_get();

                readValue();
        }
}

double InputAnalog::get_value_double()
{
        readConfig();

        Utils::logger("input") << Priority::DEBUG << "InputAnalog::get_value_double(" << get_param("id") << "): "
                                << value << " * " << real_value_max << " / " << wago_value_max << log4cpp::eol;

        if (wago_value_max > 0 && real_value_max > 0)
                return Utils::roundValue(value * real_value_max / wago_value_max);
        else
                return Utils::roundValue(value);
}

void InputAnalog::force_input_double(double v)
{
        if (wago_value_max > 0 && real_value_max > 0)
                value = v * wago_value_max / real_value_max;
        else
                value = v;

        emitChange();
}
