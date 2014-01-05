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
#ifndef S_ZIBASEDEV_H
#define S_ZIBASEDEV_H



/**********************************/
/********    DEFINE       *********/
/**********************************/
/** @brief define the default udp port used for zibase communication */
#define ZIBASE_UDP_PORT 49999
/** @brief define the nb sensor max we can declare */
#define NBSENSORMAX 32


/**********************************/
/********    TYPEDEFS     *********/
/**********************************/
/** @brief Enum definition for sensor type */
typedef enum{
/** @brief temperature sensor) */
	eTEMP,
/** @brief energy monitor sensor) */
	eENERGY,
/** @brief detector sensor) */
	eDETECT
}eZibaseSensor;

/** @brief Structure describing sensor information on zibase link */
typedef struct{
	/** @brief ip address of the zibase on which the sensor is plugged */
	char addip[32];
	/** @brief local port to be used for receiving zibase frame */
	unsigned long port;
	/** @brief id of the sensor (should be seen on zibase device activity */
	char id[64];
	/** @brief label of the sensor (ex: room first floor) */
	char label[64];
	/** @brief sensor type */
	eZibaseSensor type;
	/** @brief analog value (use for eTEMP, eEnergy sensor type) */
	float Analog;
	/** @brief digital value (use for eDETECT sensor type) */
	bool Digital;
}TstZibaseInfoSensor;


/**********************************/
/********    CLASS        *********/
/**********************************/
class zibase
{
        protected:
		TstZibaseInfoSensor InfoSensor;
                
              
        public:
                 zibase(TstZibaseInfoSensor *p);	 
                 ~zibase(); 
 
		int zibase_getAnalog(double * val);
		int zibase_getDigital(bool * val);       
};




#endif
