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
#include "MqttOutputShutter.h"
#include "MqttBrokersList.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(MqttOutputShutter)

MqttOutputShutter::MqttOutputShutter(Params &p):
    OutputShutter(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("MqttOutputShutter");
    ioDoc->descriptionSet(_("Control shutter through mqtt broker"));
    MqttCtrl::commonDoc(ioDoc);

    ctrl = MqttBrokersList::Instance().get_ctrl(get_params());

    cInfoDom("output") << "MqttOutputShutter::MqttOutputShutter()";
}

MqttOutputShutter::~MqttOutputShutter()
{
}

void MqttOutputShutter::setOutputUp(bool enable)
{
  ctrl->setValueString(get_params(), "0");

}

void MqttOutputShutter::setOutputDown(bool enable)
{
 ctrl->setValueString(get_params(), "99");

}

