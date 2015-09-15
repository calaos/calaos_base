/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include "ZibaseDigitalIn.h"
#include "Zibase.h"
#include <IOFactory.h>

using namespace Calaos;

REGISTER_IO(ZibaseDigitalIn)

ZibaseDigitalIn::ZibaseDigitalIn(Params &p):
    InputSwitch(p),
    port(0)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("ZibaseDigitalIn");
    ioDoc->descriptionSet(_("Zibase digital input. This object acts as a switch"));
    ioDoc->paramAdd("host", _("Zibase IP address on the network"), IODoc::TYPE_STRING, true);
    ioDoc->paramAddInt("port", _("Zibase ethernet port, default to 17100"), 0, 65535, false, 17100);
    ioDoc->paramAdd("zibase_id", _("First Zibase device ID (ABC)"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("zibase_id2", _("Second Zibase device ID (ABC)"), IODoc::TYPE_STRING, true);

    Params devList = {{ "detect", _("Door/Window sensor") },
                      { "inter", _("Switch/Remote control") }};
    ioDoc->paramAddList("zibase_sensor", "Type of sensor", true, devList, "detect");

    std::string type = get_param("zibase_sensor");
    host = get_param("host");
    Utils::from_string(get_param("port"), port);
    id = get_param("zibase_id");
    id2 = get_param("zibase_id2");	

    if(type == "detect")
        sensor_type = ZibaseInfoSensor::eDETECT;
    if (type == "inter")
        sensor_type = ZibaseInfoSensor::eINTER;

    Zibase::Instance(host, port).sig_newframe.connect(sigc::mem_fun(*this, &ZibaseDigitalIn::valueUpdated));

    cDebugDom("input") << get_param("id");
}

ZibaseDigitalIn::~ZibaseDigitalIn()
{
    cDebugDom("input");
}

void ZibaseDigitalIn::valueUpdated(ZibaseInfoSensor *sensor)
{
    /*check that sensor id match */
    if(((id==sensor->id)||(id2==sensor->id)) && (sensor->type == sensor_type))
    {
        val = sensor->DigitalVal;
        hasChanged();
    }
}

bool ZibaseDigitalIn::readValue()
{
    return val;
}

