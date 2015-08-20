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
#include "InputTemp.h"
#include <Ecore.h>
#include "ListeRule.h"
#include "CalaosConfig.h"

using namespace Calaos;

InputTemp::InputTemp(Params &p):
    IOBase(p, IOBase::IO_INPUT),
    value(0.0),
    timer(0.0)
{
    ioDoc->descriptionBaseSet(_("Temperature sensor input. Use for displaying temperature and to control heating devices with rules based on temperature value"));
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

    ioDoc->conditionAdd("value", _("Event on a temperature value in °C"));
    ioDoc->conditionAdd("changed", _("Event on any change of temperature value"));

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

    string v;
    if (Config::Instance().ReadValueIO(get_param("id"), v) &&
        Utils::is_of_type<double>(v))
        Utils::from_string(v, value);

    cInfoDom("input") << "frequency: " << readTime << " seconds, value from cache: " << value;

    ListeRule::Instance().Add(this); //add this specific input to the EventLoop

    cInfoDom("input") << get_param("id") << ": Ok";
}

InputTemp::~InputTemp()
{
    cInfoDom("input");
}

void InputTemp::hasChanged()
{
    if (!isEnabled()) return;

    double sec = ecore_time_get() - timer;
    if (sec >= readTime)
    {
        timer = ecore_time_get();

        readValue();

        Config::Instance().SaveValueIO(get_param("id"), Utils::to_string(value), false);
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

    EmitSignalIO();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", Utils::to_string(get_value_double()) } });
}

void InputTemp::force_input_double(double v)
{
    if (!isEnabled()) return;

    value = v;
    emitChange();
}
