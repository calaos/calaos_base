/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef MySensorsController_H
#define MySensorsController_H

#include <Utils.h>
#include <EcoreTimer.h>
#include <Params.h>
#include <unordered_map>

namespace Calaos
{

class MySensorsController
{
private:

    MySensorsController(const Params &p);

    //gateway configuration
    //Can be serial with gateway="serial" or TCP with gateway="tcp"
    //With serial: port="/dev/ttyUSB0" speed="115200"
    //With TCP: host="x.x.x.x" port="xxx"
    Params param;

    //Keep values of sensors in cache
    //Key in hash is <node_id>;<sensor_id>
    std::unordered_map<string, Params> hashSensors;

    void openSerial();
    void openTCP();

public:
    static MySensorsController &Instance(const Params &p)
    {
        static MySensorsController ctrl(p);
        return ctrl;
    }
    ~MySensorsController();

    string getValue(int nodeid, int sensorid, string key = "payload");
    void setValue(int nodeid, int sensorid, string payload);

};

}
#endif
