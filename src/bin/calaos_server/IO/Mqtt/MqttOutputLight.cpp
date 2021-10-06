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
#include "MqttOutputLight.h"
#include "MqttBrokersList.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(MqttOutputLight)

MqttOutputLight::MqttOutputLight(Params &p):
    OutputLight(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("MqttOutputLight");
    ioDoc->descriptionSet(_("Control lights through mqtt broker"));
    MqttCtrl::commonDoc(ioDoc);

    ioDoc->paramAdd("on_value", _("Value to interpret as ON value"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("off_value", _("Value to interpret as OFF value"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("data", _("The data sent when publishing to topic. The __##VALUE##__ contained in data is substituted with "
                              "with the state (on_value, off_value) to be sent."),
                    IODoc::TYPE_STRING, true);

    ctrl = MqttBrokersList::Instance().get_ctrl(get_params());

    cInfoDom("output") << "MqttOutputLight::MqttOutputLight()";
}

MqttOutputLight::~MqttOutputLight()
{

}

void MqttOutputLight::readValue()
{
    bool err;
    value = ctrl->getValueDouble(get_params(), err);
    if (!err)
        emitChange();
}

bool MqttOutputLight::set_value_real(bool val)
{
    cDebugDom("mqtt") << "Set value to " << val;
    ctrl->setValue(get_params(), val);
    return true;
}
