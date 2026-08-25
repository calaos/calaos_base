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
 * THE SEAMS are the two free functions of the anonymous namespace below. In
 * this commit they carry the jansson body of HueOutputLightRGB.cpp VERBATIM
 * (this is the characterization commit: zero src/). The migration commit
 * rewires them onto the SHIPPED header IO/Hue/HueWire.h, so that a mutation of
 * the PRODUCTION decoder turns this suite red. Every assertion that moves in
 * that commit is flagged MOVED BY E4.1d, in place - and the count is expected
 * to be ZERO, because nothing observable is supposed to change here.
 *
 * ⚠️ WHAT THIS FILE STILL DOES NOT COVER, measured and consigned rather than
 * hidden: the polling lambda itself - the URL it builds, the three cErrorDom
 * lines, and the call to updateHueState(). toStateUpdate() exists precisely to
 * pull the reachable/on branch OUT of that lambda and under test; what is left
 * at the call site is two statements with no branch.
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

#include <jansson.h>

#include "Utils.h"
#include "Params.h"
#include "ColorUtils.h"
#include "Jansson_Addition.h"

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
 * MOVED BY E4.1d: this local enum becomes an alias of the SHIPPED
 * HueWire::Decode in the migration commit. The enumerator names do not change,
 * so no assertion below moves with it.
 *-------------------------------------------------------------------------*/
enum class Decode
{
    Ok,
    Malformed,
    NotAnObject,
    NoState,
};

//The five fields the driver reads, and nothing else.
struct LightState
{
    int sat = 0;
    int bri = 0;
    int hue = 0;
    bool on = false;
    bool reachable = false;
};

//What the driver hands to updateHueState(): a colour and a state.
struct StateUpdate
{
    ColorValue color;
    bool on = false;
};

/*---------------------------------------------------------------------------
 * THE SEAM - decode.
 *
 * Verbatim body of the HueOutputLightRGB polling lambda
 * (HueOutputLightRGB.cpp:58-86). MOVED BY E4.1d onto
 * HueWire::decodeLightState().
 *-------------------------------------------------------------------------*/
Decode decodeLightState(const string &data, LightState &st)
{
    json_error_t error;
    json_t *root = json_loads(data.c_str(), 0, &error);
    if (!root)
        return Decode::Malformed;

    if (!json_is_object(root))
    {
        json_decref(root);
        return Decode::NotAnObject;
    }

    json_t *tstate = json_object_get(root, "state");
    if (!tstate || !json_is_object(tstate))
    {
        json_decref(root);
        return Decode::NoState;
    }

    st.sat = json_integer_value(json_object_get(tstate, "sat"));
    st.bri = json_integer_value(json_object_get(tstate, "bri"));
    st.hue = json_integer_value(json_object_get(tstate, "hue"));
    st.on = jansson_bool_get(tstate, "on");
    st.reachable = jansson_bool_get(tstate, "reachable");

    json_decref(root);
    return Decode::Ok;
}

/*---------------------------------------------------------------------------
 * THE SEAM - the state update.
 *
 * Verbatim body of HueOutputLightRGB.cpp:90-95, branch included. The three
 * scalings are the shipped ones: hue is 0..65535 over 360 degrees, sat and bri
 * are 0..255 over 100 percent. MOVED BY E4.1d onto HueWire::toStateUpdate(),
 * which takes a NAMED STRUCT so that its three integers cannot be permuted at
 * the call site.
 *-------------------------------------------------------------------------*/
StateUpdate toStateUpdate(const LightState &st)
{
    StateUpdate u;

    if (!st.reachable)
    {
        //updateHueState(ColorValue(), reachable) - and reachable is false here
        u.color = ColorValue();
        u.on = false;
        return u;
    }

    u.color = ColorValue::fromHsl(static_cast<int>(st.hue * 360.0 / 65535.0),
                                  static_cast<int>(st.sat * 100.0 / 255.0),
                                  static_cast<int>(st.bri * 100.0 / 255.0));
    u.on = st.on;
    return u;
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
    ASSERT_EQ(Decode::Ok, decodeLightState(HUE_LIGHT_ANSWER, st));

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
    ASSERT_EQ(Decode::Ok, decodeLightState(answerWith("200", "100", "30000",
                                                      "true", "true"), straight));
    LightState swapped;
    ASSERT_EQ(Decode::Ok, decodeLightState(answerWith("100", "200", "30000",
                                                      "true", "true"), swapped));

    //the two raw fields really did move
    EXPECT_EQ(200, straight.sat);
    EXPECT_EQ(100, straight.bri);
    EXPECT_EQ(100, swapped.sat);
    EXPECT_EQ(200, swapped.bri);
    EXPECT_NE(straight.sat, straight.bri);

    //and the colour that comes out of them is not the same colour
    const StateUpdate a = toStateUpdate(straight);
    const StateUpdate b = toStateUpdate(swapped);
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
    ASSERT_EQ(Decode::Ok, decodeLightState(HUE_LIGHT_ANSWER, st));
    st.reachable = true; //so the colour branch is taken

    const StateUpdate u = toStateUpdate(st);

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
    EXPECT_TRUE(toStateUpdate(st).color.isValid());
    EXPECT_TRUE(toStateUpdate(st).on);

    st.on = false; st.reachable = true;
    EXPECT_TRUE(toStateUpdate(st).color.isValid());
    EXPECT_FALSE(toStateUpdate(st).on);

    st.on = true; st.reachable = false;
    EXPECT_FALSE(toStateUpdate(st).color.isValid());
    EXPECT_FALSE(toStateUpdate(st).on)
            << "an unreachable light reported itself ON: the branch passes "
               "reachable as the state, not on";
    EXPECT_EQ("#000000", toStateUpdate(st).color.toString());

    st.on = false; st.reachable = false;
    EXPECT_FALSE(toStateUpdate(st).color.isValid());
    EXPECT_FALSE(toStateUpdate(st).on);
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
              decodeLightState("{\"state\":{\"bri\":100,\"on\":true,"
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
                  decodeLightState(answerWith(s.sat, "100", "30000",
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
    ASSERT_EQ(Decode::Ok, decodeLightState(answerWith("200", "100", "30000",
                                                      "1", "\"true\""), st));
    EXPECT_FALSE(st.on)        << "the NUMBER 1 was read as a boolean";
    EXPECT_FALSE(st.reachable) << "the STRING \"true\" was read as a boolean";

    LightState absent;
    ASSERT_EQ(Decode::Ok,
              decodeLightState("{\"state\":{\"sat\":200,\"bri\":100,"
                               "\"hue\":30000}}", absent));
    EXPECT_FALSE(absent.on);
    EXPECT_FALSE(absent.reachable);

    //and a real boolean IS read, both ways round, in the same payload
    LightState ok;
    ASSERT_EQ(Decode::Ok, decodeLightState(answerWith("200", "100", "30000",
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
    ASSERT_EQ(Decode::Ok, decodeLightState(answerWith("-5", "999", "70000",
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
    EXPECT_EQ(Decode::Malformed, decodeLightState("", st));
    EXPECT_EQ(Decode::Malformed, decodeLightState("{oops", st));
    EXPECT_EQ(Decode::Malformed, decodeLightState("{\"state\":{}} trailing", st));
    EXPECT_EQ(Decode::Malformed, decodeLightState("{\"state\":{}", st));
    EXPECT_EQ(Decode::Malformed, decodeLightState("<html>401</html>", st));
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
    EXPECT_EQ(Decode::Malformed, decodeLightState("3", st));
    EXPECT_EQ(Decode::Malformed, decodeLightState("\"ok\"", st));
    EXPECT_EQ(Decode::Malformed, decodeLightState("true", st));
    EXPECT_EQ(Decode::Malformed, decodeLightState("null", st));

    //a top level ARRAY parses and is refused one step later. This is the shape
    //a real bridge answers with when the API key is wrong:
    //[{"error":{"type":1,"address":"/lights/3","description":"unauthorized user"}}]
    EXPECT_EQ(Decode::NotAnObject, decodeLightState("[1,2]", st));
    EXPECT_EQ(Decode::NotAnObject,
              decodeLightState("[{\"error\":{\"type\":1,"
                               "\"description\":\"d-unauthorized user\"}}]", st));
}

//"state" ABSENT, or present but not an object, is a THIRD outcome and not the
//same as a malformed answer: it means the bridge replied something we could
//read but did not recognise.
TEST(HueWire, AMissingOrNonObjectStateIsItsOwnOutcome)
{
    LightState st;
    EXPECT_EQ(Decode::NoState,
              decodeLightState("{\"name\":\"n-Lampe Salon\",\"type\":\"t-x\"}", st));
    EXPECT_EQ(Decode::NoState, decodeLightState("{\"state\":\"s-on\"}", st));
    EXPECT_EQ(Decode::NoState, decodeLightState("{\"state\":null}", st));
    EXPECT_EQ(Decode::NoState, decodeLightState("{\"state\":[1,2]}", st));
    EXPECT_EQ(Decode::NoState, decodeLightState("{}", st));

    //an EMPTY state object is NOT this outcome: it decodes, with all defaults
    LightState empty;
    EXPECT_EQ(Decode::Ok, decodeLightState("{\"state\":{}}", empty));
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

    ASSERT_EQ(Decode::NoState, decodeLightState("{\"state\":\"s-on\"}", st));
    EXPECT_EQ(111, st.sat);
    EXPECT_EQ(222, st.bri);
    EXPECT_EQ(333, st.hue);
    EXPECT_TRUE(st.on);
    EXPECT_TRUE(st.reachable);

    ASSERT_EQ(Decode::Malformed, decodeLightState("{oops", st));
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
    ASSERT_EQ(Decode::Ok, decodeLightState(answer, st)) << escaped(answer);
    EXPECT_EQ(200, st.sat);
    EXPECT_TRUE(st.reachable);

    //raw UTF-8 in the answer is accepted too
    LightState raw;
    ASSERT_EQ(Decode::Ok,
              decodeLightState("{\"name\":\"n-Entr\xc3\xa9""e\",\"state\":{"
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
              decodeLightState("{\"name\":\"n-b\xff""ad\",\"state\":{\"sat\":200}}", st));
    EXPECT_EQ(Decode::Malformed, decodeLightState("{\"name\":\"x\xc3\"}", st));
    EXPECT_EQ(Decode::Malformed, decodeLightState("{\"name\":\"\\ud800\"}", st));
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
//This case pins the jansson column - the contract the migration must keep. The
//migration commit adds the nlohmann column next to it. It exists so that the
//next reader who reaches for the "obvious" one-liner is told, in one red line,
//what it costs.
TEST(HueWire, Tripwire_JsonIntegerValueDefaultsWhereValueWithDefaultWouldThrow)
{
    json_t *root = json_loads("{\"f\":12.5,\"s\":\"77\",\"n\":null,"
                              "\"b\":true,\"i\":42}", 0, NULL);
    ASSERT_TRUE(root != NULL);

    EXPECT_EQ(0, json_integer_value(json_object_get(root, "f")));
    EXPECT_EQ(0, json_integer_value(json_object_get(root, "s")));
    EXPECT_EQ(0, json_integer_value(json_object_get(root, "n")));
    EXPECT_EQ(0, json_integer_value(json_object_get(root, "b")));
    EXPECT_EQ(0, json_integer_value(json_object_get(root, "absent")));

    //and the one well typed key really is read: this is not "everything is 0"
    EXPECT_EQ(42, json_integer_value(json_object_get(root, "i")));

    json_decref(root);
}
