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
        host = get_param("host");
        Utils::from_string(get_param("port"), port);

        Zibase::Instance(host, port).sig_newframe.connect(sigc::mem_fun(*this, &ZibaseDigitalIn::valueUpdated));

        Utils::logger("input") << Priority::DEBUG << "ZibaseDigitalIn::ZibaseDigitalIn(" << get_param("id") << "): Ok" << log4cpp::eol;
}

ZibaseDigitalIn::~ZibaseDigitalIn()
{
        Utils::logger("input") << Priority::DEBUG << "ZibaseDigitalIn::~ZibaseDigitalIn(): Ok" << log4cpp::eol;
}

void ZibaseDigitalIn::valueUpdated(ZibaseInfoSensor *sensor)
{
/*
        if (new_value != value)
        {
                value = new_value;
                emitChange();
        }
*/
}

bool ZibaseDigitalIn::readValue()
{
        return value;
}

