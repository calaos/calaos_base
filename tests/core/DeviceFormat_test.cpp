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

//io.xml format 2: written as soon as a device exists (an inert one included),
//announced by `config get`, and the guard of `config put` that refuses an
//upload which would drop the devices. Also the ids hidden inside an inert
//device, which no new IO may take.

#include "JsonApiCharacterization.h"

#include "CalaosConfig.h"
#include "Device.h"
#include "ListeRoom.h"

#include <pugixml.hpp>

#include <filesystem>
#include <string>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

class FormatTestDevice: public Device
{
public:
    FormatTestDevice(Params &p):
        Device(p)
    {
    }
};

} //namespace

REGISTER_DEVICE(format_test, FormatTestDevice)

namespace
{

const char *const DEV_A = "c3_dev_a";
const char *const DEV_B = "c3_dev_b";
const char *const DEV_FUTURE = "c3_dev_future";

std::string deviceXml(const std::string &id, const std::string &impl)
{
    return "    <calaos:device id=\"" + id + "\" type=\"climate\" impl=\"" + impl +
           "\" name=\"Clim " + id + "\">\n"
                                    "      <calaos:internal type=\"InternalString\" id=\"" +
           id + "_mode\" name=\"Mode\""
                " enabled=\"true\" visible=\"false\" />\n"
                "    </calaos:device>\n";
}

std::string plainIo()
{
    return internalIoXml("InternalBool", "c3_plain", "Plain");
}

//The marker is what a format 2 aware tool writes; ioXmlDocument() has none.
std::string format2(std::string doc)
{
    const std::string home = "<calaos:home>";
    doc.replace(doc.find(home), home.size(), "<calaos:home format=\"2\">");
    return doc;
}

std::string houseXml(const std::string &ios, bool marker)
{
    const std::string doc = ioXmlDocument(roomXml(CoreFixture::ROOM_NAME, CoreFixture::ROOM_TYPE, ios));
    return marker ? format2(doc) : doc;
}

} //namespace

class DeviceFormatTest: public JsonApiCharacterizationTest
{
protected:
    struct PutResult
    {
        Json reply;
        bool restart = false;
    };

    //declared: what the client says it supports (io_format in the request).
    PutResult put(const Json &files, const Json &declared = Json())
    {
        PutResult res;
        HttpTestRequest req;
        Json body = Json{ { "action", "config" }, { "type", "put" }, { "config_files", files } };
        if (!declared.is_null())
            body["io_format"] = declared;
        req.send(authenticated(body));

        EXPECT_EQ(1u, req.count());
        EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
        if (req.count() == 1)
            res.reply = req.bodyJson();
        res.restart = req.needsRestart();
        return res;
    }

    PutResult putIo(const std::string &ioXml, const Json &declared = Json())
    {
        return put(Json{ { "io.xml", ioXml }, { "rules.xml", rulesXmlDocument(std::string()) } },
                   declared);
    }

    Json configGet()
    {
        HttpTestRequest req;
        req.send(authenticated(Json{ { "action", "config" }, { "type", "get" } }));
        EXPECT_EQ(1u, req.count());
        EXPECT_FALSE(req.needsRestart());
        return req.count() == 1 ? req.bodyJson() : Json::object();
    }

    size_t backupFiles() const
    {
        namespace fs = std::filesystem;
        size_t n = 0;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(configDir() + "/backups", ec), end;
             !ec && it != end; it.increment(ec))
            if (it->is_regular_file())
                n++;
        return n;
    }

    static bool hasFormatMarker(const std::string &ioXml)
    {
        pugi::xml_document doc;
        if (!doc.load_string(ioXml.c_str()))
            return false;
        return std::string(doc.child("calaos:ioconfig").child("calaos:home").attribute("format").as_string()) == "2";
    }

    //A previous load stays in the singletons until cleared.
    void reload(const std::string &ioXml)
    {
        clearCoreState();
        loadConfig(ioXml, rulesXmlDocument(std::string()));
    }

    //Two devices, one of them inert: both count.
    void loadTwoDevices()
    {
        reload(houseXml(plainIo() + deviceXml(DEV_A, "format_test") +
                            deviceXml(DEV_FUTURE, "future_impl"),
                        true));
    }

    void expectRefused(const PutResult &res, size_t lost, const std::string &ioBefore)
    {
        expectRefusedWith(res,
                          "update calaos_installer: this io.xml would drop " +
                              std::to_string(lost) + " device(s)",
                          ioBefore);
    }

    void expectRefusedWith(const PutResult &res, const std::string &message,
                           const std::string &ioBefore)
    {
        EXPECT_EQ(res.reply.value("success", std::string()), "false");
        EXPECT_EQ(res.reply.value("error", std::string()), message);
        EXPECT_FALSE(res.restart);
        EXPECT_EQ(ioXmlOnDisk(), ioBefore);
        EXPECT_EQ(backupFiles(), 0u);
    }
};

TEST_F(DeviceFormatTest, FormatMarkerOnlyWhenADeviceExists)
{
    reload(houseXml(plainIo(), false));
    saveConfig();
    EXPECT_FALSE(hasFormatMarker(ioXmlOnDisk()));
    EXPECT_EQ(ioXmlOnDisk().find("format="), std::string::npos);

    //A marker with nothing behind it is not preserved.
    reload(houseXml(plainIo(), true));
    saveConfig();
    EXPECT_FALSE(hasFormatMarker(ioXmlOnDisk()));

    reload(houseXml(plainIo() + deviceXml(DEV_A, "format_test"), false));
    saveConfig();
    EXPECT_TRUE(hasFormatMarker(ioXmlOnDisk()));
    EXPECT_TRUE(roundTripIo());
}

TEST_F(DeviceFormatTest, InertDeviceAloneMakesTheFileFormat2)
{
    reload(houseXml(plainIo() + deviceXml(DEV_FUTURE, "future_impl"), false));
    saveConfig();
    EXPECT_TRUE(hasFormatMarker(ioXmlOnDisk()));
    EXPECT_NE(ioXmlOnDisk().find(DEV_FUTURE), std::string::npos);
}

TEST_F(DeviceFormatTest, ConfigGetAnnouncesTheFormat)
{
    reload(houseXml(plainIo(), false));
    Json get = configGet();
    ASSERT_TRUE(get.contains("io_format"));
    EXPECT_TRUE(get["io_format"].is_number_integer());
    EXPECT_EQ(get["io_format"].get<int>(), 2);
    EXPECT_FALSE(hasFormatMarker(get["config_files"]["io.xml"].get<std::string>()));

    loadTwoDevices();
    get = configGet();
    EXPECT_EQ(get["io_format"].get<int>(), 2);
    EXPECT_TRUE(hasFormatMarker(get["config_files"]["io.xml"].get<std::string>()));
}

TEST_F(DeviceFormatTest, PutFromAClientThatDoesNotDeclareDevicesIsRefused)
{
    loadTwoDevices();
    saveConfig();
    const std::string before = ioXmlOnDisk();
    const std::string rulesBefore = rulesXmlOnDisk();

    PutResult res = putIo(houseXml(plainIo(), false));
    expectRefused(res, 2, before);
    EXPECT_EQ(rulesXmlOnDisk(), rulesBefore);

    //What the file says does not matter, only what the request declares.
    expectRefused(putIo(houseXml(plainIo() + deviceXml(DEV_A, "format_test") +
                                     deviceXml(DEV_FUTURE, "future_impl"),
                                 true)),
                  2, before);
    expectRefused(putIo(houseXml(plainIo(), true)), 2, before);
}

TEST_F(DeviceFormatTest, AnUnusableDeclarationCountsAsFormat1)
{
    loadTwoDevices();
    saveConfig();
    const std::string before = ioXmlOnDisk();
    const std::string upload = houseXml(plainIo(), false);

    for (const Json &declared: { Json(1), Json("1"), Json("abc"), Json(true), Json(1.5),
                                 Json::array(), Json::object() })
        expectRefused(putIo(upload, declared), 2, before);
}

//A truncating int cast or atoi() would read these as 2.
TEST_F(DeviceFormatTest, ADeclarationIsNotTruncatedNorPartiallyRead)
{
    loadTwoDevices();
    saveConfig();
    const std::string before = ioXmlOnDisk();
    const std::string upload = houseXml(plainIo(), false);

    for (const Json &declared: { Json(4294967298LL), Json(4294967298ULL), Json(18446744073709551615ULL),
                                 Json(-2), Json(0), Json(1001), Json("4294967298"),
                                 Json("2abc"), Json("2 "), Json(" 2"), Json("+2"), Json("-2"),
                                 Json(""), Json("1001"), Json("99999999999999999999") })
        expectRefused(putIo(upload, declared), 2, before);
}

TEST_F(DeviceFormatTest, PutThatDoesNotParseIsAlwaysRefused)
{
    loadTwoDevices();
    saveConfig();
    const std::string before = ioXmlOnDisk();
    const std::string broken = "<?xml version=\"1.0\"?><calaos:ioconfig><calaos:home";

    expectRefusedWith(putIo(broken), "io.xml does not parse", before);
    expectRefusedWith(putIo(broken, Json(2)), "io.xml does not parse", before);
}

//Nothing to protect, so nothing is judged: the pre-CORE-3 behavior.
TEST_F(DeviceFormatTest, PutThatDoesNotParseIsAcceptedWithoutDevices)
{
    reload(houseXml(plainIo(), false));
    saveConfig();

    const std::string broken = "<?xml version=\"1.0\"?><calaos:ioconfig><calaos:home";
    PutResult res = putIo(broken);
    EXPECT_EQ(res.reply.value("success", std::string()), "true");
    EXPECT_TRUE(res.restart);
    EXPECT_EQ(ioXmlOnDisk(), broken);
}

TEST_F(DeviceFormatTest, DevicesWithoutIdAreCountedOneByOne)
{
    reload(houseXml(plainIo() + "    <calaos:device type=\"climate\" impl=\"x1\" />\n"
                                "    <calaos:device type=\"climate\" impl=\"x2\" />\n",
                    true));
    saveConfig();
    const std::string before = ioXmlOnDisk();

    expectRefused(putIo(houseXml(plainIo(), false)), 2, before);
}

TEST_F(DeviceFormatTest, InertDeviceAloneCountsForTheRefusal)
{
    reload(houseXml(plainIo() + deviceXml(DEV_FUTURE, "future_impl"), true));
    saveConfig();
    const std::string before = ioXmlOnDisk();

    expectRefused(putIo(houseXml(plainIo(), false)), 1, before);
}

TEST_F(DeviceFormatTest, RefusalCoversTheWholePut)
{
    loadTwoDevices();
    saveConfig();
    const std::string before = ioXmlOnDisk();
    const std::string rulesNew = rulesXmlDocument("<!-- must not land -->\n");

    PutResult res = put(Json{ { "io.xml", houseXml(plainIo(), false) }, { "rules.xml", rulesNew } });
    expectRefused(res, 2, before);
    EXPECT_NE(rulesXmlOnDisk(), rulesNew);
}

TEST_F(DeviceFormatTest, DeclaredClientKeepingTheDevicesIsAccepted)
{
    loadTwoDevices();
    saveConfig();

    const std::string upload = houseXml(plainIo() + internalIoXml("InternalBool", "c3_new", "New") +
                                            deviceXml(DEV_A, "format_test") +
                                            deviceXml(DEV_FUTURE, "future_impl"),
                                        true);
    PutResult res = putIo(upload, Json(2));
    EXPECT_EQ(res.reply.value("success", std::string()), "true");
    EXPECT_FALSE(res.reply.contains("error"));
    EXPECT_TRUE(res.restart);
    EXPECT_EQ(ioXmlOnDisk(), upload);
    EXPECT_GT(backupFiles(), 0u);
}

TEST_F(DeviceFormatTest, DeclarationAsStringOrNewerFormatIsAccepted)
{
    for (const Json &declared: { Json("2"), Json(3), Json("10"), Json(1000), Json("1000") })
    {
        loadTwoDevices();
        saveConfig();
        const std::string upload = houseXml(plainIo(), false);
        PutResult res = putIo(upload, declared);
        EXPECT_EQ(res.reply.value("success", std::string()), "true") << declared.dump();
        EXPECT_TRUE(res.restart) << declared.dump();
        EXPECT_EQ(ioXmlOnDisk(), upload) << declared.dump();
    }
}

TEST_F(DeviceFormatTest, DeclaredClientMayRemoveSomeOrAllDevices)
{
    loadTwoDevices();
    saveConfig();
    std::string upload = houseXml(plainIo() + deviceXml(DEV_FUTURE, "future_impl"), true);
    PutResult res = putIo(upload, Json(2));
    EXPECT_EQ(res.reply.value("success", std::string()), "true");
    EXPECT_TRUE(res.restart);
    EXPECT_EQ(ioXmlOnDisk(), upload);

    //The last one too: that is what a format 1 looking file means from a
    //client that declared it knows devices.
    loadTwoDevices();
    saveConfig();
    upload = houseXml(plainIo(), false);
    res = putIo(upload, Json(2));
    EXPECT_EQ(res.reply.value("success", std::string()), "true");
    EXPECT_TRUE(res.restart);
    EXPECT_EQ(ioXmlOnDisk(), upload);

    reload(houseXml(plainIo() + deviceXml(DEV_FUTURE, "future_impl"), true));
    saveConfig();
    res = putIo(upload, Json(2));
    EXPECT_EQ(res.reply.value("success", std::string()), "true");
    EXPECT_TRUE(res.restart);
}

TEST_F(DeviceFormatTest, DeclaredClientUploadIsNotInspectedBeyondParsing)
{
    loadTwoDevices();
    saveConfig();

    //A newer format and a device without id: nothing in there is judged.
    std::string upload = houseXml(plainIo() + "    <calaos:device type=\"climate\" impl=\"x\" />\n", true);
    upload.replace(upload.find("format=\"2\""), 10, "format=\"3\"");
    PutResult res = putIo(upload, Json(2));
    EXPECT_EQ(res.reply.value("success", std::string()), "true");
    EXPECT_TRUE(res.restart);
    EXPECT_EQ(ioXmlOnDisk(), upload);
}

TEST_F(DeviceFormatTest, PutWithoutDevicesOnAHouseWithoutDevicesIsAccepted)
{
    reload(houseXml(plainIo(), false));
    saveConfig();

    const std::string upload = houseXml(plainIo() + internalIoXml("InternalBool", "c3_new", "New"), false);
    PutResult res = putIo(upload);
    EXPECT_EQ(res.reply.value("success", std::string()), "true");
    EXPECT_TRUE(res.restart);
    EXPECT_EQ(ioXmlOnDisk(), upload);
}

TEST_F(DeviceFormatTest, PutWithoutIoXmlIsNotJudged)
{
    loadTwoDevices();
    saveConfig();
    const std::string before = ioXmlOnDisk();

    PutResult res = put(Json{ { "rules.xml", rulesXmlDocument(std::string()) } });
    EXPECT_EQ(res.reply.value("success", std::string()), "true");
    EXPECT_TRUE(res.restart);
    EXPECT_EQ(ioXmlOnDisk(), before);
}

TEST_F(DeviceFormatTest, IdsInsideAnInertDeviceAreReserved)
{
    std::string future =
        "    <calaos:device id=\"io_2\" type=\"climate\" impl=\"future_impl\" name=\"F\">\n"
        "      <calaos:internal type=\"InternalBool\" id=\"io_0\" name=\"a\" />\n"
        "      <calaos:internal type=\"InternalBool\" id=\"io_1\" name=\"b\" />\n"
        "      <calaos:link name=\"mode\" io=\"io_0\" />\n"
        "    </calaos:device>\n";
    reload(houseXml(plainIo() + future, true));

    ListeRoom &lr = ListeRoom::Instance();
    EXPECT_EQ(lr.get_io("io_0"), nullptr);
    for (const char *id: { "io_0", "io_1", "io_2" })
        EXPECT_TRUE(lr.isIdReserved(id)) << id;
    EXPECT_FALSE(lr.isIdReserved("io_3"));
    EXPECT_FALSE(lr.isIdReserved("c3_plain"));

    EXPECT_EQ(Calaos::get_new_id("io_"), "io_3");

    Room *room = lr.get_room(0);
    ASSERT_NE(room, nullptr);
    IOBase *created = lr.createIO({ { "type", "InternalBool" } }, room);
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created->get_param("id"), "io_3");

    const int sizeBefore = room->get_size();
    EXPECT_EQ(lr.createIO({ { "type", "InternalBool" }, { "id", "io_1" } }, room), nullptr);
    EXPECT_EQ(room->get_size(), sizeBefore);

    //The ids are in the saved file, not in the model: gone with the device.
    saveConfig();
    EXPECT_NE(ioXmlOnDisk().find("id=\"io_1\""), std::string::npos);
    clearCoreState();
    EXPECT_FALSE(lr.isIdReserved("io_1"));
}
