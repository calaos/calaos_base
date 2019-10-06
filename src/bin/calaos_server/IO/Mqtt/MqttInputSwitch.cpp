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
#include "MqttInputSwitch.h"
#include "MqttBrokersList.h"
#include "WebCtrl.h"
#include "jansson.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(MqttInputSwitch)

MqttInputSwitch::MqttInputSwitch(Params &p):
    InputSwitch(p)
{

    ioDoc->friendlyNameSet("MqttInputSwitch");
    ioDoc->descriptionSet(_("Switch value readed from a mqtt broker"));
    MqttClient::commonDoc(ioDoc);

    ioDoc->paramAdd("on_value", _("Value to interpret as ON value"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("off_value", _("Value to interpret as OFF value"), IODoc::TYPE_STRING, true);

    cInfoDom("input") << "MqttInputSwitch::MqttInputSwitch()";
    Calaos::StartReadRules::Instance().addIO();

    client = MqttBrokersList::Instance().get_client(get_params());
    client->subscribeTopic(get_param("topic"), [=]()
    {
        hasChanged();
    });

}

MqttInputSwitch::~MqttInputSwitch()
{

}

bool MqttInputSwitch::readValue()
{
    string sv = client->getValue(get_params());

    cDebugDom("mqtt") << "Read value " << sv;

    if (sv == "single")
        return true;
    else if (sv == "")
        return false;

    return false;
}
