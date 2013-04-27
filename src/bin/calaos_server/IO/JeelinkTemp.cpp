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

#include <JeelinkTemp.h>
#include <ListeRule.h>
#include <IPC.h>

#include <Jeelink.h>
#include <base64.h>



using namespace Calaos;

JeelinkTemp::JeelinkTemp(Params &p):
                Input(p),
                value(0.0),
                timer(0.0)
{
        std::string tmp;

	TstJeelinkSensorInfo SensorInfo;

	Jeelink_prot = get_param("jeelink_prot");
        Jeelink_id = get_param("jeelink_id");
	Jeelink_chn = get_param("jeelink_chn");
        tmp = get_param("time");
        Jeelink_args = get_param("Jeelink_args");
        time = atof(tmp.c_str());

       //printf("Jeelink_ID : 0x%x, time : %3.3f\n", atoi(Jeelink_id.c_str()), time);
	//printf("Jeelink_Chn : 0x%x\n", atoi(Jeelink_chn.c_str()));

	unsigned int Id,Chn;
	sscanf(Jeelink_id.c_str(),"%04x",(unsigned int*)&Id);
	sscanf(Jeelink_chn.c_str(),"%04x",(unsigned int*)&Chn);

	printf("\n[JeelinkTemp] ID = 0x%x, Chn=0x%x",Id,Chn);

        if (!get_params().Exists("visible")) set_param("visible", "true");
        ListeRule::Instance().Add(this); //add this specific input to the EventLoop

	
	//tmp = get_param("id");

	value = 0;
	oldvalue = 0;
/*	if(strcmp(tmp.c_str(),"input_2") == 0)
	{       printf("Input ID is %s, init test to 0\n",tmp.c_str());
		value = 0;
	}else if(atoi(tmp.c_str()) == 3)
	{
		printf("Input ID is %s, init test to 30\n",tmp.c_str());
		value = 30;
	}
*/
	/* open jeelink */
         char * device = "/dev/ttyUSB1";
	 printf("\nJeelink:try to open %s",device); 
	if(strcmp(Jeelink_prot.c_str(),"OSV2")==0)
	{	
		SensorInfo.prot = OSV2;
		SensorInfo.SensorId = Id;
		SensorInfo.Chn = Chn; 
	}
	else if(strcmp(Jeelink_prot.c_str(),"OSV3")==0)
	{
		SensorInfo.prot = OSV3;
		SensorInfo.SensorId = Id;
		SensorInfo.Chn = Chn; 
	}
	handle = openJeelink(device,&SensorInfo);
	if(handle<0)
		printf("\n[JeelinkTemp]Error Opening Jeelink");
	






}

JeelinkTemp::~JeelinkTemp()
{
	
        Utils::logger("input") << Priority::INFO << "JeelinkTemp::~JeelinkTemp(): Ok" << log4cpp::eol;

	


}

void JeelinkTemp::hasChanged()
{

Utils::logger("input") << Priority::INFO << "JeelinkTemp::Has Changedk" << log4cpp::eol;

//if(test++ > 500)
/*if(test > 500)
	{
		string sig = "input ";
		EmitSignalInput();
                sig += get_param("id") + " ";
                sig += Utils::url_encode(string("state:") + to_string(get_value_double()));
                IPC::Instance().SendEvent("events", sig);
		test = 0;
		value += 1.0;
		printf("JeelinkTemp, Has Changed : temp=%3.3f; IPC is %s\n", value,sig.c_str() );
	}
*/

	
	if(handle>=0)
	{
		unsigned short size = 32;
		unsigned char buf[32];
		
		if(readJeelink(handle,&size,buf)==0)
		{
			//extractTemp(buf);
			
			if(strcmp(Jeelink_prot.c_str(),"OSV2")==0)
			{
				int sign = (buf[6]&0x8) ? -1 : 1;
	
				float temp = ((buf[5]&0xF0) >> 4)*10 + (buf[5]&0xF) + (float)(((buf[4]&0xF0) >> 4) / 10.0);
				int hum = (buf[7]&0xF) * 10 + ((buf[6]&0xF0) >> 4);

				oldvalue = value;
				value = temp*sign;
			}
			else if(strcmp(Jeelink_prot.c_str(),"OSV3")==0)
			{
				int conso = ((buf[4])<<8) + (buf[3]);
				oldvalue = value;
				value = conso;
			}
			else printf("\n[JeelinkTemp]no matching protocol");
			



						


                        if(oldvalue != value)
			{	string sig = "input ";
				EmitSignalInput();
				sig += get_param("id") + " ";
				sig += Utils::url_encode(string("state:") + to_string(get_value_double()));
				IPC::Instance().SendEvent("events", sig);
				printf("JeelinkTemp, Has Changed : Value=%3.3f;\n", value );
			}
		}

	}
}

double JeelinkTemp::get_value_double()
{
        return value;
}

void JeelinkTemp::force_input_double(double v)
{
	printf("JeelinkTemp, Force Input\n");
        value = v;
        EmitSignalInput();

        string sig = "input ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode(string("state:") + to_string(get_value_double()));
        IPC::Instance().SendEvent("events", sig);
}


