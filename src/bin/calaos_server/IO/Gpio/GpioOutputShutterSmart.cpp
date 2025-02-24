/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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

#include "GpioOutputShutterSmart.h"

using namespace Calaos;

REGISTER_IO(GpioOutputShutterSmart)

GpioOutputShutterSmart::GpioOutputShutterSmart(Params &p):
    OutputShutterSmart(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("GpioOutputShutterSmart");
    ioDoc->descriptionSet(_("Shutter with 2 GPIOs"));
    ioDoc->paramAddInt("gpio_up", _("GPIO ID for opening on your hardware"), 0, 65535, true);
    ioDoc->paramAddInt("gpio_down", _("GPIO ID for closing on your hardware"), 0, 65535, true);
    ioDoc->paramAdd("active_low_up", _("Set this is your GPIO has an inverted level"), IODoc::TYPE_BOOL, false, "false");
    ioDoc->paramAdd("active_low_down", _("Set this is your GPIO has an inverted level"), IODoc::TYPE_BOOL, false, "false");

    if (!param_exists("active_low_up")) set_param("active_low_up", "false");
    if (!param_exists("active_low_down")) set_param("active_low_down", "false");


    int gpio_up_nb, gpio_down_nb;
    bool active_low_up, active_low_down;

    Utils::from_string(get_param("gpio_up"), gpio_up_nb);
    Utils::from_string(get_param("gpio_down"), gpio_down_nb);

    Utils::from_string(get_param("active_low_up"), active_low_up);
    Utils::from_string(get_param("active_low_down"), active_low_down);

    gpioctrl_up = new GpioCtrl(gpio_up_nb);
    gpioctrl_up->setDirection("out");
    gpioctrl_up->setActiveLow(active_low_up);

    gpioctrl_down = new GpioCtrl(gpio_down_nb);
    gpioctrl_down->setDirection("out");
    gpioctrl_down->setActiveLow(active_low_down);

    cInfoDom("Input") << "Create Shutter Smart output" << " gpio up " << gpio_up_nb << " active_low_up : " << active_low_up
                      << " gpio down " << gpio_down_nb << " active_low_down : " << active_low_down;

}

GpioOutputShutterSmart::~GpioOutputShutterSmart()
{
    delete gpioctrl_up;
    delete gpioctrl_down;
}

void GpioOutputShutterSmart::setOutputUp(bool enable)
{
    gpioctrl_up->setVal(enable);
}

void GpioOutputShutterSmart::setOutputDown(bool enable)
{
    gpioctrl_down->setVal(enable);
}
