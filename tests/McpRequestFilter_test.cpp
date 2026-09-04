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
//T3.42: what the /mcp sidecar is allowed to believe about its client.
//
#include "McpRequestFilter.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace Calaos;
using Calaos::McpRequestFilter::Filter;

namespace {

const std::string CRED = "c0ffee-credential";
const std::string LAN_PEER = "192.0.2.55";

//Every "<credential> <ip>" the filter wrote, in order.
std::vector<std::string> identities(const std::string &out)
{
    std::vector<std::string> found;
    const std::string needle = "X-Calaos-Client: ";
    std::string::size_type pos = 0;
    while ((pos = out.find(needle, pos)) != std::string::npos)
    {
        pos += needle.size();
        auto eol = out.find("\r\n", pos);
        found.push_back(out.substr(pos, eol - pos));
        pos = eol;
    }
    return found;
}

std::string get(const std::string &extraHeaders)
{
    return "GET /mcp HTTP/1.1\r\nHost: h\r\n" + extraHeaders + "\r\n";
}

} // namespace

//---------------------------------------------------------------- the identity

TEST(RelayIdentity, ADirectClientDoesNotChooseItsBucket)
{
    Filter f(LAN_PEER, CRED);
    std::string out;
    ASSERT_TRUE(f.feed(get("X-Forwarded-For: 198.51.100.7\r\n"), out));
    EXPECT_EQ(identities(out), std::vector<std::string>{CRED + " " + LAN_PEER});
}

TEST(RelayIdentity, BehindTheProxyTheHeaderStillDecides)
{
    Filter f("127.0.0.1", CRED);
    std::string out;
    ASSERT_TRUE(f.feed(get("X-Forwarded-For: 10.0.0.1, 203.0.113.7\r\n"), out));
    EXPECT_EQ(identities(out),
              std::vector<std::string>{CRED + " 203.0.113.7"});
}

TEST(RelayIdentity, TheLastForwardedForLineIsTheProxyOne)
{
    Filter f("127.0.0.1", CRED);
    std::string out;
    ASSERT_TRUE(f.feed(get("X-Forwarded-For: 10.0.0.1\r\n"
                           "X-Forwarded-For: 203.0.113.7\r\n"), out));
    EXPECT_EQ(identities(out),
              std::vector<std::string>{CRED + " 203.0.113.7"});
}

TEST(RelayIdentity, AClientSuppliedTrustedHeaderIsDropped)
{
    Filter f(LAN_PEER, CRED);
    std::string out;
    ASSERT_TRUE(f.feed(get("X-Calaos-Client: " + CRED + " 198.51.100.7\r\n"
                           "X-Calaos-Client: stolen 198.51.100.8\r\n"), out));
    EXPECT_EQ(identities(out), std::vector<std::string>{CRED + " " + LAN_PEER});
}

TEST(RelayIdentity, AnUnreadablePeerBuysNothing)
{
    //getClientIp() answers "unknown" on a socket it could not read; that is
    //not the loopback, so the header is dropped and nothing is address-shaped.
    Filter f("unknown", CRED);
    std::string out;
    ASSERT_TRUE(f.feed(get("X-Forwarded-For: 198.51.100.7\r\n"), out));
    EXPECT_EQ(identities(out), std::vector<std::string>{CRED + " unknown"});
}

TEST(RelayIdentity, AnIdentityThatIsNotAnAddressIsReplaced)
{
    //The sidecar splits the value on a space; a forged entry carrying one must
    //not be able to shift the field boundary.
    Filter f("127.0.0.1", CRED);
    std::string out;
    ASSERT_TRUE(f.feed(get("X-Forwarded-For: evil ip\r\n"), out));
    EXPECT_EQ(identities(out), std::vector<std::string>{CRED + " unknown"});
}

//----------------------------------------------------------------- the framing

TEST(Framing, EveryPipelinedRequestIsRewritten)
{
    //F-MCP-XFF-1 replayed on the relay: 20 requests on ONE connection, each
    //rotating X-Forwarded-For. Sanitising only the first head would leave 19
    //chosen identities behind.
    Filter f(LAN_PEER, CRED);
    std::string stream;
    for (int i = 0; i < 20; i++)
        stream += get("X-Forwarded-For: 10.0." + std::to_string(i) + ".7\r\n");

    std::string out;
    ASSERT_TRUE(f.feed(stream, out));

    auto ids = identities(out);
    ASSERT_EQ(ids.size(), 20u);
    for (const auto &id : ids)
        EXPECT_EQ(id, CRED + " " + LAN_PEER);
}

TEST(Framing, ABodyIsNeverReadAsAHeader)
{
    const std::string body =
        "\r\nPOST /mcp HTTP/1.1\r\nX-Calaos-Client: " + CRED + " 6.6.6.6\r\n\r\n";
    Filter f(LAN_PEER, CRED);
    std::string out;
    ASSERT_TRUE(f.feed("POST /mcp HTTP/1.1\r\nHost: h\r\nContent-Length: " +
                       std::to_string(body.size()) + "\r\n\r\n" + body +
                       get(""), out));

    auto ids = identities(out);
    ASSERT_EQ(ids.size(), 3u); //two heads plus the one quoted in the body
    EXPECT_EQ(ids[0], CRED + " " + LAN_PEER);
    EXPECT_EQ(ids[1], CRED + " 6.6.6.6");   //still inside the body, opaque
    EXPECT_EQ(ids[2], CRED + " " + LAN_PEER);
    EXPECT_NE(out.find(body), std::string::npos); //forwarded byte for byte
}

TEST(Framing, AChunkedBodyIsPassedThrough)
{
    Filter f(LAN_PEER, CRED);
    std::string out;
    ASSERT_TRUE(f.feed("POST /mcp HTTP/1.1\r\nHost: h\r\n"
                       "Transfer-Encoding: chunked\r\n\r\n"
                       "4\r\nabcd\r\n0\r\n\r\n" +
                       get("X-Forwarded-For: 198.51.100.7\r\n"), out));

    auto ids = identities(out);
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[1], CRED + " " + LAN_PEER);
    EXPECT_NE(out.find("4\r\nabcd\r\n0\r\n\r\n"), std::string::npos);
}

TEST(Framing, ARequestSplitAcrossWritesIsFramedTheSame)
{
    const std::string stream =
        "POST /mcp HTTP/1.1\r\nHost: h\r\nContent-Length: 5\r\n\r\nhello" +
        get("X-Forwarded-For: 198.51.100.7\r\n");

    std::string whole;
    Filter one(LAN_PEER, CRED);
    ASSERT_TRUE(one.feed(stream, whole));

    std::string drip;
    Filter byByte(LAN_PEER, CRED);
    for (char c : stream)
        ASSERT_TRUE(byByte.feed(std::string(1, c), drip));

    EXPECT_EQ(drip, whole);
    EXPECT_EQ(identities(drip).size(), 2u);
}

//------------------------------------------------------- what is refused, not
//------------------------------------------------------- reasoned about

TEST(Framing, AnObsFoldedHeaderIsRefused)
{
    Filter f(LAN_PEER, CRED);
    std::string out;
    EXPECT_FALSE(f.feed("GET /mcp HTTP/1.1\r\nHost: h\r\n"
                        "X-Calaos-Client: " + CRED + "\r\n 1.2.3.4\r\n\r\n", out));
}

TEST(Framing, ContentLengthWithTransferEncodingIsRefused)
{
    Filter f(LAN_PEER, CRED);
    std::string out;
    EXPECT_FALSE(f.feed("POST /mcp HTTP/1.1\r\nHost: h\r\nContent-Length: 4\r\n"
                        "Transfer-Encoding: chunked\r\n\r\n", out));
}

TEST(Framing, TwoContentLengthsAreRefused)
{
    Filter f(LAN_PEER, CRED);
    std::string out;
    EXPECT_FALSE(f.feed("POST /mcp HTTP/1.1\r\nHost: h\r\n"
                        "Content-Length: 4\r\nContent-Length: 5\r\n\r\n", out));
}

TEST(Framing, ANonNumericContentLengthIsRefused)
{
    Filter f(LAN_PEER, CRED);
    std::string out;
    EXPECT_FALSE(f.feed("POST /mcp HTTP/1.1\r\nHost: h\r\n"
                        "Content-Length: 4x\r\n\r\n", out));
}

TEST(Framing, AnUpgradeIsRefused)
{
    Filter f(LAN_PEER, CRED);
    std::string out;
    EXPECT_FALSE(f.feed("GET /mcp HTTP/1.1\r\nHost: h\r\n"
                        "Connection: Upgrade\r\nSec-WebSocket-Key: k\r\n\r\n", out));
}

TEST(Framing, ATrailerFieldIsRefused)
{
    Filter f(LAN_PEER, CRED);
    std::string out;
    EXPECT_FALSE(f.feed("POST /mcp HTTP/1.1\r\nHost: h\r\n"
                        "Transfer-Encoding: chunked\r\n\r\n"
                        "0\r\nX-Calaos-Client: " + CRED + " 6.6.6.6\r\n\r\n", out));
}

TEST(Framing, ABadChunkSizeIsRefused)
{
    Filter f(LAN_PEER, CRED);
    std::string out;
    EXPECT_FALSE(f.feed("POST /mcp HTTP/1.1\r\nHost: h\r\n"
                        "Transfer-Encoding: chunked\r\n\r\nzz\r\n", out));
}

TEST(Framing, AHeadThatNeverEndsIsRefused)
{
    Filter f(LAN_PEER, CRED);
    std::string out;
    EXPECT_FALSE(f.feed("GET /mcp HTTP/1.1\r\nX: " +
                        std::string(McpRequestFilter::MAX_HEAD_BYTES, 'a'), out));
}

TEST(Framing, AnIncompleteHeadIsNotRefusedYet)
{
    //The witness of the case above: short of the cap, more data is awaited.
    Filter f(LAN_PEER, CRED);
    std::string out;
    EXPECT_TRUE(f.feed("GET /mcp HTTP/1.1\r\nHost: h\r\n", out));
    EXPECT_TRUE(out.empty());
}
