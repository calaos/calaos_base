/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#include "KNXOutputShutter.h"
#include "IOFactory.h"
#include "KNXCtrl.h"

using namespace Calaos;

REGISTER_IO(KNXOutputShutter)

KNXOutputShutter::KNXOutputShutter(Params &p):
    OutputShutter(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("KNXOutputShutter");
    ioDoc->descriptionSet(_("Shutter with with KNX and eibnetmux"));
    ioDoc->paramAdd("knx_group_up", _("Up KNX Group address, Ex: x/y/z"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("knx_group_down", _("Down KNX Group address, Ex: x/y/z"), IODoc::TYPE_STRING, true);

    knxBase = new KNXBase(&param, ioDoc, false);
}

KNXOutputShutter::~KNXOutputShutter()
{
    delete knxBase;
}

void KNXOutputShutter::setOutputUp(bool enable)
{
    KNXValue kval = KNXValue::fromInt(enable?1:0, 1);

    string knx_group = get_param("knx_group_up");
    KNXCtrl::Instance(get_param("host"))->writeValue(knx_group, kval);
}

void KNXOutputShutter::setOutputDown(bool enable)
{
    KNXValue kval = KNXValue::fromInt(enable?1:0, 1);

    string knx_group = get_param("knx_group_down");
    KNXCtrl::Instance(get_param("host"))->writeValue(knx_group, kval);
}
