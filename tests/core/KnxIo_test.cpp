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

/******************************************************************************
 * T3.2a — thin KNX IO subclasses collapsed into KNXIo<Base> (IO/KNX/KNXIo.cpp)
 *
 * The registered XML type names are the ABI to existing io.xml files: after
 * the refactor, each of the eleven KNX types must still be registered in
 * IOFactory and instanciable through the real ListeRoom::createIO() path.
 *
 * Environment notes (same setup as LanHue_test):
 *  - no libuv loop runs here, so nothing ever fires;
 *  - the first KNX IO creation instantiates KNXCtrl, which tries to spawn
 *    the calaos_knx helper. The binary does not exist next to the test
 *    binary, uv_spawn fails synchronously and ExternProcServer handles it
 *    (the 0.1s respawn timers stay armed but never tick);
 *  - read_at_start=true arms the 1.5s Timer::singleShot of
 *    KNXIo::readAtStart(), which is observable by counting armed timers.
 *
 * Custom main() (see bottom): KNXCtrl instances live in a function-local
 * static map. Their destruction at process exit tears down ExternProcServer,
 * whose destructor does process_exe->kill(SIGTERM); after the failed spawn
 * the libuv process handle holds pid 0, and uv_kill(0, SIGTERM) signals our
 * WHOLE PROCESS GROUP — killing the automake test harness (make check dies
 * with Error 143 right after the tests passed). Same bug class as the old
 * PingInputSwitch destructor documented in LanHue_test.cpp, but this one
 * lives in ExternProc.cpp which this ticket does not own. The test therefore
 * _exit()s after RUN_ALL_TESTS() to skip static destructors entirely.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "CalaosCoreFixture.h"
#include "libuvw.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

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

}

class KnxIoTest: public CoreFixture
{
};

//Every KNX XML type with the params a real io.xml would carry.
//read_at_start is deliberately left unset (defaults to disabled).
TEST_F(KnxIoTest, FactoryCreatesAllElevenKnxTypes)
{
    loadConfig();

    const std::vector<std::pair<std::string, Params>> knxTypes = {
        { "KNXInputSwitch",          {} },
        { "KNXInputSwitchLongPress", {} },
        { "KNXInputSwitchTriple",    {} },
        { "KNXInputAnalog",          { { "eis", "5" } } },
        { "KNXInputTemp",            { { "eis", "5" } } },
        { "KNXOutputAnalog",         { { "eis", "5" } } },
        { "KNXOutputLight",          {} },
        { "KNXOutputLightDimmer",    {} },
        { "KNXOutputLightRGB",       { { "knx_group_red", "1/1/1" },
                                       { "knx_group_green", "1/1/2" },
                                       { "knx_group_blue", "1/1/3" } } },
        { "KNXOutputShutter",        { { "knx_group_up", "2/1/1" },
                                       { "knx_group_down", "2/1/2" },
                                       { "time", "30" } } },
        { "KNXOutputShutterSmart",   { { "knx_group_up", "2/1/1" },
                                       { "knx_group_down", "2/1/2" },
                                       { "time_up", "30" },
                                       { "time_down", "30" } } },
    };

    int n = 0;
    for (const auto &[type, extra]: knxTypes)
    {
        const std::string id = "io_knx_t32a_" + Utils::to_string(n++);

        Params p = extra;
        p.Add("type", type);
        p.Add("id", id);
        p.Add("name", type);
        p.Add("knx_group", "0/1/2");
        p.Add("host", "127.0.0.1");
        p.Add("enabled", "true");
        p.Add("visible", "false");

        IOBase *o = createIO(p);
        ASSERT_NE(o, nullptr) << type << " is no longer instanciable";
        EXPECT_EQ(io(id), o) << type;
        EXPECT_EQ(o->get_param("type"), type);

        ASSERT_TRUE(deleteIO(o)) << type;
        EXPECT_EQ(io(id), nullptr) << type;
    }
}

//read_at_start=true must still schedule the startup read request: the
//KNXIo::readAtStart() singleShot timer becomes an armed libuv timer.
TEST_F(KnxIoTest, ReadAtStartArmsTheStartupReadTimer)
{
    loadConfig();

    //Make sure the KNXCtrl singleton (and its failed-spawn respawn timers)
    //already exists so that they do not pollute the timer delta below.
    {
        Params warmup = {
            { "type", "KNXInputSwitch" },
            { "id", "io_knx_t32a_warmup" },
            { "name", "warmup" },
            { "knx_group", "0/1/2" },
            { "host", "127.0.0.1" },
            { "enabled", "true" },
            { "visible", "false" },
        };
        IOBase *w = createIO(warmup);
        ASSERT_NE(w, nullptr);
        ASSERT_TRUE(deleteIO(w));
    }

    const int timersBefore = armedTimerCount();

    Params p = {
        { "type", "KNXInputSwitch" },
        { "id", "io_knx_t32a_ras" },
        { "name", "ras" },
        { "knx_group", "0/1/2" },
        { "host", "127.0.0.1" },
        { "read_at_start", "true" },
        { "enabled", "true" },
        { "visible", "false" },
    };

    IOBase *o = createIO(p);
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(armedTimerCount(), timersBefore + 1) << "read_at_start timer not armed";

    ASSERT_TRUE(deleteIO(o));
}

//Own main instead of gtest_main: skip static destructors, see file header.
//(An object file's main always wins over the one in libgtest_main.)
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    fflush(nullptr);
    _exit(ret);
}
