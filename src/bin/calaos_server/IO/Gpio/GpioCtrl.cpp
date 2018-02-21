/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#include "GpioCtrl.h"
#include "libuvw.h"

using namespace Calaos;

GpioCtrl::GpioCtrl(int _gpionum, double _debounce_time)
{
    debounce = false;
    debounce_time = _debounce_time;
    gpionum = _gpionum;
    gpionum_str = Utils::to_string(gpionum);
    cDebugDom("input") << "Create GpioCtrl for " << gpionum_str;
    exportGpio();
    fd = -1;
}

GpioCtrl::~GpioCtrl()
{
    if (fdHandle && fdHandle->active())
    {
        fdHandle->stop();
        fdHandle->close();
    }

    //unexportGpio();
    if (fd == -1)
        close(fd);
    fd = -1;
    cDebugDom("input") << "Delete GpioCtrl";
}

int GpioCtrl::writeFile(string path, string value)
{
    ofstream ofspath(path.c_str());
    cDebug() << "ofstream " << path;
    if (!ofspath.is_open())
    {
        cErrorDom("input") << "Unable to export GPIO " << gpionum;
        // Unable to export GPIO
        return false;
    }
    ofspath << value;
    ofspath.close();

    return true;
}

int GpioCtrl::readFile(string path, string &value)
{
    ifstream ifspath(path.c_str());
    if (!ifspath.is_open())
    {
        cErrorDom("input") << "Unable to read file " << strerror(errno);
        return false;
    }

    ifspath >> value;
    ifspath.close();

    return true;
}

bool GpioCtrl::exportGpio()
{
    return writeFile("/sys/class/gpio/export", gpionum_str);
}

bool GpioCtrl::unexportGpio()
{
    return writeFile("/sys/class/gpio/unexport", gpionum_str);
}

// Set GPIO direction : "in" or "out"
bool GpioCtrl::setDirection(string direction)
{
    string path = "/sys/class/gpio/gpio" + Utils::to_string(gpionum) + "/direction";
    return writeFile(path, direction);
}

// Set GPIO signal edge : "none", "rising", "falling" or "both"
bool GpioCtrl::setEdge(string direction)
{
    string path = "/sys/class/gpio/gpio" + Utils::to_string(gpionum) + "/edge";
    return writeFile(path, direction);
}

// Set GPIO active low : "1" for active_low "0", for active_high (standard behaviour)
bool GpioCtrl::setActiveLow(bool active_low)
{
    string path = "/sys/class/gpio/gpio" + Utils::to_string(gpionum) + "/active_low";
    return writeFile(path, active_low ? "1" : "0");
}

bool GpioCtrl::setVal(bool value)
{
    string strval;
    string path = "/sys/class/gpio/gpio" + Utils::to_string(gpionum) + "/value";
    strval = Utils::to_string(value);
    cDebugDom("input") << "Set value " << value << " to " << path;
    return writeFile(path, strval);
}

bool GpioCtrl::getVal(bool &value)
{
    string strval;
    string path = "/sys/class/gpio/gpio" + Utils::to_string(gpionum) + "/value";

    if (!readFile(path, strval))
    {
        cErrorDom("input") << "Unable to read file " << path;
        return false;
    }

    cDebugDom() << "Read value : " << strval;
    if (strval == "1")
        value = true;
    else
        value = false;

    return true;
}


void GpioCtrl::closeFd(void)
{
    if (fd != -1)
        close(fd);
    fd = -1;
}

int GpioCtrl::getGpioNum(void)
{
    return gpionum;
}

void GpioCtrl::emitChange()
{
    char buf[2];

    if (fd == -1)
        return;

    cInfoDom("input") << "Input change detected";

    lseek(fd, 0, SEEK_SET);
    memset(buf, 0, 2);
    read(fd, buf, 1);

    if (debounce)
    {
        cDebugDom("input") << "debouncing....";
        return;
    }
    else
    {
        debounce = true;

        Timer::singleShot(0.05, [=](){
            cDebugDom("input") << "Debounce finished";
            debounce = false;
            event_signal.emit();
        });
    }

}

bool GpioCtrl::setValueChanged(sigc::slot<void> slot)
{
    string strval;
    string path = "/sys/class/gpio/gpio" + Utils::to_string(gpionum) + "/value";

    if (fd == -1)
    {
        fd = open(path.c_str(), O_RDONLY);

        if (fd == -1)
        {
            cError() << "Unable to get fd " << strerror(errno);
            return false;
        }
    }

    // Programm both edge to trigger fd
    setEdge("both");

    connection = event_signal.connect(slot);

    fdHandle = uvw::Loop::getDefault()->resource<uvw::PipeHandle>();
    fdHandle->open(fd);

    //When serial is closed, remove it and close it
    fdHandle->once<uvw::EndEvent>([](const uvw::EndEvent &, auto &cl)
    {
        cl.close();
    });

    fdHandle->on<uvw::DataEvent>([this](const uvw::DataEvent &, auto &)
    {
        this->emitChange();
    });

    fdHandle->read();

    return true;
}
