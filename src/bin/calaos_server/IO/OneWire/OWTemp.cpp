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
#include <thread>

using namespace Calaos;

REGISTER_INPUT(OWTemp)

void *_ow_thread_callback(void *data)
{
    OWData *odata = reinterpret_cast<OWData *>(data);
    odata->owTemp->readValue_cb(odata->ret, odata->result);
    delete odata;

    return NULL;
}

OWTemp::OWTemp(Params &p):
    InputTemp(p),
    start(true)
{
    ow_id = get_param("ow_id");
    ow_args = get_param("ow_args");

    Owctrl::Instance(ow_args);
    cDebugDom("input") << get_param("id") << ": OW_ID : " << ow_id;

    Calaos::StartReadRules::Instance().addIO();
    EcoreTimer::singleShot(0.1, [=] {
        readValue();
    });
}

OWTemp::~OWTemp()
{
    cInfoDom("input");
}

void OWTemp::readValue()
{
    string ow_req = ow_id + "/temperature";

    /* Read value in a thread */
    std::thread([=]()
    {
        cDebugDom("input") << "Read OW value in thread(" << std::this_thread::get_id() << ")";
        string res;
        bool ret = Owctrl::Instance(ow_args).getValue(ow_req, res);

        OWData *data = new OWData;
        data->owTemp = this;
        data->ret = ret;
        data->result = res;
        ecore_main_loop_thread_safe_call_sync(_ow_thread_callback, data);
    }).detach();
}

void OWTemp::readValue_cb(bool ret, string res)
{
    double val;

    if (ret)
    {
        if (start)
        {
            Calaos::StartReadRules::Instance().ioRead();
            start = false;
        }
        from_string(res, val);
        val = roundValue(val);
        if (val != value)
        {
            value = val;
            emitChange();
            cInfoDom("input") << ow_id << ": Ok value :  " << val;
        }
    }
    else
    {
        cErrorDom("input") << "Cannot read One Wire Temperature Sensor (" << ow_id << ") : " << strerror(errno);
    }
}
