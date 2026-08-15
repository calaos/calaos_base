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
#ifndef __REOLINK_INPUT_SWITCH_H__
#define __REOLINK_INPUT_SWITCH_H__

#include <memory>

#include "InputSwitch.h"
#include "ReolinkCtrl.h"

namespace Calaos
{

class ReolinkInputSwitch : public InputSwitch
{
private:
    ReolinkCtrl *ctrl = nullptr;
    ReolinkCtrl::RegistrationId registrationId = ReolinkEventRegistry::INVALID_ID;
    string lastEventData;
    bool eventReceived;

    /* Lifetime token for asynchronous callbacks (camera events stored in
     * the ReolinkCtrl singleton, Timer::singleShot): callbacks hold a
     * weak_ptr on it, when this IO dies the token expires and stale
     * callbacks become no-ops. */
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    void eventReceivedCallback(string hostname, string event_type, string event_data);

protected:
    virtual bool readValue() override;

public:
    ReolinkInputSwitch(Params &p);
    virtual ~ReolinkInputSwitch();
};

}

#endif // __REOLINK_INPUT_SWITCH_H__