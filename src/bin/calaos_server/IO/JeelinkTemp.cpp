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



using namespace Calaos;

JeelinkTemp::JeelinkTemp(Params &p):
                Input(p),
                value(0.0),
                timer(0.0)
{
        std::string tmp;

        Jeelink_id = get_param("jeelink_id");
        tmp = get_param("time");
        Jeelink_args = get_param("Jeelink_args");
        time = atof(tmp.c_str());

        printf("Jeelink_ID : %s, time : %3.3f\n", Jeelink_id.c_str(), time);

        if (!get_params().Exists("visible")) set_param("visible", "true");
        ListeRule::Instance().Add(this); //add this specific input to the EventLoop

	value = 5.0;




}

JeelinkTemp::~JeelinkTemp()
{
	
        Utils::logger("input") << Priority::INFO << "JeelinkTemp::~JeelinkTemp(): Ok" << log4cpp::eol;

	


}

void JeelinkTemp::hasChanged()
{
static int Test = 0;
Utils::logger("input") << Priority::INFO << "JeelinkTemp::Has Changedk" << log4cpp::eol;

if(Test++ > 500)
	{
		string sig = "input ";
		EmitSignalInput();
                sig += get_param("id") + " ";
                sig += Utils::url_encode(string("state:") + to_string(get_value_double()));
                IPC::Instance().SendEvent("events", sig);
		Test = 0;
		value += 1.0;
		printf("JeelinkTemp, Has Changed : temp=%3.3f\n", value);
	}
}

double JeelinkTemp::get_value_double()
{
        return value;
}

void JeelinkTemp::force_input_double(double v)
{
        value = v;
        EmitSignalInput();

        string sig = "input ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode(string("state:") + to_string(get_value_double()));
        IPC::Instance().SendEvent("events", sig);
}
