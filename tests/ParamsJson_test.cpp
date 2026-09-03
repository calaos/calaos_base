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
 * The JSON face of Params, and the byte shape of the wire it feeds.
 *
 * Almost every payload of the JSON API crosses Params, so what this class
 * serializes to is a contract for the whole API. Two things are pinned:
 * the mapping itself (an object of STRINGS - a value that became a JSON number
 * would break the type-strict oracle of the golden suite) and the escaping of
 * the dump, which no golden can see because the goldens compare parsed
 * documents.
 *
 * Deliberately NOT asserted: the order of the keys inside a serialized
 * document. The user decision of 2026-08-17 assumes sorted keys
 * (nlohmann::json, not ordered_json) and E4.0's oracle contract compares
 * documents, never strings. The ordering that IS pinned here is the one that
 * IS semantic for C++ callers: Params is a std::map, so its iteration is
 * alphabetical, not insertion-ordered.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Utils.h"
#include "Params.h"

namespace
{

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

} //namespace

TEST(ParamsJson, Tripwire_TheWireIsFormThreeAndNotABareDump)
{
    /* TRIPWIRE ON THE BYTE SHAPE OF THE WIRE.
     *
     * Escaping is the one dimension the golden suite cannot see: the 145
     * goldens compare PARSED DOCUMENTS, so a dump() that changed every escape
     * would leave them all green. This case reads the RAW dumped string, with
     * no case normalisation anywhere - normalising here would let through
     * exactly the change it exists to catch.
     *
     * THE FORM THE API SHIPS, and the expression it ships it with:
     *
     *     dump(-1, SPACE, ensure_ascii = true, Json::error_handler_t::replace)
     *
     * Pure ASCII, LOWERCASE hexadecimal. It is spelled out here rather than
     * called, because this file links only libcalaos_common; the wire half,
     * which reads a byte a handler really put on a socket, is
     * Tripwire_TheHttpApiWireIsFormThreeAndNeitherOfTheOtherTwo in
     * tests/core/JsonApiDispatchWireBytes_test.cpp. Neither one alone is the
     * whole net on this dimension.
     *
     * A BARE dump() IS THE FAILURE MODE, not a variant: it emits raw UTF-8 and
     * every assertion below is written so that swapping the shipped expression
     * for one turns this case red. An UPPERCASE escape is asserted ABSENT for
     * the same reason, one library ago it was what the wire carried.
     *
     * All values below are measured, not assumed. */
    Params p;
    p.Add("k_accent", "\xc3\xa9");                      //e acute, U+00E9
    p.Add("k_ctrl", std::string("a\x1f") + "b\x01" + "c"); //U+001F then U+0001

    Json jn = p.toNJson();

    //The bare dump: raw UTF-8, no escape at all. Kept as the CONTRAST, so that
    //"the wire moved" cannot be confused with "the wire moved to the right
    //place".
    const std::string bare = jn.dump();
    EXPECT_NE(std::string::npos, bare.find("\xc3\xa9"))
            << "a bare dump() no longer emits raw UTF-8, this contrast is dead: "
            << bare;

    const std::string shipped = jn.dump(-1, ' ', true, Json::error_handler_t::replace);

    EXPECT_NE(shipped, bare)
            << "the API emission expression became a bare dump() - raw UTF-8 on "
               "a wire that has always been ASCII only";
    EXPECT_EQ(jn.dump(-1, ' ', true), shipped)
            << "the API emission expression is no longer ensure_ascii: " << shipped;

    EXPECT_NE(std::string::npos, shipped.find("\\u00e9"))
            << "the wire stopped escaping non-ASCII in lowercase hexadecimal: "
            << shipped;
    EXPECT_EQ(std::string::npos, shipped.find("\\u00E9"))
            << "the wire went back to an UPPERCASE hexadecimal escape: " << shipped;
    EXPECT_EQ(std::string::npos, shipped.find("\xc3\xa9"))
            << "raw UTF-8 bytes reached the wire: " << shipped;

    /* Control characters diverge on the SAME axis, and only when the hexadecimal
     * contains a letter: U+001F is \u001f here and U+0001 is \u0001 whatever
     * the case convention. A control-character check written on U+0001 alone
     * would therefore see nothing and prove nothing. */
    EXPECT_NE(std::string::npos, shipped.find("\\u001f"));
    EXPECT_EQ(std::string::npos, shipped.find("\\u001F"));
    EXPECT_NE(std::string::npos, shipped.find("\\u0001"));
}

/*******************************************************************************
 * The Params container itself - what the serializer walks
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
 * The serialized face of Params
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

TEST(ParamsJson, Utf8_AnInvalidUtf8ValueIsKeptInTheTreeNotDropped)
{
    /* The pair survives into the tree and the problem is deferred to dump(),
     * where the error handler of the case below deals with it. Losing the pair
     * here instead would answer a truncated payload with a 200. */
    Params p;
    p.Add("k_alpha", "v_alpha");
    p.Add("k_bad", std::string(INVALID_UTF8_BYTES));

    Json jn = p.toNJson();
    EXPECT_EQ(2u, jn.size()) << "the invalid-UTF-8 pair was dropped";
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
 * The {msg:"event", data:<event>} message LuaScript/ScriptExec.cpp sends to
 * calaos_script used to be assembled here, through a second adapter, and had
 * four cases of its own. It is assembled and dumped in one library now, and
 * tests/ScriptWire_test.cpp covers every shape that call site uses -
 * dumpJson(), parseMessage(), stringGet(), decodeObject(). The assembly in
 * between is guarded by the compiler alone.
 ******************************************************************************/
