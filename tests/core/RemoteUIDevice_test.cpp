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

//What a RemoteUI panel receives for a device: the referenced IOs expanded to
//the owned and linked ones, the "device" object inside remote_ui_io_states in
//the object form the firmware parses, and the unchanged io_changed.

#include "JsonApiCharacterization.h"

#include "IO/Device.h"
#include "IO/RemoteUI/RemoteUI.h"
#include "RemoteUIWebSocketHandler.h"
#include "HttpClient.h"
#include "ListeRoom.h"
#include "libuvw.h"

#include <chrono>
#include <thread>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

class PanelDevice: public Device
{
public:
    PanelDevice(Params &p): Device(p) {}

protected:
    bool applyCommandField(const string &field, const Json &value, string &reason) override
    {
        IOBase *mode = linkIo("mode");
        if (field != "mode" || !mode)
            return Device::applyCommandField(field, value, reason);
        string m;
        if (!enumField(value, { "off", "heat" }, m, reason))
            return false;
        return mode->set_value(m);
    }

    Json deviceCapabilities() override { return { { "modes", { "off", "heat" } } }; }
    Json deviceState() override
    {
        Json state = Json::object();
        if (IOBase *io = linkIo("mode"))
            state["mode"] = io->get_value_string();
        if (IOBase *io = linkIo("current_temp"))
            state["current_temp"] = io->get_value_double();
        return state;
    }
};

class PanelClient: public HttpClient
{
public:
    explicit PanelClient(const std::shared_ptr<uvw::TcpHandle> &h):
        HttpClient(h), handle(h) {}

    std::shared_ptr<uvw::TcpHandle> handle;
};

class PanelHandler: public RemoteUIWebSocketHandler
{
public:
    explicit PanelHandler(HttpClient *c): RemoteUIWebSocketHandler(c) {}

    void attach(RemoteUI *ui)
    {
        authenticated_remote_ui = ui;
        setAuthenticated(true);
    }
};

} //namespace

REGISTER_DEVICE(panel_device, PanelDevice)

namespace
{

const char *const SCREEN = "rui1_screen";
const char *const DEV = "rui1_dev";
const char *const DEV_MODE = "rui1_dev_mode";
const char *const DEV_CMD = "rui1_dev_cmd";
const char *const DEV_AUX = "rui1_dev_aux";
const char *const EXT_TEMP = "rui1_ext_temp";
const char *const UNRELATED = "rui1_unrelated";
const char *const PLAIN = "rui1_plain";
const char *const GHOST = "rui1_ghost";

std::string house()
{
    std::string dev =
        "    <calaos:device id=\"" + std::string(DEV) + "\" type=\"climate\" impl=\"panel_device\""
        " name=\"Dev\">\n" +
        "      <calaos:internal type=\"InternalString\" id=\"" + DEV_MODE + "\" name=\"m\" visible=\"false\" />\n" +
        "      <calaos:internal type=\"InternalString\" id=\"" + DEV_AUX + "\" name=\"a\" visible=\"false\" />\n" +
        "      <calaos:link name=\"mode\" io=\"" + DEV_MODE + "\" command_io=\"" + DEV_CMD + "\" />\n" +
        "      <calaos:link name=\"current_temp\" io=\"" + EXT_TEMP + "\" />\n" +
        "    </calaos:device>\n";

    std::string screen =
        "    <calaos:remote_ui type=\"RemoteUI\" id=\"" + std::string(SCREEN) +
        "\" name=\"Screen\" enabled=\"true\" visible=\"true\" device_type=\"waveshare-86-panel\""
        " grid_w=\"3\" grid_h=\"3\">\n"
        "      <calaos:pages>\n        <calaos:page name=\"p1\">\n"
        "          <calaos:widget type=\"Climate\" x=\"0\" y=\"0\" io_id=\"" + DEV + "\"/>\n" +
        "          <calaos:widget type=\"switch\" x=\"1\" y=\"0\" io_id=\"" + PLAIN + "\"/>\n" +
        "          <calaos:widget type=\"switch\" x=\"2\" y=\"0\" io_id=\"" + GHOST + "\"/>\n" +
        "        </calaos:page>\n      </calaos:pages>\n    </calaos:remote_ui>\n";

    return ioXmlDocument(
        //The screen comes first: its load-time expansion cannot see the device.
        roomXml("Salon", "salon", screen + dev + internalIoXml("InternalBool", PLAIN, "Plain") +
                                      internalIoXml("InternalBool", UNRELATED, "Unrelated")) +
        roomXml("Kitchen", "kitchen", internalIoXml("InternalInt", EXT_TEMP, "Temp") +
                                      internalIoXml("InternalString", DEV_CMD, "Cmd")));
}

void pumpFor(int ms, const std::function<bool()> &done = nullptr)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end)
    {
        pumpEventLoop(1);
        if (done && done())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

} //namespace

class RemoteUIDeviceTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();
        loadConfig(house(), rulesXmlDocument(""));

        screen = dynamic_cast<RemoteUI *>(ListeRoom::Instance().get_io(SCREEN));
        ASSERT_NE(screen, nullptr);

        auto tcp = uvw::Loop::getDefault()->resource<uvw::TcpHandle>();
        client = std::make_shared<PanelClient>(tcp);
        handler.reset(new PanelHandler(client.get()));
        handler->sendData.connect([this](const std::string &d) { sent.push_back(d); });
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

    Device *device() { return ListeRoom::Instance().findIOAs<Device>(DEV); }

    //The last message of this type, parsed, or null. For an event, only the
    //io_changed of `eventId` counts: the linked IOs publish their own.
    Json lastOfType(const std::string &msg, const std::string &eventId = std::string()) const
    {
        for (auto it = sent.rbegin(); it != sent.rend(); ++it)
        {
            const Json env = Json::parse(*it, nullptr, false);
            if (!env.is_object() || env.value("msg", "") != msg)
                continue;
            if (!eventId.empty() && env.at("data").at("data").value("id", "") != eventId)
                continue;
            return env;
        }
        return Json();
    }

    RemoteUI *screen = nullptr;
    std::shared_ptr<PanelClient> client;
    std::unique_ptr<PanelHandler> handler;
    std::vector<std::string> sent;
};

TEST_F(RemoteUIDeviceTest, ReferencedIosExpandADeviceToItsOwnedAndLinkedIos)
{
    screen->extractReferencedIOs();
    const auto &refs = screen->getReferencedIOs();

    for (const char *id: { DEV, DEV_MODE, DEV_CMD, DEV_AUX, EXT_TEMP, PLAIN, GHOST })
        EXPECT_TRUE(screen->hasReferencedIO(id)) << id;
    EXPECT_FALSE(screen->hasReferencedIO(UNRELATED));
    EXPECT_EQ(refs.size(), 7u);
}

TEST_F(RemoteUIDeviceTest, ReferencedIosFollowALinkResolvedAfterTheLoad)
{
    ListeRoom::Instance().deleteIO(ListeRoom::Instance().get_io(EXT_TEMP));
    screen->extractReferencedIOs();
    //The link still names the id: the consumers skip what get_io() cannot find.
    EXPECT_TRUE(screen->hasReferencedIO(EXT_TEMP));
    EXPECT_TRUE(screen->hasReferencedIO(DEV_MODE));
}

TEST_F(RemoteUIDeviceTest, IoStatesCarryTheWholeDeviceObjectInObjectForm)
{
    ListeRoom::Instance().get_io(EXT_TEMP)->set_value(21.5);
    device()->set_value(std::string("heat"));
    pumpFor(300);
    sent.clear();

    handler->sendInitialIOStates();
    const Json env = lastOfType("remote_ui_io_states");
    ASSERT_TRUE(env.is_object());
    const Json &states = env.at("data");
    ASSERT_TRUE(states.is_object());

    const Json &dev = states.at(DEV);
    ASSERT_TRUE(dev.is_object());
    EXPECT_EQ(dev.value("state", ""), "heat");
    EXPECT_EQ(dev.value("gui_type", ""), "climate");
    EXPECT_EQ(dev.at("device"), device()->deviceObject());
    EXPECT_TRUE(dev.at("device").at("state").at("current_temp").is_number());

    //Everything that is not a device keeps the bare string the shipped
    //firmwares already read; owned and linked IOs of the device are in.
    EXPECT_TRUE(states.at(PLAIN).is_string());
    EXPECT_TRUE(states.at(DEV_MODE).is_string());
    EXPECT_TRUE(states.at(DEV_AUX).is_string());
    EXPECT_TRUE(states.at(EXT_TEMP).is_string());
    EXPECT_FALSE(states.contains(UNRELATED));
    EXPECT_FALSE(states.contains(GHOST));
}

TEST_F(RemoteUIDeviceTest, ConfigUpdateListsTheDeviceIos)
{
    handler->sendConfigUpdate();
    const Json env = lastOfType("remote_ui_config_update");
    ASSERT_TRUE(env.is_object());

    std::set<std::string> ids;
    for (const Json &item: env.at("data").at("io_items"))
        ids.insert(item.value("id", ""));
    for (const char *id: { DEV, DEV_MODE, DEV_CMD, EXT_TEMP, PLAIN })
        EXPECT_TRUE(ids.count(id)) << id;
}

TEST_F(RemoteUIDeviceTest, IoChangedReachesThePanelWithTheDeviceObject)
{
    device()->set_value(std::string("heat"));
    pumpFor(300);
    sent.clear();

    ListeRoom::Instance().get_io(EXT_TEMP)->set_value(23.4);
    auto done = [this]() { return !lastOfType("event", DEV).is_null(); };
    pumpFor(2000, done);

    const Json env = lastOfType("event", DEV);
    ASSERT_TRUE(env.is_object());
    const Json &ev = env.at("data");
    EXPECT_EQ(ev.value("type_str", ""), "io_changed");
    EXPECT_EQ(ev.at("data").value("id", ""), DEV);
    EXPECT_EQ(ev.at("data").value("state", ""), "heat");
    EXPECT_EQ(ev.at("data").at("device"), device()->deviceObject());
    EXPECT_DOUBLE_EQ(ev.at("data").at("device").at("state").at("current_temp").get<double>(), 23.4);
}

TEST_F(RemoteUIDeviceTest, ADeviceTurnedPlainIsSentWithoutADeviceObject)
{
    ListeRoom::Instance().deleteIO(device());
    sent.clear();

    handler->sendInitialIOStates();
    const Json env = lastOfType("remote_ui_io_states");
    ASSERT_TRUE(env.is_object());
    //Nothing tells the firmware to drop its stored device: see RUI-3.
    EXPECT_FALSE(env.at("data").contains(DEV));
    EXPECT_TRUE(env.at("data").contains(PLAIN));
}
