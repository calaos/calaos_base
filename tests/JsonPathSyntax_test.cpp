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
 ******************************************************************************/

/*******************************************************************************
 * T3.29 - CHARACTERIZATION of the JSON `path` syntax of the MQTT and Web IOs,
 * and of the syntax their ioDoc PUBLISHES to the user.
 *
 * The `path` parameter of an MQTT or a Web IO extracts one value out of a JSON
 * document received from a third party device. Two copies of the same parser
 * implement it:
 *
 *   MqttCtrl::getValueJson(params, path, payload)   IO/Mqtt/MqttCtrl.cpp
 *   WebCtrl::getValueJson(path, filename)           IO/Web/WebCtrl.cpp
 *
 * Both split the path on '/' (Utils::split, which collapses runs of the
 * delimiter and never yields an empty token) and treat a token as an ARRAY
 * INDEX only when its FIRST CHARACTER is '['. An index is therefore a path
 * SEGMENT OF ITS OWN:
 *
 *      weather/[1]/description        <- resolves
 *      weather[1]/description         <- does NOT resolve (empty string)
 *
 * The ioDoc of the 7 MQTT `*path*` parameters and of the Web `path` parameter
 * documented the SECOND form. That text is not a comment: it goes through
 * _(), feeds `calaos_server --gendoc`, data/doc/{en,fr}/io_doc.json and the
 * calaos_installer parameter help - i.e. every user reads it at the exact
 * moment they configure the IO, writes a path that silently yields an empty
 * value, and never learns why. The failure IS logged - cWarning() is not a
 * filtered domain (LogSetup.h:29), the "[WRN] (MqttCtrl.cpp) ... subpath not
 * found" line goes to the server log by default - but the person who made the
 * mistake is in calaos_installer and the message lands in the calaos_server
 * log, which is why it does not reach them.
 *
 * TWO LEVELS, both needed:
 *
 *  (A) THE PARSER. Pins WHICH syntax actually works, so that the change of
 *      the documentation has an authority other than a claim. Every case runs
 *      TWICE, once per copy of the parser, because they are duplicated code
 *      that can drift apart. These tests are GREEN before the fix and must
 *      STAY green: T3.29 changes no parser.
 *
 *  (B) THE PUBLISHED DOCUMENTATION. Reads the descriptions back out of
 *      IODoc::genDocJson() - the very document --gendoc writes - and requires
 *      that no `key[0]/` form is taught and that the working form is. These
 *      are RED before the fix. (B) is the only net that protects the product
 *      here: (A) proves what works, only (B) catches someone re-writing the
 *      description.
 *
 * FIXTURE. `weather` carries THREE elements with THREE distinct, non
 * substitutable descriptions, and the asserted index is 1 - neither the first
 * nor the last - so that an off-by-one or a hardcoded 0 in the index branch
 * is visible. Same shape for `nested/list/[0]/deep`.
 *
 * NOT ASSERTED, on purpose:
 *  - a path token of exactly "[" CRASHES THE SERVER, deterministically, on
 *    both parsers. erase(0, 1) empties val, pop_back() then underflows the
 *    size_t, and the Utils::from_string() that follows sits OUTSIDE the try
 *    (MqttCtrl.cpp:132, WebCtrl.cpp:202), so std::bad_alloc escapes
 *    getValueJson(); nothing catches it up to main() and std::terminate()
 *    runs. Reachable by a typo in a configuration parameter (no remote
 *    vector). It is NOT pinned here on purpose - the read of the emptied
 *    string is undefined behaviour and a test must not freeze UB. Reported in
 *    FINDINGS.md and ticketed as T3.35, together with the "did you mean
 *    a/[0]/b ?" hint that shares the same catch block.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <string>

#include "IODoc.h"
#include "MqttCtrl.h"
#include "WebCtrl.h"
#include "WebDocBase.h"

using namespace Calaos;

namespace
{

//Three weather entries, three distinct descriptions; the asserted one is the
//middle one. Same rule for nested/list/[0]/deep.
const char *const kPayload = R"JSON({
  "weather": [
    { "description": "clear sky",    "id": 800 },
    { "description": "light rain",   "id": 500 },
    { "description": "thunderstorm", "id": 200 }
  ],
  "main": { "city": "Toulouse", "temp": 21.5 },
  "temperature": 14.23,
  "online": true,
  "missing": null,
  "nested": { "list": [ { "deep": [ "alpha", "beta", "gamma" ] } ] }
})JSON";

//A document that really owns a key SPELLED "weather[0]". This is the only way
//the documented-but-broken form can ever return something, and it is why
//teaching the parser to also accept `key[0]` would not be a free move: it
//would shadow this key.
const char *const kBracketKeyPayload =
    R"JSON({ "weather[0]": { "description": "a literal key, not an index" } })JSON";

/* ---------------------------------------------------------------------------
 * (A) THE PARSER - one fixture per copy, the same cases on both.
 * ------------------------------------------------------------------------ */

//MqttCtrl::getValueJson() reads nothing off its instance, but the constructor
//spawns the calaos_mqtt helper. With empty Params the broker defaults apply,
//the helper does not exist under the test prefix, uv_spawn fails and uvw
//dispatches the ErrorEvent synchronously to the handler installed by
//ExternProcServer::startProcess(); no event loop is ever run here. The object
//is leaked on purpose - ~MqttCtrl() does not free `process` either.
MqttCtrl &mqttCtrl()
{
    static Params empty;
    static MqttCtrl *ctrl = new MqttCtrl(empty);
    return *ctrl;
}

class MqttJsonPathTest: public ::testing::Test
{
protected:
    std::string resolve(const std::string &path, const char *payload = kPayload)
    {
        static Params noParams;
        return mqttCtrl().getValueJson(noParams, path, payload);
    }
};

class WebJsonPathTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        filename = "/tmp/calaos_t329_webctrl.json";
        write(filename, kPayload);
    }

    void TearDown() override
    {
        ::remove(filename.c_str());
        ::remove("/tmp/calaos_t329_webctrl_brackets.json");
    }

    static void write(const std::string &name, const char *content)
    {
        std::ofstream f(name);
        f << content;
        f.close();
    }

    std::string resolve(const std::string &path, const char *payload = kPayload)
    {
        std::string name = filename;
        if (payload != kPayload)
        {
            name = "/tmp/calaos_t329_webctrl_brackets.json";
            write(name, payload);
        }
        return ctrl.getValueJson(path, name);
    }

    std::string filename;
    WebCtrl ctrl; //default constructed: getValueJson() needs the path and the file only
};

/* --- the two cases the whole ticket turns on --------------------------- */

//The form T3.29 publishes.
TEST_F(MqttJsonPathTest, AnIndexAsItsOwnSegmentResolves)
{
    EXPECT_EQ("light rain", resolve("weather/[1]/description"));
}

TEST_F(WebJsonPathTest, AnIndexAsItsOwnSegmentResolves)
{
    EXPECT_EQ("light rain", resolve("weather/[1]/description"));
}

//The form the ioDoc used to publish. It is not a near miss, it returns
//NOTHING: `weather[1]` does not start with '[', so it is looked up whole as
//an object key, at() throws, and the value is empty.
TEST_F(MqttJsonPathTest, AnIndexGluedToTheKeyDoesNotResolve)
{
    EXPECT_EQ("", resolve("weather[1]/description"));
}

TEST_F(WebJsonPathTest, AnIndexGluedToTheKeyDoesNotResolve)
{
    EXPECT_EQ("", resolve("weather[1]/description"));
}

//...unless the document really has that key. The broken form is not
//"inert": it is a plain object lookup, and a payload owning a key named
//"weather[0]" answers it. No existing configuration can therefore be assumed
//to be a no-op, and making the parser ALSO accept the glued form would break
//exactly this document.
TEST_F(MqttJsonPathTest, AGluedIndexStillMatchesAKeySpelledThatWay)
{
    EXPECT_EQ("a literal key, not an index",
              resolve("weather[0]/description", kBracketKeyPayload));
}

TEST_F(WebJsonPathTest, AGluedIndexStillMatchesAKeySpelledThatWay)
{
    EXPECT_EQ("a literal key, not an index",
              resolve("weather[0]/description", kBracketKeyPayload));
}

/* --- the rest of the syntax, frozen as it is --------------------------- */

TEST_F(MqttJsonPathTest, APlainKeyPathStillResolves)
{
    EXPECT_EQ("Toulouse", resolve("main/city"));
    EXPECT_EQ("14.23", resolve("temperature"));
    EXPECT_EQ("true", resolve("online"));
}

TEST_F(WebJsonPathTest, APlainKeyPathStillResolves)
{
    EXPECT_EQ("Toulouse", resolve("main/city"));
    EXPECT_EQ("14.23", resolve("temperature"));
    EXPECT_EQ("true", resolve("online"));
}

TEST_F(MqttJsonPathTest, IndexesNestAndReachEveryElement)
{
    EXPECT_EQ("clear sky", resolve("weather/[0]/description"));
    EXPECT_EQ("thunderstorm", resolve("weather/[2]/description"));
    EXPECT_EQ("500", resolve("weather/[1]/id"));
    EXPECT_EQ("gamma", resolve("nested/list/[0]/deep/[2]"));
}

TEST_F(WebJsonPathTest, IndexesNestAndReachEveryElement)
{
    EXPECT_EQ("clear sky", resolve("weather/[0]/description"));
    EXPECT_EQ("thunderstorm", resolve("weather/[2]/description"));
    EXPECT_EQ("500", resolve("weather/[1]/id"));
    EXPECT_EQ("gamma", resolve("nested/list/[0]/deep/[2]"));
}

TEST_F(MqttJsonPathTest, AnOutOfBoundsIndexReturnsEmpty)
{
    EXPECT_EQ("", resolve("weather/[7]/description"));
}

TEST_F(WebJsonPathTest, AnOutOfBoundsIndexReturnsEmpty)
{
    EXPECT_EQ("", resolve("weather/[7]/description"));
}

TEST_F(MqttJsonPathTest, AnUnknownKeyReturnsEmpty)
{
    EXPECT_EQ("", resolve("weather/[1]/nosuchfield"));
    EXPECT_EQ("", resolve("nosuchobject/description"));
}

TEST_F(WebJsonPathTest, AnUnknownKeyReturnsEmpty)
{
    EXPECT_EQ("", resolve("weather/[1]/nosuchfield"));
    EXPECT_EQ("", resolve("nosuchobject/description"));
}

//Utils::split() collapses runs of the delimiter and never emits an empty
//token, so an empty segment simply disappears instead of failing a lookup.
TEST_F(MqttJsonPathTest, EmptySegmentsAreCollapsed)
{
    EXPECT_EQ("light rain", resolve("weather//[1]///description"));
    EXPECT_EQ("light rain", resolve("/weather/[1]/description/"));
}

TEST_F(WebJsonPathTest, EmptySegmentsAreCollapsed)
{
    EXPECT_EQ("light rain", resolve("weather//[1]///description"));
    EXPECT_EQ("light rain", resolve("/weather/[1]/description/"));
}

//Utils::from_string() leaves its destination at 0 when the parse fails
//(std::istringstream >> int, C++11), so a non numeric index silently reads
//element 0 instead of reporting anything. Frozen, not endorsed.
TEST_F(MqttJsonPathTest, ANonNumericIndexSilentlyReadsElementZero)
{
    EXPECT_EQ("clear sky", resolve("weather/[zz]/description"));
}

TEST_F(WebJsonPathTest, ANonNumericIndexSilentlyReadsElementZero)
{
    EXPECT_EQ("clear sky", resolve("weather/[zz]/description"));
}

//A path that stops on a container does not return the container, it returns a
//marker string. The user sees "object{}" in the IO value.
TEST_F(MqttJsonPathTest, APathStoppingOnAContainerReturnsAMarker)
{
    EXPECT_EQ("object{}", resolve("main"));
    EXPECT_EQ("array[]", resolve("weather"));
    EXPECT_EQ("null", resolve("missing"));
}

TEST_F(WebJsonPathTest, APathStoppingOnAContainerReturnsAMarker)
{
    EXPECT_EQ("object{}", resolve("main"));
    EXPECT_EQ("array[]", resolve("weather"));
    EXPECT_EQ("null", resolve("missing"));
}

//The two copies are NOT interchangeable on the empty path, and that is the
//one behaviour the shared documentation must not merge: MQTT hands the raw
//payload back, Web returns nothing at all.
TEST_F(MqttJsonPathTest, AnEmptyPathReturnsTheRawPayload)
{
    EXPECT_EQ(kPayload, resolve(""));
}

TEST_F(WebJsonPathTest, AnEmptyPathReturnsEmpty)
{
    EXPECT_EQ("", resolve(""));
    EXPECT_EQ("", resolve("///"));
}

//Malformed input is survivable on both sides: no exception escapes.
TEST_F(MqttJsonPathTest, AMalformedPayloadReturnsEmpty)
{
    EXPECT_EQ("", resolve("weather/[1]/description", "{ not json"));
}

TEST_F(WebJsonPathTest, AMalformedPayloadReturnsEmpty)
{
    EXPECT_EQ("", resolve("weather/[1]/description", "{ not json"));
}

/* ---------------------------------------------------------------------------
 * (B) THE PUBLISHED DOCUMENTATION.
 *
 * Reads the descriptions back out of IODoc::genDocJson(), the document
 * `calaos_server --gendoc` writes to io_doc.json and calaos_installer ships.
 * MqttCtrl::commonDoc() is static and WebDocBase has no state, so no IO, no
 * broker and no download is involved.
 * ------------------------------------------------------------------------ */

//`key[0]/`, `key[12]/` ... - an index glued to the token that precedes it.
const std::regex kGluedIndex(R"([A-Za-z0-9_]\[[0-9]+\])");

//The one form that resolves, as the ioDoc must spell it.
const char *const kWorkingExample = "weather/[0]/description";

struct DocParam
{
    std::string name;
    std::string description;
};

std::vector<DocParam> docParams(IODoc &doc)
{
    std::vector<DocParam> out;
    Json j = doc.genDocJson();
    for (const auto &p : j["parameters"])
    {
        DocParam d;
        d.name = p.value("name", std::string());
        d.description = p.value("description", std::string());
        out.push_back(d);
    }
    return out;
}

//Every parameter whose description talks about the weather example.
std::vector<DocParam> paramsTeachingTheExample(IODoc &doc)
{
    std::vector<DocParam> out;
    for (const auto &p : docParams(doc))
        if (p.description.find("weather") != std::string::npos)
            out.push_back(p);
    return out;
}

TEST(IoDocIndexSyntax, MqttNeverTeachesAnIndexGluedToItsKey)
{
    IODoc doc;
    MqttCtrl::commonDoc(&doc);

    auto teaching = paramsTeachingTheExample(doc);
    //path + battery_path + connected_status_path + wireless_signal_path +
    //uptime_path + ip_address_path + wifi_ssid_path
    ASSERT_EQ(7u, teaching.size());

    for (const auto &p : teaching)
    {
        EXPECT_FALSE(std::regex_search(p.description, kGluedIndex))
            << "MQTT parameter \"" << p.name
            << "\" publishes an array index glued to its key; the parser only "
               "reads an index that is its own path segment. Description: "
            << p.description;
        EXPECT_NE(std::string::npos, p.description.find(kWorkingExample))
            << "MQTT parameter \"" << p.name
            << "\" must show the form that resolves (" << kWorkingExample
            << "). Description: " << p.description;
    }
}

TEST(IoDocIndexSyntax, WebNeverTeachesAnIndexGluedToItsKey)
{
    IODoc doc;
    WebDocBase docBase;
    docBase.initDoc(&doc);

    auto teaching = paramsTeachingTheExample(doc);
    ASSERT_EQ(1u, teaching.size());
    EXPECT_EQ("path", teaching[0].name);

    EXPECT_FALSE(std::regex_search(teaching[0].description, kGluedIndex))
        << "The Web `path` parameter publishes an array index glued to its "
           "key. Description: " << teaching[0].description;
    EXPECT_NE(std::string::npos, teaching[0].description.find(kWorkingExample))
        << "The Web `path` parameter must show the form that resolves ("
        << kWorkingExample << "). Description: " << teaching[0].description;
}

//An example alone is copied badly. The rule - "an array index is a path
//segment of its own" - is what lets a user transpose it to their own payload,
//so it must be spelled out and not only shown.
TEST(IoDocIndexSyntax, TheIndexRuleIsSpelledOutAndNotOnlyShown)
{
    IODoc mqttDoc;
    MqttCtrl::commonDoc(&mqttDoc);
    IODoc webDoc;
    WebDocBase webBase;
    webBase.initDoc(&webDoc);

    const std::regex rule("array indices are their own path segment",
                          std::regex::icase);

    for (const auto &p : paramsTeachingTheExample(mqttDoc))
        EXPECT_TRUE(std::regex_search(p.description, rule))
            << "MQTT parameter \"" << p.name
            << "\" shows the example but never states the rule.";

    for (const auto &p : paramsTeachingTheExample(webDoc))
        EXPECT_TRUE(std::regex_search(p.description, rule))
            << "Web parameter \"" << p.name
            << "\" shows the example but never states the rule.";
}

} //namespace
