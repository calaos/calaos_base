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
#include "GpioCtrl.h"

using namespace Calaos;

GpioCtrl::GpioCtrl(int _gpionum)
{
    gpionum = _gpionum;
    gpionum_str = Utils::to_string(gpionum);
    exportGpio();
    fd = -1;
}

GpioCtrl::~GpioCtrl()
{
    unexportGpio();
    if (fd == -1)
        close(fd);
    fd = -1;
}

int GpioCtrl::writeFile(string path, string value)
{
    ofstream ofspath(path.c_str());
    if (ofspath.is_open())
    {
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
    if (ifspath.is_open())
    {
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
    string strval;
    char tmp[4096];
    snprintf(tmp, sizeof(tmp) - 1, "/sys/class/gpio/gpio%d/direction", gpionum);
    string path(tmp);
    return writeFile(path, direction);
}

bool GpioCtrl::setval(bool val)
{
    string strval;
    char tmp[4096];
    snprintf(tmp, sizeof(tmp) - 1, "/sys/class/gpio/gpio%d/value", gpionum);
    string path(tmp);

    strval = Utils::to_string(val);

    return writeFile(path, strval);
}

bool GpioCtrl::getVal(bool &val)
{
    string strval;
    char tmp[4096];
    snprintf(tmp, sizeof(tmp) - 1, "/sys/class/gpio/gpio%d/value", gpionum);
    string path(tmp);

    if (!readFile(path, strval))
        return false;
    if (strval == "1")
        return true;
    else
        return false;
}

int GpioCtrl::getFd(void)
{
    string strval;
    char tmp[4096];
    snprintf(tmp, sizeof(tmp) - 1, "/sys/class/gpio/gpio%d/value", gpionum);
    string path(tmp);

    if (fd == -1)
        fd = open(path.c_str(), O_RDONLY);
    return fd;

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
