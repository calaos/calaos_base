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
//createIO() is protected on the fixture, so the helper lives on a fixture too.
class RemoteUIRelayNumTest: public RemoteUIDeviceInfoTest
{
protected:
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

        return dynamic_cast<RemoteUIOutputRelay *>(createIO(p));
    }
};

TEST_F(RemoteUIRelayNumTest, AnAbsentRelayNumKeepsTheDocumentedFirstRelay)
{
    loadConfig();

    RemoteUIOutputRelay *relay = makeRelay("t325_relay_absent", false, "");
    ASSERT_NE(nullptr, relay);
    EXPECT_EQ(1, relay->getRelayNum())
            << "an absent relay_num drove relay " << relay->getRelayNum()
            << ", a relay the ioDoc says does not exist";
}

TEST_F(RemoteUIRelayNumTest, ABlankRelayNumKeepsTheDocumentedFirstRelay)
{
    loadConfig();

    RemoteUIOutputRelay *relay = makeRelay("t325_relay_blank", true, "");
    ASSERT_NE(nullptr, relay);
    EXPECT_EQ(1, relay->getRelayNum())
            << "a present but blank relay_num drove relay " << relay->getRelayNum()
            << "; RemoteUIOutputRelay.cpp:47 no longer protects the default";
}

TEST_F(RemoteUIRelayNumTest, AConfiguredRelayNumIsStillTheOneUsed)
{
    //GREEN BEFORE AND AFTER: the control of the three.
    loadConfig();

    RemoteUIOutputRelay *relay = makeRelay("t325_relay_three", true, "3");
    ASSERT_NE(nullptr, relay);
    EXPECT_EQ(3, relay->getRelayNum());
}

/*******************************************************************************
 * T3.70 - the widget coordinates, read with std::stoi and nothing around it.
 *
 * LoadFromXml() converted the x/y attributes of a <calaos:widget> with a bare
 * std::stoi(). It runs under Config::LoadConfigIO(), which main() calls with no
 * try anywhere on the path: an x="" or an x="haut" in the io.xml threw out of
 * main() and the server never reached its event loop. Whatever the reason for
 * the typo - a hand edited file, a config upload, an older writer - the whole
 * installation went down, not just the screen.
 *
 * The file already has an answer for a widget it cannot use: the check right
 * after the attribute loop drops a widget that has no type or no x/y, with a
 * warning. These cases pin that a coordinate that is not a whole number lands
 * there and nowhere else - the widget goes, the page, the screen, the rooms
 * declared after it and the surviving widgets all stay - and that the user is
 * told, because the next save writes the page back without the dropped widget.
 ******************************************************************************/

namespace
{

const char *const DEV_BAD = "t370_screen_bad";
const char *const DEV_AFTER = "t370_screen_after";

//One <calaos:widget>. `x` is injected verbatim so a test can put anything in it.
std::string widgetXml(const std::string &type, const std::string &ioId,
                      const std::string &x, const std::string &y)
{
    return std::string("        <calaos:widget type=\"") + type + "\""
           " io_id=\"" + ioId + "\""
           " x=\"" + x + "\" y=\"" + y + "\" w=\"100\" h=\"50\"/>\n";
}

std::string pagesXml(const std::string &widgets, const std::string &pageName = "Home")
{
    std::string x = "      <calaos:pages>\n";
    x += "        <calaos:page id=\"1\" name=\"" + pageName + "\">\n";
    x += widgets;
    x += "        </calaos:page>\n";
    x += "      </calaos:pages>\n";
    return x;
}

//The io_ids of the widgets a page kept, in file order.
std::vector<std::string> widgetIoIds(const RemoteUI *ui, size_t pageIndex = 0)
{
    std::vector<std::string> out;
    const Json &pages = ui->getPages();
    if (!pages.is_array() || pages.size() <= pageIndex)
        return out;

    const Json &page = pages[pageIndex];
    if (!page.contains("widgets") || !page["widgets"].is_array())
        return out;

    for (const auto &w: page["widgets"])
    {
        if (w.contains("io_id") && w["io_id"].is_string())
            out.push_back(w["io_id"].get<std::string>());
    }
    return out;
}

const Json *widgetOf(const RemoteUI *ui, const std::string &ioId)
{
    const Json &pages = ui->getPages();
    if (!pages.is_array())
        return nullptr;

    for (const auto &page: pages)
    {
        if (!page.contains("widgets") || !page["widgets"].is_array())
            continue;
        for (const auto &w: page["widgets"])
        {
            if (w.contains("io_id") && w["io_id"] == ioId)
                return &w;
        }
    }
    return nullptr;
}

}

class RemoteUIWidgetCoordinateTest: public RemoteUIDeviceInfoTest
{
protected:
    //A screen whose middle widget carries `badX`, then a SECOND ROOM with a
    //second screen: everything the aborted load took away with it.
    std::string configWithBadX(const std::string &badX)
    {
        std::string widgets = widgetXml("button", "t370_io_first", "10", "10");
        widgets += widgetXml("thermostat", "t370_io_broken", badX, "40");
        widgets += widgetXml("temp_display", "t370_io_last", "120", "10");

        std::string rooms = roomXml("Salon", "livingroom",
                                    remoteUiXml(DEV_BAD, pagesXml(widgets)));
        rooms += roomXml("Cuisine", "kitchen", remoteUiXml(DEV_AFTER));
        return rooms;
    }
};

/******************************************************************************
 * 1. The failure mode: the configuration load itself.
 ******************************************************************************/
TEST_F(RemoteUIWidgetCoordinateTest, AnUnreadableWidgetCoordinateDoesNotAbortTheConfigLoad)
{
    //Non fatal on purpose: swallowing the throw here is what lets the two
    //checks below say what the aborted load took away with it.
    EXPECT_NO_THROW(loadIo(configWithBadX("haut")))
        << "x=\"haut\" threw out of Config::LoadConfigIO(); main() has no try "
           "on that path, so the server aborts before its event loop";

    EXPECT_NE(remoteUi(DEV_BAD), nullptr)
        << "the screen holding the misspelled widget was lost whole";
    EXPECT_NE(remoteUi(DEV_AFTER), nullptr)
        << "the screen of the NEXT ROOM was lost too: the load stopped there";
}

TEST_F(RemoteUIWidgetCoordinateTest, ABlankWidgetCoordinateDoesNotAbortTheConfigLoad)
{
    EXPECT_NO_THROW(loadIo(configWithBadX("")))
        << "x=\"\" threw out of Config::LoadConfigIO()";

    EXPECT_NE(remoteUi(DEV_BAD), nullptr);
    EXPECT_NE(remoteUi(DEV_AFTER), nullptr);
}

/******************************************************************************
 * 2. What becomes of the widget: dropped, and it alone.
 ******************************************************************************/
TEST_F(RemoteUIWidgetCoordinateTest, OnlyTheWidgetWithTheUnreadableCoordinateIsDropped)
{
    ASSERT_NO_THROW(loadIo(configWithBadX("haut")));

    RemoteUI *ui = remoteUi(DEV_BAD);
    ASSERT_NE(ui, nullptr);

    //By identity, not by count: "the load did not crash" must not be enough.
    const std::vector<std::string> kept = widgetIoIds(ui);
    EXPECT_EQ(kept, (std::vector<std::string>{"t370_io_first", "t370_io_last"}))
        << "the page did not keep exactly its two well formed widgets";

    EXPECT_EQ(widgetOf(ui, "t370_io_broken"), nullptr)
        << "the widget with x=\"haut\" was kept; its x is then whatever stoi "
           "left behind, and the device gets a position nobody wrote";

    //The reference index follows the widget out.
    EXPECT_TRUE(ui->hasReferencedIO("t370_io_first"));
    EXPECT_TRUE(ui->hasReferencedIO("t370_io_last"));
    EXPECT_FALSE(ui->hasReferencedIO("t370_io_broken"));

    //The survivors keep NUMBER coordinates: a widget whose x came back as a
    //string would still reach the device, with the wrong JSON type.
    const Json *first = widgetOf(ui, "t370_io_first");
    ASSERT_NE(first, nullptr);
    EXPECT_TRUE((*first)["x"].is_number_integer()) << first->dump();
    EXPECT_EQ((*first)["x"].get<int>(), 10);
    EXPECT_EQ((*first)["y"].get<int>(), 10);
}

/******************************************************************************
 * 3. The user hears about it: the widget does not come back on the next save.
 ******************************************************************************/
TEST_F(RemoteUIWidgetCoordinateTest, ADroppedWidgetIsReportedOnTheConfigAlertChannel)
{
    const size_t alertsBefore = Config::Instance().getConfigAlerts().size();

    ASSERT_NO_THROW(loadIo(configWithBadX("haut")));

    const std::vector<std::string> &alerts = Config::Instance().getConfigAlerts();
    ASSERT_EQ(alerts.size(), alertsBefore + 1)
        << "a screen silently lost a widget: nothing was queued on the mail/push "
           "channel the rest of the configuration load uses";

    const std::string &report = alerts.back();
    EXPECT_NE(report.find(DEV_BAD), std::string::npos)
        << "the alert does not name the screen: " << report;
    EXPECT_NE(report.find("t370_io_broken"), std::string::npos)
        << "the alert does not name the widget: " << report;
    EXPECT_NE(report.find("Home"), std::string::npos)
        << "the alert does not name the page: " << report;

    //And the drop is definitive: the page is written back without it.
    saveConfig();
    EXPECT_EQ(ioXmlOnDisk().find("t370_io_broken"), std::string::npos)
        << "the widget survived on disk, so the alert would be crying wolf";
}

/******************************************************************************
 * 4. The control, green before and after: a sound page loses nothing.
 ******************************************************************************/
TEST_F(RemoteUIWidgetCoordinateTest, AWellFormedPageKeepsEveryWidget)
{
    const size_t alertsBefore = Config::Instance().getConfigAlerts().size();

    std::string widgets = widgetXml("button", "t370_io_first", "10", "10");
    widgets += widgetXml("thermostat", "t370_io_zero", "0", "40");
    widgets += widgetXml("temp_display", "t370_io_last", "120", "10");

    loadIo(roomXml("Salon", "livingroom", remoteUiXml(DEV_BAD, pagesXml(widgets))));

    RemoteUI *ui = remoteUi(DEV_BAD);
    ASSERT_NE(ui, nullptr);

    EXPECT_EQ(widgetIoIds(ui),
              (std::vector<std::string>{"t370_io_first", "t370_io_zero", "t370_io_last"}))
        << "a page whose widgets are all well formed lost one";

    //x="0" is a position, not an absence: it must not be confused with a
    //coordinate that failed to read.
    const Json *zero = widgetOf(ui, "t370_io_zero");
    ASSERT_NE(zero, nullptr);
    EXPECT_TRUE((*zero)["x"].is_number_integer());
    EXPECT_EQ((*zero)["x"].get<int>(), 0);

    EXPECT_EQ(Config::Instance().getConfigAlerts().size(), alertsBefore)
        << "a sound configuration raised a configuration alert";
    EXPECT_TRUE(roundTripIo());
}

/******************************************************************************
 * T3.75 — the SIZE attributes of a <calaos:widget>, on the way to the screen.
 *
 * T3.70 gave x/y a guard because LoadFromXml() converted them here, in the
 * server, with a bare std::stoi that took the whole configuration load down.
 * w/h/width/height were left alone for the reason that they were never
 * converted here - and that is where it stops being true: the device converts
 * them itself (calaos_protocol.cpp, PagesConfig::fromJson), and its six stoi
 * calls sit under a single `catch (const json::exception &)`. std::stoi("")
 * throws std::invalid_argument, which is not a json::exception, so it leaves
 * fromJson() and every page of the screen goes with it.
 *
 * The four names are all four of them: the device reads `w` OR `width`, `h` OR
 * `height`. A guard on w/h alone leaves the alias wide open.
 *
 * Same answer as T3.70, and for the same reason: the attribute is left OUT
 * when it does not read as a whole number, the widget lands in the block that
 * drops an incomplete widget, and the user is told. A size attribute that was
 * never written at all keeps meaning what it always meant - the device has its
 * own default of 1x1 - so those widgets are untouched.
 ******************************************************************************/

namespace
{

const char *const DEV_SIZE = "t375_screen";

//One <calaos:widget> whose size attributes are injected verbatim, so a case
//can put anything in them, or leave them out entirely.
std::string sizedWidgetXml(const std::string &type, const std::string &ioId,
                           const std::string &sizeAttrs)
{
    return std::string("        <calaos:widget type=\"") + type + "\""
           " io_id=\"" + ioId + "\" x=\"10\" y=\"10\" " + sizeAttrs + "/>\n";
}

//The four names the device converts, in the order it tries them.
const char *const SIZE_ATTRS[] = { "w", "h", "width", "height" };

}

class RemoteUIWidgetSizeTest: public RemoteUIDeviceInfoTest
{
protected:
    //A page whose middle widget carries `badSize`, framed by two sound ones -
    //one in each of the two spellings the device accepts.
    std::string configWithSize(const std::string &badSize)
    {
        std::string widgets = sizedWidgetXml("button", "t375_io_first", "w=\"2\" h=\"1\"");
        widgets += sizedWidgetXml("thermostat", "t375_io_broken", badSize);
        widgets += sizedWidgetXml("temp_display", "t375_io_last",
                                  "width=\"3\" height=\"2\"");

        return roomXml("Salon", "livingroom",
                       remoteUiXml(DEV_SIZE, pagesXml(widgets)));
    }
};

/******************************************************************************
 * 1. Every one of the four names is guarded, not just the short pair.
 ******************************************************************************/
TEST_F(RemoteUIWidgetSizeTest, AnUnreadableSizeDropsTheWidgetWhicheverNameCarriesIt)
{
    for (const char *const attr: SIZE_ATTRS)
    {
        clearCoreState();

        const std::string blank = std::string(attr) + "=\"\"";
        ASSERT_NO_THROW(loadIo(configWithSize(blank))) << attr;

        RemoteUI *ui = remoteUi(DEV_SIZE);
        ASSERT_NE(ui, nullptr) << attr;

        //By identity, not by count.
        EXPECT_EQ(widgetIoIds(ui),
                  (std::vector<std::string>{"t375_io_first", "t375_io_last"}))
            << attr << "=\"\" reaches the screen: the device feeds it to "
               "std::stoi, throws std::invalid_argument past its json catch, "
               "and loses every page";

        EXPECT_EQ(widgetOf(ui, "t375_io_broken"), nullptr) << attr;
        EXPECT_FALSE(ui->hasReferencedIO("t375_io_broken")) << attr;
    }
}

/******************************************************************************
 * 2. A size that is not a number at all goes the same way.
 ******************************************************************************/
TEST_F(RemoteUIWidgetSizeTest, ASizeThatIsNotAWholeNumberDropsTheWidget)
{
    ASSERT_NO_THROW(loadIo(configWithSize("w=\"large\" h=\"1\"")));

    RemoteUI *ui = remoteUi(DEV_SIZE);
    ASSERT_NE(ui, nullptr);

    EXPECT_EQ(widgetOf(ui, "t375_io_broken"), nullptr)
        << "w=\"large\" was handed over as a string the device converts";
    EXPECT_EQ(widgetIoIds(ui),
              (std::vector<std::string>{"t375_io_first", "t375_io_last"}));
}

/******************************************************************************
 * 3. The user hears about it, on the channel T3.70 already uses.
 ******************************************************************************/
TEST_F(RemoteUIWidgetSizeTest, AWidgetDroppedForItsSizeIsReportedOnTheConfigAlertChannel)
{
    const size_t alertsBefore = Config::Instance().getConfigAlerts().size();

    ASSERT_NO_THROW(loadIo(configWithSize("w=\"\" h=\"1\"")));

    const std::vector<std::string> &alerts = Config::Instance().getConfigAlerts();
    ASSERT_EQ(alerts.size(), alertsBefore + 1)
        << "a screen silently lost a widget";

    const std::string &report = alerts.back();
    EXPECT_NE(report.find(DEV_SIZE), std::string::npos)
        << "the alert does not name the screen: " << report;
    EXPECT_NE(report.find("t375_io_broken"), std::string::npos)
        << "the alert does not name the widget: " << report;
    EXPECT_NE(report.find("Home"), std::string::npos)
        << "the alert does not name the page: " << report;

    saveConfig();
    EXPECT_EQ(ioXmlOnDisk().find("t375_io_broken"), std::string::npos)
        << "the widget survived on disk, so the alert would be crying wolf";
}

/******************************************************************************
 * 4. A widget that never declared a size is NOT incomplete. INVARIANT.
 *
 * w/h have always been optional here, and the device carries its own 1x1
 * default for them. Dropping those widgets would empty the pages of every
 * installation that never sized a widget - a far worse bug than the one being
 * closed. This is the case that says the guard is about an attribute that is
 * written and unreadable, not about an attribute that is absent.
 ******************************************************************************/
TEST_F(RemoteUIWidgetSizeTest, AWidgetWithNoSizeAtAllIsKept)
{
    const size_t alertsBefore = Config::Instance().getConfigAlerts().size();

    std::string widgets = sizedWidgetXml("button", "t375_io_first", "w=\"2\" h=\"1\"");
    widgets += sizedWidgetXml("thermostat", "t375_io_nosize", "");
    widgets += sizedWidgetXml("temp_display", "t375_io_last", "width=\"3\" height=\"2\"");

    loadIo(roomXml("Salon", "livingroom", remoteUiXml(DEV_SIZE, pagesXml(widgets))));

    RemoteUI *ui = remoteUi(DEV_SIZE);
    ASSERT_NE(ui, nullptr);

    EXPECT_EQ(widgetIoIds(ui),
              (std::vector<std::string>{"t375_io_first", "t375_io_nosize", "t375_io_last"}))
        << "a widget that simply never had a size was dropped";

    const Json *nosize = widgetOf(ui, "t375_io_nosize");
    ASSERT_NE(nosize, nullptr);
    EXPECT_FALSE(nosize->contains("w"));
    EXPECT_FALSE(nosize->contains("h"));

    EXPECT_EQ(Config::Instance().getConfigAlerts().size(), alertsBefore)
        << "a sound configuration raised a configuration alert";
}

/******************************************************************************
 * 5. The control: a well formed size crosses the loader untouched. INVARIANT.
 *
 * Value AND JSON type. The wire has carried these four as strings since the
 * first firmware, which reads them through an is_string() branch; turning them
 * into numbers here would be a wire change smuggled in with a bug fix.
 ******************************************************************************/
TEST_F(RemoteUIWidgetSizeTest, AWellFormedSizeCrossesTheLoaderUnchanged)
{
    const size_t alertsBefore = Config::Instance().getConfigAlerts().size();

    ASSERT_NO_THROW(loadIo(configWithSize("w=\"7\" h=\"0\"")));

    RemoteUI *ui = remoteUi(DEV_SIZE);
    ASSERT_NE(ui, nullptr);

    EXPECT_EQ(widgetIoIds(ui),
              (std::vector<std::string>{"t375_io_first", "t375_io_broken", "t375_io_last"}))
        << "a page whose widgets are all well formed lost one";

    //0 is a value the device rejects on its own terms, not an unreadable one:
    //the loader has no business deciding that here.
    const Json *middle = widgetOf(ui, "t375_io_broken");
    ASSERT_NE(middle, nullptr);
    ASSERT_TRUE(middle->contains("w"));
    EXPECT_TRUE((*middle)["w"].is_string()) << middle->dump();
    EXPECT_EQ((*middle)["w"].get<std::string>(), "7");
    EXPECT_EQ((*middle)["h"].get<std::string>(), "0");

    //And the long spelling is copied just as literally.
    const Json *last = widgetOf(ui, "t375_io_last");
    ASSERT_NE(last, nullptr);
    EXPECT_EQ((*last)["width"].get<std::string>(), "3");
    EXPECT_EQ((*last)["height"].get<std::string>(), "2");

    EXPECT_EQ(Config::Instance().getConfigAlerts().size(), alertsBefore);
    EXPECT_TRUE(roundTripIo());
}
