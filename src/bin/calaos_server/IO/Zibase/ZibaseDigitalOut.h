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
#ifndef S_ZibaseDigitalOut_H
#define S_ZibaseDigitalOut_H

#include <OutputLight.h>
#include "Zibase.h"

class ZibaseInfoSensor;

namespace Calaos
{

class ZibaseDigitalOut : public OutputLight
{
    protected:
        std::string host;
        int port;
        std::string id;
        int nbburst;
        int protocol;

        void valueUpdated(ZibaseInfoSensor *sensor);
        ZibaseInfoSensor::eZibaseSensor sensor_type;

        ZibaseInfoProtocol *prot;

        bool set_value_real(bool val);

    public:
        ZibaseDigitalOut(Params &p);
        virtual ~ZibaseDigitalOut();
};

}
#endif
