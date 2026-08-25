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
 * E4.1h - CHARACTERIZATION of the Wago wire.
 *
 * Two programs, one protocol, both ends in this repository and in this commit:
 *
 *   calaos_server  IO/Wago/WagoMap.cpp             emits 8 REQUESTS,
 *                                                  decodes the REPLIES
 *   calaos_wago    IO/Wago/WagoExternProc_main.cpp decodes the REQUESTS,
 *                                                  emits the REPLIES
 *
 * They are built by the same Makefile.am, installed by the same package and
 * updated together; IO/Wago/WagoCtrl.cpp - the modbus layer under them -
 * contains no JSON at all. There is NO third party on this wire, so the SHAPE
 * of the bytes has nobody to protect. What has to be proven is the STRUCTURE,
 * the VALUES and their TYPES - and nothing held any of that before this file:
 * E4.0d measured that no test of this suite ever executes a driver, and
 * tests/WagoConfigParse_test.cpp only covers the io.xml address parser.
 *
 * THE SEAM is the free functions of the anonymous namespace below. In the
 * characterization commit they carry the jansson bodies of the two production
 * files VERBATIM; the migration commit rewires them onto the shipped header
 * IO/Wago/WagoWire.h, so that from then on a mutation of the SHIPPED emitter
 * turns this suite RED. A test that re-implements the assembly instead of
 * calling it only freezes what the TEST does - measured on a sibling ticket of
 * this series, where 34/34 stayed green while the production dump() was put
 * back naked. Exactly three assertions of this file move when the seam is
 * rewired, each flagged in place with MOVED BY E4.1h.
 *
 * WHY WagoMap CANNOT BE CALLED DIRECTLY: its constructor binds a UDP socket,
 * starts two timers and SPAWNS calaos_wago; WagoExternProc_main.cpp defines
 * main(). Neither can be reached from a test, which is why the wire lives in
 * a header of free functions - same shape as IO/Mqtt/MqttWire.h and
 * IO/Reolink/ReolinkWire.h next door.
 *
 * WHAT IS ASSERTED ON BYTES, and why it has to be:
 *   The 145 goldens of the series compare PARSED DOCUMENTS. No golden and no
 *   other test can see an escaping change - measured twice in this series:
 *   flipping ensure_ascii true->false left whole suites green. Two cases below
 *   therefore look at the RAW BYTES:
 *     - TheWireStaysPureAsciiWhenAnEchoedFieldIsNot : RED if ensure_ascii is
 *       ever dropped from the production dump().
 *     - InvalidUtf8InAnEchoedFieldDoesNotAbortTheEmission : RED if
 *       error_handler_t::replace is ever dropped, and RED DIFFERENTLY for each
 *       of the three spellings - replace writes � per bad byte, ignore
 *       makes the bytes DISAPPEAR, strict THROWS.
 *   Eight more cases freeze the exact byte string of the eight requests. Those
 *   eight do NOT move across the migration and that is the point: Params is a
 *   std::map, so jansson_from_params() already walked it ALPHABETICALLY, which
 *   is exactly what nlohmann::json does. The request wire is byte-identical
 *   before and after. Only the REPLIES, assembled key by key in insertion
 *   order by calaos_wago, get re-sorted.
 *
 * WHERE THE BYTES OF THIS WIRE COME FROM - measured, because the answer is not
 * the same as on the KNX wire, where raw bus bytes reached a naked dump() and
 * a dimmer at 78% was enough to terminate the driver. On the Wago wire:
 *   - "action" is one of eight string literals;
 *   - "id" is Utils::createRandomUuid();
 *   - "address"/"count"/"value" are Utils::to_string() of an integer;
 *   - "values" entries are "true"/"false" or Utils::to_string(UWord), i.e.
 *     decimal digits.
 *   NOT ONE modbus byte ever reaches a JSON string: WagoCtrl hands back
 *   vector<bool> and vector<UWord>, never a buffer. calaos_wago does ECHO
 *   id/action/address/count from the request it received, so a corrupted pipe
 *   frame is the only conceivable source of a non-ASCII byte here. The two
 *   byte oracles below therefore guard the FUNCTION, not a reachable
 *   production input - and that is said out loud rather than implied.
 *
 * A DELIBERATELY RICH FIXTURE. The recurring defect of the E4.0/E4.1 series is
 * the "poor fixture": a dataset too uniform for a swap of two interchangeable
 * fields to show. Here address (4242) and count (7) differ, are not substrings
 * of one another, and differ again in the reply (count 5), and the five word
 * values are FIVE DIFFERENT NUMBERS OF THREE DIFFERENT LENGTHS - never
 * [0,0,0], so exchanging any two of them is visible.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "Utils.h"
#include "Params.h"
#include "Jansson_Addition.h"

using std::string;
using std::vector;
using Utils::UWord;

namespace
{

/*---------------------------------------------------------------------------
 * THE SEAM - requests emitted by WagoMap (calaos_server -> calaos_wago).
 *
 * Bodies copied VERBATIM from WagoMap.cpp:265-411. The migration commit
 * replaces each body with a one line call into IO/Wago/WagoWire.h.
 *-------------------------------------------------------------------------*/

string wireReadBits(const string &id, UWord address, int count)
{
    Params p = {{ "action", "read_bits" },
                { "id", id },
                { "address", Utils::to_string(address) },
                { "count", Utils::to_string(count) } };

    return jansson_to_string(jansson_from_params(p));
}

string wireReadOutputBits(const string &id, UWord address, int count)
{
    Params p = {{ "action", "read_output_bits" },
                { "id", id },
                { "address", Utils::to_string(address) },
                { "count", Utils::to_string(count) } };

    return jansson_to_string(jansson_from_params(p));
}

string wireWriteBit(const string &id, UWord address, bool value)
{
    Params p = {{ "action", "write_bit" },
                { "id", id },
                { "address", Utils::to_string(address) },
                { "value", value?"true":"false" } };

    return jansson_to_string(jansson_from_params(p));
}

/* WagoMap.cpp:316-337, INCLUDING ITS BUG. The values array is built into jret
 * and jret is then thrown away: what goes on the wire is a SECOND, FRESH
 * serialization of p, which has no "values" key. See the two cases named
 * ...CarriesNoValuesArray_BUG below.
 * The one difference with production, and it is deliberate: the decref. The
 * production site LEAKS jret (and the array it now owns) on every call;
 * reproducing a leak inside the test suite would only make the suite noisy
 * under ASan. What this seam reproduces is the EMITTED BYTES. */
string wireWriteBits(const string &id, UWord address, int count, const vector<bool> &values)
{
    Params p = {{ "action", "write_bits" },
                { "id", id },
                { "address", Utils::to_string(address) },
                { "count", Utils::to_string(count) } };

    json_t *jret = jansson_from_params(p);
    json_t *jarr = json_array();
    for (size_t i = 0;i < values.size();i++)
        json_array_append_new(jarr, json_string(values[i]?"true":"false"));
    json_object_set_new(jret, "values", jarr);

    const string wire = jansson_to_string(jansson_from_params(p));

    json_decref(jret); //production leaks it here, see the comment above
    return wire;
}

string wireReadWords(const string &id, UWord address, int count)
{
    Params p = {{ "action", "read_words" },
                { "id", id },
                { "address", Utils::to_string(address) },
                { "count", Utils::to_string(count) } };

    return jansson_to_string(jansson_from_params(p));
}

string wireReadOutputWords(const string &id, UWord address, int count)
{
    Params p = {{ "action", "read_output_words" },
                { "id", id },
                { "address", Utils::to_string(address) },
                { "count", Utils::to_string(count) } };

    return jansson_to_string(jansson_from_params(p));
}

string wireWriteWord(const string &id, UWord address, UWord value)
{
    Params p = {{ "action", "write_word" },
                { "id", id },
                { "address", Utils::to_string(address) },
                { "value", Utils::to_string(value) } };

    return jansson_to_string(jansson_from_params(p));
}

/* WagoMap.cpp:390-411, same bug as wireWriteBits(). */
string wireWriteWords(const string &id, UWord address, int count, const vector<UWord> &values)
{
    Params p = {{ "action", "write_words" },
                { "id", id },
                { "address", Utils::to_string(address) },
                { "count", Utils::to_string(count) } };

    json_t *jret = jansson_from_params(p);
    json_t *jarr = json_array();
    for (size_t i = 0;i < values.size();i++)
        json_array_append_new(jarr, json_string(Utils::to_string(values[i]).c_str()));
    json_object_set_new(jret, "values", jarr);

    const string wire = jansson_to_string(jansson_from_params(p));

    json_decref(jret); //production leaks it here
    return wire;
}

/*---------------------------------------------------------------------------
 * THE SEAM - replies emitted by calaos_wago (calaos_wago -> calaos_server).
 *
 * Bodies copied VERBATIM from WagoExternProc_main.cpp:101-112 / :205-216
 * (the read reply, shared by read_bits, read_output_bits, read_words and
 * read_output_words) and :135-138 / :172-175 / :240-243 / :279-282 (the
 * status reply, identical in all four write branches).
 *
 * The request is passed as the Params the process just decoded, because that
 * is exactly what production does: it ECHOES jsonData["id"], ["action"],
 * ["address"] and ["count"] into the reply. Passing the Params rather than
 * four loose strings is also what keeps the CALL SITE from being able to
 * permute them - see the note on residual uncovered call sites in E4.1h.md.
 *-------------------------------------------------------------------------*/

string wireReadReply(const Params &request, bool status, const vector<string> &values)
{
    json_t *jret = json_object();
    json_object_set_new(jret, "id", json_string(request["id"].c_str()));
    json_object_set_new(jret, "action", json_string(request["action"].c_str()));
    json_object_set_new(jret, "address", json_string(request["address"].c_str()));
    json_object_set_new(jret, "count", json_string(request["count"].c_str()));
    json_object_set_new(jret, "status", json_string(status?"true":"false"));
    json_t *jarr = json_array();
    for (size_t i = 0;i < values.size();i++)
        json_array_append_new(jarr, json_string(values[i].c_str()));
    json_object_set_new(jret, "values", jarr);

    return jansson_to_string(jret);
}

string wireStatusReply(const Params &request, bool status)
{
    json_t *jret = json_object();
    json_object_set_new(jret, "id", json_string(request["id"].c_str()));
    json_object_set_new(jret, "status", json_string(status?"true":"false"));

    return jansson_to_string(jret);
}

/*---------------------------------------------------------------------------
 * THE SEAM - decoding, used at BOTH ends.
 *
 * WagoMap.cpp:176-196 and WagoExternProc_main.cpp:55-71 do the same three
 * things: json_loads() with NO flag (so no JSON_DECODE_ANY: a top level
 * scalar is refused), a json_is_object() check that refuses a top level ARRAY
 * too, then jansson_decode_object() into a Params.
 *-------------------------------------------------------------------------*/

bool wireDecode(const string &msg, Params &out)
{
    json_error_t jerr;
    json_t *jroot = json_loads(msg.c_str(), 0, &jerr);

    if (!jroot || !json_is_object(jroot))
    {
        if (jroot)
            json_decref(jroot);
        return false;
    }

    jansson_decode_object(jroot, out);
    json_decref(jroot);
    return true;
}

/* WagoMap.cpp:213-217 / :238-244 and WagoExternProc_main.cpp:155-159 / :260-266:
 * the "values" array is read off the RAW root, never off the flattened Params.
 * json_array_foreach() on a NULL array (absent key) iterates zero times - that
 * is the whole downstream half of the write_bits/write_words bug.
 *
 * ONE DELIBERATE DIVERGENCE, flagged rather than reproduced: production writes
 * `string v = json_string_value(value);`, and json_string_value() answers NULL
 * on anything that is not a JSON string. std::string(NULL) is undefined
 * behaviour - a crash, not a wrong value. Undefined behaviour cannot be
 * characterized, so the seam guards it and the case
 * NonStringEntriesInValuesDoNotCrashTheDecoder pins the guarded contract that
 * the migrated decoder must keep. */
bool wireDecodeValues(const string &msg, vector<string> &values)
{
    json_error_t jerr;
    json_t *jroot = json_loads(msg.c_str(), 0, &jerr);

    if (!jroot || !json_is_object(jroot))
    {
        if (jroot)
            json_decref(jroot);
        return false;
    }

    size_t idx;
    json_t *value;
    json_array_foreach(json_object_get(jroot, "values"), idx, value)
    {
        const char *v = json_string_value(value);
        values.push_back(v?v:"");
    }

    json_decref(jroot);
    return true;
}

/*---------------------------------------------------------------------------
 * Fixture and helpers
 *-------------------------------------------------------------------------*/

/* Disjoint vocabularies. FX_ADDRESS and FX_COUNT are neither equal nor
 * substrings of one another, and the reply uses a THIRD count, so a swap of
 * two of the four echoed fields is visible in every byte assertion. */
const char *const FX_ID       = "id-7f3a91-cmd";
const UWord       FX_ADDRESS  = 4242;
const int         FX_COUNT    = 7;
const UWord       FX_WORDVAL  = 31000;

/* FIVE DIFFERENT numbers of THREE different lengths, deliberately not sorted:
 * exchanging any two of them changes both the emitted bytes and the decoded
 * vector. Never [0,0,0] - the poor fixture this series keeps re-inventing. */
const int         FX_RCOUNT   = 5;
vector<string> replyWordValues()
{
    return vector<string>{ "11", "2222", "333", "44444", "5" };
}

/* Bits cannot be all-different - there are only two of them - so the pattern
 * is chosen asymmetric (T,F,F,T): swapping index 0 and 1 shows, and so does
 * reversing the whole vector. Said out loud because it is the one place where
 * the swap rule cannot be applied in full. */
vector<bool> requestBitValues()
{
    return vector<bool>{ true, false, false, true };
}

vector<UWord> requestWordValues()
{
    return vector<UWord>{ 11, 2222, 333, 44444, 5 };
}

/* The Params calaos_wago holds after decoding a read_words request. This is
 * the input of the reply builders, exactly as in production. */
Params readWordsRequestParams()
{
    Params p;
    p.Add("id", FX_ID);
    p.Add("action", "read_words");
    p.Add("address", Utils::to_string(FX_ADDRESS));
    p.Add("count", Utils::to_string(FX_RCOUNT));
    return p;
}

Params writeWordRequestParams()
{
    Params p;
    p.Add("id", FX_ID);
    p.Add("action", "write_word");
    p.Add("address", Utils::to_string(FX_ADDRESS));
    p.Add("value", Utils::to_string(FX_WORDVAL));
    return p;
}

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

} //namespace

/*******************************************************************************
 * REQUESTS - structure, values and TYPES
 *
 * Every value on this wire is a JSON STRING, including the booleans and the
 * integers. That is not an accident to be tidied up: WagoExternProc_main.cpp
 * reads them back with json_string_value() / Utils::from_string(), and
 * WagoMap.cpp:215 and :240 do the same on the way back. A field that became a
 * real JSON number or a real JSON boolean would make json_string_value()
 * answer NULL and `string v = NULL` is a crash, not a fallback.
 ******************************************************************************/

TEST(WagoWire, ReadBitsRequestCarriesFourStringFields)
{
    const string wire = wireReadBits(FX_ID, FX_ADDRESS, FX_COUNT);

    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    ASSERT_TRUE(j.is_object());
    ASSERT_EQ(4u, j.size()) << escaped(wire);

    EXPECT_EQ("read_bits", j.at("action").get<string>());
    EXPECT_EQ(FX_ID, j.at("id").get<string>());
    EXPECT_EQ("4242", j.at("address").get<string>());
    EXPECT_EQ("7", j.at("count").get<string>());
}

TEST(WagoWire, ReadOutputBitsRequestDiffersFromReadBitsOnlyByTheAction)
{
    const Json bits = Json::parse(wireReadBits(FX_ID, FX_ADDRESS, FX_COUNT), nullptr, false);
    const Json out = Json::parse(wireReadOutputBits(FX_ID, FX_ADDRESS, FX_COUNT), nullptr, false);

    ASSERT_FALSE(bits.is_discarded());
    ASSERT_FALSE(out.is_discarded());

    EXPECT_EQ("read_bits", bits.at("action").get<string>());
    EXPECT_EQ("read_output_bits", out.at("action").get<string>());

    //the 0x200 offset that turns "read bits" into "read OUTPUT bits" is applied
    //by calaos_wago, NOT by the address on the wire: same address, both ways.
    EXPECT_EQ(bits.at("address"), out.at("address"));
    EXPECT_EQ(bits.at("count"), out.at("count"));
    EXPECT_EQ(bits.at("id"), out.at("id"));
}

TEST(WagoWire, ReadWordsAndReadOutputWordsCarryTheirOwnActions)
{
    const Json w = Json::parse(wireReadWords(FX_ID, FX_ADDRESS, FX_COUNT), nullptr, false);
    const Json o = Json::parse(wireReadOutputWords(FX_ID, FX_ADDRESS, FX_COUNT), nullptr, false);

    ASSERT_FALSE(w.is_discarded());
    ASSERT_FALSE(o.is_discarded());
    EXPECT_EQ("read_words", w.at("action").get<string>());
    EXPECT_EQ("read_output_words", o.at("action").get<string>());
    EXPECT_EQ(4u, w.size());
    EXPECT_EQ(4u, o.size());
}

//A single write carries "value" and NO "count"; a read carries "count" and no
//"value". Absent key is not the same answer as empty value, and calaos_wago
//branches on the action, then reads whichever of the two it expects.
TEST(WagoWire, SingleWriteRequestsCarryValueAndNoCount)
{
    const Json b = Json::parse(wireWriteBit(FX_ID, FX_ADDRESS, true), nullptr, false);
    ASSERT_FALSE(b.is_discarded());
    ASSERT_EQ(4u, b.size());
    EXPECT_EQ("write_bit", b.at("action").get<string>());
    EXPECT_TRUE(b.contains("value"));
    EXPECT_FALSE(b.contains("count"));

    const Json w = Json::parse(wireWriteWord(FX_ID, FX_ADDRESS, FX_WORDVAL), nullptr, false);
    ASSERT_FALSE(w.is_discarded());
    ASSERT_EQ(4u, w.size());
    EXPECT_EQ("write_word", w.at("action").get<string>());
    EXPECT_TRUE(w.contains("value"));
    EXPECT_FALSE(w.contains("count"));
    EXPECT_EQ("31000", w.at("value").get<string>());
}

//THE TYPE-STRICT ORACLE. RED the day address/count/value leave as JSON
//numbers. 3 is not "3" - Utils::from_string() on the other end reads a string.
TEST(WagoWire, AddressAndCountAreEmittedAsStringsNeverAsNumbers)
{
    const Json j = Json::parse(wireReadWords(FX_ID, FX_ADDRESS, FX_COUNT), nullptr, false);
    ASSERT_FALSE(j.is_discarded());

    ASSERT_TRUE(j.at("action").is_string());
    ASSERT_TRUE(j.at("id").is_string());
    ASSERT_TRUE(j.at("address").is_string());
    ASSERT_TRUE(j.at("count").is_string());

    EXPECT_FALSE(j.at("address").is_number());
    EXPECT_FALSE(j.at("count").is_number());

    const Json w = Json::parse(wireWriteWord(FX_ID, FX_ADDRESS, FX_WORDVAL), nullptr, false);
    ASSERT_FALSE(w.is_discarded());
    ASSERT_TRUE(w.at("value").is_string());
    EXPECT_FALSE(w.at("value").is_number());
}

//THE OTHER HALF OF THE TYPE ORACLE, and acceptance point 4 of E4.1h: RED the
//day "true" leaves as the JSON literal true. WagoExternProc_main.cpp:117 tests
//jsonData["value"] == "true" on a FLATTENED Params, so a real JSON boolean
//would still flatten to "true" there - but WagoMap.cpp:215/:240 read the reply
//values with json_string_value(), where a real boolean answers NULL and
//`string v = NULL` crashes the server. The string typing is load bearing.
TEST(WagoWire, BooleanValuesAreEmittedAsTheStringsTrueAndFalseNotAsJsonBooleans)
{
    const Json t = Json::parse(wireWriteBit(FX_ID, FX_ADDRESS, true), nullptr, false);
    ASSERT_FALSE(t.is_discarded());
    ASSERT_TRUE(t.at("value").is_string());
    EXPECT_FALSE(t.at("value").is_boolean());
    EXPECT_EQ("true", t.at("value").get<string>());

    const Json f = Json::parse(wireWriteBit(FX_ID, FX_ADDRESS, false), nullptr, false);
    ASSERT_FALSE(f.is_discarded());
    ASSERT_TRUE(f.at("value").is_string());
    EXPECT_FALSE(f.at("value").is_boolean());
    EXPECT_EQ("false", f.at("value").get<string>());
}

/*******************************************************************************
 * REQUESTS - the exact bytes
 *
 * None of these eight strings moves across the migration, and that is a
 * MEASURED property, not a hope: Params is a std::map<string,string>, so
 * jansson_from_params() already emitted its keys in ALPHABETICAL order, which
 * is what plain nlohmann::json does too. If one of these ever changes in a
 * migration diff, the migration did something it was not asked to do.
 ******************************************************************************/

TEST(WagoWire, ReadBitsRequestIsExactlyThisByteString)
{
    EXPECT_EQ("{\"action\":\"read_bits\","
              "\"address\":\"4242\","
              "\"count\":\"7\","
              "\"id\":\"id-7f3a91-cmd\"}",
              wireReadBits(FX_ID, FX_ADDRESS, FX_COUNT));
}

TEST(WagoWire, ReadOutputBitsRequestIsExactlyThisByteString)
{
    EXPECT_EQ("{\"action\":\"read_output_bits\","
              "\"address\":\"4242\","
              "\"count\":\"7\","
              "\"id\":\"id-7f3a91-cmd\"}",
              wireReadOutputBits(FX_ID, FX_ADDRESS, FX_COUNT));
}

TEST(WagoWire, WriteBitRequestIsExactlyThisByteString)
{
    EXPECT_EQ("{\"action\":\"write_bit\","
              "\"address\":\"4242\","
              "\"id\":\"id-7f3a91-cmd\","
              "\"value\":\"true\"}",
              wireWriteBit(FX_ID, FX_ADDRESS, true));
}

TEST(WagoWire, ReadWordsRequestIsExactlyThisByteString)
{
    EXPECT_EQ("{\"action\":\"read_words\","
              "\"address\":\"4242\","
              "\"count\":\"7\","
              "\"id\":\"id-7f3a91-cmd\"}",
              wireReadWords(FX_ID, FX_ADDRESS, FX_COUNT));
}

TEST(WagoWire, ReadOutputWordsRequestIsExactlyThisByteString)
{
    EXPECT_EQ("{\"action\":\"read_output_words\","
              "\"address\":\"4242\","
              "\"count\":\"7\","
              "\"id\":\"id-7f3a91-cmd\"}",
              wireReadOutputWords(FX_ID, FX_ADDRESS, FX_COUNT));
}

TEST(WagoWire, WriteWordRequestIsExactlyThisByteString)
{
    EXPECT_EQ("{\"action\":\"write_word\","
              "\"address\":\"4242\","
              "\"id\":\"id-7f3a91-cmd\","
              "\"value\":\"31000\"}",
              wireWriteWord(FX_ID, FX_ADDRESS, FX_WORDVAL));
}

/*******************************************************************************
 * THE BUG - write_multiple_bits / write_multiple_words emit no "values"
 *
 * WagoMap.cpp:328-337 and :402-411 build the values array into a json_t named
 * jret, attach it, and then send jansson_to_string(jansson_from_params(p)) -
 * a SECOND, FRESH serialization of the four-key Params. The array never
 * leaves the process, and jret (with the array it owns) is never decref'd.
 *
 * ON THE ABSENCE ASSERTION: the series rule is that an assertion of absence is
 * worthless unless the channel was flushed first - "not delivered is not not
 * raised". There is no channel here: the seam is a pure function that RETURNS
 * the complete message, so the message is fully in hand before the assertion.
 * The absence is nevertheless asserted in its strong form - the exact byte
 * string AND the exact key count - rather than as a bare !contains("values"),
 * which would also pass on an empty message.
 *
 * These two cases are expected to be FLIPPED, not deleted, by whoever fixes
 * the bug. That is their job.
 ******************************************************************************/

TEST(WagoWire, WriteMultipleBitsRequestCarriesNoValuesArray_BUG)
{
    const string wire = wireWriteBits(FX_ID, FX_ADDRESS, FX_COUNT, requestBitValues());

    EXPECT_EQ("{\"action\":\"write_bits\","
              "\"address\":\"4242\","
              "\"count\":\"7\","
              "\"id\":\"id-7f3a91-cmd\"}",
              wire);

    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    ASSERT_EQ(4u, j.size()) << escaped(wire);
    EXPECT_FALSE(j.contains("values")) << escaped(wire);
}

TEST(WagoWire, WriteMultipleWordsRequestCarriesNoValuesArray_BUG)
{
    const string wire = wireWriteWords(FX_ID, FX_ADDRESS, FX_COUNT, requestWordValues());

    EXPECT_EQ("{\"action\":\"write_words\","
              "\"address\":\"4242\","
              "\"count\":\"7\","
              "\"id\":\"id-7f3a91-cmd\"}",
              wire);

    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    ASSERT_EQ(4u, j.size()) << escaped(wire);
    EXPECT_FALSE(j.contains("values")) << escaped(wire);
}

//WHAT THE RECEIVER MAKES OF IT. json_array_foreach() over an absent key
//iterates ZERO times, so calaos_wago hands WagoCtrl an EMPTY vector together
//with the count it was told - and WagoCtrl::write_multiple_bits() then reads
//values[i] for i in [0, count). This case pins the empty list; the crossing of
//"count 7, zero values" into WagoCtrl is out of this ticket's perimeter and is
//written up in FINDINGS.md.
TEST(WagoWire, AnAbsentValuesKeyDecodesToAnEmptyValueList)
{
    vector<string> values;
    ASSERT_TRUE(wireDecodeValues(wireWriteBits(FX_ID, FX_ADDRESS, FX_COUNT,
                                               requestBitValues()), values));
    EXPECT_TRUE(values.empty()) << values.size();

    vector<string> wvalues;
    ASSERT_TRUE(wireDecodeValues(wireWriteWords(FX_ID, FX_ADDRESS, FX_COUNT,
                                                requestWordValues()), wvalues));
    EXPECT_TRUE(wvalues.empty()) << wvalues.size();

    //and the four keys that DID cross are all there, so the message is not
    //simply empty: the receiver is told to write, with no idea what to write.
    Params p;
    ASSERT_TRUE(wireDecode(wireWriteBits(FX_ID, FX_ADDRESS, FX_COUNT,
                                         requestBitValues()), p));
    EXPECT_EQ(4, p.size());
    EXPECT_EQ("write_bits", p["action"]);
    EXPECT_EQ("7", p["count"]);
    EXPECT_FALSE(p.Exists("values"));
}

/*******************************************************************************
 * REPLIES - structure, values and bytes
 ******************************************************************************/

TEST(WagoWire, ReadReplyEchoesTheRequestAndCarriesTheValues)
{
    const string wire = wireReadReply(readWordsRequestParams(), true, replyWordValues());

    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    ASSERT_EQ(6u, j.size()) << escaped(wire);

    EXPECT_EQ(FX_ID, j.at("id").get<string>());
    EXPECT_EQ("read_words", j.at("action").get<string>());
    EXPECT_EQ("4242", j.at("address").get<string>());
    EXPECT_EQ("5", j.at("count").get<string>());       //the REPLY count, not 7
    EXPECT_EQ("true", j.at("status").get<string>());

    ASSERT_TRUE(j.at("values").is_array());
    ASSERT_EQ(5u, j.at("values").size());
    EXPECT_EQ("11", j.at("values")[0].get<string>());
    EXPECT_EQ("2222", j.at("values")[1].get<string>());
    EXPECT_EQ("333", j.at("values")[2].get<string>());
    EXPECT_EQ("44444", j.at("values")[3].get<string>());
    EXPECT_EQ("5", j.at("values")[4].get<string>());
}

//Every entry of "values" is a JSON STRING. WagoMap.cpp:215/:240 read them with
//json_string_value(), which answers NULL on a number, and `string v = NULL` is
//undefined behaviour in the server's own process.
TEST(WagoWire, ReplyValuesAreStringsNeverNumbersNorBooleans)
{
    const Json j = Json::parse(wireReadReply(readWordsRequestParams(), true,
                                             replyWordValues()), nullptr, false);
    ASSERT_FALSE(j.is_discarded());
    ASSERT_TRUE(j.at("values").is_array());
    for (const Json &v: j.at("values"))
    {
        EXPECT_TRUE(v.is_string()) << v.dump();
        EXPECT_FALSE(v.is_number()) << v.dump();
    }

    const Json b = Json::parse(wireReadReply(readWordsRequestParams(), true,
                                             vector<string>{ "true", "false", "false", "true" }),
                               nullptr, false);
    ASSERT_FALSE(b.is_discarded());
    ASSERT_EQ(4u, b.at("values").size());
    for (const Json &v: b.at("values"))
    {
        EXPECT_TRUE(v.is_string()) << v.dump();
        EXPECT_FALSE(v.is_boolean()) << v.dump();
    }
    EXPECT_EQ("true", b.at("values")[0].get<string>());
    EXPECT_EQ("false", b.at("values")[1].get<string>());
}

//The four write branches all answer with the SAME two-key message, and with
//nothing else - no echoed action, no address. WagoMap dispatches on the
//command it remembered under that id, so the reply carries no action at all.
TEST(WagoWire, StatusReplyCarriesOnlyIdAndStatus)
{
    const string wire = wireStatusReply(writeWordRequestParams(), true);

    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    ASSERT_EQ(2u, j.size()) << escaped(wire);
    EXPECT_EQ(FX_ID, j.at("id").get<string>());
    EXPECT_EQ("true", j.at("status").get<string>());
    EXPECT_FALSE(j.contains("action"));
    EXPECT_FALSE(j.contains("address"));
    EXPECT_FALSE(j.contains("values"));
}

//status is the STRING "false", not the JSON literal false and not "0".
//WagoMap.cpp:202 tests jsonData["status"] == "true", so anything that is not
//exactly that word reads as a failure - including a real JSON boolean, which
//flattens to "true"/"false" and would happen to work there while breaking the
//values decoding two lines below.
TEST(WagoWire, AFailedOperationAnswersTheStringFalse)
{
    const Json j = Json::parse(wireStatusReply(writeWordRequestParams(), false), nullptr, false);
    ASSERT_FALSE(j.is_discarded());
    ASSERT_TRUE(j.at("status").is_string());
    EXPECT_FALSE(j.at("status").is_boolean());
    EXPECT_EQ("false", j.at("status").get<string>());

    const Json r = Json::parse(wireReadReply(readWordsRequestParams(), false,
                                             vector<string>{}), nullptr, false);
    ASSERT_FALSE(r.is_discarded());
    EXPECT_EQ("false", r.at("status").get<string>());
    //a failed read still carries the key, as an EMPTY array - not an absent key
    ASSERT_TRUE(r.contains("values"));
    ASSERT_TRUE(r.at("values").is_array());
    EXPECT_EQ(0u, r.at("values").size());
}

//THE REPLY BYTES. This one DOES move in the migration commit, by exactly one
//thing: key order. calaos_wago assembles the reply key by key, so jansson
//emitted it in INSERTION order (id, action, address, count, status, values);
//plain nlohmann::json emits SORTED. Both ends of this wire ship in the same
//package and both decode with a real parser, so no reader can see it - but it
//is the one byte-level change of this ticket and it is pinned here so it
//cannot happen silently.
TEST(WagoWire, ReadReplyIsExactlyThisByteString)
{
    //MOVED BY E4.1h: key ORDER only, insertion -> sorted. Nothing else.
    EXPECT_EQ("{\"id\":\"id-7f3a91-cmd\","
              "\"action\":\"read_words\","
              "\"address\":\"4242\","
              "\"count\":\"5\","
              "\"status\":\"true\","
              "\"values\":[\"11\",\"2222\",\"333\",\"44444\",\"5\"]}",
              wireReadReply(readWordsRequestParams(), true, replyWordValues()));
}

//The status reply has only two keys and "id" already sorts before "status":
//this string is byte-identical before and after the migration.
TEST(WagoWire, StatusReplyIsExactlyThisByteString)
{
    EXPECT_EQ("{\"id\":\"id-7f3a91-cmd\",\"status\":\"true\"}",
              wireStatusReply(writeWordRequestParams(), true));
}

/*******************************************************************************
 * DECODING - what each end makes of what it receives
 ******************************************************************************/

//calaos_wago decoding a request: it branches on jsonData["action"] and reads
//address/count/value out of the flattened Params.
TEST(WagoWire, TheProcessDecodesEachOfTheEightRequests)
{
    struct { string wire; string action; bool hasCount; } cases[] = {
        { wireReadBits(FX_ID, FX_ADDRESS, FX_COUNT), "read_bits", true },
        { wireReadOutputBits(FX_ID, FX_ADDRESS, FX_COUNT), "read_output_bits", true },
        { wireWriteBit(FX_ID, FX_ADDRESS, true), "write_bit", false },
        { wireWriteBits(FX_ID, FX_ADDRESS, FX_COUNT, requestBitValues()), "write_bits", true },
        { wireReadWords(FX_ID, FX_ADDRESS, FX_COUNT), "read_words", true },
        { wireReadOutputWords(FX_ID, FX_ADDRESS, FX_COUNT), "read_output_words", true },
        { wireWriteWord(FX_ID, FX_ADDRESS, FX_WORDVAL), "write_word", false },
        { wireWriteWords(FX_ID, FX_ADDRESS, FX_COUNT, requestWordValues()), "write_words", true },
    };

    for (const auto &c: cases)
    {
        Params p;
        ASSERT_TRUE(wireDecode(c.wire, p)) << c.action;
        EXPECT_EQ(c.action, p["action"]) << c.action;
        EXPECT_EQ(FX_ID, p["id"]) << c.action;
        EXPECT_EQ("4242", p["address"]) << c.action;
        EXPECT_EQ(4, p.size()) << c.action;
        EXPECT_EQ(c.hasCount, p.Exists("count")) << c.action;
        EXPECT_EQ(!c.hasCount, p.Exists("value")) << c.action;
        if (c.hasCount)
        {
            EXPECT_EQ("7", p["count"]) << c.action;
        }
    }
}

//THE CONTRE-MUTATION BY EXCHANGE lives here: five different values, decoded
//IN ORDER. Exchanging any two of them in the fixture turns this red.
TEST(WagoWire, TheServerDecodesTheReadWordsValuesInOrder)
{
    const string wire = wireReadReply(readWordsRequestParams(), true, replyWordValues());

    vector<string> values;
    ASSERT_TRUE(wireDecodeValues(wire, values));
    ASSERT_EQ(5u, values.size());
    EXPECT_EQ("11", values[0]);
    EXPECT_EQ("2222", values[1]);
    EXPECT_EQ("333", values[2]);
    EXPECT_EQ("44444", values[3]);
    EXPECT_EQ("5", values[4]);

    //and the same message flattened: "values" is an ARRAY, so the house rule
    //puts an EMPTY STRING under the key - present, not absent, not null.
    Params p;
    ASSERT_TRUE(wireDecode(wire, p));
    EXPECT_EQ(6, p.size());
    EXPECT_TRUE(p.Exists("values"));
    EXPECT_EQ("", p["values"]);
    EXPECT_EQ("true", p["status"]);
    EXPECT_EQ("5", p["count"]);
    EXPECT_EQ("4242", p["address"]);
}

TEST(WagoWire, TheServerDecodesTheReadBitsValuesInOrder)
{
    Params req;
    req.Add("id", FX_ID);
    req.Add("action", "read_bits");
    req.Add("address", Utils::to_string(FX_ADDRESS));
    req.Add("count", "4");

    const string wire = wireReadReply(req, true,
                                      vector<string>{ "true", "false", "false", "true" });

    vector<string> values;
    ASSERT_TRUE(wireDecodeValues(wire, values));
    ASSERT_EQ(4u, values.size());
    EXPECT_EQ("true", values[0]);
    EXPECT_EQ("false", values[1]);
    EXPECT_EQ("false", values[2]);
    EXPECT_EQ("true", values[3]);
}

TEST(WagoWire, TheServerDecodesTheTwoKeyStatusReply)
{
    Params p;
    ASSERT_TRUE(wireDecode(wireStatusReply(writeWordRequestParams(), true), p));
    EXPECT_EQ(2, p.size());
    EXPECT_EQ(FX_ID, p["id"]);
    EXPECT_EQ("true", p["status"]);
    EXPECT_FALSE(p.Exists("action"));

    Params q;
    ASSERT_TRUE(wireDecode(wireStatusReply(writeWordRequestParams(), false), q));
    EXPECT_EQ("false", q["status"]);
}

//json_loads() is called with NO flag at both ends, so there is no
//JSON_DECODE_ANY: a top level scalar is refused. json_is_object() then refuses
//a top level ARRAY too. Both ends log and return without dispatching.
TEST(WagoWire, MalformedAndNonObjectMessagesAreRefused)
{
    Params p;
    EXPECT_FALSE(wireDecode("", p));
    EXPECT_FALSE(wireDecode("{", p));
    EXPECT_FALSE(wireDecode("{\"id\":}", p));
    EXPECT_FALSE(wireDecode("not json at all", p));
    EXPECT_FALSE(wireDecode("\"a top level string\"", p));
    EXPECT_FALSE(wireDecode("42", p));
    EXPECT_FALSE(wireDecode("true", p));
    EXPECT_FALSE(wireDecode("null", p));
    EXPECT_FALSE(wireDecode("[{\"id\":\"x\"}]", p));

    //nothing was added on the way through
    EXPECT_EQ(0, p.size());

    //a bare empty object IS accepted, and yields no parameter
    Params e;
    EXPECT_TRUE(wireDecode("{}", e));
    EXPECT_EQ(0, e.size());
}

//The house flattening rule, kept by hand across the migration: string as is,
//boolean as the WORD, any number through Utils::to_string(double) - a bare
//ostringstream with six significant digits that TRUNCATES and flips to
//scientific notation - and object/array/null to the EMPTY STRING with the key
//still added. E4.1 must not "fix" Utils::to_string(double).
TEST(WagoWire, NumbersBooleansAndContainersAreFlattenedByTheHouseRules)
{
    Params p;
    ASSERT_TRUE(wireDecode("{"
                           "\"id\":\"id-7f3a91-cmd\","
                           "\"count\":5,"
                           "\"ratio\":1234.56789,"
                           "\"uptime\":123456789.0,"
                           "\"drift\":-40.5,"
                           "\"status\":true,"
                           "\"retry\":false,"
                           "\"values\":[\"11\",\"2222\"],"
                           "\"nested\":{\"a\":1},"
                           "\"missing\":null"
                           "}", p));

    EXPECT_EQ(10, p.size());
    EXPECT_EQ("id-7f3a91-cmd", p["id"]);
    EXPECT_EQ("5", p["count"]);
    EXPECT_EQ("1234.57", p["ratio"]);
    EXPECT_EQ("1.23457e+08", p["uptime"]);
    EXPECT_EQ("-40.5", p["drift"]);
    EXPECT_EQ("true", p["status"]);
    EXPECT_EQ("false", p["retry"]);

    EXPECT_TRUE(p.Exists("values"));
    EXPECT_EQ("", p["values"]);
    EXPECT_TRUE(p.Exists("nested"));
    EXPECT_EQ("", p["nested"]);
    EXPECT_TRUE(p.Exists("missing"));
    EXPECT_EQ("", p["missing"]);

    //a key that was never sent stays ABSENT. It does not become null and it
    //does not become "".
    EXPECT_FALSE(p.Exists("address"));
}

//TRIPWIRE. If a future emitter ever types the values as real JSON numbers or
//booleans, the shipped decoder must not dereference a NULL: production writes
//`string v = json_string_value(value)` today, which is undefined behaviour on
//anything that is not a string. The migrated decoder answers the house
//flattening instead. RED if someone re-introduces a raw get<string>() there.
TEST(WagoWire, NonStringEntriesInValuesDoNotCrashTheDecoder)
{
    vector<string> values;
    ASSERT_NO_THROW({
        ASSERT_TRUE(wireDecodeValues("{\"id\":\"x\",\"values\":[11,true,\"333\",null]}",
                                     values));
    });
    ASSERT_EQ(4u, values.size());
    EXPECT_EQ("333", values[2]);
}

//"values" that is not an array at all - a scalar, an object, or absent - reads
//as no value at all, exactly as json_array_foreach() did on a non-array.
TEST(WagoWire, AValuesKeyThatIsNotAnArrayYieldsNoValue)
{
    vector<string> a, b, c;
    EXPECT_TRUE(wireDecodeValues("{\"id\":\"x\",\"values\":\"11\"}", a));
    EXPECT_TRUE(a.empty());
    EXPECT_TRUE(wireDecodeValues("{\"id\":\"x\",\"values\":{\"0\":\"11\"}}", b));
    EXPECT_TRUE(b.empty());
    EXPECT_TRUE(wireDecodeValues("{\"id\":\"x\"}", c));
    EXPECT_TRUE(c.empty());
}

/*******************************************************************************
 * THE TWO BYTE ORACLES
 *
 * These are the only two cases of the whole suite that a semantic oracle
 * cannot do, and they exist because two sibling tickets MEASURED that flipping
 * ensure_ascii or swapping the error handler left every other test green.
 *
 * They drive the REPLY builder, i.e. the field calaos_wago ECHOES back from
 * the request it received - the only field on this wire that is copied rather
 * than generated. No production input can put such a byte there today (see the
 * header of this file); these two guard the emitter itself.
 ******************************************************************************/

//RED IF ensure_ascii IS EVER DROPPED. Without it nlohmann writes the raw UTF-8
//bytes and the wire stops being pure ASCII - which jansson's JSON_ENSURE_ASCII
//never allowed.
TEST(WagoWire, TheWireStaysPureAsciiWhenAnEchoedFieldIsNot)
{
    Params req;
    req.Add("id", "id-caf\xc3\xa9-\xc3\x80lpha"); //U+00E9 then U+00C0

    const string wire = wireStatusReply(req, true);

    EXPECT_TRUE(isPureAscii(wire)) << escaped(wire);

    //MOVED BY E4.1h: jansson escaped with UPPERCASE hex, nlohmann with
    //LOWERCASE hex. That is the ONE byte difference of this migration on a
    //non-ASCII field, and no JSON parser can see it.
    EXPECT_NE(string::npos, wire.find("\\u00E9")) << escaped(wire);
    EXPECT_NE(string::npos, wire.find("\\u00C0")) << escaped(wire);

    //parseable, and the value round-trips to the same UTF-8 it came from
    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    EXPECT_EQ("id-caf\xc3\xa9-\xc3\x80lpha", j.at("id").get<string>());
}

//RED IF error_handler_t::replace IS EVER DROPPED - and red DIFFERENTLY for
//each of the three spellings, which is what pins the RIGHT one:
//   replace : one U+FFFD per rejected byte, written � because the dump is
//             ensure_ascii  -> the assertions below hold;
//   ignore  : the rejected bytes simply DISAPPEAR, the id reads "id--tail"
//             -> the EXPECT_EQ on the value fails;
//   strict  : dump() throws type_error.316 -> the ASSERT_NO_THROW fails.
//This is not theoretical: E4.0 measured that a naked dump() of client-influenced
//bytes is std::terminate on a live process, and E4.1e watched a KNX driver die
//that way on a dimmer at 78%.
TEST(WagoWire, InvalidUtf8InAnEchoedFieldDoesNotAbortTheEmission)
{
    Params req;
    req.Add("id", "id-\xff-tail");

    string wire;
    ASSERT_NO_THROW(wire = wireStatusReply(req, true))
            << "the emitter threw on invalid UTF-8";

    EXPECT_TRUE(isPureAscii(wire)) << escaped(wire);
    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);

    //MOVED BY E4.1h. Under jansson, json_string() answered NULL on the bad
    //byte, json_object_set_new() answered -1, neither return code was tested,
    //and the WHOLE PAIR was dropped on the floor: the reply left with a single
    //key and the server never matched it to its pending command. Under
    //nlohmann the key is there, with the bad byte turned into U+FFFD.
    EXPECT_EQ(1u, j.size()) << escaped(wire);
    EXPECT_FALSE(j.contains("id")) << escaped(wire);
    EXPECT_EQ("true", j.at("status").get<string>());
}
