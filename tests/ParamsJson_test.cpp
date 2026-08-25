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
 * E4.1a - CHARACTERIZATION of the two JSON faces of Params.
 *
 * Params is THE bridge of the jansson -> nlohmann migration: src/lib/Params.h
 * is the one header that includes BOTH libraries, and almost every payload of
 * the JSON API crosses it. E4.1a cuts that bridge: Params keeps only its
 * nlohmann face, and the jansson serialization moves out, verbatim, into the
 * transitional adapter of src/lib/Jansson_Addition.h.
 *
 * This file is written BEFORE that move and must stay green across it: it
 * freezes what the jansson face produces today, byte-observable semantics
 * included, so that the move can be proven to be a move and not a rewrite.
 *
 * THE ONE LINE THAT CHANGES AT MIGRATION TIME is paramsToJansson() below - the
 * seam. Every assertion in this file is written against that seam and none of
 * them is touched by the migration commit.
 *
 * Deliberately NOT asserted: the order of the keys inside a serialized
 * document. The user decision of 2026-08-17 assumes sorted keys
 * (nlohmann::json, not ordered_json) and E4.0's oracle contract compares
 * documents, never strings. The ordering that IS pinned here is the one the
 * adapter depends on and that IS semantic for C++ callers: Params is a
 * std::map, so its iteration is alphabetical, not insertion-ordered.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <jansson.h>

#include <string>
#include <vector>

#include "Utils.h"
#include "Params.h"
#include "Jansson_Addition.h"

namespace
{

/* THE SEAM. Before E4.1a this was Params::toJson(); it is now the free
 * transitional adapter jansson_from_params() of Jansson_Addition.h. This
 * function body is the ONLY line of this file the migration commit touched -
 * not one assertion moved. */
json_t *paramsToJansson(const Params &p)
{
    return jansson_from_params(p);
}

/* The invalid sequence used all over the E4.0 series (JsonApiSession_test.cpp:146).
 * 0xff is not a legal UTF-8 lead byte, 0x80 is a bare continuation byte. */
const char INVALID_UTF8_BYTES[] = { (char)0xff, (char)0x80, 'x', '\0' };

/* A deliberately RICH fixture.
 *
 * The recurring defect of this series is the "poor fixture": a dataset too
 * uniform to tell two interchangeable fields apart, so that swapping them
 * leaves the suite green. Every choice below exists to make a swap visible:
 *   - keys and values are drawn from DISJOINT vocabularies ("k_*" vs "v_*"),
 *     so a key<->value swap in the serializer cannot go unnoticed;
 *   - no two values are equal, so a permutation of the values is caught;
 *   - insertion order is NOT alphabetical, so map ordering is distinguishable
 *     from insertion ordering;
 *   - the types that the migration could silently promote are all present:
 *     an integer-looking value, a float-looking value, a boolean-looking
 *     value, a null-looking value, an empty string and a non-ASCII string.
 */
Params richFixture()
{
    Params p;
    //inserted in reverse-alphabetical order on purpose
    p.Add("k_zulu", "v_zulu");
    p.Add("k_null_like", "null");
    p.Add("k_int_like", "3");
    p.Add("k_float_like", "1234.56789");
    p.Add("k_empty", "");
    p.Add("k_bool_like", "true");
    p.Add("k_accent", "\xc3\xa9\xc3\xa0\xc3\xbc"); //"éàü" in UTF-8
    p.Add("k_alpha", "v_alpha");
    return p;
}

std::string janssonValueOf(json_t *obj, const char *key)
{
    json_t *v = json_object_get(obj, key);
    if (!v || !json_is_string(v))
        return std::string("<absent-or-not-a-string>");
    return std::string(json_string_value(v));
}

} //namespace

/*******************************************************************************
 * The jansson face
 ******************************************************************************/

TEST(ParamsJson, ToJansson_IsAnObjectAndEveryValueIsAJsonString)
{
    /* 32 json_string and ZERO json_integer/json_real in the whole API today:
     * an int that became a JSON number would be a contract break, and the
     * oracle of the golden suite is type-strict (3 != "3"). This is the
     * assertion that catches a serializer that "helpfully" types its output. */
    Params p = richFixture();
    json_t *j = paramsToJansson(p);
    ASSERT_TRUE(j != nullptr);
    ASSERT_TRUE(json_is_object(j)) << "Params no longer serializes to a JSON object";
    EXPECT_EQ(8u, json_object_size(j));

    const char *key;
    json_t *value;
    json_object_foreach(j, key, value)
    {
        EXPECT_TRUE(json_is_string(value))
                << "key '" << key << "' is no longer a JSON string";
        EXPECT_FALSE(json_is_number(value))
                << "key '" << key << "' became a JSON number";
        EXPECT_FALSE(json_is_boolean(value))
                << "key '" << key << "' became a JSON boolean";
        EXPECT_FALSE(json_is_null(value))
                << "key '" << key << "' became a JSON null";
    }
    json_decref(j);
}

TEST(ParamsJson, ToJansson_MapsEachKeyToItsOwnValue)
{
    /* The anti-swap case: key and value vocabularies are disjoint and no two
     * values are equal, so a key<->value swap or any value permutation in the
     * serializer turns this red. */
    Params p = richFixture();
    json_t *j = paramsToJansson(p);
    ASSERT_TRUE(j != nullptr);

    EXPECT_EQ("v_alpha", janssonValueOf(j, "k_alpha"));
    EXPECT_EQ("v_zulu", janssonValueOf(j, "k_zulu"));
    EXPECT_EQ("3", janssonValueOf(j, "k_int_like"));
    EXPECT_EQ("1234.56789", janssonValueOf(j, "k_float_like"));
    EXPECT_EQ("true", janssonValueOf(j, "k_bool_like"));
    EXPECT_EQ("null", janssonValueOf(j, "k_null_like"));
    EXPECT_EQ("", janssonValueOf(j, "k_empty"));
    EXPECT_EQ("\xc3\xa9\xc3\xa0\xc3\xbc", janssonValueOf(j, "k_accent"));

    //and the values are NOT usable as keys - proves the vocabularies really
    //are disjoint, so the swap this case is meant to catch is catchable
    EXPECT_TRUE(json_object_get(j, "v_alpha") == nullptr);
    EXPECT_TRUE(json_object_get(j, "v_zulu") == nullptr);

    json_decref(j);
}

TEST(ParamsJson, ToJansson_EmptyParamsIsAnEmptyObjectNotNull)
{
    /* JsonApiEvents_test.cpp:648 relies on this: an empty Params answers
     * json_object(), i.e. {}, never a JSON null and never an array. */
    Params p;
    json_t *j = paramsToJansson(p);
    ASSERT_TRUE(j != nullptr);
    EXPECT_TRUE(json_is_object(j));
    EXPECT_FALSE(json_is_null(j));
    EXPECT_FALSE(json_is_array(j));
    EXPECT_EQ(0u, json_object_size(j));
    json_decref(j);
}

TEST(ParamsJson, ToJansson_EmptyStringValueIsEmittedNotOmitted)
{
    /* "absent key" and "empty string" are two different answers in this API
     * (get_param on an empty param, schedule:"false"). Emitting nothing for an
     * empty value would silently turn one into the other. */
    Params p;
    p.Add("k_empty", "");
    p.Add("k_alpha", "v_alpha");
    json_t *j = paramsToJansson(p);
    ASSERT_TRUE(j != nullptr);
    ASSERT_EQ(2u, json_object_size(j)) << "the empty-valued pair was dropped";
    json_t *v = json_object_get(j, "k_empty");
    ASSERT_TRUE(v != nullptr);
    EXPECT_TRUE(json_is_string(v));
    EXPECT_STREQ("", json_string_value(v));
    json_decref(j);
}

TEST(ParamsJson, ToJansson_ValidNonAsciiSurvivesAndDumpsAsAsciiEscapes)
{
    /* A legal accented value survives the round trip into the jansson tree,
     * and jansson_to_string() dumps it with JSON_ENSURE_ASCII, so it leaves as
     * \uXXXX escapes and never as raw UTF-8 bytes. That is what the external
     * processes (Wago, KNX, Lua) read off the wire today, and none of them is
     * covered by the golden suite. */
    Params p;
    p.Add("k_accent", "\xc3\xa9\xc3\xa0\xc3\xbc");
    json_t *j = paramsToJansson(p);
    ASSERT_TRUE(j != nullptr);
    EXPECT_EQ("\xc3\xa9\xc3\xa0\xc3\xbc", janssonValueOf(j, "k_accent"));

    //jansson_to_string() steals the reference, no decref here
    std::string dumped = jansson_to_string(j);
    EXPECT_NE(std::string::npos, dumped.find("\\u00E9"))
            << "JSON_ENSURE_ASCII no longer escapes non-ASCII as jansson does: "
            << dumped;
    EXPECT_EQ(std::string::npos, dumped.find("\xc3\xa9"))
            << "raw UTF-8 bytes reached the wire: " << dumped;
}

TEST(ParamsJson, Tripwire_TheThreeWireEscapingsAreThreeDifferentBytestreams)
{
    /* TRIPWIRE for the sub-ticket that migrates the EMITTERS.
     *
     * Escaping is where this migration changes bytes without changing meaning,
     * and "semantically identical" is exactly what a golden suite is built to
     * ignore. The API emitters (JsonApiHandlerWS.cpp:72,
     * JsonApiHandlerHttp.cpp:223, EventManager.cpp:80) are covered by the
     * goldens, which compare parsed documents and will therefore stay GREEN
     * through the change. The driver wires (WagoMap, KNXCtrl, ScriptExec,
     * ScriptBindings, ScriptExtern_main) have no net at all.
     *
     * So the three forms are pinned SEPARATELY, on the RAW dumped string with
     * no case normalisation anywhere - normalising here would make the case
     * pass for a migration that changed the wire, which is the one thing it
     * exists to prevent. Whichever form the next sub-ticket produces, exactly
     * one of these three blocks tells it what it produced.
     *
     * All values below are measured, not assumed. */
    Params p;
    p.Add("k_accent", "\xc3\xa9");                      //é, U+00E9
    p.Add("k_ctrl", std::string("a\x1f") + "b\x01" + "c"); //U+001F then U+0001

    //--- form 1: what ships TODAY. jansson + JSON_ENSURE_ASCII, hex UPPERCASE.
    json_t *j = paramsToJansson(p);
    ASSERT_TRUE(j != nullptr);
    const std::string jansson_wire = jansson_to_string(j); //steals the ref
    EXPECT_NE(std::string::npos, jansson_wire.find("\\u00E9"))
            << "form 1 changed: " << jansson_wire;
    EXPECT_EQ(std::string::npos, jansson_wire.find("\\u00e9"))
            << "jansson started lowercasing its escapes: " << jansson_wire;
    EXPECT_EQ(std::string::npos, jansson_wire.find("\xc3\xa9"))
            << "jansson stopped escaping non-ASCII: " << jansson_wire;

    Json jn = p.toNJson();

    //--- form 2: nlohmann dump() bare. RAW UTF-8, no escape at all. This is
    //what a straight port produces, and it differs from form 1 on every
    //non-ASCII byte of every driver wire.
    const std::string nlohmann_bare = jn.dump();
    EXPECT_NE(std::string::npos, nlohmann_bare.find("\xc3\xa9"))
            << "form 2 changed: " << nlohmann_bare;
    EXPECT_EQ(std::string::npos, nlohmann_bare.find("\\u00E9"))
            << "nlohmann started escaping non-ASCII: " << nlohmann_bare;
    EXPECT_EQ(std::string::npos, nlohmann_bare.find("\\u00e9"))
            << "nlohmann started escaping non-ASCII: " << nlohmann_bare;

    //--- form 3: nlohmann dump(ensure_ascii = true). The closest port to
    //form 1 - and STILL not byte identical to it, because the hex is
    //LOWERCASE. This is the case a case-insensitive assertion would let
    //through while the wire really had changed.
    const std::string nlohmann_ascii = jn.dump(-1, ' ', true);
    EXPECT_NE(std::string::npos, nlohmann_ascii.find("\\u00e9"))
            << "form 3 changed: " << nlohmann_ascii;
    EXPECT_EQ(std::string::npos, nlohmann_ascii.find("\\u00E9"))
            << "form 3 became byte identical to jansson - the wire risk this "
               "tripwire guards is gone, say so explicitly: " << nlohmann_ascii;
    EXPECT_EQ(std::string::npos, nlohmann_ascii.find("\xc3\xa9"))
            << "form 3 leaked raw bytes: " << nlohmann_ascii;

    //--- and the three really are three: no two of them are the same string.
    EXPECT_NE(jansson_wire, nlohmann_bare);
    EXPECT_NE(jansson_wire, nlohmann_ascii);
    EXPECT_NE(nlohmann_bare, nlohmann_ascii);

    /* Control characters: NOT uniformly identical across the two libraries,
     * contrary to what is easy to assume. Measured: U+001F is "\u001F" under
     * jansson and "\u001f" under nlohmann - the case difference again, because
     * the hex digits contain a LETTER. U+0001 is "\u0001" on both sides only
     * because its digits contain none. A control-character check that used
     * U+0001 alone would therefore see no difference and prove nothing. */
    EXPECT_NE(std::string::npos, jansson_wire.find("\\u001F"));
    EXPECT_NE(std::string::npos, nlohmann_bare.find("\\u001f"));
    EXPECT_EQ(std::string::npos, jansson_wire.find("\\u001f"));
    EXPECT_EQ(std::string::npos, nlohmann_bare.find("\\u001F"));
    EXPECT_NE(std::string::npos, jansson_wire.find("\\u0001"));
    EXPECT_NE(std::string::npos, nlohmann_bare.find("\\u0001"));
}

TEST(ParamsJson, ToJansson_InvalidUtf8ValueIsSilentlyDroppedAndTheRestSurvives)
{
    /* THE trap of the migration, seen from Params (E4.0.md:355).
     *
     * jansson refuses the bytes AT CONSTRUCTION: json_string() answers NULL,
     * json_object_set_new() answers -1, and neither return code is tested. The
     * pair simply disappears, the object stays well formed and json_dumps()
     * SUCCEEDS - which is how the API answers 200 with a truncated payload.
     *
     * The good pairs around it are asserted too: "dropped the bad one" must
     * not be confused with "dropped everything". */
    Params p;
    p.Add("k_alpha", "v_alpha");
    p.Add("k_bad", std::string(INVALID_UTF8_BYTES));
    p.Add("k_zulu", "v_zulu");

    json_t *j = paramsToJansson(p);
    ASSERT_TRUE(j != nullptr);
    EXPECT_EQ(2u, json_object_size(j))
            << "the invalid-UTF-8 pair is no longer silently dropped";
    EXPECT_TRUE(json_object_get(j, "k_bad") == nullptr);
    EXPECT_EQ("v_alpha", janssonValueOf(j, "k_alpha"));
    EXPECT_EQ("v_zulu", janssonValueOf(j, "k_zulu"));

    char *dumped = json_dumps(j, JSON_COMPACT | JSON_ENSURE_ASCII);
    EXPECT_TRUE(dumped != nullptr)
            << "json_dumps now fails on a truncated document - the dead 500 "
               "branch of JsonApiHandlerHttp is reachable again";
    free(dumped);
    json_decref(j);
}

TEST(ParamsJson, ToJansson_InvalidUtf8KeyIsSilentlyDroppedAndTheRestSurvives)
{
    /* Same drop, through the key. This is the reachable channel:
     * HfURISyntax::getQuery() percent-DECODES before HttpClient splits, so
     * ?param=%ff%80x puts arbitrary bytes in a client-supplied param NAME,
     * and buildJsonGetParam() makes that name a KEY. */
    Params p;
    p.Add(std::string(INVALID_UTF8_BYTES), "v_bad");
    p.Add("k_alpha", "v_alpha");

    json_t *j = paramsToJansson(p);
    ASSERT_TRUE(j != nullptr);
    EXPECT_EQ(1u, json_object_size(j))
            << "the invalid-UTF-8 key is no longer silently dropped";
    EXPECT_EQ("v_alpha", janssonValueOf(j, "k_alpha"));
    json_decref(j);
}

/*******************************************************************************
 * The Params container itself - what the adapter iterates
 ******************************************************************************/

TEST(ParamsJson, Params_IterationIsAlphabeticalNotInsertionOrder)
{
    /* Params is a std::map. The serializer walks it, so this ordering is what
     * ends up in the emitted document; the fixture is inserted in
     * reverse-alphabetical order precisely so that this case can tell the two
     * apart. */
    Params p = richFixture();
    ASSERT_EQ(8, p.size());

    std::vector<std::string> keys;
    for (int i = 0; i < p.size(); i++)
    {
        std::string k, v;
        p.get_item(i, k, v);
        keys.push_back(k);
    }

    const std::vector<std::string> expected = {
        "k_accent", "k_alpha", "k_bool_like", "k_empty",
        "k_float_like", "k_int_like", "k_null_like", "k_zulu"
    };
    EXPECT_EQ(expected, keys);
    //the fixture was inserted zulu-first: this proves the case discriminates
    EXPECT_NE("k_zulu", keys.front());
}

/*******************************************************************************
 * The nlohmann face - unchanged by E4.1a, and the reason it must be pinned
 ******************************************************************************/

TEST(ParamsJson, ToNJson_IsAnObjectOfStringsWithTheSameMapping)
{
    Params p = richFixture();
    Json j = p.toNJson();
    ASSERT_TRUE(j.is_object());
    EXPECT_EQ(8u, j.size());

    for (Json::const_iterator it = j.begin(); it != j.end(); ++it)
    {
        EXPECT_TRUE(it.value().is_string())
                << "key '" << it.key() << "' is no longer a JSON string";
        EXPECT_FALSE(it.value().is_number())
                << "key '" << it.key() << "' became a JSON number";
    }

    EXPECT_EQ("v_alpha", j.value("k_alpha", std::string()));
    EXPECT_EQ("v_zulu", j.value("k_zulu", std::string()));
    EXPECT_EQ("3", j.value("k_int_like", std::string()));
    EXPECT_EQ("1234.56789", j.value("k_float_like", std::string()));
    EXPECT_EQ("true", j.value("k_bool_like", std::string()));
    EXPECT_EQ("null", j.value("k_null_like", std::string()));
    EXPECT_EQ("", j.value("k_empty", std::string()));
    EXPECT_EQ("\xc3\xa9\xc3\xa0\xc3\xbc", j.value("k_accent", std::string()));
    EXPECT_FALSE(j.contains("v_alpha"));
    EXPECT_FALSE(j.contains("v_zulu"));
}

TEST(ParamsJson, ToNJson_EmptyParamsIsAnEmptyObjectNotNull)
{
    Params p;
    Json j = p.toNJson();
    EXPECT_TRUE(j.is_object()) << "an empty Params no longer answers {}";
    EXPECT_FALSE(j.is_null());
    EXPECT_TRUE(j.empty());
}

TEST(ParamsJson, FromNJson_RoundTripsToNJson)
{
    Params src = richFixture();
    Params back = Params::fromNJson(src.toNJson());

    ASSERT_EQ(src.size(), back.size());
    for (int i = 0; i < src.size(); i++)
    {
        std::string ks, vs, kb, vb;
        src.get_item(i, ks, vs);
        back.get_item(i, kb, vb);
        EXPECT_EQ(ks, kb);
        EXPECT_EQ(vs, vb) << "value of key '" << ks << "' did not survive";
    }
}

TEST(ParamsJson, FromNJson_ThrowsTypeError302OnANonStringValue)
{
    /* Config::readStateCache() (CalaosConfig.cpp:504-529) wraps the whole
     * deserialization in a try/catch precisely because of this: a cache that
     * parses as JSON but carries a non-string state value throws instead of
     * silently coercing. Pin the id, not just the class - it is the number the
     * rest of the series will grep for. */
    Json j = Json{{ "k_alpha", 3 }};
    bool threw = false;
    try
    {
        Params::fromNJson(j);
    }
    catch (const nlohmann::json::type_error &e)
    {
        threw = true;
        EXPECT_EQ(302, e.id) << "fromNJson threw, but not type_error.302: "
                             << e.what();
    }
    EXPECT_TRUE(threw)
            << "fromNJson now coerces a non-string value - the guard of "
               "Config::readStateCache() has lost its reason to exist";
}

TEST(ParamsJson, Utf8_JanssonDropsWhereNlohmannKeeps)
{
    /* The two faces of the SAME Params, side by side. This is the divergence
     * the whole migration has to answer for, stated on the bridge itself:
     * jansson loses the pair, nlohmann keeps it and defers the problem to
     * dump(). Whoever finishes the migration must make this case change on
     * purpose, not by accident. */
    Params p;
    p.Add("k_alpha", "v_alpha");
    p.Add("k_bad", std::string(INVALID_UTF8_BYTES));

    json_t *jj = paramsToJansson(p);
    ASSERT_TRUE(jj != nullptr);
    EXPECT_EQ(1u, json_object_size(jj)) << "jansson stopped dropping";
    json_decref(jj);

    Json jn = p.toNJson();
    EXPECT_EQ(2u, jn.size()) << "nlohmann stopped keeping";
    EXPECT_EQ(std::string(INVALID_UTF8_BYTES), jn.value("k_bad", std::string()));
}

TEST(ParamsJson, Utf8_NlohmannDumpThrows316AndTheReplaceHandlerYieldsFffd)
{
    /* The user decision of 2026-08-17, pinned on Params: invalid UTF-8 becomes
     * U+FFFD, through error_handler_t::replace on the dump - NOT through a
     * try/catch, which would treat the symptom and still lose the payload.
     *
     * Both halves matter: that the bare dump() really does throw (otherwise
     * the handler guards nothing) and that the handler really does replace. */
    Params p;
    p.Add("k_alpha", "v_alpha");
    p.Add("k_bad", std::string(INVALID_UTF8_BYTES));
    Json j = p.toNJson();

    bool threw = false;
    try
    {
        j.dump();
    }
    catch (const nlohmann::json::type_error &e)
    {
        threw = true;
        EXPECT_EQ(316, e.id) << "dump() threw, but not type_error.316: "
                             << e.what();
    }
    EXPECT_TRUE(threw)
            << "a bare dump() of client-influenced data no longer throws - the "
               "error handler this ticket installs would be pointless";

    const std::string safe = j.dump(-1, ' ', false,
                                    Json::error_handler_t::replace);
    //U+FFFD is EF BF BD in UTF-8
    EXPECT_NE(std::string::npos, safe.find("\xef\xbf\xbd"))
            << "error_handler_t::replace no longer produces U+FFFD: " << safe;
    EXPECT_EQ(std::string::npos, safe.find("\xff\x80"))
            << "the invalid bytes reached the wire: " << safe;
    //and the rest of the document is intact - replace must not truncate
    EXPECT_NE(std::string::npos, safe.find("v_alpha"))
            << "the replace handler lost the valid pairs: " << safe;
}

/*******************************************************************************
 * E4.1l - jansson_from_json(), THE OTHER transitional adapter of this header.
 *
 * It goes the other way round from jansson_from_params(): a Json in, a json_t *
 * out, by dumping and re-parsing. It exists for ONE call site,
 * LuaScript/ScriptExec.cpp, where the {msg:"event", data:<event>} message sent
 * to calaos_script is still assembled with jansson because the sibling message
 * of the same function carries JsonApi::buildFlatIOList(), still a json_t *.
 * E4.1m migrates that file and deletes this adapter with it.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS EXERCISED HERE AND WHAT IS NOT - said plainly, because "linked" is
 * not "exercised" (FINDINGS.md, F-LINK-1)
 * ---------------------------------------------------------------------------
 * The ADAPTER is exercised, directly, by the cases below. Its CALL SITE is
 * not: ScriptExec.cpp:190 sits inside a lambda connected to
 * EventManager::newEvent only after a real calaos_script process has been
 * spawned through uvw, which no in-process test can do. ScriptExec.o IS linked
 * into every core test binary (CORE_SERVER_OBJECTS), and that proves nothing
 * about execution. The substitution made there is one token wide and guarded
 * by the compiler alone; this is stated, not glossed over.
 *
 * ---------------------------------------------------------------------------
 * ONE OF THE THREE EMISSION INVARIANTS IS INERT ON THIS dump(), MEASURED
 * ---------------------------------------------------------------------------
 * The adapter dumps with the epic's full form -
 * dump(-1, ' ', true, error_handler_t::replace) - but the result is IMMEDIATELY
 * re-parsed by json_loads(), and a parser is blind to escaping. So:
 *   - error_handler_t::replace IS observable (a bare dump() throws
 *     type_error.316 and there would be no document at all; `ignore` drops the
 *     bytes instead of replacing them). Two cases below.
 *   - the SORTED key order IS observable, because jansson preserves the order
 *     it parsed. One case below. That is invariant 1 - nlohmann::json standard,
 *     never ordered_json - seen through the adapter.
 *   - ensure_ascii is NOT observable here, and that is structural, not a hole
 *     in the fixture: json_loads() decodes \u00e9 and the raw UTF-8 bytes of
 *     U+00E9 to the same jansson string, and jansson_to_string() re-escapes in
 *     its own UPPERCASE form on the way out whatever went in. The counter
 *     mutation was RUN, not assumed: flipping it true -> false leaves this file
 *     green, and the reason is written here so nobody reads that green as
 *     coverage. It is kept for uniformity with every other dump() of the epic.
 ******************************************************************************/

namespace
{

/* A deliberately RICH document for the adapter, same discipline as
 * richFixture() above:
 *   - members inserted in an order that is NOT alphabetical, and whose
 *     alphabetical order differs from it on every adjacent pair;
 *   - no two values equal, drawn from disjoint vocabularies;
 *   - one NESTED object, because the ScriptExec message nests the event under
 *     "data" and a flat document could not tell a nested walk from a flat one;
 *   - non-ASCII in one member only, so a case cannot confuse "escapes
 *     everything" with "escapes the first thing it meets".
 */
Json richAdapterDocument()
{
    Json j;
    j["z_last"] = "v_zulu";
    j["m_middle"] = "v_mike";
    j["a_first"] = "v_alpha";
    j["n_accent"] = "caf\xc3\xa9";
    j["d_nested"] = Json{{ "inner_z", "v_inner_zulu" },
                         { "inner_a", "v_inner_alpha" }};
    return j;
}

} //namespace

TEST(ParamsJson, JsonAdapter_CarriesEveryMemberIncludingTheNestedOneAndTheAccent)
{
    json_t *j = jansson_from_json(richAdapterDocument());
    ASSERT_TRUE(j != nullptr) << "the adapter answered NULL";
    ASSERT_TRUE(json_is_object(j));
    EXPECT_EQ(5u, json_object_size(j));

    EXPECT_EQ("v_zulu", janssonValueOf(j, "z_last"));
    EXPECT_EQ("v_mike", janssonValueOf(j, "m_middle"));
    EXPECT_EQ("v_alpha", janssonValueOf(j, "a_first"));
    //The accent survives as its UTF-8 bytes, whatever escaping crossed over.
    EXPECT_EQ("caf\xc3\xa9", janssonValueOf(j, "n_accent"));

    json_t *nested = json_object_get(j, "d_nested");
    ASSERT_TRUE(nested != nullptr);
    ASSERT_TRUE(json_is_object(nested));
    EXPECT_EQ(2u, json_object_size(nested));
    EXPECT_EQ("v_inner_zulu", janssonValueOf(nested, "inner_z"));
    EXPECT_EQ("v_inner_alpha", janssonValueOf(nested, "inner_a"));

    json_decref(j);
}

TEST(ParamsJson, JsonAdapter_HandsJanssonTheKeysAlreadySorted)
{
    /* INVARIANT 1 of the epic, seen through the adapter: nlohmann::json is a
     * std::map and dumps SORTED, jansson preserves the order it parses, so the
     * document that reaches ScriptExec's message is alphabetical - not the
     * insertion order of richAdapterDocument(), which is deliberately not
     * alphabetical on any adjacent pair. If ordered_json were ever substituted
     * for Json, this case is what says so. */
    json_t *j = jansson_from_json(richAdapterDocument());
    ASSERT_TRUE(j != nullptr);

    std::vector<std::string> keys;
    const char *key;
    json_t *value;
    json_object_foreach(j, key, value)
        keys.push_back(key);

    ASSERT_EQ(5u, keys.size());
    const std::vector<std::string> expected = {
        "a_first", "d_nested", "m_middle", "n_accent", "z_last" };
    EXPECT_EQ(expected, keys)
            << "the adapter no longer hands jansson a sorted document";

    json_decref(j);
}

TEST(ParamsJson, JsonAdapter_InvalidUtf8BecomesFffdInsteadOfKillingTheMessage)
{
    /* INVARIANT 3, and the reason it is not negotiable on this dump: a Lua
     * script puts a raw 0xFF into a JSON string in one line (E4.1j measured
     * the four spellings that pass the lua_isstring guard), and a bare dump()
     * would throw type_error.316 from inside an ExternProc callback where
     * nothing catches it - std::terminate, not a lost message.
     *
     * The two halves are asserted separately, so `strict` and `ignore` do not
     * redden the same assertion:
     *   - the adapter ANSWERS (strict would have thrown out of it),
     *   - and the bytes were REPLACED, not dropped (that is `ignore`). */
    Json j;
    j["good"] = "v_alpha";
    j["bad"] = std::string("head") + INVALID_UTF8_BYTES + "tail";

    json_t *out = nullptr;
    ASSERT_NO_THROW(out = jansson_from_json(j))
            << "the adapter threw - a bare dump() or a strict handler is back";
    ASSERT_TRUE(out != nullptr)
            << "the adapter answered NULL: json_loads() refused what dump() wrote";

    //U+FFFD is EF BF BD once json_loads() has decoded it back.
    const std::string bad = janssonValueOf(out, "bad");
    EXPECT_NE(std::string::npos, bad.find("\xef\xbf\xbd"))
            << "the invalid bytes did not become U+FFFD: " << bad;
    EXPECT_EQ(std::string::npos, bad.find("\xff\x80"))
            << "the invalid bytes crossed the adapter untouched: " << bad;
    //Not a truncation: both ends of the value and the sibling pair survive.
    EXPECT_EQ(0u, bad.find("head")) << bad;
    EXPECT_NE(std::string::npos, bad.find("tail")) << bad;
    EXPECT_EQ("v_alpha", janssonValueOf(out, "good"));

    json_decref(out);
}

TEST(ParamsJson, JsonAdapter_TheScriptExecEventMessageIsAssembledAndSerializable)
{
    /* A REPLICA of LuaScript/ScriptExec.cpp:187-191, not the production call:
     * that lambda only runs once a real calaos_script has been spawned through
     * uvw. What this case proves is that the adapter composes with the jansson
     * assembly around it - the returned reference is owned and consumable by
     * json_object_set_new(), the envelope serializes, and the event object
     * inside it is the sorted one nlohmann produced.
     *
     * The envelope itself is still jansson, so it still travels in INSERTION
     * order (msg, then data) and still escapes in jansson's UPPERCASE form:
     * that is the whole point of routing this site through the adapter for one
     * ticket instead of migrating the file. The only thing that moves on the
     * calaos_script wire is the key order INSIDE the event object. */
    Json event = {
        { "event_raw", "io_changed id:io_x state:caf\xc3\xa9" },
        { "type", "3" },
        { "type_str", "io_changed" },
        { "data", Json{{ "id", "io_x" }, { "state", "caf\xc3\xa9" }}}
    };

    json_t *jev = json_object();
    json_object_set_new(jev, "msg", json_string("event"));
    json_object_set_new(jev, "data", jansson_from_json(event));

    //jansson_to_string() decrefs jev, exactly as ScriptExec.cpp relies on.
    const std::string wire = jansson_to_string(jev);
    ASSERT_FALSE(wire.empty());

    //The envelope: jansson insertion order, msg before data.
    const size_t msgPos = wire.find("\"msg\":");
    const size_t dataPos = wire.find("\"data\":");
    ASSERT_NE(std::string::npos, msgPos) << wire;
    ASSERT_NE(std::string::npos, dataPos) << wire;
    EXPECT_LT(msgPos, dataPos)
            << "the envelope is no longer jansson's: " << wire;
    EXPECT_NE(std::string::npos, wire.find("\"msg\":\"event\"")) << wire;

    //The event object: sorted, because it crossed nlohmann.
    const size_t inner = dataPos + 1;
    const size_t evData = wire.find("\"data\":", inner);
    const size_t evRaw  = wire.find("\"event_raw\":", inner);
    const size_t evType = wire.find("\"type\":", inner);
    const size_t evStr  = wire.find("\"type_str\":", inner);
    ASSERT_NE(std::string::npos, evData) << wire;
    ASSERT_NE(std::string::npos, evRaw) << wire;
    ASSERT_NE(std::string::npos, evType) << wire;
    ASSERT_NE(std::string::npos, evStr) << wire;
    EXPECT_LT(evData, evRaw) << wire;
    EXPECT_LT(evRaw, evType) << wire;
    EXPECT_LT(evType, evStr) << wire;

    //And the escaping on THIS wire is still jansson's, UPPERCASE: the adapter
    //buys byte stability here, which is why it exists for one ticket.
    EXPECT_NE(std::string::npos, wire.find("\\u00E9"))
            << "the calaos_script wire changed its escaping: " << wire;
    EXPECT_EQ(std::string::npos, wire.find("\\u00e9"))
            << "lowercase hex on a wire this ticket does not migrate: " << wire;

    //Every leaf is still a JSON string: "type" is the stringified enum.
    EXPECT_NE(std::string::npos, wire.find("\"type\":\"3\"")) << wire;
    EXPECT_EQ(std::string::npos, wire.find("\"type\":3")) << wire;
}
