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

/*
 * A destroyed Mqtt IO no longer hears its topics.
 *
 * Same stand-in sidecar as core/MqttOutputString_test: the test connects to
 * the controller's socket in its place and writes the broker messages itself.
 * The probe counts the calls a destroyed IO receives, in any build; the real
 * IOs run their callback on freed memory, which only ASan reports.
 */

#include <gtest/gtest.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <map>
#include <string>

#include "CalaosCoreFixture.h"
#include "ExternProc.h"
#include "ExternProcSpawnHarness.h"
#include "InputString.h"
#include "MqttBrokersList.h"
#include "MqttCtrl.h"
#include "MqttIOBase.h"
#include "MqttWire.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char *const kHost = "192.0.2.63";
const char *const kPort = "1888";

struct Session
{
    bool ok = false;
    int fd = -1;
    std::string rx;
};

Session &session()
{
    static Session *s = nullptr;
    if (s)
        return *s;

    s = new Session();

    //The sidecar must stay alive, or the server respawns it and drops us.
    if (!ExternProcSpawn::install("calaos_mqtt", 0, 600))
        return *s;

    Params bp;
    bp.Add("host", kHost);
    bp.Add("port", kPort);
    MqttBrokersList::Instance().get_ctrl(bp);

    if (!ExternProcSpawn::runLoopUntil([]()
                                       {
        return !ExternProcSpawn::launches("calaos_mqtt").empty();
        },
                                       8000))
        return *s;

    const auto argv = ExternProcSpawn::launches("calaos_mqtt")[0];
    if (argv.size() < 3)
        return *s;

    s->fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un remote;
    ::memset(&remote, 0, sizeof(remote));
    remote.sun_family = AF_UNIX;
    ::strncpy(remote.sun_path, argv[2].c_str(), sizeof(remote.sun_path) - 1);
    if (s->fd < 0 || ::connect(s->fd, (struct sockaddr *)&remote, sizeof(remote)) != 0)
        return *s;

    //The server only has a client once it has accepted us: the config frame
    //is the proof it did.
    ExternProcMessage frame;
    s->ok = ExternProcSpawn::runLoopUntil(
        [&]()
        {
        char buf[4096];
        const ssize_t n = ::recv(s->fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0)
            s->rx.append(buf, buf + n);
        return frame.processFrameData(s->rx) && frame.isValid();
        },
        8000);

    return *s;
}

void brokerSays(const std::string &topic, const std::string &payload,
                const std::function<bool()> &settled)
{
    const std::string raw =
        ExternProcMessage(MqttWire::encodeMessage(topic, payload)).getRawData();
    ASSERT_EQ((ssize_t)raw.size(), ::send(session().fd, raw.data(), raw.size(), 0));
    ASSERT_TRUE(ExternProcSpawn::runLoopUntil(settled, 5000))
        << "the server never acted on the message";
}

std::map<std::string, int> &probeCalls()
{
    static std::map<std::string, int> calls;
    return calls;
}

//Its callback holds its id, not `this`: a call after its death is counted
//instead of touching freed memory.
class ProbeInput: public MqttIOBase<InputString>
{
public:
    explicit ProbeInput(Params &p):
        MqttIOBase(p, "ProbeInput", "test probe")
    {
        const std::string id = get_param("id");
        subscribeTopicSub([id]()
                          {
            probeCalls()[id]++;
        });
    }

protected:
    void readValue() override {}
};

Params ioParams(const std::string &type, const std::string &id, Params extra)
{
    // clang-format off
    Params p = {
        {"type", type},
        {"id", id},
        {"name", id},
        {"host", kHost},
        {"port", kPort},
        {"enabled", "true"},
        {"visible", "true"}
    };
    // clang-format on
    for (int i = 0; i < extra.size(); i++)
    {
        std::string k, v;
        extra.get_item(i, k, v);
        p.Add(k, v);
    }
    return p;
}

} //namespace

class MqttSubscriptionLifetimeTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig();
        ASSERT_TRUE(session().ok) << "stand-in sidecar not set up";
    }

    IOBase *make(const std::string &id, Params extra)
    {
        return createIO(ioParams("MqttInputString", id, extra));
    }
};

TEST_F(MqttSubscriptionLifetimeTest, ADestroyedIoIsNoLongerCalled)
{
    Params a = ioParams("ProbeInput", "probe_gone", { { "topic_sub", "t/probe" } });
    Params b = ioParams("ProbeInput", "probe_stay", { { "topic_sub", "t/probe" } });
    ProbeInput *gone = new ProbeInput(a);
    ProbeInput *stay = new ProbeInput(b);

    brokerSays("t/probe", "1", []()
               {
        return probeCalls()["probe_stay"] == 1;
    });
    EXPECT_EQ(1, probeCalls()["probe_gone"]);

    delete gone;
    brokerSays("t/probe", "2", []()
               {
        return probeCalls()["probe_stay"] == 2;
    });
    EXPECT_EQ(1, probeCalls()["probe_gone"]);

    delete stay;
}

TEST_F(MqttSubscriptionLifetimeTest, ADeletedIoIsNotReadByTheNextMessage)
{
    IOBase *gone = make("s_gone", { { "topic_sub", "t/shared" } });
    IOBase *stay = make("s_stay", { { "topic_sub", "t/shared" } });
    ASSERT_NE(nullptr, gone);
    ASSERT_NE(nullptr, stay);

    brokerSays("t/shared", "a", [&]()
               {
        return stay->get_value_string() == "a";
    });
    EXPECT_EQ("a", gone->get_value_string());

    ASSERT_TRUE(deleteIO(gone));

    //Another IO on the same topic keeps hearing it.
    brokerSays("t/shared", "b", [&]()
               {
        return stay->get_value_string() == "b";
    });
}

TEST_F(MqttSubscriptionLifetimeTest, StatusTopicsOfADeletedIoAreDropped)
{
    IOBase *gone = make("st_gone",
                        // clang-format off
                        {
                            {"topic_sub", "t/st/state"},
                            {"battery_topic", "t/st/battery"},
                            {"connected_status_topic", "t/st/online"},
                            {"wireless_signal_topic", "t/st/rssi"},
                            {"uptime_topic", "t/st/uptime"},
                            {"ip_address_topic", "t/st/ip"},
                            {"wifi_ssid_topic", "t/st/ssid"}
                        });
    // clang-format on
    IOBase *witness = make("st_wit", { { "topic_sub", "t/st/witness" } });
    ASSERT_NE(nullptr, gone);
    ASSERT_NE(nullptr, witness);

    brokerSays("t/st/battery", "42", [&]()
               {
        return gone->getStatusInfo().Exists("battery_level");
    });

    ASSERT_TRUE(deleteIO(gone));

    // clang-format off
    const std::vector<std::pair<std::string, std::string>> status = {
        {"t/st/battery", "43"},
        {"t/st/online", "true"},
        {"t/st/rssi", "60"},
        {"t/st/uptime", "100"},
        {"t/st/ip", "10.0.0.2"},
        {"t/st/ssid", "home"},
        {"t/st/state", "x"}
    };
    // clang-format on
    for (const auto &m: status)
        brokerSays(m.first, m.second, []()
                   {
            return true;
        });

    //Messages are handled in order: once the witness has its value, every
    //message above went through the controller.
    brokerSays("t/st/witness", "seen", [&]()
               {
        return witness->get_value_string() == "seen";
    });
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    CalaosTest::ExternProcSpawn::teardown();

    fflush(nullptr);
    _exit(ret);
}
