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
#ifndef GPIOCTRL_H
#define GPIOCTRL_H

#include <Utils.h>
#include <Ecore.h>

namespace Calaos
{

class GpioCtrl
{
private:
    int gpionum; // GPIO Number
    string gpionum_str;
    int writeFile(string path, string value);
    int readFile(string path, string &value);
    int fd;
    sigc::connection connection;
    sigc::signal<void> event_signal;
    Ecore_Fd_Handler *fd_handler;
    
public:
    GpioCtrl(int _gpionum);
    ~GpioCtrl();
    bool exportGpio();
    bool unexportGpio();
    bool setDirection(string direction);
    bool setval(bool val);
    bool getVal(bool &val);
    int getFd(void);
    void closeFd(void);
    int getGpioNum(void);
    bool setValueChanged(sigc::slot<void> slot);
    void emitChange(void);
};
}
#endif // GPIOCTRL_H
