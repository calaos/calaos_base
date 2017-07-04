/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
#include "xPLOutputSwitch.h"
#include "xPLController.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(xPLOutputSwitch)

xPLOutputSwitch::xPLOutputSwitch(Params &p):
    OutputLight(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("xPLOutputSwitch");
    ioDoc->descriptionSet(_("Light/relay controlled by xPL Protocol"));
    ioDoc->paramAdd("source", _("Source name, as set in your xPL network (Format VendorId-DeviceId.Instance)"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("sensor", _("Sensor ID, as set in your xPL network"), IODoc::TYPE_STRING, true);

    if(get_param("id")=="doc") return; //Json documentation detector

    string source = get_param("source");
    string sensor = get_param("sensor");
    
    xPLController::Instance().RegisterSensor(source, sensor, sigc::mem_fun(*this, &xPLOutputSwitch::valueUpdated));

    cDebugDom("output") << source << ":" << sensor;
}

xPLOutputSwitch::~xPLOutputSwitch()
{
}

void xPLOutputSwitch::valueUpdated(xPLInfoSensor *sensor)
{
    if (sensor->DigitalVal != value)
    {
        value = sensor->DigitalVal;
        emitChange();
    }
}

void xPLOutputSwitch::readValue()
{
}

bool xPLOutputSwitch::set_value_real(bool val)
{
    string source = get_param("source");
    string sensor = get_param("sensor");

    xPLController::Instance().SetValue(source, sensor, Utils::to_string(val));

    return true;
}

