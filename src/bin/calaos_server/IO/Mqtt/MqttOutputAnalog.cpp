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
    MqttClient::commonDoc(ioDoc);

    client = MqttBrokersList::Instance().get_client(get_params());

    cInfoDom("output") << "MqttOutputAnalog::MqttOutputAnalog()";
}

MqttOutputAnalog::~MqttOutputAnalog()
{
}

void MqttOutputAnalog::readValue()
{
}

void MqttOutputAnalog::set_value_real(double val)
{
    client->setValueString(get_params(), Utils::to_string(val));
}
