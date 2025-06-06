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
#include "MySensorsInputAnalog.h"
#include "MySensorsControllerList.h"
#include "IOFactory.h"
#include "AnalogIO.h"

using namespace Calaos;

REGISTER_IO(MySensorsInputAnalog)

MySensorsInputAnalog::MySensorsInputAnalog(Params &p):
    InputAnalog(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("MySensorsInputAnalog");
    ioDoc->descriptionSet(_("Analog measurement with MySensors node"));
    MySensors::commonDoc(ioDoc);
    ioDoc->paramAdd("node_id", _("Node ID as set in your network"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("sensor_id", _("Sensor ID, as set in your node"), IODoc::TYPE_STRING, true);

    string nodeId = get_param("node_id");
    string sensorId = get_param("sensor_id");

    ctrl = MySensorsControllerList::Instance().get_controller(get_params());
    ctrl->registerIO(nodeId, sensorId, [=]()
    {
        readValue();
    });
    cInfoDom("input") << "node_id: " << nodeId << " sensor_id: " << sensorId;
}

MySensorsInputAnalog::~MySensorsInputAnalog()
{
}

void MySensorsInputAnalog::readValue()
{
    // Read the value
    string nodeId = get_param("node_id");
    string sensorId = get_param("sensor_id");

    string sv = ctrl->getValue(nodeId, sensorId);
    double v;
    if (sv.empty() || !Utils::is_of_type<double>(sv))
        return;
    Utils::from_string(sv, v);

    if (v != value)
    {
        value = AnalogIO::convertValue(get_params(), v);
        emitChange();
    }
}

