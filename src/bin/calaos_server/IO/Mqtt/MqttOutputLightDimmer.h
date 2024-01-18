/******************************************************************************
 **  Copyright (c) 2006-2019, Calaos. All Rights Reserved.
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
#ifndef __MQTT_OUTPUT_LIGHT_DIMMER_H__
#define __MQTT_OUTPUT_LIGHT_DIMMER_H__

#include "OutputLightDimmer.h"
#include "MqttCtrl.h"

namespace Calaos
{

class MqttOutputLightDimmer : public OutputLightDimmer
{
private:
    MqttCtrl *ctrl;

protected:
    void readValue();
    virtual bool set_value_real(int val) override;

public:
    MqttOutputLightDimmer(Params &p);
};

}

#endif // __MQTT_OUTPUT_LIGHT_DIMMER_H__
