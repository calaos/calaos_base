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

/*
 * E4.1g - CHARACTERIZATION OF THE MQTT WIRE, WRITTEN BEFORE ANY LINE OF src/.
 *
 * The MQTT wire has two ends and both are ours: MqttCtrl.cpp (calaos_server)
 * and MqttExternProc_main.cpp (calaos_mqtt). Nothing in the existing suite
 * executes either of them - E4.0d measured that no driver is exercised at all -
 * so this file is the only net this migration will ever have.
 *
 * WHY A LOCAL COPY OF THE PRODUCTION CODE.
 * The code under characterization lives in an anonymous namespace inside a
 * program that defines main() (EXTERN_PROC_CLIENT_MAIN), and inside a lambda
 * built by a constructor that spawns a process. Neither is reachable from a
 * test binary today. So the characterization commit did the only honest thing
 * available: it copied the jansson implementation VERBATIM into a namespace
 * legacy, measured its answers, and froze them as literals. THIS commit moves
 * the code to src/bin/calaos_server/IO/Mqtt/MqttWire.h, where a test can
 * finally reach the real thing, deletes namespace legacy and repoints the
 * five SEAM functions. Every assertion is byte for byte the one written
 * before src/ was touched, except the four declared deltas below.
 *
 * DECLARED DELTAS (and only these) - each one is marked in place with a
 * DELTA comment, so a reviewer can grep them:
 *   D1  Wire form. The server end already dumps with JSON_COMPACT |
 *       JSON_ENSURE_ASCII; the calaos_mqtt end dumps with json_dumps(root, 0),
 *       i.e. NEITHER compact NOR ascii-only, so today it puts raw UTF-8 bytes
 *       on the wire. E4.1 invariant 3 makes every dump ensure_ascii = true, so
 *       the calaos_mqtt end becomes compact and ascii-only, and the hex of the
 *       escapes goes from upper case (jansson) to lower case (nlohmann).
 *   D2  Key order. nlohmann::json sorts keys; jansson kept insertion order.
 *       "payload" now comes before "topic". Assumed, epic invariant 1.
 *   D3  A payload carrying a NUL byte is delivered instead of being dropped.
 *       Measured: json_dumps() correctly writes [u+0000], but the consuming
 *       json_loads() REFUSES it ("[u+0000] is not allowed without
 *       JSON_ALLOW_NUL"), so the whole message is thrown away by MqttCtrl
 *       today. The careful json_stringn(data, len) of payloadToJsonString()
 *       was therefore undone at the far end. nlohmann parses [u+0000], so the
 *       payload now arrives whole. User visible -> RELEASE_NOTES.
 *   D4  Both ends now refuse a document that parses but is not an object.
 *       MqttExternProc_main.cpp:218 already did; MqttCtrl.cpp:55 only tested
 *       the NULL, so a bare "42" walked on with an empty Params and stored
 *       messages[""] = "". Not user visible, and it makes the two ends of one
 *       wire agree.
 *
 * WHAT IS *NOT* A DELTA, and is pinned here so nobody turns it into one:
 *   - the LENGTH of the payload. json_string() truncates at the first NUL;
 *     json_stringn(data, len) does not. Every payload case below asserts the
 *     length, not only the content.
 *   - the '?' substitution. "MQTT non-UTF8 payloads are delivered with the
 *     invalid bytes replaced by '?'" is a user decision already shipped
 *     (RELEASE_NOTES, section Fiabilite). It stays '?', byte for byte, one
 *     '?' per rejected byte. It is NOT replaced by U+FFFD.
 *   - a payload that is VALID but non-ASCII (accents, emoji) must cross
 *     untouched: it is not on the '?' path at all. The two look alike and are
 *     easy to confuse, so both are pinned, and the valid non-ASCII one is
 *     what gives ensure_ascii an oracle: it asserts on the BYTES of the wire,
 *     not on a parsed document. A semantic oracle is blind to escaping by
 *     construction - that is exactly the hole this test closes.
 */

#include <gtest/gtest.h>

#include "Params.h"
#include "Utils.h"
#include "MqttWire.h"

using namespace std;

namespace
{


/* ------------------------------------------------------------------------ *
 * SEAM. Repointed at MqttWire.h by the migration commit; namespace legacy,
 * which held the verbatim jansson code, is gone with it. Everything below
 * this point is unchanged apart from the four DELTA sites.
 * ------------------------------------------------------------------------ */
string wirePayloadToString(const void *p, int n)
{ return MqttWire::payloadToString(p, n); }

string wireEncodeMessageServerSide(const string &topic, const string &payload)
{ return MqttWire::encodeMessage(topic, payload); }

string wireEncodeMessageProcSide(const string &topic, const void *p, int n)
{ return MqttWire::encodeMessage(topic, MqttWire::payloadToString(p, n)); }

string wireEncodeConfig(const Params &cfg)
{ return MqttWire::encodeConfig(cfg); }

bool wireDecodeMessage(const string &msg, Params &out)
{ return MqttWire::decodeMessage(msg, out); }

/* ------------------------------------------------------------------------ *
 * Helpers
 * ------------------------------------------------------------------------ */

//Readable failures on binary payloads: gtest prints raw bytes unusably.
string hexOf(const string &s)
{
    static const char *H = "0123456789abcdef";
    string o;
    o.reserve(s.size() * 2);
    for (unsigned char c: s)
    {
        o += H[c >> 4];
        o += H[c & 0x0F];
    }
    return o;
}

//The corpus. Deliberately NOT a poor fixture: every payload has a distinct
//length AND distinct bytes, none is a prefix of another, and no two share an
//expected result, so swapping any two of them reddens.
const char PAY_NUL_MIDDLE[]  = { 'a', '\0', 'b' };
const char PAY_NUL_LEADING[] = { '\0', 'x', 'y' };
const char PAY_BINARY[]      = { '\x01', '\0', '\xff', 'A' };

const string TOPIC_UP   = "zigbee2mqtt/salon/lampe";      //broker -> calaos
const string TOPIC_DOWN = "zigbee2mqtt/cuisine/store/set"; //calaos -> broker

} //namespace

/* ========================================================================= *
 * 1. payloadToJsonString(): LENGTH FIRST, then bytes.
 * ========================================================================= */

TEST(MqttPayload, AsciiPayloadCrossesVerbatim)
{
    const string got = wirePayloadToString("hello", 5);
    EXPECT_EQ(5u, got.size());
    EXPECT_EQ("68656c6c6f", hexOf(got));
}

//Valid but NOT ascii. This is the payload that must NOT go down the '?' path.
TEST(MqttPayload, ValidNonAsciiPayloadCrossesVerbatimAndIsNotSubstituted)
{
    const string cafe = "caf\xc3\xa9 \xe2\x98\x95"; //"cafe(acute) [hot beverage]"
    ASSERT_EQ(9u, cafe.size());

    const string got = wirePayloadToString(cafe.data(), (int)cafe.size());
    EXPECT_EQ(9u, got.size());
    EXPECT_EQ("636166c3a920e29895", hexOf(got));
    EXPECT_EQ(string::npos, got.find('?'));
}

TEST(MqttPayload, NulInTheMiddleDoesNotTruncateThePayload)
{
    const string got = wirePayloadToString(PAY_NUL_MIDDLE, 3);
    EXPECT_EQ(3u, got.size());
    EXPECT_EQ("610062", hexOf(got));
}

TEST(MqttPayload, PayloadStartingWithNulKeepsItsWholeLength)
{
    const string got = wirePayloadToString(PAY_NUL_LEADING, 3);
    EXPECT_EQ(3u, got.size());
    EXPECT_EQ("007879", hexOf(got));
}

TEST(MqttPayload, InvalidBytesAreReplacedByOneQuestionMarkEach)
{
    const string got = wirePayloadToString("\xff\x80", 2);
    EXPECT_EQ(2u, got.size());
    EXPECT_EQ("3f3f", hexOf(got));
    EXPECT_EQ("??", got);
}

TEST(MqttPayload, EmptyPayloadStaysEmpty)
{
    const string got = wirePayloadToString("", 0);
    EXPECT_EQ(0u, got.size());
    EXPECT_EQ("", got);
}

TEST(MqttPayload, BinaryPayloadKeepsItsNulAndReplacesOnlyTheInvalidByte)
{
    const string got = wirePayloadToString(PAY_BINARY, 4);
    EXPECT_EQ(4u, got.size());
    EXPECT_EQ("01003f41", hexOf(got));
}

//Structurally well formed but rejected: overlong NUL, and a UTF-16 surrogate
//encoded as UTF-8. Both survive the structural pass, so they exercise the
//third fallback, which replaces every non-ASCII byte - including the valid
//"e acute" that sits before them. Ugly, measured, and kept as is.
TEST(MqttPayload, OverlongEncodingIsReplaced)
{
    const string got = wirePayloadToString("\xc0\x80", 2);
    EXPECT_EQ(2u, got.size());
    EXPECT_EQ("3f3f", hexOf(got));
}

TEST(MqttPayload, SurrogateSequenceFallsBackToAsciiOnlyAndTakesValidBytesWithIt)
{
    const string mixed = "caf\xc3\xa9\xed\xa0\x80";
    ASSERT_EQ(8u, mixed.size());

    const string got = wirePayloadToString(mixed.data(), (int)mixed.size());
    EXPECT_EQ(8u, got.size());
    EXPECT_EQ("6361663f3f3f3f3f", hexOf(got)); //"caf?????"
}

TEST(MqttPayload, TruncatedSequenceAtTheEndCostsOneQuestionMarkOnly)
{
    const string truncated = "caf\xc3\xa9\xed";
    ASSERT_EQ(6u, truncated.size());

    const string got = wirePayloadToString(truncated.data(), (int)truncated.size());
    EXPECT_EQ(6u, got.size());
    EXPECT_EQ("636166c3a93f", hexOf(got));
}

//Suites de revue: no 4 byte sequence was exercised at all, so the two tight
//bounds of the strict validator - F0 needs a continuation >= 0x90 (no
//overlong) and F4 needs one <= 0x8F (nothing above U+10FFFF) - were free to
//be widened without a single case going red. One valid representative on
//each side of each bound.
TEST(MqttPayload, FourByteSequencesArePinnedAtTheirExactBounds)
{
    //U+10000, the smallest 4 byte code point: valid, crosses untouched
    const string lowest = "\xf0\x90\x80\x80";
    ASSERT_EQ(4u, lowest.size());
    EXPECT_EQ(4u, wirePayloadToString(lowest.data(), 4).size());
    EXPECT_EQ("f0908080", hexOf(wirePayloadToString(lowest.data(), 4)));

    //U+10FFFF, the largest legal code point: valid, crosses untouched
    const string highest = "\xf4\x8f\xbf\xbf";
    EXPECT_EQ("f48fbfbf", hexOf(wirePayloadToString(highest.data(), 4)));

    //one past U+10FFFF: refused. Structurally well formed, so it survives the
    //sanitising pass and only the ascii-only fallback catches it.
    const string tooBig = "\xf4\x90\xbf\xbf";
    EXPECT_EQ(4u, wirePayloadToString(tooBig.data(), 4).size());
    EXPECT_EQ("3f3f3f3f", hexOf(wirePayloadToString(tooBig.data(), 4)));

    //overlong 4 byte form of a 3 byte code point: refused the same way
    const string overlong = "\xf0\x8f\xbf\xbf";
    EXPECT_EQ(4u, wirePayloadToString(overlong.data(), 4).size());
    EXPECT_EQ("3f3f3f3f", hexOf(wirePayloadToString(overlong.data(), 4)));
}

TEST(MqttPayload, NullPointerWithAPositiveLengthYieldsAnEmptyPayload)
{
    const string got = wirePayloadToString(nullptr, 5);
    EXPECT_EQ(0u, got.size());
}

TEST(MqttPayload, NegativeLengthYieldsAnEmptyPayload)
{
    const string got = wirePayloadToString("hello", -1);
    EXPECT_EQ(0u, got.size());
}

/* ========================================================================= *
 * 2. THE WIRE, ASSERTED ON BYTES.
 *
 * These are the DELTA sites D1 and D2. They are the only byte-level
 * expectations of this file and the only assertions the migration moves.
 * They exist so that ensure_ascii has an oracle: the rest of the suite
 * compares parsed documents and is blind to escaping by construction.
 * ========================================================================= */

//DELTA D1/D2 - today: json_dumps(root, 0), insertion order, raw UTF-8 bytes.
TEST(MqttWireForm, ProcSideWireIsPinnedByteForByte)
{
    const string cafe = "caf\xc3\xa9 \xe2\x98\x95";
    const string wire = wireEncodeMessageProcSide(TOPIC_UP, cafe.data(), (int)cafe.size());

    //Was, under jansson json_dumps(root, 0): neither compact nor ascii-only,
    //raw UTF-8 bytes, insertion order:
    //  {"topic": "zigbee2mqtt/salon/lampe", "payload": "caf<c3a9> <e29895>"}
    EXPECT_EQ("{\"payload\":\"caf\\u00e9 \\u2615\",\"topic\":\"zigbee2mqtt/salon/lampe\"}",
              wire);
}

//DELTA D1/D2 - today: JSON_COMPACT | JSON_ENSURE_ASCII, insertion order,
//UPPER case hex. The payload doubles as a reminder that a JSON document is a
//perfectly normal MQTT payload and must be escaped, not spliced.
TEST(MqttWireForm, ServerSideWireIsPinnedByteForByte)
{
    const string wire = wireEncodeMessageServerSide(TOPIC_DOWN, "{\"\xc3\xa9tat\":\"ON\"}");

    //Was, under jansson: same escaping but UPPER case hex and insertion order:
    //  {"topic":"...","payload":"{\\"\\u00E9tat\\":\\"ON\\"}"}
    EXPECT_EQ("{\"payload\":\"{\\\"\\u00e9tat\\\":\\\"ON\\\"}\",\"topic\":\"zigbee2mqtt/cuisine/store/set\"}",
              wire);
}

//DELTA D1/D2 - the broker configuration handed to calaos_mqtt as argv[1].
//Values are deliberately anti alphabetical on insertion (host, port,
//keepalive, user, password) so that a change of key ordering is visible.
TEST(MqttWireForm, BrokerConfigWireIsPinnedByteForByte)
{
    Params cfg;
    cfg.Add("host", "192.168.1.42");
    cfg.Add("port", "8883");
    cfg.Add("keepalive", "45");
    cfg.Add("user", "cal\xc3\xa9os");
    cfg.Add("password", "s\xe2\x82\xac" "cret");

    //Was, under jansson: insertion order and UPPER case hex:
    //  {"host":...,"port":...,"keepalive":...,"user":"cal\\u00E9os","password":"s\\u20ACcret"}
    EXPECT_EQ("{\"host\":\"192.168.1.42\",\"keepalive\":\"45\",\"password\":\"s\\u20accret\","
              "\"port\":\"8883\",\"user\":\"cal\\u00e9os\"}",
              wireEncodeConfig(cfg));
}

TEST(MqttWireForm, BrokerConfigOmitsCredentialsWhenOnlyOneOfThemIsSet)
{
    Params cfg;
    cfg.Add("host", "10.0.0.7");
    cfg.Add("port", "1883");
    cfg.Add("keepalive", "120");
    cfg.Add("user", "lonely");

    const string wire = wireEncodeConfig(cfg);
    EXPECT_EQ(string::npos, wire.find("lonely"));
    EXPECT_EQ(string::npos, wire.find("\"user\""));
    EXPECT_EQ(string::npos, wire.find("\"password\""));
}

//DELTA D2 only (key order): the three defaults are NOT a delta.
TEST(MqttWireForm, AnEmptyConfigFallsBackOnTheThreeBuiltInDefaults)
{
    const Params none;
    EXPECT_EQ("{\"host\":\"127.0.0.1\",\"keepalive\":\"120\",\"port\":\"1883\"}",
              wireEncodeConfig(none));
}

//An EMPTY value is not an override. Distinct default per field, so a fixture
//that mixed them up would show.
TEST(MqttWireForm, AnEmptyValueDoesNotOverrideItsDefault)
{
    Params cfg;
    cfg.Add("host", "");
    cfg.Add("port", "");
    cfg.Add("keepalive", "7");

    EXPECT_EQ("{\"host\":\"127.0.0.1\",\"keepalive\":\"7\",\"port\":\"1883\"}",
              wireEncodeConfig(cfg));
}

/* ------------------------------------------------------------------------ *
 * error_handler_t::replace, THE THIRD EMISSION INVARIANT, WITH A WITNESS.
 *
 * Suites de revue R2. Until these three cases existed, mutating
 * error_handler_t::replace into ::ignore left the whole file green: two of
 * the three invariants were being applied blind. The path is load bearing,
 * not theoretical - it is the ONE dump() of this wire that receives bytes
 * nobody sanitised:
 *   - MqttCtrl::publishTopic() hands its payload straight to dumpJson(). It
 *     comes from the Calaos configuration, never from payloadToString().
 *   - the topic in the calaos_mqtt lambda comes from the broker and is NOT
 *     put through payloadToString() either - deliberately, because the '?'
 *     decision is about payloads only.
 * Without the handler, dump() raises type_error.316 and, since nobody
 * catches it, TAKES calaos_server DOWN on a live connection. That is the
 * E4.0 trap, on this wire, reachable.
 *
 * The three possible spellings are three different observable behaviours,
 * so this pins the right one rather than merely "not strict":
 *   replace -> U+FFFD per invalid byte    (what we want)
 *   ignore  -> the invalid bytes VANISH   (silent truncation, measured)
 *   strict  -> throws type_error.316      (gtest reports the throw)
 * ------------------------------------------------------------------------ */

TEST(MqttWireForm, AnInvalidTopicBecomesUFFFDAndTheDumpNeverThrows)
{
    const string badTopic = "home/\xff\x80/lampe";
    ASSERT_EQ(15u, badTopic.size());

    string wire;
    ASSERT_NO_THROW(wire = wireEncodeMessageServerSide(badTopic, "ON"));

    //replace: one U+FFFD per invalid byte, escaped because ensure_ascii.
    //ignore would give "home//lampe" - the bytes would just disappear.
    EXPECT_EQ("{\"payload\":\"ON\",\"topic\":\"home/\\ufffd\\ufffd/lampe\"}", wire);
    EXPECT_NE(string::npos, wire.find("\\ufffd"));
}

//publishTopic() is the site that proves the handler is not decoration: this
//payload never goes through payloadToString(), so nothing sanitised it.
TEST(MqttWireForm, AnUnsanitisedPayloadIsReplacedNotDropped)
{
    string wire;
    ASSERT_NO_THROW(wire = wireEncodeMessageServerSide("zigbee2mqtt/cuisine/store/set",
                                                       string("\xff\x80", 2)));

    EXPECT_EQ("{\"payload\":\"\\ufffd\\ufffd\",\"topic\":\"zigbee2mqtt/cuisine/store/set\"}",
              wire);
    //and NOT the ignore behaviour, which empties the field silently
    EXPECT_EQ(string::npos, wire.find("\"payload\":\"\""));
}

//The broker configuration is built from user supplied params too.
TEST(MqttWireForm, AnInvalidCredentialIsReplacedNotDropped)
{
    Params cfg;
    cfg.Add("host", "10.0.0.7");
    cfg.Add("port", "1883");
    cfg.Add("keepalive", "120");
    cfg.Add("user", string("ra\xff\x80ul"));
    cfg.Add("password", "s3cret");

    string wire;
    ASSERT_NO_THROW(wire = wireEncodeConfig(cfg));
    EXPECT_EQ("{\"host\":\"10.0.0.7\",\"keepalive\":\"120\",\"password\":\"s3cret\","
              "\"port\":\"1883\",\"user\":\"ra\\ufffd\\ufffdul\"}",
              wire);
}

//Suites de revue: the "both or neither" guard was only pinned on the
//user-only side, so a mutation that kept the password alone stayed green.
TEST(MqttWireForm, BrokerConfigOmitsCredentialsWhenOnlyThePasswordIsSet)
{
    Params cfg;
    cfg.Add("host", "10.0.0.8");
    cfg.Add("port", "1884");
    cfg.Add("keepalive", "90");
    cfg.Add("password", "orphan");

    const string wire = wireEncodeConfig(cfg);
    EXPECT_EQ(string::npos, wire.find("orphan"));
    EXPECT_EQ(string::npos, wire.find("\"password\""));
    EXPECT_EQ(string::npos, wire.find("\"user\""));
    EXPECT_EQ("{\"host\":\"10.0.0.8\",\"keepalive\":\"90\",\"port\":\"1884\"}", wire);
}

/* ========================================================================= *
 * 3. FULL ROUND TRIP calaos_mqtt -> text -> calaos_server.
 *
 * topic and payload are never equal and never interchangeable, so swapping
 * the two fields in the fixture reddens (contre-mutation par echange).
 * ========================================================================= */

namespace
{
struct RoundTrip
{
    Params p;
    bool decoded = false;
};

RoundTrip roundTripUp(const string &topic, const void *payload, int len)
{
    RoundTrip r;
    r.decoded = wireDecodeMessage(wireEncodeMessageProcSide(topic, payload, len), r.p);
    return r;
}
} //namespace

TEST(MqttRoundTrip, AsciiPayloadArrivesWithBothFieldsDistinct)
{
    const RoundTrip r = roundTripUp(TOPIC_UP, "hello", 5);
    ASSERT_TRUE(r.decoded);
    EXPECT_EQ("zigbee2mqtt/salon/lampe", r.p["topic"]);
    EXPECT_EQ("hello", r.p["payload"]);
    EXPECT_EQ(5u, r.p["payload"].size());
}

TEST(MqttRoundTrip, ValidNonAsciiPayloadArrivesUnchanged)
{
    const string cafe = "caf\xc3\xa9 \xe2\x98\x95";
    const RoundTrip r = roundTripUp(TOPIC_UP, cafe.data(), (int)cafe.size());
    ASSERT_TRUE(r.decoded);
    EXPECT_EQ("zigbee2mqtt/salon/lampe", r.p["topic"]);
    EXPECT_EQ(9u, r.p["payload"].size());
    EXPECT_EQ("636166c3a920e29895", hexOf(r.p["payload"]));
}

TEST(MqttRoundTrip, InvalidBytesArriveAsQuestionMarks)
{
    const RoundTrip r = roundTripUp(TOPIC_UP, "\xff\x80", 2);
    ASSERT_TRUE(r.decoded);
    EXPECT_EQ("zigbee2mqtt/salon/lampe", r.p["topic"]);
    EXPECT_EQ(2u, r.p["payload"].size());
    EXPECT_EQ("??", r.p["payload"]);
}

TEST(MqttRoundTrip, EmptyPayloadArrivesEmptyAndTheTopicSurvives)
{
    const RoundTrip r = roundTripUp(TOPIC_UP, "", 0);
    ASSERT_TRUE(r.decoded);
    EXPECT_EQ("zigbee2mqtt/salon/lampe", r.p["topic"]);
    EXPECT_EQ(0u, r.p["payload"].size());
}

TEST(MqttRoundTrip, SurrogatePayloadArrivesAsAsciiQuestionMarks)
{
    const string mixed = "caf\xc3\xa9\xed\xa0\x80";
    const RoundTrip r = roundTripUp(TOPIC_UP, mixed.data(), (int)mixed.size());
    ASSERT_TRUE(r.decoded);
    EXPECT_EQ(8u, r.p["payload"].size());
    EXPECT_EQ("caf?????", r.p["payload"]);
}

//DELTA D3 - under jansson this message was THROWN AWAY by the consumer.
//payloadToJsonString() went to the trouble of preserving the NUL with
//json_stringn(), json_dumps() wrote it correctly as [u+0000], and then
//json_loads() at the other end refused the whole document:
//  "[u+0000] is not allowed without JSON_ALLOW_NUL"
//so MqttCtrl logged "Error parsing json" and dropped topic AND payload.
//nlohmann parses it, so the payload now arrives whole, which is what the
//json_stringn() was there for. Declared, user visible (RELEASE_NOTES).
TEST(MqttRoundTrip, PayloadWithANulByteArrivesWhole)
{
    const RoundTrip r = roundTripUp(TOPIC_UP, PAY_NUL_MIDDLE, 3);
    ASSERT_TRUE(r.decoded);
    EXPECT_EQ("zigbee2mqtt/salon/lampe", r.p["topic"]);
    EXPECT_EQ(3u, r.p["payload"].size());
    EXPECT_EQ("610062", hexOf(r.p["payload"]));
}

TEST(MqttRoundTrip, BinaryPayloadWithANulByteArrivesWhole)
{
    const RoundTrip r = roundTripUp(TOPIC_UP, PAY_BINARY, 4);
    ASSERT_TRUE(r.decoded);
    EXPECT_EQ(4u, r.p["payload"].size());
    EXPECT_EQ("01003f41", hexOf(r.p["payload"]));
}

TEST(MqttRoundTrip, ServerToProcDirectionCarriesBothFieldsDistinctly)
{
    Params p;
    ASSERT_TRUE(wireDecodeMessage(
                    wireEncodeMessageServerSide(TOPIC_DOWN, "{\"state\":\"OPEN\",\"pos\":42}"), p));
    EXPECT_EQ("zigbee2mqtt/cuisine/store/set", p["topic"]);
    EXPECT_EQ("{\"state\":\"OPEN\",\"pos\":42}", p["payload"]);
}

/* ========================================================================= *
 * 4. THE DECODER. Everything arrives as a STRING - the oracle of the golden
 *    suite is type strict (3 != "3"), so a value that became a JSON number
 *    would be a contract break.
 * ========================================================================= */

TEST(MqttDecode, EveryScalarTypeIsFlattenedToAString)
{
    Params p;
    ASSERT_TRUE(wireDecodeMessage(
                    "{\"str\":\"txt\",\"yes\":true,\"no\":false,\"int\":3,"
                    "\"real\":1.5,\"neg\":-2,\"big\":123456789.0}", p));

    EXPECT_EQ("txt", p["str"]);
    EXPECT_EQ("true", p["yes"]);
    EXPECT_EQ("false", p["no"]);
    EXPECT_EQ("3", p["int"]);
    EXPECT_EQ("1.5", p["real"]);
    EXPECT_EQ("-2", p["neg"]);
    //Utils::to_string(double) is a bare ostringstream. Pinned, NOT to be fixed.
    EXPECT_EQ("1.23457e+08", p["big"]);
}

TEST(MqttDecode, NullAndContainersBecomeAnEmptyStringButTheKeyIsStillThere)
{
    Params p;
    ASSERT_TRUE(wireDecodeMessage("{\"z\":null,\"arr\":[1,2],\"obj\":{\"k\":1}}", p));

    EXPECT_TRUE(p.Exists("z"));
    EXPECT_TRUE(p.Exists("arr"));
    EXPECT_TRUE(p.Exists("obj"));
    EXPECT_EQ("", p["z"]);
    EXPECT_EQ("", p["arr"]);
    EXPECT_EQ("", p["obj"]);
}

TEST(MqttDecode, AKeyThatIsAbsentStaysAbsentItDoesNotBecomeNull)
{
    Params p;
    ASSERT_TRUE(wireDecodeMessage("{\"topic\":\"only/this\"}", p));
    EXPECT_TRUE(p.Exists("topic"));
    EXPECT_FALSE(p.Exists("payload"));
    EXPECT_EQ(1, p.size());
}

//The parser must never throw and never abort the process: the message comes
//from another process and, one hop upstream, from the broker.
TEST(MqttDecode, MalformedInputIsRefusedWithoutThrowing)
{
    const char *broken[] = {
        "",
        "{",
        "not json at all",
        "{\"topic\":}",
        "{\"topic\":\"unterminated}",
        "\xff\x80",
    };
    for (const char *b: broken)
    {
        Params p;
        EXPECT_FALSE(wireDecodeMessage(b, p)) << "should have been refused: " << b;
        EXPECT_EQ(0, p.size()) << "nothing may be delivered from: " << b;
    }
}

TEST(MqttDecode, AValidJsonDocumentThatIsNotAnObjectIsRefused)
{
    const char *notObjects[] = { "[1,2,3]", "\"a string\"", "42", "true", "null" };
    for (const char *b: notObjects)
    {
        Params p;
        EXPECT_FALSE(wireDecodeMessage(b, p)) << "should have been refused: " << b;
        EXPECT_EQ(0, p.size());
    }
}

/* ========================================================================= *
 * 5. ANTI SWAP. The fixture must be able to tell topic from payload.
 *    This is the contre-mutation par echange, expressed as a test: it fails
 *    the moment the two fields are exchanged anywhere in the chain.
 * ========================================================================= */

TEST(MqttAntiSwap, TopicAndPayloadAreNeitherEqualNorInterchangeable)
{
    const string topic   = "shellies/salle-de-bain/relay/0";
    const string payload = "{\"output\":true,\"apower\":12.5}";
    ASSERT_NE(topic, payload);
    ASSERT_NE(topic.size(), payload.size());

    Params p;
    ASSERT_TRUE(wireDecodeMessage(wireEncodeMessageServerSide(topic, payload), p));
    EXPECT_EQ(topic, p["topic"]);
    EXPECT_EQ(payload, p["payload"]);
    EXPECT_NE(p["topic"], p["payload"]);

    //And the same in the other direction, where the payload is raw bytes.
    Params q;
    ASSERT_TRUE(wireDecodeMessage(
                    wireEncodeMessageProcSide(topic, payload.data(), (int)payload.size()), q));
    EXPECT_EQ(topic, q["topic"]);
    EXPECT_EQ(payload, q["payload"]);
}
