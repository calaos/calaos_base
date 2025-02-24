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
#include "MqttOutputLightDimmer.h"
#include "MqttBrokersList.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(MqttOutputLightDimmer)

MqttOutputLightDimmer::MqttOutputLightDimmer(Params &p):
    OutputLightDimmer(p)
{
    // We use real state for this IO: only emit change when the value really changes
    useRealState = true;

    // Define IO documentation
    ioDoc->friendlyNameSet("MqttOutputLightDimmer");
    ioDoc->descriptionSet(_("Control lights through mqtt broker"));
    MqttCtrl::commonDoc(ioDoc);

    ioDoc->paramAdd("data", _("The data sent when publishing to topic. The __##VALUE##__ contained in data is substituted "
                              "with the state (integer value) to be sent."),
                    IODoc::TYPE_STRING, true);

    ctrl = MqttBrokersList::Instance().get_ctrl(get_params());
    ctrl->subscribeTopic(get_param("topic_sub"), [=](string, string)
    {
        readValue();
    });

    cInfoDom("output") << "MqttOutputLightDimmer::MqttOutputLightDimmer()";
}

void MqttOutputLightDimmer::readValue()
{
    bool err;
    auto newValue = ctrl->getValueDouble(get_params(), err);

    if (!err)
    {
        value = newValue;
        cmd_state = "set " + Utils::to_string(value);

        EmitSignalIO();
        emitChange();
    }
}

bool MqttOutputLightDimmer::set_value_real(int val)
{
    cDebugDom("mqtt") << "Set value to " << val;
    ctrl->setValueInt(get_params(), val);
    return true;
}
