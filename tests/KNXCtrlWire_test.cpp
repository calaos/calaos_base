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
 ******************************************************************************/

/******************************************************************************
 * E4.1e - characterization of the SERVER end of the KNX wire, written BEFORE
 * IO/KNX/KNXCtrl.{h,cpp} is ported from jansson to nlohmann::json.
 *
 * WHY THIS FILE HAS TO EXIST AT ALL
 * ---------------------------------
 * E4.0d established, and E4.1 repeats it, that nothing in the existing suite
 * ever executes a driver: the 145 goldens go through JsonApi, never through
 * IO/KNX. On top of that the goldens compare PARSED DOCUMENTS, not bytes, so
 * even if they did reach here they could not see an escaping change. The KNX
 * wire therefore has no net of any kind, on either dimension. This file is
 * the net, and it is built to stay green across the port.
 *
 * WHAT IS PRODUCTION CODE HERE AND WHAT IS A MIRROR - stated plainly, because
 * a test that mirrors its subject proves nothing about the subject:
 *
 *  - KNXValue::toJson() / KNXValue::fromJson() ARE production functions and
 *    are called directly. Everything about the "value" sub-document - which
 *    keys exist, which field lands in which key, how each field is rendered -
 *    is genuinely under test.
 *  - Params::toJson() (through the seam below, after the port) is production
 *    code too, and so is the dump form.
 *  - The envelope builders knxWriteMessage() and knxReadMessage() (declared
 *    in KNXCtrl.h) ARE production code and are called directly.
 *
 *    ⚠ THEY DID NOT USED TO BE, AND THAT WAS A REAL HOLE, MEASURED. This file
 *    first froze a faithful line-by-line COPY of the assembly done inside
 *    KNXCtrl::writeValue()/readValue(), because those two methods end on
 *    process->sendMessage() and cannot be reached from a test (private ctor,
 *    singleton spawning two calaos_knx children, ExternProcServer needing a
 *    live libuv loop). A copy freezes what the TEST does, not what the
 *    PRODUCT does: with the copy in place, putting the four production dump()
 *    back to a naked .dump() - i.e. reintroducing the type_error.316 /
 *    std::terminate this very ticket documents - left the whole suite
 *    34/34 GREEN. The assembly was therefore extracted into free functions
 *    that production and tests both call. Never re-inline them.
 *
 * THE SEAM. Exactly four functions - dumpKnxValue(), parseKnxValue() and the
 * two envelope mirrors - carry the jansson -> nlohmann change. NO ASSERTION
 * and NO EXPECTED LITERAL moves with them, with the three exceptions named,
 * argued and isolated below under _DECLARED_DELTA.
 ******************************************************************************/

#include <gtest/gtest.h>
#include <cctype>
#include <string>
#include "KNXCtrl.h"
#include "Params.h"

using std::string;

namespace
{

/*** THE SEAM - these four functions, and only these four, change with the port ***/

//KNXValue::toJson() rendered to the bytes that go on the wire.
string dumpKnxValue(const KNXValue &v)
{
    return v.toJson().dump(-1, ' ', true, Json::error_handler_t::replace);
}

//The reverse: the bytes of a "value" sub-document back into a KNXValue.
KNXValue parseKnxValue(const string &text)
{
    return KNXValue::fromJson(Json::parse(text, nullptr, false));
}

//NOT mirrors any more. knxWriteMessage()/knxReadMessage() are the PRODUCTION
//builders declared in KNXCtrl.h; KNXCtrl::writeValue()/readValue() call these
//very functions and then hand the result to process->sendMessage(). See the
//note at the top of this file for why the earlier copies were worthless.
string writeMessage(const string &group_addr, const KNXValue &value)
{
    return knxWriteMessage(group_addr, value);
}

string readMessage(const string &group_addr, int eis)
{
    return knxReadMessage(group_addr, eis);
}

/*** END OF THE SEAM - nothing below this line moves with the port ***/

//Lowercases the four hex digits of every \uXXXX escape and touches nothing
//else. It states the ONE invariant that has to hold across the port even
//though the exact bytes of an escape do not: jansson spells U+00E9 with an
//uppercase hex, nlohmann with a lowercase one, and no correct JSON parser
//can tell the two apart. Deliberately naive about an escaped backslash
//followed by 'u': no fixture here contains one, and making it clever would
//make it a second implementation of the thing under test.
string lowerHexEscapes(const string &s)
{
    string out = s;
    for (size_t i = 0; i + 5 < out.size(); i++)
    {
        if (out[i] == '\\' && out[i + 1] == 'u')
        {
            for (size_t k = i + 2; k < i + 6; k++)
                out[k] = (char)std::tolower((unsigned char)out[k]);
        }
    }
    return out;
}

/******************************************************************************
 * THE FIXTURES.
 *
 * Six fields cross the wire: type, eis, value_int, value_float, value_char,
 * value_string. Inside ONE document every one of the six carries a DIFFERENT
 * rendered value, so that swapping any two of them in KNXValue::toJson() -
 * the counter-mutation by exchange this ticket owes - changes the emitted
 * bytes and turns these cases red. A fixture where value_int and value_float
 * both read "0" would let that swap through unseen; that is the "poor
 * fixture" defect the E4.0 series hit seven times, and it is what these
 * numbers are chosen against.
 *
 * The INPUT documents list their keys in a deliberately NON-alphabetical
 * order while the EXPECTED outputs are alphabetical. A serializer that echoed
 * the order it was handed, or that kept an insertion order, would not produce
 * the expected bytes. Params is a std::map, so the sort is a property of the
 * data structure and not of the JSON library, and it therefore has to hold
 * identically before and after the port.
 *
 * Every float is an exact binary fraction (21.5, 18.25, 3.75, 7.5, 9.125) so
 * that float -> Utils::to_string -> float is lossless and the frozen bytes do
 * not depend on the default ostream precision.
 ******************************************************************************/

struct WireFixture
{
    const char *name;
    const char *scrambled;  //what a peer sends: keys in non-alphabetical order
    const char *canonical;  //what KNXValue::toJson() must emit: keys sorted
};

//KNXError = 0, KNXInteger = 1, KNXFloat = 2, KNXChar = 3, KNXString = 4
const WireFixture kFixtures[] =
{
    {
        "KNXInteger",
        "{\"value_string\":\"Salon\",\"value_char\":\"A\",\"value_int\":\"77\","
        "\"value_float\":\"21.5\",\"eis\":\"6\",\"type\":\"1\"}",
        "{\"eis\":\"6\",\"type\":\"1\",\"value_char\":\"A\",\"value_float\":\"21.5\","
        "\"value_int\":\"77\",\"value_string\":\"Salon\"}"
    },
    {
        "KNXFloat",
        "{\"value_string\":\"Cuisine\",\"value_char\":\"Z\",\"value_int\":\"43\","
        "\"value_float\":\"18.25\",\"eis\":\"9\",\"type\":\"2\"}",
        "{\"eis\":\"9\",\"type\":\"2\",\"value_char\":\"Z\",\"value_float\":\"18.25\","
        "\"value_int\":\"43\",\"value_string\":\"Cuisine\"}"
    },
    {
        "KNXChar",
        "{\"value_string\":\"Bureau\",\"value_char\":\"Q\",\"value_int\":\"88\","
        "\"value_float\":\"3.75\",\"eis\":\"13\",\"type\":\"3\"}",
        "{\"eis\":\"13\",\"type\":\"3\",\"value_char\":\"Q\",\"value_float\":\"3.75\","
        "\"value_int\":\"88\",\"value_string\":\"Bureau\"}"
    },
    {
        "KNXString",
        "{\"value_string\":\"Chambre\",\"value_char\":\"k\",\"value_int\":\"12\","
        "\"value_float\":\"7.5\",\"eis\":\"15\",\"type\":\"4\"}",
        "{\"eis\":\"15\",\"type\":\"4\",\"value_char\":\"k\",\"value_float\":\"7.5\","
        "\"value_int\":\"12\",\"value_string\":\"Chambre\"}"
    },
    {
        "KNXError",
        "{\"value_string\":\"Garage\",\"value_char\":\"m\",\"value_int\":\"55\","
        "\"value_float\":\"9.125\",\"eis\":\"2\",\"type\":\"0\"}",
        "{\"eis\":\"2\",\"type\":\"0\",\"value_char\":\"m\",\"value_float\":\"9.125\","
        "\"value_int\":\"55\",\"value_string\":\"Garage\"}"
    },
};

} //namespace

/******************************************************************************
 * 1. Round trip over five distinct KNX value types.
 ******************************************************************************/

TEST(KNXCtrlWire, RoundTripsEveryFieldOfTheFiveKnxValueTypes)
{
    for (const WireFixture &f: kFixtures)
    {
        SCOPED_TRACE(f.name);

        KNXValue v = parseKnxValue(f.scrambled);
        EXPECT_EQ(string(f.canonical), dumpKnxValue(v));

        //A second lap. A field lost on the way IN would still "round trip" if
        //it was lost on the way OUT as well; the assertion above is what
        //catches that, because it compares against a literal and not against
        //the input.
        EXPECT_EQ(string(f.canonical), dumpKnxValue(parseKnxValue(f.canonical)));
    }
}

TEST(KNXCtrlWire, DecodedFieldsReachThePublicAccessors)
{
    //The bytes are not the whole contract: each field has to land in the
    //member the accessors read. These assertions make a swap of two keys
    //visible even to a reader who only looks at the public API.
    KNXValue vint = parseKnxValue(kFixtures[0].scrambled);   //KNXInteger, eis 6
    EXPECT_EQ(77, vint.toInt());
    EXPECT_TRUE(vint.toBool());
    EXPECT_EQ(string("77"), vint.toString());

    KNXValue vfloat = parseKnxValue(kFixtures[1].scrambled); //KNXFloat
    EXPECT_FLOAT_EQ(18.25f, vfloat.toFloat());
    EXPECT_EQ(string("18.25"), vfloat.toString());

    KNXValue vchar = parseKnxValue(kFixtures[2].scrambled);  //KNXChar
    EXPECT_EQ('Q', vchar.toChar());
    EXPECT_EQ(string("Q"), vchar.toString());

    KNXValue vstring = parseKnxValue(kFixtures[3].scrambled); //KNXString
    EXPECT_EQ(string("Chambre"), vstring.toString());

    KNXValue verror = parseKnxValue(kFixtures[4].scrambled);  //KNXError
    EXPECT_EQ(string(), verror.toString());
}

/******************************************************************************
 * 2. The two messages KNXCtrl puts on the wire, frozen byte for byte.
 *    (Envelope assembly is mirrored - see the header of this file.)
 ******************************************************************************/

TEST(KNXCtrlWire, WriteMessageIsFrozenByteForByte)
{
    KNXValue v = parseKnxValue(kFixtures[2].scrambled); //KNXChar 'Q', eis 13

    EXPECT_EQ(string("{\"group_addr\":\"1/2/3\",\"type\":\"write\",\"value\":"
                     "{\"eis\":\"13\",\"type\":\"3\",\"value_char\":\"Q\","
                     "\"value_float\":\"3.75\",\"value_int\":\"88\","
                     "\"value_string\":\"Bureau\"}}"),
              writeMessage("1/2/3", v));
}

TEST(KNXCtrlWire, ReadMessageIsFrozenByteForByte)
{
    EXPECT_EQ(string("{\"eis\":\"6\",\"group_addr\":\"1/2/3\",\"type\":\"read\"}"),
              readMessage("1/2/3", 6));
}

TEST(KNXCtrlWire, AnAsciiOnlyMessageCarriesNoEscapeAtAll)
{
    //"the hex case may only show up when the value contains non-ASCII": the
    //other half of that sentence is that an ASCII-only message must contain
    //no \u escape whatsoever, before or after the port.
    KNXValue v = parseKnxValue(kFixtures[0].scrambled);
    EXPECT_EQ(string::npos, writeMessage("1/2/3", v).find("\\u"));
    EXPECT_EQ(string::npos, readMessage("1/2/3", 6).find("\\u"));
}

/******************************************************************************
 * 3. Non-ASCII: the one place where the bytes legitimately change.
 ******************************************************************************/

namespace
{
//U+00E9 twice, as raw UTF-8 bytes in the input. jansson dumps with
//JSON_ENSURE_ASCII and spells the hex in UPPERCASE; nlohmann's
//dump(ensure_ascii = true) spells it in lowercase. That is the whole delta
//this epic accepted, and all three ends of this wire (KNXCtrl, calaos_knx,
//calaos_knx --read/--write) decode with a real JSON parser, to which the two
//spellings are the same character.
const char *kAccentedInput =
    "{\"value_string\":\"Salle \xC3\xA9t\xC3\xA9\",\"value_char\":\"B\","
    "\"value_int\":\"1\",\"value_float\":\"2.5\",\"eis\":\"15\",\"type\":\"4\"}";

const char *kAccentedJansson =
    "{\"eis\":\"15\",\"type\":\"4\",\"value_char\":\"B\",\"value_float\":\"2.5\","
    "\"value_int\":\"1\",\"value_string\":\"Salle \\u00E9t\\u00E9\"}";

const char *kAccentedNlohmann =
    "{\"eis\":\"15\",\"type\":\"4\",\"value_char\":\"B\",\"value_float\":\"2.5\","
    "\"value_int\":\"1\",\"value_string\":\"Salle \\u00e9t\\u00e9\"}";
}

TEST(KNXCtrlWire, NonAsciiDiffersOnlyByTheCaseOfTheHexEscape)
{
    //THE INVARIANT. Identical before and after the port, and the assertion
    //that actually states the contract: whatever the library, the two forms
    //differ only in the case of the four hex digits of a \uXXXX.
    EXPECT_EQ(lowerHexEscapes(string(kAccentedJansson)),
              lowerHexEscapes(string(kAccentedNlohmann)));
    EXPECT_NE(string(kAccentedJansson), string(kAccentedNlohmann));

    KNXValue v = parseKnxValue(kAccentedInput);
    EXPECT_EQ(lowerHexEscapes(string(kAccentedNlohmann)),
              lowerHexEscapes(dumpKnxValue(v)));

    //The wire stays pure ASCII either way: no raw UTF-8 byte escapes onto it.
    for (char c: dumpKnxValue(v))
        EXPECT_LT((unsigned int)(unsigned char)c, 0x80u);

    //And the same, through the PRODUCTION builder this time, so that losing
    //ensure_ascii on one of the four production dump() sites turns this red.
    const string prod = writeMessage("1/2/3", v);
    EXPECT_NE(string::npos, prod.find("\\u00e9"));
    for (char c: prod)
        EXPECT_LT((unsigned int)(unsigned char)c, 0x80u);
}

TEST(KNXCtrlWire, NonAsciiExactBytes_DECLARED_DELTA)
{
    //DECLARED DELTA 1 of 3. jansson é -> nlohmann é. Invisible to
    //every consumer of this wire; the invariant above is what keeps the
    //contract, this one only records which side of the port we are on.
    KNXValue v = parseKnxValue(kAccentedInput);
    EXPECT_EQ(string(kAccentedNlohmann), dumpKnxValue(v));
}

/******************************************************************************
 * 4. value_char: two paths where jansson silently corrupts the value.
 *
 * KNXValue renders value_char with Utils::to_string(unsigned char), which is
 * an ostringstream: it writes the CHARACTER, not the number. So value_char=0
 * produces a one-byte NUL string and value_char=200 produces a lone 0xC8,
 * which is not valid UTF-8. jansson's json_string() truncates at the NUL and
 * answers NULL on invalid UTF-8; neither return code is tested, here or in
 * src/. These cases pin what actually happens today.
 ******************************************************************************/

TEST(KNXCtrlWire, ValueCharZeroDecodesBackToZeroWhateverTheLibrary)
{
    //THE INVARIANT, unchanged by the port. Whichever of "" (jansson, NUL
    //truncated away) or the escaped NUL (nlohmann, NUL preserved) travels,
    //the peer decodes value_char back to 0: Utils::from_string on an unsigned
    //char is the CHARACTER extractor, so on "" it fails and leaves the fresh
    //KNXValue's 0 in place, and on a one-byte NUL it reads that NUL, also 0.
    KNXValue zero = KNXValue::fromChar((char)0, 13);
    EXPECT_EQ(0, (int)parseKnxValue(dumpKnxValue(zero)).toChar());

    //Live witness in the same test: the assertion above would also hold if
    //toChar() had started answering 0 for everything.
    KNXValue live = KNXValue::fromChar('Q', 13);
    EXPECT_EQ('Q', parseKnxValue(dumpKnxValue(live)).toChar());
}

TEST(KNXCtrlWire, ValueCharZeroExactBytes_DECLARED_DELTA)
{
    //DECLARED DELTA 2 of 3, and the one on the COMMON path: every KNXString
    //value, and every default-constructed KNXValue, carries value_char = 0.
    //jansson truncates the one-byte NUL string at its NUL and emits "";
    //nlohmann keeps it and emits the escaped NUL. Same decoded value on the
    //peer (previous test), different bytes, both ends shipped together.
    EXPECT_EQ(string("{\"eis\":\"15\",\"type\":\"4\",\"value_char\":\"\\u0000\","
                     "\"value_float\":\"0\",\"value_int\":\"0\","
                     "\"value_string\":\"Chambre\"}"),
              dumpKnxValue(KNXValue::fromString("Chambre", 15)));

    EXPECT_EQ(string("{\"eis\":\"13\",\"type\":\"3\",\"value_char\":\"\\u0000\","
                     "\"value_float\":\"0\",\"value_int\":\"0\","
                     "\"value_string\":\"\"}"),
              dumpKnxValue(KNXValue::fromChar((char)0, 13)));
}

TEST(KNXCtrlWire, ValueCharAboveAsciiExactBytes_DECLARED_DELTA)
{
    //DECLARED DELTA 3 of 3, the one with a visible consequence. value_char =
    //200 renders as the single byte 0xC8, which is not valid UTF-8:
    //  jansson  -> json_string() answers NULL, json_object_set_new() answers
    //              -1, nobody looks, THE KEY IS SILENTLY MISSING from the
    //              message and the peer decodes value_char = 0;
    //  nlohmann -> the byte sits in the tree and a naked dump() would throw
    //              type_error.316; with error_handler_t::replace, which this
    //              epic mandates, it becomes U+FFFD and the peer decodes
    //              value_char = 0xEF, the first byte of U+FFFD.
    //Neither preserves 200. This is a PRE-EXISTING defect of the KNX wire for
    //EIS 13/16 characters above 0x7F, consigned in FINDINGS.md and NOT fixed
    //here: fixing it changes what the value means on the wire, which is not
    //something a library migration is allowed to do.
    //fromChar takes a signed char, so (char)200 is -56 and value_int and
    //value_float take that, while value_char takes the 200 back.
    KNXValue v = KNXValue::fromChar((char)200, 13);

    EXPECT_EQ(string("{\"eis\":\"13\",\"type\":\"3\",\"value_char\":\"\\ufffd\","
                     "\"value_float\":\"-56\",\"value_int\":\"-56\","
                     "\"value_string\":\"\"}"),
              dumpKnxValue(v));
    EXPECT_EQ(0xEF, (int)(unsigned char)parseKnxValue(dumpKnxValue(v)).toChar());
}

/******************************************************************************
 * 5. Decoding what a peer may send. jansson_decode_object() accepts strings,
 *    booleans and numbers, stringifies the last two, records every other type
 *    as an empty string, and never throws. nlohmann's natural equivalent -
 *    assigning a Json straight into a std::string, the way Params::fromJson
 *    does - THROWS type_error.302 on a non-string, from inside a
 *    messageReceived() that has no handler above it. These cases are what
 *    forces the port to keep the tolerant, non-throwing shape.
 ******************************************************************************/

TEST(KNXCtrlWire, ANumberFieldIsStringifiedNotRejected)
{
    KNXValue v = parseKnxValue("{\"type\":3,\"eis\":13,\"value_int\":88,"
                               "\"value_float\":3.75,\"value_char\":\"Q\","
                               "\"value_string\":\"Bureau\"}");

    EXPECT_EQ(string("{\"eis\":\"13\",\"type\":\"3\",\"value_char\":\"Q\","
                     "\"value_float\":\"3.75\",\"value_int\":\"88\","
                     "\"value_string\":\"Bureau\"}"),
              dumpKnxValue(v));
}

TEST(KNXCtrlWire, ABooleanFieldIsStringifiedTheJanssonWay)
{
    //jansson_decode_object() spells booleans "true"/"false". The boolean goes
    //into value_STRING, not value_int: put in value_int, "true" and "false"
    //BOTH fail from_string and BOTH leave 0, so the case could not tell them
    //apart - it was a dead oracle (dropping the is_boolean() branch, or
    //swapping "true" and "false", left it green). Two calls, two different
    //expected bytes, is what makes it bite.
    KNXValue vtrue = parseKnxValue("{\"type\":\"1\",\"eis\":\"6\",\"value_int\":\"77\","
                                   "\"value_float\":\"21.5\",\"value_char\":\"A\","
                                   "\"value_string\":true}");
    EXPECT_EQ(string("{\"eis\":\"6\",\"type\":\"1\",\"value_char\":\"A\","
                     "\"value_float\":\"21.5\",\"value_int\":\"77\","
                     "\"value_string\":\"true\"}"),
              dumpKnxValue(vtrue));

    KNXValue vfalse = parseKnxValue("{\"type\":\"1\",\"eis\":\"6\",\"value_int\":\"77\","
                                    "\"value_float\":\"21.5\",\"value_char\":\"A\","
                                    "\"value_string\":false}");
    EXPECT_EQ(string("{\"eis\":\"6\",\"type\":\"1\",\"value_char\":\"A\","
                     "\"value_float\":\"21.5\",\"value_int\":\"77\","
                     "\"value_string\":\"false\"}"),
              dumpKnxValue(vfalse));
}

TEST(KNXCtrlWire, AnArrayOrObjectFieldIsRecordedAsAnEmptyString)
{
    KNXValue v = parseKnxValue("{\"type\":\"1\",\"eis\":\"6\",\"value_int\":[1,2],"
                               "\"value_float\":\"21.5\",\"value_char\":\"A\","
                               "\"value_string\":{\"a\":\"b\"}}");

    EXPECT_EQ(string("{\"eis\":\"6\",\"type\":\"1\",\"value_char\":\"A\","
                     "\"value_float\":\"21.5\",\"value_int\":\"0\","
                     "\"value_string\":\"\"}"),
              dumpKnxValue(v));
}

TEST(KNXCtrlWire, MissingKeysLeaveTheDefaultsAndNothingThrows)
{
    //The escaped NUL under value_char is DECLARED DELTA 2 again: a default
    //KNXValue carries value_char = 0, so every case built on defaults shows
    //it. jansson emitted "" here for exactly the same reason.
    //Utils::from_string on an empty string never reaches the extractor (the
    //stream sentry fails first), so the destination keeps its value. eis
    //therefore stays at its member default of -1, not at 0.
    KNXValue v = parseKnxValue("{\"type\":\"4\"}");

    EXPECT_EQ(string("{\"eis\":\"-1\",\"type\":\"4\",\"value_char\":\"\\u0000\","
                     "\"value_float\":\"0\",\"value_int\":\"0\","
                     "\"value_string\":\"\"}"),
              dumpKnxValue(v));
}

/* ⭐ T3.25 (review reserve 2). value_int in OVERFLOW keeps 0, and that is a
 * DIFFERENT wrong number from the one it used to keep.
 *
 * KNXCtrl.cpp:171 reads value_int with from_string_or_keep() and KNXValue's
 * member default is 0 (KNXCtrl.h:82). A value that does not fit an int is a
 * parse FAILURE since T3.25, so the default wins:
 *
 *     before: "99999999999" -> value_int = 2147483647 (num_get saturates)
 *     now:    "99999999999" -> value_int = 0          (the default survives)
 *
 * ⚠️ Both are wrong - the frame said neither 0 nor INT_MAX. 0 is the safer of
 * the two on a KNX bus (INT_MAX is written out as a group value), and it is the
 * one the ticket chose, deliberately and with no clamp added. This case exists
 * so that the choice is a decision and not an accident.
 *
 * ⚠️ The CLI twin (KNXExternProc_cli.cpp:521) carries an int64_t, not an int
 * (KNXExternProc_main.h:89): "99999999999" fits there and parses fine. Its
 * overflow token is 9223372036854775808. Do not copy this input across.
 */
TEST(KNXCtrlWire, AnOverflowingValueIntKeepsTheDefaultInsteadOfSaturating)
{
    KNXValue v = parseKnxValue("{\"type\":\"1\",\"eis\":\"6\","
                               "\"value_int\":\"99999999999\","
                               "\"value_float\":\"21.5\",\"value_char\":\"A\","
                               "\"value_string\":\"\"}");

    EXPECT_EQ(string("{\"eis\":\"6\",\"type\":\"1\",\"value_char\":\"A\","
                     "\"value_float\":\"21.5\",\"value_int\":\"0\","
                     "\"value_string\":\"\"}"),
              dumpKnxValue(v));

    //The boundary that must still parse, one character away from the refusal.
    KNXValue max = parseKnxValue("{\"type\":\"1\",\"eis\":\"6\","
                                 "\"value_int\":\"2147483647\","
                                 "\"value_float\":\"21.5\",\"value_char\":\"A\","
                                 "\"value_string\":\"\"}");

    EXPECT_EQ(string("{\"eis\":\"6\",\"type\":\"1\",\"value_char\":\"A\","
                     "\"value_float\":\"21.5\",\"value_int\":\"2147483647\","
                     "\"value_string\":\"\"}"),
              dumpKnxValue(max));
}

TEST(KNXCtrlWire, ADocumentThatIsNotAnObjectDecodesToTheDefaultValue)
{
    //KNXCtrl::processNewMessage() hands fromJson() whatever sits under the
    //"value" key, without checking it - and hands it nothing at all when the
    //key is absent, which is what a "read" event looks like. An array, a bare
    //string, a null and a missing key must all give a default KNXValue, and
    //none of them may throw.
    //Same DECLARED DELTA 2 on value_char as everywhere a default value is
    //serialized.
    const string expected =
        "{\"eis\":\"-1\",\"type\":\"0\",\"value_char\":\"\\u0000\","
        "\"value_float\":\"0\",\"value_int\":\"0\",\"value_string\":\"\"}";

    EXPECT_EQ(expected, dumpKnxValue(parseKnxValue("[1,2,3]")));
    EXPECT_EQ(expected, dumpKnxValue(parseKnxValue("\"nope\"")));
    EXPECT_EQ(expected, dumpKnxValue(parseKnxValue("null")));

    //A probe that can never become valid JSON, so it can never start being
    //parsed by accident: json_loads() answers NULL, Json::parse(..., false)
    //answers a discarded value. Either way a default KNXValue, and no throw.
    EXPECT_EQ(expected, dumpKnxValue(parseKnxValue("@@ not json @@")));
}

/******************************************************************************
 * 6. THE PATH THAT MAKES ensure_ascii AND THE ERROR HANDLER NON-NEGOTIABLE.
 *
 * Added AFTER the port, on the coordinator's ensure_ascii alert, and it is the
 * strongest case of this file - so its provenance is stated rather than
 * hidden. It was NOT part of the characterization commit; what jansson does
 * with it was measured separately on the same inputs and is written below.
 *
 * KNXValue::setValue(), case 15/16 (KNXExternProc_cli.cpp), fills value_string
 * with the RAW BYTES of the bus frame - no validation, no transcoding. A KNX
 * EIS 15/16 device sending latin-1 text therefore puts INVALID UTF-8 straight
 * into a value that both ends dump. Measured on the latin-1 bytes C9 74 E9:
 *
 *   jansson (JSON_ENSURE_ASCII) -> json_string() answers NULL and THE WHOLE
 *       value_string KEY DISAPPEARS from the message, silently. The server
 *       receives an event with no string in it.
 *   nlohmann, naked dump()      -> THROWS type_error.316 ("invalid UTF-8 byte
 *       at index 1"). That throw happens inside KNXProcess::monitorWait(),
 *       with no handler above it: std::terminate on a live installation.
 *   nlohmann, ensure_ascii + error_handler_t::replace (what this ticket does)
 *       -> U+FFFD for each bad byte, pure ASCII, parseable, lossy but not
 *       fatal.
 *
 * So on THIS perimeter neither invariant is decorative: ensure_ascii is what
 * keeps the wire ASCII when a device sends accented text, and the error
 * handler is what turns a crash into a lossy string.
 ******************************************************************************/

TEST(KNXCtrlWire, RawNonUtf8BusBytesAreReplacedInsteadOfCrashing_DECLARED_DELTA)
{
    //DECLARED DELTA 4: jansson dropped value_string entirely, nlohmann keeps
    //it with U+FFFD in place of each bad byte.
    KNXValue v = KNXValue::fromString("\xc9" "t" "\xe9", 15);
    const string msg = writeMessage("1/2/3", v);

    EXPECT_NE(string::npos, msg.find("\\ufffdt\\ufffd"));

    //Pure ASCII on the wire, and still a parseable document.
    for (char c: msg)
        EXPECT_LT((unsigned int)(unsigned char)c, 0x80u);
    EXPECT_FALSE(Json::parse(msg, nullptr, false).is_discarded());
}
