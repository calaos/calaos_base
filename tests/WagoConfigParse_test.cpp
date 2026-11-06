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
 ******************************************************************************/

// T3.2e — WagoConfigParse: bounded non-throwing parse of the numeric Wago
// IO config values (modbus address "var"/"var_up"/"var_down", TCP "port"),
// same pattern as GpioCtrl::parseDebounceTime (T2.13). Out-of-range or
// garbage values fall back (address 0 / port 502) instead of leaving an
// uninitialized member behind. Helpers are header-inline, nothing of the
// calaos_server binary is linked here.

#include <gtest/gtest.h>

#include "WagoConfigParse.h"

using namespace Calaos;

TEST(WagoConfigParseTest, NominalAddressIsParsed)
{
    EXPECT_EQ(0, WagoConfigParse::parseAddress("0"));
    EXPECT_EQ(12, WagoConfigParse::parseAddress("12"));
    EXPECT_EQ(65535, WagoConfigParse::parseAddress("65535"));
}

TEST(WagoConfigParseTest, NominalPortIsParsed)
{
    EXPECT_EQ(502, WagoConfigParse::parsePort("502"));
    EXPECT_EQ(1502, WagoConfigParse::parsePort("1502"));
}

TEST(WagoConfigParseTest, EmptyAddressIsStillZeroButNoLongerSilent)
{
    //T3.25 CHANGED THE SECOND HALF OF THIS CASE, deliberately.
    //
    //It used to read "Utils::from_string(\"\") == true with a zero-filled
    //dest, keep it that way", and both halves of that premise were wrong: the
    //old from_string() returned true and did NOT write the destination at all.
    //Since T3.25 a blank string is a parse failure, so an absent or empty
    //"var" takes the fallback branch.
    //
    //THE ADDRESS IS UNCHANGED - ADDRESS_DEFAULT is 0, which is exactly what
    //the legacy path produced. What changes is `ok`, and `ok` drives nothing
    //but a warning (WagoIOBase.h:89-91): a Wago IO with no "var" parameter is
    //now REPORTED in the log instead of silently working on address 0.
    bool ok = true;
    EXPECT_EQ(0, WagoConfigParse::parseAddress("", &ok));
    EXPECT_FALSE(ok) << "an absent modbus address must be reported, not "
                        "silently read as address 0";

    //and a blank one is the same story, which it was not before T3.25
    ok = true;
    EXPECT_EQ(0, WagoConfigParse::parseAddress("   ", &ok));
    EXPECT_FALSE(ok);

    //the port keeps ITS default, which is not zero - this is the case that
    //would have caught a "write T{} on failure" correction
    ok = true;
    EXPECT_EQ(WagoConfigParse::PORT_DEFAULT, WagoConfigParse::parsePort("", &ok));
    EXPECT_FALSE(ok);
}

TEST(WagoConfigParseTest, GarbageFallsBack)
{
    bool ok = true;
    EXPECT_EQ(0, WagoConfigParse::parseAddress("abc", &ok));
    EXPECT_FALSE(ok);

    //from_string() requires eof: trailing garbage is not a valid number
    EXPECT_EQ(0, WagoConfigParse::parseAddress("12xx", &ok));
    EXPECT_FALSE(ok);

    EXPECT_EQ(502, WagoConfigParse::parsePort("modbus", &ok));
    EXPECT_FALSE(ok);
}

TEST(WagoConfigParseTest, OutOfRangeFallsBack)
{
    bool ok = true;
    EXPECT_EQ(0, WagoConfigParse::parseAddress("-1", &ok));
    EXPECT_FALSE(ok);

    EXPECT_EQ(0, WagoConfigParse::parseAddress("65536", &ok));
    EXPECT_FALSE(ok);

    EXPECT_EQ(502, WagoConfigParse::parsePort("70000", &ok));
    EXPECT_FALSE(ok);
}

TEST(WagoConfigParseTest, BoundsAreInclusive)
{
    EXPECT_TRUE(WagoConfigParse::valueValid(0));
    EXPECT_TRUE(WagoConfigParse::valueValid(65535));
    EXPECT_FALSE(WagoConfigParse::valueValid(-1));
    EXPECT_FALSE(WagoConfigParse::valueValid(65536));
}
