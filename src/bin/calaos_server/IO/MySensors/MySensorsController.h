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
#include <Ecore.h>
#include <Ecore_Con.h>

namespace Calaos
{

//Represent a Node
class MySensorsNode
{
public:
    //This contains the node parameters like:
    // "name" -> node sketch name
    // "version" -> node sketch version
    // "battery_level" -> battery level if relevent
    Params param;

    //This contains all cached sensor data connected on this node
    // "0" -> data for sensor 0
    // "2" -> data for sensor 2
    // ...
    Params sensor_data;
};

class MySensorsController
{
private:

    MySensorsController(const Params &p);

    string gatewayVersion;

    //gateway configuration
    //Can be serial with gateway="serial" or TCP with gateway="tcp"
    //With serial: port="/dev/ttyUSB0"
    //With TCP: host="x.x.x.x" port="xxx"
    Params param;

    //Keep values of sensors in cache
    //Key in hash is <node_id>
    std::unordered_map<string, MySensorsNode> hashSensors;

    std::unordered_map<string, sigc::signal<void>> sensorsCb;

    //serial
    int serialfd = 0;
    struct termios currentTermios;
    struct termios oldTermios;

    EcoreTimer *timer = nullptr;

    Ecore_Fd_Handler *serial_handler = nullptr;

    string dataBuffer;

    //tcp connection
    Ecore_Event_Handler *ehandler_add;
    Ecore_Event_Handler *ehandler_del;
    Ecore_Event_Handler *ehandler_data;
    Ecore_Con_Server *econ = nullptr;
    EcoreTimer *timer_con = nullptr;

    void timerConnReconnect();

    void openSerial();
    void closeSerial();
    void openTCP();
    void serialError();
    void openSerialLater(double time = 2.0); //default 2s

    void writeData(const string &data);
    void readNewData(const string &data);
    void processMessage(string msg);
    void sendMessage(string node_id, string sensor_id, int msgType, int ack, int subType, string payload);

    int getNextFreeId();

    void processRequestId(string node_id, string sensor_id);
    void processTime(string node_id, string sensor_id);
    void processNodeInfos(string node_id, string sensor_id, string key, string payload);
    void processSensorUpdate(string node_id, string sensor_id, int subtype, string payload);
    void processSensorRequest(string node_id, string sensor_id, int subtype, string payload);

public:
    static MySensorsController &Instance(const Params &p)
    {
        static MySensorsController ctrl(p);
        return ctrl;
    }
    ~MySensorsController();

    string getValue(string nodeid, string sensorid, string key = "payload");
    void setValue(string nodeid, string sensorid, int dataType, string payload);

    void registerIO(string nodeid, string sensorid, sigc::slot<void> callback);

    Eina_Bool _serialHandler(Ecore_Fd_Handler *handler);


    /* This is private for C callbacks */
    void addConnection(Ecore_Con_Server *srv);
    void delConnection(Ecore_Con_Server *srv);
    void dataGet(Ecore_Con_Server *srv, void *data, int size);
};

}
#endif
