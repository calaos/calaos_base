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
#include "InputAnalog.h"
#include "ListeRule.h"
#include "Ecore.h"
#include "CalaosConfig.h"

using namespace Calaos;

InputAnalog::InputAnalog(Params &p):
    IOBase(p, IOBase::IO_INPUT),
    real_value_max(0.0),
    wago_value_max(0.0),
    value(0.0),
    precision(2)
{
    ioDoc->descriptionBaseSet(_("An analog input can be used to read analog values to display them and use them in rules."));

    ioDoc->paramAdd("coeff_a", _("use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0."),
                 IODoc::TYPE_FLOAT, false);
    ioDoc->paramAdd("coeff_b", _("use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0"),
                 IODoc::TYPE_FLOAT, false);

    ioDoc->paramAdd("offset", _("same as coeff_b, can be used alone. Default value is 0.0"),
                 IODoc::TYPE_FLOAT, false);
    ioDoc->paramAdd("frequency", _("Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter"),
                 IODoc::TYPE_FLOAT, false);
    ioDoc->paramAdd("interval", _("Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s"),
                 IODoc::TYPE_FLOAT, false);
    ioDoc->paramAdd("precision", _("Precision of the returned calue. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0"),
                 IODoc::TYPE_INT, false);

    ioDoc->conditionAdd("value", _("Event on a specific value"));
    ioDoc->conditionAdd("changed", _("Event on any change of value"));

    set_param("gui_type", "analog_in");

    readConfig();

    string v;
    if (Config::Instance().ReadValueIO(get_param("id"), v) &&
        Utils::is_of_type<double>(v))
        Utils::from_string(v, value);

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

    if (!get_params().Exists("precision"))
        precision = 2;
    else
        Utils::from_string(get_param("precision"), precision);

}

void InputAnalog::emitChange()
{
    EmitSignalIO();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", Utils::to_string(get_value_double()) } });

    Config::Instance().SaveValueIO(get_param("id"), Utils::to_string(value), false);

    cInfoDom("input") << get_param("id") << ": " << get_value_double();
}

void InputAnalog::hasChanged()
{
    if (!isEnabled()) return;

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
        return Utils::roundValue(value * real_value_max / wago_value_max, precision);
    }
    else
    {
        cDebugDom("input") << get_param("id") << ": "
                           << coeff_a << " * " << value << " + " << coeff_b;
        return Utils::roundValue(value * coeff_a + coeff_b, precision);
    }
}

bool InputAnalog::set_value(double v)
{
    if (!isEnabled()) return false;

    if (wago_value_max > 0 && real_value_max > 0)
        value = v * wago_value_max / real_value_max;
    else
        value = v * coeff_a + coeff_b;

    emitChange();

    return true;
}
