/******************************************************************************
 **  Copyright (c) 2006-2015, Calaos. All Rights Reserved.
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
#include "KNXInputTemp.h"
#include "IOFactory.h"
#include "KNXCtrl.h"

using namespace Calaos;

REGISTER_IO(KNXInputTemp)

KNXInputTemp::KNXInputTemp(Params &p):
    InputTemp(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("KNXInputTemp");
    ioDoc->descriptionSet(_("Input temperature with KNX and eibnetmux"));

    knxBase = new KNXBase(&param, ioDoc);

    if (get_param("read_at_start") == "true")
    {
        Timer::singleShot(1.5, [=]()
        {
            KNXCtrl::Instance(get_param("host"))->readValue(knxBase->getReadGroupAddr());
        });
    }

    KNXCtrl::Instance(get_param("host"))->valueChanged.connect([=](const string group_addr, const KNXValue &)
    {
        if (group_addr == knxBase->getReadGroupAddr())
            readValue();
    });

    cInfoDom("input") << "knx_group: " << knxBase->getReadGroupAddr();
}

KNXInputTemp::~KNXInputTemp()
{
    delete knxBase;
}

void KNXInputTemp::readValue()
{
    int eis;
    Utils::from_string(get_param("eis"), eis);

    KNXValue val = KNXCtrl::Instance(get_param("host"))->getValue(knxBase->getReadGroupAddr());
    val.setEis(eis);

    if (value != val.toInt())
    {
        value = val.toInt();
        emitChange();
    }
}
