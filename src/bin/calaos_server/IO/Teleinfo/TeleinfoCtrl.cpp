/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
#include "libuvw.h"

#if defined(__linux__) || defined(__linux) || defined(linux)
#include <sys/ioctl.h>
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

        serialHandle = uvw::Loop::getDefault()->resource<uvw::PipeHandle>();
        serialHandle->open(serialfd);

        //When serial is closed, remove it and close it
        serialHandle->once<uvw::EndEvent>([](const uvw::EndEvent &, auto &cl)
        {
            cl.close();
        });

        serialHandle->once<uvw::ErrorEvent>([](const uvw::ErrorEvent &, auto &cl)
        {
            cl.close();
        });

        //When connection is closed
        serialHandle->once<uvw::CloseEvent>([this](const uvw::CloseEvent &, auto &)
        {
            this->closeSerial();
            this->openSerialLater();
        });

        serialHandle->on<uvw::DataEvent>([this](const uvw::DataEvent &ev, auto &)
        {
            string d((char *)ev.data.get(), ev.length);
            this->readNewData(d);
        });

        serialHandle->read();

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

    if (serialHandle && serialHandle->active())
    {
        serialHandle->stop();
        serialHandle->close();
    }

    ::tcdrain(serialfd); //flush
    ::tcsetattr(serialfd, TCSAFLUSH | TCSANOW, &oldTermios); // Restore old termios
    ::close(serialfd);
    serialfd = 0;
}

void TeleinfoCtrl::openSerialLater(double t)
{
    if (timer) return;
    timer = new Timer(t, [=]()
    {
        delete timer;
        timer = NULL;
        openSerial();
    });
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
            f.value = msg.substr(f.name.size() + 1, f.size);
            cDebugDom("teleinfo") << "Value read  " << f.name << ": " << f.value;
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
