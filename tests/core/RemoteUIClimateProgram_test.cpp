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

//A thermostat with a schedule program must reach a panel as small as one
//without: the panel keeps only the live fields. Other clients keep the program.

#include "JsonApiCharacterization.h"

#include "IO/Device.h"
#include "IO/RemoteUI/RemoteUI.h"
#include "RemoteUIWebSocketHandler.h"
#include "HttpClient.h"
#include "ListeRoom.h"
#include "libuvw.h"

#include <chrono>
#include <set>
#include <thread>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char *const SCREEN = "ruip_screen";
const char *const WITH = "ruip_with";
const char *const WITHOUT = "ruip_without";

class PanelClient: public HttpClient
{
public:
    explicit PanelClient(const std::shared_ptr<uvw::TcpHandle> &h):
        HttpClient(h),
        handle(h)
    {
    }

    std::shared_ptr<uvw::TcpHandle> handle;
};

class PanelHandler: public RemoteUIWebSocketHandler
{
public:
    explicit PanelHandler(HttpClient *c):
        RemoteUIWebSocketHandler(c)
    {
    }

    void attach(RemoteUI *ui)
    {
        authenticated_remote_ui = ui;
        setAuthenticated(true);
    }

    using RemoteUIWebSocketHandler::adjustEvent;
};

std::string thermostat(const std::string &id, int levels)
{
    auto owned = [&id](const char *type, const char *suffix)
    {
        return "      <calaos:internal type=\"" + std::string(type) + "\" id=\"" + id + suffix +
               "\" name=\"" + id + suffix + "\" visible=\"false\" />\n";
    };
    auto link = [&id](const char *name, const char *suffix)
    {
        return "      <calaos:link name=\"" + std::string(name) + "\" io=\"" + id + suffix + "\" />\n";
    };
    auto param = [](const char *name, int value)
    {
        return "      <calaos:param name=\"" + std::string(name) + "\" value=\"" +
               std::to_string(value) + "\" />\n";
    };

    return "    <calaos:device id=\"" + id + "\" type=\"climate\" impl=\"thermostat\" name=\"" + id +
           "\">\n" + owned("InternalInt", "_target") + owned("InternalString", "_action") +
           owned("InternalBool", "_heat") + owned("InternalInt", "_low") +
           owned("InternalInt", "_high") + owned("InternalBool", "_cool") +
           link("current_temp", "_temp") + link("target_temp", "_target") +
           link("action", "_action") + link("output_heat", "_heat") +
           link("target_temp_low", "_low") + link("target_temp_high", "_high") +
           link("output_cool", "_cool") + param("heat_levels", levels) +
           param("cool_levels", levels) + "    </calaos:device>\n" +
           "    <calaos:internal type=\"InternalInt\" id=\"" + id + "_temp\" name=\"t\" />\n";
}

std::string house()
{
    std::string widgets;
    int x = 0;
    for (const char *id: { WITH, WITHOUT })
        widgets += "          <calaos:widget type=\"Climate\" x=\"" + std::to_string(x++) +
                   "\" y=\"0\" io_id=\"" + id + "\"/>\n";

    const std::string screen =
        "    <calaos:remote_ui type=\"RemoteUI\" id=\"" + std::string(SCREEN) +
        "\" name=\"Screen\" enabled=\"true\" visible=\"true\" device_type=\"waveshare-86-panel\""
        " grid_w=\"3\" grid_h=\"3\">\n      <calaos:pages>\n        <calaos:page name=\"p1\">\n" +
        widgets + "        </calaos:page>\n      </calaos:pages>\n    </calaos:remote_ui>\n";

    return ioXmlDocument(roomXml("Salon", "salon", screen + thermostat(WITH, 10) + thermostat(WITHOUT, 0)));
}

void pumpFor(int ms)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end)
    {
        pumpEventLoop(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

} //namespace

class RemoteUIClimateProgramTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();
        loadConfig(house(), rulesXmlDocument(""));
        pumpFor(300);

        screen = dynamic_cast<RemoteUI *>(ListeRoom::Instance().get_io(SCREEN));
        ASSERT_NE(screen, nullptr);

        auto tcp = uvw::Loop::getDefault()->resource<uvw::TcpHandle>();
        client = std::make_shared<PanelClient>(tcp);
        handler.reset(new PanelHandler(client.get()));
        handler->sendData.connect([this](const std::string &d)
                                  {
            sent.push_back(d);
        });
        handler->attach(screen);
    }

    void TearDown() override
    {
        handler.reset();
        if (client && client->handle)
            client->handle->close();
        client.reset();
        pumpEventLoop(2);
        JsonApiCharacterizationTest::TearDown();
    }

    Device *device(const char *id) { return ListeRoom::Instance().findIOAs<Device>(id); }

    Json loginFrame()
    {
        sent.clear();
        handler->sendInitialIOStates();
        for (const std::string &raw: sent)
        {
            const Json env = Json::parse(raw, nullptr, false);
            if (env.is_object() && env.value("msg", "") == "remote_ui_io_states")
                return env;
        }
        return Json();
    }

    static bool hasProgramIo(const Json &ios)
    {
        for (auto it = ios.begin(); it != ios.end(); ++it)
        {
            const std::string &role = it.key();
            if (role.find("_level") != std::string::npos || role.find("_default") != std::string::npos ||
                role == "program_enabled")
                return true;
        }
        return false;
    }

    //Ids of the program IOs of a device, read from the full (unfiltered) device object.
    std::set<std::string> programIoIds(const char *id)
    {
        std::set<std::string> ids;
        const Json ios = device(id)->deviceObject().at("ios");
        for (auto it = ios.begin(); it != ios.end(); ++it)
            if (hasProgramIo(Json{ { it.key(), 1 } }))
                ids.insert(it.value().get<std::string>());
        return ids;
    }

    //Ids of the io_items of the config sent to the screen.
    std::vector<std::string> configIoIds()
    {
        sent.clear();
        handler->sendConfigUpdate();
        for (const std::string &raw: sent)
        {
            const Json env = Json::parse(raw, nullptr, false);
            if (!env.is_object() || env.value("msg", "") != "remote_ui_config_update")
                continue;
            std::vector<std::string> ids;
            for (const Json &item: env.at("data").at("io_items"))
                ids.push_back(item.value("id", ""));
            return ids;
        }
        ADD_FAILURE() << "no remote_ui_config_update sent";
        return {};
    }

    RemoteUI *screen = nullptr;
    std::shared_ptr<PanelClient> client;
    std::unique_ptr<PanelHandler> handler;
    std::vector<std::string> sent;
};

TEST_F(RemoteUIClimateProgramTest, OtherClientsKeepTheProgram)
{
    const Json dev = device(WITH)->deviceObject();
    EXPECT_TRUE(dev.at("capabilities").contains("program"));
    EXPECT_TRUE(dev.at("state").contains("program"));
    EXPECT_TRUE(hasProgramIo(dev.at("ios")));
    EXPECT_GT(dev.dump().size(), device(WITHOUT)->deviceObject().dump().size() * 2);
}

TEST_F(RemoteUIClimateProgramTest, TheLoginFrameCarriesNoProgram)
{
    const Json env = loginFrame();
    ASSERT_TRUE(env.is_object());
    const Json &states = env.at("data");

    const Json &with = states.at(WITH).at("device");
    const Json &without = states.at(WITHOUT).at("device");
    EXPECT_FALSE(with.at("capabilities").contains("program"));
    EXPECT_FALSE(with.at("state").contains("program"));
    EXPECT_FALSE(hasProgramIo(with.at("ios")));
    EXPECT_EQ(with.at("ios").size(), without.at("ios").size());

    //Live fields stay.
    EXPECT_TRUE(with.at("state").contains("current_temp") || with.at("state").contains("mode") ||
                with.at("state").contains("action"));

    //Same size as a thermostat without a program, whatever its levels count.
    const size_t sizeWith = states.at(WITH).dump().size();
    const size_t sizeWithout = states.at(WITHOUT).dump().size();
    EXPECT_LE(sizeWith, sizeWithout + 64) << sizeWith << " vs " << sizeWithout;
    EXPECT_LE(env.dump().size(), 8192u);

    //The expanded IO list holds no level io either.
    for (auto it = states.begin(); it != states.end(); ++it)
    {
        EXPECT_EQ(it.key().find("_level"), std::string::npos) << it.key();
        EXPECT_EQ(it.key().find("_default"), std::string::npos) << it.key();
    }
    ASSERT_TRUE(screen->hasReferencedIO(std::string(WITH) + "_target"));
}

TEST_F(RemoteUIClimateProgramTest, IoChangedCarriesNoProgram)
{
    sent.clear();
    ListeRoom::Instance().get_io(std::string(WITH) + "_temp")->set_value(21.0);
    pumpFor(500);

    bool seen = false;
    for (const std::string &raw: sent)
    {
        const Json env = Json::parse(raw, nullptr, false);
        if (!env.is_object() || env.value("msg", "") != "event")
            continue;
        const Json &data = env.at("data").at("data");
        if (data.value("id", "") != WITH || !data.contains("device"))
            continue;
        seen = true;
        EXPECT_FALSE(data.at("device").at("capabilities").contains("program"));
        EXPECT_FALSE(data.at("device").at("state").contains("program"));
        EXPECT_FALSE(hasProgramIo(data.at("device").at("ios")));
    }
    EXPECT_TRUE(seen);
}

TEST_F(RemoteUIClimateProgramTest, AdjustEventIgnoresMissingOrNonStringId)
{
    for (const Json &id: { Json(), Json(42) })
    {
        Json ev = { { "data", { { "device", { { "state", { { "program", 1 } } } } } } } };
        if (!id.is_null())
            ev["data"]["id"] = id;
        const Json before = ev;
        EXPECT_NO_THROW(handler->adjustEvent(ev));
        EXPECT_EQ(ev, before);
    }
}

TEST_F(RemoteUIClimateProgramTest, ConfigIoItemsCarryNoProgramIo)
{
    const std::set<std::string> program = programIoIds(WITH);
    ASSERT_GT(program.size(), 10u);

    const std::vector<std::string> ids = configIoIds();
    for (const std::string &id: ids)
        EXPECT_EQ(program.count(id), 0u) << id;

    //The io_items of a programmed climate are the ones of a plain thermostat, no more.
    const std::set<std::string> unique(ids.begin(), ids.end());
    EXPECT_EQ(unique.size(), ids.size());
    size_t with = 0, without = 0;
    for (const std::string &id: ids)
    {
        //"ruip_with" is a prefix of "ruip_without": match with the separator.
        if (id.rfind(std::string(WITHOUT) + "_", 0) == 0)
            ++without;
        else if (id.rfind(std::string(WITH) + "_", 0) == 0)
            ++with;
    }
    EXPECT_EQ(with, without);
    EXPECT_LE(ids.size(), 24u);
}

TEST_F(RemoteUIClimateProgramTest, TheClimateStaysWritableButNotItsProgram)
{
    configIoIds();
    EXPECT_TRUE(screen->hasWritableIO(WITH));
    for (const std::string &id: programIoIds(WITH))
    {
        EXPECT_FALSE(screen->hasWritableIO(id)) << id;
        EXPECT_FALSE(screen->hasReferencedIO(id)) << id;
    }
}

TEST_F(RemoteUIClimateProgramTest, ProgramIoChangesAreNotRelayed)
{
    configIoIds();
    const std::set<std::string> program = programIoIds(WITH);
    ASSERT_FALSE(program.empty());

    sent.clear();
    for (const std::string &id: program)
    {
        IOBase *io = ListeRoom::Instance().get_io(id);
        if (!io)
            continue;
        if (io->get_type() == TBOOL)
            io->set_value(true);
        else
            io->set_value(17.0);
    }
    pumpFor(500);

    for (const std::string &raw: sent)
    {
        const Json env = Json::parse(raw, nullptr, false);
        if (!env.is_object() || env.value("msg", "") != "event")
            continue;
        const std::string id = env.at("data").at("data").value("id", "");
        EXPECT_EQ(program.count(id), 0u) << id;
    }
}
