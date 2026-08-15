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
#pragma once

#include <utility>

#include "OutputLightRGB.h"
#include "MqttIOBase.h"

namespace Calaos
{

class MqttOutputLightRGB : public MqttIOBase<OutputLightRGB>
{
protected:
    void readValue();
    virtual void setColorReal(const ColorValue &color, bool state) override;

public:
    MqttOutputLightRGB(Params &p);

    //T3.2c: pure decision logic for the color+state broker feedback,
    //header-inline so it is unit testable without linking MqttCtrl.
    //The MQTT payload carries no separate on/off state (only
    //path_x/path_y/path_brightness), and setColorReal() publishes black
    //for OFF; so black feedback means OFF, and it must NOT clobber the
    //last known color (set_value("on") re-sends the stored color: a
    //black stored color would keep the light off forever).
    //Returns {color to store, on/off state}.
    static std::pair<ColorValue, bool> stateFromColor(const ColorValue &incoming,
                                                      const ColorValue &current)
    {
        if (incoming == ColorValue(0, 0, 0))
            return { current, false };
        return { incoming, true };
    }
};

}
