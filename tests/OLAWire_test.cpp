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
 * E4.1f - CHARACTERIZATION of the OLA wire.
 *
 * Two programs, one protocol, both ends in this repository and in this commit:
 *
 *   calaos_server  IO/OLA/OLACtrl.cpp             EMITS  [{channel,value}, ...]
 *   calaos_ola     IO/OLA/OLAExternProc_main.cpp  DECODES it and pushes the
 *                                                 pairs into an ola::DmxBuffer
 *
 * They are built by the same Makefile.am, installed by the same package and
 * updated together. The DMX fixtures downstream never see this JSON - they
 * only ever see a DMX512 frame produced by olad. There is NO third party on
 * this wire, so the SHAPE of the bytes has nobody to protect. What has to be
 * proven is the STRUCTURE, the VALUES and above all their TYPES - and nothing
 * held any of that before this file: E4.0d measured that no test of the suite
 * ever executes a driver, and there is no other OLA test at all.
 *
 * ⭐ THE ONE THING THIS WIRE DOES THAT NO OTHER WIRE OF THE SERIES DOES:
 * IT CARRIES REAL JSON INTEGERS. OLACtrl.cpp builds its two fields with
 * json_integer(), not json_string(). The rule of the rest of E4.1 is
 * "everything goes out as a string"; here that reflex is the BUG. A `value`
 * that came back as "198" instead of 198 is a silent stop of the DMX driver,
 * in a zone with no other net.
 *
 * ⚠️ AND THE DECODER CANNOT CATCH THAT REGRESSION, measured: calaos_ola
 * flattens every entry through the jansson_decode_object() house rules into a
 * Params (a map of STRINGS) and re-parses with Utils::from_string(). Feed it
 * {"channel":"9","value":"3"} and it drives channel 9 at level 3 just as
 * happily as it does {"channel":9,"value":3} - the case
 * StringTypedEntriesAreAcceptedByTheDecoderWhichIsWhyTheEmitterMustBePinned
 * pins exactly that. THE TYPE PROOF THEREFORE HAS TO BE ON THE EMITTED TEXT,
 * which is what ChannelAndValueAreEmittedAsJsonIntegersNeverAsStrings and the
 * two byte-freezes do. A round-trip test alone would be an oracle that cannot
 * fail.
 *
 * ⚠️ THE ROOT IS AN ARRAY, NOT AN OBJECT. OLAExternProc_main.cpp tests
 * json_is_array(); an is_object() would refuse every real message. Pinned by
 * ATopLevelObjectIsRefusedBecauseTheRootOfThisWireIsAnArray.
 *
 * THE SEAM is the free functions of the anonymous namespace below. In the
 * characterization commit they carried the jansson bodies of the two
 * production files VERBATIM; THIS commit has rewired them onto the shipped
 * header IO/OLA/OLAWire.h, so a mutation of the SHIPPED emitter or decoder
 * now turns this suite RED. A test that re-implements the
 * assembly instead of calling it only freezes what the TEST does - measured on
 * a sibling ticket of this series, where 34/34 stayed green while the
 * production dump() was put back naked. Every assertion that moves when the
 * seam is rewired is flagged in place with MOVED BY E4.1f.
 *
 * WHY OLACtrl CANNOT BE CALLED DIRECTLY: its constructor SPAWNS calaos_ola
 * through an ExternProcServer, and it is only reachable through a static
 * singleton map. OLAExternProc_main.cpp defines main() and links libola.
 * Neither can be reached from a test, which is why the wire lives in a header
 * of free functions - same shape as IO/Mqtt/MqttWire.h, IO/Reolink/
 * ReolinkWire.h and IO/Wago/WagoWire.h next door.
 *
 * ⭐⭐ WHERE THE BYTES OF THIS WIRE COME FROM - MEASURED, and the answer
 * decides the status of the two byte oracles below.
 *   The emitted document contains exactly TWO KEYS, both ASCII string
 *   LITERALS written in the source ("channel", "value"), and exactly TWO
 *   VALUES, both C++ `int` turned into JSON integers. NO std::string IS EVER
 *   PUT INTO THIS JSON TREE, anywhere, by either end. The channels come from
 *   io.xml through Utils::from_string() into an int (OLAOutputLightDimmer.cpp,
 *   OLAOutputLightRGB.cpp); the level comes from OutputLightDimmer as a
 *   percentage, or from ColorValue::getRed/getGreen/getBlue(). The universe -
 *   the only string OLACtrl handles - is passed to the subprocess as argv[1]
 *   and NEVER enters the JSON.
 *   ⇒ NO NON-ASCII BYTE AND NO INVALID UTF-8 BYTE CAN REACH dump() ON THIS
 *   WIRE TODAY. The two byte oracles below are therefore DEFENSIVE, NOT
 *   LOAD-BEARING: they guard the emitter FUNCTION against a future field, the
 *   way Wago and KNX said it out loud rather than implying it. That is stated
 *   here instead of being dressed up as a reachable production input.
 *   They still have to exist, and they still have to be red on mutation:
 *   two sibling tickets measured ensure_ascii true->false and replace->ignore
 *   leaving whole suites GREEN. They drive a probe key "note" that NO
 *   production message carries and that no future value of channel/value can
 *   ever make valid.
 *   The KNX precedent is the reason the question was asked at all: there, raw
 *   bus bytes DID reach a naked dump() and a dimmer at 78% terminated the
 *   driver. On OLA the equivalent input is a DMX level - an int - so the same
 *   78% appears below as a NUMBER and can hurt nobody.
 *
 * A DELIBERATELY RICH FIXTURE. The recurring defect of the E4.0/E4.1 series is
 * the "poor fixture": a dataset too uniform for a swap of two interchangeable
 * fields to show. Here:
 *   - the three RGB channels are 11 / 42 / 137, three DIFFERENT values, never
 *     0/0/0;
 *   - the three colour components are 200 / 7 / 64, three DIFFERENT values,
 *     none equal to any of the three channels;
 *   - the dimmer uses channel 23 and 78%, which scales to 198, and 198 != 78
 *     so the *255/100 scaling cannot hide.
 *   NINE numbers, all different. Exchanging ANY two of them changes the
 *   emitted bytes.
 *   ⚠️ Accuracy, because this file counts everything else: they are NOT all
 *   free of substring relations - 7 is a substring of both 78 and 137. It
 *   costs nothing here, because every assertion compares either the WHOLE
 *   message string or a QUOTED form ("\"198\"", "\"137\"") - never a bare
 *   digit run inside a larger one. Do not weaken that if a case is added.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "Utils.h"
#include "Params.h"
#include "ColorUtils.h"

/* ⭐ THE PRODUCTION HEADER. Not a copy of it: the very text OLACtrl.cpp and
 * OLAExternProc_main.cpp include and the two binaries ship. Every seam below
 * is now a one-line forwarder, so a mutation of the SHIPPED emitter or of the
 * SHIPPED decoder turns this suite red. (In the characterization commit these
 * were the jansson bodies, copied verbatim.) */
#include "OLAWire.h"

using std::string;
using std::vector;

namespace
{

/*---------------------------------------------------------------------------
 * THE FIXTURE. Eight numbers, all different (see the header of this file).
 *--------------------------------------------------------------------------*/
const int FX_DIM_CHANNEL = 23;
const int FX_DIM_PERCENT = 78;   //-> 78 * 255 / 100 == 198, integer division
const int FX_DIM_LEVEL   = 198;

const int FX_RED_CHANNEL   = 11;
const int FX_GREEN_CHANNEL = 42;
const int FX_BLUE_CHANNEL  = 137;

const int FX_RED   = 200;
const int FX_GREEN = 7;
const int FX_BLUE  = 64;

/*---------------------------------------------------------------------------
 * THE SEAM - what calaos_server puts on the wire (OLACtrl.cpp:46-88).
 *
 * Both bodies are the shipped jansson ones, character for character, so that
 * this commit freezes what MASTER does and not what the migration will do.
 *--------------------------------------------------------------------------*/

//OLACtrl::setValue(). `value` is a PERCENTAGE (0-100); the *255/100 that turns
//it into a DMX level lives in the emitter, so it is part of what is frozen.
//The two ints are wrapped in the two DISTINCT types of the wire on the way in:
//written this way, permuting them here does not compile.
string wireSetValue(int channel, int value)
{
    return OLAWire::buildSetValueMessage(OLAWire::DmxChannel(channel),
                                         OLAWire::DimmerPercent(value));
}

//OLACtrl::setColor(). NOTE THE ASYMMETRY, deliberate and pinned: the colour
//components are emitted RAW (0-255 already), with NO *255/100.
string wireSetColor(const ColorValue &color, int channel_red, int channel_green, int channel_blue)
{
    return OLAWire::buildSetColorMessage(color,
                                         OLAWire::RedChannel(channel_red),
                                         OLAWire::GreenChannel(channel_green),
                                         OLAWire::BlueChannel(channel_blue));
}

/*
 * THE PROBE EMITTER, for the two byte oracles only.
 *
 * "note" is a key NO production message of this wire carries and none can
 * grow into: the wire is closed at two integer fields. It exists so that the
 * two invariants of the SERIALIZER (ensure_ascii, error handler) have
 * something to bite on, because no production field of this wire is a string.
 * See the header of this file - these two cases are DEFENSIVE.
 *
 * It goes through OLAWire::dumpJson(), which is THE dump of this wire and the
 * one buildSetValueMessage() and buildSetColorMessage() call: mutating that
 * single production line turns both oracles red.
 */
string wireDumpProbe(const string &note)
{
    Json jroot = Json::array();

    Json entry;
    entry["channel"] = 7;
    entry["note"] = note;
    jroot.push_back(entry);

    return OLAWire::dumpJson(jroot);
}

/*---------------------------------------------------------------------------
 * THE SEAM - what calaos_ola makes of it (OLAExternProc_main.cpp:52-87).
 *--------------------------------------------------------------------------*/

//The struct calaos_ola actually hands to ola::DmxBuffer::SetChannel(), with
//NAMED members - two positional unsigned ints could be swapped in silence.
typedef OLAWire::ChannelValue WireChannelValue;

bool wireDecode(const string &msg, vector<WireChannelValue> &out)
{
    return OLAWire::decodeMessage(msg, out);
}

/*---------------------------------------------------------------------------
 * Helpers
 *--------------------------------------------------------------------------*/

bool isPureAscii(const string &s)
{
    for (size_t i = 0;i < s.size();i++)
    {
        if (static_cast<unsigned char>(s[i]) > 0x7F)
            return false;
    }
    return true;
}

//Readable failure messages: show the bytes, not the terminal's idea of them.
string escaped(const string &s)
{
    string out;
    char buf[8];
    for (size_t i = 0;i < s.size();i++)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 0x20 && c < 0x7F)
            out.push_back(static_cast<char>(c));
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
 * ⭐ THE TYPE PROOF - the most important assertion of this ticket, and the
 * reason it is written first.
 *
 * OLACtrl is the ONLY emitter of the whole E4.1 series that uses json_integer.
 * An implementer who carries over the "everything goes out as a string" reflex
 * of the sibling tickets breaks the DMX driver silently. This case fails the
 * moment `channel` or `value` leaves between quotes - on the PARSED type AND
 * on the raw bytes, because a semantic oracle alone would be satisfied by
 * Json("198") == Json("198").
 ******************************************************************************/

TEST(OLAWire, ChannelAndValueAreEmittedAsJsonIntegersNeverAsStrings)
{
    const string wire = wireSetValue(FX_DIM_CHANNEL, FX_DIM_PERCENT);

    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    ASSERT_TRUE(j.is_array()) << escaped(wire);
    ASSERT_EQ(1u, j.size()) << escaped(wire);
    ASSERT_TRUE(j[0].is_object()) << escaped(wire);

    //TYPE, not just value. j[0]["value"] must be a NUMBER.
    EXPECT_TRUE(j[0].at("channel").is_number_integer()) << escaped(wire);
    EXPECT_TRUE(j[0].at("value").is_number_integer()) << escaped(wire);
    EXPECT_FALSE(j[0].at("channel").is_string()) << escaped(wire);
    EXPECT_FALSE(j[0].at("value").is_string()) << escaped(wire);

    //Type-strict oracle of the series: 198 is not "198".
    EXPECT_NE(Json(string("198")), j[0].at("value")) << escaped(wire);
    EXPECT_NE(Json(string("23")), j[0].at("channel")) << escaped(wire);

    EXPECT_EQ(FX_DIM_CHANNEL, j[0].at("channel").get<int>()) << escaped(wire);
    EXPECT_EQ(FX_DIM_LEVEL, j[0].at("value").get<int>()) << escaped(wire);

    //And on the BYTES, because that is what actually leaves the process.
    EXPECT_EQ(string::npos, wire.find("\"198\"")) << escaped(wire);
    EXPECT_EQ(string::npos, wire.find("\"23\"")) << escaped(wire);
}

TEST(OLAWire, TheThreeRgbChannelsAreEmittedAsJsonIntegersToo)
{
    const string wire = wireSetColor(ColorValue::fromRgb(FX_RED, FX_GREEN, FX_BLUE),
                                     FX_RED_CHANNEL, FX_GREEN_CHANNEL, FX_BLUE_CHANNEL);

    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    ASSERT_TRUE(j.is_array()) << escaped(wire);
    ASSERT_EQ(3u, j.size()) << escaped(wire);

    for (size_t i = 0;i < j.size();i++)
    {
        EXPECT_TRUE(j[i].at("channel").is_number_integer()) << i << " " << escaped(wire);
        EXPECT_TRUE(j[i].at("value").is_number_integer()) << i << " " << escaped(wire);
    }

    //No quoted number anywhere in the message.
    EXPECT_EQ(string::npos, wire.find("\"200\"")) << escaped(wire);
    EXPECT_EQ(string::npos, wire.find("\"137\"")) << escaped(wire);
}

/*******************************************************************************
 * STRUCTURE AND VALUES
 ******************************************************************************/

//The dimmer message: ONE entry, TWO keys, and the percentage scaled to a DMX
//level by INTEGER arithmetic. 78% is 198, not 199 and not 78.
TEST(OLAWire, TheDimmerMessageCarriesOneEntryAndScalesThePercentageToADmxLevel)
{
    const Json j = Json::parse(wireSetValue(FX_DIM_CHANNEL, FX_DIM_PERCENT), nullptr, false);

    ASSERT_FALSE(j.is_discarded());
    ASSERT_EQ(1u, j.size());
    EXPECT_EQ(2u, j[0].size()); //channel and value, nothing else
    EXPECT_EQ(FX_DIM_CHANNEL, j[0].at("channel").get<int>());
    EXPECT_EQ(198, j[0].at("value").get<int>());

    //the scaling is INTEGER division, and the ends are exact
    EXPECT_EQ(0, Json::parse(wireSetValue(5, 0), nullptr, false)[0].at("value").get<int>());
    EXPECT_EQ(255, Json::parse(wireSetValue(5, 100), nullptr, false)[0].at("value").get<int>());
    EXPECT_EQ(2, Json::parse(wireSetValue(5, 1), nullptr, false)[0].at("value").get<int>());
    EXPECT_EQ(127, Json::parse(wireSetValue(5, 50), nullptr, false)[0].at("value").get<int>());
}

/*
 * ⭐ THE ANTI-SWAP CASE (acceptance criterion 4 of E4.1f).
 *
 * Three DIFFERENT channels and three DIFFERENT colour components, so that
 * exchanging channel_red and channel_blue - in the fixture or in the emitter -
 * shows. With 0/0/0, or with red == blue, the mutation would not bite and this
 * case would be decoration.
 *
 * The ORDER of the array is part of the contract too: calaos_ola replays the
 * entries into the DmxBuffer in wire order.
 */
TEST(OLAWire, RedGreenAndBlueEachGoToTheirOwnChannelInThatOrder)
{
    const string wire = wireSetColor(ColorValue::fromRgb(FX_RED, FX_GREEN, FX_BLUE),
                                     FX_RED_CHANNEL, FX_GREEN_CHANNEL, FX_BLUE_CHANNEL);
    const Json j = Json::parse(wire, nullptr, false);

    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    ASSERT_EQ(3u, j.size()) << escaped(wire);

    EXPECT_EQ(FX_RED_CHANNEL, j[0].at("channel").get<int>()) << escaped(wire);
    EXPECT_EQ(FX_RED, j[0].at("value").get<int>()) << escaped(wire);

    EXPECT_EQ(FX_GREEN_CHANNEL, j[1].at("channel").get<int>()) << escaped(wire);
    EXPECT_EQ(FX_GREEN, j[1].at("value").get<int>()) << escaped(wire);

    EXPECT_EQ(FX_BLUE_CHANNEL, j[2].at("channel").get<int>()) << escaped(wire);
    EXPECT_EQ(FX_BLUE, j[2].at("value").get<int>()) << escaped(wire);

    //Every entry has exactly the two keys of this wire.
    for (size_t i = 0;i < j.size();i++)
        EXPECT_EQ(2u, j[i].size()) << i << " " << escaped(wire);
}

//The colour components are NOT scaled, unlike the dimmer percentage. If the
//*255/100 of setValue() were ever copied into setColor(), 200 would become 510
//and this case would go red.
TEST(OLAWire, ColourComponentsAreEmittedRawAndAreNotScaledLikeTheDimmerPercentage)
{
    const Json j = Json::parse(wireSetColor(ColorValue::fromRgb(FX_RED, FX_GREEN, FX_BLUE),
                                            FX_RED_CHANNEL, FX_GREEN_CHANNEL, FX_BLUE_CHANNEL),
                               nullptr, false);

    ASSERT_FALSE(j.is_discarded());
    EXPECT_EQ(200, j[0].at("value").get<int>());
    EXPECT_EQ(7, j[1].at("value").get<int>());
    EXPECT_EQ(64, j[2].at("value").get<int>());

    //255 would be the scaled form of 100 - proving the two rules are distinct
    EXPECT_NE(510, j[0].at("value").get<int>());
}

/*******************************************************************************
 * THE EXACT BYTES
 *
 * These two do NOT move across the migration and that is the point: jansson
 * with JSON_COMPACT|JSON_ENSURE_ASCII and nlohmann with
 * dump(-1, ' ', true, replace) produce the same text here, because the two
 * keys are already in alphabetical order ("channel" < "value") and every value
 * is an integer. If either of the two byte strings below moves, the wire moved.
 ******************************************************************************/

TEST(OLAWire, TheDimmerMessageIsExactlyThisByteString)
{
    EXPECT_EQ("[{\"channel\":23,\"value\":198}]",
              wireSetValue(FX_DIM_CHANNEL, FX_DIM_PERCENT));
}

TEST(OLAWire, TheRgbMessageIsExactlyThisByteString)
{
    EXPECT_EQ("[{\"channel\":11,\"value\":200},"
              "{\"channel\":42,\"value\":7},"
              "{\"channel\":137,\"value\":64}]",
              wireSetColor(ColorValue::fromRgb(FX_RED, FX_GREEN, FX_BLUE),
                           FX_RED_CHANNEL, FX_GREEN_CHANNEL, FX_BLUE_CHANNEL));
}

/*******************************************************************************
 * WHAT calaos_ola MAKES OF IT
 ******************************************************************************/

//The round trip that matters: what the server emits is exactly what the
//process pushes into the DmxBuffer, in order.
TEST(OLAWire, TheProcessDecodesTheRgbMessageIntoThreeChannelsInWireOrder)
{
    const string wire = wireSetColor(ColorValue::fromRgb(FX_RED, FX_GREEN, FX_BLUE),
                                     FX_RED_CHANNEL, FX_GREEN_CHANNEL, FX_BLUE_CHANNEL);

    vector<WireChannelValue> out;
    ASSERT_TRUE(wireDecode(wire, out)) << escaped(wire);
    ASSERT_EQ(3u, out.size());

    EXPECT_EQ(11u, out[0].channel);
    EXPECT_EQ(200u, out[0].value);
    EXPECT_EQ(42u, out[1].channel);
    EXPECT_EQ(7u, out[1].value);
    EXPECT_EQ(137u, out[2].channel);
    EXPECT_EQ(64u, out[2].value);
}

TEST(OLAWire, TheProcessDecodesTheDimmerMessageIntoTheScaledLevel)
{
    vector<WireChannelValue> out;
    ASSERT_TRUE(wireDecode(wireSetValue(FX_DIM_CHANNEL, FX_DIM_PERCENT), out));
    ASSERT_EQ(1u, out.size());
    EXPECT_EQ(23u, out[0].channel);
    EXPECT_EQ(198u, out[0].value);
}

/*
 * ⚠️ THE ROOT OF THIS WIRE IS AN ARRAY. OLAExternProc_main.cpp:57 tests
 * json_is_array(); turning that into is_object() during the migration would
 * refuse every real message and stop the driver in silence.
 */
TEST(OLAWire, ATopLevelObjectIsRefusedBecauseTheRootOfThisWireIsAnArray)
{
    vector<WireChannelValue> out;
    EXPECT_FALSE(wireDecode("{\"channel\":11,\"value\":200}", out));
    EXPECT_TRUE(out.empty());
}

TEST(OLAWire, MalformedAndScalarMessagesAreRefused)
{
    vector<WireChannelValue> out;

    EXPECT_FALSE(wireDecode("", out));
    EXPECT_FALSE(wireDecode("[", out));
    EXPECT_FALSE(wireDecode("[{\"channel\":11,}]", out));
    EXPECT_FALSE(wireDecode("not json at all", out));
    //a bare scalar: jansson was called with no JSON_DECODE_ANY
    EXPECT_FALSE(wireDecode("12", out));
    EXPECT_FALSE(wireDecode("\"hello\"", out));
    EXPECT_FALSE(wireDecode("null", out));

    EXPECT_TRUE(out.empty());
}

//An empty array is a VALID message that drives nothing. It must be accepted,
//because the process sends the buffer to olad after every accepted message.
TEST(OLAWire, AnEmptyArrayIsAcceptedAndDrivesNothing)
{
    vector<WireChannelValue> out;
    EXPECT_TRUE(wireDecode("[]", out));
    EXPECT_TRUE(out.empty());
}

//An entry that is missing either key is SKIPPED, not defaulted, and it does
//not stop the entries around it.
TEST(OLAWire, EntriesMissingChannelOrValueAreSkippedAndTheOthersStillGoThrough)
{
    vector<WireChannelValue> out;
    ASSERT_TRUE(wireDecode("[{\"channel\":11,\"value\":200},"
                           "{\"channel\":42},"
                           "{\"value\":7},"
                           "{},"
                           "{\"channel\":137,\"value\":64}]", out));
    ASSERT_EQ(2u, out.size());
    EXPECT_EQ(11u, out[0].channel);
    EXPECT_EQ(200u, out[0].value);
    EXPECT_EQ(137u, out[1].channel);
    EXPECT_EQ(64u, out[1].value);
}

//An array entry that is not an object at all is skipped: json_object_foreach()
//on a non-object iterated zero times, so the Params stayed empty.
TEST(OLAWire, ArrayEntriesThatAreNotObjectsAreSkipped)
{
    vector<WireChannelValue> out;
    ASSERT_TRUE(wireDecode("[42,\"x\",null,[1,2],{\"channel\":11,\"value\":200}]", out));
    ASSERT_EQ(1u, out.size());
    EXPECT_EQ(11u, out[0].channel);
    EXPECT_EQ(200u, out[0].value);
}

//Extra keys are ignored, they do not make the entry invalid.
TEST(OLAWire, UnknownKeysInAnEntryAreIgnored)
{
    vector<WireChannelValue> out;
    ASSERT_TRUE(wireDecode("[{\"channel\":11,\"value\":200,\"universe\":3,\"note\":\"x\"}]", out));
    ASSERT_EQ(1u, out.size());
    EXPECT_EQ(11u, out[0].channel);
    EXPECT_EQ(200u, out[0].value);
}

/*
 * ⭐ WHY THE TYPE PROOF HAS TO BE ON THE EMITTER AND CANNOT BE A ROUND TRIP.
 *
 * The decoder flattens EVERY value to a string (jansson_decode_object's house
 * rules) and re-parses with Utils::from_string(). So it accepts the string
 * form of the message just as happily as the integer form, and a regression of
 * the emitter to json_string() would leave every round-trip case GREEN. This
 * case exists to make that blind spot explicit and permanent.
 */
TEST(OLAWire, StringTypedEntriesAreAcceptedByTheDecoderWhichIsWhyTheEmitterMustBePinned)
{
    vector<WireChannelValue> out;
    ASSERT_TRUE(wireDecode("[{\"channel\":\"11\",\"value\":\"200\"}]", out));
    ASSERT_EQ(1u, out.size());
    EXPECT_EQ(11u, out[0].channel);
    EXPECT_EQ(200u, out[0].value);
}

/*
 * The rest of the flattening contract, pinned because the migration has to
 * reproduce it by hand:
 *  - a real (non integer) number goes through Utils::to_string(double), a bare
 *    ostringstream: 11.0 -> "11", 11.9 -> "11.9" which from_string() reads as
 *    11 (it stops at the dot and ignores its own false return);
 *  - a boolean becomes the WORD "true"/"false", which from_string() cannot
 *    read at all -> 0, and the key IS present so the entry is NOT skipped;
 *  - an object/array/null becomes the EMPTY STRING, present but unreadable
 *    -> 0, and again the entry is NOT skipped. Present-and-empty is not the
 *    same answer as absent (which IS skipped, see the case above).
 */
TEST(OLAWire, TheFlatteningHouseRulesDecideWhatANonIntegerEntryDrives)
{
    vector<WireChannelValue> out;
    ASSERT_TRUE(wireDecode("[{\"channel\":11.0,\"value\":200.0},"
                           "{\"channel\":42.9,\"value\":7.9},"
                           "{\"channel\":true,\"value\":64},"
                           "{\"channel\":137,\"value\":null}]", out));
    ASSERT_EQ(4u, out.size());

    EXPECT_EQ(11u, out[0].channel);
    EXPECT_EQ(200u, out[0].value);

    EXPECT_EQ(42u, out[1].channel);   //"42.9" -> 42
    EXPECT_EQ(7u, out[1].value);      //"7.9"  -> 7

    EXPECT_EQ(0u, out[2].channel);    //"true" -> extraction FAILS -> C++11 stores 0
    EXPECT_EQ(64u, out[2].value);

    EXPECT_EQ(137u, out[3].channel);
    /* ⭐ FLIPPED BY THE FIX COMMIT. Before it, this assertion could not exist:
     * "value":null flattens to the EMPTY STRING, PASSES the Exists() gate, and
     * Utils::from_string("") writes NOTHING (see the next case), so the shipped
     * decoder handed an UNINITIALIZED unsigned int to
     * ola::DmxBuffer::SetChannel() - three runs of this suite read 21845,
     * 22007 and 64 there. OLAWire::decodeMessage() now zero-initializes its
     * ChannelValue, so an unreadable field drives channel 0 / level 0 instead
     * of a random one.
     * ⚠️ Honest about this one oracle: reverting the initializer makes this
     * assertion INDETERMINATE, not guaranteed red. It was red on every run
     * measured (six of six), but a compiler that happened to leave a zero in
     * that slot would let it pass. It is the only assertion of this file in
     * that situation. */
    EXPECT_EQ(0u, out[3].value);
}

/*
 * ⚠️ THE DEFECT, pinned ON THE PRIMITIVE so that the case is DETERMINISTIC
 * instead of asserting on stack garbage.
 *
 * Utils::from_string() is an istringstream extraction. On an EMPTY string the
 * stream's sentry fails before operator>> ever runs, so the C++11 rule that
 * stores 0 into the destination on a FAILED extraction never applied: the
 * destination kept whatever it held. And because the sentry's lookahead set
 * eofbit, from_string() returned TRUE - it claimed success while having
 * written nothing at all.
 *
 * ⭐ T3.25 CLOSED THAT HOLE IN THE PRIMITIVE ITSELF, and this case is rewritten
 * accordingly rather than deleted. E4.1f said "this ticket does NOT change it
 * (Utils::from_string is used everywhere)", and it was right for E4.1f: the
 * decoder fix below is what E4.1f owned, and it still stands on its own -
 * OLAWire must not hand an unwritten variable to the DMX buffer whatever the
 * primitive does. What changed is that the primitive no longer offers one:
 * from_string("") now answers FALSE and writes T{}.
 *
 * Contrast with a non-empty unreadable string ("true"): there the sentry
 * succeeds, the extraction fails, the destination IS set to 0 and the return
 * is false. THAT half is untouched by T3.25 - and it is the half that matters
 * for every guard written against a garbage value.
 */
TEST(OLAWire, AnEmptyStringIsRefusedByFromStringAndNoLongerLeavesTheDestinationAlone)
{
    //T3.25: was AnEmptyStringMakesFromStringWriteNothingAndStillClaimSuccess,
    //and the two assertions below are its exact opposites. The sentinel is
    //kept as it was (0xA5A5A5A5, never 0) so that "not written" and "written
    //zero" stay distinguishable.
    unsigned int a = 0xA5A5A5A5u;
    EXPECT_FALSE(Utils::from_string(string(""), a))
            << "from_string(\"\") claims success again: T3.25 has been reverted";
    EXPECT_EQ(0u, a)
            << "from_string(\"\") left the destination untouched again";

    //unchanged by T3.25, and the reason the decoder fix of E4.1f is still the
    //one doing the work for a NON EMPTY unreadable channel
    unsigned int b = 0xA5A5A5A5u;
    EXPECT_FALSE(Utils::from_string(string("true"), b));
    EXPECT_EQ(0u, b);
}

/*******************************************************************************
 * ⭐ THE TWO BYTE ORACLES - DEFENSIVE, and said so.
 *
 * See the header of this file: NO STRING EVER REACHES THIS WIRE IN PRODUCTION,
 * so no reachable input can put a non-ASCII or invalid byte in it today. These
 * two cases guard the SERIALIZER of this wire, which is shared by both
 * emitters, and they are here because two sibling tickets MEASURED that
 * flipping ensure_ascii or swapping the error handler left every other test
 * green - the 145 goldens compare PARSED DOCUMENTS and are blind to escaping
 * by construction.
 *
 * They drive the probe key "note", which no production message carries.
 ******************************************************************************/

/*
 * RED IF ensure_ascii IS EVER DROPPED. Without it nlohmann writes the raw
 * UTF-8 bytes and the wire stops being pure ASCII - which jansson's
 * JSON_ENSURE_ASCII never allowed.
 */
TEST(OLAWire, TheWireStaysPureAsciiWhenAStringFieldIsNot)
{
    const string wire = wireDumpProbe("caf\xc3\xa9-\xc3\x80lpha"); //U+00E9 then U+00C0

    EXPECT_TRUE(isPureAscii(wire)) << escaped(wire);

    //MOVED BY E4.1f: jansson escaped with UPPERCASE hex, nlohmann escapes with
    //LOWERCASE hex. That is the ONE byte difference this migration makes on a
    //non-ASCII field, and no JSON parser can see it. The four assertions are
    //written both ways round on purpose, so that the case pins WHICH of the
    //two forms is on the wire and not merely that one of them is.
    EXPECT_NE(string::npos, wire.find("\\u00e9")) << escaped(wire);
    EXPECT_NE(string::npos, wire.find("\\u00c0")) << escaped(wire);
    EXPECT_EQ(string::npos, wire.find("\\u00E9")) << escaped(wire);
    EXPECT_EQ(string::npos, wire.find("\\u00C0")) << escaped(wire);

    //parseable, and the value round-trips to the same UTF-8 it came from
    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    ASSERT_TRUE(j.is_array()) << escaped(wire);
    EXPECT_EQ("caf\xc3\xa9-\xc3\x80lpha", j[0].at("note").get<string>());
}

/*
 * RED IF error_handler_t::replace IS EVER DROPPED - and red DIFFERENTLY for
 * each of the three spellings, which is what pins the RIGHT one:
 *   replace : one U+FFFD per rejected byte, written � because the dump is
 *             ensure_ascii   -> the assertions below hold;
 *   ignore  : the rejected byte simply DISAPPEARS, the note reads "bad--tail"
 *             -> the EXPECT_EQ on the value and the find("\\ufffd") both fail;
 *   strict  : dump() throws type_error.316 -> the ASSERT_NO_THROW fails.
 * Three spellings, three different reds. This is not theoretical: E4.0
 * measured that a naked dump() of foreign bytes is std::terminate on a live
 * process, and E4.1e watched a KNX driver die that way on a dimmer at 78%.
 */
TEST(OLAWire, InvalidUtf8InAStringFieldDoesNotAbortTheEmission)
{
    string wire;
    ASSERT_NO_THROW(wire = wireDumpProbe("bad-\xff-tail"))
            << "the emitter threw on invalid UTF-8";

    EXPECT_TRUE(isPureAscii(wire)) << escaped(wire);

    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    ASSERT_TRUE(j.is_array()) << escaped(wire);
    ASSERT_EQ(1u, j.size()) << escaped(wire);

    //MOVED BY E4.1f. Under jansson, json_string() answered NULL on the bad
    //byte, json_object_set_new() answered -1, neither return code was tested,
    //and the WHOLE PAIR was dropped on the floor: the entry left with a
    //single key. Under nlohmann the key is THERE, with the bad byte turned
    //into U+FFFD - the wire stops lying about which fields were sent.
    //
    //THIS IS WHERE THE THREE SPELLINGS SEPARATE, and why the assertions are on
    //the VALUE and on the BYTES rather than on "it did not throw":
    //  replace : one U+FFFD per rejected byte -> everything below holds;
    //  ignore  : the byte DISAPPEARS, the note reads "bad--tail" -> the
    //            EXPECT_EQ on the value and the find("\\ufffd") both fail;
    //  strict  : dump() throws type_error.316 -> the ASSERT_NO_THROW fails.
    EXPECT_EQ(2u, j[0].size()) << escaped(wire);
    ASSERT_TRUE(j[0].contains("note")) << escaped(wire);
    EXPECT_EQ("bad-\xef\xbf\xbd-tail", j[0].at("note").get<string>()) << escaped(wire);
    EXPECT_NE(string::npos, wire.find("\\ufffd")) << escaped(wire);
    EXPECT_EQ(string::npos, wire.find("bad--tail")) << escaped(wire);

    //what survives, either way
    EXPECT_EQ(7, j[0].at("channel").get<int>()) << escaped(wire);
}
