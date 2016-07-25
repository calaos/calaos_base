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
#include <WIAnalog.h>
#include <WagoMap.h>
#include <IOFactory.h>

using namespace Calaos;

REGISTER_IO(WIAnalog)
REGISTER_IO_USERTYPE(WagoInputAnalog, WIAnalog)

WIAnalog::WIAnalog(Params &p):
    InputAnalog(p),
    port(502),
    start(true)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("WIAnalog");
    ioDoc->aliasAdd("WagoInputAnalog");
    ioDoc->descriptionSet(_("Analog measurement with Wago module (like 0-10V, 4-20mA, ...)"));
    ioDoc->linkAdd("Calaos Wiki", _("http://calaos.fr/wiki/fr/entree_analog"));
    ioDoc->paramAdd("host", _("Wago PLC IP address on the network"), IODoc::TYPE_STRING, true);
    ioDoc->paramAddInt("port", _("Wago ethernet port, default to 502"),
                    0, 65535, false, 502);

    ioDoc->paramAddInt("var", _("PLC address of the input sensor"), 0, 65535, true);

    host = get_param("host");
    if (get_params().Exists("port"))
        Utils::from_string(get_param("port"), port);

    WagoMap::Instance(host, port);

    Utils::from_string(get_param("var"), address);

    WagoMap::Instance(host, port).onWagoConnected.connect([=]()
    {
        readValue();
    });

    Calaos::StartReadRules::Instance().addIO();

    cDebugDom("input") << get_param("id") << ": Ok";
}

WIAnalog::~WIAnalog()
{
}

void WIAnalog::WagoReadCallback(bool status, UWord addr, int count, vector<UWord> &values)
{
    if (!status)
    {
        cErrorDom("input") << get_param("id") << ": Failed to read value";
        if (start)
        {
            Calaos::StartReadRules::Instance().ioRead();
            start = false;
        }

        return;
    }

    double val = value;

    if (!values.empty())
    {
        val = values[0];
    }

    if (val != value)
    {
        value = val;
        emitChange();
    }

    if (start)
    {
        Calaos::StartReadRules::Instance().ioRead();
        start = false;
    }
}

void WIAnalog::readValue()
{
    host = get_param("host");
    if (get_params().Exists("port"))
        Utils::from_string(get_param("port"), port);

    Utils::from_string(get_param("var"), address);

    WagoMap::Instance(host, port).read_words((UWord)address, 1, sigc::mem_fun(*this, &WIAnalog::WagoReadCallback));
}
