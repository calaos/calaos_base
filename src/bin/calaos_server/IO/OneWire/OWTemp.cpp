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

#include <OWTemp.h>
#include <IOFactory.h>

#ifdef HAVE_OWCAPI_H
#include <owcapi.h>
#endif

using namespace Calaos;

REGISTER_INPUT(OWTemp)

OWTemp::OWTemp(Params &p):
    InputTemp(p)
{
    ow_id = get_param("ow_id");
    ow_args = get_param("ow_args");

    retry = 0;
    cDebugDom("input") << get_param("id") << ": OW_ID : " << ow_id;

    //read value when calaos_server is started
    readValue();
    Calaos::StartReadRules::Instance().ioRead();
}

OWTemp::~OWTemp()
{
    cInfoDom("input");
}

void OWTemp::readValueReal()
{
    #ifdef HAVE_OWCAPI_H
    char *res;
    size_t len;
    double val;
    std::string ow_req;

    /* Read value */
    if ( OW_init(ow_args.c_str()) != 0)
    {
        cError() << "Init errror OWFS : %s" << strerror(errno);
    }
    else
    {
        ow_req = ow_id + "/temperature";
        if(OW_get(ow_req.c_str(), &res, &len) >= 0)
        {
            val = atof(res);
            free(res);
            cInfoDom("input") << get_param("id") << ": Ok";
        }
        else
        {
            cInfoDom("input") << get_param("id") << ": Cannot read One Wire Temperature Sensor (" << ow_id << ")";
            val = 0;
        }

        if (val != value)
        {
            value = val;
            emitChange();
        }
        OW_finish();
        retry = 0;
        delete retry_timer;
        retry_timer = nullptr;
    }
    #endif
}

void OWTemp::readValue()
{
    /* TODO: should be rewritten in async using a thread for all OneWire inputs.
         * Like in WagoMap.
         */

    if (retry != 0)
        return;

#ifdef HAVE_OWCAPI_H    

    if ( OW_init(ow_args.c_str()) != 0)
    {
        cError() << "Init errror OWFS : %s" << strerror(errno);
        retry = 3;
        retry_timer = new EcoreTimer(1.0, [=]() {
            retry--;
            if (retry != 0)
                readValueReal();
            else
            {
                delete retry_timer;
                retry_timer = nullptr;
            }
        });

    }
    else
    {
        readValueReal();
        retry = 0;

    }
#else
    cInfoDom("input") << get_param("id") << ": One Wire support not enabled !";
#endif
}
