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
#include "KNXOutputLightDimmer.h"
#include "IOFactory.h"
#include "KNXCtrl.h"

namespace Calaos {

REGISTER_IO(KNXOutputLightDimmer)

KNXOutputLightDimmer::KNXOutputLightDimmer(Params &p):
    OutputLightDimmer(p)
{
    useRealState = true;

    // Define IO documentation
    ioDoc->friendlyNameSet("KNXOutputLightDimmer");
    ioDoc->descriptionSet(_("Light dimmer with KNX and eibnetmux"));
    ioDoc->linkAdd("eibnetmux", _("http://eibnetmux.sourceforge.net"));
    ioDoc->paramAdd("knx_group", _("KNX Group address, Ex: x/y/z"), IODoc::TYPE_STRING, true);

    std::string knx_group = get_param("knx_group");

    //KNXCtrl::Instance(get_param("host"))->readValue(knx_group, KNXValue::EIS_Switch_OnOff);

    KNXCtrl::Instance(get_param("host"))->valueChanged.connect([=](const std::string group_addr, const KNXValue &v)
    {
        if (group_addr != get_param("knx_group")) return;
        KNXValue val = v;
        val.setEis(KNXValue::EIS_Dim_UpDown);
        value = val.toInt();
        EmitSignalIO();
        emitChange();
    });

    cInfoDom("input") << "knx_group: " << knx_group;
}

KNXOutputLightDimmer::~KNXOutputLightDimmer()
{
}

bool KNXOutputLightDimmer::set_value_real(int val)
{
    KNXValue kval = KNXValue::fromInt(val, KNXValue::EIS_Dim_UpDown);

    std::string knx_group = get_param("knx_group");
    KNXCtrl::Instance(get_param("host"))->writeValue(knx_group, kval);

    return true;
}

}
