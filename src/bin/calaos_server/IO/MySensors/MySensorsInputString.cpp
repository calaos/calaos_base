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
#include "MySensorsInputString.h"
#include "MySensorsController.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(MySensorsInputString)

MySensorsInputString::MySensorsInputString(Params &p):
    InputString(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("MySensorsInputString");
    ioDoc->descriptionSet(_("Display string from MySensors node"));
    ioDoc->linkAdd("MySensors", _("http://mysensors.org"));
    ioDoc->paramAdd("node_id", _("Node ID as set in your network"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("sensor_id", _("Sensor ID, as set in your node"), IODoc::TYPE_STRING, true);

    Params gwlist = {{ "serial", _("Serial") },
                     { "tcp", _("Tcp") }};
    ioDoc->paramAddList("gateway", _("Gateway type used, tcp or serial are supported"), true, gwlist, "serial");
    ioDoc->paramAdd("port",
                    _("If using serial gateway, port is the serial port (/dev/ttyUSB0 for ex.). If using tcp gateway port is TCP port of the gateway."),
                    IODoc::TYPE_STRING, true, "/dev/ttyUSB0");
    ioDoc->paramAdd("host", _("IP address of the tcp gateway if relevant"), IODoc::TYPE_STRING, true);

    string nodeId = get_param("node_id");
    string sensorId = get_param("sensor_id");

    MySensorsController::Instance(get_params()).registerIO(nodeId, sensorId, [=]()
    {
        // Read the value
        string nId = get_param("node_id");
        string sId = get_param("sensor_id");

        string v = MySensorsController::Instance(get_params()).getValue(nId, sId);

        if (v != current)
        {
            current = v;
            cInfoDom("input") << "node_id: " << nodeId << " sensor_id: " << sensorId << " new value: " << value;
            emitChange();
        }
    });
    cInfoDom("input") << "node_id: " << nodeId << " sensor_id: " << sensorId;
}

MySensorsInputString::~MySensorsInputString()
{
}

void MySensorsInputString::readValue()
{
    value = current;
}
