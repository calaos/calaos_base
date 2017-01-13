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
#include "KNXOutputLightRGB.h"
#include "IOFactory.h"
#include "KNXCtrl.h"

using namespace Calaos;

REGISTER_IO(KNXOutputLightRGB)

KNXOutputLightRGB::KNXOutputLightRGB(Params &p):
    OutputLightRGB(p)
{
    useRealState = false; //TODO: i do not have access to a RGB knx device. So i can't test!

    // Define IO documentation
    ioDoc->friendlyNameSet("KNXOutputLightRGB");
    ioDoc->descriptionSet(_("Light RGB with KNX and eibnetmux"));
    ioDoc->paramAdd("knx_group_red", _("Red channel KNX Group address, Ex: x/y/z"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("knx_group_green", _("Green channel KNX Group address, Ex: x/y/z"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("knx_group_blue", _("Blue channel KNX Group address, Ex: x/y/z"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("listen_knx_group_red", _("Red Group address for listening status, Ex: x/y/z"), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("listen_knx_group_green", _("Green Group address for listening status, Ex: x/y/z"), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("listen_knx_group_blue", _("Blue Group address for listening status, Ex: x/y/z"), IODoc::TYPE_STRING, false);

    knxBase = new KNXBase(&param, ioDoc, false);

    if (get_param("read_at_start") == "true")
    {
        Timer::singleShot(1.5, [=]()
        {
            KNXCtrl::Instance(get_param("host"))->readValue(knxBase->getReadGroupAddr("knx_group_red"));
            KNXCtrl::Instance(get_param("host"))->readValue(knxBase->getReadGroupAddr("knx_group_green"));
            KNXCtrl::Instance(get_param("host"))->readValue(knxBase->getReadGroupAddr("knx_group_blue"));
        });
    }

    KNXCtrl::Instance(get_param("host"))->valueChanged.connect([=](const string group_addr, const KNXValue &v)
    {
        if (group_addr != knxBase->getReadGroupAddr("knx_group_red") &&
            group_addr != knxBase->getReadGroupAddr("knx_group_green") &&
            group_addr != knxBase->getReadGroupAddr("knx_group_blue"))
            return;

        //TODO
//        KNXValue val = v;
//        val.setEis(KNXValue::EIS_Dim_UpDown);
//        value = val.toInt();
//        EmitSignalIO();
//        emitChange();
    });
}

KNXOutputLightRGB::~KNXOutputLightRGB()
{
    delete knxBase;
}

void KNXOutputLightRGB::setColorReal(const ColorValue &c, bool s)
{
    string knx_group_red = get_param("knx_group_red");
    string knx_group_green = get_param("knx_group_green");
    string knx_group_blue = get_param("knx_group_blue");

    int r = 0, g = 0, b = 0;
    if (s)
    {
        r = c.getRed();
        g = c.getGreen();
        b = c.getBlue();
    }

    KNXValue kval;
    kval = KNXValue::fromInt(r, KNXValue::EIS_Dim_UpDown);
    KNXCtrl::Instance(get_param("host"))->writeValue(knx_group_red, kval);
    kval = KNXValue::fromInt(g, KNXValue::EIS_Dim_UpDown);
    KNXCtrl::Instance(get_param("host"))->writeValue(knx_group_green, kval);
    kval = KNXValue::fromInt(b, KNXValue::EIS_Dim_UpDown);
    KNXCtrl::Instance(get_param("host"))->writeValue(knx_group_blue, kval);
}
