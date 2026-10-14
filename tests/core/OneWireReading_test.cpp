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

/*
 * OWTemp against a real OwCtrl, the calaos_1wire process being a stand-in as
 * in core/ProxyClimateMqtt_test: what the process sends is written to its
 * socket. The input poll reads OwCtrl's last value again; only a value the
 * process sent again is a reading (IOBase::lastReading()).
 */

#include <gtest/gtest.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "CalaosCoreFixture.h"
#include "ExternProc.h"
#include "ExternProcSpawnHarness.h"
#include "IOFactory.h"
#include "OWCtrl.h"
#include "OWTemp.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char *const kArgs = "--stand-in";
const char *const kSensor = "28-000000000001";

//The poll, which the rules loop runs every interval.
class OWTempProbe: public OWTemp
{
public:
    OWTempProbe(Params &p):
        OWTemp(p)
    {
    }
    using OWTemp::readValue;
};

REGISTER_IO(OWTempProbe)

//The socket of the stand-in process, -1 when it could not be set up.
int processSocket()
{
    static int fd = -2;
    if (fd != -2)
        return fd;
    fd = -1;

    //It must stay alive, or the server respawns it and drops us.
    if (!ExternProcSpawn::install("calaos_1wire", 0, 600))
        return fd;
    OwCtrl::Instance(kArgs);
    if (!ExternProcSpawn::runLoopUntil([]()
                                       {
        return !ExternProcSpawn::launches("calaos_1wire").empty();
        },
                                       8000))
        return fd;

    const auto argv = ExternProcSpawn::launches("calaos_1wire")[0];
    if (argv.size() < 3)
        return fd;

    const int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un remote;
    ::memset(&remote, 0, sizeof(remote));
    remote.sun_family = AF_UNIX;
    ::strncpy(remote.sun_path, argv[2].c_str(), sizeof(remote.sun_path) - 1);
    if (s >= 0 && ::connect(s, (struct sockaddr *)&remote, sizeof(remote)) == 0)
        fd = s;
    return fd;
}

void processSends(const std::string &json)
{
    const std::string raw = ExternProcMessage(json).getRawData();
    ASSERT_EQ((ssize_t)raw.size(), ::send(processSocket(), raw.data(), raw.size(), 0));
}

std::string scan(const std::string &value)
{
    return "[{\"id\":\"" + std::string(kSensor) + "\",\"value\":\"" + value +
           "\",\"device_type\":\"temperature\"}]";
}

} //namespace

class OneWireReadingTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        ASSERT_GE(processSocket(), 0) << "stand-in calaos_1wire not set up";
        loadConfig();
        Params p = { { "type", "OWTempProbe" }, { "id", "ow_fresh" }, { "name", "ow_fresh" },
                     { "ow_id", kSensor },      { "ow_args", kArgs },  { "enabled", "true" },
                     { "visible", "false" } };
        probe = dynamic_cast<OWTempProbe *>(createIO(p));
        ASSERT_NE(probe, nullptr);
    }

    //Polls until `done` holds, the process messages being read meanwhile.
    bool pollUntil(const std::function<bool()> &done)
    {
        return ExternProcSpawn::runLoopUntil([&]()
                                             {
            probe->readValue();
            return done();
        },
                                             5000);
    }

    //Long enough for a message sent now to have been read.
    void settle()
    {
        ExternProcSpawn::runLoopUntil([]()
                                      {
            return false;
        },
                                      100);
        probe->readValue();
    }

    OWTempProbe *probe = nullptr;
};

TEST_F(OneWireReadingTest, ARepeatedScanIsStillAReading)
{
    processSends(scan("21.5"));
    ASSERT_TRUE(pollUntil([this]()
                          {
        return probe->lastReading().has_value();
    }));
    EXPECT_DOUBLE_EQ(probe->get_value_double(), 21.5);
    const auto first = *probe->lastReading();

    //The poll alone reads the same value again: nothing was received.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    settle();
    EXPECT_EQ(*probe->lastReading(), first);

    processSends(scan("21.5"));
    EXPECT_TRUE(pollUntil([&]()
                          {
        return *probe->lastReading() > first;
    })) << "a scan of the same value was not taken as a reading";

    //A failed read of the sensor, and a scan without it, are not readings.
    const auto second = *probe->lastReading();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    processSends(scan(""));
    settle();
    processSends("[]");
    settle();
    EXPECT_EQ(*probe->lastReading(), second);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    CalaosTest::ExternProcSpawn::teardown();

    fflush(nullptr);
    _exit(ret);
}
