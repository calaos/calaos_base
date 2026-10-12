/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
#include "MqttOutputString.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(MqttOutputString)

MqttOutputString::MqttOutputString(Params &p):
    MqttIOBase(p, "MqttOutputString", _("Control a string output through mqtt broker"))
{
    ioDoc->paramAdd("data", _("The data sent when publishing to topic. The __##VALUE##__ contained in data is substituted "
                              "with the string to be sent. If data is a JSON document (starts with `{`, `[` or `\"`), "
                              "the string is JSON-escaped, so write the placeholder inside the quotes: "
                              "`{\"mode\":\"__##VALUE##__\"}`."),
                    IODoc::TYPE_STRING, true);
    ioDoc->actionAdd("value", _("Publish a specific string value"));

    subscribeTopicSub([this]()
                      {
        readValue();
    });

    cInfoDom("output") << "MqttOutputString::MqttOutputString()";
}

void MqttOutputString::readValue()
{
    bool err;
    string v = ctrl->getValue(get_params(), err, "topic_sub");

    if (!err && v != value)
    {
        value = v;

        EmitSignalIO();
        emitChange();
    }
}

void MqttOutputString::set_value_real(string val)
{
    ctrl->setValueString(get_params(), val);
}
