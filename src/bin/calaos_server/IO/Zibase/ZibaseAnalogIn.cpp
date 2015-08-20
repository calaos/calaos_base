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
#include "ZibaseAnalogIn.h"
#include "Zibase.h"
#include <IOFactory.h>

using namespace Calaos;

REGISTER_IO(ZibaseAnalogIn);

ZibaseAnalogIn::ZibaseAnalogIn(Params &p):
    InputAnalog(p),
    port(0)
{
    std::string type = get_param("zibase_sensor");

    host = get_param("host");
    Utils::from_string(get_param("port"), port);
    id = get_param("zibase_id");

    if(type.compare("energy")==0)
        sensor_type = ZibaseInfoSensor::eENERGY;
    else if(type.compare("lux")==0)
        sensor_type = ZibaseInfoSensor::eLUX;
    else if(type.compare("t_rain")==0)
        sensor_type = ZibaseInfoSensor::eTOTALRAIN;
    else if(type.compare("wind")==0)
        sensor_type = ZibaseInfoSensor::eWIND;

    Zibase::Instance(host, port).sig_newframe.connect(sigc::mem_fun(*this, &ZibaseAnalogIn::valueUpdated));

    cDebugDom("input") << get_param("id");
}

ZibaseAnalogIn::~ZibaseAnalogIn()
{
    cDebugDom("input");
}

void ZibaseAnalogIn::valueUpdated(ZibaseInfoSensor *sensor)
{

    /*check that sensor id match */
    if((id==sensor->id) && (sensor->type == sensor_type))
    {
        if (sensor->AnalogVal != value)
        {
            value = sensor->AnalogVal;
            emitChange();
        }

    }

}

void ZibaseAnalogIn::readValue()
{
    //don't do anything here, as we can't query the zibase sensor for a value,
    //the zibase will send us new values
}

