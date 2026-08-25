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

/******************************************************************************
 * T3.15 — the RemoteUI <calaos:device_info> round trip.
 *
 * The bug this suite closes: SaveToXml() attached <calaos:device_info> to the
 * node it is given, which is the ROOM element, while LoadFromXml() has always
 * read it inside <calaos:remote_ui>. Writer and reader never met, so what was
 * saved was never read back — and nobody noticed because no test ever went
 * through a full save/reload of a device_info. That is exactly what is
 * asserted here, on the real code path (Config::LoadConfigIO/SaveConfigIO,
 * Room, IOFactory), never on a hand built node:
 *
 *   1. a device_info in its documented place survives save -> reload, value
 *      for value, and lands inside <calaos:remote_ui> in the file;
 *   2. the legacy shape produced by the broken writer (device_info as the
 *      previous sibling of the remote_ui, under the room) is adopted once at
 *      load and re-saved in the right place;
 *   3. that migration cannot steal an orphan from another device of the same
 *      room, and does not invent one;
 *   4. an empty device_info still writes no element at all.
 *
 * The payload carries UTF-8 and the XML specials on purpose: "identical" here
 * means the strings themselves, not a shape that happens to survive.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <pugixml.hpp>

#include "CalaosCoreFixture.h"

#include "IO/RemoteUI/RemoteUI.h"
#include "IO/RemoteUI/RemoteUIOutputRelay.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char *const DEVICE_A = "remote_ui_A";
const char *const DEVICE_B = "remote_ui_B";

//A device_info as a device reports it at provisioning time, plus the two
//things a serializer gets wrong: accented UTF-8 and the XML specials.
std::string deviceInfoXml(const std::string &model)
{
    return std::string("      <calaos:device_info"
                       " type=\"esp32_touch_panel\""
                       " manufacturer=\"Calaos\""
                       " model=\"") + model + "\""
           " firmware=\"1.2.3\""
           " mac_address=\"AA:BB:CC:DD:EE:FF\""
           " capabilities=\"{&apos;screen&apos;:&apos;480x320&apos;}\""
           " />\n";
}

Json expectedDeviceInfo(const std::string &model)
{
    Json j = Json::object();
    j["type"] = "esp32_touch_panel";
    j["manufacturer"] = "Calaos";
    j["model"] = model;
    j["firmware"] = "1.2.3";
    j["mac_address"] = "AA:BB:CC:DD:EE:FF";
    j["capabilities"] = "{'screen':'480x320'}";
    return j;
}

//`inner` goes inside <calaos:remote_ui>, `before` right before it (under the
//room), which is where the broken writer used to put the device_info.
std::string remoteUiXml(const std::string &id,
                        const std::string &inner = std::string(),
                        const std::string &before = std::string())
{
    std::string x = before;
    x += "    <calaos:remote_ui type=\"RemoteUI\" id=\"" + id + "\"";
    x += " name=\"Screen " + id + "\"";
    x += " enabled=\"true\" visible=\"true\" device_type=\"waveshare-86-panel\"";
    x += " grid_w=\"3\" grid_h=\"3\">\n";
    x += inner;
    x += "    </calaos:remote_ui>\n";
    return x;
}

RemoteUI *remoteUi(const std::string &id)
{
    return dynamic_cast<RemoteUI *>(ListeRoom::Instance().get_io(id));
}

//Every <calaos:device_info> of a document, with the name and the id of the
//element that owns it. pugixml's XPath has no namespace support, so the tree
//is walked by hand rather than queried.
struct DeviceInfoPlacement
{
    std::string parentName;
    std::string parentId;
};

void collectDeviceInfo(const pugi::xml_node &node, std::vector<DeviceInfoPlacement> &out)
{
    for (pugi::xml_node c = node.first_child(); c; c = c.next_sibling())
    {
        if (c.type() != pugi::node_element)
            continue;

        if (std::string(c.name()) == "calaos:device_info")
        {
            DeviceInfoPlacement p;
            p.parentName = node.name();
            p.parentId = node.attribute("id").value();
            out.push_back(p);
        }

        collectDeviceInfo(c, out);
    }
}

std::vector<DeviceInfoPlacement> deviceInfoPlacements(const std::string &xml)
{
    pugi::xml_document doc;
    EXPECT_TRUE(doc.load_string(xml.c_str())) << "the saved io.xml does not parse";

    std::vector<DeviceInfoPlacement> out;
    collectDeviceInfo(doc, out);
    return out;
}

}

class RemoteUIDeviceInfoTest: public CoreFixture
{
protected:
    void loadIo(const std::string &roomsXml)
    {
        //No rule: the default rules.xml references the ids of minimalIoXml(),
        //which this config does not declare.
        loadConfig(ioXmlDocument(roomsXml), rulesXmlDocument(""));
    }
};

/******************************************************************************
 * 1. The round trip nobody had ever tested.
 ******************************************************************************/
TEST_F(RemoteUIDeviceInfoTest, DeviceInfoSurvivesSaveAndReload)
{
    const std::string model = "TouchPanel « salon » & <v1>";

    loadIo(roomXml("Salon", "livingroom", remoteUiXml(DEVICE_A, deviceInfoXml(model))));

    RemoteUI *ui = remoteUi(DEVICE_A);
    ASSERT_NE(ui, nullptr) << "the RemoteUI IO was not created";
    ASSERT_EQ(ui->getDeviceInfo().dump(), expectedDeviceInfo(model).dump());

    saveConfig();

    //Written where the reader looks: inside <calaos:remote_ui>, and nowhere else
    const std::vector<DeviceInfoPlacement> placements = deviceInfoPlacements(ioXmlOnDisk());
    ASSERT_EQ(placements.size(), 1u) << ioXmlOnDisk();
    EXPECT_EQ(placements[0].parentName, "calaos:remote_ui");
    EXPECT_EQ(placements[0].parentId, DEVICE_A);

    reloadFromDisk();

    RemoteUI *reloaded = remoteUi(DEVICE_A);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(reloaded->getDeviceInfo().dump(), expectedDeviceInfo(model).dump())
        << "device_info did not come back identical";

    //And it is stable: a second cycle changes nothing
    saveConfig();
    reloadFromDisk();
    ASSERT_NE(remoteUi(DEVICE_A), nullptr);
    EXPECT_EQ(remoteUi(DEVICE_A)->getDeviceInfo().dump(), expectedDeviceInfo(model).dump());
    EXPECT_EQ(deviceInfoPlacements(ioXmlOnDisk()).size(), 1u);
}

/******************************************************************************
 * 2. Migration of the configs written by the broken writer.
 ******************************************************************************/
TEST_F(RemoteUIDeviceInfoTest, LegacyDeviceInfoUnderRoomIsAdoptedAndRewritten)
{
    const std::string model = "TouchPanel v1.0";

    //Exactly what SaveToXml() used to produce: the element under the room,
    //immediately before the device it describes.
    loadIo(roomXml("Salon", "livingroom",
                   remoteUiXml(DEVICE_A, std::string(), deviceInfoXml(model))));

    RemoteUI *ui = remoteUi(DEVICE_A);
    ASSERT_NE(ui, nullptr);
    EXPECT_EQ(ui->getDeviceInfo().dump(), expectedDeviceInfo(model).dump())
        << "the orphan left under the room node was not adopted";

    saveConfig();

    const std::vector<DeviceInfoPlacement> placements = deviceInfoPlacements(ioXmlOnDisk());
    ASSERT_EQ(placements.size(), 1u) << ioXmlOnDisk();
    EXPECT_EQ(placements[0].parentName, "calaos:remote_ui")
        << "the migrated device_info was not rewritten inside <calaos:remote_ui>";
    EXPECT_EQ(placements[0].parentId, DEVICE_A);

    reloadFromDisk();

    ASSERT_NE(remoteUi(DEVICE_A), nullptr);
    EXPECT_EQ(remoteUi(DEVICE_A)->getDeviceInfo().dump(), expectedDeviceInfo(model).dump());
}

/******************************************************************************
 * 3. The migration attributes the orphan to the right device, and only to it.
 ******************************************************************************/
TEST_F(RemoteUIDeviceInfoTest, LegacyOrphanGoesToItsOwnDeviceOnly)
{
    const std::string model = "TouchPanel B";

    //A room with two devices, only the second one had a device_info: the file
    //reads remote_ui(A), device_info(B), remote_ui(B).
    std::string ios = remoteUiXml(DEVICE_A);
    ios += remoteUiXml(DEVICE_B, std::string(), deviceInfoXml(model));

    loadIo(roomXml("Salon", "livingroom", ios));

    ASSERT_NE(remoteUi(DEVICE_A), nullptr);
    ASSERT_NE(remoteUi(DEVICE_B), nullptr);

    EXPECT_TRUE(remoteUi(DEVICE_A)->getDeviceInfo().empty())
        << "a device without device_info adopted the orphan of another one: "
        << remoteUi(DEVICE_A)->getDeviceInfo().dump();
    EXPECT_EQ(remoteUi(DEVICE_B)->getDeviceInfo().dump(), expectedDeviceInfo(model).dump());

    saveConfig();

    const std::vector<DeviceInfoPlacement> placements = deviceInfoPlacements(ioXmlOnDisk());
    ASSERT_EQ(placements.size(), 1u) << ioXmlOnDisk();
    EXPECT_EQ(placements[0].parentName, "calaos:remote_ui");
    EXPECT_EQ(placements[0].parentId, DEVICE_B);
}

/******************************************************************************
 * 4. No device_info, no element: the fix invents nothing.
 ******************************************************************************/
TEST_F(RemoteUIDeviceInfoTest, NoDeviceInfoWritesNoElement)
{
    loadIo(roomXml("Salon", "livingroom", remoteUiXml(DEVICE_A)));

    ASSERT_NE(remoteUi(DEVICE_A), nullptr);
    EXPECT_TRUE(remoteUi(DEVICE_A)->getDeviceInfo().empty());

    saveConfig();
    EXPECT_TRUE(deviceInfoPlacements(ioXmlOnDisk()).empty()) << ioXmlOnDisk();

    //The rest of the IO is untouched by all this
    EXPECT_TRUE(roundTripIo());
}

/*******************************************************************************
 * T3.25 (review reserve 1) - relay_num, which had no oracle at all.
 *
 * RemoteUIOutputRelay.o has been linked into this binary since T3.15, but the
 * class was never instantiated by any test: the review measured that the token
 * `relay_num` appears in NO file of tests/. Its constructor is the ONE
 * from_string_or_keep() site of the RemoteUI family and there is no Exists()
 * guard in front of it, so before T3.25 an absent or blank "relay_num" made a
 * plain from_string() write 0 - a relay number the ioDoc rules out (1..99) -
 * over the in-class default of 1, and the wrong relay was then sent on the wire
 * by set_value_real().
 *
 * The parsed value has no other observable (see the comment on getRelayNum()),
 * so these three cases are what stands between that line and the next
 * refactoring. The third one is the control: `_or_keep` must still let a
 * configured relay number WIN, or "keep the default" would be satisfied by
 * never reading the parameter.
 ******************************************************************************/
namespace
{

RemoteUIOutputRelay *makeRelay(const std::string &id, bool withParam,
                               const std::string &relayNum)
{
    Params p = {{ "type", "RemoteUIOutputRelay" },
                { "id", id },
                { "name", "Relay under test" },
                { "remote_ui_id", DEVICE_A },
                { "enabled", "true" },
                { "visible", "true" }};
    if (withParam)
        p.Add("relay_num", relayNum);

    return dynamic_cast<RemoteUIOutputRelay *>(CoreFixture::createIO(p));
}

}

TEST_F(RemoteUIDeviceInfoTest, AnAbsentRelayNumKeepsTheDocumentedFirstRelay)
{
    loadConfig();

    RemoteUIOutputRelay *relay = makeRelay("t325_relay_absent", false, "");
    ASSERT_NE(nullptr, relay);
    EXPECT_EQ(1, relay->getRelayNum())
            << "an absent relay_num drove relay " << relay->getRelayNum()
            << ", a relay the ioDoc says does not exist";
}

TEST_F(RemoteUIDeviceInfoTest, ABlankRelayNumKeepsTheDocumentedFirstRelay)
{
    loadConfig();

    RemoteUIOutputRelay *relay = makeRelay("t325_relay_blank", true, "");
    ASSERT_NE(nullptr, relay);
    EXPECT_EQ(1, relay->getRelayNum())
            << "a present but blank relay_num drove relay " << relay->getRelayNum()
            << "; RemoteUIOutputRelay.cpp:47 no longer protects the default";
}

TEST_F(RemoteUIDeviceInfoTest, AConfiguredRelayNumIsStillTheOneUsed)
{
    //GREEN BEFORE AND AFTER: the control of the three.
    loadConfig();

    RemoteUIOutputRelay *relay = makeRelay("t325_relay_three", true, "3");
    ASSERT_NE(nullptr, relay);
    EXPECT_EQ(3, relay->getRelayNum());
}
