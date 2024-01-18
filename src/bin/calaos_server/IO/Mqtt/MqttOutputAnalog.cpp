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
#include "MqttOutputAnalog.h"
#include "MqttBrokersList.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(MqttOutputAnalog)

MqttOutputAnalog::MqttOutputAnalog(Params &p):
    OutputAnalog(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("MqttOutputAnalog");
    ioDoc->descriptionSet(_("Control analog output through mqtt broker"));
    MqttCtrl::commonDoc(ioDoc);

    ioDoc->paramAdd("data", _("The data sent when publishing to topic. The __##VALUE##__ contained in data is substituted "
                              "with the state (float value) to be sent."),
                    IODoc::TYPE_STRING, true);

    ctrl = MqttBrokersList::Instance().get_ctrl(get_params());
    ctrl->subscribeTopic(get_param("topic_sub"), [=](string, string)
    {
        readValue();
    });

    cInfoDom("output") << "MqttOutputAnalog::MqttOutputAnalog()";
}

void MqttOutputAnalog::readValue()
{
    bool err;
    auto newValue = ctrl->getValueDouble(get_params(), err);
    
    if (!err)
    {
        value = newValue;

        EmitSignalIO();
        emitChange();
    }
}

void MqttOutputAnalog::set_value_real(double val)
{
    ctrl->setValueString(get_params(), Utils::to_string(val));
}
