/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
#ifndef ANALOGBOUNDS_H
#define ANALOGBOUNDS_H

#include "Calaos.h"
#include "IODoc.h"
#include "Params.h"
#include "Utils.h"

//Optional `value_min` / `value_max` params of the analog outputs and InternalInt.
//Header only: IntValue.o is linked on its own by many test binaries.
//An absent or non numeric bound is unbounded on that side; min > max is a
//config error and both bounds are ignored rather than guessing which one
//was meant.
namespace AnalogBounds
{

inline void boundsDoc(IODoc *ioDoc)
{
    ioDoc->paramAdd("value_min", _("Lowest value accepted when writing. A lower value is raised to `value_min`. "
                                   "The bound applies to the written value, before coeff_a/coeff_b/calc_expr, "
                                   "and also to the result of inc/dec. No lower bound if not set. "
                                   "Ignored, with an error in the log, if greater than `value_max`."),
                    IODoc::TYPE_FLOAT, false);
    ioDoc->paramAdd("value_max", _("Highest value accepted when writing. A higher value is lowered to `value_max`. "
                                   "The bound applies to the written value, before coeff_a/coeff_b/calc_expr, "
                                   "and also to the result of inc/dec. No upper bound if not set. "
                                   "Ignored, with an error in the log, if lower than `value_min`."),
                    IODoc::TYPE_FLOAT, false);
}

//calaos_installer wrote `min` / `max` on every analog IO for a UI widget
//and nothing in the server ever read them. They are dropped on load so they
//leave io.xml at the next save, and cannot be mistaken for real bounds.
//Only called from analog IO constructors: InputTime/InputTimer use a `min`
//param (minutes) that must stay.
template <class IO>
void dropLegacyBounds(IO *io)
{
    std::string dropped;
    for (const char *name: { "min", "max" })
    {
        if (!io->get_params().Exists(name))
            continue;
        io->del_param(name);
        dropped += (dropped.empty() ? "" : ", ");
        dropped += name;
    }

    if (!dropped.empty())
        cInfoDom("analog") << io->get_param("id") << ": dropped unused legacy param(s) " << dropped;
}

inline bool readBound(const Params &params, const char *name, double &out)
{
    if (!params.Exists(name) || params[name].empty())
        return false;

    if (!Utils::is_of_type<double>(params[name]))
    {
        cWarningDom("analog") << "Ignoring non numeric " << name << " bound: " << params[name];
        return false;
    }

    Utils::from_string(params[name], out);
    return true;
}

inline double clampToBounds(const Params &params, double dvalue)
{
    double lo = 0.0, hi = 0.0;
    const bool hasMin = readBound(params, "value_min", lo);
    const bool hasMax = readBound(params, "value_max", hi);

    if (hasMin && hasMax && lo > hi)
    {
        cErrorDom("analog") << params["id"] << ": value_min (" << lo << ") is greater than value_max ("
                            << hi << "), bounds ignored";
        return dvalue;
    }

    if (hasMin && dvalue < lo) return lo;
    if (hasMax && dvalue > hi) return hi;

    return dvalue;
}

} //namespace AnalogBounds

#endif // ANALOGBOUNDS_H
