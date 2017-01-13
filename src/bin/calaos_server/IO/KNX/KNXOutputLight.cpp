/******************************************************************************
 **  Copyright (c) 2007-2015, Calaos. All Rights Reserved.
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
#include "KNXOutputLight.h"
#include "IOFactory.h"
#include "KNXCtrl.h"

using namespace Calaos;

REGISTER_IO(KNXOutputLight)

KNXOutputLight::KNXOutputLight(Params &p):
    OutputLight(p)
{
    useRealState = true;

    // Define IO documentation
    ioDoc->friendlyNameSet("KNXOutputLight");
    ioDoc->descriptionSet(_("Light output with KNX and eibnetmux"));

    knxBase = new KNXBase(&param, ioDoc);

    if (get_param("read_at_start") == "true")
    {
        Timer::singleShot(1.5, [=]()
        {
            KNXCtrl::Instance(get_param("host"))->readValue(knxBase->getReadGroupAddr(), KNXValue::EIS_Switch_OnOff);
        });
    }

    KNXCtrl::Instance(get_param("host"))->valueChanged.connect([=](const string group_addr, const KNXValue &val)
    {
        if (group_addr != knxBase->getReadGroupAddr()) return;
        if (val.toBool())
            set_value(string("set_state true"));
        else
            set_value(string("set_state false"));
    });

    cInfoDom("input") << "knx_group: " << knxBase->getReadGroupAddr();
}

KNXOutputLight::~KNXOutputLight()
{
    delete knxBase;
}

bool KNXOutputLight::set_value_real(bool val)
{
    KNXValue kval = KNXValue::fromInt(val?1:0, 1);

    string knx_group = get_param("knx_group");
    KNXCtrl::Instance(get_param("host"))->writeValue(knx_group, kval);

    return true;
}
