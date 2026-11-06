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
 * A thermostat on a KNX sensor that only sends on change: the group is read
 * when the last reading gets old, one read at a time. A real KNXCtrl, the
 * calaos_knx command process being a stand-in whose socket this test reads,
 * as in core/ProxyClimateMqtt_test.
 */

#include <gtest/gtest.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <string>

#include "CalaosCoreFixture.h"
#include "ExternProc.h"
#include "ExternProcSpawnHarness.h"
#include "InternalThermostat.h"
#include "KNXCtrl.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

//A host of its own, so that its two stand-in processes are the only ones.
const char *const kHost = "127.0.0.3";
const char *const kGroup = "1/2/3";

struct Command
{
    int fd = -1;
    std::string rx;
};

//The socket of the stand-in command process, fd -1 when it could not be set up.
Command &command()
{
    static Command *c = nullptr;
    if (c)
        return *c;
    c = new Command();

    //It must stay alive, or the server respawns it.
    if (!ExternProcSpawn::install("calaos_knx", 0, 600))
        return *c;
    KNXCtrl::Instance(kHost);
    auto both = []()
    {
        return ExternProcSpawn::launches("calaos_knx").size() >= 2;
    };
    if (!ExternProcSpawn::runLoopUntil(both, 8000))
        return *c;

    //The command process, not the bus monitor.
    for (const auto &argv: ExternProcSpawn::launches("calaos_knx"))
    {
        bool monitor = false;
        for (const std::string &a: argv)
            monitor = monitor || a == "--internal-monitor-bus";
        if (monitor || argv.size() < 3)
            continue;

        const int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un remote;
        ::memset(&remote, 0, sizeof(remote));
        remote.sun_family = AF_UNIX;
        ::strncpy(remote.sun_path, argv[2].c_str(), sizeof(remote.sun_path) - 1);
        if (s >= 0 && ::connect(s, (struct sockaddr *)&remote, sizeof(remote)) == 0)
            c->fd = s;
        break;
    }
    //Until the server has accepted us, what it sends is dropped.
    ExternProcSpawn::runLoopUntil([]()
                                  {
        return false;
    },
                                  200);
    return *c;
}

//Group reads of kGroup the server sent within `ms`.
int readsWithin(int ms)
{
    Command &c = command();
    int reads = 0;
    ExternProcMessage frame;
    ExternProcSpawn::runLoopUntil([&]()
                                  {
        char buf[4096];
        const ssize_t n = ::recv(c.fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0)
            c.rx.append(buf, buf + n);
        while (frame.processFrameData(c.rx) && frame.isValid())
        {
            const Json j = Json::parse(frame.getPayload(), nullptr, false);
            frame.clear();
            if (j.is_object() && j.value("type", "") == "read" && j.value("group_addr", "") == kGroup)
                reads++;
        }
        return false;
    },
                                  ms);
    return reads;
}

std::string house()
{
    const std::string knx = "    <calaos:input type=\"KNXInputTemp\" id=\"knx_temp\" name=\"knx_temp\" "
                            "knx_group=\"" +
                            std::string(kGroup) + "\" host=\"" + kHost +
                            "\" eis=\"5\" visible=\"false\" />\n";
    const std::string th =
        "    <calaos:device id=\"knxth\" type=\"climate\" impl=\"thermostat\" name=\"knxth\">\n"
        "      <calaos:internal type=\"InternalInt\" id=\"knxth_target\" name=\"knxth_target\" "
        "visible=\"false\" />\n"
        "      <calaos:internal type=\"InternalBool\" id=\"knxth_heat\" name=\"knxth_heat\" "
        "visible=\"false\" />\n"
        "      <calaos:link name=\"current_temp\" io=\"knx_temp\" />\n"
        "      <calaos:link name=\"target_temp\" io=\"knxth_target\" />\n"
        "      <calaos:link name=\"output_heat\" io=\"knxth_heat\" />\n"
        "      <calaos:param name=\"sensor_timeout\" value=\"0.8\" />\n"
        "    </calaos:device>\n";
    return ioXmlDocument(roomXml("Salon", "salon", knx + th));
}

} //namespace

class KnxThermostatReadTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        ASSERT_GE(command().fd, 0) << "stand-in calaos_knx not set up";
        loadConfig(house(), rulesXmlDocument(""));
    }
};

TEST_F(KnxThermostatReadTest, AnAgingSensorIsReadOneRequestAtATime)
{
    auto *th = ListeRoom::Instance().findIOAs<InternalThermostat>("knxth");
    ASSERT_NE(th, nullptr);
    io("knxth_target")->set_value(21.0);
    th->set_value(std::string("heat"));

    //Never read since the start: asked at once, and only once while unanswered.
    EXPECT_EQ(readsWithin(500), 1);

    //The answer, a telegram: the sensor is fresh and nothing is asked for a while.
    KNXCtrl::Instance(kHost)->valueChanged.emit(kGroup, KNXValue::fromFloat(18, 5));
    const auto answered = std::chrono::steady_clock::now();
    EXPECT_EQ(readsWithin(300), 0);
    EXPECT_TRUE(io("knxth_heat")->get_value_bool());

    //Past half of sensor_timeout, read again, once, before it goes stale.
    EXPECT_EQ(readsWithin(400), 1);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - answered)
                  .count(),
              800);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    CalaosTest::ExternProcSpawn::teardown();

    fflush(nullptr);
    _exit(ret);
}
