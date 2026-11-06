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

//Device base: <calaos:device> in a room, owned IOs registered in the
//room's flat list, links resolved once every room is loaded, nested save and
//cascade delete.

#include "CalaosCoreFixture.h"
#include "Device.h"
#include "InputTemp.h"
#include "IOChangeHub.h"
#include "JsonApi.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

//Records whether the links already resolve when the loader asks the device to
//check them: they may target an IO of a room loaded after the device's.
class TestDevice: public Device
{
public:
    TestDevice(Params &p):
        Device(p)
    {
    }

    void resolveLinks() override
    {
        Device::resolveLinks();
        resolveCalls++;
        externalResolvedAtLoad = linkIo("current_temp") != nullptr;
    }

    int resolveCalls = 0;
    bool externalResolvedAtLoad = false;
};

//InputTemp only exists through its drivers; this one never reads anything.
class TestTemp: public InputTemp
{
public:
    TestTemp(Params &p):
        InputTemp(p)
    {
    }

    void readValue() override {}
};

} //namespace

REGISTER_DEVICE(test_device, TestDevice)
REGISTER_IO(TestTemp)

namespace
{

const char *const DEV = "dev_salon";
const char *const OWNED_MODE = "dev_salon_mode";
const char *const OWNED_TARGET = "dev_salon_target";
const char *const EXT_TEMP = "ext_temp";
const char *const ROOM2 = "Kitchen";

std::string deviceXml(const std::string &id, const std::string &inner,
                      const std::string &impl = "test_device")
{
    return "    <calaos:device id=\"" + id + "\" type=\"climate\" impl=\"" + impl +
           "\" name=\"Clim\">\n" + inner + "    </calaos:device>\n";
}

//Hidden, as templates generate them.
std::string ownedIoXml(const std::string &type, const std::string &id, const std::string &name)
{
    return "      <calaos:internal type=\"" + type + "\" id=\"" + id + "\" name=\"" + name +
           "\" enabled=\"true\" visible=\"false\" />\n";
}

//InputTime, InPlageHoraire and InputAnalog register in the rule event loop
//from their constructor: an IO discarded without leaving it is polled freed.
std::string eventIoXml(const std::string &type, const std::string &id)
{
    return "      <calaos:input type=\"" + type + "\" id=\"" + id + "\" name=\"" + id + "\" />\n";
}

std::string subtree(pugi::xml_node node)
{
    std::ostringstream ss;
    node.print(ss, "", pugi::format_raw);
    return ss.str();
}

std::string linkXml(const std::string &name, const std::string &io,
                    const std::string &commandIo = std::string())
{
    std::string x = "      <calaos:link name=\"" + name + "\" io=\"" + io + "\"";
    if (!commandIo.empty())
        x += " command_io=\"" + commandIo + "\"";
    return x + " />\n";
}

std::string paramXml(const std::string &name, const std::string &value)
{
    return "      <calaos:param name=\"" + name + "\" value=\"" + value + "\" />\n";
}

//Owned IOs, links to them and to an external IO, two params in non sorted order.
std::string salonDevice()
{
    std::string inner;
    inner += ownedIoXml("InternalString", OWNED_MODE, "Mode");
    inner += ownedIoXml("InternalInt", OWNED_TARGET, "Target");
    inner += linkXml("mode", OWNED_MODE, OWNED_MODE);
    inner += linkXml("target_temp", OWNED_TARGET);
    inner += linkXml("current_temp", EXT_TEMP);
    inner += paramXml("hysteresis", "0.5");
    inner += paramXml("emitter", "floor");
    return deviceXml(DEV, inner);
}

//The device in the first room, between two room IOs; its external sensor in a
//second room, loaded after it.
std::string salonHouse()
{
    std::string ios;
    ios += internalIoXml("InternalBool", ID_BOOL_IN, "Bool input");
    ios += salonDevice();
    ios += internalIoXml("InternalBool", ID_BOOL_OUT, "Bool output");
    std::string rooms = roomXml(ROOM_NAME, ROOM_TYPE, ios);
    rooms += roomXml(ROOM2, "kitchen", internalIoXml("InternalInt", EXT_TEMP, "Temp"));
    return ioXmlDocument(rooms);
}

std::vector<std::string> ids(const std::vector<IOBase *> &ios)
{
    std::vector<std::string> res;
    for (IOBase *io: ios)
        res.push_back(io->get_param("id"));
    return res;
}

std::vector<std::string> roomIds(Room *room)
{
    std::vector<std::string> res;
    for (int i = 0; i < room->get_size(); i++)
        res.push_back(room->get_io(i)->get_param("id"));
    return res;
}

//Name and id of each child element, in document order.
std::vector<std::string> children(pugi::xml_node node)
{
    std::vector<std::string> res;
    for (pugi::xml_node c = node.first_child(); c; c = c.next_sibling())
    {
        std::string s = c.name();
        if (c.attribute("id"))
            s += "#" + std::string(c.attribute("id").as_string());
        else if (c.attribute("name"))
            s += "#" + std::string(c.attribute("name").as_string());
        res.push_back(s);
    }
    return res;
}

size_t countOf(const std::string &haystack, const std::string &needle)
{
    size_t n = 0;
    for (size_t pos = haystack.find(needle); pos != std::string::npos;
         pos = haystack.find(needle, pos + 1))
        n++;
    return n;
}

} //namespace

class DeviceTest: public CoreFixture
{
protected:
    static Device *device(const std::string &id = DEV)
    {
        return ListeRoom::Instance().findIOAs<Device>(id);
    }

    pugi::xml_node savedRoom(pugi::xml_document &doc, int index = 0)
    {
        saveConfig();
        EXPECT_TRUE(doc.load_string(ioXmlOnDisk().c_str()));
        pugi::xml_node room = doc.child("calaos:ioconfig").child("calaos:home").child("calaos:room");
        for (int i = 0; i < index; i++)
            room = room.next_sibling("calaos:room");
        return room;
    }
};

TEST_F(DeviceTest, OwnedIosJoinTheRoomFlatListRightAfterTheirDevice)
{
    loadConfig(salonHouse(), rulesXmlDocument(""));

    Room *room = findRoom(ROOM_NAME);
    ASSERT_NE(room, nullptr);
    EXPECT_EQ(roomIds(room), (std::vector<std::string>{ ID_BOOL_IN, DEV, OWNED_MODE,
                                                        OWNED_TARGET, ID_BOOL_OUT }));

    ASSERT_NE(device(), nullptr);
    EXPECT_EQ(device()->get_param("type"), "climate");
    EXPECT_EQ(device()->get_type(), TSTRING);
    EXPECT_EQ(ids(device()->ownedIos()), (std::vector<std::string>{ OWNED_MODE, OWNED_TARGET }));
    EXPECT_EQ(io(OWNED_MODE)->getOwnerDevice(), DEV);
    EXPECT_EQ(io(ID_BOOL_IN)->getOwnerDevice(), "");
    EXPECT_EQ(io(EXT_TEMP)->getOwnerDevice(), "");

    EXPECT_EQ(device()->linkIo("mode"), io(OWNED_MODE));
    EXPECT_EQ(device()->linkCommandIo("mode"), io(OWNED_MODE));
    EXPECT_EQ(device()->linkIo("target_temp"), io(OWNED_TARGET));
    EXPECT_EQ(device()->linkCommandIo("target_temp"), nullptr);
    EXPECT_EQ(device()->linkIo("fan_mode"), nullptr);
    EXPECT_EQ(device()->linkCommandIo("fan_mode"), nullptr);

    EXPECT_TRUE(device()->hasDeviceParam("emitter"));
    EXPECT_EQ(device()->getDeviceParam("hysteresis"), "0.5");
    EXPECT_FALSE(device()->hasDeviceParam("min_on_time"));
    EXPECT_EQ(device()->getDeviceParam("min_on_time"), "");
}

TEST_F(DeviceTest, ExternalLinkToALaterRoomResolvesBeforeTheRules)
{
    loadConfig(salonHouse(), rulesXmlDocument(""));

    auto *dev = dynamic_cast<TestDevice *>(device());
    ASSERT_NE(dev, nullptr);
    EXPECT_EQ(dev->resolveCalls, 1);
    EXPECT_TRUE(dev->externalResolvedAtLoad);
    EXPECT_EQ(dev->linkIo("current_temp"), io(EXT_TEMP));
    EXPECT_EQ(ListeRoom::Instance().getRoomByIO(io(EXT_TEMP)), findRoom(ROOM2));
}

TEST_F(DeviceTest, UnresolvedLinkLeavesTheCapabilityOutButLoadsTheDevice)
{
    std::string inner;
    inner += internalIoXml("InternalString", OWNED_MODE, "Mode");
    inner += linkXml("mode", OWNED_MODE, "no_such_cmd");
    inner += linkXml("current_temp", "no_such_sensor");
    loadConfig(ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE, deviceXml(DEV, inner))),
               rulesXmlDocument(""));

    ASSERT_NE(device(), nullptr);
    EXPECT_EQ(device()->linkIo("current_temp"), nullptr);
    EXPECT_EQ(device()->linkIo("mode"), io(OWNED_MODE));
    EXPECT_EQ(device()->linkCommandIo("mode"), nullptr);
    ASSERT_EQ(device()->getLinks().size(), 2u);

    //Kept as written: the IO may come back, and the file is the user's.
    pugi::xml_document doc;
    pugi::xml_node dev = savedRoom(doc).child("calaos:device");
    EXPECT_EQ(children(dev), (std::vector<std::string>{ "calaos:internal#" + std::string(OWNED_MODE),
                                                        "calaos:link#mode",
                                                        "calaos:link#current_temp" }));
    EXPECT_STREQ(dev.find_child_by_attribute("calaos:link", "name", "mode")
                     .attribute("command_io")
                     .as_string(),
                 "no_such_cmd");
    EXPECT_TRUE(roundTripIo());
}

TEST_F(DeviceTest, SaveNestsOwnedIosLinksAndParamsInFileOrder)
{
    loadConfig(salonHouse(), rulesXmlDocument(""));

    pugi::xml_document doc;
    pugi::xml_node room = savedRoom(doc);
    EXPECT_EQ(children(room), (std::vector<std::string>{ "calaos:internal#" + std::string(ID_BOOL_IN),
                                                         "calaos:device#" + std::string(DEV),
                                                         "calaos:internal#" + std::string(ID_BOOL_OUT) }));

    pugi::xml_node dev = room.child("calaos:device");
    EXPECT_STREQ(dev.attribute("impl").as_string(), "test_device");
    EXPECT_STREQ(dev.attribute("type").as_string(), "climate");
    EXPECT_EQ(children(dev), (std::vector<std::string>{ "calaos:internal#" + std::string(OWNED_MODE),
                                                        "calaos:internal#" + std::string(OWNED_TARGET),
                                                        "calaos:link#mode",
                                                        "calaos:link#target_temp",
                                                        "calaos:link#current_temp",
                                                        "calaos:param#hysteresis",
                                                        "calaos:param#emitter" }));
    EXPECT_FALSE(dev.find_child_by_attribute("calaos:link", "name", "target_temp")
                     .attribute("command_io"));
    EXPECT_STREQ(dev.find_child_by_attribute("calaos:param", "name", "emitter")
                     .attribute("value")
                     .as_string(),
                 "floor");
}

TEST_F(DeviceTest, RoundTripIsStable)
{
    loadConfig(salonHouse(), rulesXmlDocument(""));
    EXPECT_TRUE(roundTripIo());

    //The fixture's dump carries the owner: still nested after the reload.
    ASSERT_NE(device(), nullptr);
    EXPECT_EQ(ids(device()->ownedIos()), (std::vector<std::string>{ OWNED_MODE, OWNED_TARGET }));
    EXPECT_EQ(device()->getDeviceParam("emitter"), "floor");
}

TEST_F(DeviceTest, OwnedIoIsSeenByRulesLikeAnyIo)
{
    loadConfig(salonHouse(),
               rulesXmlDocument(simpleRuleXml("owned_rule", OWNED_TARGET, "==", "21",
                                              ID_BOOL_OUT, "true")));

    ASSERT_EQ(ListeRule::Instance().size(), 1);
    Rule *rule = findRule("owned_rule");
    ASSERT_NE(rule, nullptr);
    EXPECT_FALSE(rule->isDisabled());

    io(OWNED_TARGET)->set_value(21.0);
    EXPECT_TRUE(io(ID_BOOL_OUT)->get_value_bool());
}

TEST_F(DeviceTest, OwnedIosAndTheDeviceAreGetHomeItems)
{
    loadConfig(salonHouse(), rulesXmlDocument(""));

    JsonApi api;
    Json home = api.buildJsonHome();
    ASSERT_TRUE(home.is_array());
    ASSERT_EQ(home.size(), 2u);

    std::vector<std::string> items;
    for (const Json &it: home[0]["items"])
        items.push_back(it["id"].get<std::string>());
    EXPECT_EQ(items, (std::vector<std::string>{ ID_BOOL_IN, DEV, OWNED_MODE,
                                                OWNED_TARGET, ID_BOOL_OUT }));
    EXPECT_EQ(home[0]["items"][2]["visible"], "false");
}

//Clients list only the items whose visible is "true": a device written without
//the attribute, as the spec's examples and the installer's wizard do, is shown.
TEST_F(DeviceTest, ADeviceIsShownUnlessItsFileHidesIt)
{
    loadConfig(salonHouse(), rulesXmlDocument(""));

    Json home = JsonApi().buildJsonHome();
    ASSERT_EQ(home[0]["items"][1]["id"], DEV);
    EXPECT_EQ(home[0]["items"][1]["visible"], "true");
}

TEST_F(DeviceTest, ADeviceHiddenByItsFileStaysHidden)
{
    std::string house = salonHouse();
    const std::string tag = "name=\"Clim\">";
    house.replace(house.find(tag), tag.size(), "name=\"Clim\" visible=\"false\">");
    loadConfig(house, rulesXmlDocument(""));

    Json home = JsonApi().buildJsonHome();
    ASSERT_EQ(home[0]["items"][1]["id"], DEV);
    EXPECT_EQ(home[0]["items"][1]["visible"], "false");
}

TEST_F(DeviceTest, DeletingTheDeviceDeletesItsOwnedIosOnly)
{
    loadConfig(salonHouse(),
               rulesXmlDocument(simpleRuleXml("owned_rule", OWNED_TARGET, "==", "21",
                                              ID_BOOL_OUT, "true") +
                                simpleRuleXml("ext_rule", EXT_TEMP, "==", "1",
                                              ID_BOOL_OUT, "true")));

    ASSERT_TRUE(deleteIO(device()));

    EXPECT_EQ(io(DEV), nullptr);
    EXPECT_EQ(io(OWNED_MODE), nullptr);
    EXPECT_EQ(io(OWNED_TARGET), nullptr);
    EXPECT_NE(io(EXT_TEMP), nullptr);
    EXPECT_EQ(roomIds(findRoom(ROOM_NAME)), (std::vector<std::string>{ ID_BOOL_IN, ID_BOOL_OUT }));

    Rule *rule = findRule("owned_rule");
    ASSERT_NE(rule, nullptr);
    EXPECT_TRUE(rule->isDisabled());
    EXPECT_EQ(rule->getMissingIoIds(), (std::vector<std::string>{ OWNED_TARGET }));
    ASSERT_NE(findRule("ext_rule"), nullptr);
    EXPECT_FALSE(findRule("ext_rule")->isDisabled());

    saveConfig();
    const std::string file = ioXmlOnDisk();
    EXPECT_EQ(file.find("calaos:device"), std::string::npos);
    EXPECT_EQ(file.find(OWNED_MODE), std::string::npos);
    EXPECT_NE(file.find(EXT_TEMP), std::string::npos);
}

TEST_F(DeviceTest, DeletingTheDeviceIsRefusedWhileRulesRun)
{
    loadConfig(salonHouse(), rulesXmlDocument(""));

    bool deleted = true;
    auto sub = IOChangeHub::Instance().subscribe(ID_BOOL_IN, [&deleted](IOBase &)
                                                 {
        deleted = ListeRoom::Instance().deleteIO(io(DEV));
    });

    io(ID_BOOL_IN)->set_value(true);

    EXPECT_FALSE(deleted);
    EXPECT_NE(io(DEV), nullptr);
    EXPECT_NE(io(OWNED_MODE), nullptr);
    EXPECT_NE(io(OWNED_TARGET), nullptr);
}

//An owned IO whose device went without the cascade (delete_io() does not
//cascade) is written at room level instead of being lost.
TEST_F(DeviceTest, OwnedIoOutlivingItsDeviceIsSavedInTheRoom)
{
    loadConfig(salonHouse(), rulesXmlDocument(""));

    ASSERT_TRUE(ListeRoom::Instance().delete_io(device()));
    ASSERT_NE(io(OWNED_MODE), nullptr);

    pugi::xml_document doc;
    pugi::xml_node room = savedRoom(doc);
    EXPECT_EQ(children(room), (std::vector<std::string>{ "calaos:internal#" + std::string(ID_BOOL_IN),
                                                         "calaos:internal#" + std::string(OWNED_MODE),
                                                         "calaos:internal#" + std::string(OWNED_TARGET),
                                                         "calaos:internal#" + std::string(ID_BOOL_OUT) }));

    reloadFromDisk();
    ASSERT_NE(io(OWNED_MODE), nullptr);
    EXPECT_EQ(io(OWNED_MODE)->getOwnerDevice(), "");
}

//Same as two room IOs sharing an id: both stay in the room, the first keeps
//the id, and both are saved where they were declared.
TEST_F(DeviceTest, OwnedIoDuplicatingARoomIoIdKeepsTodaysDuplicateHandling)
{
    std::string ios;
    ios += internalIoXml("InternalBool", "dup_id", "Room one");
    ios += deviceXml(DEV, internalIoXml("InternalInt", "dup_id", "Owned one"));
    loadConfig(ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE, ios)), rulesXmlDocument(""));

    Room *room = firstRoom();
    ASSERT_EQ(room->get_size(), 3);
    EXPECT_EQ(io("dup_id")->get_param("name"), "Room one");
    EXPECT_EQ(io("dup_id")->getOwnerDevice(), "");
    EXPECT_EQ(room->get_io(2)->getOwnerDevice(), DEV);
    EXPECT_EQ(device()->ownedIos(), (std::vector<IOBase *>{ room->get_io(2) }));

    pugi::xml_document doc;
    pugi::xml_node saved = savedRoom(doc);
    EXPECT_EQ(children(saved), (std::vector<std::string>{ "calaos:internal#dup_id",
                                                          "calaos:device#" + std::string(DEV) }));
    EXPECT_EQ(children(saved.child("calaos:device")),
              (std::vector<std::string>{ "calaos:internal#dup_id" }));
    EXPECT_TRUE(roundTripIo());
}

//The device holding the id owns the IOs tagged with it; the shadowed one
//claims none, so they are saved once and not once per device.
TEST_F(DeviceTest, DuplicateDeviceIdSavesEachOwnedIoOnce)
{
    std::string ios;
    ios += deviceXml(DEV, internalIoXml("InternalInt", "owned_a", "A"));
    ios += deviceXml(DEV, internalIoXml("InternalInt", "owned_b", "B"));
    loadConfig(ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE, ios)), rulesXmlDocument(""));

    Room *room = firstRoom();
    ASSERT_EQ(room->get_size(), 4);
    auto *first = dynamic_cast<Device *>(room->get_io(0));
    auto *shadowed = dynamic_cast<Device *>(room->get_io(2));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(shadowed, nullptr);
    EXPECT_EQ(device(), first);
    EXPECT_EQ(ids(first->ownedIos()), (std::vector<std::string>{ "owned_a", "owned_b" }));
    EXPECT_TRUE(shadowed->ownedIos().empty());

    saveConfig();
    const std::string file = ioXmlOnDisk();
    EXPECT_EQ(countOf(file, "id=\"owned_a\""), 1u);
    EXPECT_EQ(countOf(file, "id=\"owned_b\""), 1u);

    //Regrouped under the first device by the first save, stable from there.
    reloadFromDisk();
    EXPECT_TRUE(roundTripIo());
}

TEST_F(DeviceTest, NestedDevicesAndUnknownOwnedTypesAreDropped)
{
    std::string inner;
    inner += deviceXml("inner_dev", eventIoXml("InPlageHoraire", "inner_owned"));
    inner += "      <calaos:device id=\"inner_no_impl\" type=\"InputTime\" name=\"Z\" />\n";
    inner += "      <calaos:input id=\"inner_impl_on_io\" type=\"InputTime\" impl=\"test_device\" name=\"W\">\n" +
             eventIoXml("TestTemp", "inner_impl_owned") + "      </calaos:input>\n";
    inner += internalIoXml("NoSuchType", "unknown_owned", "Unknown");
    inner += eventIoXml("InputTime", OWNED_TARGET);
    loadConfig(ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE, deviceXml(DEV, inner))),
               rulesXmlDocument(""));

    for (const char *id: { "inner_dev", "inner_owned", "inner_no_impl", "inner_impl_on_io",
                           "inner_impl_owned", "unknown_owned" })
        EXPECT_EQ(io(id), nullptr) << id;
    EXPECT_EQ(roomIds(firstRoom()), (std::vector<std::string>{ DEV, OWNED_TARGET }));
    EXPECT_EQ(io(OWNED_TARGET)->getOwnerDevice(), DEV);

    //Only the kept IO polls: the refused ones left the loop before dying.
    EXPECT_EQ(ListeRule::Instance().eventCount(), 1u);
    ListeRule::Instance().RunEventLoop();
}

//"impl" selects the factory key, so the element and the class may disagree.
TEST_F(DeviceTest, ElementAndClassMustAgreeOnBeingADevice)
{
    std::string ios;
    ios += "    <calaos:device id=\"no_impl\" type=\"InPlageHoraire\" name=\"X\" />\n";
    ios += "    <calaos:input id=\"impl_on_io\" type=\"InputTime\" impl=\"test_device\" name=\"Y\">\n" +
           eventIoXml("InputTime", "impl_owned_time") + eventIoXml("TestTemp", "impl_owned_temp") +
           "    </calaos:input>\n";
    ios += eventIoXml("InputTime", "kept_time");
    loadConfig(ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE, ios)), rulesXmlDocument(""));

    for (const char *id: { "no_impl", "impl_on_io", "impl_owned_time", "impl_owned_temp" })
        EXPECT_EQ(io(id), nullptr) << id;
    EXPECT_EQ(roomIds(firstRoom()), (std::vector<std::string>{ "kept_time" }));

    EXPECT_EQ(ListeRule::Instance().eventCount(), 1u);
    ListeRule::Instance().RunEventLoop();
}

//A downgrade within format 2: the device is inert, never instantiated, and
//written back as it was read.
TEST_F(DeviceTest, UnknownImplIsKeptVerbatimAndInert)
{
    std::string future;
    future += eventIoXml("InputTime", "future_owned");
    future += linkXml("mode", "future_owned");
    future += linkXml("current_temp", ID_BOOL_IN);
    future += paramXml("hysteresis", "0.5");

    std::string ios;
    ios += internalIoXml("InternalBool", ID_BOOL_IN, "Bool input");
    ios += deviceXml("future_dev", future, "future_impl");
    ios += internalIoXml("InternalBool", ID_BOOL_OUT, "Bool output");
    ios += "    <calaos:device id=\"no_impl_dev\" type=\"climate\" name=\"N\" />\n";
    const std::string source = ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE, ios));
    loadConfig(source, rulesXmlDocument(""));

    EXPECT_EQ(io("future_dev"), nullptr);
    EXPECT_EQ(io("future_owned"), nullptr);
    EXPECT_EQ(io("no_impl_dev"), nullptr);
    EXPECT_EQ(roomIds(firstRoom()), (std::vector<std::string>{ ID_BOOL_IN, ID_BOOL_OUT }));
    EXPECT_EQ(ListeRule::Instance().eventCount(), 0u);

    JsonApi api;
    Json home = api.buildJsonHome();
    ASSERT_EQ(home.size(), 1u);
    EXPECT_EQ(home[0]["items"].size(), 2u);

    pugi::xml_document src;
    ASSERT_TRUE(src.load_string(source.c_str()));
    pugi::xml_node srcRoom = src.child("calaos:ioconfig").child("calaos:home").child("calaos:room");

    pugi::xml_document doc;
    pugi::xml_node room = savedRoom(doc);
    EXPECT_EQ(children(room), (std::vector<std::string>{ "calaos:internal#" + std::string(ID_BOOL_IN),
                                                         "calaos:device#future_dev",
                                                         "calaos:internal#" + std::string(ID_BOOL_OUT),
                                                         "calaos:device#no_impl_dev" }));
    EXPECT_EQ(subtree(room.find_child_by_attribute("calaos:device", "id", "future_dev")),
              subtree(srcRoom.find_child_by_attribute("calaos:device", "id", "future_dev")));
    EXPECT_EQ(subtree(room.find_child_by_attribute("calaos:device", "id", "no_impl_dev")),
              subtree(srcRoom.find_child_by_attribute("calaos:device", "id", "no_impl_dev")));
    EXPECT_TRUE(roundTripIo());
}

//The factory needs "impl" to rebuild a device: the model refuses to drop it.
TEST_F(DeviceTest, ImplIsAStructuralParam)
{
    loadConfig(salonHouse(), rulesXmlDocument(""));

    EXPECT_FALSE(device()->del_param("impl"));
    EXPECT_FALSE(device()->set_param("impl", "no_such_impl"));
    EXPECT_TRUE(device()->set_param("type", "heater"));
    EXPECT_FALSE(io(ID_BOOL_IN)->set_param("impl", "test_device"));
    EXPECT_EQ(device()->get_param("impl"), "test_device");

    //Assembled outside the loader: there is nothing to strand, nothing frozen.
    Params p = { { "id", "hand_made" }, { "type", "climate" } };
    Device handMade(p);
    EXPECT_TRUE(handMade.set_param("impl", "test_device"));
}
