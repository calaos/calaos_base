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

#include <array>
#include <cstring>
#include <map>
#include <string>
#include <vector>

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

MqttCtrl *sessionCtrl()
{
    Params bp;
    bp.Add("host", kHost);
    bp.Add("port", kPort);
    return MqttBrokersList::Instance().get_ctrl(bp);
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
    EXPECT_EQ(2u, sessionCtrl()->subscriptionCount("t/shared"));

    ASSERT_TRUE(deleteIO(gone));
    EXPECT_EQ(1u, sessionCtrl()->subscriptionCount("t/shared"));

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

    //Every status topic reaches the live IO.
    // clang-format off
    const std::vector<std::array<std::string, 3>> live = {
        {"t/st/battery", "42", "battery_level"},
        {"t/st/online", "true", "connected"},
        {"t/st/rssi", "60", "wireless_signal"},
        {"t/st/uptime", "100", "uptime"},
        {"t/st/ip", "10.0.0.1", "ip_address"},
        {"t/st/ssid", "home", "wifi_ssid"}
    };
    // clang-format on
    for (const auto &m: live)
        brokerSays(m[0], m[1], [&]()
                   {
            return gone->getStatusInfo().Exists(m[2]);
        });

    const char *const topics[] = { "t/st/state", "t/st/battery", "t/st/online", "t/st/rssi",
                                   "t/st/uptime", "t/st/ip", "t/st/ssid" };
    for (const char *t: topics)
        EXPECT_EQ(1u, sessionCtrl()->subscriptionCount(t)) << t;

    ASSERT_TRUE(deleteIO(gone));
    for (const char *t: topics)
        EXPECT_EQ(0u, sessionCtrl()->subscriptionCount(t)) << t;

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

TEST_F(MqttSubscriptionLifetimeTest, ASubscriptionLastsAsLongAsItsHandle)
{
    int calls = 0;
    auto count = [&calls](string, string)
    {
        calls++;
    };

    EXPECT_FALSE(sessionCtrl()->subscribeTopic("", count).active());

    {
        MqttCtrl::Subscription s = sessionCtrl()->subscribeTopic("t/api", count);
        EXPECT_TRUE(s.active());

        MqttCtrl::Subscription moved(std::move(s));
        EXPECT_FALSE(s.active());
        EXPECT_TRUE(moved.active());

        MqttCtrl::Subscription assigned;
        assigned = std::move(moved);
        EXPECT_FALSE(moved.active());
        EXPECT_TRUE(assigned.active());
        EXPECT_EQ(1u, sessionCtrl()->subscriptionCount("t/api"));

        brokerSays("t/api", "1", [&]()
                   {
            return calls == 1;
        });

        //Assigning over a live handle drops what it held.
        MqttCtrl::Subscription other = sessionCtrl()->subscribeTopic("t/api", count);
        EXPECT_EQ(2u, sessionCtrl()->subscriptionCount("t/api"));
        assigned = std::move(other);
        EXPECT_EQ(1u, sessionCtrl()->subscriptionCount("t/api"));
    }
    EXPECT_EQ(0u, sessionCtrl()->subscriptionCount("t/api"));

    int witness = 0;
    MqttCtrl::Subscription w = sessionCtrl()->subscribeTopic("t/api/witness", [&witness](string, string)
                                                             {
        witness++;
    });
    brokerSays("t/api", "2", []()
               {
        return true;
    });
    brokerSays("t/api/witness", "x", [&]()
               {
        return witness == 1;
    });
    EXPECT_EQ(1, calls);
}

TEST_F(MqttSubscriptionLifetimeTest, ACallbackCanDropSubscriptionsOfTheMessageItHandles)
{
    int first = 0, second = 0, self = 0;
    MqttCtrl::Subscription b;
    MqttCtrl::Subscription a = sessionCtrl()->subscribeTopic("t/drop", [&](string, string)
                                                             {
        first++;
        b.reset();
    });
    b = sessionCtrl()->subscribeTopic("t/drop", [&](string, string)
                                      {
        second++;
    });

    //The callback outlives its own entry: `self` is read after the reset.
    MqttCtrl::Subscription c;
    c = sessionCtrl()->subscribeTopic("t/drop", [&self, &c](string, string)
                                      {
        c.reset();
        self++;
    });

    //One message is dispatched in one go: once `first` moved, all ran.
    brokerSays("t/drop", "1", [&]()
               {
        return first == 1;
    });
    EXPECT_EQ(0, second);
    EXPECT_EQ(1, self);
    EXPECT_EQ(1u, sessionCtrl()->subscriptionCount("t/drop"));
}

TEST_F(MqttSubscriptionLifetimeTest, ASubscriptionOutlivingItsControllerIsHarmless)
{
    const size_t before = ExternProcSpawn::launches("calaos_mqtt").size();
    Params bp;
    bp.Add("host", "192.0.2.64");
    bp.Add("port", "1889");
    MqttCtrl *other = new MqttCtrl(bp);
    ASSERT_TRUE(ExternProcSpawn::runLoopUntil([&]()
                                              {
        return ExternProcSpawn::launches("calaos_mqtt").size() > before;
    },
                                              8000));
    const auto argv = ExternProcSpawn::launches("calaos_mqtt").back();
    ASSERT_GE(argv.size(), 3u);
    EXPECT_EQ(0, ::access(argv[2].c_str(), F_OK)) << argv[2];

    MqttCtrl::Subscription s = other->subscribeTopic("t/other", MqttCtrl::MsgReceivedSignal());
    EXPECT_TRUE(s.active());

    delete other;

    //Its sidecar went with it.
    EXPECT_NE(0, ::access(argv[2].c_str(), F_OK)) << argv[2];
    EXPECT_FALSE(s.active());
    s.reset();
    EXPECT_FALSE(s.active());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    CalaosTest::ExternProcSpawn::teardown();

    fflush(nullptr);
    _exit(ret);
}
