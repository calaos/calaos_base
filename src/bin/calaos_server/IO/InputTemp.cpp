/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos.
 **
 **  Calaos is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos is distributed in the hope that it will be useful,
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
    timer(0.0)
{
    set_param("gui_type", "temp");

    coeff_a = 1.0;
    coeff_b = 0.0;
    timer = ecore_time_get();
    if (get_params().Exists("coeff_a"))
      Utils::from_string(get_param("coeff_a"), coeff_a);
    if (get_params().Exists("coeff_b"))
      Utils::from_string(get_param("coeff_b"), coeff_b);
    if (get_params().Exists("offset"))
      Utils::from_string(get_param("offset"), coeff_b);

    if (!get_params().Exists("visible")) set_param("visible", "true");
    if (get_params().Exists("frequency"))
    {
        /* Frequency is in milliseconds */
        Utils::from_string(get_param("frequency"), readTime);
        readTime /= 1000.0;
    }
    else if (get_params().Exists("interval"))
    {
        /* interval is in seconds for legacy reasons */
        Utils::from_string(get_param("interval"), readTime);
    }
    else
      readTime = 15.0;

    cInfoDom("input") << "frequency : " << readTime << " seconds";

    ListeRule::Instance().Add(this); //add this specific input to the EventLoop

    Calaos::StartReadRules::Instance().addIO();

    cInfoDom("input") << get_param("id") << ": Ok";
}

InputTemp::~InputTemp()
{
    cInfoDom("input");
}

void InputTemp::hasChanged()
{
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

    v = coeff_a * value + coeff_b;

    return v;
}

void InputTemp::emitChange()
{
    cInfoDom("input") << get_param("id") << ": " << get_value_double() << " °C";

    EmitSignalInput();

    string sig = "input ";
    sig += get_param("id") + " ";
    sig += Utils::url_encode(string("state:") + Utils::to_string(get_value_double()));
    IPC::Instance().SendEvent("events", sig);
}

void InputTemp::force_input_double(double v)
{
    value = v;
    emitChange();
}
