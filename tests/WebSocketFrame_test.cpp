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
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
#include "WebSocketFrame.h"
#include <gtest/gtest.h>

namespace
{

//Header of a frame: FIN + opcode, then mask bit + the 7 bits length code.
//lengthCode is 126 or 127 when an extended length field follows.
std::string frameHeader(int opcode, bool fin, bool masked, uint8_t lengthCode)
{
    std::string h;
    h.push_back(static_cast<char>((opcode & 0x0F) | (fin? 0x80: 0x00)));
    h.push_back(static_cast<char>((lengthCode & 0x7F) | (masked? 0x80: 0x00)));
    return h;
}

//The 8 bytes of the 127 extended length field, network order
void appendBigLength(std::string &frame, uint64_t len)
{
    for (int i = 7;i >= 0;i--)
        frame.push_back(static_cast<char>((len >> (i * 8)) & 0xFF));
}

//A frame announcing len bytes of payload through the 64 bits length field,
//with no payload byte at all: everything tested here must be decided on the
//header alone, before a single payload byte is buffered.
std::string bigLengthFrame(uint64_t len, bool masked = false)
{
    std::string frame = frameHeader(WebSocketFrame::OpCodeBinary, true, masked, 127);
    appendBigLength(frame, len);
    return frame;
}

//processFrameData() consumes one state of the frame per call and returns
//false as long as the frame is not complete. Drive it until it is done or
//until it stops consuming data (needs more bytes from the network).
bool feed(WebSocketFrame &frame, std::string &data)
{
    for (;;)
    {
        std::string::size_type before = data.size();
        if (frame.processFrameData(data))
            return true;
        if (data.empty() || data.size() == before)
            return false;
    }
}

}

//The cap has to stay a realistic value: the largest legitimate payload sent
//by a Calaos client is ~215 KiB, and a 2 GiB allocation on an appliance is a
//denial of service, not a feature.
TEST(WebSocketFrameTest, FrameSizeCapIsRealistic)
{
    EXPECT_EQ(4u * 1024u * 1024u, WebSocketFrame::MAX_FRAME_SIZE_IN_BYTES);
}

//Bytes 4 to 7 of the 64 bits length must be widened before being shifted.
//As int, 0x80 << 24 sets the sign bit and the length becomes
//0xFFFFFFFF80000000 once converted, which is reported as "highest bit set"
//instead of the real length.
TEST(WebSocketFrameTest, BigLengthDoesNotSignExtend)
{
    std::string data = bigLengthFrame(0x80000000ULL);

    WebSocketFrame frame;
    EXPECT_TRUE(feed(frame, data));
    EXPECT_FALSE(frame.isValid());
    EXPECT_EQ(WebSocketFrame::CloseCodeTooMuchData, frame.getCloseCode());
}

//Same for a length that only lights up bits 32 to 62: it must be read as it
//is and refused for its size, not for a bit that is not set.
TEST(WebSocketFrameTest, BigLengthKeepsAllBytes)
{
    std::string data = bigLengthFrame(0x00000000FF00FF00ULL);

    WebSocketFrame frame;
    EXPECT_TRUE(feed(frame, data));
    EXPECT_FALSE(frame.isValid());
    EXPECT_EQ(WebSocketFrame::CloseCodeTooMuchData, frame.getCloseCode());
}

//RFC 6455: the most significant bit of the 64 bits length must be 0
TEST(WebSocketFrameTest, BigLengthWithHighestBitSetIsAProtocolError)
{
    std::string data = bigLengthFrame(0x8000000000000000ULL);

    WebSocketFrame frame;
    EXPECT_TRUE(feed(frame, data));
    EXPECT_FALSE(frame.isValid());
    EXPECT_EQ(WebSocketFrame::CloseCodeProtocolError, frame.getCloseCode());
}

//A frame bigger than the cap is refused on its announced length, without
//waiting for (and buffering) any payload byte
TEST(WebSocketFrameTest, FrameOverCapIsRefusedOnItsHeader)
{
    std::string data = bigLengthFrame(WebSocketFrame::MAX_FRAME_SIZE_IN_BYTES + 1);

    WebSocketFrame frame;
    EXPECT_TRUE(feed(frame, data));
    EXPECT_FALSE(frame.isValid());
    EXPECT_EQ(WebSocketFrame::CloseCodeTooMuchData, frame.getCloseCode());
    EXPECT_TRUE(frame.getPayload().empty());
}

//Real clients always mask: the refusal must happen before the masking key,
//not after it
TEST(WebSocketFrameTest, MaskedFrameOverCapIsRefusedOnItsHeader)
{
    std::string data = bigLengthFrame(WebSocketFrame::MAX_FRAME_SIZE_IN_BYTES + 1, true);

    WebSocketFrame frame;
    EXPECT_TRUE(feed(frame, data));
    EXPECT_FALSE(frame.isValid());
    EXPECT_EQ(WebSocketFrame::CloseCodeTooMuchData, frame.getCloseCode());
}

//A frame exactly at the cap is still accepted, the parser just waits for the
//rest of the payload
TEST(WebSocketFrameTest, FrameAtCapIsAccepted)
{
    std::string data = bigLengthFrame(WebSocketFrame::MAX_FRAME_SIZE_IN_BYTES);
    data.append(16, 'x');

    WebSocketFrame frame;
    EXPECT_FALSE(feed(frame, data));
    EXPECT_EQ(WebSocketFrame::CloseCodeNormal, frame.getCloseCode());
}

//Non minimal encodings stay refused (Autobahn 1.1/1.2)
TEST(WebSocketFrameTest, BigLengthUnderTwoBytesIsAProtocolError)
{
    std::string data = bigLengthFrame(0xFFFFULL);

    WebSocketFrame frame;
    EXPECT_TRUE(feed(frame, data));
    EXPECT_FALSE(frame.isValid());
    EXPECT_EQ(WebSocketFrame::CloseCodeProtocolError, frame.getCloseCode());
}

//Nothing of the above changes how a normal frame is decoded
TEST(WebSocketFrameTest, SmallTextFrameIsStillParsed)
{
    std::string data = WebSocketFrame::makeFrame(WebSocketFrame::OpCodeText, "hello", true);

    WebSocketFrame frame;
    EXPECT_TRUE(feed(frame, data));
    EXPECT_TRUE(frame.isValid());
    EXPECT_TRUE(frame.isTextFrame());
    EXPECT_TRUE(frame.isFinalFrame());
    EXPECT_EQ("hello", frame.getPayload());
}

TEST(WebSocketFrameTest, MaskedTextFrameIsStillParsed)
{
    std::string data = WebSocketFrame::makeFrame(WebSocketFrame::OpCodeText, "hello world", true, 0x1234ABCD);

    WebSocketFrame frame;
    EXPECT_TRUE(feed(frame, data));
    EXPECT_TRUE(frame.isValid());
    EXPECT_EQ("hello world", frame.getPayload());
}

//A frame announced with the 2 bytes length field is unaffected by the cap
TEST(WebSocketFrameTest, TwoBytesLengthFrameIsStillParsed)
{
    std::string payload(1000, 'a');
    std::string data = WebSocketFrame::makeFrame(WebSocketFrame::OpCodeBinary, payload, true);

    WebSocketFrame frame;
    EXPECT_TRUE(feed(frame, data));
    EXPECT_TRUE(frame.isValid());
    EXPECT_EQ(payload, frame.getPayload());
}

TEST(WebSocketFrameTest, ControlFrameBiggerThan125IsAProtocolError)
{
    std::string data = frameHeader(WebSocketFrame::OpCodePing, true, false, 126);
    data.push_back(static_cast<char>(0x01));
    data.push_back(static_cast<char>(0x00));

    WebSocketFrame frame;
    feed(frame, data);
    EXPECT_FALSE(frame.isValid());
    EXPECT_EQ(WebSocketFrame::CloseCodeProtocolError, frame.getCloseCode());
}
