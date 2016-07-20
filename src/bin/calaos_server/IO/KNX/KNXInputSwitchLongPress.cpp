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
#include "KNXInputSwitchLongPress.h"
#include "IOFactory.h"
#include "KNXCtrl.h"

using namespace Calaos;

REGISTER_IO(KNXInputSwitchLongPress)

KNXInputSwitchLongPress::KNXInputSwitchLongPress(Params &p):
    InputSwitchLongPress(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("KNXInputSwitchLongPress");
    ioDoc->descriptionSet(_("Input switch long press with KNX and eibnetmux"));

    knxBase = new KNXBase(&param, ioDoc);

    /* No use for long press switch
    if (get_param("read_at_start") == "true")
        KNXCtrl::Instance(get_param("host"))->readValue(knxBase->getReadGroupAddr(), KNXValue::EIS_Switch_OnOff);
    */

    KNXCtrl::Instance(get_param("host"))->valueChanged.connect([=](const string group_addr, const KNXValue &)
    {
        if (group_addr == knxBase->getReadGroupAddr())
            hasChanged();
    });

    cInfoDom("input") << "knx_group: " << knxBase->getReadGroupAddr();
}

KNXInputSwitchLongPress::~KNXInputSwitchLongPress()
{
    delete knxBase;
}

bool KNXInputSwitchLongPress::readValue()
{
    KNXValue val = KNXCtrl::Instance(get_param("host"))->getValue(knxBase->getReadGroupAddr());

    return val.toBool();
}
