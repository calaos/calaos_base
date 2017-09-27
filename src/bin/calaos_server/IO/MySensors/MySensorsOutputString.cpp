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
#include "MySensorsOutputString.h"
#include "MySensorsControllerList.h"
#include "IOFactory.h"
#include "MySensors.h"

using namespace Calaos;

REGISTER_IO(MySensorsOutputString)

MySensorsOutputString::MySensorsOutputString(Params &p):
    OutputString(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("MySensorsOutputString");
    ioDoc->descriptionSet(_("String output with MySensors node"));
    MySensors::commonDoc(ioDoc);
    ioDoc->paramAdd("node_id", _("Node ID as set in your network"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("sensor_id", _("Sensor ID, as set in your node"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("data_type", _("Data type sent to the node. Default: V_VAR1, see MySensors.cpp for more values."), IODoc::TYPE_STRING, false);

    string nodeId = get_param("node_id");
    string sensorId = get_param("sensor_id");

    ctrl = MySensorsControllerList::Instance().get_controller(get_params());
    ctrl->registerIO(nodeId, sensorId, [=]() { /*nothing*/ });
    cInfoDom("output") << "node_id: " << nodeId << " sensor_id: " << sensorId;
}

MySensorsOutputString::~MySensorsOutputString()
{
}

void MySensorsOutputString::set_value_real(string val)
{
    string nodeId = get_param("node_id");
    string sensorId = get_param("sensor_id");

    int dataType = MySensors::V_VAR1;
    if (MySensors::String2DataType(get_param("data_type")) != MySensors::V_ERROR)
        dataType = MySensors::String2DataType(get_param("data_type"));

    ctrl->setValue(nodeId, sensorId, dataType, val);
}
