/******************************************************************************
 **  Copyright (c) 2006-2016, Calaos. All Rights Reserved.
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

#include "TeleinfoCtrl.h"

#include <termios.h>



#if defined(__linux__) || defined(__linux) || defined(linux)
#include <sys/ioctlh>
#include <linux/serial.h>
#endif

using namespace Calaos;


TeleinfoCtrl::TeleinfoCtrl(const Params &p):
    param(p)
{
    if (!param.Exists("port")) param.Add("port", "/dev/ttyAMA0");
    openSerial();
}

TeleinfoCtrl::~TeleinfoCtrl()
{
}

static Eina_Bool _serial_handler_cb(void *data, Ecore_Fd_Handler *handler)
{
    TeleinfoCtrl *o = reinterpret_cast<TeleinfoCtrl *>(data);
    if (o) return o->_serialHandler(handler);
    return ECORE_CALLBACK_RENEW;
}

void TeleinfoCtrl::serialError()
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

    cErrorDom("teleinfo") << "Error on serial port: '" << errstr << "'";
    cErrorDom("teleinfo") << "Closing port and try again later...";

    closeSerial();
    openSerialLater();
}

void TeleinfoCtrl::openSerial()
{
    cDebugDom("teleinfo") << "Trying to open serial port " << param["port"];

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
        currentTermios.c_cc[VTIME] = 80;

        //7E1 1200
        currentTermios.c_cflag |= PARENB  ;
        currentTermios.c_cflag &= ~PARODD ;
        currentTermios.c_cflag &= ~CSTOPB ;
        currentTermios.c_cflag &= ~CSIZE ;
        currentTermios.c_cflag |= CS7 ;

        currentTermios.c_iflag |= (INPCK | ISTRIP) ;
        currentTermios.c_cflag &= ~CRTSCTS ;
        currentTermios.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG) ;
        currentTermios.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL) ;
        currentTermios.c_oflag &= ~OPOST ;

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
        if (::cfsetispeed(&currentTermios, B1200) < 0)
        {
            serialError();
            return;
        }
        if (::cfsetospeed(&currentTermios, B1200) < 0)
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

        cInfoDom("teleinfo") << "Serial port opened.";
    }
    else
    {
        cErrorDom("teleinfo") << "Unable to open serial gateway on " << param["port"];
        cErrorDom("teleinfo") << "Retry later...";

        openSerialLater();
    }
}

void TeleinfoCtrl::closeSerial()
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

void TeleinfoCtrl::openSerialLater(double t)
{
    if (timer) return;
    timer = new EcoreTimer(t, [=]()
    {
        delete timer;
        timer = NULL;
        openSerial();
    });
}

Eina_Bool TeleinfoCtrl::_serialHandler(Ecore_Fd_Handler *handler)
{
    if (ecore_main_fd_handler_active_get(handler, ECORE_FD_ERROR))
    {
        cErrorDom("teleinfo") << "An error occured on the serial port";
        serialError();
        serial_handler = nullptr;
        return ECORE_CALLBACK_CANCEL;
    }

    if (ecore_main_fd_handler_active_get(handler, ECORE_FD_READ))
    {
        int bytesAvail = 4096;
#if defined(__linux__) || defined(__linux) || defined(linux)
        if (::ioctl(serialfd, FIONREAD, &bytesAvail) == -1)
            bytesAvail = 4096;
#endif
        string data;
        data.resize(bytesAvail);
        if (::read(serialfd, (char *)data.c_str(), bytesAvail) == -1)
            serialError();

        cDebugDom("teleinfo") << "Data available on serial port, " << bytesAvail << " bytes: " << data;

        readNewData(data);
    }

    return ECORE_CALLBACK_RENEW;
}

void TeleinfoCtrl::readNewData(const string &data)
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

void TeleinfoCtrl::processMessage(string msg)
{
    cDebugDom("teleinfo") << msg;

    string::size_type n;
    for (auto &f : fields)
    {
        n = msg.find(f.name);
        if (n != string::npos)
        {
            f.value = msg.substr(n, f.size);
            valuesCb[f.name].emit();
        }
    }
}

void TeleinfoCtrl::registerIO(string teleinfoValue, sigc::slot<void> callback)
{
    valuesCb[teleinfoValue].connect(callback);
}

string TeleinfoCtrl::getValue(string teleinfoValue)
{
    for (auto &f : fields)
    {
        if (f.name == teleinfoValue)
            return f.value;
    }
    return string();
}
