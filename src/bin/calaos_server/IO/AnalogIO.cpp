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
#include "AnalogIO.h"
#include "ExpressionEvaluator.h"

void AnalogIO::commonDoc(IODoc *ioDoc)
{
    ioDoc->paramAdd("unit", _("Unit which will be displayed on the UI as a suffix."), IODoc::TYPE_STRING, false);

    Params io_style = {{ "default", _("Default") },
                        { "temperature", _("Temperature") },
                        { "humidity", _("Humidity") },
                        { "pressure", _("Pressure") },
                        { "luminosity", _("Luminosity") },
                        { "speed", _("Speed") },
                        { "current", _("Current") },
                        { "voltage", _("Voltage") },
                        { "watt", _("Watt") },
                       };
    ioDoc->paramAddList("io_style", _("GUI style display. This will control the icon displayed on the UI"), true, io_style, "default");

    ioDoc->conditionAdd("value", _("Event on a specific value"));
    ioDoc->conditionAdd("changed", _("Event on any change of value"));
}

double AnalogIO::convertValue(const Params &params, double dvalue)
{
    if (params.Exists("calc_expr") &&
        ExpressionEvaluator::isExpressionValid(params["calc_expr"]))
    {
        bool failed = false;
        double v = ExpressionEvaluator::calculateExpression(params["calc_expr"], dvalue, failed);
        if (failed)
        {
            cWarningDom("analog") << "Failed to calculate expression: " << params["calc_expr"];
            return dvalue; // Return original value on failure
        }

        cDebugDom("input") << "Calculated value from expression (" << dvalue << "): " << v;
        return v;
    }

    double coeff_a, coeff_b;
    if (params.Exists("coeff_a"))
        Utils::from_string(params["coeff_a"], coeff_a);
    else
        coeff_a = 1.0;

    if (params.Exists("coeff_b"))
        Utils::from_string(params["coeff_b"], coeff_b);
    else if (params.Exists("offset"))
        Utils::from_string(params["offset"], coeff_b);
    else
        coeff_b = 0.0;

    // Apply coefficients to the value
    double v = (dvalue * coeff_a) + coeff_b;
    cDebugDom("analog") << "Converted value: " << v << " from raw value: " << dvalue
                        << " using coeff_a: " << coeff_a << " and coeff_b: " << coeff_b;
    return v;
}
