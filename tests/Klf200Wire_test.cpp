/******************************************************************************
 **  Copyright (c) 2026, Calaos. All Rights Reserved.
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
 * Characterization of the Klf200 wire (IO/Klf200/Klf200Wire.h): the three
 * messages Klf200Ctrl sends to calaos_klf200, the decoding of what comes back,
 * and the two field readers the IOs rely on. The other end is
 * IO/Klf200/ExternProcKlf200_main.py, pinned by tests/python/test_klf200.py.
 *
 * Same seam as tests/ReolinkWire_test.cpp: the production header is called
 * directly, so a mutation of the shipped emitter or decoder turns this red.
 */

#include <gtest/gtest.h>

#include "Params.h"
#include "Klf200Wire.h"

using Klf200Types::Host;
using Klf200Types::NodeId;
using Klf200Types::Password;

//----------------------------------------------------------------------------
// Emission
//----------------------------------------------------------------------------

TEST(Klf200WireRegister, CarriesTheFiveFieldsWithTypedValues)
{
    const std::string wire = Klf200Wire::buildRegisterMessage(Host("klf200.lan"),
                                                              Password("klf200123"),
                                                              NodeId(3),
                                                              true);
    const Json j = Json::parse(wire);

    EXPECT_EQ("register", j.at("action").get<std::string>());
    EXPECT_EQ("klf200.lan", j.at("host").get<std::string>());
    EXPECT_EQ("klf200123", j.at("password").get<std::string>());
    //node_id and rain travel as JSON number and JSON boolean, not strings:
    //the python end does int()/bool() on them but the documented shape is
    //the typed one.
    EXPECT_TRUE(j.at("node_id").is_number_integer());
    EXPECT_EQ(3, j.at("node_id").get<int>());
    EXPECT_TRUE(j.at("rain").is_boolean());
    EXPECT_TRUE(j.at("rain").get<bool>());
    EXPECT_EQ(5u, j.size());
}

TEST(Klf200WireRegister, RainFalseIsSentNotOmitted)
{
    const Json j = Json::parse(Klf200Wire::buildRegisterMessage(Host("h"), Password("p"), NodeId(1), false));
    ASSERT_TRUE(j.contains("rain"));
    EXPECT_FALSE(j.at("rain").get<bool>());
}

TEST(Klf200WireRegister, TheWireIsPureAsciiEvenWhenTheParamsAreNot)
{
    //A password with an accent (io.xml is UTF-8): the byte string must not
    //carry the raw UTF-8, only its \u escape (ensure_ascii).
    const std::string wire = Klf200Wire::buildRegisterMessage(Host("h"), Password("caf\xc3\xa9"), NodeId(1), false);
    for (unsigned char c: wire)
        EXPECT_LT(c, 0x80u) << "non-ASCII byte on the wire";
    EXPECT_NE(std::string::npos, wire.find("caf\\u00e9"));
}

TEST(Klf200WireRegister, InvalidUtf8InAParamDoesNotAbortTheEmission)
{
    //error_handler_t::replace: a lone continuation byte becomes U+FFFD
    //instead of a thrown type_error.316 in a callback with no try/catch.
    std::string wire;
    EXPECT_NO_THROW(wire = Klf200Wire::buildRegisterMessage(Host("h\x80"), Password("p"), NodeId(1), false));
    EXPECT_NE(std::string::npos, wire.find("\\ufffd"));
}

TEST(Klf200WireSetPosition, CarriesActionHostNodeAndClampedPosition)
{
    Json j = Json::parse(Klf200Wire::buildSetPositionMessage(Host("h"), NodeId(7), 42));
    EXPECT_EQ("set_position", j.at("action").get<std::string>());
    EXPECT_EQ("h", j.at("host").get<std::string>());
    EXPECT_EQ(7, j.at("node_id").get<int>());
    EXPECT_EQ(42, j.at("position").get<int>());
    EXPECT_EQ(4u, j.size());

    //Out of range on either side is clamped here, before the sidecar sees it
    j = Json::parse(Klf200Wire::buildSetPositionMessage(Host("h"), NodeId(7), -5));
    EXPECT_EQ(0, j.at("position").get<int>());
    j = Json::parse(Klf200Wire::buildSetPositionMessage(Host("h"), NodeId(7), 250));
    EXPECT_EQ(100, j.at("position").get<int>());
}

TEST(Klf200WireStop, CarriesActionHostAndNode)
{
    const Json j = Json::parse(Klf200Wire::buildStopMessage(Host("h"), NodeId(9)));
    EXPECT_EQ("stop", j.at("action").get<std::string>());
    EXPECT_EQ("h", j.at("host").get<std::string>());
    EXPECT_EQ(9, j.at("node_id").get<int>());
    EXPECT_EQ(3u, j.size());
}

//----------------------------------------------------------------------------
// Error vocabulary
//----------------------------------------------------------------------------

TEST(Klf200WireErrors, TheFiveCodesOfThePythonEndAreKnown)
{
    //Mirror of the ERR_* constants of ExternProcKlf200_main.py
    EXPECT_TRUE(Klf200Wire::isKnownErrorCode("gateway_connect_failed"));
    EXPECT_TRUE(Klf200Wire::isKnownErrorCode("gateway_not_connected"));
    EXPECT_TRUE(Klf200Wire::isKnownErrorCode("node_not_found"));
    EXPECT_TRUE(Klf200Wire::isKnownErrorCode("node_unsupported"));
    EXPECT_TRUE(Klf200Wire::isKnownErrorCode("command_failed"));
}

TEST(Klf200WireErrors, AnythingElseIsNotACode)
{
    EXPECT_FALSE(Klf200Wire::isKnownErrorCode(""));
    EXPECT_FALSE(Klf200Wire::isKnownErrorCode("Traceback (most recent call last)"));
    EXPECT_FALSE(Klf200Wire::isKnownErrorCode("Gateway_Connect_Failed"));
}

//----------------------------------------------------------------------------
// Reception
//----------------------------------------------------------------------------

TEST(Klf200WireDecode, FlattensEveryTypeToAString)
{
    Params p;
    ASSERT_TRUE(Klf200Wire::decodeMessage(
        R"({"event":"node","host":"h","node_id":3,"position":42,"moving":"up",)"
        R"("node_type":"Window","name":"Bureau","rain_sensor":true,"extra":null,"obj":{"a":1}})",
        p));

    EXPECT_EQ("node", p["event"]);
    EXPECT_EQ("h", p["host"]);
    EXPECT_EQ("true", p["rain_sensor"]);
    EXPECT_EQ("Window", p["node_type"]);
    //null and object: key present, value empty (jansson_decode_object contract)
    EXPECT_TRUE(p.Exists("extra"));
    EXPECT_EQ("", p["extra"]);
    EXPECT_TRUE(p.Exists("obj"));
    EXPECT_EQ("", p["obj"]);
    //numbers cross as Utils::to_string(double): read back through the readers
    EXPECT_EQ(3, Klf200Wire::nodeIdOf(p));
    EXPECT_EQ(42, Klf200Wire::positionOf(p));
}

TEST(Klf200WireDecode, RefusesMalformedAndScalarPayloads)
{
    Params p;
    EXPECT_FALSE(Klf200Wire::decodeMessage("{not json", p));
    EXPECT_FALSE(Klf200Wire::decodeMessage("42", p));
    EXPECT_FALSE(Klf200Wire::decodeMessage("\"str\"", p));
    //A top-level array is accepted and yields nothing
    EXPECT_TRUE(Klf200Wire::decodeMessage("[1,2]", p));
    EXPECT_FALSE(p.Exists("0"));
}

TEST(Klf200WireReaders, NodeIdAnswersMinusOneWhenAbsentOrGarbled)
{
    Params p;
    EXPECT_EQ(-1, Klf200Wire::nodeIdOf(p));
    p.Add("node_id", "abc");
    EXPECT_EQ(-1, Klf200Wire::nodeIdOf(p));
    p.Add("node_id", "-4");
    EXPECT_EQ(-1, Klf200Wire::nodeIdOf(p));
    p.Add("node_id", "12");
    EXPECT_EQ(12, Klf200Wire::nodeIdOf(p));
    //The double spelling Utils::to_string may produce
    p.Add("node_id", "12.000000");
    EXPECT_EQ(12, Klf200Wire::nodeIdOf(p));
}

TEST(Klf200WireReaders, NodeIdRefusesFractionsAndValuesOutsideOneByte)
{
    Params p;
    //A fraction is not truncated onto a neighbouring node
    p.Add("node_id", "3.7");
    EXPECT_EQ(-1, Klf200Wire::nodeIdOf(p));
    //Out of range is refused before the cast to int, which would be UB
    p.Add("node_id", "1e20");
    EXPECT_EQ(-1, Klf200Wire::nodeIdOf(p));
    p.Add("node_id", "256");
    EXPECT_EQ(-1, Klf200Wire::nodeIdOf(p));
    p.Add("node_id", "nan");
    EXPECT_EQ(-1, Klf200Wire::nodeIdOf(p));
    //Both ends of the one-byte range are fine
    p.Add("node_id", "0");
    EXPECT_EQ(0, Klf200Wire::nodeIdOf(p));
    p.Add("node_id", "255");
    EXPECT_EQ(255, Klf200Wire::nodeIdOf(p));
}

TEST(Klf200WireReaders, StyleIsWindowOnlyForAWindowNode)
{
    Params p;
    EXPECT_EQ("shutter", Klf200Wire::styleOf(p));
    p.Add("node_type", "Window");
    EXPECT_EQ("window", Klf200Wire::styleOf(p));
    p.Add("node_type", "RollerShutter");
    EXPECT_EQ("shutter", Klf200Wire::styleOf(p));
}

TEST(Klf200WireReaders, PositionAnswersMinusOneOutsideZeroToHundred)
{
    Params p;
    EXPECT_EQ(-1, Klf200Wire::positionOf(p));
    p.Add("position", "-1"); //the sidecar's "unknown" spelling
    EXPECT_EQ(-1, Klf200Wire::positionOf(p));
    p.Add("position", "101");
    EXPECT_EQ(-1, Klf200Wire::positionOf(p));
    p.Add("position", "0");
    EXPECT_EQ(0, Klf200Wire::positionOf(p));
    p.Add("position", "100");
    EXPECT_EQ(100, Klf200Wire::positionOf(p));
    p.Add("position", "57.000000");
    EXPECT_EQ(57, Klf200Wire::positionOf(p));
}
