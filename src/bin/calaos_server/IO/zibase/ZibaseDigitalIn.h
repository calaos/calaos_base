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
#ifndef S_zibaseDigI_H
#define S_zibaseDigI_H

//#include <Calaos.h>
#include <InputSwitch.h>
//#include <Ecore.h>

#include <zibase.h>

namespace Calaos
{

class ZibaseDigitalIn : public InputSwitch,public sigc::trackable
{
        protected:
	       type_signal_zibase::iterator iter;
               std::string Zibase_id;
		std::string Zibase_ip;
		std::string Zibase_sensortype;
		std::string Zibase_name;
                
		
		zibase* handle;
		bool val;
		int count;
		
		TstZibaseInfoSensor SensorInfo;
		
		virtual bool readValue();
		
        public:
                ZibaseDigitalIn(Params &p);
                ~ZibaseDigitalIn();

		virtual void ReceiveDigitalZibase(std::string id,bool data);
		

               
		
};

}
#endif
