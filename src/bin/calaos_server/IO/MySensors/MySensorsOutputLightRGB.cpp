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
#include "MySensorsOutputLightRGB.h"
#include "MySensorsController.h"
#include "IOFactory.h"
#include "MySensors.h"

using namespace Calaos;

REGISTER_IO(MySensorsOutputLightRGB)

MySensorsOutputLightRGB::MySensorsOutputLightRGB(Params &p):
    OutputLightRGB(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("MySensorsOutputLightRGB");
    ioDoc->descriptionSet(_("RGB Light dimmer with MySensors node"));
    MySensors::commonDoc(ioDoc);
    ioDoc->paramAdd("node_id_red", _("Node ID for red channel, as set in your network"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("sensor_id_red", _("Sensor ID for red channel, as set in your node"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("node_id_green", _("Node ID for green channel, as set in your network"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("sensor_id_green", _("Sensor ID green red channel, as set in your node"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("node_id_blue", _("Node ID for blue channel, as set in your network"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("sensor_id_blue", _("Sensor ID blue red channel, as set in your node"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("data_type", _("Data type sent to the node. Default: V_DIMMER, see MySensors.cpp for more values."), IODoc::TYPE_STRING, false);

    string nodeId_r = get_param("node_id_red");
    string sensorId_r = get_param("sensor_id_red");
    string nodeId_g = get_param("node_id_green");
    string sensorId_g = get_param("sensor_id_green");
    string nodeId_b = get_param("node_id_blue");
    string sensorId_b = get_param("sensor_id_blue");

    MySensorsController::Instance(get_params()).registerIO(nodeId_r, sensorId_r, [=]() { /*nothing*/ });
    MySensorsController::Instance(get_params()).registerIO(nodeId_g, sensorId_g, [=]() { /*nothing*/ });
    MySensorsController::Instance(get_params()).registerIO(nodeId_b, sensorId_b, [=]() { /*nothing*/ });
    cInfoDom("output") << "node_id_r: " << nodeId_r << " sensor_id_r: " << sensorId_r;
    cInfoDom("output") << "node_id_g: " << nodeId_g << " sensor_id_g: " << sensorId_g;
    cInfoDom("output") << "node_id_b: " << nodeId_b << " sensor_id_b: " << sensorId_b;
}

MySensorsOutputLightRGB::~MySensorsOutputLightRGB()
{
}

void MySensorsOutputLightRGB::setColorReal(const ColorValue &c, bool s)
{
    string nodeId_r = get_param("node_id_red");
    string sensorId_r = get_param("sensor_id_red");
    string nodeId_g = get_param("node_id_green");
    string sensorId_g = get_param("sensor_id_green");
    string nodeId_b = get_param("node_id_blue");
    string sensorId_b = get_param("sensor_id_blue");

    int dataType = MySensors::V_DIMMER;
    if (MySensors::String2DataType(get_param("data_type")) != MySensors::V_ERROR)
        dataType = MySensors::String2DataType(get_param("data_type"));

    int r = 0, g = 0, b = 0;
    if (s)
    {
        r = c.getRed();
        g = c.getGreen();
        b = c.getBlue();
    }

    MySensorsController::Instance(get_params()).setValue(nodeId_r, sensorId_r, dataType, Utils::to_string(r));
    MySensorsController::Instance(get_params()).setValue(nodeId_g, sensorId_g, dataType, Utils::to_string(g));
    MySensorsController::Instance(get_params()).setValue(nodeId_b, sensorId_b, dataType, Utils::to_string(b));
}

