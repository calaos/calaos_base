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
//T2.11: transport hardening regressions.
//
//1. WebSocketFrame: a frame whose header is already invalid (oversized or
//   fragmented control frame, RSV bits, reserved opcode) is refused at the
//   header, before any payload byte is consumed or buffered. Before T2.11 the
//   parser kept reading the extended length and the whole payload of a frame
//   it had already condemned.
//
//2. TransportLimits::parseLimit: a config override that is empty, garbage or
//   out of range falls back to the built-in default, so a broken
//   local_config.xml can never turn a hardening limit off.
//
//3. TransportLimits::effectiveClientIp: the per-client connection cap
//   identifies a client by the LAST comma entry of the LAST X-Forwarded-For
//   header line (the trusted haproxy hop, same rule as the MCP sidecar
//   throttle of T1.8), never by a client supplied entry, and falls back to
//   the TCP peer without the header.
//
//Only header-inline helpers of HttpClient.h and libcalaos_common code are
//exercised: nothing of the calaos_server binary is linked.
#include "WebSocketFrame.h"
#include "HttpClient.h"
#include <gtest/gtest.h>
#include <string>

//--- 1. Invalid frame headers abort at the header ---------------------------

TEST(WebSocketFrameHardening, ControlFrameWith16bitLengthCodeAbortsAtHeader)
{
    //Ping (0x89) with length code 126: a control frame may not exceed 125
    //bytes, so an extended length is invalid by construction. The parser
    //must finish (and condemn) the frame on those 2 bytes alone instead of
    //waiting for the extended length and payload.
    std::string data("\x89\x7E", 2);
    WebSocketFrame frame;

    ASSERT_TRUE(frame.processFrameData(data));
    EXPECT_TRUE(frame.hasError());
    EXPECT_FALSE(frame.isValid());
    EXPECT_EQ(WebSocketFrame::CloseCodeProtocolError, frame.getCloseCode());
    EXPECT_TRUE(data.empty()); //header consumed, nothing more wanted
}

TEST(WebSocketFrameHardening, ControlFrameWith64bitLengthCodeAbortsAtHeader)
{
    //Same with length code 127 (8 bytes of extended length would follow)
    std::string data("\x89\x7F", 2);
    WebSocketFrame frame;

    ASSERT_TRUE(frame.processFrameData(data));
    EXPECT_TRUE(frame.hasError());
    EXPECT_FALSE(frame.isValid());
    EXPECT_EQ(WebSocketFrame::CloseCodeProtocolError, frame.getCloseCode());
}

TEST(WebSocketFrameHardening, OversizedControlFramePayloadIsNotBuffered)
{
    //A condemned ping followed by 4 bytes that would have been its extended
    //length + payload start: the parser must not consume them as payload.
    std::string data("\x89\x7E\x01\x00XY", 6);
    WebSocketFrame frame;

    ASSERT_TRUE(frame.processFrameData(data));
    EXPECT_TRUE(frame.hasError());
    EXPECT_EQ(std::string("\x01\x00XY", 4), data); //left untouched for the caller
    EXPECT_TRUE(frame.getPayload().empty());
}

TEST(WebSocketFrameHardening, FragmentedControlFrameAbortsAtHeader)
{
    //Ping without FIN (0x09): control frames cannot be fragmented
    std::string data("\x09\x01", 2);
    WebSocketFrame frame;

    ASSERT_TRUE(frame.processFrameData(data));
    EXPECT_TRUE(frame.hasError());
    EXPECT_FALSE(frame.isValid());
    EXPECT_EQ(WebSocketFrame::CloseCodeProtocolError, frame.getCloseCode());
}

TEST(WebSocketFrameHardening, RsvBitsAbortAtHeader)
{
    //Text frame with RSV1 set (0xC1), payload length 5: refused on the
    //header, the 5 payload bytes are never waited for
    std::string data("\xC1\x05", 2);
    WebSocketFrame frame;

    ASSERT_TRUE(frame.processFrameData(data));
    EXPECT_TRUE(frame.hasError());
    EXPECT_FALSE(frame.isValid());
    EXPECT_EQ(WebSocketFrame::CloseCodeProtocolError, frame.getCloseCode());
}

TEST(WebSocketFrameHardening, ValidUnmaskedPingStillParses)
{
    //Regression guard: a well-formed ping still goes through untouched
    std::string data("\x89\x04ping", 6);
    WebSocketFrame frame;

    ASSERT_TRUE(frame.processFrameData(data));
    EXPECT_FALSE(frame.hasError());
    EXPECT_TRUE(frame.isValid());
    EXPECT_TRUE(frame.isControlFrame());
    EXPECT_EQ("ping", frame.getPayload());
}

TEST(WebSocketFrameHardening, DataFrameWithExtendedLengthStillWaitsForIt)
{
    //Regression guard: the early abort only fires on errors. A DATA frame
    //announcing a 16 bit extended length is valid and must keep waiting.
    std::string data("\x81\x7E", 2);
    WebSocketFrame frame;

    EXPECT_FALSE(frame.processFrameData(data));
    EXPECT_FALSE(frame.hasError());
}

//--- 2. Config overrides of the transport limits ----------------------------

TEST(TransportLimitsParse, EmptyValueFallsBackToDefault)
{
    EXPECT_EQ(100u, TransportLimits::parseLimit("", 100, 1, 10000));
}

TEST(TransportLimitsParse, GarbageFallsBackToDefault)
{
    EXPECT_EQ(100u, TransportLimits::parseLimit("abc", 100, 1, 10000));
    EXPECT_EQ(100u, TransportLimits::parseLimit("12abc", 100, 1, 10000));
    EXPECT_EQ(100u, TransportLimits::parseLimit("-5", 100, 1, 10000));
    EXPECT_EQ(100u, TransportLimits::parseLimit("+5", 100, 1, 10000));
    EXPECT_EQ(100u, TransportLimits::parseLimit("1e3", 100, 1, 10000));
    EXPECT_EQ(100u, TransportLimits::parseLimit(" 42", 100, 1, 10000));
}

TEST(TransportLimitsParse, OutOfRangeFallsBackToDefault)
{
    EXPECT_EQ(100u, TransportLimits::parseLimit("0", 100, 1, 10000));
    EXPECT_EQ(100u, TransportLimits::parseLimit("10001", 100, 1, 10000));
    //bigger than uint64_t: stoull overflow must not escape
    EXPECT_EQ(100u, TransportLimits::parseLimit("99999999999999999999999999",
                                                100, 1, 10000));
}

TEST(TransportLimitsParse, ValidValueIsUsed)
{
    EXPECT_EQ(1u, TransportLimits::parseLimit("1", 100, 1, 10000));
    EXPECT_EQ(42u, TransportLimits::parseLimit("42", 100, 1, 10000));
    EXPECT_EQ(10000u, TransportLimits::parseLimit("10000", 100, 1, 10000));
    EXPECT_EQ(4194304u, TransportLimits::parseLimit("4194304", 0, 4096,
                                                    1073741824));
}

TEST(TransportLimitsParse, DefaultsAreUnchangedFromT15)
{
    //The configurable limits keep the exact defaults T1.5 hard wired
    EXPECT_EQ(4u * 1024 * 1024, TransportLimits::DefaultMaxHttpBodySize);
    EXPECT_EQ(4u * 1024 * 1024, TransportLimits::DefaultMaxWebsocketMessageSize);
    EXPECT_EQ(100u, TransportLimits::DefaultMaxConnections);
    EXPECT_EQ(30.0, TransportLimits::DefaultRequestReadTimeout);
    //New limit of T2.11; default raised from 20 to 50 by user decision
    EXPECT_EQ(50u, TransportLimits::DefaultMaxConnectionsPerIp);
}

//--- 3. Client identity behind haproxy --------------------------------------

TEST(EffectiveClientIp, NoHeaderFallsBackToPeer)
{
    EXPECT_EQ("10.0.0.1", TransportLimits::effectiveClientIp("", "10.0.0.1"));
    EXPECT_EQ("10.0.0.1", TransportLimits::effectiveClientIp("   ", "10.0.0.1"));
}

TEST(EffectiveClientIp, SingleEntryIsTaken)
{
    EXPECT_EQ("1.2.3.4",
              TransportLimits::effectiveClientIp("1.2.3.4", "127.0.0.1"));
    EXPECT_EQ("1.2.3.4",
              TransportLimits::effectiveClientIp("  1.2.3.4  ", "127.0.0.1"));
}

TEST(EffectiveClientIp, LastCommaEntryWins)
{
    //Everything before the last entry is client supplied and spoofable:
    //haproxy only appends, so the last entry is the address it saw
    EXPECT_EQ("5.6.7.8",
              TransportLimits::effectiveClientIp("spoofed, 5.6.7.8", "127.0.0.1"));
    EXPECT_EQ("9.9.9.9",
              TransportLimits::effectiveClientIp("a, b ,  9.9.9.9 ", "127.0.0.1"));
}

TEST(EffectiveClientIp, EmptyLastEntryFallsBackToPeer)
{
    //"fake," has an empty last entry: never trust it, use the peer
    EXPECT_EQ("127.0.0.1",
              TransportLimits::effectiveClientIp("fake,", "127.0.0.1"));
    EXPECT_EQ("127.0.0.1",
              TransportLimits::effectiveClientIp("fake, ", "127.0.0.1"));
}

TEST(EffectiveClientIp, Ipv6EntrySurvives)
{
    EXPECT_EQ("2001:db8::1",
              TransportLimits::effectiveClientIp("2001:db8::1", "127.0.0.1"));
    EXPECT_EQ("2001:db8::2",
              TransportLimits::effectiveClientIp("1.1.1.1, 2001:db8::2",
                                                 "127.0.0.1"));
}
