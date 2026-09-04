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
 *   Eight more cases freeze the exact byte string of the eight requests. Not
 *   one of the eight moves across the MIGRATION, and that is the point: Params
 *   is a std::map, so jansson_from_params() already walked it ALPHABETICALLY,
 *   which is exactly what nlohmann::json does. The request wire is
 *   byte-identical before and after. Only the REPLIES, assembled key by key in
 *   insertion order by calaos_wago, get re-sorted.
 *   Two of the eight - the multiple writes - move in the FIX commit instead,
 *   which is a different thing and is flagged as such where they sit.
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
#include <type_traits>
#include <vector>

#include "Utils.h"
#include "Params.h"
/* THE PRODUCTION HEADER. Not a copy of it: the very text WagoMap.cpp and
 * WagoExternProc_main.cpp include and the two binaries ship. */
#include "WagoWire.h"

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

/* T3.31 - the shipped builders now take WagoTypes::Address / Count / Value.
 * The seams keep bare scalars on purpose: they stand where WagoMap.cpp
 * stands, and the wrapping they do here is the wrapping the production call
 * site does. Not one assertion below changed with them. */
string wireReadBits(const string &id, UWord address, int count)
{
    return WagoWire::buildReadBitsRequest(id, WagoTypes::Address(address), WagoTypes::Count(count));
}

string wireReadOutputBits(const string &id, UWord address, int count)
{
    return WagoWire::buildReadOutputBitsRequest(id, WagoTypes::Address(address), WagoTypes::Count(count));
}

string wireWriteBit(const string &id, UWord address, bool value)
{
    return WagoWire::buildWriteBitRequest(id, WagoTypes::Address(address), WagoTypes::BitValue(value));
}

/* The site that carried the bug. Before the rewiring this body was the
 * jansson assembly of WagoMap.cpp:316-337 verbatim: build the values array
 * into a json_t called jret, attach it, then send a SECOND, FRESH
 * serialization of the four-key Params - and leak jret. It now forwards to
 * the shipped builder, which since the fix commit EMITS the array.
 * See the two cases named ...CarriesTheValuesArray below. */
string wireWriteBits(const string &id, UWord address, int count, const vector<bool> &values)
{
    return WagoWire::buildWriteBitsRequest(id, WagoTypes::Address(address), WagoTypes::Count(count), values);
}

string wireReadWords(const string &id, UWord address, int count)
{
    return WagoWire::buildReadWordsRequest(id, WagoTypes::Address(address), WagoTypes::Count(count));
}

string wireReadOutputWords(const string &id, UWord address, int count)
{
    return WagoWire::buildReadOutputWordsRequest(id, WagoTypes::Address(address), WagoTypes::Count(count));
}

string wireWriteWord(const string &id, UWord address, UWord value)
{
    return WagoWire::buildWriteWordRequest(id, WagoTypes::Address(address), WagoTypes::WordValue(value));
}

/* Same defect as wireWriteBits(), same rewiring, same fix. */
string wireWriteWords(const string &id, UWord address, int count, const vector<UWord> &values)
{
    return WagoWire::buildWriteWordsRequest(id, WagoTypes::Address(address), WagoTypes::Count(count), values);
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
    return WagoWire::buildReadReply(request, status, values);
}

string wireStatusReply(const Params &request, bool status)
{
    return WagoWire::buildStatusReply(request, status);
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
    return WagoWire::decodeMessage(msg, out);
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
    Params ignored;
    return WagoWire::decodeMessage(msg, ignored, &values);
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
 * vector. Never [0,0,0] - the poor fixture this series keeps re-inventing.
 *
 * ⚠️ FX_RCOUNT IS 3 AND THE REPLY CARRIES 5 VALUES, ON PURPOSE. It used to be
 * 5, which is exactly replyWordValues().size() - and that made a real oracle
 * DEAD: rebuilding the reply's "count" from values.size() instead of ECHOING
 * it from the request was invisible, 31/31 green (mutation N1 of the review).
 * "count" is the ONE field where the request and the reply must be allowed to
 * disagree - calaos_wago echoes what it was ASKED for, it does not recount
 * what the PLC gave back - so the fixture has to make them disagree.
 * 9th recurrence of the "poor fixture" defect in this series, found by a
 * reviewer again and not by the implementer. */
const int         FX_RCOUNT   = 3;
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
 * THE BUG, AND ITS FIX - write_multiple_bits / write_multiple_words
 *
 * WagoMap.cpp:328-337 and :402-411 used to build the values array into a
 * json_t named jret, attach it, and then send
 * jansson_to_string(jansson_from_params(p)) - a SECOND, FRESH serialization of
 * the four-key Params. The array never left the process and jret, with the
 * array it owned, was never decref'd: one functional defect and one leak on
 * the same four lines.
 *
 * THE TWO CASES BELOW WERE WRITTEN AS ABSENCE ASSERTIONS and are FLIPPED here,
 * in the fix commit, exactly as their predecessors said they would be. Both
 * halves are worth reading in the diff: the first shows what crossed before,
 * the second what crosses now.
 *
 * Fixing it is safe because the two emitters have ZERO CALLERS in the tree -
 * no PLC has ever received this message. Measured, not assumed; see
 * FINDINGS.md, F-WAGO-2.
 *
 * ON ASSERTIONS OF ABSENCE, which the "values" key still needs elsewhere: the
 * series rule is that they are worthless unless the channel was flushed first
 * - "not delivered is not not raised". There is no channel here: the seam is a
 * pure function that RETURNS the complete message, so the message is fully in
 * hand before the assertion. They are nevertheless asserted in their strong
 * form - the exact byte string AND the exact key count - rather than as a bare
 * !contains(), which would also pass on an empty message.
 ******************************************************************************/

TEST(WagoWire, WriteMultipleBitsRequestCarriesTheValuesArray)
{
    const string wire = wireWriteBits(FX_ID, FX_ADDRESS, FX_COUNT, requestBitValues());

    //FLIPPED BY THE FIX. It used to read, and pass:
    //  "{\"action\":\"write_bits\",\"address\":\"4242\","
    //  "\"count\":\"7\",\"id\":\"id-7f3a91-cmd\"}"   - four keys, no values
    EXPECT_EQ("{\"action\":\"write_bits\","
              "\"address\":\"4242\","
              "\"count\":\"7\","
              "\"id\":\"id-7f3a91-cmd\","
              "\"values\":[\"true\",\"false\",\"false\",\"true\"]}",
              wire);

    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    ASSERT_EQ(5u, j.size()) << escaped(wire);
    ASSERT_TRUE(j.contains("values")) << escaped(wire);
    ASSERT_TRUE(j.at("values").is_array());
    ASSERT_EQ(4u, j.at("values").size());

    //STRINGS, not JSON booleans, and in the order they were given
    for (const Json &v: j.at("values"))
    {
        EXPECT_TRUE(v.is_string()) << v.dump();
        EXPECT_FALSE(v.is_boolean()) << v.dump();
    }
    EXPECT_EQ("true", j.at("values")[0].get<string>());
    EXPECT_EQ("false", j.at("values")[1].get<string>());
    EXPECT_EQ("false", j.at("values")[2].get<string>());
    EXPECT_EQ("true", j.at("values")[3].get<string>());
}

TEST(WagoWire, WriteMultipleWordsRequestCarriesTheValuesArray)
{
    const string wire = wireWriteWords(FX_ID, FX_ADDRESS, FX_COUNT, requestWordValues());

    //FLIPPED BY THE FIX. It used to read, and pass:
    //  "{\"action\":\"write_words\",\"address\":\"4242\","
    //  "\"count\":\"7\",\"id\":\"id-7f3a91-cmd\"}"   - four keys, no values
    //FIVE DIFFERENT numbers of THREE different lengths: exchanging any two of
    //them changes this string.
    EXPECT_EQ("{\"action\":\"write_words\","
              "\"address\":\"4242\","
              "\"count\":\"7\","
              "\"id\":\"id-7f3a91-cmd\","
              "\"values\":[\"11\",\"2222\",\"333\",\"44444\",\"5\"]}",
              wire);

    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    ASSERT_EQ(5u, j.size()) << escaped(wire);
    ASSERT_TRUE(j.at("values").is_array());
    ASSERT_EQ(5u, j.at("values").size());

    //STRINGS, not JSON numbers: 3 is not "3".
    for (const Json &v: j.at("values"))
    {
        EXPECT_TRUE(v.is_string()) << v.dump();
        EXPECT_FALSE(v.is_number()) << v.dump();
    }
    EXPECT_EQ("11", j.at("values")[0].get<string>());
    EXPECT_EQ("2222", j.at("values")[1].get<string>());
    EXPECT_EQ("333", j.at("values")[2].get<string>());
    EXPECT_EQ("44444", j.at("values")[3].get<string>());
    EXPECT_EQ("5", j.at("values")[4].get<string>());
}

//WHAT THE RECEIVER MAKES OF IT, end to end. Before the fix this case asserted
//that both lists came back EMPTY: json_array_foreach() over an absent key
//iterates zero times, so calaos_wago handed WagoCtrl an empty vector together
//with the count it had been told - and WagoCtrl::write_multiple_bits() reads
//values[i] for i in [0, count), out of bounds. The values now cross.
//⚠️ The count/size MISMATCH itself is NOT closed by this ticket: WagoCtrl
//still trusts `count` blindly. It is out of perimeter (WagoCtrl.cpp) and
//written up in FINDINGS.md as F-WAGO-2.
TEST(WagoWire, TheWriteMultipleValuesNowReachTheReceiver)
{
    vector<string> values;
    ASSERT_TRUE(wireDecodeValues(wireWriteBits(FX_ID, FX_ADDRESS, FX_COUNT,
                                               requestBitValues()), values));
    ASSERT_EQ(4u, values.size());
    EXPECT_EQ("true", values[0]);
    EXPECT_EQ("false", values[1]);
    EXPECT_EQ("false", values[2]);
    EXPECT_EQ("true", values[3]);

    vector<string> wvalues;
    ASSERT_TRUE(wireDecodeValues(wireWriteWords(FX_ID, FX_ADDRESS, FX_COUNT,
                                                requestWordValues()), wvalues));
    ASSERT_EQ(5u, wvalues.size());
    EXPECT_EQ("11", wvalues[0]);
    EXPECT_EQ("2222", wvalues[1]);
    EXPECT_EQ("333", wvalues[2]);
    EXPECT_EQ("44444", wvalues[3]);
    EXPECT_EQ("5", wvalues[4]);

    //and the flattened Params now carries the key too, empty-valued, which is
    //the house rule for an array - present is not the same answer as absent.
    Params p;
    ASSERT_TRUE(wireDecode(wireWriteBits(FX_ID, FX_ADDRESS, FX_COUNT,
                                         requestBitValues()), p));
    EXPECT_EQ(5, p.size());
    EXPECT_EQ("write_bits", p["action"]);
    EXPECT_EQ("7", p["count"]);
    EXPECT_TRUE(p.Exists("values"));
    EXPECT_EQ("", p["values"]);
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
    //ECHOED from the request, NOT recomputed: neither 7 (the request fixture of
    //the other cases) nor 5 (the number of values below). Mutation N1 -
    //Utils::to_string(values.size()) in place of the echo - is red here.
    EXPECT_EQ("3", j.at("count").get<string>());
    EXPECT_NE(Utils::to_string(j.at("values").size()), j.at("count").get<string>());
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
    //MOVED BY E4.1h, and this is the whole of the move: the keys used to come
    //out in INSERTION order (id, action, address, count, status, values), they
    //now come out SORTED. Not one other byte of this string changed.
    EXPECT_EQ("{\"action\":\"read_words\","
              "\"address\":\"4242\","
              "\"count\":\"3\","
              "\"id\":\"id-7f3a91-cmd\","
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
    //nkeys: 4 everywhere, 5 for the two multiple-write requests since the fix
    //put their "values" array back on the wire.
    struct { string wire; string action; bool hasCount; int nkeys; } cases[] = {
        { wireReadBits(FX_ID, FX_ADDRESS, FX_COUNT), "read_bits", true, 4 },
        { wireReadOutputBits(FX_ID, FX_ADDRESS, FX_COUNT), "read_output_bits", true, 4 },
        { wireWriteBit(FX_ID, FX_ADDRESS, true), "write_bit", false, 4 },
        { wireWriteBits(FX_ID, FX_ADDRESS, FX_COUNT, requestBitValues()), "write_bits", true, 5 },
        { wireReadWords(FX_ID, FX_ADDRESS, FX_COUNT), "read_words", true, 4 },
        { wireReadOutputWords(FX_ID, FX_ADDRESS, FX_COUNT), "read_output_words", true, 4 },
        { wireWriteWord(FX_ID, FX_ADDRESS, FX_WORDVAL), "write_word", false, 4 },
        { wireWriteWords(FX_ID, FX_ADDRESS, FX_COUNT, requestWordValues()), "write_words", true, 5 },
    };

    for (const auto &c: cases)
    {
        Params p;
        ASSERT_TRUE(wireDecode(c.wire, p)) << c.action;
        EXPECT_EQ(c.action, p["action"]) << c.action;
        EXPECT_EQ(FX_ID, p["id"]) << c.action;
        EXPECT_EQ("4242", p["address"]) << c.action;
        EXPECT_EQ(c.nkeys, p.size()) << c.action;
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
    EXPECT_EQ("3", p["count"]);            //echoed, and NOT values.size()
    EXPECT_EQ("4242", p["address"]);
}

TEST(WagoWire, TheServerDecodesTheReadBitsValuesInOrder)
{
    Params req;
    req.Add("id", FX_ID);
    req.Add("action", "read_bits");
    req.Add("address", Utils::to_string(FX_ADDRESS));
    req.Add("count", "9");   //again NOT the number of values below

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
    EXPECT_NE(string::npos, wire.find("\\u00e9")) << escaped(wire);
    EXPECT_NE(string::npos, wire.find("\\u00c0")) << escaped(wire);
    EXPECT_EQ(string::npos, wire.find("\\u00E9")) << escaped(wire);
    EXPECT_EQ(string::npos, wire.find("\\u00C0")) << escaped(wire);

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
    //nlohmann the key is THERE, with the bad byte turned into U+FFFD - the
    //wire no longer lies about which fields were sent.
    //
    //THIS IS WHERE THE THREE SPELLINGS SEPARATE, and why the assertions below
    //are on the value and on the bytes rather than on "it did not throw":
    //  replace : one U+FFFD per rejected byte -> everything below holds;
    //  ignore  : the byte DISAPPEARS, the id reads "id--tail" -> the EXPECT_EQ
    //            on the value and the find("\\ufffd") both fail;
    //  strict  : dump() throws type_error.316 -> the ASSERT_NO_THROW fails.
    EXPECT_EQ(2u, j.size()) << escaped(wire);
    ASSERT_TRUE(j.contains("id")) << escaped(wire);
    EXPECT_EQ("id-\xef\xbf\xbd-tail", j.at("id").get<string>()) << escaped(wire);
    EXPECT_NE(string::npos, wire.find("\\ufffd")) << escaped(wire);
    EXPECT_EQ(string::npos, wire.find("id--tail")) << escaped(wire);
    EXPECT_EQ("true", j.at("status").get<string>());
}

/*----------------------------------------------------------------------------
 * T3.31 - THE POSITIONAL-ARGUMENT HOLE, ASKED OF THE TYPE SYSTEM AT RUNTIME.
 *
 * Every case above exercises a CORRECTLY ordered call. None of them can ever
 * fail on a call site that swapped two arguments, because a swapped call site
 * is not in this binary: WagoWire_test_LDADD carries libcalaos_common and
 * gtest and NO server object at all, so muting WagoMap.cpp changes nothing
 * here. That is measured, not supposed - E4.1h swapped `address` and `nb` at
 * WagoMap.cpp and this suite stayed 31/31 GREEN with no warning, because
 * Utils::UWord and int convert into one another in silence.
 *
 * These cases therefore do NOT test behaviour. They ask a question the
 * compiler can answer and gtest can report: CAN a caller still hand this
 * builder its arguments in the wrong order? std::is_invocable_v answers
 * exactly that, at compile time, and EXPECT_FALSE turns the answer into a
 * PASS/FAIL line instead of a broken build.
 *
 * ⚠️ This is a COMPILATION property reported through an executable oracle. It
 * is not a behavioural oracle and must not be sold as one: it proves that a
 * permuted call no longer TYPE-CHECKS, it proves nothing about what the
 * builder emits. What the builder emits is pinned by the thirty-one cases
 * above, and they are untouched.
 *
 * ⚠️ It does NOT close the residual of wrapping the WRONG variable:
 * buildWriteWordRequest(id, Address(value), WordValue(address)) type-checks
 * and always will. See docs/refactoring/T3.31.md section 3.
 *
 * The probed argument lists below are the PERMUTED ones, spelled out:
 *   - the two single writes carry (address, value) of the SAME type, so the
 *     permuted list is identical to the correct one and one probe covers it;
 *   - the four reads and the two multiple writes carry (address, count) as
 *     (UWord, int), so the permuted list is (int, UWord).
 *--------------------------------------------------------------------------*/

namespace
{

using ReadBitsFn        = decltype(&WagoWire::buildReadBitsRequest);
using ReadOutputBitsFn  = decltype(&WagoWire::buildReadOutputBitsRequest);
using ReadWordsFn       = decltype(&WagoWire::buildReadWordsRequest);
using ReadOutputWordsFn = decltype(&WagoWire::buildReadOutputWordsRequest);
using WriteBitFn        = decltype(&WagoWire::buildWriteBitRequest);
using WriteWordFn       = decltype(&WagoWire::buildWriteWordRequest);
using WriteBitsFn       = decltype(&WagoWire::buildWriteBitsRequest);
using WriteWordsFn      = decltype(&WagoWire::buildWriteWordsRequest);

} //namespace

/* The two RED ones of the whole family: address and value are the same width
 * and go out as a WRITE. A permutation here does not read the wrong register,
 * it DRIVES A RELAY at whatever address the value happened to be. This is the
 * pair F-WAGO-7 names, one hop above WagoCtrl::write_single_word(). */
TEST(WagoWire, TheTwoSingleWritesRefuseABareAddressAndValuePair)
{
    EXPECT_FALSE((std::is_invocable_v<WriteWordFn, const string &, UWord, UWord>))
        << "buildWriteWordRequest() still takes two bare UWord: at WagoMap.cpp "
           "the address and the value are interchangeable in total silence";

    EXPECT_FALSE((std::is_invocable_v<WriteBitFn, const string &, bool, UWord>))
        << "buildWriteBitRequest() still takes a bare UWord and a bare bool: "
           "bool and UWord convert both ways, so the permutation compiles";

    //The counterpart: the typed forms ARE accepted. Without these, a
    //signature that accepted nothing would pass the two above for free.
    EXPECT_TRUE((std::is_invocable_v<WriteWordFn, const string &,
                                     WagoTypes::Address, WagoTypes::WordValue>));
    EXPECT_TRUE((std::is_invocable_v<WriteBitFn, const string &,
                                     WagoTypes::Address, WagoTypes::BitValue>));

    //⭐ And the permutation OF THE TYPED FORM, which is the whole point.
    EXPECT_FALSE((std::is_invocable_v<WriteWordFn, const string &,
                                      WagoTypes::WordValue, WagoTypes::Address>))
        << "the address and the value of a single word write are still "
           "interchangeable - this is the pair F-WAGO-7 names";
    EXPECT_FALSE((std::is_invocable_v<WriteBitFn, const string &,
                                      WagoTypes::BitValue, WagoTypes::Address>));
}

/* The four reads: (UWord address, int count). Different types, permutable all
 * the same - this is exactly E4.1h's mutation M6, which left the suite green. */
TEST(WagoWire, TheFourReadsRefuseAPermutedAddressAndCount)
{
    EXPECT_FALSE((std::is_invocable_v<ReadBitsFn, const string &, int, UWord>))
        << "buildReadBitsRequest(id, count, address) still type-checks";
    EXPECT_FALSE((std::is_invocable_v<ReadOutputBitsFn, const string &, int, UWord>))
        << "buildReadOutputBitsRequest(id, count, address) still type-checks";
    EXPECT_FALSE((std::is_invocable_v<ReadWordsFn, const string &, int, UWord>))
        << "buildReadWordsRequest(id, count, address) still type-checks";
    EXPECT_FALSE((std::is_invocable_v<ReadOutputWordsFn, const string &, int, UWord>))
        << "buildReadOutputWordsRequest(id, count, address) still type-checks";

    EXPECT_TRUE((std::is_invocable_v<ReadBitsFn, const string &,
                                     WagoTypes::Address, WagoTypes::Count>));
    EXPECT_FALSE((std::is_invocable_v<ReadBitsFn, const string &,
                                      WagoTypes::Count, WagoTypes::Address>))
        << "address and count are still interchangeable - this is E4.1h's M6";
}

/* The two multiple writes. Their chain is dead end to end (T3.30 section 6.1:
 * WagoMap::write_multiple_* has no caller), so this is the cheapest of the
 * eight to get right and the one most likely to be got wrong the day someone
 * revives it. */
TEST(WagoWire, TheTwoMultipleWritesRefuseAPermutedAddressAndCount)
{
    EXPECT_FALSE((std::is_invocable_v<WriteBitsFn, const string &, int, UWord,
                                      const vector<bool> &>))
        << "buildWriteBitsRequest(id, count, address, values) still type-checks";
    EXPECT_FALSE((std::is_invocable_v<WriteWordsFn, const string &, int, UWord,
                                      const vector<UWord> &>))
        << "buildWriteWordsRequest(id, count, address, values) still type-checks";

    EXPECT_TRUE((std::is_invocable_v<WriteBitsFn, const string &,
                                     WagoTypes::Address, WagoTypes::Count,
                                     const vector<bool> &>));
    EXPECT_FALSE((std::is_invocable_v<WriteBitsFn, const string &,
                                      WagoTypes::Count, WagoTypes::Address,
                                      const vector<bool> &>));
}

/* T3.31 - ⭐ THE SHAPE OF THE WRAPPERS, ASKED OF THE TYPE SYSTEM.
 *
 * The three cases above would still pass if someone "simplified" these types
 * into non-explicit wrappers, or gave them a common base, or added a
 * conversion operator back to the scalar - because they only probe the bare
 * scalar lists. These four probes pin the properties that make the closure
 * hold, each one measured against a workaround that defeats it
 * (docs/refactoring/T3.31.md section 7.3):
 *
 *   W1  a non-explicit constructor lets a bare scalar convert in on its own
 *   W4  a shared base lets sibling wrappers stand in for one another
 *   W5  copy-list-init f({a},{b}) writes the permutation in short form
 *   W6  a conversion operator back to the scalar re-arms everything
 */
TEST(WagoWire, TheWrapperShapeIsTheThingThatCloses)
{
    //W1 - the constructors are explicit, so a bare scalar is not an Address.
    EXPECT_FALSE((std::is_convertible_v<UWord, WagoTypes::Address>));
    EXPECT_FALSE((std::is_convertible_v<UWord, WagoTypes::WordValue>));
    EXPECT_FALSE((std::is_convertible_v<int, WagoTypes::Count>));
    EXPECT_FALSE((std::is_convertible_v<bool, WagoTypes::BitValue>));
    EXPECT_TRUE((std::is_constructible_v<WagoTypes::Address, UWord>));

    //W4 - no common base, so no sibling can stand in for another.
    EXPECT_FALSE((std::is_convertible_v<WagoTypes::WordValue, WagoTypes::Address>));
    EXPECT_FALSE((std::is_convertible_v<WagoTypes::Address, WagoTypes::WordValue>));
    EXPECT_FALSE((std::is_convertible_v<WagoTypes::Count, WagoTypes::Address>));
    EXPECT_FALSE((std::is_base_of_v<WagoTypes::Address, WagoTypes::WordValue>));

    //W6 - and no way back to the raw scalar without naming .v.
    EXPECT_FALSE((std::is_convertible_v<WagoTypes::Address, UWord>));
    EXPECT_FALSE((std::is_convertible_v<WagoTypes::Count, int>));

    //W5 is the same statement as W1 for a one-argument constructor:
    //is_convertible_v is exactly copy-initialisation, which is what {a}
    //performs at a call site.
}

/*******************************************************************************
 * T3.33 - WHAT calaos_wago HANDS TO THE PLC.
 *
 * Everything above this line is about the BYTES of the wire. This block is
 * about the six ARGUMENT TUPLES that leave calaos_wago and enter WagoCtrl,
 * i.e. modbus, i.e. the relays - and nothing held any of that before.
 *
 * WHY A SEAM AND NOT THE PRODUCTION CODE, measured rather than assumed:
 * WagoProcess::messageReceived() is a protected member of a class declared in
 * WagoExternProc_main.cpp, and that translation unit ends on
 * EXTERN_PROC_CLIENT_MAIN(WagoProcess), which defines the main() of
 * calaos_wago. It cannot be linked next to gtest_main. So the characterization
 * commit carries the decode VERBATIM here, and the fix commit rewires
 * wagoDispatch() onto WagoWire::decodeRequest() - the same move E4.1h made for
 * the builders. Every assertion that moves with the rewiring is flagged
 * _DECLARED_DELTA in place.
 *
 * ⭐ THE PRE-SEEDED PATTERN, and why this suite cannot do without it.
 * Production declares six pairs of locals with NO INITIALISER and hands them
 * to Utils::from_string(). An uninitialised int is very often 0 in practice,
 * so a test that only checks "the address is not 4242" passes by accident. The
 * destinations here are therefore fields of BusCall PRE-SEEDED at 0x5555 -
 * 21845, the DMX channel calaos_ola drove through this exact defect - which is
 * a value no part of this wire ever produces. An assertion that names 0x5555
 * can only be answered by the decode.
 ******************************************************************************/

namespace
{

const UWord T33_SEED_ADDRESS = 0x5555;
const int   T33_SEED_COUNT   = 0x5555;
const UWord T33_SEED_VALUE   = 0x5555;

//Neither zero (what a naive initialisation would produce) nor a substring of
//the count or of the value.
const UWord T33_ADDRESS = 0x2A3F;
const int   T33_COUNT   = 6;
const UWord T33_VALUE   = 0x1234;

//The whole of what calaos_wago is about to put on modbus, plus whether it got
//that far at all and what it answered calaos_server.
struct BusCall
{
    bool reached = false;
    string call;
    UWord address = T33_SEED_ADDRESS;
    int count = T33_SEED_COUNT;
    UWord wordValue = T33_SEED_VALUE;
    bool bitValue = false;
    vector<bool> bits;
    vector<UWord> words;

    bool replied = false;
    bool replyStatus = true;
};

/* T3.33 fix commit: this body was the verbatim copy of
 * WagoExternProc_main.cpp:56-255; the DECODE is now the shipped
 * WagoWire::decodeRequest(), so a mutation of the production text turns this
 * suite red. What stays here is the shape of messageReceived() around it, with
 * the six wago->... calls replaced by a record of the arguments they were
 * about to hand to modbus. The modbus calls are assumed to succeed: what is
 * under test is what is HANDED to them, not what they answer. */
void wagoDispatch(const string &msg, BusCall &bus)
{
    Params jsonData;
    vector<string> values;

    if (!WagoWire::decodeMessage(msg, jsonData, &values))
        return;

    WagoWire::Request req;
    const WagoWire::Decoded decoded = WagoWire::decodeRequest(jsonData, values, req);

    if (decoded == WagoWire::Decoded::NoSuchAction)
        return;

    if (decoded == WagoWire::Decoded::Refused)
    {
        bus.replied = true;
        bus.replyStatus = false;
        return;
    }

    bus.address = req.address;

    switch (req.command)
    {
    case WagoWire::Request::ReadBits:
        bus.count = req.count;
        bus.call = "read_bits";
        break;
    case WagoWire::Request::ReadWords:
        bus.count = req.count;
        bus.call = "read_words";
        break;
    case WagoWire::Request::WriteBit:
        bus.bitValue = req.bitValue;
        bus.call = "write_single_bit";
        break;
    case WagoWire::Request::WriteBits:
        bus.count = req.count;
        bus.bits = req.bits;
        bus.call = "write_multiple_bits";
        break;
    case WagoWire::Request::WriteWord:
        bus.wordValue = req.wordValue;
        bus.call = "write_single_word";
        break;
    case WagoWire::Request::WriteWords:
        bus.count = req.count;
        bus.words = req.words;
        bus.call = "write_multiple_words";
        break;
    case WagoWire::Request::None:
        break;
    }

    bus.reached = true;
    bus.replied = true;
}

//A request built key by key, so that a key can be left OUT or left EMPTY -
//neither of which the eight production builders can express.
string rawRequest(const string &body)
{
    return "{" + body + "}";
}

} //namespace

/*------------------------------------------------------------------------------
 * ⭐ THE DEFECT, one case per action branch. Six branches, six disjoint sets:
 * removing the guard of one branch turns exactly one of these red.
 *----------------------------------------------------------------------------*/

TEST(WagoWireBus, AReadBitsRequestWithNoAddressIsRefused_DECLARED_DELTA)
{
    BusCall bus;
    wagoDispatch(rawRequest("\"action\":\"read_bits\",\"id\":\"x\",\"count\":\"6\""), bus);

    //DECLARED DELTA 1, moved: it used to be sent anyway, to modbus register 0.
    EXPECT_FALSE(bus.reached);
    EXPECT_EQ(T33_SEED_ADDRESS, bus.address) << "the destination is not even written";
    EXPECT_TRUE(bus.replied) << "calaos_server must not be left waiting";
    EXPECT_FALSE(bus.replyStatus);
}

TEST(WagoWireBus, AReadWordsRequestWithAnEmptyAddressIsRefused_DECLARED_DELTA)
{
    BusCall bus;
    wagoDispatch(rawRequest("\"action\":\"read_words\",\"id\":\"x\",\"address\":\"\",\"count\":\"6\""), bus);

    //DECLARED DELTA 2. ⭐ The key is PRESENT and EMPTY - the real shape, since
    //Params::operator[] answers "" for an absent key too.
    EXPECT_FALSE(bus.reached);
    EXPECT_EQ(T33_SEED_ADDRESS, bus.address);
}

TEST(WagoWireBus, AWriteBitRequestWithNoAddressIsRefused_DECLARED_DELTA)
{
    BusCall bus;
    wagoDispatch(rawRequest("\"action\":\"write_bit\",\"id\":\"x\",\"value\":\"true\""), bus);

    //DECLARED DELTA 3. This one WRITES: coil 0 of the PLC is a physical relay.
    EXPECT_FALSE(bus.reached);
    EXPECT_EQ("", bus.call);
    EXPECT_TRUE(bus.replied);
    EXPECT_FALSE(bus.replyStatus);
}

TEST(WagoWireBus, AWriteBitsRequestWithNoCountIsRefused_DECLARED_DELTA)
{
    BusCall bus;
    wagoDispatch(rawRequest("\"action\":\"write_bits\",\"id\":\"x\",\"address\":\"10815\","
                            "\"values\":[\"true\",\"false\"]"), bus);

    //DECLARED DELTA 4. The ADDRESS was readable here: it is the count that
    //refuses the frame, and a partly readable request is refused whole.
    EXPECT_FALSE(bus.reached);
    EXPECT_EQ(T33_SEED_COUNT, bus.count);
}

TEST(WagoWireBus, AWriteWordRequestWithAnUnreadableValueIsRefused_DECLARED_DELTA)
{
    BusCall bus;
    wagoDispatch(rawRequest("\"action\":\"write_word\",\"id\":\"x\",\"address\":\"10815\","
                            "\"value\":\"nope\""), bus);

    //DECLARED DELTA 5. The address was the one that was asked for and the
    //PAYLOAD was invented: a register preset to zero, silently.
    EXPECT_FALSE(bus.reached);
    EXPECT_EQ(T33_SEED_VALUE, bus.wordValue);

    //And the overflow regime T3.25 measured: 70000 saturates a UWord, which is
    //a value the caller never asked for either.
    BusCall over;
    wagoDispatch(rawRequest("\"action\":\"write_word\",\"id\":\"x\",\"address\":\"10815\","
                            "\"value\":\"70000\""), over);
    EXPECT_FALSE(over.reached);
}

TEST(WagoWireBus, AWriteWordsRequestWithAnUnreadableEntryIsRefused_DECLARED_DELTA)
{
    BusCall bus;
    wagoDispatch(rawRequest("\"action\":\"write_words\",\"id\":\"x\",\"address\":\"10815\","
                            "\"count\":\"3\",\"values\":[\"11\",\"nope\",\"333\"]"), bus);

    //DECLARED DELTA 6. The unreadable entry used to become register content -
    //a zero written between two values the caller did ask for.
    EXPECT_FALSE(bus.reached);
    EXPECT_TRUE(bus.words.empty());
}

/*------------------------------------------------------------------------------
 * ⭐ THE WITNESS. Green before AND after the fix, and it is the one that says
 * the pre-seeded pattern is doing its job: T3.25 made from_string() write its
 * destination on every path, so 0x5555 no longer survives a failed decode.
 * If this ever turns red, from_string() has gone back to leaving `dest` alone
 * and every uninitialised local of this file is live again.
 *----------------------------------------------------------------------------*/
TEST(WagoWireBus, TheSeededPatternNeverReachesThePlc)
{
    const char *const bodies[] = {
        "\"action\":\"read_bits\",\"id\":\"x\"",
        "\"action\":\"read_words\",\"id\":\"x\",\"address\":\"\",\"count\":\"\"",
        "\"action\":\"write_bit\",\"id\":\"x\"",
        "\"action\":\"write_bits\",\"id\":\"x\"",
        "\"action\":\"write_word\",\"id\":\"x\"",
        "\"action\":\"write_words\",\"id\":\"x\",\"values\":[\"\"]",
    };

    for (const char *const body: bodies)
    {
        BusCall bus;
        wagoDispatch(rawRequest(body), bus);

        if (bus.reached)
            EXPECT_NE(T33_SEED_ADDRESS, bus.address) << body;
        else
            EXPECT_EQ(T33_SEED_ADDRESS, bus.address)
                << body << ": a refused request must not decode into its destination";
    }
}

/*------------------------------------------------------------------------------
 * THE ACQUIS. A well formed request behaves exactly as it always has - the
 * non-regression half, and it counts as much as the six above.
 *----------------------------------------------------------------------------*/

TEST(WagoWireBus, AWellFormedReadBitsRequestIsUnchanged)
{
    BusCall bus;
    wagoDispatch(WagoWire::buildReadBitsRequest(FX_ID, WagoTypes::Address(T33_ADDRESS),
                                                WagoTypes::Count(T33_COUNT)), bus);

    EXPECT_TRUE(bus.reached);
    EXPECT_EQ("read_bits", bus.call);
    EXPECT_EQ(T33_ADDRESS, bus.address);
    EXPECT_EQ(T33_COUNT, bus.count);
    EXPECT_TRUE(bus.replied);
}

//The 0x200 output image offset is applied by calaos_wago and never travels on
//the wire. It is the one arithmetic this dispatch does.
TEST(WagoWireBus, AWellFormedReadOutputBitsRequestStillGetsTheOutputOffset)
{
    BusCall bus;
    wagoDispatch(WagoWire::buildReadOutputBitsRequest(FX_ID, WagoTypes::Address(T33_ADDRESS),
                                                      WagoTypes::Count(T33_COUNT)), bus);

    EXPECT_TRUE(bus.reached);
    EXPECT_EQ(T33_ADDRESS + 0x200, bus.address);
    EXPECT_EQ(T33_COUNT, bus.count);
}

TEST(WagoWireBus, AWellFormedReadOutputWordsRequestStillGetsTheOutputOffset)
{
    BusCall bus;
    wagoDispatch(WagoWire::buildReadOutputWordsRequest(FX_ID, WagoTypes::Address(T33_ADDRESS),
                                                       WagoTypes::Count(T33_COUNT)), bus);

    EXPECT_TRUE(bus.reached);
    EXPECT_EQ("read_words", bus.call);
    EXPECT_EQ(T33_ADDRESS + 0x200, bus.address);
}

TEST(WagoWireBus, AWellFormedWriteBitRequestIsUnchanged)
{
    BusCall bus;
    wagoDispatch(WagoWire::buildWriteBitRequest(FX_ID, WagoTypes::Address(T33_ADDRESS),
                                                WagoTypes::BitValue(true)), bus);

    EXPECT_TRUE(bus.reached);
    EXPECT_EQ("write_single_bit", bus.call);
    EXPECT_EQ(T33_ADDRESS, bus.address);
    EXPECT_TRUE(bus.bitValue);
}

TEST(WagoWireBus, AWellFormedWriteWordRequestIsUnchanged)
{
    BusCall bus;
    wagoDispatch(WagoWire::buildWriteWordRequest(FX_ID, WagoTypes::Address(T33_ADDRESS),
                                                 WagoTypes::WordValue(T33_VALUE)), bus);

    EXPECT_TRUE(bus.reached);
    EXPECT_EQ("write_single_word", bus.call);
    EXPECT_EQ(T33_ADDRESS, bus.address);
    EXPECT_EQ(T33_VALUE, bus.wordValue);
}

TEST(WagoWireBus, AWellFormedWriteBitsRequestIsUnchanged)
{
    BusCall bus;
    wagoDispatch(WagoWire::buildWriteBitsRequest(FX_ID, WagoTypes::Address(T33_ADDRESS),
                                                 WagoTypes::Count(T33_COUNT),
                                                 requestBitValues()), bus);

    EXPECT_TRUE(bus.reached);
    EXPECT_EQ(T33_ADDRESS, bus.address);
    EXPECT_EQ(T33_COUNT, bus.count);
    EXPECT_EQ(requestBitValues(), bus.bits);
}

TEST(WagoWireBus, AWellFormedWriteWordsRequestIsUnchanged)
{
    BusCall bus;
    wagoDispatch(WagoWire::buildWriteWordsRequest(FX_ID, WagoTypes::Address(T33_ADDRESS),
                                                  WagoTypes::Count(T33_COUNT),
                                                  requestWordValues()), bus);

    EXPECT_TRUE(bus.reached);
    EXPECT_EQ(T33_ADDRESS, bus.address);
    EXPECT_EQ(T33_COUNT, bus.count);
    EXPECT_EQ(requestWordValues(), bus.words);
}

//An action nobody implements has always been answered with SILENCE - no bus
//call and no reply at all. Pinned so that adding a refusal path cannot turn
//this into a spurious failed status.
TEST(WagoWireBus, AnUnknownActionTouchesNothingAndAnswersNothing)
{
    BusCall bus;
    wagoDispatch(rawRequest("\"action\":\"reboot_plc\",\"id\":\"x\",\"address\":\"10815\""), bus);

    EXPECT_FALSE(bus.reached);
    EXPECT_FALSE(bus.replied);
}

//Malformed JSON is refused by decodeMessage() and the sidecar returns without
//dispatching. Pinned here because the T3.33 refusal path must not change it.
TEST(WagoWireBus, MalformedJsonTouchesNothing)
{
    BusCall bus;
    wagoDispatch("{\"action\":\"write_bit\"", bus);

    EXPECT_FALSE(bus.reached);
    EXPECT_FALSE(bus.replied);
}

/* ⭐ A REFUSAL IS NOT A DEATH. The sidecar has one dispatch loop for every
 * message it will ever receive: if refusing one left it wedged, the whole PLC
 * would go dark on a single bad frame. Six refusals in a row, then the frame
 * that follows them still reaches modbus with the address it asked for. */
TEST(WagoWireBus, TheDispatchKeepsServingAfterASequenceOfRefusals)
{
    const char *const refused[] = {
        "\"action\":\"read_bits\",\"id\":\"x\"",
        "\"action\":\"read_words\",\"id\":\"x\",\"address\":\"\"",
        "\"action\":\"write_bit\",\"id\":\"x\"",
        "\"action\":\"write_bits\",\"id\":\"x\",\"address\":\"1\"",
        "\"action\":\"write_word\",\"id\":\"x\",\"address\":\"1\",\"value\":\"nope\"",
        "\"action\":\"write_words\",\"id\":\"x\",\"address\":\"1\",\"count\":\"1\",\"values\":[\"nope\"]",
    };

    for (const char *const body: refused)
    {
        BusCall bus;
        wagoDispatch(rawRequest(body), bus);
        ASSERT_FALSE(bus.reached) << body;
        EXPECT_TRUE(bus.replied) << body << ": the pending command must be released";

        BusCall next;
        wagoDispatch(WagoWire::buildWriteWordRequest(FX_ID, WagoTypes::Address(T33_ADDRESS),
                                                     WagoTypes::WordValue(T33_VALUE)), next);
        EXPECT_TRUE(next.reached) << body;
        EXPECT_EQ(T33_ADDRESS, next.address) << body;
        EXPECT_EQ(T33_VALUE, next.wordValue) << body;
    }
}
