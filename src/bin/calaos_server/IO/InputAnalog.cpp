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
#include <InputAnalog.h>
#include <ListeRule.h>
#include <IPC.h>
#include <Ecore.h>

using namespace Calaos;

InputAnalog::InputAnalog(Params &p):
    Input(p),
    real_value_max(0.0),
    wago_value_max(0.0),
    value(0.0)
{
    set_param("gui_type", "analog_in");

    readConfig();
    timer = ecore_time_get();
    ListeRule::Instance().Add(this); //add this specific input to the EventLoop

    cInfoDom("input") << get_param("id") << ": Ok";
}

InputAnalog::~InputAnalog()
{
    cInfoDom("input");
}

void InputAnalog::readConfig()
{
    if (!get_params().Exists("visible"))
      set_param("visible", "true");

    if (get_params().Exists("real_max")) 
      Utils::from_string(get_param("real_max"), real_value_max);
    else 
      real_value_max = 0.0;

    if (get_params().Exists("wago_max"))
      Utils::from_string(get_param("wago_max"), wago_value_max);
    else 
      wago_value_max = 0.0;

    if (get_params().Exists("coeff_a"))
      Utils::from_string(get_param("coeff_a"), coeff_a);
    else 
      coeff_a = 1.0;

    if (get_params().Exists("coeff_b"))
      Utils::from_string(get_param("coeff_b"), coeff_b);
    else
      coeff_b = 0.0;

    if (get_params().Exists("interval"))
    {
        /* Interval for legacy reasons is in seconds */
        Utils::from_string(get_param("interval"), frequency);
    }
    else if (get_params().Exists("frequency"))
    {
        Utils::from_string(get_param("frequency"), frequency);
        /* frequency parameter is in millisecond */
        frequency /= 1000.0;
    }
    else
      frequency = 15.0;
}

void InputAnalog::emitChange()
{
    EmitSignalInput();

    string sig = "input ";
    sig += get_param("id") + " ";
    sig += Utils::url_encode(string("state:") + Utils::to_string(get_value_double()));
    IPC::Instance().SendEvent("events", sig);

    cInfoDom("input") << get_param("id") << ": " << get_value_double();
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

    if (wago_value_max > 0 && real_value_max > 0)
    {
        cDebugDom("input") << get_param("id") << ": "
                           << value << " * " << real_value_max << " / " << wago_value_max;
        return Utils::roundValue(value * real_value_max / wago_value_max);
    }
    else
    {
        cDebugDom("input") << get_param("id") << ": "
                           << coeff_a << " * " << value << " + " << coeff_b;
        return Utils::roundValue(value * coeff_a + coeff_b);
    }
}

void InputAnalog::force_input_double(double v)
{
    if (wago_value_max > 0 && real_value_max > 0)
        value = v * wago_value_max / real_value_max;
    else
        value = v * coeff_a + coeff_b;

    emitChange();
}
