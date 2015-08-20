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
#include "MySensorsOutputAnalog.h"
#include "MySensorsController.h"
#include "IOFactory.h"
#include "MySensors.h"

using namespace Calaos;

REGISTER_IO(MySensorsOutputAnalog)

MySensorsOutputAnalog::MySensorsOutputAnalog(Params &p):
    OutputAnalog(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("MySensorsOutputAnalog");
    ioDoc->descriptionSet(_("Analog output with MySensors node"));
    ioDoc->linkAdd("MySensors", _("http://mysensors.org"));
    ioDoc->paramAdd("node_id", _("Node ID as set in your network"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("sensor_id", _("Sensor ID, as set in your node"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("data_type", _("Data type sent to the node. Default: V_DIMMER, see MySensors.cpp for more values."), IODoc::TYPE_STRING, false);

    string nodeId = get_param("node_id");
    string sensorId = get_param("sensor_id");

    MySensorsController::Instance(get_params()).registerIO(nodeId, sensorId, [=]() { /*nothing*/ });
    cInfoDom("output") << "node_id: " << nodeId << " sensor_id: " << sensorId;
}

MySensorsOutputAnalog::~MySensorsOutputAnalog()
{
    cInfoDom("output");
}

void MySensorsOutputAnalog::set_value_real(double val)
{
    string nodeId = get_param("node_id_down");
    string sensorId = get_param("sensor_id_down");

    int dataType = MySensors::V_DIMMER;
    if (MySensors::String2DataType(get_param("data_type")) != MySensors::V_ERROR)
        dataType = MySensors::String2DataType(get_param("data_type"));

    MySensorsController::Instance(get_params()).setValue(nodeId, sensorId, dataType, Utils::to_string(val));
}

