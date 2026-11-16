/******************************************************************************
 **  Copyright (c) 2026, Calaos. All Rights Reserved.
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
#ifndef S_Klf200InputSwitch_H
#define S_Klf200InputSwitch_H

#include "InputSwitch.h"
#include "Klf200Ctrl.h"

namespace Calaos
{

/*
 * The rain sensor of a VELUX window, read through the KLF 200 gateway.
 *
 * The gateway does not expose the sensor as such: when it rains, it puts a
 * minimum-position limitation on the window, and calaos_klf200 polls that
 * limitation. The switch is true while the sensor reports rain.
 */
class Klf200InputSwitch: public InputSwitch
{
private:
    Klf200Ctrl *ctrl = nullptr;
    Klf200Ctrl::RegistrationId registrationId = Klf200Registry::INVALID_ID;
    bool rain = false;
    bool warnedNoSensor = false;

    void nodeEventReceived(const Params &event);

protected:
    virtual bool readValue() override;

public:
    Klf200InputSwitch(Params &p);
    virtual ~Klf200InputSwitch();
};

} //namespace Calaos

#endif
