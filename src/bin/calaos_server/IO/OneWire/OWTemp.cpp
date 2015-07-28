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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <OWCtrl.h>
#include <OWTemp.h>
#include <IOFactory.h>
#include <thread>

using namespace Calaos;

REGISTER_INPUT(OWTemp)

OWTemp::OWTemp(Params &p):
    InputTemp(p)
{
    // Define IO documentation for OWTemp

    ioDoc->descriptionSet("Temperature measurement with DS18B20 Onewire Sensor");
    ioDoc->linkAdd("Calaos Wiki", "http://calaos.fr/wiki/OneWire");
    ioDoc->paramAdd("ow_id", "Unique ID of sensor on OneWire bus.", "string", true);
    ioDoc->paramAdd("ow_args", "Additional paramter use during owfs init.\n"
                               "For example you can use -u to use the USB owfs drivers",
                    "string", true);

    ioDoc->paramAdd("use_w1", "Force the use of w1 kernel driver instead of OneWire drivers", "bool", false);
    ioDoc->actionAdd("value", "Event on a temperature value in °C");
    ioDoc->actionAdd("changed", "Event on any changes of temperature value");

    ow_id = get_param("ow_id");
    ow_args = get_param("ow_args");

    OwCtrl::Instance(ow_args);

    cDebugDom("input") << get_param("id") << ": OW_ID : " << ow_id;
}

void OWTemp::readValue()
{
    string v = OwCtrl::Instance(ow_args)->getValue(ow_id);
    if (v.empty() || !Utils::is_of_type<double>(v))
        return;

    double val;
    Utils::from_string(v, val);
    val = Utils::roundValue(val);
    if (val != value)
    {
        value = val;
        emitChange();
        cDebugDom("input") << ow_id << ": value: " << val;
    }
}
