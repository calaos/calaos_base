/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
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
#include <ZibaseDigitalIn.h>
#include <ListeRule.h>
#include <IPC.h>

using namespace Calaos;

ZibaseDigitalIn::ZibaseDigitalIn(Params &p):
                InputSwitch(p)
		   
{
        

	value = 0;
	
	

        std::string tmp;

        Zibase_id = get_param("Zibase_id");
	Zibase_ip = get_param("host");
	Zibase_sensortype = get_param("Zibase_sensor");
	Zibase_name = get_param("name");
	value = 0;
        

	
        if (!get_params().Exists("visible")) set_param("visible", "true");
       

	/* open Zibase */
	strcpy(SensorInfo.addip,Zibase_ip.c_str());
	strcpy(SensorInfo.id,Zibase_id.c_str());
	strcpy(SensorInfo.label,Zibase_name.c_str());

	/* get port */
	Utils::from_string(get_param("port"),SensorInfo.port); 
	/* reset value */	
	SensorInfo.Digital = 0;
	/* set sensor type */
	SensorInfo.type = eDETECT;	
	
        

	handle = new zibase(&SensorInfo);
	if(handle==0)
	{	
		Utils::logger("input") << Priority::INFO << "Error Opening Zibase device" << log4cpp::eol;
	}
	else
	{	
		// for polling ListeRule::Instance().Add(this);
		iter = Utils::signal_zibase.connect( sigc::mem_fun(this, &ZibaseDigitalIn::ReceiveDigitalZibase) );
	}	



}

ZibaseDigitalIn::~ZibaseDigitalIn()
{
        iter->disconnect();
        Utils::logger("input") << Priority::DEBUG << "ZibaseDigitalIn::~ZibaseDigitalIn(): Ok" << log4cpp::eol;
		
}

void ZibaseDigitalIn::ReceiveDigitalZibase(std::string id,bool data)
{  
			val = data;
                        hasChanged();              
}

bool ZibaseDigitalIn::readValue()
{
        return val;
}

