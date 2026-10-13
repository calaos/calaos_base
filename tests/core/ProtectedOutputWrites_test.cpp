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

//Spec §8.3 on the client write paths: set_state over HTTP and WebSocket, and a
//Lua script, write an output of a thermostat. Each is warned about with its
//own source, and the value is applied. The rule path is in
//InternalThermostat_test.
#include "JsonApiCharacterization.h"
#include "ScriptExec.h"
#include "libuvw.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

bool pumpUntil(const std::function<bool()> &done, int ms = 10000)
{
    auto loop = uvw::Loop::getDefault();
    const auto t0 = std::chrono::steady_clock::now();
    while (!done())
    {
        const auto spent = std::chrono::steady_clock::now() - t0;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(spent).count() > ms)
            return false;
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

int occurrences(const std::string &text, const std::string &needle)
{
    int n = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1))
        n++;
    return n;
}

std::string link(const std::string &name, const std::string &io)
{
    return "      <calaos:link name=\"" + name + "\" io=\"" + io + "\" />\n";
}

std::string owned(const std::string &type, const std::string &id, const std::string &extra = std::string())
{
    return "      <calaos:internal type=\"" + type + "\" id=\"" + id + "\" name=\"" + id +
           "\" visible=\"false\"" + extra + " />\n";
}

//Held ON for 2 s once something else switched it: the value stays visible.
std::string house()
{
    const char *saved = R"( save="true")";
    std::string inner = owned("InternalInt", "pt_target", saved) + owned("InternalInt", "pt_low", saved) +
                        owned("InternalInt", "pt_high", saved) + owned("InternalString", "pt_action") +
                        owned("InternalBool", "pt_heat") + owned("InternalBool", "pt_cool") +
                        link("current_temp", "pt_temp") + link("target_temp", "pt_target") +
                        link("target_temp_low", "pt_low") + link("target_temp_high", "pt_high") +
                        link("action", "pt_action") + link("output_heat", "pt_heat") +
                        link("output_cool", "pt_cool") +
                        "      <calaos:param name=\"min_on_time\" value=\"2\" />\n";
    std::string ios = "    <calaos:device id=\"pt\" type=\"climate\" impl=\"thermostat\" name=\"pt\">\n" +
                      inner + "    </calaos:device>\n" +
                      "    <calaos:internal type=\"InternalInt\" id=\"pt_temp\" name=\"pt_temp\" />\n" +
                      "    <calaos:internal type=\"InternalBool\" id=\"pt_free\" name=\"pt_free\" />\n";
    return ioXmlDocument(roomXml("Salon", "salon", ios));
}

} //namespace

class ProtectedOutputWritesTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();
        for (const char *id: { "pt", "pt_target", "pt_low", "pt_high", "pt_free" })
            forgetIOState(id);
        ASSERT_EQ(setenv("CALAOS_BIN_PREFIX", CALAOS_SCRIPT_BINDIR, 1), 0);
        //As calaos_server does: a script that exits first must not kill the test.
        signal(SIGPIPE, SIG_IGN);

        loadConfig(house(), rulesXmlDocument(""));
        auto evaluated = []()
        {
            return io("pt_action")->hasValue();
        };
        ASSERT_TRUE(pumpUntil(evaluated));
        io("pt_temp")->set_value(21.0);
        io("pt_low")->set_value(19.0);
        io("pt_high")->set_value(23.0);
        ASSERT_TRUE(io("pt")->set_value(std::string("auto")));
    }

    void TearDown() override
    {
        auto never = []()
        {
            return false;
        };
        pumpUntil(never, 5);
        JsonApiCharacterizationTest::TearDown();
    }

    template <class F>
    static std::string journal(F fn)
    {
        EXPECT_TRUE(Utils::calaosLogger("device")->isLevelEnabled(Logger::LOG_LEVEL_WARNING));
        testing::internal::CaptureStdout();
        fn();
        return testing::internal::GetCapturedStdout();
    }

    static bool on(const char *id) { return io(id)->get_value_bool(); }

    static std::string driven(const char *role, const char *ioId, const char *source)
    {
        return std::string("Device(pt): ") + role + " '" + ioId +
               "' is driven by the thermostat and was written by " + source + ";";
    }

    static Json httpSetState(const char *id, const char *value)
    {
        return authenticated(Json{ { "action", "set_state" }, { "id", id }, { "value", value } });
    }
};

TEST_F(ProtectedOutputWritesTest, SetStateOverHttpIsWarnedAndApplied)
{
    HttpTestRequest req;
    auto write = [&]()
    {
        req.send(httpSetState("pt_heat", "true"));
    };
    const std::string log = journal(write);

    EXPECT_EQ(req.body(), "{\"success\":\"true\"}");
    EXPECT_TRUE(on("pt_heat"));
    EXPECT_NE(log.find(driven("output_heat", "pt_heat", "set_state over HTTP")), std::string::npos) << log;
}

TEST_F(ProtectedOutputWritesTest, SetStateOverWebSocketIsWarnedAndApplied)
{
    WsTestSession ws;
    auto write = [&]()
    {
        ws.send(Json{ { "msg", "set_state" },
                      { "msg_id", "w1" },
                      { "data", Json{ { "id", "pt_cool" }, { "value", "true" } } } });
    };
    const std::string log = journal(write);

    EXPECT_NE(ws.lastMessage().find("\"success\":\"true\""), std::string::npos);
    EXPECT_TRUE(on("pt_cool"));
    EXPECT_NE(log.find(driven("output_cool", "pt_cool", "set_state over WebSocket")), std::string::npos) << log;
}

//Two sources writing one output are two lines; one source repeating is one.
TEST_F(ProtectedOutputWritesTest, EachSourceIsWarnedOnceInTheWindow)
{
    WsTestSession ws;
    auto wsWrite = [&]()
    {
        ws.send(Json{ { "msg", "set_state" },
                      { "msg_id", "w2" },
                      { "data", Json{ { "id", "pt_heat" }, { "value", "true" } } } });
    };
    auto write = [&]()
    {
        for (int i = 0; i < 3; i++)
        {
            HttpTestRequest req;
            req.send(httpSetState("pt_heat", "true"));
        }
        wsWrite();
        wsWrite();
    };
    const std::string log = journal(write);

    EXPECT_EQ(occurrences(log, driven("output_heat", "pt_heat", "set_state over HTTP")), 1) << log;
    EXPECT_EQ(occurrences(log, driven("output_heat", "pt_heat", "set_state over WebSocket")), 1) << log;
}

//A value set_state refuses is not written, so there is nothing to warn about;
//nor for an IO that is not an output of the thermostat, or the device itself.
TEST_F(ProtectedOutputWritesTest, OnlyAWrittenOutputIsWarnedAbout)
{
    auto write = [&]()
    {
        HttpTestRequest refused;
        refused.send(httpSetState("pt_heat", "true "));
        HttpTestRequest plain;
        plain.send(httpSetState("pt_free", "true"));
        HttpTestRequest device;
        device.send(httpSetState("pt", "heat"));
        HttpTestRequest unknown;
        unknown.send(httpSetState("nothing", "true"));
    };
    const std::string log = journal(write);

    EXPECT_FALSE(on("pt_heat"));
    EXPECT_TRUE(on("pt_free"));
    EXPECT_EQ(log.find("is driven by the thermostat"), std::string::npos) << log;
}

TEST_F(ProtectedOutputWritesTest, ALuaScriptWritingAnOutputIsWarnedAndTheValueIsApplied)
{
    bool finished = false;
    auto done = [&finished](bool)
    {
        finished = true;
    };
    auto applied = [&]()
    {
        return finished && on("pt_heat");
    };
    auto run = [&]()
    {
        ScriptExec::ExecuteScriptDetached("calaos:setIOValue(\"pt_heat\", true)\nreturn true", done);
        EXPECT_TRUE(pumpUntil(applied));
    };
    const std::string log = journal(run);

    EXPECT_TRUE(on("pt_heat"));
    EXPECT_NE(log.find(driven("output_heat", "pt_heat", "a Lua script")), std::string::npos) << log;
}
