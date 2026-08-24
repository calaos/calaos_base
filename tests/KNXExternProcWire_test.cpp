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
 * E4.1e - characterization of the calaos_knx (SUBPROCESS) end of the KNX
 * wire, written BEFORE IO/KNX/KNXExternProc_main.{h,cpp} and
 * IO/KNX/KNXExternProc_cli.cpp are ported from jansson to nlohmann::json.
 *
 * WHY A SECOND FILE, AND WHY IT MATTERS MORE THAN IT LOOKS
 * -------------------------------------------------------
 * KNXValue is declared TWICE in this repository - IO/KNX/KNXCtrl.h:64 for the
 * server and IO/KNX/KNXExternProc_main.h:57 for calaos_knx - and toJson() is
 * defined in two places, KNXCtrl.cpp and KNXExternProc_cli.cpp. The two
 * classes are not the same type: the subprocess one keeps value_int in an
 * int64_t and exposes its fields publicly, the server one keeps an int and
 * hides them. They cannot live in one test binary either, so this file is the
 * mirror image of KNXCtrlWire_test.cpp for the other copy.
 *
 * THE FIXTURES AND THE EXPECTED LITERALS ARE DELIBERATELY THE SAME AS IN
 * KNXCtrlWire_test.cpp. That is the point: the two copies of toJson() have to
 * emit the same bytes for the same value, or the two ends of the wire stop
 * understanding each other. Two identical literals in two binaries is what
 * turns "the three copies must move together" from a comment into an
 * assertion.
 *
 * PRODUCTION CODE vs MIRROR, same honesty as the sibling file:
 *  - KNXValue::toJson() / fromJson() are production, called directly.
 *  - knxEventMessage() / knxDisconnectedMessage() ARE production code and
 *    are called directly. ⚠ They used to be MIRRORED here, and a mirror
 *    freezes what the TEST does, not what the PRODUCT does: with the mirror
 *    in place, putting the four production dump() back to a naked .dump()
 *    left the whole suite 34/34 GREEN. Extracted, never re-inline.
 *
 * THE SEAM: dumpKnxValue(), parseKnxValue(), eventMessage(),
 * disconnectedMessage(). Nothing else moves with the port except the three
 * cases marked _DECLARED_DELTA.
 ******************************************************************************/

#include <gtest/gtest.h>
#include <cctype>
#include <string>
#include "KNXExternProc_main.h"

using std::string;

/******************************************************************************
 * KNXExternProc_cli.o carries KNXValue::toJson()/fromJson() - what this file
 * is about - but also KNXProcess::doRead/doWrite/doMonitorBus, the command
 * line entry points, and those call four address helpers that live in
 * KNXExternProc_main.cpp. That object cannot be linked here: it ends on
 * EXTERN_PROC_CLIENT_MAIN(KNXProcess), which defines the main() of calaos_knx
 * and would fight gtest_main for the entry point.
 *
 * The four helpers are pure bit shuffling on a 16 bit KNX address, they have
 * nothing to do with JSON, and nothing in this file reaches them - no test
 * here calls doRead, doWrite or doMonitorBus. They are defined once, loudly,
 * so that the linker is satisfied AND so that a test that ever did reach them
 * would fail instead of silently getting a wrong address.
 ******************************************************************************/
string KNXProcess::knxPhysicalAddr(eibaddr_t)
{
    ADD_FAILURE() << "stub: link KNXExternProc_main.o to use knxPhysicalAddr";
    return string();
}

string KNXProcess::knxGroupAddr(eibaddr_t)
{
    ADD_FAILURE() << "stub: link KNXExternProc_main.o to use knxGroupAddr";
    return string();
}

eibaddr_t KNXProcess::eKnxGroupAddr(const string &)
{
    ADD_FAILURE() << "stub: link KNXExternProc_main.o to use eKnxGroupAddr";
    return 0;
}

eibaddr_t KNXProcess::eKnxPhysicalAddr(const string &)
{
    ADD_FAILURE() << "stub: link KNXExternProc_main.o to use eKnxPhysicalAddr";
    return 0;
}

namespace
{

/*** THE SEAM - these four functions, and only these four, change with the port ***/

string dumpKnxValue(const KNXValue &v)
{
    return v.toJson().dump(-1, ' ', true, Json::error_handler_t::replace);
}

KNXValue parseKnxValue(const string &text)
{
    return KNXValue::fromJson(Json::parse(text, nullptr, false));
}

//NOT mirrors any more. knxEventMessage()/knxDisconnectedMessage() are the
//PRODUCTION builders declared in KNXExternProc_main.h, and monitorWait()
//calls these very functions. See the note at the top of this file.
string eventMessage(const string &group_addr, const string &knx_type,
                    const KNXValue &value, bool printValue)
{
    return knxEventMessage(group_addr, knx_type, value, printValue);
}

string disconnectedMessage()
{
    return knxDisconnectedMessage();
}

/*** END OF THE SEAM - nothing below this line moves with the port ***/

//See KNXCtrlWire_test.cpp for why this exists and why it is naive.
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
 * THE FIXTURES - byte for byte the ones of KNXCtrlWire_test.cpp. Six fields,
 * six DIFFERENT rendered values inside each document, so that exchanging any
 * two of them in toJson() turns these cases red; input keys deliberately in
 * non-alphabetical order against the alphabetical expected output; floats
 * chosen as exact binary fractions.
 ******************************************************************************/

struct WireFixture
{
    const char *name;
    const char *scrambled;
    const char *canonical;
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

KNXValue makeValue(int type, int eis, int64_t value_int, float value_float,
                   unsigned char value_char, const string &value_string)
{
    KNXValue v;
    v.type = type;
    v.eis = eis;
    v.value_int = value_int;
    v.value_float = value_float;
    v.value_char = value_char;
    v.value_string = value_string;
    return v;
}

} //namespace

/******************************************************************************
 * 1. Round trip over five distinct KNX value types.
 ******************************************************************************/

TEST(KNXExternProcWire, RoundTripsEveryFieldOfTheFiveKnxValueTypes)
{
    for (const WireFixture &f: kFixtures)
    {
        SCOPED_TRACE(f.name);

        KNXValue v = parseKnxValue(f.scrambled);
        EXPECT_EQ(string(f.canonical), dumpKnxValue(v));
        EXPECT_EQ(string(f.canonical), dumpKnxValue(parseKnxValue(f.canonical)));
    }
}

TEST(KNXExternProcWire, EveryFieldSetOnTheObjectLandsInItsOwnKey)
{
    //The subprocess copy exposes its fields, so this end can build a value
    //without going through the decoder at all: an exchange of two keys in
    //toJson() is caught here even if fromJson() had been exchanged the same
    //way and the round trip above had stayed green.
    KNXValue v = makeValue(3, 13, 88, 3.75f, 'Q', "Bureau");

    EXPECT_EQ(string(kFixtures[2].canonical), dumpKnxValue(v));
}

TEST(KNXExternProcWire, DecodedFieldsLandInTheDeclaredMembers)
{
    KNXValue v = parseKnxValue(kFixtures[0].scrambled);

    EXPECT_EQ(1, v.type);
    EXPECT_EQ(6, v.eis);
    EXPECT_EQ(77, v.value_int);
    EXPECT_FLOAT_EQ(21.5f, v.value_float);
    EXPECT_EQ('A', (char)v.value_char);
    EXPECT_EQ(string("Salon"), v.value_string);
}

TEST(KNXExternProcWire, ValueIntKeepsThe64BitRangeItIsDeclaredWith)
{
    //value_int is an int64_t on this side and an int on the server side. The
    //wire carries it as a string either way; a port that turned it into a
    //JSON number would also have to answer for this, so it is pinned.
    KNXValue v = makeValue(1, 12, 4294967296LL, 1.5f, 'x', "big");

    EXPECT_EQ(string("{\"eis\":\"12\",\"type\":\"1\",\"value_char\":\"x\","
                     "\"value_float\":\"1.5\",\"value_int\":\"4294967296\","
                     "\"value_string\":\"big\"}"),
              dumpKnxValue(v));
    EXPECT_EQ(4294967296LL, parseKnxValue(dumpKnxValue(v)).value_int);
}

/******************************************************************************
 * 2. The two messages calaos_knx puts on the wire, frozen byte for byte.
 *    (Envelope assembly is mirrored - see the header of this file.)
 ******************************************************************************/

TEST(KNXExternProcWire, EventMessageIsFrozenByteForByte)
{
    KNXValue v = makeValue(3, 13, 88, 3.75f, 'Q', "Bureau");

    EXPECT_EQ(string("{\"group_addr\":\"1/2/3\",\"knx_type\":\"write\","
                     "\"type\":\"event\",\"value\":"
                     "{\"eis\":\"13\",\"type\":\"3\",\"value_char\":\"Q\","
                     "\"value_float\":\"3.75\",\"value_int\":\"88\","
                     "\"value_string\":\"Bureau\"}}"),
              eventMessage("1/2/3", "write", v, true));
}

TEST(KNXExternProcWire, AReadEventCarriesNoValueKeyAtAll)
{
    //monitorWait() sets printValue = false for a Read APDU, so the "value"
    //key is OMITTED - not set to null. KNXCtrl::processNewMessage() then
    //hands fromJson() the absence of that key. An absent key is not a null
    //key, and the port must not turn one into the other.
    KNXValue v = makeValue(3, 13, 88, 3.75f, 'Q', "Bureau");

    const string msg = eventMessage("1/2/3", "read", v, false);

    EXPECT_EQ(string("{\"group_addr\":\"1/2/3\",\"knx_type\":\"read\","
                     "\"type\":\"event\"}"),
              msg);
    EXPECT_EQ(string::npos, msg.find("value"));
    EXPECT_EQ(string::npos, msg.find("null"));
}

TEST(KNXExternProcWire, DisconnectedMessageIsFrozenByteForByte)
{
    EXPECT_EQ(string("{\"type\":\"disconnected\"}"), disconnectedMessage());
}

TEST(KNXExternProcWire, AnAsciiOnlyMessageCarriesNoEscapeAtAll)
{
    KNXValue v = makeValue(1, 6, 77, 21.5f, 'A', "Salon");

    EXPECT_EQ(string::npos, eventMessage("1/2/3", "write", v, true).find("\\u"));
    EXPECT_EQ(string::npos, disconnectedMessage().find("\\u"));
}

/******************************************************************************
 * 3. Non-ASCII: the one place where the bytes legitimately change.
 ******************************************************************************/

namespace
{
const char *kAccentedJansson =
    "{\"eis\":\"15\",\"type\":\"4\",\"value_char\":\"B\",\"value_float\":\"2.5\","
    "\"value_int\":\"1\",\"value_string\":\"Salle \\u00E9t\\u00E9\"}";

const char *kAccentedNlohmann =
    "{\"eis\":\"15\",\"type\":\"4\",\"value_char\":\"B\",\"value_float\":\"2.5\","
    "\"value_int\":\"1\",\"value_string\":\"Salle \\u00e9t\\u00e9\"}";

//"Salle ete" with both e acute as raw UTF-8 bytes.
KNXValue accentedValue()
{
    return makeValue(4, 15, 1, 2.5f, 'B', "Salle \xC3\xA9t\xC3\xA9");
}
}

TEST(KNXExternProcWire, NonAsciiDiffersOnlyByTheCaseOfTheHexEscape)
{
    //THE INVARIANT, identical before and after the port.
    EXPECT_EQ(lowerHexEscapes(string(kAccentedJansson)),
              lowerHexEscapes(string(kAccentedNlohmann)));
    EXPECT_NE(string(kAccentedJansson), string(kAccentedNlohmann));

    EXPECT_EQ(lowerHexEscapes(string(kAccentedNlohmann)),
              lowerHexEscapes(dumpKnxValue(accentedValue())));

    for (char c: dumpKnxValue(accentedValue()))
        EXPECT_LT((unsigned int)(unsigned char)c, 0x80u);

    //And the same, through the PRODUCTION builder this time.
    const string prod = eventMessage("1/2/3", "write", accentedValue(), true);
    EXPECT_NE(string::npos, prod.find("\\u00e9"));
    for (char c: prod)
        EXPECT_LT((unsigned int)(unsigned char)c, 0x80u);
}

TEST(KNXExternProcWire, NonAsciiExactBytes_DECLARED_DELTA)
{
    //DECLARED DELTA 1 of 3, and it has to flip on the SAME commit as its twin
    //in KNXCtrlWire_test.cpp: the two ends of this wire ship together.
    EXPECT_EQ(string(kAccentedNlohmann), dumpKnxValue(accentedValue()));
}

/******************************************************************************
 * 4. value_char - Utils::to_string(unsigned char) writes the CHARACTER, not
 *    the number. Same two corrupting paths as on the server end.
 ******************************************************************************/

TEST(KNXExternProcWire, ValueCharZeroDecodesBackToZeroWhateverTheLibrary)
{
    //THE INVARIANT: "" (jansson, NUL truncated away) and the escaped NUL
    //(nlohmann, NUL preserved) both decode back to 0.
    KNXValue zero = makeValue(3, 13, 0, 0.0f, 0, "");
    EXPECT_EQ(0, (int)parseKnxValue(dumpKnxValue(zero)).value_char);

    //Live witness: the assertion above would also hold if value_char had
    //stopped being decoded at all.
    KNXValue live = makeValue(3, 13, 0, 0.0f, 'Q', "");
    EXPECT_EQ('Q', (char)parseKnxValue(dumpKnxValue(live)).value_char);
}

TEST(KNXExternProcWire, ValueCharZeroExactBytes_DECLARED_DELTA)
{
    //DECLARED DELTA 2 of 3, on the common path: jansson truncates the
    //one-byte NUL string and emits "", nlohmann keeps it and emits the
    //escaped NUL.
    EXPECT_EQ(string("{\"eis\":\"13\",\"type\":\"3\",\"value_char\":\"\\u0000\","
                     "\"value_float\":\"0\",\"value_int\":\"0\","
                     "\"value_string\":\"\"}"),
              dumpKnxValue(makeValue(3, 13, 0, 0.0f, 0, "")));
}

TEST(KNXExternProcWire, ValueCharAboveAsciiExactBytes_DECLARED_DELTA)
{
    //DECLARED DELTA 3 of 3. 0xC8 alone is not valid UTF-8:
    //  jansson  -> the key is SILENTLY MISSING, peer decodes 0;
    //  nlohmann -> error_handler_t::replace turns it into U+FFFD, peer
    //              decodes 0xEF.
    //Pre-existing defect of the KNX wire for EIS 13/16 characters above 0x7F,
    //consigned in FINDINGS.md, NOT fixed here.
    KNXValue v = makeValue(3, 13, 200, 200.0f, 200, "");

    EXPECT_EQ(string("{\"eis\":\"13\",\"type\":\"3\",\"value_char\":\"\\ufffd\","
                     "\"value_float\":\"200\",\"value_int\":\"200\","
                     "\"value_string\":\"\"}"),
              dumpKnxValue(v));
    EXPECT_EQ(0xEF, (int)parseKnxValue(dumpKnxValue(v)).value_char);
}

/******************************************************************************
 * 5. Decoding what the server may send. Same tolerance contract as the other
 *    end: strings, booleans and numbers accepted, anything else recorded as
 *    an empty string, and never a throw.
 ******************************************************************************/

TEST(KNXExternProcWire, ANumberFieldIsStringifiedNotRejected)
{
    KNXValue v = parseKnxValue("{\"type\":3,\"eis\":13,\"value_int\":88,"
                               "\"value_float\":3.75,\"value_char\":\"Q\","
                               "\"value_string\":\"Bureau\"}");

    EXPECT_EQ(string(kFixtures[2].canonical), dumpKnxValue(v));
}

TEST(KNXExternProcWire, ABooleanFieldIsStringifiedTheJanssonWay)
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

TEST(KNXExternProcWire, MissingKeysLeaveTheDefaultsAndNothingThrows)
{
    //The escaped NUL under value_char is DECLARED DELTA 2 again: a default
    //KNXValue carries value_char = 0, so every case built on defaults shows
    //it. jansson emitted "" here for exactly the same reason.
    //Utils::from_string on an empty string never reaches the extractor, so
    //the destination keeps its value: eis stays at its member default of -1.
    KNXValue v = parseKnxValue("{\"type\":\"4\"}");

    EXPECT_EQ(string("{\"eis\":\"-1\",\"type\":\"4\",\"value_char\":\"\\u0000\","
                     "\"value_float\":\"0\",\"value_int\":\"0\","
                     "\"value_string\":\"\"}"),
              dumpKnxValue(v));
}

TEST(KNXExternProcWire, ADocumentThatIsNotAnObjectDecodesToTheDefaultValue)
{
    //KNXProcess::messageReceived() hands fromJson() whatever sits under the
    //"value" key of a "write" command, without checking it, and hands it
    //nothing at all when the key is absent.
    //Same DECLARED DELTA 2 on value_char as everywhere a default value is
    //serialized.
    const string expected =
        "{\"eis\":\"-1\",\"type\":\"0\",\"value_char\":\"\\u0000\","
        "\"value_float\":\"0\",\"value_int\":\"0\",\"value_string\":\"\"}";

    EXPECT_EQ(expected, dumpKnxValue(parseKnxValue("[1,2,3]")));
    EXPECT_EQ(expected, dumpKnxValue(parseKnxValue("\"nope\"")));
    EXPECT_EQ(expected, dumpKnxValue(parseKnxValue("null")));

    //A probe that can never become valid JSON.
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

TEST(KNXExternProcWire, RawNonUtf8BusBytesAreReplacedInsteadOfCrashing_DECLARED_DELTA)
{
    //DECLARED DELTA 4, on the end that actually reads the bus: this is the
    //very value monitorWait() builds from the frame it just received.
    KNXValue v = makeValue(4, 15, 0, 0.0f, 'B', "\xc9" "t" "\xe9");
    const string msg = eventMessage("1/2/3", "write", v, true);

    EXPECT_NE(string::npos, msg.find("\\ufffdt\\ufffd"));

    for (char c: msg)
        EXPECT_LT((unsigned int)(unsigned char)c, 0x80u);
    EXPECT_FALSE(Json::parse(msg, nullptr, false).is_discarded());
}
