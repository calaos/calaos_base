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

/******************************************************************************
 * Tests for the LAN and Hue IOs (T1.13).
 *
 * Three things are covered here:
 *
 *  - WOLOutputBool::parseMacAddress(): pure function, no IO needed. Upper
 *    case hex digits used to be rejected.
 *  - PingInputSwitch construction: doPing() builds the argv array handed to
 *    uv_spawn(). It used to leave argv[0] uninitialized and to write its
 *    terminating NULL one element past the allocation, so simply creating
 *    the IO is enough for ASan to catch a regression.
 *  - HueOutputLightRGB (and the two LAN IOs) create/delete cycle: their
 *    timers keep a pointer to the IO, so they must not survive it. No libuv
 *    loop runs in the tests (see CalaosCoreFixture.h), a timer that outlives
 *    its IO would therefore never tick here. Instead of ticking it, the
 *    tests count the timer handles that are still armed on the default loop
 *    before and after the deletion, which detects the leaked timer without
 *    running anything.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "CalaosCoreFixture.h"
#include "WOLOutputBool.h"
#include "libuvw.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/* Number of timer handles currently armed on the default loop. Handles that
 * were closed are still walked as long as the loop did not run, they are
 * filtered out with closing().
 */
int armedTimerCount()
{
    int count = 0;

    uvw::Loop::getDefault()->walk([&count](uvw::BaseHandle &handle)
    {
        if (handle.type() == uvw::HandleType::TIMER &&
            handle.active() && !handle.closing())
            count++;
    });

    return count;
}

std::vector<uint8_t> macBytes(std::initializer_list<int> bytes)
{
    std::vector<uint8_t> v;
    for (int b: bytes) v.push_back(uint8_t(b));
    return v;
}

}

/******************************************************************************
 * WOLOutputBool::parseMacAddress()
 ******************************************************************************/

TEST(WolMacAddress, ParsesLowerCase)
{
    std::vector<uint8_t> mac;
    ASSERT_TRUE(WOLOutputBool::parseMacAddress("0a:1b:2c:3d:4e:5f", mac));
    EXPECT_EQ(mac, macBytes({ 0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f }));
}

TEST(WolMacAddress, ParsesUpperCase)
{
    std::vector<uint8_t> mac;
    ASSERT_TRUE(WOLOutputBool::parseMacAddress("0A:1B:2C:3D:4E:5F", mac));
    EXPECT_EQ(mac, macBytes({ 0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f }));
}

TEST(WolMacAddress, ParsesMixedCase)
{
    std::vector<uint8_t> mac;
    ASSERT_TRUE(WOLOutputBool::parseMacAddress("aA:Bb:cC:Dd:eE:Ff", mac));
    EXPECT_EQ(mac, macBytes({ 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff }));
}

TEST(WolMacAddress, AcceptsEverySeparatorAndNone)
{
    const std::vector<uint8_t> expected = macBytes({ 0x00, 0x11, 0x22, 0xAB, 0xCD, 0xEF });

    for (const std::string &addr: { "00:11:22:AB:CD:EF",
                                    "00-11-22-ab-cd-ef",
                                    "0011.22AB.cdef",
                                    "001122ABCDEF" })
    {
        std::vector<uint8_t> mac;
        ASSERT_TRUE(WOLOutputBool::parseMacAddress(addr, mac)) << addr;
        EXPECT_EQ(mac, expected) << addr;
    }
}

TEST(WolMacAddress, RejectsInvalidAddresses)
{
    for (const std::string &addr: { "",
                                    "00:11:22:33:44",          //too short
                                    "00:11:22:33:44:55:66",    //too long
                                    "0g:11:22:33:44:55",       //not hex
                                    "00:11:22:33:44:5G",       //not hex, upper
                                    "00 11 22 33 44 55",       //unknown separator
                                    "zzzzzzzzzzzz" })
    {
        std::vector<uint8_t> mac;
        EXPECT_FALSE(WOLOutputBool::parseMacAddress(addr, mac)) << addr;
    }
}

TEST(WolMacAddress, LeavesOutputUntouchedOnFailure)
{
    std::vector<uint8_t> mac = macBytes({ 1, 2, 3 });
    EXPECT_FALSE(WOLOutputBool::parseMacAddress("nope", mac));
    EXPECT_EQ(mac, macBytes({ 1, 2, 3 }));
}

/******************************************************************************
 * IO lifecycle
 ******************************************************************************/

class LanHueTest: public CoreFixture
{
};

//Creating the IO builds the ping argv array (heap overflow regression) and
//spawns the ping process, deleting it must leave no armed timer behind.
//The ping binary does not have to exist: spawn() then fails, and the
//destructor used to signal pid 0, ie our own process group, which killed the
//whole test run (this test simply dies when that comes back).
TEST_F(LanHueTest, PingInputSwitchCreateDelete)
{
    loadConfig();

    const int timersBefore = armedTimerCount();

    Params p = {
        { "type", "PingInputSwitch" },
        { "id", "input_ping_t113" },
        { "name", "Ping" },
        { "host", "127.0.0.1" },
        { "timeout", "1" },
        { "enabled", "true" },
        { "visible", "false" },
    };

    IOBase *pio = createIO(p);
    ASSERT_NE(pio, nullptr);
    EXPECT_EQ(io("input_ping_t113"), pio);

    ASSERT_TRUE(deleteIO(pio));
    EXPECT_EQ(io("input_ping_t113"), nullptr);
    EXPECT_EQ(armedTimerCount(), timersBefore);
}

TEST_F(LanHueTest, WolOutputBoolCreateDelete)
{
    loadConfig();

    const int timersBefore = armedTimerCount();

    Params p = {
        { "type", "WOLOutputBool" },
        { "id", "output_wol_t113" },
        { "name", "Wol" },
        { "address", "0A:1B:2C:3D:4E:5F" },
        { "enabled", "true" },
        { "visible", "false" },
    };

    IOBase *wol = createIO(p);
    ASSERT_NE(wol, nullptr);

    ASSERT_TRUE(deleteIO(wol));
    EXPECT_EQ(io("output_wol_t113"), nullptr);
    EXPECT_EQ(armedTimerCount(), timersBefore);
}

//The Hue IO polls its bridge from a repeating timer created in its
//constructor. The timer callback uses the IO, so the destructor has to
//destroy it: one timer must be armed while the IO lives, none after.
TEST_F(LanHueTest, HueOutputLightRgbStopsPollingTimerWhenDeleted)
{
    loadConfig();

    const int timersBefore = armedTimerCount();

    Params p = {
        { "type", "HueOutputLightRGB" },
        { "id", "output_hue_t113" },
        { "name", "Hue" },
        { "host", "127.0.0.1" },
        { "api", "notakey" },
        { "id_hue", "1" },
        { "enabled", "true" },
        { "visible", "false" },
    };

    IOBase *hue = createIO(p);
    ASSERT_NE(hue, nullptr);
    EXPECT_EQ(armedTimerCount(), timersBefore + 1) << "polling timer not started";

    ASSERT_TRUE(deleteIO(hue));
    EXPECT_EQ(io("output_hue_t113"), nullptr);
    EXPECT_EQ(armedTimerCount(), timersBefore) << "polling timer survived the IO";
}

//Several create/delete cycles, the way a config reload does it.
TEST_F(LanHueTest, HueOutputLightRgbSurvivesReloadCycles)
{
    loadConfig();

    const int timersBefore = armedTimerCount();

    for (int i = 0;i < 5;i++)
    {
        Params p = {
            { "type", "HueOutputLightRGB" },
            { "id", "output_hue_cycle_t113" },
            { "name", "Hue" },
            { "host", "127.0.0.1" },
            { "api", "notakey" },
            { "id_hue", "1" },
            { "enabled", "true" },
            { "visible", "false" },
        };

        IOBase *hue = createIO(p);
        ASSERT_NE(hue, nullptr);
        ASSERT_TRUE(deleteIO(hue));
    }

    EXPECT_EQ(armedTimerCount(), timersBefore);
}
