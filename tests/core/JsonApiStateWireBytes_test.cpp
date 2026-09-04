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
 * THE FLIP HAS HAPPENED. This file shipped in two commits: the first pinned the
 * JANSSON bytes, case by case, on an untouched tree (19 + 5 green, suffix
 * ...Today); the migration commit rewrote exactly the assertions that had to
 * move and dropped the suffix. That is the proof the path is EXERCISED and not
 * merely compiled - a case that had to be edited is a case that ran.
 *
 * Eleven cases flipped and thirteen did NOT. The thirteen are INVARIANTS and
 * they must stay green forever: the three ...AnswersAnEmptyObject (the sixth
 * delta guard), GetStatesKeysAreAlphabetical (Params is already a std::map),
 * SetStateHttpBodyIsOneAsciiPair, and the two RemoteUI negatives
 * (InitialStatesAreSortedAsciiAndLowercaseHex, InitialStatesAlreadyEscapeDel).
 * If one of those moves, a VALUE or a STRUCTURE changed - stop and understand
 * why before touching the assertion.
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

#include <iostream>
#include <map>
#include <sstream>
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
TEST_F(JsonApiStateWireBytesTest, GetStateHttpKeysAreAlphabetical)
{
    HttpTestRequest req;
    req.send(getStateRequest());

    ASSERT_EQ(1u, req.count()) << "get_state did not answer";
    const std::string wire = req.body();

    ASSERT_NE(std::string::npos, keyPos(wire, IO_ZULU)) << wire;
    ASSERT_NE(std::string::npos, keyPos(wire, IO_ALPHA)) << wire;

    EXPECT_LT(keyPos(wire, IO_ALPHA), keyPos(wire, IO_ZULU))
            << "nlohmann::json is a std::map: the answer is sorted, whatever "
               "order the request asked for. DELTA 1, declared: " << wire;
}

TEST_F(JsonApiStateWireBytesTest, GetStateWsKeysAreAlphabetical)
{
    WsTestSession ws;
    ws.send(wsGetStateRequest());

    ASSERT_EQ(1u, ws.count()) << "get_state did not answer on the websocket";
    const std::string wire = ws.lastMessage();

    ASSERT_NE(std::string::npos, keyPos(wire, IO_ZULU)) << wire;
    ASSERT_NE(std::string::npos, keyPos(wire, IO_ALPHA)) << wire;

    EXPECT_LT(keyPos(wire, IO_ALPHA), keyPos(wire, IO_ZULU)) << wire;
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
TEST_F(JsonApiStateWireBytesTest, GetStateWsEnvelopePutsDataFirst)
{
    WsTestSession ws;
    ws.send(wsGetStateRequest());

    ASSERT_EQ(1u, ws.count());
    const std::string wire = ws.lastMessage();

    ASSERT_NE(std::string::npos, keyPos(wire, "msg")) << wire;
    ASSERT_NE(std::string::npos, keyPos(wire, "data")) << wire;

    EXPECT_LT(keyPos(wire, "data"), keyPos(wire, "msg"))
            << "the nlohmann envelope sorts to data, msg, msg_id: " << wire;
    EXPECT_LT(keyPos(wire, "data"), keyPos(wire, "msg_id")) << wire;
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
TEST_F(JsonApiStateWireBytesTest, GetStateHttpEscapesAccentWithLowercaseHex)
{
    zulu->setRawString(std::string("caf") + RAW_E_ACUTE);

    HttpTestRequest req;
    req.send(getStateRequest());

    ASSERT_EQ(1u, req.count());
    const std::string wire = req.body();

    EXPECT_TRUE(contains(wire, ASCII_E_LOWER))
            << "nlohmann escapes U+00E9 with LOWERCASE hex. DELTA 2, declared, "
               "and it is the ONLY difference from jansson's form here: " << wire;
    EXPECT_FALSE(contains(wire, ASCII_E_UPPER))
            << "not jansson's form either - a case folding comparison would see "
               "the two as equal, which is the defect E4.1a found in the "
               "ParamsJson tripwire: " << wire;
    EXPECT_FALSE(contains(wire, RAW_E_ACUTE))
            << "ensure_ascii = true: no raw UTF-8 on this wire: " << wire;

    for (unsigned char c : wire)
        ASSERT_LT(c, 0x80u) << "the get_state wire must stay ASCII only";
}

TEST_F(JsonApiStateWireBytesTest, GetStateWsEscapesAccentWithLowercaseHex)
{
    zulu->setRawString(std::string("caf") + RAW_E_ACUTE);

    WsTestSession ws;
    ws.send(wsGetStateRequest());

    ASSERT_EQ(1u, ws.count());
    const std::string wire = ws.lastMessage();

    EXPECT_TRUE(contains(wire, ASCII_E_LOWER)) << wire;
    EXPECT_FALSE(contains(wire, ASCII_E_UPPER)) << wire;
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
TEST_F(JsonApiStateWireBytesTest, GetStateHttpKeepsThePairOnInvalidUtf8AsReplacementChar)
{
    zulu->setRawString(std::string("a") + INVALID_UTF8 + "z");

    HttpTestRequest req;
    req.send(getStateRequest());

    ASSERT_EQ(1u, req.count());
    const std::string wire = req.body();

    EXPECT_NE(std::string::npos, keyPos(wire, IO_ZULU))
            << "the pair no longer disappears. DELTA 3, and it is a STRUCTURE "
               "delta: " << wire;
    EXPECT_NE(std::string::npos, keyPos(wire, IO_ALPHA)) << wire;
    //One U+FFFD per invalid byte, and 0xFF 0x80 is two of them.
    EXPECT_EQ(2u, occurrences(wire, ASCII_FFFD)) << wire;
    EXPECT_TRUE(contains(wire, std::string("\"") + IO_ZULU + "\":\"a"
                         + ASCII_FFFD + ASCII_FFFD + "z\"")) << wire;
}

TEST_F(JsonApiStateWireBytesTest, GetStateWsKeepsThePairOnInvalidUtf8AsReplacementChar)
{
    zulu->setRawString(std::string("a") + INVALID_UTF8 + "z");

    WsTestSession ws;
    ws.send(wsGetStateRequest());

    ASSERT_EQ(1u, ws.count());
    const std::string wire = ws.lastMessage();

    EXPECT_NE(std::string::npos, keyPos(wire, IO_ZULU)) << wire;
    EXPECT_NE(std::string::npos, keyPos(wire, IO_ALPHA)) << wire;
    EXPECT_EQ(2u, occurrences(wire, ASCII_FFFD)) << wire;
}

/*******************************************************************************
 * 4. get_state - DEL, 0x7F (delta 4). THE ONE THAT MOVES Content-Length.
 *
 * JSON_ENSURE_ASCII only acts on NON-ASCII, and DEL is ASCII: jansson puts the
 * raw control byte on the wire. nlohmann under ensure_ascii escapes every
 * codepoint >= 0x7F. Five bytes per occurrence, and the HTTP Content-Length
 * header moves with the body - which is why this case reads the header too.
 ******************************************************************************/
TEST_F(JsonApiStateWireBytesTest, GetStateHttpEscapesDelAndSizesTheBody)
{
    zulu->setRawString(std::string("a") + DEL_BYTE + "z");

    HttpTestRequest req;
    req.send(getStateRequest());

    ASSERT_EQ(1u, req.count());
    const std::string wire = req.body();

    EXPECT_EQ(0u, occurrences(wire, std::string(1, DEL_BYTE)))
            << "DELTA 4: nlohmann escapes every codepoint >= 0x7F under "
               "ensure_ascii, DEL included: " << wire;
    EXPECT_TRUE(contains(wire, ASCII_DEL)) << wire;

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
TEST_F(JsonApiStateWireBytesTest, GetStateHttpKeepsTheWholeValueAcrossAnEmbeddedNul)
{
    zulu->setRawString(std::string("a\0z", 3));

    HttpTestRequest req;
    req.send(getStateRequest());

    ASSERT_EQ(1u, req.count());
    const std::string wire = req.body();

    ASSERT_NE(std::string::npos, keyPos(wire, IO_ZULU)) << wire;
    EXPECT_TRUE(contains(wire, std::string("\"") + IO_ZULU + "\":\"a"
                         + ASCII_NUL + "z\""))
            << "DELTA 5: the tail after the NUL is no longer lost: " << wire;
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
TEST_F(JsonApiStateWireBytesTest, GetStatesEscapesAccentWithLowercaseHex)
{
    probe->allValues["zulu"]  = std::string("caf") + RAW_E_ACUTE;
    probe->allValues["alpha"] = "plain";

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "get_states" }, { "id", IO_PROBE }}));

    ASSERT_EQ(1u, req.count());
    const std::string wire = req.body();

    EXPECT_TRUE(contains(wire, ASCII_E_LOWER)) << wire;
    EXPECT_FALSE(contains(wire, ASCII_E_UPPER)) << wire;
    EXPECT_FALSE(contains(wire, RAW_E_ACUTE)) << wire;
}

TEST_F(JsonApiStateWireBytesTest, GetStatesKeepsThePairOnInvalidUtf8AsReplacementChar)
{
    probe->allValues["zulu"]  = std::string("a") + INVALID_UTF8 + "z";
    probe->allValues["alpha"] = "plain";

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "get_states" }, { "id", IO_PROBE }}));

    ASSERT_EQ(1u, req.count());
    const std::string wire = req.body();

    EXPECT_NE(std::string::npos, keyPos(wire, "zulu"))
            << "Params::toJson() keeps what jansson_from_params() dropped in "
               "silence. DELTA 3: " << wire;
    EXPECT_NE(std::string::npos, keyPos(wire, "alpha")) << wire;
    EXPECT_EQ(2u, occurrences(wire, ASCII_FFFD)) << wire;
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
TEST_F(JsonApiStateWireBytesTest, QueryEscapesAccentWithLowercaseHex)
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

    EXPECT_TRUE(contains(wire, ASCII_E_LOWER)) << wire;
    EXPECT_FALSE(contains(wire, ASCII_E_UPPER)) << wire;
    EXPECT_FALSE(contains(wire, RAW_E_ACUTE)) << wire;
}

TEST_F(JsonApiStateWireBytesTest, QueryKeepsThePairOnInvalidUtf8AsReplacementChar)
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

    EXPECT_NE(std::string::npos, keyPos(wire, "zulu")) << wire;
    EXPECT_NE(std::string::npos, keyPos(wire, "alpha")) << wire;
    EXPECT_EQ(2u, occurrences(wire, ASCII_FFFD)) << wire;
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

TEST_F(JsonApiStateWireBytesTest, SetStateWsEnvelopePutsDataFirst)
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
    EXPECT_LT(keyPos(wire, "data"), keyPos(wire, "msg"))
            << "the nlohmann envelope sorts to data, msg, msg_id: " << wire;
}

/*******************************************************************************
 * ===========================================================================
 * PART TWO - THE REMOTEUI BRIDGE.
 * ===========================================================================
 *
 * RemoteUIWebSocketHandler::sendInitialIOStates() is the reason E4.1n exists as
 * a ticket rather than as a line of another one. It calls buildJsonState() and
 * then does, on the jansson result it is handed:
 *
 *      char *json_str = json_dumps(jret, JSON_COMPACT);   (:246)
 *      Json data = Json::parse(json_str);                 (:250)
 *
 * a FULL serialization plus a FULL reparse, for no purpose but crossing the
 * border between the two libraries. When buildJsonState() answers a Json those
 * six lines become an assignment.
 *
 * ---------------------------------------------------------------------------
 * THE FORM THAT COMPILES AND THROWS, AND NOTHING CATCHES IT
 * ---------------------------------------------------------------------------
 * :250 is a THROWING Json::parse (the two other Json::parse of that file, :128
 * and :185, sit inside a try). It is called from the buildJsonState()
 * completion, i.e. from an audio callback on the event loop, and there is NO
 * try anywhere above it: a parse_error there is std::terminate on a live
 * server. Same shape as the KNX precedent, where a bare dump() in
 * monitorWait() took calaos_knx down on an ordinary bus frame.
 *
 * MEASURED, and the honest verdict: no input could be built that makes it
 * throw TODAY, because jansson validates UTF-8 at json_string() and json_dumps
 * therefore cannot emit anything Json::parse refuses - the NULL return is the
 * only failure and :249 guards it. The form is a LATENT mine, disarmed by the
 * very validation this epic removes. This ticket deletes the form outright,
 * which is the only way to be sure it never becomes live.
 *
 * ---------------------------------------------------------------------------
 * WHAT THE DEVICE SEES, AND IT IS ALMOST NOTHING - MEASURED
 * ---------------------------------------------------------------------------
 * The RemoteUI wire is the one wire of this ticket that talks to a PHYSICAL
 * DEVICE with its own client, not updated in lockstep with the server, so every
 * byte counts double. It is also, measured, the wire that moves LEAST, and for
 * a reason worth spelling out: the payload ALREADY makes a round trip through
 * nlohmann today. json_dumps -> Json::parse -> dump(ensure_ascii) means that of
 * the five deltas of this ticket,
 *
 *   - key order       does NOT move. Twice over: the round trip already sorts,
 *                     AND RemoteUI::referenced_ios is a std::set, so the iolist
 *                     handed to buildJsonState() is already alphabetical.
 *   - hex case        does NOT move: the final dump is already nlohmann's.
 *   - DEL 0x7F        does NOT move: the final dump already escapes it.
 *   - invalid UTF-8   MOVES. Today the pair is dropped by json_string(), before
 *                     the bridge; tomorrow it arrives as U+FFFD. A key the
 *                     device never received starts arriving.
 *   - embedded NUL    MOVES, same reason: silent truncation becomes an escaped
 *                     NUL in the value.
 *
 * Two deltas, both of them poison payloads. That is the paragraph
 * RELEASE_NOTES.md needs, and these cases are what it is founded on.
 ******************************************************************************/

#include "RemoteUIWebSocketHandler.h"
#include "AutoScenarioDef.h"
#include "IO/RemoteUI/RemoteUI.h"
#include "AudioPlayer.h"
#include "HttpClient.h"
#include "libuvw.h"

#include <algorithm>
#include <deque>
#include <iterator>
#include <memory>
#include <set>
#include <vector>

namespace
{

const char *const REMOTE_UI_ID = "e41n_screen";

//A real HttpClient on an unconnected uvw::TcpHandle. Same object, and the same
//measurement, as HttpTestRequest::Client (JsonApiCharacterization.cpp:449): the
//constructor only needs the handle to be non-null, and ~HttpClient() does not
//call CloseConnection(). RemoteUIWebSocketHandler's constructor dereferences
//httpClient (it logs getClientIp()), so unlike WsTestSession::Handler this one
//cannot be built on nullptr.
class BridgeClient: public HttpClient
{
public:
    explicit BridgeClient(const std::shared_ptr<uvw::TcpHandle> &h):
        HttpClient(h), handle(h) {}

    std::shared_ptr<uvw::TcpHandle> handle;
};

//Subclass only to reach the protected session state, exactly as
//WsTestSession::Handler does for JsonApiHandlerWS. The production class is not
//modified beyond the private -> protected of that one block.
class BridgeHandler: public Calaos::RemoteUIWebSocketHandler
{
public:
    explicit BridgeHandler(HttpClient *c): RemoteUIWebSocketHandler(c) {}

    void attach(Calaos::RemoteUI *ui)
    {
        authenticated_remote_ui = ui;
        setAuthenticated(true);
    }
};

/* An AudioPlayer whose async getters never answer on their own. Same shape as
 * the one in core/JsonApiAudioState_test.cpp, and duplicated rather than shared
 * on purpose: that file characterizes buildJsonState() itself, this one
 * characterizes what RemoteUI does with its answer, and a shared fake would tie
 * two independent oracles together.
 */
class BridgeFakePlayer: public Calaos::AudioPlayer
{
public:
    explicit BridgeFakePlayer(Params &p): AudioPlayer(p) {}

    std::deque<Calaos::AudioRequest_cb> pending;

    void get_playlist_current(Calaos::AudioRequest_cb cb, Calaos::AudioPlayerData = Calaos::AudioPlayerData()) override
    { pending.push_back(cb); }
    void get_volume(Calaos::AudioRequest_cb cb, Calaos::AudioPlayerData = Calaos::AudioPlayerData()) override
    { pending.push_back(cb); }
    void get_playlist_size(Calaos::AudioRequest_cb cb, Calaos::AudioPlayerData = Calaos::AudioPlayerData()) override
    { pending.push_back(cb); }
    void get_current_time(Calaos::AudioRequest_cb cb, Calaos::AudioPlayerData = Calaos::AudioPlayerData()) override
    { pending.push_back(cb); }
    void get_status(Calaos::AudioRequest_cb cb, Calaos::AudioPlayerData = Calaos::AudioPlayerData()) override
    { pending.push_back(cb); }
    void get_songinfo(Calaos::AudioRequest_cb cb, Calaos::AudioPlayerData = Calaos::AudioPlayerData()) override
    { pending.push_back(cb); }

    //Detach the oldest pending answer, the way a real connection owns it and
    //can fire it after the IO - or the handler - is gone.
    Calaos::AudioRequest_cb takeNext()
    {
        if (pending.empty())
            return Calaos::AudioRequest_cb();
        Calaos::AudioRequest_cb cb = pending.front();
        pending.pop_front();
        return cb;
    }
};

std::string remoteUiXml()
{
    std::string x;
    x += "    <calaos:remote_ui type=\"RemoteUI\" id=\"";
    x += REMOTE_UI_ID;
    x += "\" name=\"Screen\" enabled=\"true\" visible=\"true\""
         " device_type=\"waveshare-86-panel\" grid_w=\"3\" grid_h=\"3\">\n";
    x += "      <calaos:pages>\n";
    x += "        <calaos:page name=\"p1\">\n";
    x += std::string("          <calaos:widget type=\"switch\" x=\"0\" y=\"0\" io_id=\"") + IO_ZULU + "\"/>\n";
    x += std::string("          <calaos:widget type=\"switch\" x=\"1\" y=\"0\" io_id=\"") + IO_ALPHA + "\"/>\n";
    x += "        </calaos:page>\n";
    x += "      </calaos:pages>\n";
    x += "    </calaos:remote_ui>\n";
    return x;
}

} //namespace

class RemoteUiStateBridgeTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();

        forgetIOState(IO_ZULU);
        forgetIOState(IO_ALPHA);

        //No rule: the default rules.xml references ids this house does not
        //declare, and RulesFactory silently drops a rule whose ids are unknown.
        loadConfig(ioXmlDocument(roomXml("Salon", "livingroom", remoteUiXml())),
                   rulesXmlDocument(""));

        screen = dynamic_cast<Calaos::RemoteUI *>(ListeRoom::Instance().get_io(REMOTE_UI_ID));
        ASSERT_NE(screen, nullptr) << "the RemoteUI IO was not created";

        //The two state IOs are added by hand, for the reason given at the top
        //of this file: set_value() would Save() the poisoned bytes into
        //Config's process-wide state cache.
        zulu  = addProbe(IO_ZULU, "InternalString", "Zulu");
        alpha = addProbe(IO_ALPHA, "InternalBool", "Alpha");

        auto loop = uvw::Loop::getDefault();
        auto tcp = loop->resource<uvw::TcpHandle>();
        client = std::make_shared<BridgeClient>(tcp);

        handler.reset(new BridgeHandler(client.get()));
        handler->sendData.connect([this](const std::string &d) { sent.push_back(d); });
        handler->attach(screen);
    }

    void TearDown() override
    {
        //Destroy the handler first: it holds a raw pointer on the client.
        handler.reset();
        if (client && client->handle)
            client->handle->close();
        client.reset();
        pumpEventLoop(2);

        JsonApiCharacterizationTest::TearDown();
    }

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

    BridgeFakePlayer *addPlayer(const std::string &id)
    {
        Params p;
        p.Add("id", id);
        p.Add("name", "Player");
        p.Add("type", "BridgeFakePlayer");

        BridgeFakePlayer *pl = new BridgeFakePlayer(p);
        firstRoom()->AddIO(pl);
        ListeRoom::Instance().addIOHash(pl);
        return pl;
    }

    std::string lastMessage() const { return sent.empty() ? std::string() : sent.back(); }

    Calaos::RemoteUI *screen = nullptr;
    ProbeIO *zulu = nullptr;
    ProbeIO *alpha = nullptr;

    std::shared_ptr<BridgeClient> client;
    std::unique_ptr<BridgeHandler> handler;
    std::vector<std::string> sent;
};

/*******************************************************************************
 * R1. THE ORDINARY PAYLOAD DOES NOT MOVE BY ONE BYTE. INVARIANT.
 *
 * This is the case the physical device depends on, and it is an INVARIANT on
 * BOTH sides of the migration: sorted keys, lowercase hex, ASCII only. If it
 * moves, the screens see a wire they were not shipped against.
 ******************************************************************************/
TEST_F(RemoteUiStateBridgeTest, InitialStatesAreSortedAsciiAndLowercaseHex)
{
    zulu->setRawString(std::string("caf") + RAW_E_ACUTE);

    handler->sendInitialIOStates();

    ASSERT_EQ(1u, sent.size()) << "sendInitialIOStates() sent nothing";
    const std::string wire = lastMessage();

    EXPECT_TRUE(contains(wire, "\"remote_ui_io_states\"")) << wire;

    ASSERT_NE(std::string::npos, keyPos(wire, IO_ALPHA)) << wire;
    ASSERT_NE(std::string::npos, keyPos(wire, IO_ZULU)) << wire;
    EXPECT_LT(keyPos(wire, IO_ALPHA), keyPos(wire, IO_ZULU))
            << "RemoteUI::referenced_ios is a std::set AND the payload already "
               "round trips through nlohmann: this wire is sorted on both sides "
               "of the migration: " << wire;

    EXPECT_TRUE(contains(wire, ASCII_E_LOWER))
            << "the RemoteUI wire is already dumped by nlohmann today: " << wire;
    EXPECT_FALSE(contains(wire, ASCII_E_UPPER)) << wire;
    EXPECT_FALSE(contains(wire, RAW_E_ACUTE)) << wire;

    for (unsigned char c : wire)
        ASSERT_LT(c, 0x80u) << "the RemoteUI wire must be ASCII only";
}

/*******************************************************************************
 * R1bis. DEL DOES NOT MOVE EITHER. INVARIANT.
 *
 * Same reason: the escaping the device sees is nlohmann's already, and nlohmann
 * escapes every codepoint >= 0x7F under ensure_ascii. Pinned as a case rather
 * than as a sentence, because it is a NEGATIVE result on a wire where a
 * positive one would have cost a firmware release.
 ******************************************************************************/
TEST_F(RemoteUiStateBridgeTest, InitialStatesAlreadyEscapeDel)
{
    zulu->setRawString(std::string("a") + DEL_BYTE + "z");

    handler->sendInitialIOStates();

    ASSERT_EQ(1u, sent.size());
    const std::string wire = lastMessage();

    EXPECT_TRUE(contains(wire, ASCII_DEL)) << wire;
    EXPECT_EQ(0u, occurrences(wire, std::string(1, DEL_BYTE))) << wire;
}

/*******************************************************************************
 * R2. INVALID UTF-8 - ONE OF THE TWO DELTAS THE DEVICE WILL SEE.
 ******************************************************************************/
TEST_F(RemoteUiStateBridgeTest, InitialStatesKeepThePairOnInvalidUtf8AsReplacementChar)
{
    zulu->setRawString(std::string("a") + INVALID_UTF8 + "z");

    handler->sendInitialIOStates();

    ASSERT_EQ(1u, sent.size());
    const std::string wire = lastMessage();

    EXPECT_NE(std::string::npos, keyPos(wire, IO_ZULU))
            << "DELTA, AND IT REACHES A PHYSICAL DEVICE: a key the screens "
               "never received starts arriving: " << wire;
    EXPECT_NE(std::string::npos, keyPos(wire, IO_ALPHA)) << wire;
    EXPECT_EQ(2u, occurrences(wire, ASCII_FFFD)) << wire;
}

/*******************************************************************************
 * R3. EMBEDDED NUL - THE OTHER ONE.
 ******************************************************************************/
TEST_F(RemoteUiStateBridgeTest, InitialStatesKeepTheWholeValueAcrossAnEmbeddedNul)
{
    zulu->setRawString(std::string("a\0z", 3));

    handler->sendInitialIOStates();

    ASSERT_EQ(1u, sent.size());
    const std::string wire = lastMessage();

    EXPECT_TRUE(contains(wire, std::string("\"") + IO_ZULU + "\":\"a"
                         + ASCII_NUL + "z\""))
            << "DELTA, AND IT REACHES A PHYSICAL DEVICE: the truncated tail "
               "comes back, escaped: " << wire;
}

/*******************************************************************************
 * R4. THE LIFE GUARD - THE RISK N.1 OF THIS TICKET, AND IT IS AN INVARIANT.
 *
 * buildJsonState() DEFERS its callback behind the audio chain. The device can
 * drop the connection in that window, and then the handler is destroyed while
 * an answer is in flight. Before the migration the callback owned a json_t and
 * had to json_decref() it on the abandon path (:241); after it, ownership is by
 * value and those decrefs disappear - BUT THE LIFE GUARDS MUST NOT.
 *
 * MEASURED WHILE WRITING THIS CASE, and it corrects the reading of the ticket:
 * there are TWO guards on this path, not one, and the one that actually stops
 * the callback is NOT the weak_ptr in RemoteUIWebSocketHandler.
 * RemoteUIWebSocketHandler IS-A JsonApiHandlerWS IS-A JsonApi, so destroying
 * the handler destroys JsonApi::apiAlive, and JsonApi.cpp:491 checks THAT token
 * before every step of the chain and before finishOne() - the result lambda is
 * never entered at all. The handler's own handlerAlive weak_ptr (:237) is a
 * second belt, load bearing for the post-auth Timer::singleShot which is NOT
 * inside JsonApi. Do not "clean up" either of them.
 ******************************************************************************/
TEST_F(RemoteUiStateBridgeTest, InitialStatesSurviveTheHandlerDyingMidFlight)
{
    /* referenced_ios is built from the XML at load time and this test does not
     * touch it: the PLAYER TAKES THE PLACE of the string IO, under the id the
     * screen already references. That is also what makes the case realistic -
     * a screen showing a music widget is exactly how the deferred path is
     * reached in production.
     */
    ASSERT_TRUE(deleteIO(zulu));
    zulu = nullptr;
    BridgeFakePlayer *player = addPlayer(IO_ZULU);

    handler->sendInitialIOStates();

    //Nothing sent yet: the answer waits for the six audio callbacks.
    ASSERT_EQ(0u, sent.size()) << "the answer must be deferred behind the player";
    ASSERT_EQ(1u, player->pending.size()) << "the chain did not start";

    //The connection object keeps the callback, then the device disconnects.
    Calaos::AudioRequest_cb lateAnswer = player->takeNext();
    ASSERT_FALSE(lateAnswer.empty());

    handler.reset();    //the device dropped the connection

    //The squeezebox-style answer arrives afterwards. Before T2.15 this
    //dereferenced a dead JsonApi; it must now be a no-op.
    lateAnswer(Calaos::AudioPlayerData());
    //Drop our copy of the chain, exactly as the dying connection object would.
    lateAnswer = Calaos::AudioRequest_cb();

    //The chain stopped at the first guard: no next request was queued...
    EXPECT_TRUE(player->pending.empty());
    //...and nothing was ever written to the socket.
    EXPECT_EQ(0u, sent.size());
}

/*******************************************************************************
 * R5/R6 - THE CONFIG PAYLOAD AND THE HANDLER'S OWN PARSE.
 *
 * remote_ui_config_update projects every referenced IO a SECOND time, next to
 * the projection JsonApi::buildJsonIO() publishes on 5454. The list of params
 * had been written twice, so the re-key of the scenario marker landed on one
 * transport only and the same equipment named the same scenario two ways. The
 * list is declared once now; R5 is what turns red the day a copy comes back.
 *
 * processApi() parses the frame itself before delegating, so the nesting
 * ceiling of the parent did not cover that parse. Reachable only after the HMAC
 * handshake, so what it cost was one allocation imposed by a provisioned
 * device, not an anonymous denial of service - which is why it could wait.
 ******************************************************************************/

namespace
{

//Nested arrays as raw TEXT: a document built with Json and dumped would nest
//no deeper than the builder went.
std::string bracketNest(int levels)
{
    std::string s;
    s.reserve(2 * static_cast<size_t>(levels));
    for (int i = 0; i < levels; i++) s += '[';
    for (int i = 0; i < levels; i++) s += ']';
    return s;
}

//Spelled out rather than read from JsonApi::MaxRequestNestingDepth: a case that
//takes the ceiling from the code under test moves with it and pins nothing.
const int REQUEST_DEPTH_CAP = 2048;

std::set<std::string> keysOf(const Json &object)
{
    std::set<std::string> keys;
    for (Json::const_iterator it = object.cbegin(); it != object.cend(); ++it)
        keys.insert(it.key());
    return keys;
}

std::vector<std::string> difference(const std::set<std::string> &a,
                                    const std::set<std::string> &b)
{
    std::vector<std::string> out;
    std::set_difference(a.cbegin(), a.cend(), b.cbegin(), b.cend(),
                        std::back_inserter(out));
    return out;
}

std::string joined(const std::vector<std::string> &v)
{
    std::string s;
    for (const std::string &e: v) { if (!s.empty()) s += ", "; s += e; }
    return s.empty()? std::string("<none>"): s;
}

} //namespace

class RemoteUiConfigProjectionTest: public RemoteUiStateBridgeTest
{
protected:
    void SetUp() override
    {
        RemoteUiStateBridgeTest::SetUp();
        handler->closeConnection.connect([this](int code, const std::string &reason)
        { closeEvents.push_back(std::make_pair(code, reason)); });

        /* The screen is left exactly as the XML declares it - without
         * `brightness` and without `timeout`. Until T3.68 the two had to be
         * added here or the local branch answered nothing at all, so their
         * absence is now part of what R6bis and R6ter measure.
         */
    }

    /* Every param either projection knows about, all of them non empty, and
     * BOTH scenario markers. A param that is absent is dropped by both sides
     * for reasons of their own, so an IO carrying only some of them would make
     * the two key sets agree without proving anything.
     */
    void fillEveryProjectedParam(IOBase *io)
    {
        Params &p = io->get_params();
        p.Add("hits", "7");
        p.Add("var_type", "string");
        p.Add("visible", "true");
        p.Add("chauffage_id", "ch1");
        p.Add("rw", "true");
        p.Add("unit", "C");
        p.Add("gui_type", "text");
        p.Add("state", "on");
        p.Add("auto_scenario", "as_legacy");
        p.Add(Calaos::AutoScenarioDef::KEY_UID, "as_0");
        p.Add("step", "1");
        p.Add("io_type", "input");
        p.Add("io_style", "flat");
        p.Add("value_warning", "9");
    }

    //The io_items entry the screen receives for one IO.
    Json remoteUiProjection(const std::string &ioId)
    {
        sent.clear();
        handler->sendConfigUpdate();

        const Json envelope = Json::parse(lastMessage(), nullptr, false);
        if (!envelope.is_object())
            return Json();

        const Json items = envelope.value("data", Json::object()).value("io_items", Json::array());
        for (const Json &item: items)
            if (item.value("id", std::string()) == ioId)
                return item;

        return Json();
    }

    //The projection 5454 publishes for the same IO. status_info is dropped:
    //it is a nested object the config payload has never carried, and it is not
    //part of the param list the two sides share.
    Json apiProjection(IOBase *io)
    {
        Json jio = Json::object();
        handler->buildJsonIO(io, jio);
        jio.erase("status_info");
        return jio;
    }

    //The same projection untouched. R7 needs status_info: it is one of the
    //three points the two sides disagree on, so trimming it away would make
    //that case measure nothing.
    Json apiProjectionRaw(IOBase *io)
    {
        Json jio = Json::object();
        handler->buildJsonIO(io, jio);
        return jio;
    }

    /* The IO the value policies are measured on, and it is built to make each
     * of them BITE: a non empty VALUE but no `state` param, a param that
     * EXISTS and is EMPTY, and status info. An IO whose params are all filled
     * and which carries no status info makes the two projections agree for
     * reasons of its own, and every case below would pass on an unchanged wire.
     */
    void makePolicyProbe(ProbeIO *io)
    {
        io->setRawString("hello");
        io->get_params().Add("unit", "");
        io->setStatusInfo(Calaos::IOBase::StatusType::BatteryLevel, 42.0);
    }

    //One remote_ui_get_config frame whose "probe" member nests `levels` arrays.
    //The root object is a level of its own, so the document is levels + 1 deep.
    static std::string deepGetConfig(int levels)
    {
        return std::string("{\"msg\":\"remote_ui_get_config\",\"probe\":") +
               bracketNest(levels) + "}";
    }

    std::vector<std::pair<int, std::string>> closeEvents;
};

/*******************************************************************************
 * R5. THE TWO PROJECTIONS OF THE SAME IO PUBLISH THE SAME KEYS.
 *
 * ⭐ THE GUARD RAIL AGAINST THE NEXT DIVERGENCE. The list of params is declared
 * once now, so a re-key cannot land on one transport only - but a second copy
 * can always be written back in, and this is what turns red when it is. It
 * needs an IO carrying EVERY projected param, both markers included: an IO with
 * only some of them makes the two key sets agree for reasons of its own.
 ******************************************************************************/
TEST_F(RemoteUiConfigProjectionTest, TheTwoProjectionsOfAnIoPublishTheSameKeys)
{
    fillEveryProjectedParam(zulu);

    const Json remote = remoteUiProjection(IO_ZULU);
    ASSERT_TRUE(remote.is_object()) << "the config payload carried no entry for " << IO_ZULU;

    const Json api = apiProjection(zulu);
    ASSERT_TRUE(api.is_object());

    const std::set<std::string> remoteKeys = keysOf(remote);
    const std::set<std::string> apiKeys = keysOf(api);

    const std::vector<std::string> onlyRemote = difference(remoteKeys, apiKeys);
    const std::vector<std::string> onlyApi = difference(apiKeys, remoteKeys);

    EXPECT_TRUE(onlyRemote.empty())
            << "only in the RemoteUI payload: " << joined(onlyRemote);
    EXPECT_TRUE(onlyApi.empty())
            << "only in the 5454 payload: " << joined(onlyApi);
}

/*******************************************************************************
 * R5bis. THE MARKER THE SCREEN ACTUALLY HEARS.
 *
 * The IO below carries BOTH markers, so this is a choice and not an absence:
 * the legacy key is on the IO and stays off the wire. Read through
 * AutoScenarioDef::KEY_UID and not through a literal, so the next re-key moves
 * this case with the code instead of staying green on a name nothing
 * publishes any more.
 ******************************************************************************/
TEST_F(RemoteUiConfigProjectionTest, ConfigUpdatePublishesTheDefinitionUid)
{
    fillEveryProjectedParam(zulu);

    const Json remote = remoteUiProjection(IO_ZULU);
    ASSERT_TRUE(remote.is_object());

    EXPECT_TRUE(remote.contains(Calaos::AutoScenarioDef::KEY_UID))
            << "the screen no longer hears the published marker";
    EXPECT_FALSE(remote.contains("auto_scenario"))
            << "the two transports name the same scenario differently";
}

/*******************************************************************************
 * R7. THE THREE VALUE POLICIES THE TWO PROJECTIONS DO NOT SHARE.
 *
 * The list of params is declared once (R5); the VALUE each side publishes for
 * one param is not, and the three differences below are pinned AS THEY ARE.
 * None of them can be levelled without moving a wire: the 5454 side is under
 * golden files, the config side is read by a physical screen that is not
 * upgraded with the server and negotiates no version - see
 * docs/refactoring/T3.69.md.
 ******************************************************************************/

/* P1. `state` and `var_type` are COMPUTED on 5454 and READ AS PARAMS here.
 *
 * The probe has a value and no `state` param, so the two sides answer
 * differently for the same key on the same IO. The screen is not left blind by
 * this: it gets its states from remote_ui_io_states, pinned by R1.
 */
TEST_F(RemoteUiConfigProjectionTest, StateAndVarTypeAreComputedFor5454Only)
{
    fillEveryProjectedParam(zulu);
    zulu->get_params().Delete("state");
    zulu->get_params().Delete("var_type");
    makePolicyProbe(zulu);

    const Json remote = remoteUiProjection(IO_ZULU);
    ASSERT_TRUE(remote.is_object()) << "the config payload carried no entry for " << IO_ZULU;
    const Json api = apiProjectionRaw(zulu);

    EXPECT_EQ("hello", api.value("state", std::string()))
            << "5454 stopped computing the state from the value of the IO";
    EXPECT_EQ("string", api.value("var_type", std::string()));

    EXPECT_FALSE(remote.contains("state"))
            << "the config payload started carrying a state the screen never had";
    EXPECT_FALSE(remote.contains("var_type"));
}

/* P1bis. AND THE PARAM READ IS A REAL READ, NOT AN ABSENCE. INVARIANT.
 *
 * No IO the server builds carries `state` as a param - `set_param("state")` is
 * 0 site in src/ - so an io.xml written by hand is the only way this branch
 * ever fires. It fires: the screen is handed the param, 5454 the computed
 * value, and the same key names two different things on the two wires.
 */
TEST_F(RemoteUiConfigProjectionTest, TheConfigPayloadReadsStateAsAPlainParam)
{
    makePolicyProbe(zulu);
    zulu->get_params().Add("state", "written-by-hand");

    const Json remote = remoteUiProjection(IO_ZULU);
    ASSERT_TRUE(remote.is_object());

    EXPECT_EQ("written-by-hand", remote.value("state", std::string()))
            << "the config payload stopped reading `state` as a param";
    EXPECT_EQ("hello", apiProjectionRaw(zulu).value("state", std::string()))
            << "5454 started trusting the param over the value";
}

/* P2. A PARAM THAT EXISTS AND IS EMPTY IS A KEY ON 5454 AND NO KEY HERE.
 *
 * `unit` is added empty, so this is the present-but-empty case and not the
 * absent one - the two are indistinguishable in the config payload, which
 * never asks whether the param exists.
 */
TEST_F(RemoteUiConfigProjectionTest, AnEmptyParamIsAKeyOn5454AndNoKeyForTheScreen)
{
    makePolicyProbe(zulu);
    ASSERT_TRUE(zulu->get_params().Exists("unit")) << "the empty param was not added";

    const Json remote = remoteUiProjection(IO_ZULU);
    ASSERT_TRUE(remote.is_object());
    const Json api = apiProjectionRaw(zulu);

    ASSERT_TRUE(api.contains("unit")) << "5454 stopped publishing an empty param";
    EXPECT_EQ("", api.value("unit", std::string("absent")));
    EXPECT_FALSE(remote.contains("unit"))
            << "the screen started receiving empty keys it never had";
}

/* P2bis. AN ABSENT PARAM IS NO KEY ON EITHER SIDE. INVARIANT.
 *
 * The half of the pair that keeps P2 from passing on an absence: without it a
 * projection that dropped every `unit` would leave P2 green.
 */
TEST_F(RemoteUiConfigProjectionTest, AnAbsentParamIsNoKeyOnEitherSide)
{
    makePolicyProbe(zulu);
    ASSERT_FALSE(zulu->get_params().Exists("chauffage_id"));

    const Json remote = remoteUiProjection(IO_ZULU);
    ASSERT_TRUE(remote.is_object());

    EXPECT_FALSE(apiProjectionRaw(zulu).contains("chauffage_id"))
            << "an absent param must emit NO key, never a null and never an empty string";
    EXPECT_FALSE(remote.contains("chauffage_id"));
}

/* P3. status_info IS A 5454 OBJECT, AND THE CONFIG PAYLOAD HAS NEVER HAD IT.
 *
 * The probe carries status info on purpose: on an IO that has none, both sides
 * omit the key and this case measures nothing.
 */
TEST_F(RemoteUiConfigProjectionTest, StatusInfoIsPublishedOn5454Only)
{
    makePolicyProbe(zulu);
    ASSERT_TRUE(zulu->hasStatusInfo()) << "the probe carries no status info";

    const Json remote = remoteUiProjection(IO_ZULU);
    ASSERT_TRUE(remote.is_object());
    const Json api = apiProjectionRaw(zulu);

    ASSERT_TRUE(api.contains("status_info")) << "5454 stopped publishing status_info";
    EXPECT_TRUE(api["status_info"].is_object());
    EXPECT_FALSE(remote.contains("status_info"))
            << "the screen started receiving a nested object it never had";
}

/* P3bis. AN IO WITHOUT STATUS INFO GETS NO KEY ANYWHERE. INVARIANT.
 *
 * A null Json means "no status info" and NOT an empty object, which is truthy
 * and would put "status_info":{} on every IO of the API.
 */
TEST_F(RemoteUiConfigProjectionTest, AnIoWithoutStatusInfoGetsNoKeyOnEitherSide)
{
    ASSERT_FALSE(alpha->hasStatusInfo());

    const Json remote = remoteUiProjection(IO_ALPHA);
    ASSERT_TRUE(remote.is_object()) << "the config payload carried no entry for " << IO_ALPHA;

    EXPECT_FALSE(apiProjectionRaw(alpha).contains("status_info"));
    EXPECT_FALSE(remote.contains("status_info"));
}

/*******************************************************************************
 * R7bis. ⭐ THE GUARD RAIL AGAINST A FOURTH POLICY.
 *
 * R5 compares the KEYS of the two projections; this compares the VALUES, on an
 * IO built so that all three known deltas fire. Each delta is asserted to have
 * fired before it is subtracted - a fixture that stopped exercising one would
 * turn this red rather than let the comparison pass on nothing - and what is
 * left must agree key for key AND byte for byte. A fourth divergence, whichever
 * side introduces it, lands here.
 ******************************************************************************/
TEST_F(RemoteUiConfigProjectionTest, ApartFromTheThreeKnownDeltasBothProjectionsAgree)
{
    fillEveryProjectedParam(zulu);
    zulu->get_params().Delete("state");
    zulu->get_params().Delete("var_type");
    makePolicyProbe(zulu);

    const Json remote = remoteUiProjection(IO_ZULU);
    ASSERT_TRUE(remote.is_object());
    Json api = apiProjectionRaw(zulu);

    //Delta 1: computed on 5454, absent here.
    ASSERT_TRUE(api.contains("state") && api.contains("var_type"));
    ASSERT_FALSE(remote.contains("state") || remote.contains("var_type"));
    api.erase("state");
    api.erase("var_type");

    //Delta 3: a nested object on 5454 only.
    ASSERT_TRUE(api.contains("status_info"));
    ASSERT_FALSE(remote.contains("status_info"));
    api.erase("status_info");

    //Delta 2: a param that exists and is empty. Subtracted by VALUE and not by
    //name, so a key that turns empty tomorrow is covered too; the screen side
    //is checked to hold none, which is what makes the subtraction one sided.
    std::vector<std::string> emptyOn5454;
    for (Json::const_iterator it = api.cbegin(); it != api.cend(); ++it)
        if (it.value().is_string() && it.value().get<std::string>().empty())
            emptyOn5454.push_back(it.key());
    ASSERT_FALSE(emptyOn5454.empty()) << "no empty param left: delta 2 is not exercised";
    for (const std::string &key: emptyOn5454)
    {
        EXPECT_FALSE(remote.contains(key))
                << "the screen received an empty key: " << key;
        api.erase(key);
    }

    EXPECT_EQ(api, remote)
            << "a fourth value policy separates the two projections\n"
            << "  5454:     " << api.dump() << "\n"
            << "  RemoteUI: " << remote.dump();
}

/*******************************************************************************
 * R6. THE HANDLER'S OWN PARSE IS UNDER THE NESTING CEILING TOO.
 *
 * remote_ui_get_config is answered by the LOCAL branch, before the parent is
 * reached at all: a silence here means the ceiling was applied to that parse
 * and not merely to the parent's, which is the whole point of the case.
 ******************************************************************************/
TEST_F(RemoteUiConfigProjectionTest, AFrameAboveTheCapIsRefusedByTheLocalParse)
{
    handler->processApi(deepGetConfig(REQUEST_DEPTH_CAP), Params());

    EXPECT_EQ(0u, sent.size())
            << "the local parse served a frame above the cap: " << lastMessage();
}

/*******************************************************************************
 * R6bis. AND THE FRAME JUST BELOW THE LINE IS SERVED. INVARIANT.
 *
 * The half of the pair that keeps the other one from passing on an empty
 * transport: a ceiling that refused everything would leave R6 green.
 ******************************************************************************/
TEST_F(RemoteUiConfigProjectionTest, AFrameAtTheCapIsServed)
{
    handler->processApi(deepGetConfig(REQUEST_DEPTH_CAP - 1), Params());

    ASSERT_EQ(1u, sent.size()) << "a frame at the cap stopped being served by "
                                  "the local branch";
    const Json envelope = Json::parse(lastMessage(), nullptr, false);
    EXPECT_EQ("remote_ui_config", envelope.value("msg", std::string()));
}

/*******************************************************************************
 * R6ter. A REFUSAL LEAVES THE SESSION ALONE.
 *
 * An unparsable frame has never closed a RemoteUI socket, and the ceiling must
 * not start: the device would reconnect, re-authenticate and resend.
 ******************************************************************************/
TEST_F(RemoteUiConfigProjectionTest, ADeepFrameLeavesTheSessionUsable)
{
    handler->processApi(deepGetConfig(REQUEST_DEPTH_CAP), Params());
    sent.clear();

    EXPECT_TRUE(closeEvents.empty()) << "the deep frame closed the session";

    handler->processApi("{\"msg\":\"remote_ui_get_config\"}", Params());

    ASSERT_EQ(1u, sent.size()) << "the session stopped answering after a deep frame";
    const Json envelope = Json::parse(lastMessage(), nullptr, false);
    EXPECT_EQ("remote_ui_config", envelope.value("msg", std::string()));
}

namespace
{

/* The handler reports its failures with cWarningDom, and LogStream ends on
 * std::cout: swapping the buffer is the only way to read what a device-facing
 * failure actually blames. Restore in the destructor, gtest writes there too.
 */
class CoutCapture
{
public:
    CoutCapture(): saved(std::cout.rdbuf(buffer.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(saved); }
    std::string text() const { return buffer.str(); }

private:
    std::ostringstream buffer;
    std::streambuf *saved;
};

} //namespace

/* The screen loaded by RemoteUiStateBridgeTest carries neither `brightness`
 * nor `timeout` - no write path puts them there, so this is a device that was
 * provisioned and never adjusted, not a crippled fixture.
 */
class RemoteUiUnadjustedScreenTest: public RemoteUiStateBridgeTest
{
protected:
    Json getConfigEnvelope()
    {
        sent.clear();
        handler->processApi("{\"msg\":\"remote_ui_get_config\"}", Params());
        return Json::parse(lastMessage(), nullptr, false);
    }

    std::string logOf(const std::string &frame)
    {
        CoutCapture capture;
        handler->processApi(frame, Params());
        return capture.text();
    }
};

/*******************************************************************************
 * R7. A SCREEN NOBODY EVER ADJUSTED IS ANSWERED, AND WITH USABLE VALUES.
 *
 * The content is the case, not the arrival: a screen that receives an envelope
 * with no brightness in it is as dark as one that receives nothing. 100 is the
 * default getBrightness() has carried since T3.25; 30 is the value every
 * example in the wire spec shows, and the only one the tree states anywhere.
 ******************************************************************************/
TEST_F(RemoteUiUnadjustedScreenTest, AnUnadjustedScreenIsAnsweredWithUsableDefaults)
{
    const Json envelope = getConfigEnvelope();

    ASSERT_TRUE(envelope.is_object()) << "the screen received nothing at all";
    EXPECT_EQ("remote_ui_config", envelope.value("msg", std::string()));

    const Json data = envelope.value("data", Json::object());
    ASSERT_TRUE(data.contains("brightness")) << "no brightness on the wire";
    ASSERT_TRUE(data.contains("timeout")) << "no timeout on the wire";

    EXPECT_TRUE(data["brightness"].is_number_integer())
            << "brightness stopped being an int: " << data["brightness"].dump();
    EXPECT_TRUE(data["timeout"].is_number_integer())
            << "timeout stopped being an int: " << data["timeout"].dump();

    EXPECT_EQ(100, data.value("brightness", -1));
    EXPECT_EQ(30, data.value("timeout", -1));

    EXPECT_EQ("Screen", data.value("name", std::string()));
    EXPECT_TRUE(data.contains("pages")) << "the screen got no page list";
}

/*******************************************************************************
 * R7bis. AN ADJUSTED SCREEN RECEIVES WHAT IT WAS ADJUSTED TO. INVARIANT.
 *
 * The half that keeps R7 from passing on a projection that answers a constant.
 * Both values differ from the defaults on purpose, and the key set is pinned:
 * a physical device already reads this payload.
 ******************************************************************************/
TEST_F(RemoteUiUnadjustedScreenTest, AnAdjustedScreenStillReceivesItsOwnValues)
{
    screen->get_params().Add("brightness", "55");
    screen->get_params().Add("timeout", "45");

    const Json data = getConfigEnvelope().value("data", Json::object());

    EXPECT_EQ(55, data.value("brightness", -1));
    EXPECT_EQ(45, data.value("timeout", -1));

    const std::set<std::string> expected = {"brightness", "name", "pages",
                                            "room", "theme", "timeout"};
    EXPECT_EQ(expected, keysOf(data)) << "the config payload changed shape";
}

/*******************************************************************************
 * R8. A VALID FRAME IS NOT REPORTED AS A PARSE FAILURE.
 *
 * The frame below is well formed JSON. Blaming the parser for what happens
 * after it sends the next reader into the parser for hours; that misdirection
 * is the defect, on equal footing with the silence.
 ******************************************************************************/
TEST_F(RemoteUiUnadjustedScreenTest, AValidFrameIsNeverBlamedOnTheJsonParser)
{
    const std::string logged = logOf("{\"msg\":\"remote_ui_get_config\"}");

    EXPECT_EQ(std::string::npos, logged.find("JSON parse error"))
            << "a well formed frame was reported as a parse failure: " << logged;
}

/*******************************************************************************
 * R8bis. AND A FRAME THAT REALLY IS UNPARSABLE STILL SAYS SO. INVARIANT.
 *
 * The half that keeps R8 from passing on a handler that simply stopped naming
 * parse errors.
 ******************************************************************************/
TEST_F(RemoteUiUnadjustedScreenTest, AnUnparsableFrameIsStillReportedAsAParseError)
{
    const std::string logged = logOf("{\"msg\":");

    EXPECT_NE(std::string::npos, logged.find("JSON parse error"))
            << "a truncated frame no longer names the parser: " << logged;
}

namespace
{

/* Every place in `doc` that holds an empty string, named by its path. A scan
 * and not a key list: the next key added to the payload is covered without
 * anyone remembering to add it here.
 */
void collectEmptyStrings(const Json &doc, const std::string &path,
                         std::vector<std::string> &out)
{
    if (doc.is_string())
    {
        if (doc.get<std::string>().empty())
            out.push_back(path.empty()? std::string("<root>"): path);
    }
    else if (doc.is_object())
    {
        for (Json::const_iterator it = doc.cbegin(); it != doc.cend(); ++it)
            collectEmptyStrings(it.value(), path + "/" + it.key(), out);
    }
    else if (doc.is_array())
    {
        for (size_t i = 0; i < doc.size(); i++)
            collectEmptyStrings(doc[i], path + "/" + std::to_string(i), out);
    }
}

std::vector<std::string> emptyStringsIn(const Json &doc)
{
    std::vector<std::string> out;
    collectEmptyStrings(doc, std::string(), out);
    return out;
}

//The params a screen only carries once somebody opened its settings.
const char *const SCREENSAVER_PARAMS[] = {
    "screensaver_timeout", "screensaver_dimming", "screensaver_mode",
    "screensaver_clock_timezone", "screensaver_clock_format",
    "screensaver_clock_show_date", "screensaver_clock_date_format",
    "screensaver_clock_seconds"
};

} //namespace

/* The screen of RemoteUiStateBridgeTest is declared with an id, a name, a grid
 * and its pages, and nothing else - the shape a device has after provisioning
 * and before anybody opened its settings. Every case below depends on that:
 * a screen whose params are all filled emits no empty value even on the
 * unfixed server, and would measure nothing.
 */
class RemoteUiUnsetParamsTest: public RemoteUiStateBridgeTest
{
protected:
    void SetUp() override
    {
        RemoteUiStateBridgeTest::SetUp();

        for (const char *const param: SCREENSAVER_PARAMS)
            ASSERT_FALSE(screen->get_params().Exists(param))
                    << "the fixture now sets " << param << ": it stopped being "
                       "a screen nobody ever configured";
    }

    //The payload of the push, which is the only one the device listens to.
    Json pushPayload()
    {
        sent.clear();
        handler->sendConfigUpdate();

        const Json envelope = Json::parse(lastMessage(), nullptr, false);
        EXPECT_EQ("remote_ui_config_update", envelope.value("msg", std::string()));
        return envelope.value("data", Json::object());
    }

    //The payload of the answer to remote_ui_get_config.
    Json answerPayload()
    {
        sent.clear();
        handler->processApi("{\"msg\":\"remote_ui_get_config\"}", Params());

        const Json envelope = Json::parse(lastMessage(), nullptr, false);
        EXPECT_EQ("remote_ui_config", envelope.value("msg", std::string()));
        return envelope.value("data", Json::object());
    }
};

/*******************************************************************************
 * E1. ⭐⭐ NO VALUE OF THE PUSHED CONFIGURATION IS AN EMPTY STRING.
 *
 * The contract the device imposes, and it is one sided: a key it does not find
 * falls back to a default it carries itself, a key present and empty is fed to
 * a conversion that throws and takes the WHOLE payload down with it - pages,
 * widgets and IOs included. So the screen loses everything it is meant to
 * display because nobody ever opened its settings.
 *
 * Written as a sweep of the payload rather than as a list of keys: the day a
 * ninth screensaver key is added the same way, this is what turns red.
 ******************************************************************************/
TEST_F(RemoteUiUnsetParamsTest, ThePushedConfigurationCarriesNoEmptyValue)
{
    const Json data = pushPayload();
    ASSERT_FALSE(data.empty()) << "the screen received no configuration at all";

    const std::vector<std::string> empties = emptyStringsIn(data);
    EXPECT_TRUE(empties.empty())
            << "the screen is handed " << empties.size() << " empty value(s) it "
               "cannot convert, and drops the whole configuration: "
            << joined(empties) << "\n  payload: " << data.dump();
}

/*******************************************************************************
 * E1bis. AND NEITHER DOES THE ANSWER TO remote_ui_get_config.
 *
 * The second builder of a configuration payload. No shipped firmware asks for
 * it today, so this is the cheaper half of the pair - but it is built from the
 * same raw params and would carry the same empty values to whoever asks.
 ******************************************************************************/
TEST_F(RemoteUiUnsetParamsTest, TheAnsweredConfigurationCarriesNoEmptyValue)
{
    const Json data = answerPayload();
    ASSERT_FALSE(data.empty()) << "the screen received no configuration at all";

    const std::vector<std::string> empties = emptyStringsIn(data);
    EXPECT_TRUE(empties.empty())
            << "empty value(s) on the answered payload: " << joined(empties)
            << "\n  payload: " << data.dump();
}

/*******************************************************************************
 * E2. THE TWO KEYS THE DEVICE CONVERTS ARE ABSENT, NOT EMPTY AND NOT ZEROED.
 *
 * E1 would also be satisfied by sending "0", which is a different bug: the
 * screen would dim after nothing at all instead of keeping its own timing. The
 * direction of the answer is the case here, not just the absence of "".
 ******************************************************************************/
TEST_F(RemoteUiUnsetParamsTest, AnUnsetScreensaverKeyIsOmittedRatherThanZeroed)
{
    const Json data = pushPayload();

    for (const char *const param: SCREENSAVER_PARAMS)
        EXPECT_FALSE(data.contains(param))
                << param << " is on the wire although nothing ever set it: "
                << data[param].dump();
}

/*******************************************************************************
 * E3. A SCREEN THAT WAS CONFIGURED STILL RECEIVES ITS SETTINGS. INVARIANT.
 *
 * The half that keeps E1 and E2 from passing on a payload that dropped the
 * screensaver altogether. Values that are not defaults on purpose, checked
 * byte for byte.
 ******************************************************************************/
TEST_F(RemoteUiUnsetParamsTest, AConfiguredScreensaverIsPublishedUnchanged)
{
    screen->get_params().Add("screensaver_timeout", "120");
    screen->get_params().Add("screensaver_dimming", "15");
    screen->get_params().Add("screensaver_mode", "clock");
    screen->get_params().Add("screensaver_clock_format", "12");

    const Json data = pushPayload();

    EXPECT_EQ("120", data.value("screensaver_timeout", std::string()));
    EXPECT_EQ("15", data.value("screensaver_dimming", std::string()));
    EXPECT_EQ("clock", data.value("screensaver_mode", std::string()));
    EXPECT_EQ("12", data.value("screensaver_clock_format", std::string()));

    //The four that were left alone stay off the wire.
    EXPECT_FALSE(data.contains("screensaver_clock_timezone"));
    EXPECT_FALSE(data.contains("screensaver_clock_show_date"));
    EXPECT_FALSE(data.contains("screensaver_clock_date_format"));
    EXPECT_FALSE(data.contains("screensaver_clock_seconds"));

    EXPECT_TRUE(emptyStringsIn(data).empty())
            << "a configured screen is handed empty values too: "
            << joined(emptyStringsIn(data));
}

/*******************************************************************************
 * E4. THE PUSH STILL CARRIES WHAT THE SCREEN IS MEANT TO DISPLAY.
 *
 * The failure mode being closed is TOTAL: the device throws the payload away
 * whole. A fix that emptied the payload instead of the keys would leave E1
 * green and the screen just as blank, so the parts that must survive are named
 * here.
 ******************************************************************************/
TEST_F(RemoteUiUnsetParamsTest, ThePushStillCarriesTheNameThePagesAndTheIos)
{
    const Json data = pushPayload();

    EXPECT_EQ("Screen", data.value("name", std::string()));

    ASSERT_TRUE(data.contains("pages")) << "the screen got no page list";
    ASSERT_TRUE(data["pages"].is_array());
    EXPECT_EQ(1u, data["pages"].size());

    ASSERT_TRUE(data.contains("io_items"));
    EXPECT_EQ(2u, data["io_items"].size());

    EXPECT_EQ(3, data.value("grid_width", -1));
    EXPECT_EQ(3, data.value("grid_height", -1));
    ASSERT_TRUE(data.contains("brightness"));
    EXPECT_TRUE(data.at("brightness").is_number_integer());
}
