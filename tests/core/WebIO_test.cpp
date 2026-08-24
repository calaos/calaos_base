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
 * Tests for the thin Web IO subclasses (T3.2d).
 *
 * The seven Web IO types share their constructor skeleton through the
 * WebIOBase/WebInputBase mixins of WebDocBase.h. What must not change:
 *
 *  - every type stays registered in IOFactory under its XML name and is
 *    instantiable from an io.xml document,
 *  - the ioDoc parameters coming from WebDocBase are preserved per type:
 *    url/file_type/path for the "GET" flavor, url/data/data_type for the
 *    "POST" flavor, both for WebOutputString, plus raw_value for
 *    WebOutputLightRGB (--gendoc content),
 *  - a local file:// url still feeds the inputs synchronously at creation
 *    time (WebCtrl::Add fires the downloaded-callback in line for local
 *    files - the reason why the polling registration must stay the last
 *    statement of the concrete constructors, see WebDocBase.h),
 *  - the create/delete cycle unregisters the WebCtrl callback (no dangling
 *    callback into a deleted IO).
 */

#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <string>

#include "CalaosCoreFixture.h"
#include "IOBase.h"
#include "IODoc.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/* E4.1k: genDocJson() answers a nlohmann Json instead of a json_t*. The walk
 * below is the same walk, name for name: value() keeps the tolerance of the
 * jansson version (a missing "parameters" gave a NULL that json_array_foreach
 * simply skipped, it never threw), and only string "name" fields are kept, as
 * before. No assertion of this file moved. */
std::set<std::string> docParamNames(IOBase *io)
{
    std::set<std::string> names;

    Json doc = io->getDoc()->genDocJson();
    for (const Json &val: doc.value("parameters", Json::array()))
    {
        if (val.is_object() && val.contains("name") && val.at("name").is_string())
            names.insert(val.at("name").get<std::string>());
    }

    return names;
}

std::string webIoXml(const std::string &elem, const std::string &type,
                     const std::string &id, const std::string &extraAttributes)
{
    return "    <calaos:" + elem + " type=\"" + type + "\" id=\"" + id +
           "\" name=\"" + id + "\" enabled=\"true\" visible=\"true\" " +
           extraAttributes + " />\n";
}

} // namespace

class WebIOTest: public CoreFixture {};

/* All seven Web types instantiate from an io.xml document, and the inputs
 * read their value from a local json file synchronously at creation. */
TEST_F(WebIOTest, AllWebTypesInstantiateFromIoXml)
{
    const std::string jsonFile = cacheDir() + "/webio_doc.json";
    {
        std::ofstream f(jsonFile);
        f << "{\"temp\": 21.5, \"msg\": \"hello\", \"num\": 42.5}";
    }

    const std::string url = "file://" + jsonFile;
    const std::string getAttrs =
        "url=\"" + url + "\" file_type=\"json\" period=\"10\" ";

    const std::string ios =
        webIoXml("input", "WebInputAnalog", "web_ana", getAttrs + "path=\"num\"") +
        webIoXml("input", "WebInputTemp", "web_temp", getAttrs + "path=\"temp\"") +
        webIoXml("input", "WebInputString", "web_str", getAttrs + "path=\"msg\"") +
        webIoXml("output", "WebOutputAnalog", "web_oana", getAttrs + "path=\"num\"") +
        webIoXml("output", "WebOutputLight", "web_light", "url=\"" + url + "\"") +
        webIoXml("output", "WebOutputLightRGB", "web_rgb", "url=\"" + url + "\"") +
        webIoXml("output", "WebOutputString", "web_ostr", getAttrs + "path=\"msg\"");

    loadConfig(ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE, ios)),
               rulesXmlDocument(""));

    const char *const ids[] = {"web_ana", "web_temp", "web_str", "web_oana",
                               "web_light", "web_rgb", "web_ostr"};
    for (const char *id: ids)
        EXPECT_NE(io(id), nullptr) << id << " was not instantiated";

    //The local file url makes WebCtrl fire the download callback in line
    //during the constructor, so the values are already there.
    ASSERT_NE(io("web_ana"), nullptr);
    EXPECT_DOUBLE_EQ(io("web_ana")->get_value_double(), 42.5);
    ASSERT_NE(io("web_temp"), nullptr);
    EXPECT_DOUBLE_EQ(io("web_temp")->get_value_double(), 21.5);
    ASSERT_NE(io("web_str"), nullptr);
    EXPECT_EQ(io("web_str")->get_value_string(), "hello");

    //Delete cycle: the destructor must unregister the WebCtrl callback.
    EXPECT_TRUE(deleteIO(io("web_ana")));
    EXPECT_EQ(io("web_ana"), nullptr);
}

/* The WebDocBase ioDoc parameters are preserved per type (--gendoc). */
TEST_F(WebIOTest, IoDocParametersPreserved)
{
    loadConfig();
    Room *room = firstRoom();
    ASSERT_NE(room, nullptr);

    auto make = [&](const char *type, const char *id) -> IOBase *
    {
        return createIO({{ "type", type }, { "id", id }, { "name", id }}, room);
    };

    //GET flavor: url/file_type/path, no POST params
    for (const char *type: {"WebInputAnalog", "WebInputTemp", "WebInputString"})
    {
        IOBase *in = make(type, (std::string("doc_") + type).c_str());
        ASSERT_NE(in, nullptr) << type;
        auto n = docParamNames(in);
        EXPECT_TRUE(n.count("url")) << type;
        EXPECT_TRUE(n.count("file_type")) << type;
        EXPECT_TRUE(n.count("path")) << type;
        EXPECT_FALSE(n.count("data")) << type;
        EXPECT_FALSE(n.count("data_type")) << type;
    }

    IOBase *oana = make("WebOutputAnalog", "doc_oana");
    ASSERT_NE(oana, nullptr);
    auto n = docParamNames(oana);
    EXPECT_TRUE(n.count("url"));
    EXPECT_TRUE(n.count("file_type"));
    EXPECT_TRUE(n.count("path"));
    EXPECT_FALSE(n.count("data"));

    //POST flavor: url/data/data_type, no GET params
    for (const char *type: {"WebOutputLight", "WebOutputLightRGB"})
    {
        IOBase *out = make(type, (std::string("doc_") + type).c_str());
        ASSERT_NE(out, nullptr) << type;
        n = docParamNames(out);
        EXPECT_TRUE(n.count("url")) << type;
        EXPECT_TRUE(n.count("data")) << type;
        EXPECT_TRUE(n.count("data_type")) << type;
        EXPECT_FALSE(n.count("file_type")) << type;
        EXPECT_FALSE(n.count("path")) << type;
    }

    //WebOutputLightRGB documents its extra raw_value parameter
    IOBase *rgb = io("doc_WebOutputLightRGB");
    ASSERT_NE(rgb, nullptr);
    EXPECT_TRUE(docParamNames(rgb).count("raw_value"));

    //WebOutputString documents both flavors
    IOBase *ostr = make("WebOutputString", "doc_ostr");
    ASSERT_NE(ostr, nullptr);
    n = docParamNames(ostr);
    for (const char *paramName: {"url", "file_type", "path", "data", "data_type"})
        EXPECT_TRUE(n.count(paramName)) << paramName;
}
