/******************************************************************************
**  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
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

using namespace Calaos;

ZibaseDigitalIn::ZibaseDigitalIn(Params &p):
                InputSwitch(p),
                port(0)
{
        std::string type = get_param("zibase_sensor");
        host = get_param("host");
        Utils::from_string(get_param("port"), port);
        id = get_param("zibase_id");
        if(type.compare("detect")==0)
                sensor_type = ZibaseInfoSensor::eDETECT;   

        Zibase::Instance(host, port).sig_newframe.connect(sigc::mem_fun(*this, &ZibaseDigitalIn::valueUpdated));

        cDebugDom("input") << "ZibaseDigitalIn::ZibaseDigitalIn(" << get_param("id") << "): Ok";
}

ZibaseDigitalIn::~ZibaseDigitalIn()
{
        cDebugDom("input") << "ZibaseDigitalIn::~ZibaseDigitalIn(): Ok";
}

void ZibaseDigitalIn::valueUpdated(ZibaseInfoSensor *sensor)
{
        /*check that sensor id match */       
        if((id==sensor->id) && (sensor->type == sensor_type))
        {                                       
                val = sensor->DigitalVal;                                               
                hasChanged();                                           
        }
}

bool ZibaseDigitalIn::readValue()
{
        return val;
}

