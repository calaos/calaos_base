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
        openSerial();
        delete timer;
        timer = NULL;
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

    return ECORE_CALLBACK_RENEW;
}

void MySensorsController::openTCP()
{
    cInfoDom("mysensors") << "TCP gateway not implemented yet";
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

