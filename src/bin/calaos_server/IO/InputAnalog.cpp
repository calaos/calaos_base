/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#include "CalaosConfig.h"
#include "AnalogIO.h"

using namespace Calaos;

InputAnalog::InputAnalog(Params &p):
    IOBase(p, IOBase::IO_INPUT),
    value(0.0),
    precision(2)
{
    ioDoc->descriptionBaseSet(_("An analog input can be used to read analog values to display them and use them in rules."));

    ioDoc->paramAdd("coeff_a", _("use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0."),
                 IODoc::TYPE_FLOAT, false, "1");
    ioDoc->paramAdd("coeff_b", _("use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0"),
                 IODoc::TYPE_FLOAT, false, "0");
    ioDoc->paramAdd("calc_expr", _("Use a mathematical expression to calculate the value from the raw value. The variable `x` is replaced with the raw value. For example, if you want to convert a raw value of 0-1000 to a temperature in Celsius, you can use `x / 10.0 - 50.0`. If this expression is set, coeff_a, coeff_b and offset parameters are ignored."),
                 IODoc::TYPE_STRING, false);

    ioDoc->paramAdd("offset", _("same as coeff_b, can be used alone. Default value is 0.0"),
                 IODoc::TYPE_FLOAT, false);
    ioDoc->paramAdd("period", _("Sampling time in millisecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter"),
                 IODoc::TYPE_FLOAT, false);
    ioDoc->paramAdd("interval", _("Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s"),
                 IODoc::TYPE_FLOAT, false, "15");
    ioDoc->paramAddInt("precision", _("Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0"), 0, 9999, false, 2);

    ioDoc->paramAdd("display_warning", _("Display a warning if value has not been updated for a long time. Default to true"), IODoc::TYPE_BOOL, false, "true");

    AnalogIO::commonDoc(ioDoc);

    set_param("gui_type", "analog_in");

    readConfig();

    string v;
    if (Config::Instance().ReadValueIO(get_param("id"), v) &&
        Utils::is_of_type<double>(v))
        Utils::from_string(v, value);

    timer = Utils::getMainLoopTime();
    ListeRule::Instance().Add(this); //add this specific input to the EventLoop

    set_param("value_warning", "false");

    if (!param.Exists("display_warning"))
        param.Add("display_warning", "true");

    timerChanged = new Timer(IOBase::TimerChangedWarning, [=]()
    {
        if (get_param("display_warning") != "true") return;

        set_param("value_warning", "true");
        EventManager::create(CalaosEvent::EventIOChanged,
                             { { "id", get_param("id") },
                               { "value_warning", "true" } });
    });

    cInfoDom("input") << get_param("id") << ": Ok";
}

InputAnalog::~InputAnalog()
{
}

void InputAnalog::readConfig()
{
    if (!get_params().Exists("visible"))
        set_param("visible", "true");

    /* rename frequency to period */
    if (get_params().Exists("frequency"))
    {
	    Utils::from_string(get_param("frequency"), frequency);
        set_param("period", Utils::to_string(frequency));
        del_param("frequency");
    }

    if (get_params().Exists("interval"))
    {
        /* Interval for legacy reasons is in seconds */
        Utils::from_string(get_param("interval"), frequency);
    }
    else if (get_params().Exists("period"))
    {
        Utils::from_string(get_param("period"), frequency);
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

    timerChanged->Reset();

    if (get_param("value_warning") != "false" &&
        get_param("display_warning") == "true")
    {
        set_param("value_warning", "false");
        EventManager::create(CalaosEvent::EventIOChanged,
                             { { "id", get_param("id") },
                               { "value_warning", "false" } });
    }
}

void InputAnalog::hasChanged()
{
    if (!isEnabled()) return;

    readConfig();

    double sec = Utils::getMainLoopTime() - timer;
    if (sec >= frequency)
    {
        timer = Utils::getMainLoopTime();

        readValue();
    }
}

double InputAnalog::get_value_double()
{
    return value;
}

bool InputAnalog::set_value(double v)
{
    if (!isEnabled()) return false;

    value = v;

    emitChange();

    return true;
}

bool InputAnalog::set_value(string v)
{
    double dval;
    Utils::from_string(v, dval);
    return set_value(dval);
}
