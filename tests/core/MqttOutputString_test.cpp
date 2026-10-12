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
 * MqttOutputString: the payload it publishes and the state it reads back.
 *
 * The IOs are real (IOFactory, real MqttCtrl). The sidecar is a stand-in: a
 * recorder script that sleeps, and this test connecting to the server's socket
 * in its place - frames the server writes are the publishes, frames written
 * here are broker messages. No broker, no network.
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

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char *const kHost = "192.0.2.61";
const char *const kPort = "1886";

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

    if (!ExternProcSpawn::runLoopUntil(
            []() { return !ExternProcSpawn::launches("calaos_mqtt").empty(); }, 8000))
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
    s->ok = ExternProcSpawn::runLoopUntil([&]()
    {
        char buf[4096];
        const ssize_t n = ::recv(s->fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0)
            s->rx.append(buf, buf + n);
        return frame.processFrameData(s->rx) && frame.isValid();
    }, 8000);

    return *s;
}

//Next frame the server wrote that is a publish (the config frame is skipped).
bool nextPublish(Params &out)
{
    Session &s = session();
    ExternProcMessage frame;
    bool got = false;

    ExternProcSpawn::runLoopUntil([&]()
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
            if (decoded && !p.Exists("action"))
            {
                out = p;
                got = true;
                return true;
            }
        }
        return false;
    }, 5000);

    return got;
}

//A message from the broker, as the sidecar would hand it over.
void brokerSays(const std::string &topic, const std::string &payload,
                const std::function<bool()> &settled)
{
    const std::string raw =
        ExternProcMessage(MqttWire::encodeMessage(topic, payload)).getRawData();
    ASSERT_EQ((ssize_t)raw.size(), ::send(session().fd, raw.data(), raw.size(), 0));
    ASSERT_TRUE(ExternProcSpawn::runLoopUntil(settled, 5000))
        << "the server never acted on the message";
}

} //namespace

class MqttOutputStringTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig();
        ASSERT_TRUE(session().ok) << "stand-in sidecar not set up";
    }

    IOBase *make(const std::string &type, const std::string &id, Params extra)
    {
        Params p = {{"type", type}, {"id", id}, {"name", id}, {"host", kHost},
                    {"port", kPort}, {"enabled", "true"}, {"visible", "true"}};
        for (int i = 0; i < extra.size(); i++)
        {
            std::string k, v;
            extra.get_item(i, k, v);
            p.Add(k, v);
        }
        return createIO(p);
    }
};

TEST(MqttRenderData, PlainTemplatesAreSentVerbatim)
{
    EXPECT_EQ("heat", MqttCtrl::renderData("__##VALUE##__", "heat"));
    EXPECT_EQ("mode=a\"b\\c", MqttCtrl::renderData("mode=__##VALUE##__", "a\"b\\c"));
    EXPECT_EQ("fixed", MqttCtrl::renderData("fixed", "ignored"));
}

TEST(MqttRenderData, JsonTemplatesGetAnEscapedValue)
{
    const std::string nasty = "say \"hi\" \\ back\nline\ttab \x01 caf\xc3\xa9 \xe2\x82\xac";

    for (const char *tmpl: {"{\"mode\":\"__##VALUE##__\"}",
                            "  {\"mode\":\"__##VALUE##__\"}",
                            "\"__##VALUE##__\""})
    {
        const std::string out = MqttCtrl::renderData(tmpl, nasty);
        const Json j = Json::parse(out, nullptr, false);
        ASSERT_FALSE(j.is_discarded()) << out;
        const Json &v = j.is_object() ? j.at("mode") : j;
        EXPECT_EQ(nasty, v.get<std::string>());
    }

    const Json arr = Json::parse(MqttCtrl::renderData("[\"__##VALUE##__\"]", "q\"q"), nullptr, false);
    ASSERT_TRUE(arr.is_array());
    EXPECT_EQ("q\"q", arr.at(0).get<std::string>());
}

TEST(MqttRenderData, InvalidUtf8DoesNotThrow)
{
    const std::string out = MqttCtrl::renderData("{\"m\":\"__##VALUE##__\"}", "bad\xff");
    EXPECT_FALSE(Json::parse(out, nullptr, false).is_discarded()) << out;
}

TEST(MqttRenderData, NumbersInJsonTemplatesStayNumbers)
{
    const Json j = Json::parse(MqttCtrl::renderData("{\"v\":__##VALUE##__}", "21.5"));
    EXPECT_EQ(21.5, j.at("v").get<double>());
}

TEST_F(MqttOutputStringTest, APlainTemplatePublishesTheStringOnTopicPub)
{
    IOBase *o = make("MqttOutputString", "s_plain",
                     {{"topic_pub", "t/s_plain/set"}, {"topic_sub", "t/s_plain/state"},
                      {"data", "__##VALUE##__"}});
    ASSERT_NE(nullptr, o);

    o->set_value(std::string("say \"hi\" caf\xc3\xa9"));

    Params pub;
    ASSERT_TRUE(nextPublish(pub));
    EXPECT_EQ("t/s_plain/set", pub["topic"]);
    EXPECT_EQ("say \"hi\" caf\xc3\xa9", pub["payload"]);
    EXPECT_EQ("say \"hi\" caf\xc3\xa9", o->get_value_string());
}

TEST_F(MqttOutputStringTest, AJsonTemplatePublishesValidJson)
{
    IOBase *o = make("MqttOutputString", "s_json",
                     {{"topic_pub", "t/s_json/set"}, {"topic_sub", "t/s_json/state"},
                      {"data", "{\"mode\":\"__##VALUE##__\",\"n\":1}"}});
    ASSERT_NE(nullptr, o);

    const std::string nasty = "a\"b\\c \xc3\xa9 \xe2\x82\xac\n";
    o->set_value(nasty);

    Params pub;
    ASSERT_TRUE(nextPublish(pub));
    EXPECT_EQ("t/s_json/set", pub["topic"]);
    const Json j = Json::parse(pub["payload"], nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << pub["payload"];
    EXPECT_EQ(nasty, j.at("mode").get<std::string>());
    EXPECT_EQ(1, j.at("n").get<int>());
}

TEST_F(MqttOutputStringTest, WithoutDataNothingIsPublished)
{
    IOBase *bad = make("MqttOutputString", "s_nodata",
                       {{"topic_pub", "t/nodata/set"}, {"topic_sub", "t/nodata/state"}});
    IOBase *good = make("MqttOutputString", "s_good",
                        {{"topic_pub", "t/good/set"}, {"topic_sub", "t/good/state"},
                         {"data", "__##VALUE##__"}});
    ASSERT_NE(nullptr, bad);
    ASSERT_NE(nullptr, good);

    bad->set_value(std::string("x"));
    good->set_value(std::string("y"));

    Params pub;
    ASSERT_TRUE(nextPublish(pub));
    EXPECT_EQ("t/good/set", pub["topic"]);
}

TEST_F(MqttOutputStringTest, StateIsReadBackFromTopicSubThroughPath)
{
    IOBase *o = make("MqttOutputString", "s_read",
                     {{"topic_pub", "t/read/set"}, {"topic_sub", "t/read/state"},
                      {"path", "mode"}, {"data", "__##VALUE##__"}});
    ASSERT_NE(nullptr, o);
    EXPECT_EQ("", o->get_value_string());

    brokerSays("t/read/state", "{\"mode\":\"cool\"}",
               [&]() { return o->get_value_string() == "cool"; });

    //a path that resolves to nothing must not wipe the state. Messages are
    //handled in order: once the witness is seen, the one before it was too.
    IOBase *witness = make("MqttOutputString", "s_wit",
                           {{"topic_pub", "t/wit/set"}, {"topic_sub", "t/wit/state"},
                            {"data", "__##VALUE##__"}});
    ASSERT_NE(nullptr, witness);
    brokerSays("t/read/state", "{\"other\":1}", []() { return true; });
    brokerSays("t/wit/state", "seen", [&]() { return witness->get_value_string() == "seen"; });
    EXPECT_EQ("cool", o->get_value_string());

    brokerSays("t/read/state", "{\"mode\":\"heat\"}",
               [&]() { return o->get_value_string() == "heat"; });
}

TEST_F(MqttOutputStringTest, WithoutPathTheRawPayloadIsTheState)
{
    IOBase *o = make("MqttOutputString", "s_raw",
                     {{"topic_pub", "t/raw/set"}, {"topic_sub", "t/raw/state"},
                      {"data", "__##VALUE##__"}});
    ASSERT_NE(nullptr, o);

    brokerSays("t/raw/state", "auto", [&]() { return o->get_value_string() == "auto"; });
}

TEST_F(MqttOutputStringTest, TheIODocDescribesDataAndTheAction)
{
    IOBase *o = make("MqttOutputString", "s_doc",
                     {{"topic_pub", "t/doc/set"}, {"topic_sub", "t/doc/state"},
                      {"data", "x"}});
    ASSERT_NE(nullptr, o);

    const std::string doc = o->getDoc()->genDocJson().dump();
    EXPECT_NE(std::string::npos, doc.find("\"data\""));
    EXPECT_NE(std::string::npos, doc.find("Publish a specific string value"));
}

TEST_F(MqttOutputStringTest, MqttOutputAnalogIsClampedBeforeItIsPublished)
{
    IOBase *o = make("MqttOutputAnalog", "a_clamp",
                     {{"topic_pub", "t/a/set"}, {"topic_sub", "t/a/state"},
                      {"data", "{\"setpoint\":__##VALUE##__}"},
                      {"min", "5"}, {"max", "30"}});
    ASSERT_NE(nullptr, o);

    o->set_value(500.0);
    Params pub;
    ASSERT_TRUE(nextPublish(pub));
    EXPECT_EQ(30.0, Json::parse(pub["payload"]).at("setpoint").get<double>());

    o->set_value(-500.0);
    ASSERT_TRUE(nextPublish(pub));
    EXPECT_EQ(5.0, Json::parse(pub["payload"]).at("setpoint").get<double>());

    o->set_value(21.5);
    ASSERT_TRUE(nextPublish(pub));
    EXPECT_EQ(21.5, Json::parse(pub["payload"]).at("setpoint").get<double>());
}

TEST_F(MqttOutputStringTest, MqttOutputAnalogWithoutBoundsIsUnchanged)
{
    IOBase *o = make("MqttOutputAnalog", "a_free",
                     {{"topic_pub", "t/af/set"}, {"topic_sub", "t/af/state"},
                      {"data", "__##VALUE##__"}});
    ASSERT_NE(nullptr, o);

    o->set_value(-1234.0);
    Params pub;
    ASSERT_TRUE(nextPublish(pub));
    EXPECT_EQ("-1234", pub["payload"]);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    CalaosTest::ExternProcSpawn::teardown();

    fflush(nullptr);
    _exit(ret);
}
