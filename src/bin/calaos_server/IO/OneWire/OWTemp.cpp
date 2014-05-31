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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <OWCtrl.h>
#include <OWTemp.h>
#include <IOFactory.h>


using namespace Calaos;

REGISTER_INPUT(OWTemp)

OWTemp::OWTemp(Params &p):
InputTemp(p)
{
    ow_id = get_param("ow_id");
    ow_args = get_param("ow_args");

    Owctrl::Instance(ow_args);
    cDebugDom("input") << get_param("id") << ": OW_ID : " << ow_id;

    //read value when calaos_server is started
    readValue();
    Calaos::StartReadRules::Instance().ioRead();
}

OWTemp::~OWTemp()
{
    cInfoDom("input");
}

void OWTemp::readValue()
{
    string res;
    double val;
    string ow_req;

    /* Read value */
    ow_req = ow_id + "/temperature";
    if (Owctrl::Instance(ow_args).getValue(ow_req, res))
    {
	from_string(res, val);
	cInfoDom("input") << ow_id << ": Ok";
    }
    else
    {
	cErrorDom("input") << "Cannot read One Wire Temperature Sensor (" << ow_id << ") : " << strerror(errno);
    }
}
