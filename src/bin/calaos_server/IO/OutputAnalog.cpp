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
#include "OutputAnalog.h"

using namespace Calaos;
using namespace Utils;

OutputAnalog::OutputAnalog(Params &p):
    Output(p),
    value(-1),
    real_value_max(0.0),
    wago_value_max(0.0)
{
    ioDoc->descriptionBaseSet(_("Analog output. Useful to control analog output devices connected to calaos."));
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

    ioDoc->conditionAdd("value", _("Event on a specific value"));
    ioDoc->conditionAdd("changed", _("Event on any change of value"));

    ioDoc->paramAdd("step", _("Set a step for increment/decrement value. Default is 1.0"), IODoc::TYPE_FLOAT, false);
    ioDoc->conditionAdd("0", _("Event on a specific number value"));
    ioDoc->actionAdd("0", _("Set a specific number value"));
    ioDoc->actionAdd("inc", _("Increment value with configured step"));
    ioDoc->actionAdd("dec", _("Decrement value with configured step"));
    ioDoc->actionAdd("inc 1", _("Increment value by value"));
    ioDoc->actionAdd("dec 1", _("Decrement value by value"));


    set_param("gui_type", "analog_out");

    readConfig();

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

    if (get_params().Exists("coeff_a")) Utils::from_string(get_param("coeff_a"), coeff_a);
    if (get_params().Exists("coeff_b")) Utils::from_string(get_param("coeff_b"), coeff_b);


    if (!get_params().Exists("visible")) set_param("visible", "true");
}

double OutputAnalog::get_value_double()
{
    readConfig();
 
    if (wago_value_max > 0 && real_value_max > 0)
        return Utils::roundValue(value * real_value_max / wago_value_max);
    else
        return Utils::roundValue(value * coeff_a + coeff_b);
}

void OutputAnalog::emitChange()
{   
    EventManager::create(CalaosEvent::EventOutputChanged,
                         { { "id", get_param("id") },
                           { "state", Utils::to_string(value) } });
}

bool OutputAnalog::set_value(double val)
{
    if (!isEnabled()) return true;

    UWord v;

    readConfig();

    if (wago_value_max > 0 && real_value_max > 0)
        v = (UWord)(val * wago_value_max / real_value_max);
    else
        v = (UWord)(val * coeff_a + coeff_b);

    set_value_real(v);

    value = val;
    EmitSignalOutput();
    emitChange();

    return true;
}

bool OutputAnalog::set_value(string val)
{
    if (!isEnabled()) return true;

    if (val == "inc")
    {
        double step = 1.0;
        if (Utils::is_of_type<double>(get_param("step")))
            Utils::from_string(get_param("step"), step);

        set_value(value + step);
    }
    else if (val == "dec")
    {
        double step = 1.0;
        if (Utils::is_of_type<double>(get_param("step")))
            Utils::from_string(get_param("step"), step);

        set_value(value - step);
    }
    else if (val.compare(0, 4, "inc ") == 0)
    {
        string t = val;
        t.erase(0, 4);

        double step = 1.0;
        if (Utils::is_of_type<double>(t))
            Utils::from_string(t, step);

        set_value(value + step);
    }
    else if (val.compare(0, 4, "dec ") == 0)
    {
        string t = val;
        t.erase(0, 4);

        double step = 1.0;
        if (Utils::is_of_type<double>(t))
            Utils::from_string(t, step);

        set_value(value - step);
    }
    else if (Utils::is_of_type<double>(val))
    {
        double dval;
        Utils::from_string(val, dval);

        set_value(dval);
    }
    else
    {
        return false;
    }

    return true;
}

