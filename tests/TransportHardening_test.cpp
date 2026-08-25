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
//4. T2.16 — HttpParsing::RequestState: the llhttp callbacks (production
//   wiring, header-inline in HttpClient.h) wipe every bit of per-request
//   state on message begin, so the headers of request N never leak into
//   request N+1 of the same keep-alive connection (a stale Origin used to
//   keep triggering CORS for the whole connection).
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

//--- 4. T2.16: no header leak between requests of a keep-alive connection ---

//Drives llhttp with the exact production callbacks
//(HttpParsing::initParserSettings) against a bare RequestState, the way
//HttpClient wires them, minus the socket.
namespace
{
struct KeepAliveParser
{
    HttpParsing::RequestState state;
    llhttp_settings_t settings;
    llhttp_t parser;

    KeepAliveParser()
    {
        HttpParsing::initParserSettings(settings);
        HttpParsing::bindParser(&parser, settings, state);
    }

    llhttp_errno feed(const std::string &data)
    {
        return llhttp_execute(&parser, data.c_str(), data.size());
    }
};
}

TEST(KeepAliveRequestReset, HeadersDoNotLeakIntoNextRequest)
{
    KeepAliveParser p;

    //Request N carries per-request headers with security meaning
    ASSERT_EQ(HPE_OK, p.feed("GET /api HTTP/1.1\r\n"
                             "Host: calaos\r\n"
                             "Origin: http://attacker.example\r\n"
                             "X-Forwarded-For: 1.2.3.4\r\n"
                             "\r\n"));
    ASSERT_TRUE(p.state.parse_done);
    ASSERT_EQ(1u, p.state.request_headers.count("origin"));
    ASSERT_EQ(1u, p.state.request_headers.count("x-forwarded-for"));

    //Request N+1 on the same connection sends none of them: it must not see
    //them either. Before T2.16 request_headers was never reset, so the stale
    //Origin kept triggering CORS and the stale X-Forwarded-For polluted the
    //per-IP client identity for the rest of the connection.
    ASSERT_EQ(HPE_OK, p.feed("GET /api HTTP/1.1\r\n"
                             "Host: calaos\r\n"
                             "\r\n"));
    ASSERT_TRUE(p.state.parse_done);
    EXPECT_EQ(0u, p.state.request_headers.count("origin"));
    EXPECT_EQ(0u, p.state.request_headers.count("x-forwarded-for"));
    EXPECT_EQ(1u, p.state.request_headers.count("host"));
    EXPECT_EQ("calaos", p.state.request_headers["host"]);
}

TEST(KeepAliveRequestReset, PipelinedRequestsInOneBufferAreResetToo)
{
    //Both requests arrive in a single TCP read: on_message_begin of the
    //second one must wipe the first one's headers all the same
    KeepAliveParser p;

    ASSERT_EQ(HPE_OK, p.feed("GET /a HTTP/1.1\r\n"
                             "Host: calaos\r\n"
                             "Origin: http://attacker.example\r\n"
                             "\r\n"
                             "GET /b HTTP/1.1\r\n"
                             "Host: calaos\r\n"
                             "\r\n"));
    ASSERT_TRUE(p.state.parse_done);
    EXPECT_EQ("/b", p.state.parse_url);
    EXPECT_EQ(0u, p.state.request_headers.count("origin"));
    EXPECT_EQ(1u, p.state.request_headers.count("host"));
}

TEST(KeepAliveRequestReset, StaleResponseCorsHeadersAreDropped)
{
    //The CORS echo of request N's Origin lands in resHeaders; request N+1
    //must start from an empty response header set or the stale
    //Access-Control-Allow-Origin would be sent again
    KeepAliveParser p;

    ASSERT_EQ(HPE_OK, p.feed("GET /api HTTP/1.1\r\n"
                             "Host: calaos\r\n"
                             "Origin: http://attacker.example\r\n"
                             "\r\n"));
    ASSERT_TRUE(p.state.parse_done);
    //what HttpClient::processHeaders does on seeing Origin
    p.state.resHeaders.Add("Access-Control-Allow-Origin",
                           p.state.request_headers["origin"]);

    ASSERT_EQ(HPE_OK, p.feed("GET /api HTTP/1.1\r\n"
                             "Host: calaos\r\n"
                             "\r\n"));
    ASSERT_TRUE(p.state.parse_done);
    EXPECT_FALSE(p.state.resHeaders.Exists("Access-Control-Allow-Origin"));
}

TEST(KeepAliveRequestReset, BodyAndUrlAreResetBetweenRequests)
{
    //The rest of the per-request state must not survive either
    KeepAliveParser p;

    ASSERT_EQ(HPE_OK, p.feed("POST /api HTTP/1.1\r\n"
                             "Host: calaos\r\n"
                             "Content-Length: 5\r\n"
                             "\r\n"
                             "hello"));
    ASSERT_TRUE(p.state.parse_done);
    ASSERT_EQ("hello", p.state.bodymessage);
    ASSERT_EQ("/api", p.state.parse_url);
    ASSERT_EQ(HTTP_POST, p.state.request_method);

    ASSERT_EQ(HPE_OK, p.feed("GET /other HTTP/1.1\r\n"
                             "Host: calaos\r\n"
                             "\r\n"));
    ASSERT_TRUE(p.state.parse_done);
    EXPECT_TRUE(p.state.bodymessage.empty());
    EXPECT_EQ("/other", p.state.parse_url);
    EXPECT_EQ(HTTP_GET, p.state.request_method);
}

TEST(KeepAliveRequestReset, BindParserTargetsTheStateSubobjectNotTheOwner)
{
    //HttpClient does NOT keep its RequestState at offset 0: a vptr and the
    //sigc::trackable base sit in front of it. parser->data must therefore
    //hold the adjusted RequestState subobject pointer — storing the raw
    //owner pointer (the reviewed WebSocket re-init bug: `parser->data =
    //this;`) makes every callback read/write 16 bytes off and clobber the
    //vptr region. This owner reproduces the nonzero offset and runs two
    //pipelined requests through the production binding
    //(HttpParsing::bindParser); the sentinels canary the offset-0 region.
    struct Pad
    {
        void *sentinel1 = nullptr;
        void *sentinel2 = nullptr;
    };
    struct PaddedOwner : Pad, HttpParsing::RequestState {};

    PaddedOwner owner;
    owner.sentinel1 = &owner;
    owner.sentinel2 = &owner;

    //the test only means something if the state really is at a nonzero
    //offset, like in HttpClient
    ASSERT_NE(static_cast<void *>(&owner),
              static_cast<void *>(
                  static_cast<HttpParsing::RequestState *>(&owner)));

    llhttp_settings_t settings;
    HttpParsing::initParserSettings(settings);
    llhttp_t parser;
    //bindParser takes the state by reference: passing the derived owner
    //performs the derived-to-base adjustment at the call site, which is the
    //whole point of the helper
    HttpParsing::bindParser(&parser, settings, owner);

    std::string reqs = "GET /a HTTP/1.1\r\n"
                       "Host: calaos\r\n"
                       "Origin: http://attacker.example\r\n"
                       "\r\n"
                       "GET /b HTTP/1.1\r\n"
                       "Host: calaos\r\n"
                       "\r\n";
    ASSERT_EQ(HPE_OK, llhttp_execute(&parser, reqs.c_str(), reqs.size()));

    EXPECT_TRUE(owner.parse_done);
    EXPECT_EQ("/b", owner.parse_url);
    EXPECT_EQ(0u, owner.request_headers.count("origin"));
    EXPECT_EQ(1u, owner.request_headers.count("host"));

    //the offset-0 region was never touched by the callbacks
    EXPECT_EQ(static_cast<void *>(&owner), owner.sentinel1);
    EXPECT_EQ(static_cast<void *>(&owner), owner.sentinel2);
}

//--- 5. T3.24: WHICH X-Forwarded-For LINE WINS WHEN THE HEADER REPEATS? ------
//
//Section 3 above tests effectiveClientIp() on a STRING. That is only half the
//argument: the other half is which string the production parser hands it when
//the client sent its own X-Forwarded-For and haproxy appended another.
//
//This is the invariant the whole security claim of T3.24 rests on - "behind
//haproxy the throttle is not bypassable by a forged header" - and nothing
//exercised it end to end: core/JsonApiThrottleIdentity_test seeds
//request_headers by hand, so it would keep passing if the parser started
//keeping the FIRST line. Driven here through the PRODUCTION llhttp callbacks,
//wired exactly the way HttpClient wires them.
//
//Written by the T3.24 review; imported into the ticket's branch on its request.

TEST(ForwardedForLine, LastRepeatedHeaderLineWins)
{
    KeepAliveParser p;

    //What production puts on the wire. calaos-os pins calaos-os-conf, whose
    //conf/haproxy-calaos.cfg carries "option forwardfor" WITHOUT "if-none"
    //(backend calaos-server 127.0.0.1:5454): haproxy ALWAYS appends its own
    //line, at the tail of the header block, whatever the client sent.
    ASSERT_EQ(HPE_OK, p.feed("GET /api HTTP/1.1\r\n"
                             "Host: calaos\r\n"
                             "X-Forwarded-For: 6.6.6.6\r\n"
                             "X-Forwarded-For: 198.51.100.7\r\n"
                             "\r\n"));
    ASSERT_TRUE(p.state.parse_done);

    //LAST line, not the first: request_headers is a map and the callback
    //assigns, so the client supplied line is overwritten (HttpClient.h:265).
    EXPECT_EQ("198.51.100.7", p.state.request_headers["x-forwarded-for"]);
    //T3.39: the peer of this scenario is haproxy, and haproxy reaches
    //calaos_server on 127.0.0.1:5454 (backend calaos-server of
    //conf/haproxy-calaos.cfg). It was seeded "10.0.0.254" before, which no
    //deployment produces: this case is about the PROXY path, so it must be
    //driven with the proxy's real peer address, otherwise the trust guard of
    //T3.39 turns it red and makes it look like the proxy path broke.
    EXPECT_EQ("198.51.100.7",
              TransportLimits::effectiveClientIp(
                  p.state.request_headers["x-forwarded-for"], "127.0.0.1"));
}

TEST(ForwardedForLine, ClientSuppliedListIsDiscardedWholesale)
{
    KeepAliveParser p;

    //Client forges a whole list; haproxy appends its own single entry line.
    //The forged list is not merged, not appended to: it is dropped entirely.
    ASSERT_EQ(HPE_OK, p.feed("GET /api HTTP/1.1\r\n"
                             "Host: calaos\r\n"
                             "X-Forwarded-For: a, b\r\n"
                             "X-Forwarded-For: 198.51.100.7\r\n"
                             "\r\n"));
    ASSERT_TRUE(p.state.parse_done);
    //T3.39: re-seeded on the proxy's real peer address, same reason as above.
    EXPECT_EQ("198.51.100.7",
              TransportLimits::effectiveClientIp(
                  p.state.request_headers["x-forwarded-for"], "127.0.0.1"));
}

TEST(ForwardedForLine, WithoutAProxyTheHeaderIsIgnored)
{
    KeepAliveParser p;

    //T3.39 TURNS THIS CASE. It used to pin F-XFF-1 as a FACT: with no proxy in
    //front nothing overwrites the client's line, so the client named its own
    //throttle bucket and its own connection-cap counter while its real address
    //(192.0.2.55 here) was ignored - which handed a direct LAN client both the
    //exemption from the login backoff and the power to throttle a victim.
    //
    //Same request, INVERTED oracle: the header is only as trustworthy as the
    //hop that wrote it, and on this connection no trusted hop wrote it. The
    //peer is 192.0.2.55, which is not the loopback, so the line is client
    //supplied from end to end and is dropped. F-XFF-1 is CLOSED by this case
    //going green, not by it going red.
    ASSERT_EQ(HPE_OK, p.feed("GET /api HTTP/1.1\r\n"
                             "Host: calaos\r\n"
                             "X-Forwarded-For: 203.0.113.9\r\n"
                             "\r\n"));
    ASSERT_TRUE(p.state.parse_done);
    //The parser still keeps the line - the guard is about TRUST, not parsing.
    EXPECT_EQ("203.0.113.9", p.state.request_headers["x-forwarded-for"]);
    EXPECT_EQ("192.0.2.55",
              TransportLimits::effectiveClientIp(
                  p.state.request_headers["x-forwarded-for"], "192.0.2.55"));
}

//--- 6. T3.39: WHOSE WORD IS THE X-Forwarded-For LINE? -----------------------
//
//Section 5 proves that BEHIND haproxy the header cannot be forged: the proxy
//appends its own line last and the last line wins. That argument has a silent
//premise - that a proxy is in front at all - and nothing checked it. T3.24
//routed LoginThrottle through this helper, so on a server reachable from the
//LAN (HttpServer.cpp:29-31 binds listen_address = "0.0.0.0" by default, and it
//MUST stay reachable: the RemoteUI fleet and the LAN mobile apps speak to
//port 5454 directly) a client wrote its own identity and gained two things:
//it exempted itself from the login backoff by rotating the header, and it
//throttled a victim by wearing her address. That was F-XFF-1.
//
//The rule pinned here: the header is worth exactly as much as the hop that
//wrote it. haproxy reaches calaos_server over the loopback (backend
//calaos-server 127.0.0.1:5454), so a loopback peer IS the proxy and its header
//is read. Any other peer is a client talking to us directly, and its header is
//dropped in favour of the address the kernel reports.
//
//WHY THE ADDRESSES BELOW ARE WHAT THEY ARE. Every case seeds a peer and a
//header that DIFFER, and asserts WHICH OF THE TWO came out - not merely that
//two clients differ, which is true on both sides of the guard as soon as the
//headers differ, and would pin nothing. None of the non-loopback addresses is
//in 127.0.0.0/8, and they are drawn from three different documentation ranges
//so no prefix or truncation slip can make one pass for another.

namespace
{
//A direct client on the LAN, its forged header, and the victim it would like
//to wear. Three distinct documentation ranges (RFC 5737), none of them
//loopback, none a prefix of another.
const char *const kDirectPeer   = "192.0.2.55";
const char *const kForgedIp     = "203.0.113.9";
const char *const kVictimIp     = "198.51.100.7";
}

TEST(TrustedProxyPeer, ADirectClientCannotNameItsOwnBucket)
{
    //PINS THE FIX, capability (a) of F-XFF-1: exempting yourself from the
    //backoff. The peer is a LAN address, so the header is somebody's claim
    //about himself and carries no weight.
    EXPECT_EQ(kDirectPeer,
              TransportLimits::effectiveClientIp(kForgedIp, kDirectPeer));
}

TEST(TrustedProxyPeer, ADirectClientCannotRotateItsBucket)
{
    //PINS THE FIX, the exploit itself rather than one request of it: a client
    //that puts a FRESH address in the header on every attempt must keep
    //landing in the SAME bucket, or the login throttle simply does not exist
    //for him. Two forged identities, one peer, one answer - and the answer is
    //asserted by VALUE, so a guard that returned some third string would not
    //sneak through on "they are equal".
    const std::string first  =
        TransportLimits::effectiveClientIp(kForgedIp, kDirectPeer);
    const std::string second =
        TransportLimits::effectiveClientIp(kVictimIp, kDirectPeer);

    EXPECT_EQ(kDirectPeer, first);
    EXPECT_EQ(kDirectPeer, second);
    EXPECT_EQ(first, second)
            << "rotating X-Forwarded-For bought a direct client a new bucket";
}

TEST(TrustedProxyPeer, ADirectClientCannotThrottleAVictim)
{
    //PINS THE FIX, capability (b): wearing somebody else's address to burn HER
    //backoff window. The identity must be the attacker's own peer, never the
    //address he typed.
    const std::string id =
        TransportLimits::effectiveClientIp(kVictimIp, kDirectPeer);

    EXPECT_EQ(kDirectPeer, id);
    EXPECT_NE(kVictimIp, id)
            << "a forged header still charges the login backoff to the victim";
}

TEST(TrustedProxyPeer, ALookalikePeerIsNotTheLoopback)
{
    //THE PREFIX TRAP, named instead of suffered. 127.0.0.0/8 is loopback, but
    //"127." appearing ANYWHERE in the string is not: a containment test rather
    //than a prefix test would hand the whole exploit back to any client whose
    //address happens to embed the octets. 10.127.0.5 is an ordinary LAN
    //address and must be treated as one.
    EXPECT_EQ("10.127.0.5",
              TransportLimits::effectiveClientIp(kForgedIp, "10.127.0.5"));
}

TEST(TrustedProxyPeer, AMappedLanAddressIsNotTheLoopback)
{
    //THE SECOND HALF OF THE SAME TRAP. A dual stack listener reports IPv4
    //peers in the ::ffff: form, so the guard has to understand that form - but
    //understanding it must not degrade into trusting everything that carries
    //the prefix. ::ffff:192.0.2.55 is the LAN client of this file, wearing its
    //mapped clothes.
    EXPECT_EQ("::ffff:192.0.2.55",
              TransportLimits::effectiveClientIp(kForgedIp,
                                                 "::ffff:192.0.2.55"));
}

TEST(TrustedProxyPeer, AnUnknownPeerBuysNoTrust)
{
    //HttpClient::getClientIp() answers the literal "unknown" when both
    //peer<uvw::IPv4>() and peer<uvw::IPv6>() fail (HttpClient.cpp:700-727).
    //"unknown" is not the loopback, so the header is dropped and every such
    //connection shares one bucket. That is the SAFE reading - we decline to
    //believe a header on a connection we cannot even name - and it is written
    //down here so the next reader finds a decision rather than an accident.
    EXPECT_EQ("unknown",
              TransportLimits::effectiveClientIp(kForgedIp, "unknown"));
}

//--- The proxy path, unchanged: these are the witnesses ----------------------

TEST(TrustedProxyPeer, BehindTheProxyTheHeaderStillDecides)
{
    //PINS AN ACQUIS - the whole point of T3.24, which must survive T3.39. The
    //peer is haproxy on the loopback, so the header is the proxy's word about
    //the real client and it wins over the peer.
    EXPECT_EQ(kForgedIp,
              TransportLimits::effectiveClientIp(kForgedIp, "127.0.0.1"));
}

TEST(TrustedProxyPeer, TheWholeLoopbackRangeIsTheProxy)
{
    //DECIDED EXPLICITLY: all of 127.0.0.0/8 is loopback (RFC 1122), not just
    //127.0.0.1, so the guard tests the range and not one address. A local
    //resolver or a second proxy bound on 127.0.0.53 is still on this machine.
    EXPECT_EQ(kForgedIp,
              TransportLimits::effectiveClientIp(kForgedIp, "127.0.0.53"));
    EXPECT_EQ(kForgedIp,
              TransportLimits::effectiveClientIp(kForgedIp, "127.1.2.3"));
}

TEST(TrustedProxyPeer, Ipv6LoopbackIsTrustedToo)
{
    //THE WORST FAILURE MODE THIS TICKET CAN HAVE, pinned. If ::1 were left out
    //of the trusted set, haproxy on a dual stack machine would have its header
    //ignored and EVERY client of that installation would collapse into the
    //proxy's single bucket - the exact defect T3.24 fixed, silently
    //reintroduced on the standard deployment.
    EXPECT_EQ(kForgedIp,
              TransportLimits::effectiveClientIp(kForgedIp, "::1"));
}

TEST(TrustedProxyPeer, MappedIpv4LoopbackIsTrustedToo)
{
    //SAME FAILURE MODE, other spelling: a dual stack listener hands back
    //::ffff:127.0.0.1 for an IPv4 loopback connection. Miss this form and
    //haproxy loses its trust on exactly the machines that run dual stack.
    EXPECT_EQ(kForgedIp,
              TransportLimits::effectiveClientIp(kForgedIp,
                                                 "::ffff:127.0.0.1"));
}

TEST(TrustedProxyPeer, TheHeaderlessFallbackIsUntouched)
{
    //PINS AN ACQUIS on both sides of the guard: with no header there is
    //nothing to distrust, and the peer is the answer whoever the peer is.
    //Without this, a guard could "pass" by always returning the peer.
    EXPECT_EQ("127.0.0.1", TransportLimits::effectiveClientIp("", "127.0.0.1"));
    EXPECT_EQ(kDirectPeer, TransportLimits::effectiveClientIp("", kDirectPeer));
    EXPECT_EQ(kDirectPeer, TransportLimits::effectiveClientIp("  ", kDirectPeer));
}

TEST(TrustedProxyPeer, BehindTheProxyTheLastEntryIsStillTheOneRead)
{
    //PINS AN ACQUIS that the guard must not cost us: on the proxy path the
    //client supplied prefix of the list stays worthless. Guard plus rfind, not
    //guard instead of rfind - a fix that trusted the FIRST entry behind the
    //proxy would be vulnerable again through a legitimate haproxy.
    EXPECT_EQ(kVictimIp,
              TransportLimits::effectiveClientIp(
                  std::string(kForgedIp) + ", " + kVictimIp, "127.0.0.1"));
}
