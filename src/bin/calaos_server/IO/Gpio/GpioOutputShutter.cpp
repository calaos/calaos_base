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
#include <IOFactory.h>
#include "GpioCtrl.h"

#include "GpioOutputShutter.h"

using namespace Calaos;

REGISTER_OUTPUT(GpioOutputShutter)

GpioOutputShutter::GpioOutputShutter(Params &p):
    OutputShutter(p)
{
    int up_address, down_address;

    Utils::from_string(get_param("gpio_up"), up_address);
    Utils::from_string(get_param("gpio_down"), down_address);

    gpioctrl_up = new GpioCtrl(up_address);
    gpioctrl_up->setDirection("out");

    gpioctrl_down = new GpioCtrl(down_address);
    gpioctrl_down->setDirection("out");
}

GpioOutputShutter::~GpioOutputShutter()
{
    delete gpioctrl_up;
    delete gpioctrl_down;
}

void GpioOutputShutter::setOutputUp(bool enable)
{
    gpioctrl_up->setVal(enable);
}

void GpioOutputShutter::setOutputDown(bool enable)
{
    gpioctrl_down->setVal(enable);
}

