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

//T1.9: ExternProcMessage framing parser tests.
//Wire format: 1 byte opcode + 4 bytes big endian length + payload.

#include <gtest/gtest.h>
#include "ExternProc.h"

namespace
{

//Build a frame by hand, byte per byte, so the test does not depend on
//getRawData() being correct.
std::string makeFrame(uint8_t opcode, uint32_t len, const std::string &payload)
{
    std::string f;
    f.push_back(static_cast<char>(opcode));
    f.push_back(static_cast<char>((len >> 24) & 0xFF));
    f.push_back(static_cast<char>((len >> 16) & 0xFF));
    f.push_back(static_cast<char>((len >> 8) & 0xFF));
    f.push_back(static_cast<char>(len & 0xFF));
    f += payload;
    return f;
}

//A buffer shorter than the 5 byte header must not be parsed nor consumed:
//the parser has to wait for more data. The old code tested size() >= 3 but
//read data[0]..data[4] and erased 5 bytes -> OOB read on a 3-4 byte buffer.
TEST(ExternProcFraming, ShortBufferIsNotParsedNorConsumed)
{
    for (size_t n = 1; n <= 4; n++)
    {
        ExternProcMessage msg;
        std::string data = makeFrame(ExternProcMessage::TypeMessage, 5, "hello").substr(0, n);
        ASSERT_EQ(n, data.size());

        EXPECT_FALSE(msg.processFrameData(data)) << "with a " << n << " byte buffer";
        EXPECT_FALSE(msg.isValid()) << "with a " << n << " byte buffer";
        //nothing may be consumed until a full 5 byte header is available
        EXPECT_EQ(n, data.size()) << "with a " << n << " byte buffer";
    }
}

//A complete well formed frame parses into a valid message
TEST(ExternProcFraming, WellFormedFrameParses)
{
    ExternProcMessage msg;
    std::string data = makeFrame(ExternProcMessage::TypeMessage, 5, "hello");

    EXPECT_TRUE(msg.processFrameData(data));
    EXPECT_TRUE(msg.isValid());
    EXPECT_EQ("hello", msg.getPayload());
    EXPECT_TRUE(data.empty());
}

//The 4 length bytes are big endian: 260 == 0x00000104
TEST(ExternProcFraming, LengthIsBigEndian)
{
    std::string payload(260, 'a');
    std::string data;
    data.push_back(static_cast<char>(0x21)); //TypeMessage
    data.push_back(static_cast<char>(0x00));
    data.push_back(static_cast<char>(0x00));
    data.push_back(static_cast<char>(0x01));
    data.push_back(static_cast<char>(0x04));
    data += payload;

    ExternProcMessage msg;
    EXPECT_TRUE(msg.processFrameData(data));
    EXPECT_TRUE(msg.isValid());
    EXPECT_EQ(payload, msg.getPayload());
    EXPECT_TRUE(data.empty());

    //and getRawData() writes the same big endian encoding
    ExternProcMessage out(payload);
    std::string raw = out.getRawData();
    ASSERT_GE(raw.size(), size_t(5));
    EXPECT_EQ(0x21, uint8_t(raw[0]));
    EXPECT_EQ(0x00, uint8_t(raw[1]));
    EXPECT_EQ(0x00, uint8_t(raw[2]));
    EXPECT_EQ(0x01, uint8_t(raw[3]));
    EXPECT_EQ(0x04, uint8_t(raw[4]));
    EXPECT_EQ(payload, raw.substr(5));
}

//Data arriving in arbitrary small chunks must reassemble into the same frame
TEST(ExternProcFraming, FragmentedDeliveryParses)
{
    ExternProcMessage msg;
    std::string frame = makeFrame(ExternProcMessage::TypeMessage, 11, "hello world");
    std::string buffer;

    bool got = false;
    for (size_t i = 0; i < frame.size(); i++)
    {
        buffer.push_back(frame[i]);
        got = msg.processFrameData(buffer);
        if (i < frame.size() - 1)
            EXPECT_FALSE(got) << "at byte " << i;
    }

    EXPECT_TRUE(got);
    EXPECT_TRUE(msg.isValid());
    EXPECT_EQ("hello world", msg.getPayload());
}

//An announced payload length above the cap must be rejected: the parser must
//not sit in StateReadPayload buffering towards a huge length. After the
//rejection it must be able to parse a subsequent well formed frame.
TEST(ExternProcFraming, OversizedLengthIsRejected)
{
    ExternProcMessage msg;
    //5 MiB announced: above the 4 MiB cap
    std::string data = makeFrame(ExternProcMessage::TypeMessage, 5 * 1024 * 1024, "");

    EXPECT_FALSE(msg.processFrameData(data));
    EXPECT_FALSE(msg.isValid());
    //the error is flagged so callers can drop the connection, and the
    //buffered data is discarded instead of accumulating unbounded
    EXPECT_TRUE(msg.hasError());
    EXPECT_TRUE(data.empty());

    //a valid frame arriving next must parse: the parser must not be stuck
    //waiting for 5 MiB of payload
    std::string next = makeFrame(ExternProcMessage::TypeMessage, 5, "hello");
    EXPECT_TRUE(msg.processFrameData(next));
    EXPECT_TRUE(msg.isValid());
    EXPECT_EQ("hello", msg.getPayload());

    //clear() resets the error flag
    msg.clear();
    EXPECT_FALSE(msg.hasError());
}

//Exact boundary: a length of MaxPayloadLength is accepted (the parser then
//waits for the payload), MaxPayloadLength + 1 is rejected. Only headers are
//fed here, no huge allocation happens.
TEST(ExternProcFraming, PayloadCapBoundary)
{
    {
        ExternProcMessage msg;
        std::string data = makeFrame(ExternProcMessage::TypeMessage,
                                     ExternProcMessage::MaxPayloadLength, "");
        EXPECT_FALSE(msg.processFrameData(data)); //waiting for payload
        EXPECT_FALSE(msg.hasError());
        EXPECT_TRUE(msg.isValid()); //header was accepted
    }
    {
        ExternProcMessage msg;
        std::string data = makeFrame(ExternProcMessage::TypeMessage,
                                     ExternProcMessage::MaxPayloadLength + 1, "");
        EXPECT_FALSE(msg.processFrameData(data));
        EXPECT_TRUE(msg.hasError());
        EXPECT_FALSE(msg.isValid());
    }
}

//An empty payload is a legal frame
TEST(ExternProcFraming, ZeroLengthPayloadParses)
{
    ExternProcMessage msg;
    std::string data = makeFrame(ExternProcMessage::TypeMessage, 0, "")
                     + makeFrame(ExternProcMessage::TypeMessage, 5, "hello");

    EXPECT_TRUE(msg.processFrameData(data));
    EXPECT_TRUE(msg.isValid());
    EXPECT_EQ("", msg.getPayload());

    msg.clear();
    EXPECT_TRUE(msg.processFrameData(data));
    EXPECT_TRUE(msg.isValid());
    EXPECT_EQ("hello", msg.getPayload());
    EXPECT_TRUE(data.empty());
}

} //namespace
