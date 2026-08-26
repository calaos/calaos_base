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
 * E4.1n - THE BYTES THE STATE CHAIN PUTS ON THE WIRE.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS FILE EXISTS
 * ---------------------------------------------------------------------------
 * E4.1n migrates the four state builders of JsonApi to nlohmann:
 *
 *      buildJsonState()   -> get_state   (HTTP, WS, and the RemoteUI screens)
 *      buildJsonStates()  -> get_states
 *      buildQuery()       -> query
 *      decodeSetState()   -> set_state   (its two answer builders)
 *
 * The regime of proof of the epic, restated because it is the whole reason for
 * this file: the 145 goldens compare PARSED DOCUMENTS. They are, by
 * construction, blind to key order, to the case of a \uXXXX escape and to
 * whether a byte was escaped at all. Measured by the E4.1k review: flipping
 * ensure_ascii left `make check` GREEN. So the byte dimension of this ticket is
 * covered by NOTHING until this file exists.
 *
 * Every assertion below reads RAW RESPONSE BYTES - HttpTestRequest::body() and
 * WsTestSession::lastMessage(), never bodyJson() / lastEnvelope().
 *
 * ---------------------------------------------------------------------------
 * THE FIVE DELTAS, MEASURED ON THIS PERIMETER (probe, 2026-08-26)
 * ---------------------------------------------------------------------------
 * A standalone g++ probe was run against the tree's own jansson and json.hpp
 * on the exact shapes this chain emits. Verbatim results:
 *
 *   1. KEY ORDER      jansson {"zulu":"1","alpha":"2"}
 *                     nlohmann {"alpha":"2","zulu":"1"}
 *   2. HEX CASE       jansson writes \\u00E9 (UPPERCASE), nlohmann \\u00e9 (lowercase)
 *   3. INVALID UTF-8  jansson json_string() answers NULL, json_object_set_new()
 *                     answers -1, THE WHOLE PAIR IS DROPPED: {} .
 *                     nlohmann keeps it: {"k":"a\\ufffd\\ufffdz"} - ONE U+FFFD
 *                     PER INVALID BYTE. This one is a STRUCTURE delta, not
 *                     only a byte delta: a key that was absent becomes present.
 *   4. DEL (0x7F)     jansson writes the raw byte (JSON_ENSURE_ASCII only acts
 *                     on non-ASCII): {"k":"a<7F> z"}, 12 bytes.
 *                     nlohmann escapes every codepoint >= 0x7F under
 *                     ensure_ascii: {"k":"a\\u007f z"}, 17 bytes. +5 bytes, and
 *                     Content-Length moves with it.
 *   5. EMBEDDED NUL   jansson takes a const char*: {"k":"a"} - the value is
 *                     TRUNCATED IN SILENCE. nlohmann takes a std::string:
 *                     {"k":"a\\u0000z"}. Same on the KEY side.
 *
 * The three families that move for a reason OTHER than ensure_ascii are (1),
 * (3) and numbers. NUMBERS DO NOT BITE HERE, and that is measured, not assumed:
 * JsonApi.cpp holds 32 json_string() and ZERO json_real()/json_integer(); every
 * value of this chain, including the int and the double ones, goes through
 * Utils::to_string() and leaves as a JSON STRING. An int that became a JSON
 * number would break the type-strict oracle (3 != "3").
 *
 * A SIXTH delta exists and it is one this ticket must not create: nlohmann's
 * DEFAULT-CONSTRUCTED Json is `null`, not `{}`. `Json jret;` where the code
 * meant `Json::object()` turns every empty answer of this chain from {} into
 * null - it compiles without a warning and no golden sees it. The three
 * ...AnswersAnEmptyObject cases below are the guard, and they are INVARIANTS:
 * they must read the same before and after the migration.
 *
 * ---------------------------------------------------------------------------
 * DELTA CASES vs INVARIANT CASES - READ BEFORE EDITING
 * ---------------------------------------------------------------------------
 * Cases named ...Today pin the JANSSON bytes and are EXPECTED to be flipped by
 * the migration commit, which rewrites the assertion and drops the suffix. They
 * are the characterization half: they are green on an untouched tree and red
 * the moment src/ moves, which is how this file proves the path is EXERCISED
 * rather than merely compiled.
 *
 * Cases with no suffix are INVARIANTS: they must stay green on both sides. If
 * one of them moves, a VALUE or a STRUCTURE changed - stop and understand why.
 *
 * ---------------------------------------------------------------------------
 * FIXTURE, AND THE "POOR FIXTURE" TRAP (12 recorded relapses in this series)
 * ---------------------------------------------------------------------------
 * A symmetric payload, or one with a single key, distinguishes NEITHER an order
 * NOR an encoding. This house is therefore deliberately asymmetric:
 *   - two IOs whose ALPHABETICAL order (alpha, zulu) is the REVERSE of the
 *     order the request asks for (zulu, alpha), so key order is observable,
 *   - a string IO whose value carries the poison of the case, set through the
 *     protected member rather than set_value(): Internal::set_value() calls
 *     Save(), which would write the poisoned bytes into Config's PROCESS-WIDE
 *     state cache and leak them into the next case of this binary.
 *
 * Ids are prefixed e41n_. Taken so far: e40_, e40b_ .. e40f_, e41b_, t317a_ ..
 * t317f_, t319_.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "IO/IntValue.h"
#include "ListeRoom.h"

#include <map>
#include <string>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/*******************************************************************************
 * The byte shapes, spelled in escapes so no editor and no locale can rewrite
 * them silently.
 ******************************************************************************/
const char *const RAW_E_ACUTE   = "\xc3\xa9";       //UTF-8 of U+00E9
const char *const ASCII_E_UPPER = "\\u00E9";        //what jansson writes
const char *const ASCII_E_LOWER = "\\u00e9";        //what nlohmann writes

//0xFF is not a legal UTF-8 lead byte in any position and 0x80 is a
//continuation byte with nothing to continue: this cannot become valid text by
//accident.
const char *const INVALID_UTF8  = "\xff\x80";
const char *const ASCII_FFFD    = "\\ufffd";

const char DEL_BYTE             = '\x7f';
const char *const ASCII_DEL     = "\\u007f";
const char *const ASCII_NUL     = "\\u0000";

//Ids. ALPHABETICAL order is (alpha, zulu); every request below asks for them in
//the REVERSE order, which is what makes the key order observable at all.
const char *const IO_ZULU  = "e41n_zulu";       //InternalString, carries the poison
const char *const IO_ALPHA = "e41n_alpha";      //InternalBool
const char *const IO_PROBE = "e41n_probe";      //get_states / query source

bool contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

size_t occurrences(const std::string &haystack, const std::string &needle)
{
    size_t n = 0;
    for (size_t p = haystack.find(needle); p != std::string::npos;
         p = haystack.find(needle, p + needle.size()))
        n++;
    return n;
}

//Position of a key on the wire, so a case can assert an ORDER instead of a
//presence. npos when absent, which makes the comparison fail loudly.
size_t keyPos(const std::string &wire, const std::string &key)
{
    return wire.find("\"" + key + "\":");
}

/*******************************************************************************
 * An Internal whose value can be set to ARBITRARY BYTES without going anywhere
 * near Save(), and whose get_all_values_string()/query_param() answer a map the
 * case controls.
 *
 * WHY A SUBCLASS AT ALL, AND IT IS MEASURED, NOT DEFENSIVE: get_all_values_*()
 * and query_param() have exactly ONE override each in the whole tree
 * (IOAVReceiver, Audio/AVReceiver.cpp:307). Every other IO answers an EMPTY
 * map, so `get_states` and `query` answer {} for every IO that a test house can
 * possibly build - and a fixture that only ever sees {} distinguishes nothing
 * at all. This class is what gives those two actions a non-empty, asymmetric,
 * poisonable payload.
 ******************************************************************************/
class ProbeIO: public Internal
{
public:
    explicit ProbeIO(Params &p): Internal(p) {}

    //Bypasses set_value()/Save(): Config's IO state cache is process wide and
    //never cleared, so a poisoned value written through the normal path would
    //survive into the next case of this binary.
    void setRawString(const std::string &v) { svalue = v; }

    std::map<std::string, std::string> allValues;
    std::map<std::string, std::string> queryValues;

    std::map<std::string, std::string> get_all_values_string() override
    { return allValues; }

    std::map<std::string, std::string> query_param(std::string) override
    { return queryValues; }
};

} //namespace

class JsonApiStateWireBytesTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();

        forgetIOState(IO_ZULU);
        forgetIOState(IO_ALPHA);
        forgetIOState(IO_PROBE);

        loadConfig();   //minimal house: one room, ID_BOOL_IN/OUT, ID_INT, ID_STRING

        zulu  = addProbe(IO_ZULU, "InternalString", "Zulu");
        probe = addProbe(IO_PROBE, "InternalString", "Probe");
        alpha = addProbe(IO_ALPHA, "InternalBool", "Alpha");
    }

    //Register a ProbeIO exactly like a config-loaded IO: owned by the room
    //(clearCoreState() deletes it) and reachable through ListeRoom::get_io().
    ProbeIO *addProbe(const std::string &id, const std::string &type,
                      const std::string &name)
    {
        Params p;
        p.Add("id", id);
        p.Add("name", name);
        p.Add("type", type);

        ProbeIO *o = new ProbeIO(p);
        firstRoom()->AddIO(o);
        ListeRoom::Instance().addIOHash(o);
        return o;
    }

    //get_state over HTTP, asking for zulu THEN alpha - the reverse of their
    //alphabetical order.
    static Json getStateRequest()
    {
        return authenticated(Json{{ "action", "get_state" },
                                  { "items", Json::array({ IO_ZULU, IO_ALPHA }) }});
    }

    static Json wsGetStateRequest()
    {
        return Json{{ "msg", "get_state" },
                    { "msg_id", "e41n-state" },
                    { "data", Json{{ "items", Json::array({ IO_ZULU, IO_ALPHA }) }} }};
    }

    ProbeIO *zulu = nullptr;
    ProbeIO *alpha = nullptr;
    ProbeIO *probe = nullptr;
};

/*******************************************************************************
 * 1. get_state - KEY ORDER (delta 1).
 *
 * The request asks for zulu then alpha. jansson keeps the INSERTION order of
 * buildJsonState()'s loop over iolist; nlohmann::json is a std::map and SORTS.
 * The fixture is asymmetric on purpose: with two ids already in alphabetical
 * order this case would be green on both sides and would measure nothing.
 ******************************************************************************/
TEST_F(JsonApiStateWireBytesTest, GetStateHttpKeysAreInRequestOrderToday)
{
    HttpTestRequest req;
    req.send(getStateRequest());

    ASSERT_EQ(1u, req.count()) << "get_state did not answer";
    const std::string wire = req.body();

    ASSERT_NE(std::string::npos, keyPos(wire, IO_ZULU)) << wire;
    ASSERT_NE(std::string::npos, keyPos(wire, IO_ALPHA)) << wire;

    EXPECT_LT(keyPos(wire, IO_ZULU), keyPos(wire, IO_ALPHA))
            << "jansson emits the keys in the order buildJsonState() inserted "
               "them, i.e. the order of the request: " << wire;
}

TEST_F(JsonApiStateWireBytesTest, GetStateWsKeysAreInRequestOrderToday)
{
    WsTestSession ws;
    ws.send(wsGetStateRequest());

    ASSERT_EQ(1u, ws.count()) << "get_state did not answer on the websocket";
    const std::string wire = ws.lastMessage();

    ASSERT_NE(std::string::npos, keyPos(wire, IO_ZULU)) << wire;
    ASSERT_NE(std::string::npos, keyPos(wire, IO_ALPHA)) << wire;

    EXPECT_LT(keyPos(wire, IO_ZULU), keyPos(wire, IO_ALPHA)) << wire;
}

/*******************************************************************************
 * 1bis. get_state, WS - THE ENVELOPE ORDER (delta 1, one level up).
 *
 * JsonApiHandlerWS::sendJson(json_t *) builds msg, then msg_id, then data, and
 * jansson keeps that order. The nlohmann overload (:90) builds the same three
 * members into a std::map, which sorts them to data, msg, msg_id. Migrating the
 * payload moves the call to the other overload, so THE ENVELOPE MOVES TOO - a
 * separate case because it is a separate emitter and a separate delta.
 ******************************************************************************/
TEST_F(JsonApiStateWireBytesTest, GetStateWsEnvelopePutsMsgBeforeDataToday)
{
    WsTestSession ws;
    ws.send(wsGetStateRequest());

    ASSERT_EQ(1u, ws.count());
    const std::string wire = ws.lastMessage();

    ASSERT_NE(std::string::npos, keyPos(wire, "msg")) << wire;
    ASSERT_NE(std::string::npos, keyPos(wire, "data")) << wire;

    EXPECT_LT(keyPos(wire, "msg"), keyPos(wire, "data"))
            << "the jansson envelope is msg, msg_id, data: " << wire;
    EXPECT_LT(keyPos(wire, "msg_id"), keyPos(wire, "data")) << wire;
}

/*******************************************************************************
 * 2. get_state - HEX CASE (delta 2).
 *
 * The probe is a VALID character on purpose: no error handler ever looks at it,
 * so this case is sensitive to ensure_ascii and to the emitter's hexadecimal
 * case ONLY. Nothing here is case folded - E4.1a found exactly that defect in
 * the ParamsJson tripwire, which lowercased the wire before matching and would
 * have stayed green through the whole migration.
 ******************************************************************************/
TEST_F(JsonApiStateWireBytesTest, GetStateHttpEscapesAccentWithUppercaseHexToday)
{
    zulu->setRawString(std::string("caf") + RAW_E_ACUTE);

    HttpTestRequest req;
    req.send(getStateRequest());

    ASSERT_EQ(1u, req.count());
    const std::string wire = req.body();

    EXPECT_TRUE(contains(wire, ASCII_E_UPPER))
            << "jansson escapes U+00E9 with UPPERCASE hex: " << wire;
    EXPECT_FALSE(contains(wire, ASCII_E_LOWER)) << wire;
    EXPECT_FALSE(contains(wire, RAW_E_ACUTE))
            << "the jansson wire is ASCII only (JSON_ENSURE_ASCII): " << wire;

    for (unsigned char c : wire)
        ASSERT_LT(c, 0x80u) << "the jansson get_state wire must be ASCII only";
}

TEST_F(JsonApiStateWireBytesTest, GetStateWsEscapesAccentWithUppercaseHexToday)
{
    zulu->setRawString(std::string("caf") + RAW_E_ACUTE);

    WsTestSession ws;
    ws.send(wsGetStateRequest());

    ASSERT_EQ(1u, ws.count());
    const std::string wire = ws.lastMessage();

    EXPECT_TRUE(contains(wire, ASCII_E_UPPER)) << wire;
    EXPECT_FALSE(contains(wire, ASCII_E_LOWER)) << wire;
    EXPECT_FALSE(contains(wire, RAW_E_ACUTE)) << wire;
}

/*******************************************************************************
 * 3. get_state - INVALID UTF-8 (delta 3). THE STRUCTURE ONE.
 *
 * This is not a change of escaping, it is a change of DOCUMENT: today the state
 * of that IO is MISSING from the answer entirely, because json_string() answers
 * NULL on invalid UTF-8 and json_object_set_new() answers -1 without adding
 * anything, and neither return code is tested. A client asking for two IOs gets
 * one.
 *
 * The path in is not hypothetical and needs no privilege beyond an API account:
 * GET /api/?action=set_state&id=<a string IO>&value=%ff%80 reaches
 * IOBase::set_value(std::string) with percent-decoded BYTES - the GET parameter
 * fallback (JsonApiHandlerHttp.cpp:88) puts no JSON parser on that path, and a
 * JSON parser is the only thing in the tree that refuses invalid UTF-8.
 ******************************************************************************/
TEST_F(JsonApiStateWireBytesTest, GetStateHttpDropsThePairOnInvalidUtf8Today)
{
    zulu->setRawString(std::string("a") + INVALID_UTF8 + "z");

    HttpTestRequest req;
    req.send(getStateRequest());

    ASSERT_EQ(1u, req.count());
    const std::string wire = req.body();

    EXPECT_EQ(std::string::npos, keyPos(wire, IO_ZULU))
            << "jansson DROPS the whole pair on invalid UTF-8: " << wire;
    EXPECT_NE(std::string::npos, keyPos(wire, IO_ALPHA))
            << "the other IO must still be answered: " << wire;
    EXPECT_FALSE(contains(wire, ASCII_FFFD)) << wire;
}

TEST_F(JsonApiStateWireBytesTest, GetStateWsDropsThePairOnInvalidUtf8Today)
{
    zulu->setRawString(std::string("a") + INVALID_UTF8 + "z");

    WsTestSession ws;
    ws.send(wsGetStateRequest());

    ASSERT_EQ(1u, ws.count());
    const std::string wire = ws.lastMessage();

    EXPECT_EQ(std::string::npos, keyPos(wire, IO_ZULU)) << wire;
    EXPECT_NE(std::string::npos, keyPos(wire, IO_ALPHA)) << wire;
    EXPECT_FALSE(contains(wire, ASCII_FFFD)) << wire;
}

/*******************************************************************************
 * 4. get_state - DEL, 0x7F (delta 4). THE ONE THAT MOVES Content-Length.
 *
 * JSON_ENSURE_ASCII only acts on NON-ASCII, and DEL is ASCII: jansson puts the
 * raw control byte on the wire. nlohmann under ensure_ascii escapes every
 * codepoint >= 0x7F. Five bytes per occurrence, and the HTTP Content-Length
 * header moves with the body - which is why this case reads the header too.
 ******************************************************************************/
TEST_F(JsonApiStateWireBytesTest, GetStateHttpWritesDelRawAndSizesTheBodyToday)
{
    zulu->setRawString(std::string("a") + DEL_BYTE + "z");

    HttpTestRequest req;
    req.send(getStateRequest());

    ASSERT_EQ(1u, req.count());
    const std::string wire = req.body();

    EXPECT_EQ(1u, occurrences(wire, std::string(1, DEL_BYTE)))
            << "jansson writes DEL as the raw byte";
    EXPECT_FALSE(contains(wire, ASCII_DEL)) << wire;

    //Content-Length is computed from these exact bytes
    //(JsonApiHandlerHttp::sendJson). Escaping DEL adds five bytes to both.
    EXPECT_EQ(std::to_string(wire.size()), req.header("Content-Length"))
            << "Content-Length must describe the body it ships";
}

/*******************************************************************************
 * 5. get_state - EMBEDDED NUL (delta 5). THE SILENT TRUNCATION.
 *
 * jansson's json_string() takes a const char*, so the value STOPS at the first
 * NUL and the tail is lost without a trace. nlohmann takes the std::string and
 * writes \\u0000. Same mechanism on the key side, not exercised here: an id
 * carrying a NUL cannot reach buildJsonState(), the request parser already
 * truncated it at json_string_value() -> std::string.
 ******************************************************************************/
TEST_F(JsonApiStateWireBytesTest, GetStateHttpTruncatesTheValueAtAnEmbeddedNulToday)
{
    zulu->setRawString(std::string("a\0z", 3));

    HttpTestRequest req;
    req.send(getStateRequest());

    ASSERT_EQ(1u, req.count());
    const std::string wire = req.body();

    ASSERT_NE(std::string::npos, keyPos(wire, IO_ZULU)) << wire;
    EXPECT_TRUE(contains(wire, std::string("\"") + IO_ZULU + "\":\"a\""))
            << "jansson truncates the value at the NUL: " << wire;
    EXPECT_FALSE(contains(wire, ASCII_NUL)) << wire;
}

/*******************************************************************************
 * 6. get_state - THE EMPTY ANSWER. INVARIANT, AND THE SIXTH-DELTA GUARD.
 *
 * An unknown id is SKIPPED, never answered as null (the contract of
 * buildJsonState()'s `if (!io) continue;`). The answer is therefore the EMPTY
 * OBJECT. `Json jret;` instead of `Json::object()` would make it `null` - it
 * compiles silently and no golden sees the difference. This case is the guard,
 * and it must read the same on both sides of the migration.
 ******************************************************************************/
TEST_F(JsonApiStateWireBytesTest, GetStateAnswersAnEmptyObjectForAnUnknownId)
{
    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "get_state" },
                                { "items", Json::array({ "e41n_no_such_io" }) }}));

    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("{}", req.body())
            << "an unknown id is skipped and the answer is the empty OBJECT, "
               "never null";
}

/*******************************************************************************
 * 7. get_states - HEX CASE and INVALID UTF-8 (deltas 2 and 3).
 *
 * buildJsonStates() goes through jansson_from_params() (Jansson_Addition.h),
 * whose own header documents the silent drop this case pins.
 ******************************************************************************/
TEST_F(JsonApiStateWireBytesTest, GetStatesEscapesAccentWithUppercaseHexToday)
{
    probe->allValues["zulu"]  = std::string("caf") + RAW_E_ACUTE;
    probe->allValues["alpha"] = "plain";

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "get_states" }, { "id", IO_PROBE }}));

    ASSERT_EQ(1u, req.count());
    const std::string wire = req.body();

    EXPECT_TRUE(contains(wire, ASCII_E_UPPER)) << wire;
    EXPECT_FALSE(contains(wire, ASCII_E_LOWER)) << wire;
    EXPECT_FALSE(contains(wire, RAW_E_ACUTE)) << wire;
}

TEST_F(JsonApiStateWireBytesTest, GetStatesDropsThePairOnInvalidUtf8Today)
{
    probe->allValues["zulu"]  = std::string("a") + INVALID_UTF8 + "z";
    probe->allValues["alpha"] = "plain";

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "get_states" }, { "id", IO_PROBE }}));

    ASSERT_EQ(1u, req.count());
    const std::string wire = req.body();

    EXPECT_EQ(std::string::npos, keyPos(wire, "zulu"))
            << "jansson_from_params() drops the pair in silence: " << wire;
    EXPECT_NE(std::string::npos, keyPos(wire, "alpha")) << wire;
    EXPECT_FALSE(contains(wire, ASCII_FFFD)) << wire;
}

/*******************************************************************************
 * 7bis. get_states - KEY ORDER. INVARIANT, AND IT IS A NEGATIVE RESULT.
 *
 * Params is a std::map, so jansson_from_params() ALREADY walks it
 * alphabetically. Unlike get_state, this family does NOT move on key order, and
 * that is worth a case rather than a sentence: it is the difference between the
 * two halves of this ticket.
 ******************************************************************************/
TEST_F(JsonApiStateWireBytesTest, GetStatesKeysAreAlphabeticalOnBothSides)
{
    probe->allValues["zulu"]  = "1";
    probe->allValues["alpha"] = "2";

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "get_states" }, { "id", IO_PROBE }}));

    ASSERT_EQ(1u, req.count());
    const std::string wire = req.body();

    ASSERT_NE(std::string::npos, keyPos(wire, "alpha")) << wire;
    ASSERT_NE(std::string::npos, keyPos(wire, "zulu")) << wire;
    EXPECT_LT(keyPos(wire, "alpha"), keyPos(wire, "zulu"))
            << "Params is a std::map: this family is already sorted: " << wire;
}

TEST_F(JsonApiStateWireBytesTest, GetStatesAnswersAnEmptyObjectForAnIoWithNoValues)
{
    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "get_states" }, { "id", IO_PROBE }}));

    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("{}", req.body()) << "empty OBJECT, never null";
}

/*******************************************************************************
 * 8. query - HEX CASE and INVALID UTF-8 (deltas 2 and 3).
 *
 * NOTE ON THE REQUEST, and it is not a typo: buildQuery() tests
 * jParam.Exists("id") and then reads jParam["input_id"]. Both keys are sent
 * below because that quirk is PRE-EXISTING behaviour this ticket does not
 * touch; changing it would be a behaviour change hidden inside a port.
 ******************************************************************************/
TEST_F(JsonApiStateWireBytesTest, QueryEscapesAccentWithUppercaseHexToday)
{
    probe->queryValues["zulu"]  = std::string("caf") + RAW_E_ACUTE;
    probe->queryValues["alpha"] = "plain";

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "query" },
                                { "id", IO_PROBE },
                                { "input_id", IO_PROBE },
                                { "param", "anything" }}));

    ASSERT_EQ(1u, req.count());
    const std::string wire = req.body();

    EXPECT_TRUE(contains(wire, ASCII_E_UPPER)) << wire;
    EXPECT_FALSE(contains(wire, ASCII_E_LOWER)) << wire;
    EXPECT_FALSE(contains(wire, RAW_E_ACUTE)) << wire;
}

TEST_F(JsonApiStateWireBytesTest, QueryDropsThePairOnInvalidUtf8Today)
{
    probe->queryValues["zulu"]  = std::string("a") + INVALID_UTF8 + "z";
    probe->queryValues["alpha"] = "plain";

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "query" },
                                { "id", IO_PROBE },
                                { "input_id", IO_PROBE },
                                { "param", "anything" }}));

    ASSERT_EQ(1u, req.count());
    const std::string wire = req.body();

    EXPECT_EQ(std::string::npos, keyPos(wire, "zulu")) << wire;
    EXPECT_NE(std::string::npos, keyPos(wire, "alpha")) << wire;
    EXPECT_FALSE(contains(wire, ASCII_FFFD)) << wire;
}

TEST_F(JsonApiStateWireBytesTest, QueryAnswersAnEmptyObjectWhenTheIoAnswersNothing)
{
    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "query" },
                                { "id", IO_PROBE },
                                { "input_id", IO_PROBE },
                                { "param", "anything" }}));

    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("{}", req.body()) << "empty OBJECT, never null";
}

/*******************************************************************************
 * 9. set_state - THE ANSWER, AND THE WS ENVELOPE.
 *
 * The payload itself is one ASCII pair and moves by NOT ONE BYTE: it is the
 * INVARIANT half of this family. What does move on the websocket is the
 * envelope, for the same reason as case 1bis - the answer changes emitter.
 ******************************************************************************/
TEST_F(JsonApiStateWireBytesTest, SetStateHttpBodyIsOneAsciiPair)
{
    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "set_state" },
                                { "id", IO_ZULU },
                                { "value", "hello" }}));

    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("{\"success\":\"true\"}", req.body());
}

TEST_F(JsonApiStateWireBytesTest, SetStateWsEnvelopePutsMsgBeforeDataToday)
{
    WsTestSession ws;
    ws.send(Json{{ "msg", "set_state" },
                 { "msg_id", "e41n-set" },
                 { "data", Json{{ "id", IO_ZULU }, { "value", "hello" }} }});

    ASSERT_EQ(1u, ws.count());
    const std::string wire = ws.lastMessage();

    EXPECT_TRUE(contains(wire, "\"success\":\"true\"")) << wire;

    ASSERT_NE(std::string::npos, keyPos(wire, "msg")) << wire;
    ASSERT_NE(std::string::npos, keyPos(wire, "data")) << wire;
    EXPECT_LT(keyPos(wire, "msg"), keyPos(wire, "data"))
            << "the jansson envelope is msg, msg_id, data: " << wire;
}
