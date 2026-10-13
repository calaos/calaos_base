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
 * ProxyClimate on real Mqtt IOs and a real MqttCtrl, the sidecar being a
 * stand-in as in core/MqttOutputString_test: what the server publishes is read
 * on the socket, what the broker says is written to it. No broker.
 */

#include <gtest/gtest.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include "CalaosCoreFixture.h"
#include "ExternProc.h"
#include "ExternProcSpawnHarness.h"
#include "MqttBrokersList.h"
#include "MqttCtrl.h"
#include "MqttWire.h"
#include "ProxyClimate.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char *const kHost = "192.0.2.62";
const char *const kPort = "1887";

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

    if (!ExternProcSpawn::runLoopUntil([]() { return !ExternProcSpawn::launches("calaos_mqtt").empty(); },
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

    //The server only has a client once it has accepted us, and what it sends
    //before that is dropped: the config frame is the proof it did.
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

//Next publish on `topic`. Publishes left by an earlier case (a keep-alive
//keeps going until its device is torn down) are skipped.
bool nextPublishOn(const std::string &topic, std::string &payload, int timeoutMs = 5000)
{
    Session &s = session();
    ExternProcMessage frame;
    bool got = false;

    ExternProcSpawn::runLoopUntil(
        [&]()
        {
        char buf[4096];
        const ssize_t n = ::recv(s.fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0)
            s.rx.append(buf, buf + n);

        while (frame.processFrameData(s.rx) && frame.isValid())
        {
            Params p;
            const bool decoded = MqttWire::decodeMessage(frame.getPayload(), p);
            frame.clear();
            if (decoded && !p.Exists("action") && p["topic"] == topic)
            {
                payload = p["payload"];
                got = true;
                return true;
            }
        }
        return false;
    },
        timeoutMs);

    return got;
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

std::string mqtt(const std::string &kind, const std::string &type, const std::string &id,
                 const std::string &extra)
{
    return "      <calaos:" + kind + " type=\"" + type + "\" id=\"" + id + "\" name=\"" + id +
           "\" visible=\"false\" host=\"" + kHost + "\" port=\"" + kPort + "\" " + extra + " />\n";
}

std::string link(const std::string &name, const std::string &io,
                 const std::string &commandIo = std::string())
{
    std::string x = "      <calaos:link name=\"" + name + "\" io=\"" + io + "\"";
    if (!commandIo.empty())
        x += " command_io=\"" + commandIo + "\"";
    return x + " />\n";
}

//Topics are per case: MqttCtrl keeps the subscription of an IO that is gone,
//and a broker message on a shared topic would call into it.
std::string prefix;

std::string t(const std::string &suffix) { return "t/" + prefix + "/" + suffix; }

std::string house()
{
    //The spec's proxy example: read the state topic, write the set topic.
    std::string ac;
    ac += mqtt("input", "MqttInputString", "e2e_mode", "topic_sub=\"" + t("mode") + "\"");
    ac += mqtt("output", "MqttOutputString", "e2e_mode_cmd",
               "topic_pub=\"" + t("mode/set") + "\" topic_sub=\"" + t("mode/echo") +
                   "\" data=\"__##VALUE##__\"");
    ac += mqtt("output", "MqttOutputAnalog", "e2e_remote",
               "topic_pub=\"" + t("remote/set") + "\" topic_sub=\"" + t("remote/echo") +
                   "\" data=\"__##VALUE##__\"");
    ac += link("mode", "e2e_mode", "e2e_mode_cmd") + link("remote_temp", "e2e_remote") +
          link("remote_temp_source", "e2e_src") +
          "      <calaos:param name=\"remote_temp_keepalive\" value=\"0.1\" />\n";

    //Inputs only: nothing can be sent to the appliance through them.
    std::string ro;
    ro += mqtt("input", "MqttInputString", "e2e_ro_mode", "topic_sub=\"" + t("ro/mode") + "\"");
    ro += mqtt("input", "MqttInputAnalog", "e2e_ro_target", "topic_sub=\"" + t("ro/target") + "\"");
    ro += link("mode", "e2e_ro_mode") + link("target_temp", "e2e_ro_target");

    const std::string salon =
        "    <calaos:device id=\"e2e\" type=\"climate\" impl=\"proxy\" name=\"e2e\">\n" + ac +
        "    </calaos:device>\n"
        "    <calaos:device id=\"e2e_ro\" type=\"climate\" impl=\"proxy\" name=\"e2e_ro\">\n" +
        ro + "    </calaos:device>\n" +
        "      <calaos:internal type=\"InternalInt\" id=\"e2e_src\" name=\"e2e_src\" visible=\"false\" />\n";

    return ioXmlDocument(roomXml("Salon", "salon", salon));
}

} //namespace

class ProxyClimateMqttTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        ASSERT_TRUE(session().ok) << "stand-in sidecar not set up";
        prefix = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        loadConfig(house(), rulesXmlDocument(""));
    }

    static ProxyClimate *dev(const std::string &id)
    {
        return ListeRoom::Instance().findIOAs<ProxyClimate>(id);
    }
};

//The set topic is published again with a value that did not change: the
//appliance falls back to its own sensor if it stops hearing from us.
TEST_F(ProxyClimateMqttTest, TheKeepAlivePublishesTheSameValueAgain)
{
    io("e2e_src")->set_value(21.5);

    std::string payload;
    for (int i = 0; i < 3; i++)
    {
        ASSERT_TRUE(nextPublishOn(t("remote/set"), payload)) << "publication " << i;
        EXPECT_EQ(payload, "21.5") << "publication " << i;
    }

    //Publications of the old value may still be queued ahead of the new one.
    io("e2e_src")->set_value(19.0);
    for (int i = 0; i < 4 && payload != "19"; i++)
        ASSERT_TRUE(nextPublishOn(t("remote/set"), payload));
    EXPECT_EQ(payload, "19");
}

TEST_F(ProxyClimateMqttTest, NothingIsPublishedBeforeTheSourceHasAValue)
{
    std::string payload;
    EXPECT_FALSE(nextPublishOn(t("remote/set"), payload, 400));
}

TEST_F(ProxyClimateMqttTest, TheModeIsReadFromTheStateTopicAndWrittenToTheSetTopic)
{
    brokerSays(t("mode"), "heat_cool",
               [&]() { return dev("e2e")->get_value_string() == "auto"; });

    std::string payload;
    ASSERT_TRUE(dev("e2e")->set_value(std::string("cool")));
    ASSERT_TRUE(nextPublishOn(t("mode/set"), payload));
    EXPECT_EQ(payload, "cool");

    ASSERT_TRUE(dev("e2e")->set_value(std::string("auto")));
    ASSERT_TRUE(nextPublishOn(t("mode/set"), payload));
    EXPECT_EQ(payload, "heat_cool");

    brokerSays(t("mode"), "auto", [&]() { return io("e2e_mode")->get_value_string() == "auto"; });
    ASSERT_TRUE(dev("e2e")->set_value(std::string("auto")));
    ASSERT_TRUE(nextPublishOn(t("mode/set"), payload));
    EXPECT_EQ(payload, "auto");
}

//MqttInputAnalog takes a write and sends it nowhere: the device must not
//claim to have set the target.
TEST_F(ProxyClimateMqttTest, InputsOnlyLinksCannotBeWritten)
{
    const Json caps = dev("e2e_ro")->deviceObject().at("capabilities");
    EXPECT_FALSE(caps.contains("modes"));
    EXPECT_FALSE(caps.contains("target_temp"));

    EXPECT_FALSE(dev("e2e_ro")->set_value(std::string(R"({"target_temp":22})")));
    EXPECT_FALSE(dev("e2e_ro")->set_value(std::string("heat")));
    EXPECT_DOUBLE_EQ(io("e2e_ro_target")->get_value_double(), 0);
    EXPECT_FALSE(dev("e2e_ro")->deviceObject().at("state").contains("target_temp"));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    CalaosTest::ExternProcSpawn::teardown();

    fflush(nullptr);
    _exit(ret);
}
