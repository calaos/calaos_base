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

/*******************************************************************************
 * T3.33 - THE TWO ADDRESS DECODERS, and what they put on the KNX bus.
 *
 * eKnxGroupAddr() is the last thing that runs before EIBSendGroup(): whatever
 * it answers IS the group that gets written. Nothing in this repository ever
 * exercised it.
 *
 * ⛔ THE FINDING THAT OPENED THIS PERIMETER WAS WRONG, and the case
 * SplitPadsTheTokenListToThree exists so that nobody "re-fixes" it: it
 * announced an OUT OF BOUNDS read of tokens[1]/tokens[2] on an address with
 * fewer than three components. Utils::split() PADS its list up to `max`
 * (StringUtils.cpp:210), so with max = 3 the vector always carries three
 * entries and there is no out of bounds access and no segfault. The real
 * defect is quieter: the padding is "", the components that were never given
 * are invented, and the write lands on ANOTHER GROUP.
 *
 * THE SEAM, same discipline as the four above: the two bodies below are copied
 * VERBATIM from KNXExternProc_main.cpp:140-164, because that translation unit
 * ends on EXTERN_PROC_CLIENT_MAIN and cannot be linked here. The fix commit
 * replaces both bodies with a call into the production header. Assertions that
 * move with it are flagged _DECLARED_DELTA.
 *
 * ⭐ 0x5555 IN a, b AND c. Production declares `int a, b, c;` with no
 * initialiser; an uninitialised int is very often 0, so a test that only says
 * "not 3" passes by accident. Seeding them with 21845 - the value calaos_ola
 * put on a DMX channel through this same defect - makes an unwritten component
 * name itself: 21845 masks down to 21/5/85, group address 44373.
 ******************************************************************************/

#include <vector>
#include "Utils.h"

namespace
{

const int       T33_SEED       = 0x5555;
//21/5/85, what a group address made of three never-written components reads as.
const eibaddr_t T33_SEED_GROUP = 44373;
//1.5.85, the same three components through the physical layout.
const eibaddr_t T33_SEED_PHYS  = 0x1555;

bool seamGroupAddr(const string &group_addr, eibaddr_t &out)
{
    std::vector<string> tokens;
    Utils::split(group_addr, tokens, "/", 3);
    int a = T33_SEED, b = T33_SEED, c = T33_SEED;
    Utils::from_string(tokens[0], a);
    Utils::from_string(tokens[1], b);
    Utils::from_string(tokens[2], c);
    out = (eibaddr_t)(((a & 0x01F) << 11) |
                      ((b & 0x07) << 8) |
                      (c & 0xFF));
    //Production has no way to say no: the signature returns the address alone.
    return true;
}

bool seamPhysicalAddr(const string &addr, eibaddr_t &out)
{
    std::vector<string> tokens;
    Utils::split(addr, tokens, ".", 3);
    int a = T33_SEED, b = T33_SEED, c = T33_SEED;
    Utils::from_string(tokens[0], a);
    Utils::from_string(tokens[1], b);
    Utils::from_string(tokens[2], c);
    out = (eibaddr_t)(((a & 0x0F) << 12) |
                      ((b & 0x0F) << 8) |
                      (c & 0xFF));
    return true;
}

} //namespace

/* ⭐ THE FACT THAT INVALIDATES THE ORIGINAL FINDING. Frozen explicitly, and
 * deliberately asserted on Utils::split() itself rather than through the
 * decoders, so that it stays true no matter what they become. */
TEST(KNXExternProcAddr, SplitPadsTheTokenListToThree)
{
    std::vector<string> tokens;
    Utils::split("1", tokens, "/", 3);
    ASSERT_EQ(3u, tokens.size()) << "no out of bounds read: split() pads";
    EXPECT_EQ("1", tokens[0]);
    EXPECT_EQ("", tokens[1]);
    EXPECT_EQ("", tokens[2]);

    tokens.clear();
    Utils::split("", tokens, "/", 3);
    EXPECT_EQ(3u, tokens.size());

    //And what it does with a FOURTH component: the remainder lands whole in
    //the last token, which is why "1/2/3/4" is refusable on the token alone.
    tokens.clear();
    Utils::split("1/2/3/4", tokens, "/", 3);
    ASSERT_EQ(3u, tokens.size());
    EXPECT_EQ("3/4", tokens[2]);
}

TEST(KNXExternProcAddr, AGroupAddressWithOneComponentStillReachesTheBus_DECLARED_DELTA)
{
    eibaddr_t out = T33_SEED_GROUP;

    //DECLARED DELTA 5 - the fix commit reads EXPECT_FALSE.
    EXPECT_TRUE(seamGroupAddr("1", out)) << "characterization: nothing refuses it";
    EXPECT_EQ(1 << 11, out) << "and 1 is written as group 1/0/0";
    EXPECT_NE(T33_SEED_GROUP, out);
}

TEST(KNXExternProcAddr, AnOutOfRangeGroupComponentIsMaskedInsteadOfRefused_DECLARED_DELTA)
{
    eibaddr_t out = T33_SEED_GROUP;

    //DECLARED DELTA 6. ⭐ The masks are not a guard: 300 & 0xFF is 44, so a
    //write meant for a group that does not exist lands on 1/2/44, which does.
    EXPECT_TRUE(seamGroupAddr("1/2/300", out));
    EXPECT_EQ((1 << 11) | (2 << 8) | 44, out);
}

TEST(KNXExternProcAddr, AnEmptyGroupAddressStillReachesTheBus_DECLARED_DELTA)
{
    eibaddr_t out = T33_SEED_GROUP;

    //DECLARED DELTA 7. An io.xml with no group address writes to group 0/0/0.
    EXPECT_TRUE(seamGroupAddr("", out));
    EXPECT_EQ(0, out);
}

TEST(KNXExternProcAddr, APhysicalAddressWithOneComponentStillResolves_DECLARED_DELTA)
{
    eibaddr_t out = T33_SEED_PHYS;

    //DECLARED DELTA 8. eKnxPhysicalAddr() has no caller in the tree today -
    //measured - so this one is a contract, not a live path.
    EXPECT_TRUE(seamPhysicalAddr("1", out));
    EXPECT_EQ(1 << 12, out);
}

/* ⭐ THE WITNESS. Green before AND after the fix. It says the seeded pattern is
 * doing its job: since T3.25 from_string() writes its destination on every
 * path, so 21845 no longer survives a failed decode. If this turns red,
 * from_string() has gone back to leaving `dest` alone and the three
 * uninitialised components of both decoders are live again. */
TEST(KNXExternProcAddr, TheSeededComponentsNeverReachTheBus)
{
    const char *const addrs[] = { "", "1", "1/", "//", "x/y/z", "1/2" };

    for (const char *const a: addrs)
    {
        eibaddr_t group = T33_SEED_GROUP;
        seamGroupAddr(a, group);
        EXPECT_NE(T33_SEED_GROUP, group) << a;

        eibaddr_t phys = T33_SEED_PHYS;
        seamPhysicalAddr(a, phys);
        EXPECT_NE(T33_SEED_PHYS, phys) << a;
    }
}

/* THE ACQUIS - the non-regression half. Both layouts, both ends of their
 * range, and they must answer exactly this before and after the fix. */
TEST(KNXExternProcAddr, AWellFormedGroupAddressStillResolves)
{
    eibaddr_t out = T33_SEED_GROUP;

    ASSERT_TRUE(seamGroupAddr("1/2/3", out));
    EXPECT_EQ((1 << 11) | (2 << 8) | 3, out);

    ASSERT_TRUE(seamGroupAddr("0/0/0", out));
    EXPECT_EQ(0, out);

    //The widest address the 5/3/8 layout can carry.
    ASSERT_TRUE(seamGroupAddr("31/7/255", out));
    EXPECT_EQ(0xFFFF, out);
}

TEST(KNXExternProcAddr, AWellFormedPhysicalAddressStillResolves)
{
    eibaddr_t out = T33_SEED_PHYS;

    ASSERT_TRUE(seamPhysicalAddr("1.2.3", out));
    EXPECT_EQ((1 << 12) | (2 << 8) | 3, out);

    ASSERT_TRUE(seamPhysicalAddr("15.15.255", out));
    EXPECT_EQ(0xFFFF, out);
}
