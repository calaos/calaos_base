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
#include "MqttOutputLightRGB.h"
#include "MqttBrokersList.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(MqttOutputLightRGB)

MqttOutputLightRGB::MqttOutputLightRGB(Params &p):
    OutputLightRGB(p)
{
    // We use real state for this IO: only emit change when the value really changes
    //useRealState = true;

    // Define IO documentation
    ioDoc->friendlyNameSet("MqttOutputLightRGB");
    ioDoc->descriptionSet(_("Control RGB lights through mqtt broker"));
    MqttCtrl::commonDoc(ioDoc);

    ioDoc->paramAdd("data", _("The data sent when publishing color to topic. The __##VALUE_R##__  __##VALUE_G##__  __##VALUE_B##__ or __##VALUE_HEX##__ or __##VALUE_X##__ __##VALUE_Y##__ __##VALUE_BRIGHTNESS##__ contained in data is substituted "
                              "with the color (integer value or #RRGGBB string value) to be sent."),
                    IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("path_x", _("The path where to found the X (X/Y Color space) value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example color/x, try to read the x value from the color object."), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("path_y", _("The path where to found the Y (X/Y Color space) value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example color/y, try to read the x value from the color object."), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("path_brightness", _("The path where to found the brightness value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example 'brightness'"), IODoc::TYPE_STRING, true);

    ctrl = MqttBrokersList::Instance().get_ctrl(get_params());
    ctrl->subscribeTopic(get_param("topic_sub"), [=](string, string)
    {
        readValue();
    });

    cInfoDom("output") << "MqttOutputLightRGB::MqttOutputLightRGB()";
}

void MqttOutputLightRGB::readValue()
{
    bool err;
    auto c = ctrl->getValueColor(get_params(), err);

    //TODO: it does not work for now. We need to refactor the way it handle color+state in all
    //RGB class and also add better state/color/brightness control in calaos

    //if (!err)
        //stateUpdated(c, c != ColorValue(0, 0, 0));
}

void MqttOutputLightRGB::setColorReal(const ColorValue &c, bool _state)
{
    cDebugDom("mqtt") << "Set value to " << c.toString() << " state " << _state;

    if (!_state) //turn light off directly
        ctrl->setValueColor(get_params(), ColorValue(0, 0, 0));
    else
        ctrl->setValueColor(get_params(), c);
}
