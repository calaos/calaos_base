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
#include <linux/serial.h>

using namespace Calaos;

MySensorsController::MySensorsController(const Params &p):
    param(p)
{
    sensorsIds.resize(255, false);
    sensorsIds[0] = true; //ID 0 is the gateway

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
        break;
    case MySensors::SET_VARIABLE:
        break;
    case MySensors::REQUEST_VARIABLE:
        break;
    case MySensors::INTERNAL:
    {
        switch (subtype)
        {
        case MySensors::I_LOG_MESSAGE:
        case MySensors::I_GATEWAY_READY:
            cInfoDom("mysensors") << payload;
            break;
        case MySensors::I_ID_REQUEST:
            processRequestId(nodeid, sensorid);
            break;
        case MySensors::I_TIME:
            processTime(nodeid, sensorid);
            break;
        case MySensors::I_SKETCH_NAME:
            processNodeInfos(nodeid, sensorid, "name");
            break;
        case MySensors::I_SKETCH_VERSION:
            processNodeInfos(nodeid, sensorid, "version");
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

    sendMessage(node_id, sensor_id, MySensors::INTERNAL, MySensors::I_ID_RESPONSE, Utils::to_string(newId));
}

void MySensorsController::processTime(string node_id, string sensor_id)
{
    //Request time from one sensor, send back time as the seconds since 1970
    sendMessage(node_id, sensor_id, MySensors::INTERNAL, MySensors::I_ID_RESPONSE, Utils::to_string(time(NULL)));
}

void MySensorsController::processNodeInfos(string node_id, string sensor_id, string key)
{
    //TODO
}

void MySensorsController::sendMessage(string node_id, string sensor_id, int msgType, int subType, string payload)
{
    stringstream data;

    //check for payload size
    //should not exceed 25 bytes
    if (payload.length() > 25)
    {
        cWarningDom("mysensors") << "Payload size exceeds 25 bytes, truncating!";
        payload = payload.substr(0, 25);
    }
    data << node_id << ";" << sensor_id << ";" << msgType << ";" << subType << ";" << payload << "\n";

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
    bool retry = true;
    for (uint i = nextFreeId;i < sensorsIds.size();i++)
    {
        if (!sensorsIds[i]) //free
        {
            nextFreeId = i;
            break;
        }

        if (i == sensorsIds.size() - 1)
        {
            //we are at the end, restart at the beginning in case we missed one
            if (retry)
                i = 0;
            else
                return 0xDEAD;
            retry = false;
        }
    }

    return nextFreeId;
}

void MySensorsController::registerIO(string nodeid, string sensorid, sigc::slot<void> callback)
{
    string key = nodeid;
    key += ";";
    key += sensorid;

    sensorsCb[key].connect(callback);
}

string MySensorsController::getValue(string nodeid, string sensorid, string key)
{
    return string();
}

void MySensorsController::setValue(string nodeid, string sensorid, string payload)
{
}

