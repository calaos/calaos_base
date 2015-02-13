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

REGISTER_INPUT(MySensorsInputString)

MySensorsInputString::MySensorsInputString(Params &p):
    InputString(p)
{
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
    cInfoDom("input");
}

void MySensorsInputString::readValue()
{
    value = current;
}
