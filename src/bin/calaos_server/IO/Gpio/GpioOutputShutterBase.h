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
#ifndef S_GpioOutputShutterBase_H
#define S_GpioOutputShutterBase_H

#include <Utils.h>
#include <Params.h>
#include <IODoc.h>
#include "GpioCtrl.h"

namespace Calaos
{

//Shared skeleton for the two Gpio shutter subclasses (T3.2f): common ioDoc,
//param parsing and up/down GpioCtrl setup. BaseT is OutputShutter or
//OutputShutterSmart.
template<typename BaseT>
class GpioOutputShutterBase : public BaseT
{
private:
    GpioCtrl *gpioctrl_up = nullptr, *gpioctrl_down = nullptr;

    virtual void setOutputUp(bool enable)
    {
        gpioctrl_up->setVal(enable);
    }

    virtual void setOutputDown(bool enable)
    {
        gpioctrl_down->setVal(enable);
    }

protected:
    GpioOutputShutterBase(Params &p, const char *friendlyName, const char *logLabel):
        BaseT(p)
    {
        // Define IO documentation
        this->ioDoc->friendlyNameSet(friendlyName);
        this->ioDoc->descriptionSet(_("Shutter with 2 GPIOs"));
        this->ioDoc->paramAddInt("gpio_up", _("GPIO ID for opening on your hardware"), 0, 65535, true);
        this->ioDoc->paramAddInt("gpio_down", _("GPIO ID for closing on your hardware"), 0, 65535, true);
        this->ioDoc->paramAdd("active_low_up", _("Set this if your GPIO has an inverted level"), IODoc::TYPE_BOOL, false, "false");
        this->ioDoc->paramAdd("active_low_down", _("Set this if your GPIO has an inverted level"), IODoc::TYPE_BOOL, false, "false");

        if (!this->param_exists("active_low_up")) this->set_param("active_low_up", "false");
        if (!this->param_exists("active_low_down")) this->set_param("active_low_down", "false");

        int gpio_up_nb, gpio_down_nb;
        bool active_low_up, active_low_down;

        Utils::from_string(this->get_param("gpio_up"), gpio_up_nb);
        Utils::from_string(this->get_param("gpio_down"), gpio_down_nb);

        Utils::from_string(this->get_param("active_low_up"), active_low_up);
        Utils::from_string(this->get_param("active_low_down"), active_low_down);

        gpioctrl_up = new GpioCtrl(gpio_up_nb);
        gpioctrl_up->setDirection("out");
        gpioctrl_up->setActiveLow(active_low_up);

        gpioctrl_down = new GpioCtrl(gpio_down_nb);
        gpioctrl_down->setDirection("out");
        gpioctrl_down->setActiveLow(active_low_down);

        cInfoDom("Input") << "Create " << logLabel << " gpio up " << gpio_up_nb << " active_low_up : " << active_low_up
                          << " gpio down " << gpio_down_nb << " active_low_down : " << active_low_down;
    }

    virtual ~GpioOutputShutterBase()
    {
        delete gpioctrl_up;
        delete gpioctrl_down;
    }
};

}
#endif
