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
 *  - see the T3.35 block below for the one case T3.29 left out and T3.35
 *    brings in: a path token of exactly "[".
 ******************************************************************************/

/*******************************************************************************
 * T3.35 - CHARACTERIZATION of the two changes that share ONE catch block.
 *
 * (1) THE CRASH. A path token of exactly "[" takes down calaos_server,
 *     deterministically, on BOTH parsers. val.erase(0, 1) empties the token,
 *     val.pop_back() then underflows the size_t length, and the
 *     Utils::from_string(val, idx) that follows sits OUTSIDE the try block
 *     (MqttCtrl.cpp:132, WebCtrl.cpp:202 on master dd619482), so the
 *     std::bad_alloc escapes getValueJson(). Nothing catches it anywhere on
 *     the way up - getValue() -> the Mqtt/Web IO -> main() - and
 *     std::terminate() runs. Reachable by a TYPO in a configuration
 *     parameter; there is no remote vector, a `path` is only ever written by
 *     calaos_installer.
 *
 *     T3.29 deliberately did not pin this, because reading a string whose
 *     length has underflowed is undefined behaviour and a test must not
 *     freeze UB. The cases below therefore do NOT pin the crash: they pin the
 *     ERROR PATH THAT SHOULD EXIST - an empty value, no exception, and a
 *     warning that names the offending token. That is a statement about the
 *     behaviour we want, not about the behaviour we have, which is what makes
 *     it a legitimate red-before-green.
 *
 *     ! HOW THESE FAIL BEFORE THE FIX. A dying binary prints no FAILED line.
 *     Depending on where the underflowed string lands, the run either raises
 *     (gtest turns it into a failure) or dies outright, in which case the
 *     only trustworthy signal is the EXIT CODE of the test binary, not the
 *     absence of red lines in its output.
 *
 * (2) THE HINT ("option C" of T3.29 sect. 5.6). The user who copied the old
 *     glued syntax gets an empty value and a "subpath not found" that does
 *     not tell them what to do. T3.35 logs, in the OBJECT branch's catch,
 *     "did you mean a/[0]/b ? array indices are their own path segment".
 *
 *     ! WHY NO FALSE POSITIVE IS POSSIBLE, and it is the whole design: that
 *     catch is only ever entered when parent.at(val) HAS ALREADY THROWN. A
 *     payload whose key really is spelled "action[0]" - a real, measured
 *     Zigbee2MQTT shape - RESOLVES, so it never reaches the catch and never
 *     gets a hint. AKeyReallySpelledThatWayGetsNoHint is that case, and it is
 *     exactly what separates option C from option B (teaching the parser to
 *     ALSO split a glued index), which T3.29 implemented, measured, and
 *     REJECTED: it shadowed that key silently. AnIndexGluedToTheKeyDoesNotResolve
 *     and AGluedIndexStillMatchesAKeySpelledThatWay are the anti-option-B
 *     guard; do not weaken them.
 *
 *     The hint is a WEAK improvement and should not be oversold: cWarning()
 *     is not a filtered domain, so the line does reach the server log by
 *     default - but the person who made the typo is sitting in
 *     calaos_installer, and the message lands in the calaos_server log.
 *
 * FIXTURE, for (2): kZigbeePayload owns a key literally spelled "action[0]"
 * AND a real array named "action", plus a string, an integer and a nested
 * object. The two together are what makes option B visible: under option B
 * `action[0]` reads action's element 0 ("hold") instead of the key's value
 * ("single"). The diversity of the FIELDS is the net here, not the number of
 * array elements.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
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

//T3.35. The shape that makes the "no false positive" claim checkable, taken
//from a real Zigbee2MQTT button payload: the document owns a key LITERALLY
//spelled "action[0]" and, next to it, a real ARRAY named "action". A parser
//that also split a glued index - option B, rejected by T3.29 - would answer
//`action[0]` with the array's element 0 ("hold") instead of the key's value
//("single"), silently. The other fields are deliberately of different kinds
//(string, integer, nested object, array of strings): the diversity of the
//FIELDS is what makes a wrong lookup visible, not the length of the array.
const char *const kZigbeePayload = R"JSON({
  "action[0]": "single",
  "action[1]": "double",
  "action": [ "hold", "release" ],
  "battery": 87,
  "linkquality": 132,
  "device": { "friendlyName": "kitchen_button", "model": "WXKG01LM" }
})JSON";

//T3.35. cWarning() writes to std::cout (Logger.cpp, LogStream::~LogStream),
//and WARNING (3) is below the default level INFO (4), so the line is emitted
//by default - cWarning() is not a filtered domain. Swapping the streambuf for
//the duration of one call is therefore enough to read back what the server
//would have logged.
//
//WARM UP FIRST: the very first log of the process builds Logger.cpp's level
//cache, which reads the config and can print on its own. Every case below
//resolves something harmless before installing the capture.
class CoutCapture
{
public:
    CoutCapture(): saved(std::cout.rdbuf(buffer.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(saved); }
    std::string str() const { return buffer.str(); }

private:
    std::ostringstream buffer;
    std::streambuf *saved;
};

bool logContains(const std::string &log, const std::string &needle)
{
    return log.find(needle) != std::string::npos;
}

//The user facing wording the hint must carry. "did you mean" is the question,
//the second half is the RULE - the same sentence the ioDoc publishes (see
//IoDocIndexSyntax.TheIndexRuleIsSpelledOutAndNotOnlyShown) - so that the log
//and the parameter help say the same thing.
const char *const kHintQuestion = "did you mean";
const char *const kHintRule = "array indices are their own path segment";

//T3.35b. The wording of the warning an index token that does not parse must
//carry. It is asserted on its own needle so that a case which reaches it
//cannot be confused with the "malformed array index" case above.
const char *const kUnreadableIndex = "is not a number";

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
 * T3.35 (1) - THE CRASH, pinned by the ERROR PATH THAT SHOULD EXIST.
 *
 * These do NOT assert what master does; master reads a string whose length
 * has underflowed, and that is undefined behaviour nobody may freeze. They
 * assert the contract a malformed index token must honour: an EMPTY value, NO
 * exception, and a warning that names the offending token - the same contract
 * every other failing path of this parser already honours (unknown key, out
 * of bounds index, malformed payload).
 *
 * Before the fix these fail by RAISING or by KILLING the process. A dead
 * binary prints no FAILED line: the signal to check is the exit code.
 * ------------------------------------------------------------------------ */

TEST_F(MqttJsonPathTest, ALonePathBracketDoesNotKillTheProcess)
{
    EXPECT_NO_THROW({ EXPECT_EQ("", resolve("[")); });
    EXPECT_NO_THROW({ EXPECT_EQ("", resolve("weather/[/description")); });
}

TEST_F(WebJsonPathTest, ALonePathBracketDoesNotKillTheProcess)
{
    EXPECT_NO_THROW({ EXPECT_EQ("", resolve("[")); });
    EXPECT_NO_THROW({ EXPECT_EQ("", resolve("weather/[/description")); });
}

//A silent empty value is what T3.29 measured as the reason nobody can debug a
//bad path. The error path added here must name what it choked on, and it must
//not pretend it looked an index up: reaching "index not found" would mean the
//garbage index was used after all.
TEST_F(MqttJsonPathTest, ALonePathBracketIsLoggedAsAnError)
{
    resolve("main/city"); //warm up the logger before capturing
    std::string log;
    {
        CoutCapture capture;
        resolve("weather/[/description");
        log = capture.str();
    }
    EXPECT_TRUE(logContains(log, "weather/[/description")) << "log was: " << log;
    //! Names the offending TOKEN. "array index" alone was also true of a
    //message that only echoed the path, so it could not tell the two apart.
    EXPECT_TRUE(logContains(log, "malformed array index [")) << "log was: " << log;
    EXPECT_FALSE(logContains(log, "index not found")) << "log was: " << log;
}

TEST_F(WebJsonPathTest, ALonePathBracketIsLoggedAsAnError)
{
    resolve("main/city");
    std::string log;
    {
        CoutCapture capture;
        resolve("weather/[/description");
        log = capture.str();
    }
    EXPECT_TRUE(logContains(log, "weather/[/description")) << "log was: " << log;
    //! Names the offending TOKEN. "array index" alone was also true of a
    //message that only echoed the path, so it could not tell the two apart.
    EXPECT_TRUE(logContains(log, "malformed array index [")) << "log was: " << log;
    EXPECT_FALSE(logContains(log, "index not found")) << "log was: " << log;
}

/* ---------------------------------------------------------------------------
 * T3.35 (2) - THE HINT, and the case that proves it cannot fire wrongly.
 * ------------------------------------------------------------------------ */

//The old, documented-until-T3.29 form. It still resolves to nothing - that is
//option B staying rejected - but the log now says why, and how to fix it.
TEST_F(MqttJsonPathTest, AGluedIndexIsAnsweredWithAHint)
{
    resolve("main/city");
    std::string log;
    {
        CoutCapture capture;
        EXPECT_EQ("", resolve("weather[0]/description"));
        log = capture.str();
    }
    EXPECT_TRUE(logContains(log, kHintRule)) << "log was: " << log;
    //! The needle is the SUGGESTION, "weather/[0]", and not the path the user
    //typed. Asserting on "weather[0]" was satisfied by the "subpath not found
    //weather[0]" line printed just above the hint, so the suggested text
    //itself was pinned by nothing: a hint that echoed the faulty path back
    //unchanged passed. The '/' is the whole content of the advice.
    EXPECT_TRUE(logContains(log, std::string(kHintQuestion) + " weather/[0]"))
        << "log was: " << log;
}

TEST_F(WebJsonPathTest, AGluedIndexIsAnsweredWithAHint)
{
    resolve("main/city");
    std::string log;
    {
        CoutCapture capture;
        EXPECT_EQ("", resolve("weather[0]/description"));
        log = capture.str();
    }
    EXPECT_TRUE(logContains(log, kHintRule)) << "log was: " << log;
    //! The needle is the SUGGESTION, "weather/[0]", and not the path the user
    //typed. Asserting on "weather[0]" was satisfied by the "subpath not found
    //weather[0]" line printed just above the hint, so the suggested text
    //itself was pinned by nothing: a hint that echoed the faulty path back
    //unchanged passed. The '/' is the whole content of the advice.
    EXPECT_TRUE(logContains(log, std::string(kHintQuestion) + " weather/[0]"))
        << "log was: " << log;
}

//! THE CASE THE WHOLE DESIGN RESTS ON. `action[0]` is a REAL key of a real
//Zigbee2MQTT payload. It RESOLVES, so parent.at() never throws, so the catch
//that carries the hint is never entered, so no hint can be emitted. The false
//positive is impossible by construction, not by heuristic - and that is
//precisely what option B could not offer: splitting the glued token would
//have answered "hold" here, silently, forever.
TEST_F(MqttJsonPathTest, AKeyReallySpelledThatWayGetsNoHint)
{
    resolve("main/city");
    std::string log;
    std::string value;
    {
        CoutCapture capture;
        value = resolve("action[0]", kZigbeePayload);
        log = capture.str();
    }
    EXPECT_EQ("single", value);
    EXPECT_FALSE(logContains(log, kHintQuestion)) << "log was: " << log;
    EXPECT_FALSE(logContains(log, kHintRule)) << "log was: " << log;
    EXPECT_EQ("", log) << "a path that resolves must log nothing at all";

    //The neighbours of the key, so that a lookup landing on the wrong field
    //is visible instead of accidentally right.
    EXPECT_EQ("double", resolve("action[1]", kZigbeePayload));
    EXPECT_EQ("hold", resolve("action/[0]", kZigbeePayload));
    EXPECT_EQ("release", resolve("action/[1]", kZigbeePayload));
    EXPECT_EQ("87", resolve("battery", kZigbeePayload));
    EXPECT_EQ("kitchen_button", resolve("device/friendlyName", kZigbeePayload));
}

TEST_F(WebJsonPathTest, AKeyReallySpelledThatWayGetsNoHint)
{
    resolve("main/city");
    std::string log;
    std::string value;
    {
        CoutCapture capture;
        value = resolve("action[0]", kZigbeePayload);
        log = capture.str();
    }
    EXPECT_EQ("single", value);
    EXPECT_FALSE(logContains(log, kHintQuestion)) << "log was: " << log;
    EXPECT_FALSE(logContains(log, kHintRule)) << "log was: " << log;
    EXPECT_EQ("", log) << "a path that resolves must log nothing at all";

    EXPECT_EQ("double", resolve("action[1]", kZigbeePayload));
    EXPECT_EQ("hold", resolve("action/[0]", kZigbeePayload));
    EXPECT_EQ("release", resolve("action/[1]", kZigbeePayload));
    EXPECT_EQ("87", resolve("battery", kZigbeePayload));
    EXPECT_EQ("kitchen_button", resolve("device/friendlyName", kZigbeePayload));
}

//The other half of the "cannot fire wrongly" claim: a plain missing key is
//still a plain missing key. The hint is bound to the presence of a '[' in the
//token, so a token without one must not get it - otherwise the hint becomes
//noise on every typo and stops being read.
TEST_F(MqttJsonPathTest, APlainMissingKeyGetsNoHint)
{
    resolve("main/city");
    std::string log;
    {
        CoutCapture capture;
        EXPECT_EQ("", resolve("nosuchobject/description"));
        log = capture.str();
    }
    EXPECT_TRUE(logContains(log, "nosuchobject")) << "log was: " << log;
    EXPECT_FALSE(logContains(log, kHintQuestion)) << "log was: " << log;
    EXPECT_FALSE(logContains(log, kHintRule)) << "log was: " << log;
}

TEST_F(WebJsonPathTest, APlainMissingKeyGetsNoHint)
{
    resolve("main/city");
    std::string log;
    {
        CoutCapture capture;
        EXPECT_EQ("", resolve("nosuchobject/description"));
        log = capture.str();
    }
    EXPECT_TRUE(logContains(log, "nosuchobject")) << "log was: " << log;
    EXPECT_FALSE(logContains(log, kHintQuestion)) << "log was: " << log;
    EXPECT_FALSE(logContains(log, kHintRule)) << "log was: " << log;
}

/* ---------------------------------------------------------------------------
 * T3.35b - WHAT THE REVIEW OF T3.35 FOUND, characterized.
 *
 * (a) THE GUARD CHECKS A LENGTH, THE MESSAGE CLAIMS A FORM. T3.35 added
 *     `if (val.size() < 2)` and printed "an array index must be written [n]".
 *     Those are not the same statement. "[5" and "[12" are two characters or
 *     more, so they walk straight past the guard, get their first AND LAST
 *     character stripped anyway, and resolve: "[5" loses its '5' and reads
 *     element 0, "[12" loses its '2' and reads element 1. The user is told the
 *     parser wants "[n]" while the parser is in fact accepting "[n" and
 *     answering with the WRONG element - silently, with a plausible value.
 *     That is worse than the empty string the same typo used to produce
 *     elsewhere: it cannot be told apart from a correct reading.
 *
 *     These four cases are RED on 3db14a92 - they get "clear sky" and "light
 *     rain" instead of nothing.
 *
 * (b) A TOKEN WHOSE INDEX DOES NOT PARSE SAYS NOTHING. T3.29 froze the VALUE
 *     ("[zz]" reads element 0, ANonNumericIndexSilentlyReadsElementZero) and
 *     that stays frozen - it is a behaviour real configurations may lean on.
 *     What must not stay is the SILENCE: reading element 0 because the index
 *     was unreadable is exactly the case the user cannot diagnose, and it is
 *     the same class of failure as the guard above. The value is kept, a
 *     warning is added. RED on 3db14a92: nothing is logged at all.
 *
 *     "[]" belongs to the same family and is the one that made `int idx = 0`
 *     load bearing: after erase/pop_back the inner text is BLANK, and
 *     Utils::from_string() does not write its destination on a blank string
 *     (its stream sentry fails before num_get runs), so the index was read
 *     UNINITIALISED. Measured directly, not deduced: from_string("", d) leaves
 *     d at its previous value AND returns true, so its return code cannot be
 *     used to detect the case either.
 * ------------------------------------------------------------------------ */

//! The two forms the T3.35 message promises to reject and does not. They must
//! return NOTHING - not element 0, not element 1.
TEST_F(MqttJsonPathTest, AnIndexMissingItsClosingBracketIsRejected)
{
    EXPECT_EQ("", resolve("weather/[5/description"));
    EXPECT_EQ("", resolve("weather/[12/description"));
}

TEST_F(WebJsonPathTest, AnIndexMissingItsClosingBracketIsRejected)
{
    EXPECT_EQ("", resolve("weather/[5/description"));
    EXPECT_EQ("", resolve("weather/[12/description"));
}

//! ...and they must say so, naming the TOKEN and not only echoing the path.
//The needle carries the token itself ("[12"), so an assertion on the path
//alone cannot stand in for it.
TEST_F(MqttJsonPathTest, AnIndexMissingItsClosingBracketIsLogged)
{
    resolve("main/city"); //warm up the logger before capturing
    std::string log;
    {
        CoutCapture capture;
        resolve("weather/[12/description");
        log = capture.str();
    }
    EXPECT_TRUE(logContains(log, "malformed array index [12")) << "log was: " << log;
    EXPECT_FALSE(logContains(log, "index not found")) << "log was: " << log;
}

TEST_F(WebJsonPathTest, AnIndexMissingItsClosingBracketIsLogged)
{
    resolve("main/city");
    std::string log;
    {
        CoutCapture capture;
        resolve("weather/[12/description");
        log = capture.str();
    }
    EXPECT_TRUE(logContains(log, "malformed array index [12")) << "log was: " << log;
    EXPECT_FALSE(logContains(log, "index not found")) << "log was: " << log;
}

//! An index that cannot be parsed still reads element 0 - T3.29 froze that -
//but it is no longer silent about it. The VALUE assertion is the T3.29
//behaviour, unchanged; the LOG assertion is what T3.35b adds.
TEST_F(MqttJsonPathTest, AnUnreadableIndexIsLoggedAndStillReadsElementZero)
{
    resolve("main/city");
    std::string log;
    std::string value;
    {
        CoutCapture capture;
        value = resolve("weather/[zz]/description");
        log = capture.str();
    }
    EXPECT_EQ("clear sky", value);
    EXPECT_TRUE(logContains(log, "[zz]")) << "log was: " << log;
    EXPECT_TRUE(logContains(log, kUnreadableIndex)) << "log was: " << log;
}

TEST_F(WebJsonPathTest, AnUnreadableIndexIsLoggedAndStillReadsElementZero)
{
    resolve("main/city");
    std::string log;
    std::string value;
    {
        CoutCapture capture;
        value = resolve("weather/[zz]/description");
        log = capture.str();
    }
    EXPECT_EQ("clear sky", value);
    EXPECT_TRUE(logContains(log, "[zz]")) << "log was: " << log;
    EXPECT_TRUE(logContains(log, kUnreadableIndex)) << "log was: " << log;
}

//! "[]" - the token that made `int idx = 0` load bearing. It must land in the
//SAME place as "[zz]": element 0, and a warning. Before T3.35b the index was
//read uninitialised here, so the value was whatever the stack held.
TEST_F(MqttJsonPathTest, AnEmptyIndexIsLoggedAndStillReadsElementZero)
{
    resolve("main/city");
    std::string log;
    std::string value;
    {
        CoutCapture capture;
        value = resolve("weather/[]/description");
        log = capture.str();
    }
    EXPECT_EQ("clear sky", value);
    EXPECT_TRUE(logContains(log, kUnreadableIndex)) << "log was: " << log;
}

TEST_F(WebJsonPathTest, AnEmptyIndexIsLoggedAndStillReadsElementZero)
{
    resolve("main/city");
    std::string log;
    std::string value;
    {
        CoutCapture capture;
        value = resolve("weather/[]/description");
        log = capture.str();
    }
    EXPECT_EQ("clear sky", value);
    EXPECT_TRUE(logContains(log, kUnreadableIndex)) << "log was: " << log;
}

//! The measured reason the two cases above exist at all. Utils::from_string()
//is the only thing standing between a failed parse and the caller's variable,
//and on a BLANK string it neither writes the destination nor reports it:
//the stream sentry fails before num_get runs, and iss.eof() is TRUE because
//the stream did reach its end. A caller cannot detect the case from the
//return code, which is why every caller must either pre-check the string or
//own an initialised destination.
TEST(UtilsFromString, ABlankStringNeitherWritesTheDestinationNorReportsIt)
{
    double d = 424242.0;
    EXPECT_TRUE(Utils::from_string(std::string(""), d));
    EXPECT_DOUBLE_EQ(424242.0, d);

    int i = 7777;
    EXPECT_TRUE(Utils::from_string(std::string(""), i));
    EXPECT_EQ(7777, i);

    //By contrast a NON blank string that fails to parse DOES write 0: the
    //sentry succeeds, num_get runs and C++11 makes it store zero on failure.
    //That asymmetry is the whole trap.
    int j = 7777;
    EXPECT_FALSE(Utils::from_string(std::string("zz"), j));
    EXPECT_EQ(0, j);
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
