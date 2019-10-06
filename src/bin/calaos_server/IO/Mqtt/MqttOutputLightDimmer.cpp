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
#include "MqttOutputLightDimmer.h"
#include "MqttBrokersList.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(MqttOutputLightDimmer)

MqttOutputLightDimmer::MqttOutputLightDimmer(Params &p):
    OutputLightDimmer(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("MqttOutputLightDimmer");
    ioDoc->descriptionSet(_("Control lights through mqtt broker"));
    MqttClient::commonDoc(ioDoc);

    client = MqttBrokersList::Instance().get_client(get_params());

    cInfoDom("output") << "MqttOutputLightDimmer::MqttOutputLightDimmer()";
}

MqttOutputLightDimmer::~MqttOutputLightDimmer()
{

}

void MqttOutputLightDimmer::readValue()
{
    bool err;
    value = client->getValueDouble(get_params(), err);
    if (!err)
        emitChange();
}

bool MqttOutputLightDimmer::set_value_real(int val)
{
    cDebugDom("mqtt") << "Set value to " << val;
    client->setValueInt(get_params(), val);
    return true;
}
