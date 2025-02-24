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
#include "OutputAnalog.h"
#include "AnalogIO.h"

using namespace Calaos;
using namespace Utils;

OutputAnalog::OutputAnalog(Params &p):
    IOBase(p, IOBase::IO_OUTPUT),
    value(0)
{
    ioDoc->descriptionBaseSet(_("Analog output. Useful to control analog output devices connected to calaos."));
    ioDoc->paramAdd("coeff_a", _("use in conjunction of coeff_b to apply equation of the form `value_sent = coeff_a * raw_value + coeff_b`. Default value is 1.0."),
                 IODoc::TYPE_FLOAT, false, "1");
    ioDoc->paramAdd("coeff_b", _("use in conjunction of coeff_a to apply equation of the form `value_sent = coeff_a * raw_value + coeff_b`. Default value is 0.0"),
                 IODoc::TYPE_FLOAT, false, "0");

    ioDoc->paramAdd("step", _("Set a step for increment/decrement value. Default is 1.0"), IODoc::TYPE_FLOAT, false, "1");
    AnalogIO::commonDoc(ioDoc);
    ioDoc->conditionAdd("0", _("Event on a specific number value"));
    ioDoc->actionAdd("0", _("Set a specific number value"));
    ioDoc->actionAdd("inc", _("Increment value with configured step"));
    ioDoc->actionAdd("dec", _("Decrement value with configured step"));
    ioDoc->actionAdd("inc 1", _("Increment value by value"));
    ioDoc->actionAdd("dec 1", _("Decrement value by value"));

    set_param("gui_type", "analog_out");

    if (!get_params().Exists("log_history")) set_param("log_history", "true");

    readConfig();

    cInfoDom("output") << "OutputAnalog::OutputAnalog(" << get_param("id") << "): Ok";
}

OutputAnalog::~OutputAnalog()
{
}

void OutputAnalog::readConfig()
{
    if (get_params().Exists("coeff_a")) Utils::from_string(get_param("coeff_a"), coeff_a);
    if (get_params().Exists("coeff_b")) Utils::from_string(get_param("coeff_b"), coeff_b);

    if (!get_params().Exists("visible")) set_param("visible", "true");
}

double OutputAnalog::get_value_double()
{
    readConfig();

    return value;
}

void OutputAnalog::emitChange()
{
    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", Utils::to_string(value) } });
}

bool OutputAnalog::set_value(double val)
{
    if (!isEnabled()) return true;

    readConfig();

    cmd_state = val;

    //send computed value to device
    set_value_real(val * coeff_a + coeff_b);

    bool hasChanged = false;
    if (value != val)
        hasChanged = true;

    //save raw value
    value = val;

    EmitSignalIO();

    if (hasChanged)
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

