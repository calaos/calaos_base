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
#include "MySensorsOutputShutter.h"
#include "MySensorsController.h"
#include "IOFactory.h"
#include "MySensors.h"

namespace Calaos {

REGISTER_IO(MySensorsOutputShutter)

MySensorsOutputShutter::MySensorsOutputShutter(Params &p):
    OutputShutter(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("MySensorsOutputShutter");
    ioDoc->descriptionSet(_("Shutter with MySensors node"));
    ioDoc->linkAdd("MySensors", _("http://mysensors.org"));
    ioDoc->paramAdd("node_id_up", _("Node ID for opening shutter, as set in your network"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("sensor_id_up", _("Sensor ID for opening shutter, as set in your node"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("node_id_down", _("Node ID for closing shutter, as set in your network"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("sensor_id_down", _("Sensor ID for closing shutter, as set in your node"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("data_type", _("Data type sent to the node. Default: V_LIGHT, see MySensors.cpp for more values."), IODoc::TYPE_STRING, false);

    Params gwlist = {{ "serial", _("Serial") },
                     { "tcp", _("Tcp") }};
    ioDoc->paramAddList("gateway", _("Gateway type used, tcp or serial are supported"), true, gwlist, "serial");
    ioDoc->paramAdd("port",
                    _("If using serial gateway, port is the serial port (/dev/ttyUSB0 for ex.). If using tcp gateway port is TCP port of the gateway."),
                    IODoc::TYPE_STRING, true, "/dev/ttyUSB0");
    ioDoc->paramAdd("host", _("IP address of the tcp gateway if relevant"), IODoc::TYPE_STRING, true);

    std::string nodeIdUp = get_param("node_id_up");
    std::string sensorIdUp = get_param("sensor_id_up");
    std::string nodeIdDown = get_param("node_id_up");
    std::string sensorIdDown = get_param("sensor_id_up");

    MySensorsController::Instance(get_params()).registerIO(nodeIdUp, sensorIdUp, [=]() { /*nothing*/ });
    MySensorsController::Instance(get_params()).registerIO(nodeIdDown, sensorIdDown, [=]() { /*nothing*/ });
    cInfoDom("output") << "node_id_up: " << nodeIdUp << " sensor_id_up: " << sensorIdUp;
    cInfoDom("output") << "node_id_down: " << nodeIdDown << " sensor_id_down: " << sensorIdDown;
}

MySensorsOutputShutter::~MySensorsOutputShutter()
{
    cDebugDom("output");
}

void MySensorsOutputShutter::setOutputUp(bool enable)
{
    std::string nodeId = get_param("node_id_up");
    std::string sensorId = get_param("sensor_id_up");

    int dataType = MySensors::V_LIGHT;
    if (MySensors::String2DataType(get_param("data_type")) != MySensors::V_ERROR)
        dataType = MySensors::String2DataType(get_param("data_type"));

    MySensorsController::Instance(get_params()).setValue(nodeId, sensorId, dataType, Utils::to_string(enable));
}

void MySensorsOutputShutter::setOutputDown(bool enable)
{
    std::string nodeId = get_param("node_id_down");
    std::string sensorId = get_param("sensor_id_down");

    int dataType = MySensors::V_LIGHT;
    if (MySensors::String2DataType(get_param("data_type")) != MySensors::V_ERROR)
        dataType = MySensors::String2DataType(get_param("data_type"));

    MySensorsController::Instance(get_params()).setValue(nodeId, sensorId, dataType, Utils::to_string(enable));
}


}
