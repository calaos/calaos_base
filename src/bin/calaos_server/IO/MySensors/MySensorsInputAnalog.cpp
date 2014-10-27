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
#include "MySensorsInputAnalog.h"
#include "MySensorsController.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_INPUT(MySensorsInputAnalog)

MySensorsInputAnalog::MySensorsInputAnalog(Params &p):
    InputAnalog(p)
{
    MySensorsController::Instance(get_params());
    cInfoDom("input") << "MySensorsInputAnalog::MySensorsInputAnalog()";
}

MySensorsInputAnalog::~MySensorsInputAnalog()
{
    cInfoDom("input") << "MySensorsInputAnalog::~MySensorsInputAnalog()";
}


void MySensorsInputAnalog::readValue()
{
    // Read the value
    int nodeId;
    int sensorId;
    Utils::from_string(get_param("node_id"), nodeId);
    Utils::from_string(get_param("sensor_id"), sensorId);

    string sv = MySensorsController::Instance(get_params()).getValue(nodeId, sensorId);
    double v;
    if (!Utils::is_of_type<double>(sv))
        return;
    Utils::from_string(sv, v);

    if (v != value)
        emitChange();
}

