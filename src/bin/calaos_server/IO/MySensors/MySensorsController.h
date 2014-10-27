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
#include <termios.h>

namespace Calaos
{

class MySensorsController
{
private:

    MySensorsController(const Params &p);

    //gateway configuration
    //Can be serial with gateway="serial" or TCP with gateway="tcp"
    //With serial: port="/dev/ttyUSB0"
    //With TCP: host="x.x.x.x" port="xxx"
    Params param;

    //Keep values of sensors in cache
    //Key in hash is <node_id>;<sensor_id>
    std::unordered_map<string, Params> hashSensors;

    std::unordered_map<string, sigc::signal<void>> sensorsCb;

    //serial
    int serialfd = 0;
    struct termios currentTermios;
    struct termios oldTermios;

    EcoreTimer *timer = nullptr;

    Ecore_Fd_Handler *serial_handler = nullptr;

    void openSerial();
    void closeSerial();
    void openTCP();
    void serialError();
    void openSerialLater(double time = 2.0); //default 2s

    void writeData(const string &data);
    void readData(string &data);

public:
    static MySensorsController &Instance(const Params &p)
    {
        static MySensorsController ctrl(p);
        return ctrl;
    }
    ~MySensorsController();

    string getValue(string nodeid, string sensorid, string key = "payload");
    void setValue(string nodeid, string sensorid, string payload);

    void registerIO(string nodeid, string sensorid, sigc::slot<void> callback);

    Eina_Bool _serialHandler(Ecore_Fd_Handler *handler);
};

}
#endif
