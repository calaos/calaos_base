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
 * E4.1i - CHARACTERIZATION of the Reolink wire.
 *
 * Two directions, both carried by ReolinkCtrl.cpp and by nothing else:
 *   - EMISSION: the {action:"register", hostname, username, password,
 *     event_type} message sent to the calaos_reolink external process
 *     (ReolinkCtrl::doRegisterCamera);
 *   - RECEPTION: every message the external process sends back, flattened
 *     into a Params by jansson_decode_object() (ReolinkCtrl's
 *     messageReceived lambda).
 *
 * The other end of this wire is IO/Reolink/ExternProcReolink_main.py, in this
 * repository, decoding with the stdlib "json" module. NOTHING held either
 * direction before this file: tests/ReolinkRegistry_test.cpp covers the
 * callback bookkeeping (ReolinkEventRegistry.h) and never touches JSON.
 *
 * THE SEAM is the two free functions of the anonymous namespace below. They
 * FORWARD to IO/Reolink/ReolinkWire.h - the production header ReolinkCtrl.cpp
 * itself includes - so that a mutation of the SHIPPED emitter or of the
 * SHIPPED decoder turns this suite RED. A test that reproduces the assembly
 * instead of calling it only freezes what the TEST does (measured on a sibling
 * ticket of this same series), which is why the seam exists and why it is
 * only two lines wide. Before the migration it carried the jansson body of
 * ReolinkCtrl.cpp verbatim; exactly three assertions of this file moved when
 * it was rewired, each flagged in place with MOVED BY E4.1i.
 *
 * WHAT IS ASSERTED ON BYTES, not on the parsed document, and why:
 *
 *   The 145 goldens of the series compare PARSED DOCUMENTS. No golden, and no
 *   other test of this suite, can see an escaping change. Two cases below
 *   therefore look at the raw bytes of the wire:
 *     - TheRegisterWireIsPureAsciiEvenWhenTheParamsAreNot: RED if
 *       ensure_ascii is ever dropped from the dump().
 *     - InvalidUtf8InAParamDoesNotAbortTheEmission: RED (by exception) if
 *       error_handler_t::replace is ever dropped from the dump().
 *   Both are asserted on the byte string. A semantic oracle is blind to
 *   escaping by construction, so it could not do this job.
 *
 * THE NON-ASCII PATH IS REAL, not synthetic: hostname/username/password/
 * event_type are IO parameters read out of io.xml (ReolinkInputSwitch.cpp),
 * and a camera password with an accent in it is an ordinary password. Invalid
 * UTF-8 is one mis-encoded byte away from that (a latin-1 editor on io.xml).
 *
 * A DELIBERATELY RICH FIXTURE. The recurring defect of the E4.0/E4.1 series
 * is the "poor fixture": a dataset too uniform for a swap of two
 * interchangeable fields to show. The four register parameters below carry
 * four disjoint vocabularies and no one of them is a substring of another, so
 * exchanging any two of them - in the fixture or in the emitter - is visible.
 * Same rule in the decoded payloads: every value is unique.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <string>
#include <type_traits>

#include "Utils.h"
#include "Params.h"

/* THE PRODUCTION HEADER. Not a copy of it: the very text ReolinkCtrl.cpp
 * includes and the server ships. */
#include "ReolinkWire.h"

using std::string;

namespace
{

/*---------------------------------------------------------------------------
 * THE SEAM - emission.
 *
 * Was the jansson message assembly of ReolinkCtrl::doRegisterCamera() copied
 * verbatim; since E4.1i it forwards to the SHIPPED emitter, so a mutation of
 * ReolinkWire::buildRegisterMessage() turns this suite red. Three assertions
 * of this file moved with the migration, each flagged where it sits.
 *-------------------------------------------------------------------------*/
string buildRegisterWire(const string &hostname, const string &username,
                         const string &password, const string &event_type)
{
    return ReolinkWire::buildRegisterMessage(hostname, username, password,
                                             event_type);
}

/*---------------------------------------------------------------------------
 * THE SEAM - reception.
 *
 * Was the jansson decoding of the ReolinkCtrl messageReceived lambda copied
 * verbatim; since E4.1i it forwards to the SHIPPED decoder. Answers false
 * exactly where ReolinkCtrl logs its parse error and returns without
 * dispatching - which is where json_loads() used to answer NULL.
 *-------------------------------------------------------------------------*/
bool decodeWire(const string &msg, Params &p)
{
    return ReolinkWire::decodeMessage(msg, p);
}

/*---------------------------------------------------------------------------
 * Fixture and helpers
 *-------------------------------------------------------------------------*/

/* Four disjoint vocabularies, no one a substring of another: a swap of any
 * two of them changes the assertions of RegisterMessageCarriesTheFourParams
 * AND the exact byte string below. */
const char *const FX_HOST  = "h-cam-front-door.lan";
const char *const FX_USER  = "u-operator-account";
const char *const FX_PASS  = "p-Sekr3t-Phrase";
const char *const FX_EVENT = "e-visitor-doorbell";

bool isPureAscii(const string &s)
{
    for (unsigned char c: s)
    {
        if (c >= 0x80)
            return false;
    }
    return true;
}

/* Renders a byte string readable in a gtest failure message: a raw \xc3\xa9
 * pasted into an assertion log is unreadable and, worse, invisible. */
string escaped(const string &s)
{
    string out;
    char buf[8];
    for (unsigned char c: s)
    {
        if (c >= 0x20 && c < 0x7f)
        {
            out += static_cast<char>(c);
        }
        else
        {
            snprintf(buf, sizeof(buf), "\\x%02x", c);
            out += buf;
        }
    }
    return out;
}

/* A real "detection" event of ExternProcReolink_main.py:1292-1303. Every
 * type the python side can put on this wire is present, and no two values are
 * equal, so a decoder that attached a payload to the wrong key shows. */
const char *const PY_DETECTION_EVENT =
        "{"
        "\"event\":\"detection\","
        "\"hostname\":\"h-cam-front-door.lan\","
        "\"event_type\":\"e-visitor-doorbell\","
        "\"channel\":3,"
        "\"timestamp\":\"2026-08-25T04:05:06.700000\","
        "\"camera_name\":\"n-Camera-Porch\","
        "\"tcp_push_active\":true,"
        "\"callback_duration\":0.123456789,"
        "\"async_callback\":false,"
        "\"adaptive_timeout\":45"
        "}";

} //namespace

/*******************************************************************************
 * EMISSION - structure and values
 ******************************************************************************/

//The five keys, their five values, all of them JSON STRINGS. "An int that
//became a JSON number" is the number one trap of this migration and the
//python side reads event_type/hostname straight out of this object.
TEST(ReolinkWire, RegisterMessageCarriesTheFourParamsUnderTheirOwnKeys)
{
    const string wire = buildRegisterWire(FX_HOST, FX_USER, FX_PASS, FX_EVENT);

    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    ASSERT_TRUE(j.is_object());
    ASSERT_EQ(5u, j.size()) << escaped(wire);

    ASSERT_TRUE(j.at("action").is_string());
    ASSERT_TRUE(j.at("hostname").is_string());
    ASSERT_TRUE(j.at("username").is_string());
    ASSERT_TRUE(j.at("password").is_string());
    ASSERT_TRUE(j.at("event_type").is_string());

    EXPECT_EQ("register", j.at("action").get<string>());
    EXPECT_EQ(FX_HOST, j.at("hostname").get<string>());
    EXPECT_EQ(FX_USER, j.at("username").get<string>());
    EXPECT_EQ(FX_PASS, j.at("password").get<string>());
    EXPECT_EQ(FX_EVENT, j.at("event_type").get<string>());
}

//The message the calaos_reolink process actually receives, byte for byte.
//THIS ASSERTION IS EXPECTED TO MOVE EXACTLY ONCE, in the migration commit:
//jansson emits in INSERTION order, nlohmann::json in SORTED order (user
//decision of 2026-08-17, sorted keys assumed, never ordered_json). The python
//side decodes with the stdlib json module and is indifferent to key order;
//pinning the string here is what makes that - and only that - visible in the
//migration diff.
TEST(ReolinkWire, RegisterMessageIsExactlyThisByteString)
{
    const string wire = buildRegisterWire(FX_HOST, FX_USER, FX_PASS, FX_EVENT);

    //MOVED BY E4.1i, and this is the whole of the move: keys used to come out
    //in INSERTION order (action, hostname, username, password, event_type),
    //they now come out SORTED. Nothing else about this string changed.
    EXPECT_EQ("{\"action\":\"register\","
              "\"event_type\":\"e-visitor-doorbell\","
              "\"hostname\":\"h-cam-front-door.lan\","
              "\"password\":\"p-Sekr3t-Phrase\","
              "\"username\":\"u-operator-account\"}",
              wire)
            << "the register wire changed: " << escaped(wire);
}

//An empty parameter is EMITTED as an empty string, not omitted: "absent key"
//and "empty value" are two different answers, and ReolinkInputSwitch refuses
//to register at all when a parameter is empty, so an omission here would be
//an undetectable second failure mode.
TEST(ReolinkWire, EmptyParamsAreEmittedNotOmitted)
{
    const string wire = buildRegisterWire("", "", "", "");

    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    ASSERT_EQ(5u, j.size()) << escaped(wire);
    EXPECT_EQ("", j.at("hostname").get<string>());
    EXPECT_EQ("", j.at("username").get<string>());
    EXPECT_EQ("", j.at("password").get<string>());
    EXPECT_EQ("", j.at("event_type").get<string>());
    EXPECT_EQ("register", j.at("action").get<string>());
}

//A quote, a backslash, a newline and a tab in a password: JSON metacharacters
//must be escaped, not fed raw into the stream. A password is free text.
TEST(ReolinkWire, JsonMetacharactersInAPasswordAreEscaped)
{
    const string wire = buildRegisterWire(FX_HOST, FX_USER,
                                          string("a\"b\\c\nd\te"), FX_EVENT);

    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    EXPECT_EQ(string("a\"b\\c\nd\te"), j.at("password").get<string>());
    EXPECT_NE(string::npos, wire.find("a\\\"b\\\\c\\nd\\te"))
            << escaped(wire);
}

/*******************************************************************************
 * EMISSION - THE BYTE ORACLES
 *
 * These two cases are the only thing in the tree that can see the escaping of
 * this wire. They assert on the raw byte string on purpose.
 ******************************************************************************/

//RED IF ensure_ascii IS DROPPED.
//io.xml is read as bytes; an accented camera password is ordinary. Today
//jansson's JSON_ENSURE_ASCII keeps the wire pure ASCII, and the migration must
//keep it pure ASCII (invariant 3 of the epic). Without the flag nlohmann emits
//the raw UTF-8 bytes and every assertion below fails.
TEST(ReolinkWire, TheRegisterWireIsPureAsciiEvenWhenTheParamsAreNot)
{
    //é U+00E9 in the hostname, ü U+00FC in the user, À U+00C0 in the password:
    //three DIFFERENT codepoints, so an escape attached to the wrong key shows.
    const string wire = buildRegisterWire("h-caf\xc3\xa9.lan",
                                          "u-m\xc3\xbcller",
                                          "p-\xc3\x80lpha",
                                          FX_EVENT);

    EXPECT_TRUE(isPureAscii(wire))
            << "raw UTF-8 bytes reached the reolink wire: " << escaped(wire);

    //and the escapes really are there, at the right key
    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    EXPECT_EQ("h-caf\xc3\xa9.lan", j.at("hostname").get<string>());
    EXPECT_EQ("u-m\xc3\xbcller", j.at("username").get<string>());
    EXPECT_EQ("p-\xc3\x80lpha", j.at("password").get<string>());

    //no raw byte of any of the three sequences survived
    EXPECT_EQ(string::npos, wire.find("\xc3\xa9")) << escaped(wire);
    EXPECT_EQ(string::npos, wire.find("\xc3\xbc")) << escaped(wire);
    EXPECT_EQ(string::npos, wire.find("\xc3\x80")) << escaped(wire);
}

//The ONE measured byte delta of this migration, pinned with no case
//normalisation anywhere: jansson escapes with UPPERCASE hex, nlohmann with
//LOWERCASE hex. Normalising the case here would let a real wire change pass.
//THIS ASSERTION IS EXPECTED TO MOVE EXACTLY ONCE, in the migration commit.
TEST(ReolinkWire, TheHexCaseOfTheEscapesIsTheMeasuredDelta)
{
    const string wire = buildRegisterWire("h-caf\xc3\xa9.lan", FX_USER,
                                          "p-\xc3\x80lpha", FX_EVENT);

    //MOVED BY E4.1i: jansson escaped with UPPERCASE hex, nlohmann with
    //LOWERCASE hex. The wire stays pure ASCII either way (the case above);
    //this is the ONE byte difference the migration produces, and the python
    //end - json.loads() - cannot see it.
    EXPECT_NE(string::npos, wire.find("\\u00e9")) << escaped(wire);
    EXPECT_NE(string::npos, wire.find("\\u00c0")) << escaped(wire);
    EXPECT_EQ(string::npos, wire.find("\\u00E9")) << escaped(wire);
    EXPECT_EQ(string::npos, wire.find("\\u00C0")) << escaped(wire);
}

//RED IF error_handler_t::replace IS DROPPED (by exception).
//Measured, both libraries, on the same byte: jansson's json_string() answers
//NULL on invalid UTF-8, json_object_set_new() answers -1, neither return code
//is tested, and the pair is SILENTLY DROPPED - the process receives a register
//message with no password and the camera never authenticates. nlohmann does
//the opposite and worse: it accepts the bytes into the tree and dump() throws
//type_error.316, which in ReolinkCtrl::doRegisterCamera - a void called from a
//process callback with no try/catch anywhere on the path - is std::terminate.
//error_handler_t::replace is what turns that into U+FFFD.
TEST(ReolinkWire, InvalidUtf8InAParamDoesNotAbortTheEmission)
{
    string wire;
    ASSERT_NO_THROW(wire = buildRegisterWire(FX_HOST, FX_USER,
                                             string("p-\xff-tail"), FX_EVENT))
            << "the emitter threw on invalid UTF-8";

    //whatever it did, it still produced parseable, pure-ASCII JSON
    EXPECT_TRUE(isPureAscii(wire)) << escaped(wire);
    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);

    //MOVED BY E4.1i. Under jansson the password pair was DROPPED ON THE FLOOR
    //(4 keys, no "password") and nothing in the tree noticed. It is now
    //present, with the bad byte replaced by U+FFFD - which the ensure_ascii
    //dump writes as the literal escape \ufffd. Either way the camera fails to
    //authenticate; what changes is that the wire no longer lies about which
    //fields were sent.
    EXPECT_EQ(5u, j.size()) << escaped(wire);
    ASSERT_TRUE(j.contains("password")) << escaped(wire);
    EXPECT_EQ("p-\xef\xbf\xbd-tail", j.at("password").get<string>());
    EXPECT_NE(string::npos, wire.find("\\ufffd")) << escaped(wire);
    EXPECT_EQ(FX_HOST, j.at("hostname").get<string>());
    EXPECT_EQ(FX_USER, j.at("username").get<string>());
    EXPECT_EQ(FX_EVENT, j.at("event_type").get<string>());
}

/*******************************************************************************
 * RECEPTION - the decoding contract
 *
 * jansson_decode_object() flattens EVERYTHING into strings: string as is,
 * boolean as "true"/"false", any number through Utils::to_string(double), and
 * ANY OTHER TYPE (object, array, null) into the EMPTY STRING with the key
 * still added. Params::fromNJson() does none of that - see the tripwire at
 * the end of this file - so the migration keeps this contract by hand.
 ******************************************************************************/

//The message ReolinkCtrl dispatches on: event + hostname + event_type.
TEST(ReolinkWire, DecodesAWellFormedCameraEvent)
{
    Params p;
    ASSERT_TRUE(decodeWire(PY_DETECTION_EVENT, p));

    //the three keys ReolinkCtrl tests with Exists() before dispatching
    ASSERT_TRUE(p.Exists("event"));
    ASSERT_TRUE(p.Exists("hostname"));
    ASSERT_TRUE(p.Exists("event_type"));

    EXPECT_EQ("detection", p["event"]);
    EXPECT_EQ("h-cam-front-door.lan", p["hostname"]);
    EXPECT_EQ("e-visitor-doorbell", p["event_type"]);
    EXPECT_EQ("n-Camera-Porch", p["camera_name"]);
    EXPECT_EQ("2026-08-25T04:05:06.700000", p["timestamp"]);

    //every key of the payload made it, none was invented
    EXPECT_EQ(10, p.size());
}

//The status handshake (ExternProcReolink_main.py:800-804 and :1498-1509).
TEST(ReolinkWire, DecodesTheStatusHandshakeAndTheErrorReport)
{
    Params ok;
    ASSERT_TRUE(decodeWire("{\"status\":\"connected\","
                           "\"message\":\"m-Reolink client ready\"}", ok));
    EXPECT_EQ("connected", ok["status"]);
    EXPECT_EQ("m-Reolink client ready", ok["message"]);

    Params ko;
    ASSERT_TRUE(decodeWire("{\"status\":\"error\","
                           "\"message\":\"m-Timeout connecting to camera\"}", ko));
    EXPECT_EQ("error", ko["status"]);
    EXPECT_EQ("m-Timeout connecting to camera", ko["message"]);
}

//NUMBERS. Every one of these is a real field of the python driver, and every
//one of these strings is MEASURED, not assumed. Utils::to_string(double) is a
//bare ostringstream with six significant digits: it TRUNCATES and it flips to
//scientific notation. That is frozen here on purpose - E4.1 must not "fix" it.
TEST(ReolinkWire, NumbersAreFlattenedThroughUtilsToString)
{
    Params p;
    ASSERT_TRUE(decodeWire("{"
                           "\"channel\":3,"                       //int
                           "\"failure_count\":0,"                 //int zero
                           "\"callback_duration\":0.123456789,"   //truncated
                           "\"next_retry_in\":12.5,"              //exact
                           "\"ratio\":1234.56789,"                //truncated
                           "\"uptime\":123456789.0,"              //scientific
                           "\"drift\":-40.5"                      //negative
                           "}", p));

    EXPECT_EQ("3", p["channel"]);
    EXPECT_EQ("0", p["failure_count"]);
    EXPECT_EQ("0.123457", p["callback_duration"]);
    EXPECT_EQ("12.5", p["next_retry_in"]);
    EXPECT_EQ("1234.57", p["ratio"]);
    EXPECT_EQ("1.23457e+08", p["uptime"]);
    EXPECT_EQ("-40.5", p["drift"]);

    //and they are STRINGS in the Params, which is a map<string,string>: the
    //type-strict oracle of the golden suite says 3 != "3".
    EXPECT_EQ(7, p.size());
}

//BOOLEANS become the words "true"/"false", not "1"/"0". Both booleans of the
//real event payload are present with OPPOSITE values, so a decoder that
//inverted them, or that attached one to the other's key, shows.
TEST(ReolinkWire, BooleansAreFlattenedToTheWordsTrueAndFalse)
{
    Params p;
    ASSERT_TRUE(decodeWire("{\"tcp_push_active\":true,"
                           "\"async_callback\":false,"
                           "\"doorbell_optimized\":true,"
                           "\"fallback\":false}", p));

    EXPECT_EQ("true", p["tcp_push_active"]);
    EXPECT_EQ("false", p["async_callback"]);
    EXPECT_EQ("true", p["doorbell_optimized"]);
    EXPECT_EQ("false", p["fallback"]);
}

//NESTED OBJECTS, ARRAYS AND null collapse to the EMPTY STRING, and the key is
//STILL ADDED. This is not a synthetic case: the health_check answer of
//ExternProcReolink_main.py:1653-1674 carries TWO nested objects
//("circuit_breakers", "memory_optimization"). "key present, value empty" and
//"key absent" are different answers to Params::Exists(), which is what
//ReolinkCtrl branches on.
TEST(ReolinkWire, NestedObjectsArraysAndNullBecomeTheEmptyStringWithTheKeyKept)
{
    Params p;
    ASSERT_TRUE(decodeWire("{"
                           "\"status\":\"healthy\","
                           "\"circuit_breakers\":{\"h-cam\":{\"state\":\"open\"}},"
                           "\"memory_optimization\":{\"thread_safe_timers\":true},"
                           "\"channels\":[1,2,3],"
                           "\"last_error\":null"
                           "}", p));

    EXPECT_EQ(5, p.size());
    EXPECT_EQ("healthy", p["status"]);

    EXPECT_TRUE(p.Exists("circuit_breakers"));
    EXPECT_EQ("", p["circuit_breakers"]);
    EXPECT_TRUE(p.Exists("memory_optimization"));
    EXPECT_EQ("", p["memory_optimization"]);
    EXPECT_TRUE(p.Exists("channels"));
    EXPECT_EQ("", p["channels"]);
    EXPECT_TRUE(p.Exists("last_error"));
    EXPECT_EQ("", p["last_error"]);
}

//A non-ASCII camera name comes back through the wire intact. The python side
//dumps with json.dumps() default ensure_ascii=True, so it arrives escaped; the
//decoded Params must hold the raw UTF-8 bytes.
TEST(ReolinkWire, EscapedNonAsciiComesBackAsRawUtf8Bytes)
{
    Params p;
    ASSERT_TRUE(decodeWire("{\"event\":\"detection\","
                           "\"camera_name\":\"n-Entr\\u00e9e\"}", p));
    EXPECT_EQ("n-Entr\xc3\xa9""e", p["camera_name"]);
}

//MALFORMED payloads are REFUSED, and the caller (ReolinkCtrl) returns without
//dispatching. Never a half-filled Params.
TEST(ReolinkWire, MalformedPayloadsAreRefused)
{
    Params a;
    EXPECT_FALSE(decodeWire("{oops", a));
    EXPECT_EQ(0, a.size());

    Params b;
    EXPECT_FALSE(decodeWire("", b));
    EXPECT_EQ(0, b.size());

    Params c;
    EXPECT_FALSE(decodeWire("{\"event\":\"detection\"} trailing", c));
    EXPECT_EQ(0, c.size());

    Params d;
    EXPECT_FALSE(decodeWire("{\"event\":\"detection\"", d));
    EXPECT_EQ(0, d.size());
}

//THE ACCEPTANCE SET, measured on jansson with no decode flag: a top-level
//SCALAR is refused (JSON_DECODE_ANY is off), a top-level ARRAY is accepted and
//decodes to NO parameter at all. Neither can come from our own python driver,
//which always dumps a dict - but the acceptance set is what tells a later
//reader whether a payload was rejected or silently emptied.
TEST(ReolinkWire, TopLevelScalarsAreRefusedAndArraysDecodeToNoParams)
{
    Params n;
    EXPECT_FALSE(decodeWire("3", n));
    Params s;
    EXPECT_FALSE(decodeWire("\"detection\"", s));
    Params b;
    EXPECT_FALSE(decodeWire("true", b));
    Params z;
    EXPECT_FALSE(decodeWire("null", z));

    Params arr;
    EXPECT_TRUE(decodeWire("[1,2]", arr));
    EXPECT_EQ(0, arr.size());
}

//Duplicate keys: the LAST one wins, in both libraries. Measured.
TEST(ReolinkWire, DuplicateKeysKeepTheLastOccurrence)
{
    Params p;
    ASSERT_TRUE(decodeWire("{\"event_type\":\"e-first\","
                           "\"event_type\":\"e-second\"}", p));
    EXPECT_EQ("e-second", p["event_type"]);
    EXPECT_EQ(1, p.size());
}

/*******************************************************************************
 * TRIPWIRE
 ******************************************************************************/

//WHY THE DECODER IS WRITTEN BY HAND AND NOT AS Params::fromNJson().
//
//Params::fromNJson() assigns each json value straight into a std::string,
//which for anything that is not a JSON string throws type_error.302. Every
//real detection event of ExternProcReolink_main.py carries "channel" (int),
//"tcp_push_active" (bool) and "callback_duration" (float); the health answer
//carries two nested objects. Substituting fromNJson() for the flattening
//decoder would therefore throw on EVERY camera event, inside an ExternProc
//callback with no try/catch on the path - std::terminate, driver dead.
//
//This case exists so that the next reader who reaches for the "obvious"
//simplification is told, in one red line, what it costs.
TEST(ReolinkWire, Tripwire_ParamsFromNJsonIsNotASubstituteForThisDecoder)
{
    const Json j = Json::parse(PY_DETECTION_EVENT, nullptr, false);
    ASSERT_FALSE(j.is_discarded());
    ASSERT_TRUE(j.contains("channel"));

    EXPECT_THROW(Params::fromNJson(j), Json::exception)
            << "Params::fromNJson() stopped throwing on a non-string value: "
               "re-measure its contract against jansson_decode_object() before "
               "substituting it here";

    //an all-string payload is the ONE case where the two agree
    const Json allStrings = Json::parse("{\"event\":\"detection\","
                                        "\"hostname\":\"h-cam\"}", nullptr, false);
    Params p = Params::fromNJson(allStrings);
    EXPECT_EQ("detection", p["event"]);
    EXPECT_EQ("h-cam", p["hostname"]);
}

/*----------------------------------------------------------------------------
 * T3.31 - THE POSITIONAL-ARGUMENT HOLE ON THE REGISTER MESSAGE.
 *
 * buildRegisterMessage() takes FOUR std::string in a row. Every case above
 * calls it in the right order, and none of them can catch a caller that does
 * not - ReolinkCtrl.cpp is not linked into this binary and cannot be (the
 * singleton spawns calaos_reolink from its constructor). E4.1i measured it:
 * swapping `username` and `password` at ReolinkCtrl.cpp left this suite 17/17
 * GREEN, while the same swap made INSIDE this header reddens five cases.
 *
 * The consequence of that permutation is the worst of the whole series: the
 * message is perfectly well formed, the camera silently fails to
 * authenticate, and THE PASSWORD IS SENT IN CLEAR IN THE USERNAME FIELD.
 *
 * ⚠️ Compilation property reported through an executable oracle, exactly as
 * in tests/WagoWire_test.cpp - it says a permuted call no longer type-checks,
 * it says nothing about what is emitted. What is emitted stays pinned by
 * RegisterMessageCarriesTheFourParamsUnderTheirOwnKeys and its neighbours,
 * whose four fixture values are mutually non-substitutable on purpose.
 *--------------------------------------------------------------------------*/

TEST(ReolinkWire, TheRegisterMessageRefusesFourBareStrings)
{
    using RegisterFn = decltype(&ReolinkWire::buildRegisterMessage);

    EXPECT_FALSE((std::is_invocable_v<RegisterFn, const string &, const string &,
                                      const string &, const string &>))
        << "buildRegisterMessage() still takes four bare std::string: any two "
           "of hostname/username/password/event_type are interchangeable at "
           "the call site with no diagnostic at all";

    //The same question asked of raw literals, which is how a hurried caller
    //writes it. const char* converts to std::string, so this must fall with
    //the previous one and not separately.
    EXPECT_FALSE((std::is_invocable_v<RegisterFn, const char *, const char *,
                                      const char *, const char *>))
        << "buildRegisterMessage() still accepts four bare string literals";
}
