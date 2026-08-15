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
#ifndef S_GpioInputBase_H
#define S_GpioInputBase_H

#include <Utils.h>
#include <Params.h>
#include <IODoc.h>
#include "ThinIo.h"
#include "GpioCtrl.h"

namespace Calaos
{

//Shared skeleton for the thin Gpio input subclasses (T3.2f): common ioDoc,
//param parsing, GpioCtrl setup and value plumbing. BaseT is one of
//InputSwitch / InputSwitchLongPress / InputSwitchTriple.
//
//T3.10: the friendlyName/description preamble comes from the shared ThinIo
//template (IO/ThinIo.h); the description is the same for the three inputs.
template<typename BaseT>
class GpioInputBase : public ThinIo<BaseT>
{
private:
    GpioCtrl *gpioctrl = nullptr;

protected:
    bool val = false;

    GpioInputBase(Params &p, const char *friendlyName):
        ThinIo<BaseT>(p, friendlyName, _("Input switch with a GPIO"))
    {
        // Define IO documentation
        this->ioDoc->paramAddInt("gpio", _("GPIO ID on your hardware"), 0, 65535, true);
        this->ioDoc->paramAdd("active_low", _("Set this if your GPIO has an inverted level"), IODoc::TYPE_BOOL, false, "false");
        this->ioDoc->paramAddFloat("debounce", _("Debounce time in seconds. Values changing faster than this are filtered out"),
                                   false, 0.0, GpioCtrl::DEBOUNCE_TIME_MAX, GpioCtrl::DEBOUNCE_TIME_DEFAULT);

        int gpio_nb;
        bool active_low = false;

        if (!this->param_exists("active_low")) this->set_param("active_low", "false");

        Utils::from_string(this->get_param("gpio"), gpio_nb);
        Utils::from_string(this->get_param("active_low"), active_low);

        //Bounded parse with fallback: a garbage or out-of-range debounce
        //value must warn and use the default, not silently become 0.
        const std::string dbstr = this->get_param("debounce");
        double debounce = GpioCtrl::parseDebounceTime(dbstr);
        if (!dbstr.empty() && GpioCtrl::parseDebounceTime(dbstr, -1.0) < 0.0)
            cWarningDom("input") << "Invalid debounce '" << dbstr
                                 << "', falling back to " << debounce << "s";

        gpioctrl = new GpioCtrl(gpio_nb, debounce);
        gpioctrl->setDirection("in");
        gpioctrl->setActiveLow(active_low);

        gpioctrl->setValueChanged([this] {
                gpioctrl->getVal(val);
                this->hasChanged();
                cInfoDom("Input") << "Input value changed, new value : " << val;
            });

        cInfoDom("Input") << "Create gpio input for gpio " << gpio_nb << " active_low : " << active_low;
    }

    virtual ~GpioInputBase()
    {
        delete gpioctrl;
    }

    virtual bool readValue()
    {
        cInfoDom("Input") << "Read Value : " << val;
        return val;
    }
};

}
#endif
