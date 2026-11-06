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

/*******************************************************************************
 * E4.1d - CHARACTERIZATION of the Philips Hue state reader.
 *
 * READ ONLY, and that is the whole point of this ticket.
 * HueOutputLightRGB polls http://<bridge>/api/<key>/lights/<id> every two
 * seconds and decodes state.{sat,bri,hue,on,reachable} out of the answer. It
 * NEVER builds JSON: setOff() sends the literal "{\"on\":false}" and
 * setColor() concatenates three integers into a string. There is not one
 * json_dumps() in the file, before or after this migration.
 *
 * ⭐ WHAT THAT MEANS FOR THE BYTE INVARIANTS OF E4.1, SAID PLAINLY SO THAT
 *    NOBODY BELIEVES IN A PROTECTION THIS FILE DOES NOT GIVE:
 *
 *   THERE IS NO dump() IN THIS PERIMETER AT ALL. ensure_ascii and
 *   error_handler_t::replace have NOTHING to apply to here - not "they are
 *   defensive", not "they are untested": there is no emission. This file
 *   therefore carries NO byte oracle and claims none. The byte oracles of the
 *   ticket live in tests/SqueezeboxWire_test.cpp, on the one dump() of the
 *   perimeter (a cDebug() trace).
 *
 *   The only escaping this file can see is on the way IN, and it is pinned:
 *   NonAsciiInTheAnswerIsDecodedBackToRawUtf8 - a Hue bridge escapes the light
 *   name it echoes back, and the decoder must give the bytes back intact.
 *
 * ⛔ THE REAL RISK OF THIS FILE IS NOT ESCAPING, IT IS TYPE TOLERANCE, and it
 * is measured here key by key. Under jansson, json_integer_value() answers 0
 * for an ABSENT key AND for a key of the wrong type - a float, a string, a
 * boolean, a null - without ever failing. Under nlohmann the "obvious" port
 * j.value("sat", 0) does NOT reproduce that on three shapes out of five
 * (measured, and pinned by the tripwire at the end of this file):
 *      {"sat":12.5}   jansson 0   ->   value("sat",0) = 12
 *      {"sat":"77"}   jansson 0   ->   value("sat",0) THROWS type_error.302
 *      {"sat":null}   jansson 0   ->   value("sat",0) THROWS type_error.302
 * And a throw here is a throw inside a UrlDownloader completion callback,
 * with no try/catch anywhere on the path: std::terminate, server dead, every
 * two seconds. That is the same family of defect as the KNX one, reached from
 * a THIRD PARTY device instead of a bus.
 *
 * THE SEAMS are the two free functions of the anonymous namespace below. They
 * carried the jansson body of HueOutputLightRGB.cpp VERBATIM in the
 * characterization commit; since the migration commit they FORWARD to the
 * SHIPPED header IO/Hue/HueWire.h - the very header HueOutputLightRGB.cpp
 * itself includes - so that a mutation of the PRODUCTION decoder turns this
 * suite red.
 * ⭐ NOT ONE ASSERTION OF THIS FILE MOVED with that rewiring, and that is the
 * result: nothing observable changes here. The only MOVED BY E4.1d marks below
 * are on the three type aliases and on the tripwire, which swapped the jansson
 * column of its table for the nlohmann one.
 *
 * ⚠️ WHAT THIS FILE STILL DOES NOT COVER, measured and consigned rather than
 * hidden: the polling lambda itself - the URL it builds, the three cErrorDom
 * lines, and the call to updateHueState(). HueWire::toStateUpdate() exists
 * precisely to pull the reachable/on branch OUT of that lambda and under test;
 * what is left at the call site is two statements with no branch.
 *
 * A DELIBERATELY RICH FIXTURE. sat, bri and hue carry THREE DIFFERENT values
 * whose THREE SCALED RESULTS are also different (200 -> 78%, 100 -> 39%,
 * 30000 -> 164 deg), and on/reachable are always given OPPOSITE values in the
 * same payload. SwappingSatAndBriInTheAnswerIsVisible is the explicit
 * counter-mutation the fiche demands: sat == bri is the exact trap of the nine
 * recorded "poor fixture" relapses of this series.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <string>
#include <cstdio>

#include "Utils.h"
#include "Params.h"
#include "ColorUtils.h"

/* THE PRODUCTION HEADER. Not a copy of it: the very text
 * HueOutputLightRGB.cpp includes and the server ships. */
#include "HueWire.h"

using std::string;

namespace
{

/*---------------------------------------------------------------------------
 * The outcome of decoding one bridge answer.
 *
 * HueOutputLightRGB.cpp answers FOUR different things today and logs three
 * different lines, so they are four outcomes and not one "failed":
 *   - Ok         : root is an object and root.state is an object;
 *   - Malformed  : json_loads() answered NULL -> cErrorDom "Json received
 *                  malformed : ...". ⚠️ Under jansson a TOP LEVEL SCALAR lands
 *                  here, because json_loads() has no JSON_DECODE_ANY;
 *   - NotAnObject: parsed, but the root is not an object (a top level ARRAY:
 *                  the Hue bridge really does answer `[{"error":{...}}]` on a
 *                  bad API key) -> cErrorDom "Protocol changed ? ...";
 *   - NoState    : root is an object but "state" is absent or not an object
 *                  -> cErrorDom "Protocol changed ? ..." (the same line).
 * All four end the same way for the user: the poll returns and NO state
 * update is emitted at all - not even an "unreachable" one.
 *
 * MOVED BY E4.1d: Decode, LightState and StateUpdate were local declarations
 * carrying the jansson outcomes; they are now aliases of the SHIPPED ones. No
 * name changed, so no assertion below moved with them.
 *-------------------------------------------------------------------------*/
using Decode = HueWire::Decode;
using LightState = HueWire::LightState;
using StateUpdate = HueWire::StateUpdate;

/*---------------------------------------------------------------------------
 * THE SEAM - decode.
 *
 * Was the jansson body of the HueOutputLightRGB polling lambda copied
 * verbatim; since E4.1d it forwards to the SHIPPED decoder, so a mutation of
 * HueWire::decodeLightState() turns this suite red.
 * RENAMED BY E4.1d (decodeLightState -> decodeWire): LightState is now the
 * shipped type, so an unqualified call to the old name found BOTH this seam
 * and HueWire::decodeLightState() by ADL and did not compile. Same reason for
 * stateUpdateOf() below.
 *-------------------------------------------------------------------------*/
Decode decodeWire(const string &data, LightState &st)
{
    return HueWire::decodeLightState(data, st);
}

/*---------------------------------------------------------------------------
 * THE SEAM - the state update.
 *
 * Was the reachable/on branch of the polling lambda copied verbatim; since
 * E4.1d it forwards to the SHIPPED HueWire::toStateUpdate(). The three
 * scalings sit on TWO axes: hue is 0..65535 over 360 degrees, sat and bri are
 * 0..255 over 100 percent. HueWire::toStateUpdate() takes a NAMED STRUCT so
 * that its three integers cannot be permuted at the call site - a test can
 * only ever catch the permutation INSIDE the function.
 *-------------------------------------------------------------------------*/
StateUpdate stateUpdateOf(const LightState &st)
{
    return HueWire::toStateUpdate(st);
}

/*---------------------------------------------------------------------------
 * Fixture
 *
 * THREE DIFFERENT raw values whose THREE SCALED results are ALSO different:
 *   hue 30000 -> 30000 * 360 / 65535 = 164 degrees
 *   sat   200 ->   200 * 100 / 255   =  78 %
 *   bri   100 ->   100 * 100 / 255   =  39 %
 * so swapping any two of them - in the payload or in the scaling - changes an
 * assertion. on and reachable are given OPPOSITE values.
 *-------------------------------------------------------------------------*/
const int FX_HUE = 30000;
const int FX_SAT = 200;
const int FX_BRI = 100;

const int FX_HUE_DEG = 164;
const int FX_SAT_PCT = 78;
const int FX_BRI_PCT = 39;

//A real Hue bridge answer for one light, trimmed to the object the driver
//walks plus the siblings it must ignore.
const char *const HUE_LIGHT_ANSWER =
        "{"
        "\"state\":{"
            "\"on\":true,"
            "\"bri\":100,"
            "\"hue\":30000,"
            "\"sat\":200,"
            "\"effect\":\"none\","
            "\"colormode\":\"hs\","
            "\"reachable\":false"
        "},"
        "\"type\":\"Extended color light\","
        "\"name\":\"n-Lampe Salon\","
        "\"modelid\":\"m-LCT001\""
        "}";

string escaped(const string &s)
{
    string out;
    char buf[8];
    for (unsigned char c: s)
    {
        if (c >= 0x20 && c < 0x7f)
            out += static_cast<char>(c);
        else
        {
            snprintf(buf, sizeof(buf), "\\x%02x", c);
            out += buf;
        }
    }
    return out;
}

//Builds a bridge answer whose "state" carries exactly the five fields given,
//as RAW JSON TEXT so that a case can put any type it likes in any of them.
string answerWith(const string &sat, const string &bri, const string &hue,
                  const string &on, const string &reachable)
{
    return "{\"state\":{\"sat\":" + sat + ",\"bri\":" + bri +
           ",\"hue\":" + hue + ",\"on\":" + on +
           ",\"reachable\":" + reachable + "},\"name\":\"n-Lampe Salon\"}";
}

} //namespace

/*******************************************************************************
 * DECODING - structure and values
 ******************************************************************************/

//The nominal answer. Note that on and reachable are OPPOSITE here: a decoder
//that read one into the other's slot shows.
TEST(HueWire, DecodesAWellFormedBridgeAnswer)
{
    LightState st;
    ASSERT_EQ(Decode::Ok, decodeWire(HUE_LIGHT_ANSWER, st));

    EXPECT_EQ(FX_SAT, st.sat);
    EXPECT_EQ(FX_BRI, st.bri);
    EXPECT_EQ(FX_HUE, st.hue);
    EXPECT_TRUE(st.on);
    EXPECT_FALSE(st.reachable);
}

//⭐ THE COUNTER-MUTATION BY EXCHANGE the fiche asks for, written as a case so
//that it can never be forgotten: sat and bri are SWAPPED in the payload, and
//both the decoded state and the resulting colour must change. If this passes
//with the same values as the case above, the fixture is too poor - which is
//exactly the trap sat == bri sets.
TEST(HueWire, SwappingSatAndBriInTheAnswerIsVisible)
{
    LightState straight;
    ASSERT_EQ(Decode::Ok, decodeWire(answerWith("200", "100", "30000",
                                                      "true", "true"), straight));
    LightState swapped;
    ASSERT_EQ(Decode::Ok, decodeWire(answerWith("100", "200", "30000",
                                                      "true", "true"), swapped));

    //the two raw fields really did move
    EXPECT_EQ(200, straight.sat);
    EXPECT_EQ(100, straight.bri);
    EXPECT_EQ(100, swapped.sat);
    EXPECT_EQ(200, swapped.bri);
    EXPECT_NE(straight.sat, straight.bri);

    //and the colour that comes out of them is not the same colour
    const StateUpdate a = stateUpdateOf(straight);
    const StateUpdate b = stateUpdateOf(swapped);
    EXPECT_EQ(FX_SAT_PCT, a.color.getHSLSaturation());
    EXPECT_EQ(FX_BRI_PCT, a.color.getHSLLightness());
    EXPECT_EQ(FX_BRI_PCT, b.color.getHSLSaturation());
    EXPECT_EQ(FX_SAT_PCT, b.color.getHSLLightness());
    EXPECT_TRUE(a.color != b.color);
}

//The hue field is scaled on its own axis (0..65535 over 360 degrees), the two
//others on a shared one (0..255 over 100 percent). Three raw values, three
//DIFFERENT scaled results, pinned as literals so a permuted scaling shows.
TEST(HueWire, TheThreeScalingsAreThreeDifferentAxes)
{
    LightState st;
    ASSERT_EQ(Decode::Ok, decodeWire(HUE_LIGHT_ANSWER, st));
    st.reachable = true; //so the colour branch is taken

    const StateUpdate u = stateUpdateOf(st);

    ASSERT_TRUE(u.color.isValid());
    EXPECT_EQ(FX_HUE_DEG, u.color.getHSLHue());
    EXPECT_EQ(FX_SAT_PCT, u.color.getHSLSaturation());
    EXPECT_EQ(FX_BRI_PCT, u.color.getHSLLightness());
    EXPECT_EQ("hsla(164, 78%, 39%, 1)", u.color.toString());
}

//The reachable / on matrix, all four combinations. An UNREACHABLE light is
//reported with an INVALID colour and off, whatever "on" said - the driver
//passes reachable, not on, as the second argument in that branch.
TEST(HueWire, AnUnreachableLightIsReportedInvalidAndOffWhateverOnSaid)
{
    LightState st;

    st = LightState(); st.hue = FX_HUE; st.sat = FX_SAT; st.bri = FX_BRI;
    st.on = true; st.reachable = true;
    EXPECT_TRUE(stateUpdateOf(st).color.isValid());
    EXPECT_TRUE(stateUpdateOf(st).on);

    st.on = false; st.reachable = true;
    EXPECT_TRUE(stateUpdateOf(st).color.isValid());
    EXPECT_FALSE(stateUpdateOf(st).on);

    st.on = true; st.reachable = false;
    EXPECT_FALSE(stateUpdateOf(st).color.isValid());
    EXPECT_FALSE(stateUpdateOf(st).on)
            << "an unreachable light reported itself ON: the branch passes "
               "reachable as the state, not on";
    EXPECT_EQ("#000000", stateUpdateOf(st).color.toString());

    st.on = false; st.reachable = false;
    EXPECT_FALSE(stateUpdateOf(st).color.isValid());
    EXPECT_FALSE(stateUpdateOf(st).on);
}

/*******************************************************************************
 * DECODING - THE TYPE TOLERANCE CONTRACT
 *
 * This is where a mechanical port breaks the driver. Every case below is a
 * shape json_integer_value() / jansson_bool_get() answer a DEFAULT for, in
 * silence, without ever failing.
 ******************************************************************************/

//ABSENT integer keys decode to 0 and MUST NOT throw. HueOutputLightRGB.cpp
//reads sat/bri/hue with no presence check at all, so this is the shipped
//contract - and a Hue bridge really does omit them: a plain white
//dimmable light (LWB010) has bri but NO hue and NO sat.
TEST(HueWire, AbsentIntegerKeysDecodeToZeroWithoutThrowing)
{
    LightState st;
    ASSERT_EQ(Decode::Ok,
              decodeWire("{\"state\":{\"bri\":100,\"on\":true,"
                               "\"reachable\":true}}", st));

    EXPECT_EQ(0, st.sat);
    EXPECT_EQ(0, st.hue);
    EXPECT_EQ(100, st.bri);   //the one that IS there is not defaulted too
    EXPECT_TRUE(st.on);
    EXPECT_TRUE(st.reachable);
}

//⛔ INTEGER KEYS OF THE WRONG TYPE decode to 0. All five wrong shapes, one
//case each, because the "obvious" nlohmann port diverges on three of them and
//THROWS on two - inside a download callback with no try/catch on the path.
TEST(HueWire, IntegerKeysOfTheWrongTypeDecodeToZero)
{
    struct { const char *label; const char *sat; } shapes[] = {
        { "float",   "12.5" },
        { "string",  "\"77\"" },
        { "boolean", "true" },
        { "null",    "null" },
        { "object",  "{\"v\":77}" },
        { "array",   "[77]" },
    };

    for (const auto &s: shapes)
    {
        LightState st;
        ASSERT_EQ(Decode::Ok,
                  decodeWire(answerWith(s.sat, "100", "30000",
                                              "true", "true"), st))
                << s.label;
        EXPECT_EQ(0, st.sat) << s.label;
        //the two well typed siblings are untouched: the default did not leak
        EXPECT_EQ(100, st.bri) << s.label;
        EXPECT_EQ(30000, st.hue) << s.label;
    }
}

//BOOLEAN keys of the wrong type - and an ABSENT one - fall back to FALSE.
//jansson_bool_get()'s contract, reproduced exactly: only a real JSON boolean
//is read, the number 1 and the string "true" are NOT booleans.
TEST(HueWire, BooleanKeysOfTheWrongTypeOrAbsentFallBackToFalse)
{
    LightState st;
    ASSERT_EQ(Decode::Ok, decodeWire(answerWith("200", "100", "30000",
                                                      "1", "\"true\""), st));
    EXPECT_FALSE(st.on)        << "the NUMBER 1 was read as a boolean";
    EXPECT_FALSE(st.reachable) << "the STRING \"true\" was read as a boolean";

    LightState absent;
    ASSERT_EQ(Decode::Ok,
              decodeWire("{\"state\":{\"sat\":200,\"bri\":100,"
                               "\"hue\":30000}}", absent));
    EXPECT_FALSE(absent.on);
    EXPECT_FALSE(absent.reachable);

    //and a real boolean IS read, both ways round, in the same payload
    LightState ok;
    ASSERT_EQ(Decode::Ok, decodeWire(answerWith("200", "100", "30000",
                                                      "false", "true"), ok));
    EXPECT_FALSE(ok.on);
    EXPECT_TRUE(ok.reachable);
}

//NEGATIVE and out-of-range integers are taken as they come: the driver does no
//clamping today and this migration does not add any. Pinned so that adding one
//later is a deliberate act with a red test to change.
TEST(HueWire, NegativeAndOversizedIntegersArePassedThroughUnclamped)
{
    LightState st;
    ASSERT_EQ(Decode::Ok, decodeWire(answerWith("-5", "999", "70000",
                                                      "true", "true"), st));
    EXPECT_EQ(-5, st.sat);
    EXPECT_EQ(999, st.bri);
    EXPECT_EQ(70000, st.hue);
}

/*******************************************************************************
 * DECODING - THE GUARD CONTRACT
 ******************************************************************************/

//MALFORMED answers are refused with the "Json received malformed" outcome.
TEST(HueWire, MalformedAnswersAreRefused)
{
    LightState st;
    EXPECT_EQ(Decode::Malformed, decodeWire("", st));
    EXPECT_EQ(Decode::Malformed, decodeWire("{oops", st));
    EXPECT_EQ(Decode::Malformed, decodeWire("{\"state\":{}} trailing", st));
    EXPECT_EQ(Decode::Malformed, decodeWire("{\"state\":{}", st));
    EXPECT_EQ(Decode::Malformed, decodeWire("<html>401</html>", st));
}

//⛔ THE ACCEPTANCE SET, measured on jansson with no decode flag: a TOP LEVEL
//SCALAR is a PARSE ERROR (json_loads has no JSON_DECODE_ANY), so a bridge
//answering `null` or `"ok"` lands in "malformed", not in "protocol changed".
//nlohmann's parse() accepts all four of them, so without an explicit guard the
//migration would move them from one branch to the other. Same user outcome,
//different acceptance set - and the fiche requires the guard contract to stay
//EXACTLY today's.
TEST(HueWire, TopLevelScalarsAreMalformedAndTopLevelArraysAreNotAnObject)
{
    LightState st;
    EXPECT_EQ(Decode::Malformed, decodeWire("3", st));
    EXPECT_EQ(Decode::Malformed, decodeWire("\"ok\"", st));
    EXPECT_EQ(Decode::Malformed, decodeWire("true", st));
    EXPECT_EQ(Decode::Malformed, decodeWire("null", st));

    //a top level ARRAY parses and is refused one step later. This is the shape
    //a real bridge answers with when the API key is wrong:
    //[{"error":{"type":1,"address":"/lights/3","description":"unauthorized user"}}]
    EXPECT_EQ(Decode::NotAnObject, decodeWire("[1,2]", st));
    EXPECT_EQ(Decode::NotAnObject,
              decodeWire("[{\"error\":{\"type\":1,"
                               "\"description\":\"d-unauthorized user\"}}]", st));
}

//⛔ THE TWO MEASURED ACCEPTANCE DIVERGENCES OF THIS MIGRATION, both DECLARED
//and both pinned here so that changing them again is a deliberate act.
//
//(1) AN ESCAPED NUL, "\u0000". jansson REFUSED it outright - "\u0000 is not
//    allowed without JSON_ALLOW_NUL" in a value, "NUL byte in object key not
//    supported" in a key - so the whole answer was Malformed. nlohmann ACCEPTS
//    it and decodes it to a real 0x00 byte inside the std::string. So a bridge
//    answer carrying an escaped NUL moves from Malformed to Ok.
//    ⚠️ A RAW NUL byte is refused by BOTH (jansson: "control character 0x0";
//    nlohmann: discarded), so passing the whole std::string rather than a
//    c_str() digs no hole - measured both ways.
//
//(2) AN INTEGER BEYOND int64. jansson refused the WHOLE document ("too big
//    integer"); nlohmann parses it as a float, which is not is_number_integer(),
//    so integerOrDefault() answers its default. The answer moves from Malformed
//    to Ok with sat = 0.
//    ⚠️ MEASURED, and it is the argument FOR the hand written helper: on that
//    same payload j.value("sat", 0) answers -2147483648, not 0 and not 12.
//
//Neither shape can come from a real Hue bridge. Reproducing (1) would mean
//re-scanning the escapes and (2) re-implementing a number lexer; both were
//judged more expensive than the divergence is worth. What is NOT acceptable is
//that they change again in silence, which is what this case prevents.
TEST(HueWire, TheTwoDeclaredAcceptanceDivergencesFromJansson)
{
    //(1) escaped NUL - jansson said Malformed, we now say Ok
    LightState nulInValue;
    EXPECT_EQ(Decode::Ok,
              decodeWire("{\"name\":\"n-a\\u0000b\",\"state\":{"
                         "\"sat\":200,\"bri\":100,\"hue\":30000,"
                         "\"on\":true,\"reachable\":true}}", nulInValue))
            << "the escaped-NUL divergence changed: re-read the comment above";
    EXPECT_EQ(200, nulInValue.sat);
    EXPECT_TRUE(nulInValue.reachable);

    LightState nulInKey;
    EXPECT_EQ(Decode::Ok,
              decodeWire("{\"n-a\\u0000b\":1,\"state\":{\"sat\":200}}", nulInKey));
    EXPECT_EQ(200, nulInKey.sat);

    //...but a RAW NUL byte is still refused, by both libraries. Built as a
    //std::string with an embedded 0x00 so that nothing truncates it.
    string rawNul = string("{\"name\":\"n-a");
    rawNul.push_back('\0');
    rawNul += "b\",\"state\":{\"sat\":200}}";
    //the probe must be a value that cannot silently become valid: if the NUL
    //ever stops being IN the fixture, the case below proves nothing
    ASSERT_NE(string::npos, rawNul.find('\0')) << "the fixture lost its embedded NUL";
    ASSERT_EQ(36u, rawNul.size()) << "the fixture was truncated at the NUL";
    LightState raw;
    EXPECT_EQ(Decode::Malformed, decodeWire(rawNul, raw))
            << "a RAW NUL byte became acceptable: that one was refused by BOTH "
               "libraries and the guard contract just changed";

    //(2) an integer beyond int64 - jansson said Malformed, we now say Ok/0
    LightState big;
    EXPECT_EQ(Decode::Ok,
              decodeWire("{\"state\":{\"sat\":99999999999999999999,"
                         "\"bri\":100,\"hue\":30000,"
                         "\"on\":true,\"reachable\":true}}", big))
            << "the too-big-integer divergence changed: re-read the comment above";
    EXPECT_EQ(0, big.sat) << "the shipped helper must answer its DEFAULT here; "
                             "j.value(\"sat\", 0) answers -2147483648 on this "
                             "very payload, which is why it is not used";
    //the well typed siblings are untouched: the default did not leak
    EXPECT_EQ(100, big.bri);
    EXPECT_EQ(30000, big.hue);
}

//"state" ABSENT, or present but not an object, is a THIRD outcome and not the
//same as a malformed answer: it means the bridge replied something we could
//read but did not recognise.
TEST(HueWire, AMissingOrNonObjectStateIsItsOwnOutcome)
{
    LightState st;
    EXPECT_EQ(Decode::NoState,
              decodeWire("{\"name\":\"n-Lampe Salon\",\"type\":\"t-x\"}", st));
    EXPECT_EQ(Decode::NoState, decodeWire("{\"state\":\"s-on\"}", st));
    EXPECT_EQ(Decode::NoState, decodeWire("{\"state\":null}", st));
    EXPECT_EQ(Decode::NoState, decodeWire("{\"state\":[1,2]}", st));
    EXPECT_EQ(Decode::NoState, decodeWire("{}", st));

    //an EMPTY state object is NOT this outcome: it decodes, with all defaults
    LightState empty;
    EXPECT_EQ(Decode::Ok, decodeWire("{\"state\":{}}", empty));
    EXPECT_EQ(0, empty.sat);
    EXPECT_EQ(0, empty.bri);
    EXPECT_EQ(0, empty.hue);
    EXPECT_FALSE(empty.on);
    EXPECT_FALSE(empty.reachable);
}

//A REFUSED answer leaves the caller's LightState untouched: the driver returns
//without updating anything, so a half-filled state must never come back.
TEST(HueWire, ARefusedAnswerNeverPartiallyFillsTheState)
{
    LightState st;
    st.sat = 111; st.bri = 222; st.hue = 333; st.on = true; st.reachable = true;

    ASSERT_EQ(Decode::NoState, decodeWire("{\"state\":\"s-on\"}", st));
    EXPECT_EQ(111, st.sat);
    EXPECT_EQ(222, st.bri);
    EXPECT_EQ(333, st.hue);
    EXPECT_TRUE(st.on);
    EXPECT_TRUE(st.reachable);

    ASSERT_EQ(Decode::Malformed, decodeWire("{oops", st));
    EXPECT_EQ(111, st.sat);
    EXPECT_EQ(222, st.bri);
}

//NON-ASCII on the way IN. A Hue bridge escapes what it echoes back, and the
//light name is free text the user typed in the Hue app. Nothing here is
//emitted anywhere, so this is a DECODING assertion and not a byte oracle - it
//is the only escaping this perimeter can see at all.
TEST(HueWire, NonAsciiInTheAnswerIsDecodedBackToRawUtf8)
{
    const string answer = "{\"name\":\"n-Entr\\u00e9""e\",\"state\":{"
                          "\"sat\":200,\"bri\":100,\"hue\":30000,"
                          "\"on\":true,\"reachable\":true}}";

    LightState st;
    ASSERT_EQ(Decode::Ok, decodeWire(answer, st)) << escaped(answer);
    EXPECT_EQ(200, st.sat);
    EXPECT_TRUE(st.reachable);

    //raw UTF-8 in the answer is accepted too
    LightState raw;
    ASSERT_EQ(Decode::Ok,
              decodeWire("{\"name\":\"n-Entr\xc3\xa9""e\",\"state\":{"
                               "\"sat\":200,\"bri\":100,\"hue\":30000,"
                               "\"on\":true,\"reachable\":true}}", raw));
    EXPECT_EQ(200, raw.sat);
}

//INVALID UTF-8 from the bridge is refused at the PARSE, by both libraries, so
//it never reaches the decoder. There is no dump() in this perimeter for it to
//reach anyway - this case is here so the acceptance set is complete and so the
//KNX defect ("raw device bytes reach a bare dump()") can be ruled out at the
//source rather than by assertion.
TEST(HueWire, InvalidUtf8FromTheBridgeIsRefusedAtTheParse)
{
    LightState st;
    EXPECT_EQ(Decode::Malformed,
              decodeWire("{\"name\":\"n-b\xff""ad\",\"state\":{\"sat\":200}}", st));
    EXPECT_EQ(Decode::Malformed, decodeWire("{\"name\":\"x\xc3\"}", st));
    EXPECT_EQ(Decode::Malformed, decodeWire("{\"name\":\"\\ud800\"}", st));
}

/*******************************************************************************
 * TRIPWIRE - the "obvious simplification" that would kill the driver
 ******************************************************************************/

//⛔ WHY THE INTEGER READS ARE WRITTEN BY HAND AND NOT AS j.value("sat", 0).
//
//The fiche of this ticket recommends j.value("sat", 0). MEASURED, IT IS WRONG
//ON THREE SHAPES OUT OF FIVE, and two of those three are a THROW inside a
//UrlDownloader completion callback with no try/catch on the path -
//std::terminate, on a poll that repeats every two seconds:
//
//    payload        json_integer_value()     j.value("sat", 0)
//    {"sat":12.5}   0                        12          (silent wrong value)
//    {"sat":"77"}   0                        THROWS 302
//    {"sat":null}   0                        THROWS 302
//    {"sat":true}   0                        1           (silent wrong value)
//    absent         0                        0           (agree)
//
//This case pins the nlohmann column, on the shipped library, and next to it
//the answer HueWire::integerOrDefault() gives on the same five shapes - which
//is the jansson column, kept by hand. It exists so that the next reader who
//reaches for the "obvious" one-liner is told, in one red line, what it costs.
TEST(HueWire, Tripwire_ValueWithDefaultIsNotASubstituteForTheIntegerContract)
{
    const Json j = Json::parse(string("{\"f\":12.5,\"s\":\"77\",\"n\":null,"
                                      "\"b\":true,\"i\":42}"), nullptr, false);
    ASSERT_FALSE(j.is_discarded());

    //MOVED BY E4.1d: this case used to measure the jansson column of the table
    //above, on the shipped jansson. It now measures the nlohmann column, on
    //the shipped nlohmann - the one that says why the "obvious" one-liner is
    //not usable here.
    EXPECT_THROW(j.value("s", 0), Json::exception)
            << "j.value() stopped throwing on a STRING: re-measure the whole "
               "table before substituting it for HueWire::integerOrDefault()";
    EXPECT_THROW(j.value("n", 0), Json::exception)
            << "j.value() stopped throwing on a NULL: re-measure the whole "
               "table before substituting it for HueWire::integerOrDefault()";
    EXPECT_EQ(12, j.value("f", 0)) << "j.value() truncates a float to 12 where "
                                      "json_integer_value() answered 0";
    EXPECT_EQ(1, j.value("b", 0)) << "j.value() reads a boolean as 1 where "
                                     "json_integer_value() answered 0";

    //...and the shipped reader answers the jansson default on every one of
    //them, without ever throwing. THIS is the contract.
    EXPECT_EQ(0, HueWire::integerOrDefault(j, "f"));
    EXPECT_EQ(0, HueWire::integerOrDefault(j, "s"));
    EXPECT_EQ(0, HueWire::integerOrDefault(j, "n"));
    EXPECT_EQ(0, HueWire::integerOrDefault(j, "b"));
    EXPECT_EQ(0, HueWire::integerOrDefault(j, "absent"));

    //and the one well typed key really is read: this is not "everything is 0"
    EXPECT_EQ(42, HueWire::integerOrDefault(j, "i"));
}
