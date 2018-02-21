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
#include <WODigital.h>
#include <IOFactory.h>
#include <WagoMap.h>

using namespace Calaos;

REGISTER_IO(WODigital)
REGISTER_IO_USERTYPE(WagoOutputLight, WODigital)

WODigital::WODigital(Params &p):
    OutputLight(p),
    port(502),
    start(true)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("WODigital");
    ioDoc->aliasAdd("WagoOutputLight");
    ioDoc->descriptionSet(_("Simple light or relay control using wago digital output modules (like 750-1504, ...)"));
    ioDoc->linkAdd("Calaos Wiki", _("http://calaos.fr/wiki/fr/750-1504"));
    ioDoc->paramAdd("host", _("Wago PLC IP address on the network"), IODoc::TYPE_STRING, true);
    ioDoc->paramAddInt("port", _("Wago ethernet port, default to 502"), 0, 65535, false, 502);
    ioDoc->paramAddInt("var", _("PLC address of the output"), 0, 65535, true);
    ioDoc->paramAdd("wago_841", _("Should be false if PLC is 750-842, true otherwise"), IODoc::TYPE_BOOL, true, "true");
    ioDoc->paramAdd("knx", _("Set to true if output is a KNX device (only for 750-849 with KNX/TP1 module)"), IODoc::TYPE_BOOL, false);

    host = get_param("host");

    from_string(get_param("var"), address);

    if (get_params().Exists("port"))
        Utils::from_string(get_param("port"), port);

    WagoMap::Instance(host, port);

    if (get_param("knx") == "true")
        address += WAGO_KNX_START_ADDRESS;

    int noTranslatedAddress = address;
    WagoMap::Instance(host, port).onWagoConnected.connect([=]()
    {
        //Do this before translating address to 841/849
        WagoMap::Instance(host, port).read_output_bits((UWord)noTranslatedAddress, 1, sigc::mem_fun(*this, &WODigital::WagoReadCallback));
    });

    if (get_param("wago_841") == "true" && get_param("knx") != "true")
        address += WAGO_841_START_ADDRESS;

    Calaos::StartReadRules::Instance().addIO();

    cDebugDom("output") << get_param("id") << ": Ok";
}

WODigital::~WODigital()
{
}

void WODigital::WagoReadCallback(bool status, UWord addr, int count, vector<bool> &values)
{
    if (!status)
    {
        cErrorDom("output") << get_param("id") << ": Failed to read value";
        if (start)
        {
            Calaos::StartReadRules::Instance().ioRead();
            start = false;
        }

        return;
    }

    if (!values.empty())
        value = values[0];

    cInfoDom("output") << get_param("id") << ": Reading initial value: " << (value?"true":"false");

    emitChange();

    if (start)
    {
        Calaos::StartReadRules::Instance().ioRead();
        start = false;
    }
}

void WODigital::WagoWriteCallback(bool status, UWord addr, bool _value)
{
    if (!status)
    {
        cErrorDom("output") << get_param("id") << ": Failed to write value";
        return;
    }
}

bool WODigital::set_value_real(bool val)
{
    host = get_param("host");
    Utils::from_string(get_param("var"), address);
    if (get_param("knx") == "true")
        address += WAGO_KNX_START_ADDRESS;
    if (get_param("wago_841") == "true" && get_param("knx") != "true")
        address += WAGO_841_START_ADDRESS;
    if (get_params().Exists("port"))
        Utils::from_string(get_param("port"), port);

    WagoMap::Instance(host, port).write_single_bit((UWord)address, val, sigc::mem_fun(*this, &WODigital::WagoWriteCallback));

    return true;
}

