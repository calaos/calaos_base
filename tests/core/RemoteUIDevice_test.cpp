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
#include "IO/IOFactory.h"
#include "ListeRoom.h"
#include "libuvw.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

class PanelDevice: public Device
{
public:
    PanelDevice(Params &p):
        Device(p)
    {
    }

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
        "          <calaos:widget type=\"Climate\" x=\"0\" y=\"0\" io_id=\"" +
        DEV + "\"/>\n" +
        "          <calaos:widget type=\"switch\" x=\"1\" y=\"0\" io_id=\"" + PLAIN + "\"/>\n" +
        "          <calaos:widget type=\"switch\" x=\"2\" y=\"0\" io_id=\"" + GHOST + "\"/>\n" +
        "        </calaos:page>\n      </calaos:pages>\n    </calaos:remote_ui>\n";

    return ioXmlDocument(
        //The screen comes first: its load-time expansion cannot see the device.
        roomXml("Salon", "salon", screen + dev + internalIoXml("InternalBool", PLAIN, "Plain") + internalIoXml("InternalBool", UNRELATED, "Unrelated")) +
        roomXml("Kitchen", "kitchen", internalIoXml("InternalInt", EXT_TEMP, "Temp") + internalIoXml("InternalString", DEV_CMD, "Cmd")));
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
    auto done = [this]()
    {
        return !lastOfType("event", DEV).is_null();
    };
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

TEST_F(RemoteUIDeviceTest, DeletedDeviceIsSentWithoutADeviceObject)
{
    ListeRoom::Instance().deleteIO(device());
    sent.clear();

    handler->sendInitialIOStates();
    const Json env = lastOfType("remote_ui_io_states");
    ASSERT_TRUE(env.is_object());
    //The firmware keeps a stored device when none is sent; the server cannot clear it.
    EXPECT_FALSE(env.at("data").contains(DEV));
    EXPECT_TRUE(env.at("data").contains(PLAIN));
}

TEST_F(RemoteUIDeviceTest, IdReusedByAPlainIoIsSentWithoutADeviceObject)
{
    ListeRoom::Instance().deleteIO(device());
    Params p;
    p.Add("id", DEV);
    p.Add("name", "Reused");
    p.Add("type", "InternalString");
    IOBase *io = IOFactory::Instance().CreateIO("InternalString", p);
    ASSERT_NE(io, nullptr);
    firstRoom()->AddIO(io);
    ListeRoom::Instance().addIOHash(io);
    sent.clear();

    handler->sendInitialIOStates();
    const Json env = lastOfType("remote_ui_io_states");
    ASSERT_TRUE(env.is_object());
    ASSERT_TRUE(env.at("data").contains(DEV));
    EXPECT_TRUE(env.at("data").at(DEV).is_string());
}

//SEC-1: a panel reads what its configuration references and writes only what
//its widgets name; nothing else of the JSON API is served to it.

namespace
{

//LogStream ends on std::cout.
class CoutCapture
{
public:
    CoutCapture():
        saved(std::cout.rdbuf(buffer.rdbuf()))
    {
    }
    ~CoutCapture() { std::cout.rdbuf(saved); }
    std::string text() const { return buffer.str(); }

private:
    std::ostringstream buffer;
    std::streambuf *saved;
};

//The three frames calaos_remote_ui sends, built the way
//main/calaos_websocket_manager.cpp builds them (setIoState, requestConfig,
//sendRelayState). The firmware never sends a msg_id.
std::string firmwareSetState(const std::string &id, const std::string &state)
{
    Json j;
    j["msg"] = "set_state";
    j["data"]["id"] = id;
    j["data"]["value"] = state;
    return j.dump();
}

std::string firmwareGetConfig()
{
    Json j;
    j["msg"] = "remote_ui_get_config";
    return j.dump();
}

std::string firmwareRelayState(int relay, bool state)
{
    Json j;
    j["msg"] = "remote_ui_relay_state";
    j["data"]["relay"] = relay;
    j["data"]["state"] = state;
    return j.dump();
}

const char *const RELAY = "sec1_relay";

} //namespace

class RemoteUIScopeTest: public RemoteUIDeviceTest
{
protected:
    void send(const std::string &frame) { handler->processApi(frame, Params()); }

    //Every answer of this type, parsed.
    std::vector<Json> allOfType(const std::string &msg) const
    {
        std::vector<Json> out;
        for (const std::string &s: sent)
        {
            const Json env = Json::parse(s, nullptr, false);
            if (env.is_object() && env.value("msg", "") == msg)
                out.push_back(env);
        }
        return out;
    }

    std::string value(const char *id)
    {
        IOBase *io = ListeRoom::Instance().get_io(id);
        if (io->get_type() == TBOOL)
            return io->get_value_bool() ? "true" : "false";
        return io->get_value_string();
    }

    bool eventSeen(const std::string &id) const { return !lastOfType("event", id).is_null(); }

    //Events are delivered in order: once `marker`'s io_changed is in, every
    //event queued before it has been through the filter.
    void flushEventsWith(const char *marker)
    {
        IOBase *io = ListeRoom::Instance().get_io(marker);
        io->set_value(!io->get_value_bool());
        pumpFor(2000, [this, marker]()
                {
            return eventSeen(marker);
        });
        ASSERT_TRUE(eventSeen(marker)) << "the control event never arrived";
    }

    void expectForbidden(const std::string &msg, const Json &env)
    {
        ASSERT_TRUE(env.is_object()) << msg << ": no answer";
        EXPECT_EQ(env.value("msg", ""), msg);
        EXPECT_EQ(env.at("data").value("error", ""), "forbidden") << msg;
    }
};

TEST_F(RemoteUIScopeTest, SetStateOnAWidgetIoIsApplied)
{
    const std::string before = value(PLAIN);
    const std::string wanted = before == "true" ? "false" : "true";

    Json req = { { "msg", "set_state" }, { "msg_id", "w1" }, { "data", { { "id", PLAIN }, { "value", wanted } } } };
    send(req.dump());

    EXPECT_EQ(value(PLAIN), wanted);
    const Json env = lastOfType("set_state");
    ASSERT_TRUE(env.is_object());
    EXPECT_EQ(env.value("msg_id", ""), "w1");
    EXPECT_EQ(env.at("data"), (Json{ { "success", "true" } }));
}

TEST_F(RemoteUIScopeTest, SetStateOnAnUnreferencedIoIsRefusedAndLoggedWithoutTheId)
{
    const std::string before = value(UNRELATED);
    const std::string wanted = before == "true" ? "false" : "true";

    CoutCapture capture;
    Json req = { { "msg", "set_state" }, { "msg_id", "w2" }, { "data", { { "id", UNRELATED }, { "value", wanted } } } };
    send(req.dump());
    const std::string unknownId = "sec1_injected_name_4f2a";
    send(firmwareSetState(unknownId, "true"));
    const std::string log = capture.text();

    EXPECT_EQ(value(UNRELATED), before);
    const std::vector<Json> answers = allOfType("set_state");
    ASSERT_EQ(answers.size(), 2u);
    expectForbidden("set_state", answers[0]);
    EXPECT_EQ(answers[0].value("msg_id", ""), "w2");
    EXPECT_EQ(answers[0].at("data").value("success", ""), "false");
    expectForbidden("set_state", answers[1]);

    EXPECT_NE(log.find("set_state refused on IO #"), std::string::npos) << log;
    EXPECT_EQ(log.find(UNRELATED), std::string::npos) << log;
    EXPECT_EQ(log.find(unknownId), std::string::npos) << log;
}

TEST_F(RemoteUIScopeTest, ADeviceIsCommandedThroughItsOwnId)
{
    ListeRoom::Instance().get_io(DEV_MODE)->set_value(std::string("off"));
    send(firmwareSetState(DEV, "heat"));
    EXPECT_EQ(value(DEV_MODE), "heat");
    EXPECT_TRUE(allOfType("set_state").empty()) << "an accepted write without msg_id is not answered";
}

TEST_F(RemoteUIScopeTest, TheIosBehindADeviceAreReadableButNotWritable)
{
    send(firmwareSetState(DEV, "off"));
    ASSERT_EQ(value(DEV_MODE), "off");
    const std::string cmdBefore = value(DEV_CMD);
    const std::string auxBefore = value(DEV_AUX);
    const std::string tempBefore = value(EXT_TEMP);

    send(firmwareSetState(DEV_MODE, "heat")); //owned relay
    send(firmwareSetState(DEV_AUX, "x"));     //owned IO
    send(firmwareSetState(DEV_CMD, "heat"));  //command_io of a link
    send(firmwareSetState(EXT_TEMP, "40"));   //sensor of a link

    EXPECT_EQ(value(DEV_MODE), "off");
    EXPECT_EQ(value(DEV_AUX), auxBefore);
    EXPECT_EQ(value(DEV_CMD), cmdBefore);
    EXPECT_EQ(value(EXT_TEMP), tempBefore);
    const std::vector<Json> answers = allOfType("set_state");
    ASSERT_EQ(answers.size(), 4u);
    for (const Json &env: answers)
        expectForbidden("set_state", env);

    for (const char *id: { DEV, DEV_MODE, DEV_AUX, DEV_CMD, EXT_TEMP })
        EXPECT_TRUE(screen->hasReferencedIO(id)) << id;
}

TEST_F(RemoteUIScopeTest, MalformedSetStateIsRefused)
{
    const std::string before = value(PLAIN);
    send(R"({"msg":"set_state","data":{"id":["rui1_plain"],"value":"true"}})");
    send(R"({"msg":"set_state","data":"rui1_plain"})");
    send(R"({"msg":"set_state"})");

    EXPECT_EQ(value(PLAIN), before);
    EXPECT_EQ(allOfType("set_state").size(), 3u);
}

TEST_F(RemoteUIScopeTest, EventsOfUnreferencedIosAreNotDelivered)
{
    pumpFor(300);
    sent.clear();

    ListeRoom::Instance().get_io(UNRELATED)->set_value(!ListeRoom::Instance().get_io(UNRELATED)->get_value_bool());
    flushEventsWith(PLAIN);

    EXPECT_FALSE(eventSeen(UNRELATED));
}

TEST_F(RemoteUIScopeTest, EventsThatAreNotAboutAnIoAreNotDelivered)
{
    pumpFor(300);
    sent.clear();

    //Even when they carry the id of an IO the panel shows.
    EventManager::create(CalaosEvent::EventRoomChanged, { { "id", PLAIN }, { "room_name", "Salon" } });
    EventManager::create(CalaosEvent::EventPushNotification, { { "id", PLAIN }, { "message", "m" } });
    flushEventsWith(PLAIN);

    for (const std::string &s: sent)
    {
        const Json env = Json::parse(s, nullptr, false);
        ASSERT_TRUE(env.is_object());
        EXPECT_EQ(env.at("data").value("type_str", ""), "io_changed") << s;
    }
}

TEST_F(RemoteUIScopeTest, TheFirstUseAfterTheConnectionExpandsTheDevice)
{
    //The screen is loaded before the device it shows: its load-time set
    //cannot hold the device's IOs yet.
    ASSERT_FALSE(screen->hasReferencedIO(DEV_CMD));
    pumpFor(300);
    sent.clear();

    ListeRoom::Instance().get_io(DEV_CMD)->set_value(std::string("heat"));
    flushEventsWith(PLAIN);

    EXPECT_TRUE(eventSeen(DEV_CMD));
}

TEST_F(RemoteUIScopeTest, ADeletedDeviceTakesItsLinkedIosOutOfTheScope)
{
    handler->sendConfigUpdate();
    ASSERT_TRUE(screen->hasReferencedIO(DEV_CMD));
    ListeRoom::Instance().deleteIO(device());
    pumpFor(300);
    sent.clear();

    ListeRoom::Instance().get_io(DEV_CMD)->set_value(std::string("off"));
    flushEventsWith(PLAIN);

    EXPECT_FALSE(eventSeen(DEV_CMD));
    EXPECT_FALSE(screen->hasReferencedIO(DEV_CMD));
}

TEST_F(RemoteUIScopeTest, BothSetsFollowAConfigurationChange)
{
    const std::string widget = std::string("<calaos:widget type=\"switch\" x=\"0\" y=\"0\" io_id=\"") + UNRELATED + "\"/>";
    const std::string xml = std::string("<calaos:remote_ui type=\"RemoteUI\" id=\"") + SCREEN +
                            "\" name=\"Screen\" enabled=\"true\" visible=\"true\" device_type=\"waveshare-86-panel\""
                            " grid_w=\"3\" grid_h=\"3\"><calaos:pages><calaos:page name=\"p1\">" +
                            widget + "</calaos:page></calaos:pages></calaos:remote_ui>";
    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_string(xml.c_str()));
    ASSERT_TRUE(screen->LoadFromXml(doc.first_child()));
    handler->sendConfigUpdate();
    ListeRoom::Instance().get_io(DEV_MODE)->set_value(std::string("off"));
    pumpFor(300);
    sent.clear();

    const std::string plainBefore = value(PLAIN);
    send(firmwareSetState(PLAIN, plainBefore == "true" ? "false" : "true"));
    EXPECT_EQ(value(PLAIN), plainBefore);

    const std::string unrelatedBefore = value(UNRELATED);
    const std::string wanted = unrelatedBefore == "true" ? "false" : "true";
    send(firmwareSetState(UNRELATED, wanted));
    EXPECT_EQ(value(UNRELATED), wanted);

    send(firmwareSetState(DEV, "heat"));
    EXPECT_EQ(value(DEV_MODE), "off");

    flushEventsWith(UNRELATED);
    EXPECT_FALSE(eventSeen(PLAIN));
}

//Every message the JSON API serves a user session, as reachable from a panel
//before SEC-1 (JsonApiHandlerWS::processApi).
TEST_F(RemoteUIScopeTest, EveryOtherActionIsRefused)
{
    const std::string nameBefore = ListeRoom::Instance().get_io(PLAIN)->get_param("name");
    const char *const actions[] = { "login", "login_service", "get_home", "get_state",
                                    "get_states", "query", "get_param", "set_param",
                                    "del_param", "get_playlist", "get_io", "audio",
                                    "audio_db", "get_timerange", "set_timerange", "autoscenario",
                                    "eventlog", "register_push", "settings", "remote_ui_io_states",
                                    "no_such_action" };

    pumpFor(300);
    for (const char *action: actions)
    {
        sent.clear();
        Json req = { { "msg", action },
                     { "msg_id", "m1" },
                     { "data",
                       { { "id", PLAIN },
                         { "items", { PLAIN } },
                         { "param", "name" },
                         { "value", "Hacked" },
                         { "cn_user", apiUser() },
                         { "cn_pass", apiPassword() } } } };
        send(req.dump());
        pumpFor(20);

        std::vector<Json> answers;
        for (const std::string &s: sent)
        {
            const Json env = Json::parse(s, nullptr, false);
            if (env.value("msg", "") != "event")
                answers.push_back(env);
        }
        ASSERT_EQ(answers.size(), 1u) << action;
        const Json &env = answers.front();
        expectForbidden(action, env);
        EXPECT_EQ(env.value("msg_id", ""), "m1") << action;
        EXPECT_EQ(env.at("data").size(), 1u) << action;
    }

    EXPECT_EQ(ListeRoom::Instance().get_io(PLAIN)->get_param("name"), nameBefore);
}

TEST_F(RemoteUIScopeTest, EveryMessageTheFirmwareSendsIsServed)
{
    Params p;
    p.Add("id", RELAY);
    p.Add("name", "Relay");
    p.Add("type", "RemoteUIOutputRelay");
    p.Add("remote_ui_id", SCREEN);
    p.Add("relay_num", "1");
    IOBase *relay = IOFactory::Instance().CreateIO("RemoteUIOutputRelay", p);
    ASSERT_NE(relay, nullptr);
    firstRoom()->AddIO(relay);
    ListeRoom::Instance().addIOHash(relay);

    send(firmwareGetConfig());
    const Json config = lastOfType("remote_ui_config");
    ASSERT_TRUE(config.is_object());
    EXPECT_TRUE(config.at("data").contains("pages"));

    const bool relayBefore = relay->get_value_bool();
    send(firmwareRelayState(1, !relayBefore));
    EXPECT_EQ(relay->get_value_bool(), !relayBefore);

    const std::string before = value(PLAIN);
    const std::string wanted = before == "true" ? "false" : "true";
    send(firmwareSetState(PLAIN, wanted));
    EXPECT_EQ(value(PLAIN), wanted);

    for (const std::string &s: sent)
        EXPECT_EQ(Json::parse(s, nullptr, false).at("data").value("error", ""), "") << s;
}

TEST_F(RemoteUIScopeTest, FramesWithoutAMessageTypeAreDropped)
{
    CoutCapture capture;
    send(R"({"msg":5})");
    send(R"(["set_state"])");
    EXPECT_TRUE(sent.empty());
    EXPECT_NE(capture.text().find("frame without a message type"), std::string::npos);
}

TEST_F(RemoteUIScopeTest, AHandlerWithoutAPanelServesNothing)
{
    handler->attach(nullptr);
    const std::string before = value(PLAIN);

    send(firmwareSetState(PLAIN, before == "true" ? "false" : "true"));
    EXPECT_EQ(value(PLAIN), before);
    expectForbidden("set_state", lastOfType("set_state"));

    pumpFor(300);
    sent.clear();
    ListeRoom::Instance().get_io(PLAIN)->set_value(!ListeRoom::Instance().get_io(PLAIN)->get_value_bool());
    pumpFor(300);
    EXPECT_FALSE(eventSeen(PLAIN));
}
