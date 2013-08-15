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
#include <InputTemp.h>
#include <Ecore.h>
#include <ListeRule.h>
#include <IPC.h>

using namespace Calaos;

InputTemp::InputTemp(Params &p):
                Input(p),
                value(0.0),
                timer(0.0),
                readTime(15.0)
{
        Utils::from_string(get_param("offset"), offset);
        if (!get_params().Exists("visible")) set_param("visible", "true");
        if (!get_params().Exists("interval"))
                Utils::from_string(get_param("interval"), readTime);

        ListeRule::Instance().Add(this); //add this specific input to the EventLoop

        Calaos::StartReadRules::Instance().addIO();

        Utils::logger("input") << Priority::INFO << "InputTemp::InputTemp(" << get_param("id") << "): Ok" << log4cpp::eol;
}

InputTemp::~InputTemp()
{
        Utils::logger("input") << Priority::INFO << "InputTemp::~InputTemp(): Ok" << log4cpp::eol;
}

void InputTemp::hasChanged()
{
        if (!get_params().Exists("interval"))
                Utils::from_string(get_param("interval"), readTime);

        double sec = ecore_time_get() - timer;
        if (sec >= readTime)
        {
                timer = ecore_time_get();

                readValue();
        }
}

double InputTemp::get_value_double()
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

void InputTemp::emitChange()
{
        Utils::logger("input") << Priority::INFO << "WITemp:changed(" << get_param("id") << ") : " << get_value_double() << " °C" << log4cpp::eol;

        EmitSignalInput();

        string sig = "input ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode(string("state:") + to_string(get_value_double()));
        IPC::Instance().SendEvent("events", sig);
}

void InputTemp::force_input_double(double v)
{
        value = v;
        emitChange();
}
