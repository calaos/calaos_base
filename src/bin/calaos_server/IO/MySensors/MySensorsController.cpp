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
#include "MySensorsController.h"
#include "MySensors.h"
#include <termios.h>
#include <sys/ioctl.h>
#if defined(__linux__) || defined(__linux) || defined(linux)
  #include <linux/serial.h>
#endif
#include "ListeRoom.h"

using namespace Calaos;

MySensorsController::MySensorsController(const Params &p):
    param(p)
{
    hashSensors["0"] = MySensorsNode(); //Gateway

    if (!param.Exists("gateway")) param.Add("gateway", "serial");
    if (!param.Exists("port")) param.Add("port", "/dev/ttyUSB0");

    if (param["gateway"] == "serial")
        openSerial();
    else if (param["gateway"] == "tcp")
        openTCP();
    else
        cErrorDom("mysensors") << "Error, gateway '" << param["gateway"] << "' unknown! not doing anything...";
}

MySensorsController::~MySensorsController()
{
}

static Eina_Bool _serial_handler_cb(void *data, Ecore_Fd_Handler *handler)
{
    MySensorsController *o = reinterpret_cast<MySensorsController *>(data);
    if (o) return o->_serialHandler(handler);
    return ECORE_CALLBACK_RENEW;
}

void MySensorsController::serialError()
{
    string errstr;
    switch (errno)
    {
    case ENOENT:
    case ENODEV: errstr = "Device not found"; break;
    case EACCES: errstr = "Access denied"; break;
    case EBUSY: errstr = "Device is busy"; break;
    case EIO:
    case EBADF:
    case EAGAIN: errstr = "Ressource error"; break;
    default: errstr = "Unknown error"; break;
    }

    cErrorDom("mysensors") << "Error on serial port: '" << errstr << "'";
    cErrorDom("mysensors") << "Closing port and try again later...";

    closeSerial();
    openSerialLater();
}

void MySensorsController::openSerial()
{
    cDebugDom("mysensors") << "Trying to open serial port " << param["port"];

    if ((serialfd = ::open(param["port"].c_str(), O_RDWR | O_NOCTTY | O_NDELAY)) != -1)
    {
        ::tcgetattr(serialfd, &oldTermios); // Save the old termios
        currentTermios = oldTermios;
        ::cfmakeraw(&currentTermios); // Enable raw access

        //setup
        currentTermios.c_cflag |= CREAD | CLOCAL;
        currentTermios.c_lflag &= (~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ISIG));
        currentTermios.c_iflag &= (~(INPCK | IGNPAR | PARMRK | ISTRIP | ICRNL | IXANY));
        currentTermios.c_oflag &= (~OPOST);
        currentTermios.c_cc[VMIN] = 0;
        currentTermios.c_cc[VTIME] = 0;

        //8N1 115200
        currentTermios.c_cflag |= CS8;
        currentTermios.c_iflag &= ~(PARMRK | INPCK);
        currentTermios.c_iflag |= IGNPAR;
        currentTermios.c_cflag &= ~PARENB;
        currentTermios.c_cflag &= ~CSTOPB;
        currentTermios.c_cflag &= ~CRTSCTS;
        currentTermios.c_iflag &= ~(IXON | IXOFF | IXANY);

#if defined(__linux__) || defined(__linux) || defined(linux)
        struct serial_struct currentSerialInfo;
        if ((::ioctl(serialfd, TIOCGSERIAL, &currentSerialInfo) != -1) &&
            (currentSerialInfo.flags & ASYNC_SPD_CUST))
        {
            currentSerialInfo.flags &= ~ASYNC_SPD_CUST;
            currentSerialInfo.custom_divisor = 0;
            if (::ioctl(serialfd, TIOCSSERIAL, &currentSerialInfo) == -1)
            {
                serialError();
                return;
            }
        }
#endif
        if (::cfsetispeed(&currentTermios, B115200) < 0)
        {
            serialError();
            return;
        }
        if (::cfsetospeed(&currentTermios, B115200) < 0)
        {
            serialError();
            return;
        }

        if (::tcsetattr(serialfd, TCSANOW, &currentTermios) == -1)
        {
            serialError();
            return;
        }

        serial_handler = ecore_main_fd_handler_add(serialfd,
                                                   (Ecore_Fd_Handler_Flags)
                                                   (ECORE_FD_READ | ECORE_FD_WRITE | ECORE_FD_ERROR),
                                                   _serial_handler_cb,
                                                   this,
                                                   NULL,
                                                   NULL);

        cInfoDom("mysensors") << "Serial port opened.";

        //request lib version from gateway
        sendMessage("0", "0", MySensors::INTERNAL, 0, MySensors::I_VERSION, "Get Version");
    }
    else
    {
        cErrorDom("mysensors") << "Unable to open serial gateway on " << param["port"];
        cErrorDom("mysensors") << "Retry later...";

        openSerialLater();
    }
}

void MySensorsController::closeSerial()
{
    if (serialfd == 0) return;
    ::tcdrain(serialfd); //flush
    ::tcsetattr(serialfd, TCSAFLUSH | TCSANOW, &oldTermios); // Restore old termios
    ::close(serialfd);
    serialfd = 0;
    if (serial_handler)
    {
        ecore_main_fd_handler_del(serial_handler);
        serial_handler = nullptr;
    }
}

void MySensorsController::openSerialLater(double t)
{
    if (timer) return;
    timer = new EcoreTimer(t, [=]()
    {
        delete timer;
        timer = NULL;
        openSerial();
    });
}

Eina_Bool MySensorsController::_serialHandler(Ecore_Fd_Handler *handler)
{
    if (ecore_main_fd_handler_active_get(handler, ECORE_FD_ERROR))
    {
        cErrorDom("mysensors") << "An error occured on the serial port";
        serialError();
        serial_handler = nullptr;
        return ECORE_CALLBACK_CANCEL;
    }

    if (ecore_main_fd_handler_active_get(handler, ECORE_FD_READ))
    {
        int bytesAvail = -1;
        if (::ioctl(serialfd, FIONREAD, &bytesAvail) == -1)
            bytesAvail = 4096;

        string data;
        data.resize(bytesAvail);
        if (::read(serialfd, (char *)data.c_str(), bytesAvail) == -1)
            serialError();

        //cDebugDom("mysensors") << "Data available on serial port, " << bytesAvail << " bytes: " << data;

        readNewData(data);
    }

    if (ecore_main_fd_handler_active_get(handler, ECORE_FD_WRITE))
    {
//        cDebugDom("mysensors") << "Data written on serial port";
    }

    return ECORE_CALLBACK_RENEW;
}

void MySensorsController::openTCP()
{
    cInfoDom("mysensors") << "TCP gateway not implemented yet";
}

void MySensorsController::readNewData(const string &data)
{
    dataBuffer += data;

    if (dataBuffer.find('\n') == string::npos)
    {
        //We don't have a complete paquet yet, wait for more data.
        return;
    }

    size_t pos;
    while ((pos = dataBuffer.find_first_of('\n')) != string::npos)
    {
        string message = dataBuffer.substr(0, pos); //extract message
        dataBuffer.erase(0, pos + 1); //remove from buffer
        processMessage(message);
    }
}

void MySensorsController::processMessage(string msg)
{
    cDebugDom("mysensors") << msg;
    //keep in sync with the lua Vera version here:
    //https://github.com/mysensors/Vera/blob/master/L_Arduino.lua

    vector<string> tokens;
    Utils::split(msg, tokens, ";", 6);

    string nodeid = tokens[0];
    string sensorid = tokens[1];
    int messagetype;
    Utils::from_string(tokens[2], messagetype);
    string ack = tokens[3];
    int subtype;
    Utils::from_string(tokens[4], subtype);
    string payload = tokens[5];

    switch (messagetype)
    {
    case MySensors::PRESENTATION:
        //payload contains lib version
        if (!gatewayVersion.empty() &&
            payload != gatewayVersion)
            cWarningDom("mysensors") << "Gateway version and node (" << nodeid << ") version mismatch!";
        //create a node if needed
        if (!hashSensors[nodeid].sensor_data.Exists(sensorid))
            hashSensors[nodeid].param.Add(sensorid, string());
        break;
    case MySensors::SET_VARIABLE:
        processSensorUpdate(nodeid, sensorid, subtype, payload);
        break;
    case MySensors::REQUEST_VARIABLE:
        processSensorRequest(nodeid, sensorid, subtype, payload);
        break;
    case MySensors::INTERNAL:
    {
        switch (subtype)
        {
        case MySensors::I_LOG_MESSAGE:
        case MySensors::I_GATEWAY_READY:
            cDebugDom("mysensors") << payload;
            break;
        case MySensors::I_ID_REQUEST:
            processRequestId(nodeid, sensorid);
            break;
        case MySensors::I_TIME:
            processTime(nodeid, sensorid);
            break;
        case MySensors::I_SKETCH_NAME:
            processNodeInfos(nodeid, sensorid, "name", payload);
            break;
        case MySensors::I_SKETCH_VERSION:
            processNodeInfos(nodeid, sensorid, "version", payload);
            break;
        case MySensors::I_BATTERY_LEVEL:
            processNodeInfos(nodeid, sensorid, "battery_level", payload);
            break;
        case MySensors::I_VERSION:
            gatewayVersion = payload;
            break;
        }
        break;
    }
    case MySensors::STREAM:
        break;
    default: break;
    }
}

void MySensorsController::processRequestId(string node_id, string sensor_id)
{
    //A node is requesting a new free id
    int newId = getNextFreeId();

    if (newId == 0xDEAD) //no more free ids...
    {
        cErrorDom("mysensors") << "A new node is requesting a free ID, but all 255 IDs are already taken. Give up.";
        return;
    }

    sendMessage(node_id, sensor_id, MySensors::INTERNAL, 0, MySensors::I_ID_RESPONSE, Utils::to_string(newId));
}

void MySensorsController::processTime(string node_id, string sensor_id)
{
    //Request time from one sensor, send back time as the seconds since 1970
    sendMessage(node_id, sensor_id, MySensors::INTERNAL, 0, MySensors::I_ID_RESPONSE, Utils::to_string(time(NULL)));
}

void MySensorsController::processNodeInfos(string node_id, string sensor_id, string key, string payload)
{
    VAR_UNUSED(sensor_id)
    hashSensors[node_id].param.Add(key, payload);
}

void MySensorsController::processSensorUpdate(string node_id, string sensor_id, int subtype, string payload)
{
    if (subtype == MySensors::V_CALAOS)
    {
        //the node wants to set a new value to a calaos IO
        //payload format is <calaos_id>:<value>
        //remember that payload size cannot exceed 24 bytes.
        //ex: output_45:toggle
        vector<string> tokens;
        Utils::split(payload, tokens, ":", 2);
        IOBase *io = ListeRoom::Instance().get_io(tokens[0]);
        if (io)
        {
            if (io->isInput())
            {
                if (io->get_type() == TBOOL)
                    io->set_value(tokens[1] == "true" || tokens[1] == "1");
                else if (io->get_type() == TINT)
                {
                    if (Utils::is_of_type<double>(tokens[1]))
                    {
                        double v;
                        Utils::from_string(tokens[1], v);
                        io->set_value(v);
                    }
                }
                else if (io->get_type() == TSTRING)
                    io->set_value(tokens[1]);
            }
            else
                io->set_value(tokens[1]);
        }
        else
        {
            cWarningDom("mysensors") << "Node " << node_id << "-" << sensor_id
                                     << " want to set value " << payload
                                     << " but IO is not found.";
            return;
        }
    }
    else
    {
        //cache data
        hashSensors[node_id].sensor_data.Add(sensor_id, payload);

        //notify calaos IO that value changed
        string key = node_id;
        key += ";";
        key += sensor_id;

        cDebugDom("mysensors") << "Node " << node_id << "-" << sensor_id
                               << " want to set value " << payload
                               << " hash key = " << key;

        sensorsCb[key].emit();
    }
}

void MySensorsController::processSensorRequest(string node_id, string sensor_id, int subtype, string payload)
{
    if (subtype == MySensors::V_CALAOS)
    {
        //the node requested an IO value from calaos
        //the payload is the calaos IO id
        string s;
        IOBase *io = ListeRoom::Instance().get_io(payload);
        if (io)
        {
            if (io->get_type() == TBOOL) s = Utils::to_string(io->get_value_bool());
            else if (io->get_type() == TINT) s = Utils::to_string(io->get_value_double());
            else if (io->get_type() == TSTRING) s = io->get_value_string();
        }
        else
        {
            cWarningDom("mysensors") << "Node " << node_id << "-" << sensor_id
                                     << " is requesting value from IO " << payload
                                     << " but IO is not found.";
            return;
        }
        sendMessage(node_id, sensor_id, MySensors::SET_VARIABLE, false, subtype, s);
    }
    else
    {
        //a node requested for it's last value
        string cached_data = hashSensors[node_id].sensor_data[sensor_id];
        sendMessage(node_id, sensor_id, MySensors::SET_VARIABLE, false, subtype, cached_data);
    }
}

void MySensorsController::sendMessage(string node_id, string sensor_id, int msgType, int ack, int subType, string payload)
{
    stringstream data;

    //check for payload size
    //should not exceed 25 bytes
    if (payload.length() > 25)
    {
        cWarningDom("mysensors") << "Payload size exceeds 25 bytes, truncating!";
        payload = payload.substr(0, 25);
    }
    data << node_id << ";" << sensor_id << ";" << msgType << ";" << ack << ";" << subType << ";" << payload << "\n";

    if (param["gateway"] == "serial")
    {
        if (::write(serialfd, data.str().c_str(), data.str().length()) == -1)
            serialError();
    }
    else if (param["gateway"] == "tcp")
    {
        //TODO
    }
}

int MySensorsController::getNextFreeId()
{
    for (uint i = 1;i < 255;i++)
    {
        if (hashSensors.find(Utils::to_string(i)) == hashSensors.end())
            return i;
    }

    return 0xDEAD;
}

void MySensorsController::registerIO(string nodeid, string sensorid, sigc::slot<void> callback)
{
    string key = nodeid;
    key += ";";
    key += sensorid;

    //check id
    int id;
    Utils::from_string(nodeid, id);
    if (id < 0 || id > 254)
    {
        cErrorDom("mysensors") << "Wrong node_id: " << nodeid;
        return;
    }

    //create a node object

    MySensorsNode node;
    //TODO: load from cache
    hashSensors[nodeid] = node;
    sensorsCb[key].connect(callback);
}

string MySensorsController::getValue(string node_id, string sensor_id, string key)
{
    //read data from the cache
    if (key == "payload")
        return hashSensors[node_id].sensor_data[sensor_id];
    else if (key == "name")
        return hashSensors[node_id].param["name"];
    else if (key == "version")
        return hashSensors[node_id].param["version"];
    else if (key == "battery_level")
        return hashSensors[node_id].param["battery_level"];

    return string();
}

void MySensorsController::setValue(string nodeid, string sensorid, int dataType, string payload)
{
    sendMessage(nodeid, sensorid, MySensors::SET_VARIABLE, 0, dataType, payload);
}

