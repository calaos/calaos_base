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

#include <OWTemp.h>

#ifdef HAVE_OWCAPI_H
#include <owcapi.h>
#endif

using namespace Calaos;

OWTemp::OWTemp(Params &p):
                InputTemp(p)
{
        ow_id = get_param("ow_id");
        ow_args = get_param("ow_args");

#ifdef HAVE_OWCAPI_H
        OW_init(ow_args.c_str());
#endif

        cDebugDom("input") << "OWTemp(" << get_param("id") << "): OW_ID : " << ow_id;

        //read value when calaos_server is started
        readValue();
        Calaos::StartReadRules::Instance().ioRead();
}

OWTemp::~OWTemp()
{
        cInfoDom("input") << "OWTemp::~OWTemp(): Ok";

#ifdef HAVE_OWCAPI_H
        OW_finish();
#endif
}

void OWTemp::readValue()
{
        /* TODO: should be rewritten in async using a thread for all OneWire inputs.
         * Like in WagoMap.
         */

        ow_id = get_param("ow_id");

#ifdef HAVE_OWCAPI_H
        char *res;
        size_t len;
        double val;

        /* Read value */
        ow_req = ow_id + "/temperature";
        if(OW_get(ow_req.c_str(), &res, &len) >= 0)
        {
                val = atof(res);
                free(res);
                cInfoDom("input") << "OWTemp::OWTemp(" << get_param("id") << "): Ok";
        }
        else
        {
                cInfoDom("input") << "OWTemp::OWTemp(" << get_param("id") << "): Cannot read One Wire Temperature Sensor (" << ow_id << ")";
        }

        if (val != value)
        {
                value = val;
                emitChange();
        }
#else
        cInfoDom("input") << "OWTemp::OWTemp(" << get_param("id") << "): One Wire support not enabled !";
#endif
}
