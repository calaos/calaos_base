/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#include <WOAnalog.h>
#include <WagoMap.h>
#include <IOFactory.h>

using namespace Calaos;
using namespace Utils;

REGISTER_IO(WOAnalog)
REGISTER_IO_USERTYPE(WagoOutputAnalog, WOAnalog)

WOAnalog::WOAnalog(Params &p):
    OutputAnalog(p),
    port(502)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("WOAnalog");
    ioDoc->aliasAdd("WagoOutputAnalog");
    ioDoc->descriptionSet(_("Analog output with Wago module (like 0-10V, 4-20mA, ...)"));
    ioDoc->linkAdd("Calaos Wiki", _("http://calaos.fr/wiki/fr/sortie_analog"));
    ioDoc->paramAdd("host", _("Wago PLC IP address on the network"), IODoc::TYPE_STRING, true);
    ioDoc->paramAddInt("port", _("Wago ethernet port, default to 502"), 0, 65535, false, 502);
    ioDoc->paramAddInt("var", _("PLC address of the output"), 0, 65535, true);

    host = get_param("host");
    Utils::from_string(get_param("var"), address);
    /* T3.25 (review). _or_keep: Exists() proves the key is THERE, not that it
     * carries a number. A "port" that is present and blank made a plain
     * from_string() write 0 over the Modbus default of 502. */
    if (get_params().Exists("port"))
        Utils::from_string_or_keep(get_param("port"), port);

    WagoMap::Instance(host, port);

    WagoMap::Instance(host, port).onWagoConnected.connect([=]()
    {
        WagoMap::Instance(host, port).read_words(WagoTypes::Address((UWord)address + 0x200), WagoTypes::Count(1),
                                              sigc::mem_fun(*this, &WOAnalog::WagoReadCallback));
    });

    Calaos::StartReadRules::Instance().addIO();

    cDebugDom("output") << get_param("id");
}

WOAnalog::~WOAnalog()
{
}

void WOAnalog::WagoReadCallback(bool status, WagoTypes::Address addr, WagoTypes::Count count, vector<UWord> &values)
{
    if (!status)
    {
        cErrorDom("output") << get_param("id") << ": Failed to read value";
        Calaos::StartReadRules::Instance().ioRead();

        return;
    }

    if (!values.empty()) value = values[0];

    emitChange();

    Calaos::StartReadRules::Instance().ioRead();
}

void WOAnalog::WagoWriteCallback(bool status, WagoTypes::Address addr, WagoTypes::WordValue _value)
{
    if (!status)
    {
        cErrorDom("output") << get_param("id") << ": Failed to write value";
        return;
    }

    /* ⚠️ T3.46 - the one line where the wrapper is undone (workaround W7).
     * `value = addr.v` would type-check just as well; typing the signature
     * removes the ORDER mistake, not this one. And the value that lands here
     * is the literal 0 WagoMap.cpp sends, which is a defect of its own,
     * reported at T3.46.md section 6.4 and deliberately NOT changed here. */
    value = _value.v;

    emitChange();

    cInfoDom("output") << get_param("id") << ", executed action " << value << " (" << get_value_double() << ")";
}

void WOAnalog::set_value_real(double val)
{
    host = get_param("host");
    Utils::from_string(get_param("var"), address);
    /* T3.25 (review). _or_keep: Exists() proves the key is THERE, not that it
     * carries a number. A "port" that is present and blank made a plain
     * from_string() write 0 over the Modbus default of 502. */
    if (get_params().Exists("port"))
        Utils::from_string_or_keep(get_param("port"), port);

    WagoMap::Instance(host, port).write_single_word(WagoTypes::Address((UWord)address), WagoTypes::WordValue(val),
                                                     sigc::mem_fun(*this, &WOAnalog::WagoWriteCallback));
}
