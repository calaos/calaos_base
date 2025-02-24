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
#include <Utils.h>
#include <IOFactory.h>

#include "GpioInputSwitchLongPress.h"

using namespace Calaos;

REGISTER_IO(GpioInputSwitchLongPress)

GpioInputSwitchLongPress::GpioInputSwitchLongPress(Params &p):
    InputSwitchLongPress(p),
    gpioctrl(NULL)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("GpioInputSwitchLongPress");
    ioDoc->descriptionSet(_("Input switch with a GPIO"));
    ioDoc->paramAddInt("gpio", _("GPIO ID on your hardware"), 0, 65535, true);
    ioDoc->paramAdd("active_low", _("Set this is your GPIO has an inverted level"), IODoc::TYPE_BOOL, false, "false");

    int gpio_nb;
    bool active_low = false;

    if (!param_exists("active_low")) set_param("active_low", "false");

    Utils::from_string(get_param("gpio"), gpio_nb);
    Utils::from_string(get_param("active_low"), active_low);

    gpioctrl = new GpioCtrl(gpio_nb);
    gpioctrl->setDirection("in");

    gpioctrl->setValueChanged([=] {
            gpioctrl->getVal(val);
            hasChanged();
            cInfoDom("Input") << "Input value changed, new value : " << val;
        });

    cInfoDom("Input") << "Create gpio input for gpio " << gpio_nb << " active_low : " << active_low;
}

GpioInputSwitchLongPress::~GpioInputSwitchLongPress()
{
    delete gpioctrl;
}

bool GpioInputSwitchLongPress::readValue()
{
    cInfoDom("Input") << "Read Value : " << val;
    return val;
}


