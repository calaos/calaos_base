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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "MqttInputAnalog.h"
#include "IOFactory.h"
#include "AnalogIO.h"

using namespace Calaos;

REGISTER_IO(MqttInputAnalog)

MqttInputAnalog::MqttInputAnalog(Params &p):
    MqttIOBase(p, "MqttInputAnalog", _("Analog value read from a mqtt broker"))
{
    subscribeTopicSub([this]()
                      {
        readMessage();
    });

    cInfoDom("input") << "MqttInputAnalog::MqttInputAnalog()";
}

void MqttInputAnalog::readMessage()
{
    bool err;
    ctrl->getValueDouble(get_params(), err);
    //A message the broker replayed (retained) gives the value, but its sensor
    //may be long dead: it is not a reading, even when the value changes.
    replayed = ctrl->replayingRetained();
    //readValue() signals a change only, yet the sensor is alive.
    if (!err && !replayed)
        markReading();
    readValue();
    replayed = false;
}

void MqttInputAnalog::readValue()
{
    bool err;
    double v = ctrl->getValueDouble(get_params(), err);
    if (err)
        return;
    //Compared once converted: the input poll reads the last value again, and
    //signalling it would date a reading.
    const double converted = AnalogIO::convertValue(get_params(), v);
    if (converted != value)
    {
        value = converted;
        emitChange();
    }
}
