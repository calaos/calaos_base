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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include <ListeRule.h>
#include <IPC.h>

#include <ZibaseAnalogIn.h>
#include <base64.h>



using namespace Calaos;

ZibaseAnalogIn::ZibaseAnalogIn(Params &p):
                InputAnalog(p)
                
{
        std::string tmp;

        Zibase_id = get_param("Zibase_id");
	Zibase_ip = get_param("host");
	Zibase_sensortype = get_param("Zibase_sensor");
	Zibase_name = get_param("name");
	value = 0;
        

	//printf("Zibase_IP : %s\n", Zibase_ip.c_str());
        //printf("Zibase_ID : %s\n", Zibase_id.c_str());
	//printf("Zibase_Sensor : %s\n", Zibase_sensortype.c_str());
	
        if (!get_params().Exists("visible")) set_param("visible", "true");
        ListeRule::Instance().Add(this); //add this specific input to the EventLoop
	value = 0;
	oldvalue = 0;

	/* open Zibase */
	strcpy(SensorInfo.addip,Zibase_ip.c_str());
	strcpy(SensorInfo.id,Zibase_id.c_str());
	strcpy(SensorInfo.label,Zibase_name.c_str());

	/* get port */
	Utils::from_string(get_param("port"),SensorInfo.port); 
	/* reset value */	
	SensorInfo.Analog = 0;
	/* determine sensor type */
	if(strcmp("temp",Zibase_sensortype.c_str())==0)
	{
		SensorInfo.type = eTEMP;
	}else if(strcmp("energy",Zibase_sensortype.c_str())==0)
	{
		SensorInfo.type = eENERGY;	
	}
        

	handle = new zibase(&SensorInfo);
	if(handle==0)
	{	
		Utils::logger("input") << Priority::INFO << "Error Opening Zibase device" << log4cpp::eol;
	}
}

ZibaseAnalogIn::~ZibaseAnalogIn()
{	
        Utils::logger("input") << Priority::INFO << "zibase::~zibase(): Ok" << log4cpp::eol;
}


void ZibaseAnalogIn::readValue()
{
      	double val;  

	float val;
	if(handle>=0)
	{	
		if(readZibaseDev(handle,SensorInfo.type,&val)==0)
		{
			oldvalue = value;
			value = val;
			
                       if(oldvalue != value)
			{	string sig = "input ";
				EmitSignalInput();
				sig += get_param("id") + " ";
				sig += Utils::url_encode(string("state:") + to_string(get_value_double()));
				IPC::Instance().SendEvent("events", sig);
				//Utils::logger("input") << Priority::INFO << "zibase::Has Changedk" << log4cpp::eol;
			}
		}

	//printf("\n ZibaseAnalog Read Value %3.3f",val);
	if (val != value)
        {
                value = val;
                emitChange();
        }

      
        
}



