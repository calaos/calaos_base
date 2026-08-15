/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
#ifndef S_WagoConfigParse_H
#define S_WagoConfigParse_H

#include <string>
#include <Utils.h>

namespace Calaos
{

/* T3.2e — Bounded non-throwing parses for the numeric Wago IO config
 * values (modbus address "var"/"var_up"/"var_down" and TCP "port"),
 * following the pattern of GpioCtrl::parseDebounceTime (T2.13): a value
 * that does not parse or falls outside the ioDoc-documented 0..65535
 * range yields the fallback instead of garbage (the legacy code left an
 * *uninitialized* address member untouched on a failed parse).
 * Beware: Utils::from_string("") returns true with a zero-filled dest,
 * so an absent param surfaces as 0, exactly like the legacy behavior.
 * Header-inline (std + libcalaos_common only) so it can be unit-tested
 * without linking server objects. */
class WagoConfigParse
{
public:
    static constexpr int PORT_DEFAULT = 502;
    static constexpr int ADDRESS_DEFAULT = 0;
    static constexpr int MODBUS_VALUE_MAX = 65535;

    //True when v is inside the ioDoc-documented range for both the
    //modbus addresses and the ethernet port: 0..65535
    static bool valueValid(int v)
    {
        return v >= 0 && v <= MODBUS_VALUE_MAX;
    }

    static int parseValue(const std::string &value, int fallback, bool *ok = nullptr)
    {
        int v = 0;
        bool good = Utils::from_string(value, v) && valueValid(v);
        if (ok) *ok = good;
        return good? v: fallback;
    }

    static int parseAddress(const std::string &value, bool *ok = nullptr)
    {
        return parseValue(value, ADDRESS_DEFAULT, ok);
    }

    static int parsePort(const std::string &value, bool *ok = nullptr)
    {
        return parseValue(value, PORT_DEFAULT, ok);
    }
};

}
#endif
