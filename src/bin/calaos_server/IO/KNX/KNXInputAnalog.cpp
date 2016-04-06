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
#include "KNXInputAnalog.h"
#include "IOFactory.h"
#include "KNXCtrl.h"

namespace Calaos {

REGISTER_IO(KNXInputAnalog)

KNXInputAnalog::KNXInputAnalog(Params &p):
    InputAnalog(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("KNXInputAnalog");
    ioDoc->descriptionSet(_("Input analog with KNX and eibnetmux"));
    ioDoc->linkAdd("eibnetmux", _("http://eibnetmux.sourceforge.net"));
    ioDoc->paramAdd("knx_group", _("KNX Group address, Ex: x/y/z"), IODoc::TYPE_STRING, true);
    ioDoc->paramAddInt("eis", _("KNX EIS (Data type)"), 0, 15, false, KNXValue::EIS_Value_Int);

    std::string knx_group = get_param("knx_group");

    //KNXCtrl::Instance(get_param("host"))->readValue(knx_group, );

    KNXCtrl::Instance(get_param("host"))->valueChanged.connect([=](const std::string group_addr, const KNXValue &)
    {
        if (group_addr != get_param("knx_group")) return;
        readValue();
    });

    cInfoDom("input") << "knx_group: " << knx_group;
}

KNXInputAnalog::~KNXInputAnalog()
{
}

void KNXInputAnalog::readValue()
{
    std::string knx_group = get_param("knx_group");
    int eis;
    Utils::from_string(get_param("eis"), eis);

    KNXValue val = KNXCtrl::Instance(get_param("host"))->getValue(knx_group);
    val.setEis(eis);

    if (value != val.toInt())
    {
        value = val.toInt();
        emitChange();
    }
}

}
