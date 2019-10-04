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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ListeRule.h"
#include "MqttInputAnalog.h"
#include "MqttBrokersList.h"
#include "WebCtrl.h"
#include "jansson.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(MqttInputAnalog)

MqttInputAnalog::MqttInputAnalog(Params &p):
    InputAnalog(p)
{

    ioDoc->friendlyNameSet("MqttInputAnalog");
    ioDoc->descriptionSet(_("Temperature read from a mqtt broker"));

    cInfoDom("input") << "MqttInputAnalog::MqttInputAnalog()";
    Calaos::StartReadRules::Instance().addIO();

    client = MqttBrokersList::Instance().get_client(get_params());
    client->subscribeTopic(get_param("topic"), [=]()
    {
        cDebugDom("mqtt") << "Read Value";
        readValue();
    });

}

MqttInputAnalog::~MqttInputAnalog()
{

}

void MqttInputAnalog::readValue()
{
    bool err;
    value = client->getValueDouble(get_params(), err);
    if (!err)
        emitChange();
}
