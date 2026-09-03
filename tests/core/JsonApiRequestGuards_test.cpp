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
/*******************************************************************************
 * THE THREE WIDENINGS OF THE REQUEST PARSER, AND WHERE EACH ONE LANDS.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS FILE EXISTS NEXT TO core/JsonApiDispatchWireBytes_test
 * ---------------------------------------------------------------------------
 * That file pinned WHAT the parser now accepts, at the door: an escaped NUL in
 * a value and in a key, an integer beyond int64, and a nesting depth above the
 * 2048 the previous parser enforced. It stopped there on purpose.
 *
 * This file answers the next question - WHERE DOES IT LAND - and it is the
 * question the door cannot answer. Two of the three widenings turn out to be
 * harmless at the door and destructive three layers down, and the third is a
 * cost the single event loop pays on behalf of every other client.
 *
 * ---------------------------------------------------------------------------
 * THREE ORACLES, DELIBERATELY DISJOINT
 * ---------------------------------------------------------------------------
 *   D_  NESTING DEPTH. What the parser accepts, what it refuses, and what the
 *       refusal looks like from the client's side. No case here asserts an
 *       escaping, a key order or a stored value.
 *
 *   B_  THE ESCAPED NUL, PAST THE API. Every case here goes through the XML
 *       writer and the configuration on disk, which is the layer nobody had
 *       measured. Nothing here asserts a depth.
 *
 *   I_  NUMBERS. What a JSON number becomes once the flattening contract has
 *       turned it into a Params string. Nothing here carries a NUL.
 *
 * ---------------------------------------------------------------------------
 * ANTI "FIXTURE PAUVRE"
 * ---------------------------------------------------------------------------
 *   - every depth case sits on a REAL request (get_home, set_param) and the
 *     accepted half asserts the answer is that request's answer, not merely a
 *     200: a transport that answered 200 to everything would otherwise pass;
 *   - the two depths that frame the limit are asserted in the SAME file and
 *     differ by ONE level, so "the guard fires" and "the guard fires too
 *     early" cannot be confused;
 *   - the three string-scanning cases (a brace inside a string, an escaped
 *     quote, an escaped backslash) carry FIVE THOUSAND opening brackets each:
 *     a scanner that counted them would blow any limit worth having, so these
 *     cases cannot pass by coincidence;
 *   - every B_ case asserts the value in memory AND the bytes on disk. They
 *     disagree, and a case that looked at only one of the two would report the
 *     opposite verdict;
 *   - the I_ cases carry a SMALL integer as a control: without it, "the value
 *     is mangled" could just as well mean "nothing is stored at all".
 *
 * This file adds NO GOLDEN. tests/core/golden must keep the tree hash it has.
 *
 * Ids are prefixed t358_. Taken so far: e40_, e40b_ .. e40f_, e41b_, e41n_,
 * e41o_, e41p_, e41q_, e41r_, e41s_, e46b_, t317a_ .. t317f_, t318_, t319_.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "ListeRoom.h"
#include "IOBase.h"

#include <cstring>
#include <string>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char T358_ROOM_NAME[] = "T3.58 room";
const char T358_ROOM_TYPE[] = "salon";
const char T358_IO_BOOL[] = "t358_bool";
const char T358_IO_STRING[] = "t358_string";

//The id production hands out for the FIRST scenario of a fresh house.
const char T358_SCENARIO_IO_ID[] = "io_0";

//A value with an embedded NUL, built with an explicit length: a plain literal
//would stop at the zero byte and the probe would BE the truncation it detects.
const std::string NUL_INSIDE = std::string("head\0tail", 9);

//The nesting the previous parser refused, and the two neighbours that frame it.
const int JANSSON_DEPTH_CAP = 2048;

//Nested arrays as raw TEXT. No P_/D_ case can be built by dumping a Json: the
//dump would produce a document every parser accepts.
std::string brackets(int levels)
{
    std::string s;
    s.reserve(2 * levels);
    for (int i = 0; i < levels; i++) s += '[';
    for (int i = 0; i < levels; i++) s += ']';
    return s;
}

//The same, in OBJECTS, so a guard that only counted brackets is not enough.
std::string braces(int levels)
{
    std::string s;
    s.reserve(6 * levels);
    for (int i = 0; i < levels; i++) s += "{\"a\":";
    s += '1';
    for (int i = 0; i < levels; i++) s += '}';
    return s;
}

std::string repeated(char c, int n)
{
    return std::string(static_cast<size_t>(n), c);
}

bool contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

} //namespace

class JsonApiRequestGuardsTest: public JsonApiCharacterizationTest
{
protected:
    /* A house with one boolean output and one string IO, in one room. Small on
     * purpose: every case here is about a byte, not about a payload shape.
     */
    void loadGuardHouse()
    {
        std::string ios;
        ios += internalIoXml("InternalBool", T358_IO_BOOL, "Bool value");
        ios += internalIoXml("InternalString", T358_IO_STRING, "String value");

        loadConfig(ioXmlDocument(roomXml(T358_ROOM_NAME, T358_ROOM_TYPE, ios, 0)),
                   rulesXmlDocument(std::string()));
        pumpEventLoop();
    }

    //A RAW HTTP body with the credentials spelled in by hand: every case here
    //needs its probe to survive as TEXT, and authenticated(Json) would dump it.
    static std::string httpRawBody(const std::string &members)
    {
        return std::string("{\"cn_user\":\"") + apiUser() +
               "\",\"cn_pass\":\"" + apiPassword() + "\"" +
               (members.empty()? std::string(): "," + members) + "}";
    }

    /* ⚠️ EVERY HTTP request of this file clears the login throttle first, and
     * that is not hygiene. A refused request registers a login FAILURE, and the
     * next request from the same address inside the backoff window answers 400
     * whatever its body says - so a case that sends a refusal and then a probe
     * measures the throttle, not the parser. The red round of E4.1s caught
     * exactly that on its own depth case. The throttle has a suite of its own
     * (core/JsonApiThrottleIdentity_test); it is not this file's oracle.
     */
    static void clearThrottle() { LoginThrottle::clear(); }

    static std::string httpStatusFor(const std::string &rawBody)
    {
        clearThrottle();
        HttpTestRequest req;
        req.send(rawBody);
        EXPECT_EQ(1u, req.count());
        return req.statusLine();
    }

    //A get_home request whose "probe" member carries `nesting` as raw text.
    static std::string deepGetHome(const std::string &nesting)
    {
        return httpRawBody("\"action\":\"get_home\",\"probe\":" + nesting);
    }

    //True when the body really is the answer to get_home and not just any 200.
    //Without this, "the deep document was served" could mean "the transport
    //answers 200 to everything".
    static bool isGetHomeAnswer(const std::string &body)
    {
        const Json j = Json::parse(body, nullptr, false);
        return j.is_object() && j.find("home") != j.cend() && j["home"].is_array();
    }

    //Drives one HTTP request on a clean throttle and answers the raw body.
    static std::string httpBodyFor(const std::string &rawBody)
    {
        clearThrottle();
        HttpTestRequest req;
        req.send(rawBody);
        EXPECT_EQ(1u, req.count());
        return req.body();
    }

    //set_param through the real HTTP path, with the value spelled as raw JSON
    //text so a probe can be a number, an escape, or anything else.
    std::string setParamRaw(const char *ioId, const char *param,
                            const std::string &rawJsonValue)
    {
        return httpStatusFor(httpRawBody(std::string("\"action\":\"set_param\",\"id\":\"") +
                                         ioId + "\",\"param\":\"" + param +
                                         "\",\"value\":" + rawJsonValue));
    }
};

/*******************************************************************************
 * D_ - NESTING DEPTH.
 *
 * The root object counts as one level, so a body whose "probe" nests N arrays
 * has depth N + 1. Every pair below is written that way.
 ******************************************************************************/

TEST_F(JsonApiRequestGuardsTest, D_ADepthOfExactlyTheCapIsServed)
{
    /* ⭐ THE FIXTURE JUST BELOW THE LINE, and the one that is missing from
     * every guard written in a hurry. It is an INVARIANT: it was green before
     * the cap existed and it must stay green after, otherwise the cap is off
     * by one and refuses a document the API has always accepted.
     */
    loadGuardHouse();

    const std::string body = httpBodyFor(deepGetHome(brackets(JANSSON_DEPTH_CAP - 1)));

    EXPECT_TRUE(isGetHomeAnswer(body))
            << "a document exactly at the cap stopped being served: " << body.substr(0, 200);
}

TEST_F(JsonApiRequestGuardsTest, D_ADepthOfOneAboveTheCapIsServedToday)
{
    //One level deeper than the case above, nothing else changed. The pair is
    //what makes the cap a measurement instead of an assertion.
    loadGuardHouse();

    EXPECT_TRUE(isGetHomeAnswer(httpBodyFor(deepGetHome(brackets(JANSSON_DEPTH_CAP)))))
            << "the parser already refuses a nesting depth above the cap";
}

TEST_F(JsonApiRequestGuardsTest, D_NestedObjectsAboveTheCapAreServedToday)
{
    //The objects half. A guard that only counted '[' would leave this document
    //served, which is why it is pinned apart from the brackets one.
    loadGuardHouse();

    EXPECT_TRUE(isGetHomeAnswer(httpBodyFor(deepGetHome(braces(JANSSON_DEPTH_CAP)))))
            << "nested objects above the cap are already refused";

    //The neighbour just below, which must stay served either way.
    EXPECT_TRUE(isGetHomeAnswer(httpBodyFor(deepGetHome(braces(JANSSON_DEPTH_CAP - 1)))))
            << "nested objects at the cap stopped being served";
}

TEST_F(JsonApiRequestGuardsTest, D_ADepthOfFourThousandIsServedToday)
{
    /* ⭐ THE CASE THAT SAYS WHY A CAP IS WANTED, AND IT IS NOT THE PARSER.
     * Parsing is cheap and iterative: a hundred thousand levels parse and
     * destruct in 13 ms without touching the stack. What is not cheap is what
     * processApi() does NEXT, on every request and BEFORE the credentials are
     * checked - JsonApi::dumpJsonRedacted(), which deep-COPIES the document,
     * walks it with a recursive std::function and then dump()s it INDENTED.
     *
     * The indentation makes the log line quadratic in the depth. Measured at
     * -O2 on an 8 MiB stack:
     *
     *      depth  2048   16.8 MB of log line     served
     *      depth  4096   67 MB                   served   <- this case
     *      depth 16384   1.07 GB                 served
     *      depth 43000   7.4 GB                  served
     *      depth 44000   -                       SEGFAULT, stack exhausted
     *
     * ⚠️ THAT IS WHY NO CASE OF THIS FILE GOES ABOVE FOUR THOUSAND while the
     * cap is absent: a case that did would take the binary down with it. The
     * ones that do exist live in the commit that adds the cap, because the cap
     * is what makes them survivable.
     */
    loadGuardHouse();

    EXPECT_TRUE(isGetHomeAnswer(httpBodyFor(deepGetHome(brackets(4096)))));
}

TEST_F(JsonApiRequestGuardsTest, D_ADeepBodyIsAnsweredWithOneResponseAndNoCloseToday)
{
    /* The refusal-to-be, seen from the client's side. Pinned as it is TODAY -
     * one 200 and an open socket - so that what the refusal answers instead
     * can be read as a change and not as a discovery.
     */
    loadGuardHouse();
    clearThrottle();

    HttpTestRequest req;
    req.send(deepGetHome(brackets(JANSSON_DEPTH_CAP)));

    ASSERT_EQ(1u, req.count()) << "the deep body answered twice, or not at all";
    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_TRUE(req.closes().empty());
}

TEST_F(JsonApiRequestGuardsTest, D_TheWsParserServesADocumentAboveTheCapToday)
{
    //The websocket parses in a different function, and a guard that reached
    //only one transport would leave this case green.
    loadGuardHouse();

    WsTestSession ws;
    ws.send(std::string("{\"msg\":\"get_home\",\"msg_id\":\"1\",\"probe\":") +
            brackets(JANSSON_DEPTH_CAP) + "}");
    pumpEventLoop();
    ASSERT_EQ(1u, ws.count()) << "the WS parser already refuses a document above the cap";
    EXPECT_EQ("get_home", ws.lastEnvelope().value("msg", std::string()));

    //And the session is not torn down for it: an unparsable message has never
    //closed a websocket, and a cap must not change that.
    EXPECT_TRUE(ws.closes().empty());
}

TEST_F(JsonApiRequestGuardsTest, D_TheWsParserStillServesADocumentAtTheCap)
{
    //The websocket half of the fixture just below the line.
    loadGuardHouse();

    WsTestSession ws;
    ws.send(std::string("{\"msg\":\"get_home\",\"msg_id\":\"1\",\"probe\":") +
            brackets(JANSSON_DEPTH_CAP - 1) + "}");
    pumpEventLoop();

    ASSERT_EQ(1u, ws.count()) << "a WS document at the cap stopped being served";
    EXPECT_EQ("get_home", ws.lastEnvelope().value("msg", std::string()));
}

TEST_F(JsonApiRequestGuardsTest, D_BracesInsideAStringAreNotNesting)
{
    /* ⭐ THE CASE A DEPTH COUNT WRITTEN ON THE RAW TEXT FAILS. Five thousand
     * opening brackets, all of them inside ONE string, in a document two
     * levels deep. A counter that did not skip strings would refuse it, and it
     * would refuse it for a reason no client could ever guess.
     */
    loadGuardHouse();

    EXPECT_TRUE(isGetHomeAnswer(httpBodyFor(
                    httpRawBody("\"action\":\"get_home\",\"probe\":\"" +
                                repeated('[', 5000) + "\""))))
            << "brackets inside a string are counted as nesting";

    EXPECT_TRUE(isGetHomeAnswer(httpBodyFor(
                    httpRawBody("\"action\":\"get_home\",\"probe\":\"" +
                                repeated('{', 5000) + "\""))))
            << "braces inside a string are counted as nesting";
}

TEST_F(JsonApiRequestGuardsTest, D_AnEscapedQuoteDoesNotCloseTheString)
{
    //The second way to get the string skipping wrong: treat \" as the closing
    //quote and everything after it becomes nesting.
    loadGuardHouse();

    EXPECT_TRUE(isGetHomeAnswer(httpBodyFor(
                    httpRawBody("\"action\":\"get_home\",\"probe\":\"x\\\"" +
                                repeated('[', 5000) + "\""))))
            << "an escaped quote was read as the end of the string";
}

TEST_F(JsonApiRequestGuardsTest, D_AnEscapedBackslashClosesTheStringSoWhatFollowsCountsToday)
{
    //And the third: a backslash that escapes a BACKSLASH does not escape the
    //quote that follows it. Get this wrong in the other direction and a
    //scanner never leaves the string again, so nothing is ever counted and the
    //cap becomes decorative.
    loadGuardHouse();

    EXPECT_TRUE(isGetHomeAnswer(httpBodyFor(
                    httpRawBody("\"action\":\"get_home\",\"probe\":\"x\\\\\",\"deep\":" +
                                brackets(JANSSON_DEPTH_CAP)))))
            << "this document is already refused";
}

TEST_F(JsonApiRequestGuardsTest, D_ARawNulEndsTheTextForTheDepthCountToo)
{
    /* INVARIANT, and it is the reason the depth count stops at the first zero
     * byte. Both parsers treat 0x00 as end of input - json.hpp lists '\0' next
     * to eof() in its lexer - so bytes past it are never parsed. A depth count
     * that read them would refuse a document the parser never sees.
     */
    loadGuardHouse();

    EXPECT_TRUE(isGetHomeAnswer(httpBodyFor(
                    httpRawBody("\"action\":\"get_home\"") +
                    std::string("\0", 1) + repeated('[', 5000))))
            << "the depth count read past the zero byte the parser stops at";
}

/*******************************************************************************
 * B_ - THE ESCAPED NUL, PAST THE API AND INTO THE CONFIGURATION.
 *
 * ⛔ NO GUARD IS ADDED ON THIS. Accepting the escaped NUL was decided when the
 * parser was changed, and the payload writer was then reworked so the byte
 * travels WHOLE through the API. These cases pin where it stops travelling.
 ******************************************************************************/

TEST_F(JsonApiRequestGuardsTest, B_ANulInAParamValueIsTruncatedWhenItIsWrittenToIoXml)
{
    /* ⭐ THE LAYER NOBODY HAD MEASURED. The API carries the three bytes in and
     * the three bytes out; io.xml gets ONE. XmlUtils::setAttribute() ends in
     * pugi::xml_attribute::set_value(value.c_str()), and a C string stops at
     * the zero byte - the same shape of defect as the payload truncation that
     * was fixed, one layer lower and still there.
     *
     * The probe is asymmetric ("head" before the zero byte, "tail" after) so a
     * truncation, a drop and a replacement are three different answers.
     */
    loadGuardHouse();

    ASSERT_EQ("HTTP/1.0 200 OK", setParamRaw(T358_IO_STRING, "t358_nul",
                                             "\"head\\u0000tail\""));

    IOBase *io = ListeRoom::Instance().get_io(T358_IO_STRING);
    ASSERT_TRUE(io != nullptr);
    ASSERT_EQ(NUL_INSIDE, io->get_param("t358_nul"));
    ASSERT_EQ(9u, io->get_param("t358_nul").size()) << "the value was already short in memory";

    saveConfig();
    const std::string xml = ioXmlOnDisk();

    EXPECT_TRUE(contains(xml, "t358_nul=\"head\""))
            << "the attribute is not the truncated value either: " << xml;
    EXPECT_FALSE(contains(xml, "tail"))
            << "everything after the zero byte survived after all: " << xml;
    EXPECT_EQ(std::string::npos, xml.find('\0'))
            << "a raw zero byte was written into the configuration file";
}

TEST_F(JsonApiRequestGuardsTest, B_TheTruncationIsInvisibleUntilTheConfigurationIsReloaded)
{
    /* The part that makes it worth a finding rather than a footnote: nothing
     * observable changes when the file is written. The API keeps answering the
     * nine bytes it was given, and the amputation only surfaces at the next
     * start of the server - by which time the request that caused it is long
     * gone from any log.
     */
    loadGuardHouse();

    ASSERT_EQ("HTTP/1.0 200 OK", setParamRaw(T358_IO_STRING, "t358_nul",
                                             "\"head\\u0000tail\""));
    saveConfig();

    EXPECT_EQ(NUL_INSIDE,
              ListeRoom::Instance().get_io(T358_IO_STRING)->get_param("t358_nul"))
            << "writing the file changed the value in memory";

    reloadFromDisk();

    IOBase *io = ListeRoom::Instance().get_io(T358_IO_STRING);
    ASSERT_TRUE(io != nullptr) << "the IO did not survive the reload";
    EXPECT_EQ(std::string("head"), io->get_param("t358_nul"))
            << "the value that came back is not the truncated one";
}

TEST_F(JsonApiRequestGuardsTest, B_ANulInAParamNameOverwritesTheNeighbourItTruncatesInto)
{
    /* ⭐ THE WORST OF THE THREE, and it needs the NUL in the NAME rather than
     * in the value. pugi::xml_node::attribute(name.c_str()) truncates the same
     * way, so a param called "name\0anything" is written as the attribute
     * "name" - and "name" is the IO's display name. One request renames an IO
     * on disk without ever touching the name in memory.
     */
    loadGuardHouse();

    IOBase *io = ListeRoom::Instance().get_io(T358_IO_STRING);
    ASSERT_TRUE(io != nullptr);
    ASSERT_EQ(std::string("String value"), io->get_param("name"));

    ASSERT_EQ("HTTP/1.0 200 OK", setParamRaw(T358_IO_STRING, "name\\u0000squat",
                                             "\"squatted\""));

    //In memory the two params coexist: Params is a std::map and the keys differ.
    EXPECT_EQ(std::string("String value"), io->get_param("name"));
    EXPECT_EQ(std::string("squatted"), io->get_param(std::string("name\0squat", 10)));

    saveConfig();
    const std::string xml = ioXmlOnDisk();

    EXPECT_TRUE(contains(xml, "name=\"squatted\""))
            << "the squatting attribute is not on disk: " << xml;
    EXPECT_FALSE(contains(xml, "name=\"String value\""))
            << "the real name survived, so this case measures nothing: " << xml;

    reloadFromDisk();
    IOBase *back = ListeRoom::Instance().get_io(T358_IO_STRING);
    ASSERT_TRUE(back != nullptr);
    EXPECT_EQ(std::string("squatted"), back->get_param("name"))
            << "the IO came back with its original name after all";
}

TEST_F(JsonApiRequestGuardsTest, B_ANulInAnAutoscenarioActionCutsTheWholeEncodedStep)
{
    /* ⭐ WHY THE TRUNCATION IS NOT WORTH ONE VALUE. An autoscenario encodes a
     * whole step - every action, separated by '|' - into ONE parameter, and
     * that parameter is one XML attribute. A zero byte inside the first action
     * therefore takes the rest of the step with it.
     *
     * The percent-encoding of the params codec does not help: it escapes '%',
     * '|' and '=', and the zero byte is none of the three.
     */
    loadGuardHouse();

    WsTestSession ws;
    ws.send(Json{{ "msg", "autoscenario" }, { "msg_id", "1" },
                 { "data", {{ "type", "create" },
                            { "name", "t358" },
                            { "room_name", T358_ROOM_NAME },
                            { "room_type", T358_ROOM_TYPE },
                            { "steps", Json::array({
                                  Json{{ "pause", "1" },
                                       { "actions", Json::array({
                                             Json{{ "io", T358_IO_STRING },
                                                  { "value", std::string("a\0b", 3) }},
                                             Json{{ "io", T358_IO_BOOL },
                                                  { "value", "true" }}
                                         }) }}
                              }) }} }});
    pumpEventLoop();

    IOBase *sc = ListeRoom::Instance().get_io(T358_SCENARIO_IO_ID);
    ASSERT_TRUE(sc != nullptr) << "the scenario was not created";

    //The step parameter holds both actions, and the zero byte sits inside it.
    std::string packed;
    for (Params::const_iterator it = sc->get_params().cbegin();
         it != sc->get_params().cend(); ++it)
    {
        if (it->second.find('\0') != std::string::npos &&
            it->second.find(T358_IO_BOOL) != std::string::npos)
            packed = it->second;
    }
    ASSERT_FALSE(packed.empty())
            << "no single parameter carries both actions and the zero byte; "
               "the encoding changed and this case measures nothing";

    saveConfig();

    /* The attribute alone, not the whole file: t358_bool is also the id of a
     * declared IO, and searching the document for it would find that instead.
     */
    const std::string xml = ioXmlOnDisk();
    const size_t at = xml.find("as_s0_actions=\"");
    ASSERT_NE(std::string::npos, at) << "the step attribute is not on disk: " << xml;
    const size_t from = at + strlen("as_s0_actions=\"");
    const std::string written = xml.substr(from, xml.find('"', from) - from);

    EXPECT_EQ(std::string(T358_IO_STRING) + "=a", written)
            << "the step was not cut at the zero byte";
    EXPECT_EQ(std::string::npos, written.find(T358_IO_BOOL))
            << "the second action survived the write, so nothing was cut";
}

TEST_F(JsonApiRequestGuardsTest, B_APlainValueWithNoNulMakesTheWholeRoundTrip)
{
    //THE CONTROL. Without it, every case above could be read as "the XML
    //writer loses parameters", which is not what is being measured.
    loadGuardHouse();

    ASSERT_EQ("HTTP/1.0 200 OK", setParamRaw(T358_IO_STRING, "t358_plain",
                                             "\"headtail\""));
    saveConfig();

    EXPECT_TRUE(contains(ioXmlOnDisk(), "t358_plain=\"headtail\""));

    reloadFromDisk();
    IOBase *io = ListeRoom::Instance().get_io(T358_IO_STRING);
    ASSERT_TRUE(io != nullptr);
    EXPECT_EQ(std::string("headtail"), io->get_param("t358_plain"));
}

TEST_F(JsonApiRequestGuardsTest, B_ANulInAKeyIsPartOfTheKeyAndNeverMatchesTheCommand)
{
    /* Where a NUL in a KEY lands, which is the half the door pinned without
     * following. Keys are compared byte for byte, so a zero byte inside one
     * makes it a different key and the dispatch simply never sees it.
     */
    loadGuardHouse();

    //A NUL-carrying key ALONGSIDE the command: the command still dispatches.
    EXPECT_TRUE(isGetHomeAnswer(httpBodyFor(
                    httpRawBody("\"action\":\"get_home\",\"a\\u0000b\":\"v\""))))
            << "a NUL in a foreign key disturbed the dispatch";

    //A NUL inside the command key itself: "a\0ction" is not "action", so the
    //dispatch never fires and the request falls through to the answer an
    //unknown action gets. The credentials are still valid, so the status line
    //says nothing here - what has to be asserted is the PAYLOAD.
    EXPECT_FALSE(isGetHomeAnswer(httpBodyFor(
                     httpRawBody("\"a\\u0000ction\":\"get_home\""))))
            << "a key carrying a zero byte matched \"action\" anyway";
}

/*******************************************************************************
 * I_ - NUMBERS, AND WHAT THE FLATTENING CONTRACT LEAVES OF THEM.
 *
 * ⛔ NO GUARD IS ADDED ON THIS EITHER. Every case here is a measurement of a
 * contract that is frozen on purpose (Utils::to_string(double), a bare
 * ostringstream) and must not be "fixed" from here.
 ******************************************************************************/

TEST_F(JsonApiRequestGuardsTest, I_AnIntegerBeyondInt64IsServedAndLandsAsSixDigits)
{
    //The literal the previous parser threw the whole document away for. It is
    //parsed into a double and then stringified at the default precision of an
    //ostringstream, which is six significant digits.
    loadGuardHouse();

    ASSERT_EQ("HTTP/1.0 200 OK",
              setParamRaw(T358_IO_STRING, "t358_big", "123456789012345678901234567890"));

    IOBase *io = ListeRoom::Instance().get_io(T358_IO_STRING);
    ASSERT_TRUE(io != nullptr);
    EXPECT_EQ(std::string("1.23457e+29"), io->get_param("t358_big"));
}

TEST_F(JsonApiRequestGuardsTest, I_AnOrdinarySevenDigitIntegerIsMangledExactlyTheSameWay)
{
    /* ⭐ THE CASE THAT DECIDES WHETHER A GUARD ON int64 WOULD BE WORTH
     * ANYTHING. It would not: the precision is not lost at the parser, it is
     * lost at the flattening contract, and the contract has been eating any
     * integer above six digits since long before the parser changed. Refusing
     * the ones that no longer fit in an int64 would close a window in a wall
     * that is not there.
     */
    loadGuardHouse();

    ASSERT_EQ("HTTP/1.0 200 OK", setParamRaw(T358_IO_STRING, "t358_seven", "1234567"));
    ASSERT_EQ("HTTP/1.0 200 OK",
              setParamRaw(T358_IO_STRING, "t358_int64max", "9223372036854775807"));

    IOBase *io = ListeRoom::Instance().get_io(T358_IO_STRING);
    ASSERT_TRUE(io != nullptr);
    EXPECT_EQ(std::string("1.23457e+06"), io->get_param("t358_seven"));
    EXPECT_EQ(std::string("9.22337e+18"), io->get_param("t358_int64max"));
}

TEST_F(JsonApiRequestGuardsTest, I_ASmallIntegerStillLandsExactly)
{
    //THE CONTROL for the two cases above: the flattening contract is not
    //simply broken, it has a range in which it is exact.
    loadGuardHouse();

    ASSERT_EQ("HTTP/1.0 200 OK", setParamRaw(T358_IO_STRING, "t358_small", "42"));

    IOBase *io = ListeRoom::Instance().get_io(T358_IO_STRING);
    ASSERT_TRUE(io != nullptr);
    EXPECT_EQ(std::string("42"), io->get_param("t358_small"));
}

TEST_F(JsonApiRequestGuardsTest, I_TheSameDigitsSentAsAStringAreStoredExactly)
{
    /* ⭐ THE ESCAPE HATCH THAT ALREADY EXISTS, and the reason a refusal at the
     * parser would cost a client more than it buys. Every param is a string
     * once it is stored; a client that needs its digits back sends them as a
     * JSON string and gets all thirty of them.
     */
    loadGuardHouse();

    ASSERT_EQ("HTTP/1.0 200 OK",
              setParamRaw(T358_IO_STRING, "t358_asstring",
                          "\"123456789012345678901234567890\""));

    IOBase *io = ListeRoom::Instance().get_io(T358_IO_STRING);
    ASSERT_TRUE(io != nullptr);
    EXPECT_EQ(std::string("123456789012345678901234567890"),
              io->get_param("t358_asstring"));
}
