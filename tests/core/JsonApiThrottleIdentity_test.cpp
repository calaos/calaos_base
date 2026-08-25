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
 * T3.24 - WHICH CLIENT DOES THE LOGIN THROTTLE COUNT AGAINST?
 *
 * Two cases here pin the FIX (they were written red-on-purpose in the
 * characterization commit and flipped by it), four pin an ACQUIS the fix had to
 * keep. Each case says which of the two it is, in its own body.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT THAT WAS FIXED
 * ---------------------------------------------------------------------------
 * LoginThrottle is keyed on the string returned by JsonApi::clientIp(), and
 * BOTH transports used to return the TCP peer address:
 *      JsonApiHandlerWS.cpp:45-51    -> httpClient->getClientIp()
 *      JsonApiHandlerHttp.cpp:55-61  -> httpClient->getClientIp()
 * calaos_server always sits behind haproxy in calaos-os (DECISIONS.md,
 * "Throttle de login derrière haproxy"), so that peer is the PROXY, the same
 * address for every user of the installation. One bucket for everybody:
 *   - an attacker who burns the backoff window locked out every legitimate user,
 *   - and his own budget was diluted by everybody else's traffic.
 *
 * The helper that answers the real question already existed and was already
 * used, ten lines away, by the per-IP CONNECTION cap:
 *      HttpClient.cpp:200 -> TransportLimits::effectiveClientIp(xff, peer)
 * so the connection cap identified the client correctly while the login
 * throttle, on the very same connection, did not. Both clientIp() now go
 * through HttpClient::getEffectiveClientIp(), which wraps that same helper -
 * read its trust note before adding a third caller.
 *
 * ---------------------------------------------------------------------------
 * WHY THE TWO ADDRESSES BELOW ARE AS UNALIKE AS THEY ARE
 * ---------------------------------------------------------------------------
 * A fixture with one client, or with two clients that differ by one digit,
 * proves nothing here: the whole question is whether two DIFFERENT clients land
 * in two DIFFERENT buckets. kClientA and kClientB differ in address family, in
 * length and in every character, so no substring/prefix/truncation bug can make
 * them collide by accident. The control that these cases actually bite is to
 * SWAP the two constants (A <-> B) and re-run: every case here must go red.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "JsonApi.h"
#include "JsonApiHandlerWS.h"
#include "JsonApiHandlerHttp.h"
#include "HttpClient.h"
#include "HttpCodes.h"
#include "libuvw.h"

#include <memory>
#include <string>
#include <vector>

using namespace CalaosTest;

namespace
{

//The two clients of the whole file. Different family, different documentation
//range, different length: nothing about one can be mistaken for the other.
const char *const kClientA = "198.51.100.7";
const char *const kClientB = "2001:db8:dead:beef::1";

//T3.39: the two PEER addresses, i.e. the two deployments this file plays.
//
//kProxyPeer is haproxy. calaos-os installs it and calaos_server from the same
//`calaos` meta package, as two podman units on --network=host, and haproxy's
//backend is `server calaos-server 127.0.0.1:5454`: behind the proxy the peer
//is ALWAYS the loopback, so that is what these sessions must seed. Before
//T3.39 they ran on an unconnected socket and the peer was the literal
//"unknown" - which happened to work only because the header was believed
//unconditionally.
//
//kLanPeer is a client that reached port 5454 directly, which a standard
//install allows (listen_address defaults to "0.0.0.0", and must: RemoteUI and
//the LAN mobile apps need it). It is not in 127.0.0.0/8 and shares no prefix
//with either client address above.
const char *const kProxyPeer = "127.0.0.1";
const char *const kLanPeer   = "192.0.2.55";

/*******************************************************************************
 * A connection whose request carried an X-Forwarded-For line.
 *
 * HttpClient inherits HttpParsing::RequestState protected, so a subclass can
 * seed request_headers exactly the way llhttp's on_header_value callback would
 * have (HttpClient.h:265 and :301 write request_headers[lower(field)] = value,
 * i.e. the LAST line of a repeated header wins - which is haproxy's, appended
 * after any line the client itself supplied).
 *
 * The TCP handle is never connected, so the real getClientIp() would answer
 * "unknown" for every client here. Since T3.39 the peer address is part of the
 * security decision, so it is injected instead: see ProxiedClient below.
 ******************************************************************************/
class ProxiedClient: public HttpClient
{
public:
    ProxiedClient(const std::shared_ptr<uvw::TcpHandle> &h,
                  const std::string &xff, const std::string &peer):
        HttpClient(h),
        handle(h),
        peerIp(peer)
    {
        if (!xff.empty())
            request_headers["x-forwarded-for"] = xff;
    }

    //THE SEAM T3.39 NEEDED, and why it is a seam and not a fake. The guard
    //keys on the TCP peer, and the peer comes from a real connected socket: a
    //unit test cannot be reached from 192.0.2.55, and the unconnected handle
    //these sessions run on answers "unknown" for everybody. Overriding the one
    //accessor lets both deployments run through the REAL production path -
    //getEffectiveClientIp() -> TransportLimits::effectiveClientIp() - with the
    //guard itself untouched. The guard is NOT reimplemented here (T3.39 §4.5).
    string getClientIp() const override { return peerIp; }

    /***************************************************************************
     * T3.39 R2 - REACHING THE SECOND CALLER FOR REAL, NOT BY READING.
     *
     * getEffectiveClientIp() has exactly two callers:
     *   - the login throttle of both transports (JsonApiHandlerWS.cpp:45-51,
     *     JsonApiHandlerHttp.cpp:55-61), which every other case in this file
     *     drives, and
     *   - the per-IP CONNECTION cap, HttpClient::trackPerIpCap()
     *     (HttpClient.cpp:188-207).
     *
     * Nothing in the tree reached that second one. Mutating its call site from
     * getEffectiveClientIp() to getClientIp() left the entire suite green
     * (mutation MI of the T3.39 review), so the acceptance criterion "both
     * callers inherit the guard" was held by reading only - the exact shape of
     * the F-LINK-1 debt, where "linked" was mistaken for "exercised".
     *
     * No seam and no reimplementation were needed to close it: trackPerIpCap()
     * is protected (HttpClient.h), so a subclass calls THE PRODUCTION METHOD
     * ITSELF. What comes back is trackedIp, the string trackPerIpCap() commits
     * to HttpServer's per-IP map - the decision as taken, not a recomputation
     * of it.
     *
     * The refusal sentinel is a word, not an empty string and not a zero: a cap
     * that turned the connection away must be distinguishable from a cap that
     * counted the empty identity, and both must be distinguishable from every
     * legitimate answer.
     **************************************************************************/
    std::string trackAndReportCapIdentity()
    {
        if (!trackPerIpCap())
            return "<refused-by-cap>";
        return trackedIp;
    }

    std::shared_ptr<uvw::TcpHandle> handle;
    std::string peerIp;
};

//Builds client + handler on the default loop, and tears them down in the order
//HttpTestRequest documents (handler first, it holds a raw pointer on the
//client; then close the handle and pump so uv runs the close callbacks).
template<typename HandlerT>
class ProxiedSession
{
public:
    explicit ProxiedSession(const std::string &xff,
                            const std::string &peer = kProxyPeer)
    {
        auto loop = uvw::Loop::getDefault();
        tcp = loop->resource<uvw::TcpHandle>();
        client.reset(new ProxiedClient(tcp, xff, peer));
        handler.reset(new HandlerT(client.get()));

        handler->sendData.connect([this](const std::string &data)
        {
            sent.push_back(data);
        });
        handler->closeConnection.connect([this](int code, const std::string &reason)
        {
            closes.emplace_back(code, reason);
        });
    }

    ~ProxiedSession()
    {
        handler.reset();
        client.reset();
        if (tcp)
            tcp->close();
        pumpEventLoop(2);
    }

    ProxiedSession(const ProxiedSession &) = delete;
    ProxiedSession &operator=(const ProxiedSession &) = delete;

protected:
    std::shared_ptr<uvw::TcpHandle> tcp;
    std::unique_ptr<ProxiedClient> client;
    std::unique_ptr<HandlerT> handler;
    std::vector<std::string> sent;
    std::vector<std::pair<int, std::string>> closes;

public:
    //The single identity every per-client security decision of this connection
    //is keyed on: the login throttle of both transports (JsonApiHandlerWS.cpp
    //:45-51, JsonApiHandlerHttp.cpp:55-61) and the per-IP connection cap
    //(HttpClient.cpp:193) all call this one method.
    std::string identity() const { return client->getEffectiveClientIp(); }

    //The identity the per-IP CONNECTION cap actually counted, obtained by
    //running the real HttpClient::trackPerIpCap() on this connection. See
    //ProxiedClient::trackAndReportCapIdentity() for why this exists.
    //
    //It reaches HttpServer::Instance(), which is a live singleton owning a
    //listening socket; the balancing releaseClientIp() is done by
    //~HttpClient(), which ProxiedSession's destructor runs, so a case that
    //calls this leaves the per-IP map exactly as it found it.
    std::string capIdentity() { return client->trackAndReportCapIdentity(); }
};

/*******************************************************************************
 * One websocket login attempt from one client.
 ******************************************************************************/
class WsLogin: public ProxiedSession<JsonApiHandlerWS>
{
public:
    using ProxiedSession<JsonApiHandlerWS>::ProxiedSession;

    //Sends a "login" message and answers true when the server accepted it.
    bool attempt(const std::string &user, const std::string &pass)
    {
        sent.clear();
        handler->processApi(Json{{ "msg", "login" }, { "msg_id", "1" },
                                 { "data", {{ "cn_user", user },
                                            { "cn_pass", pass }} }}.dump(),
                            Params());

        if (sent.size() != 1u)
            return false;

        Json env;
        std::string err;
        if (!parseJsonText(sent.back(), env, err))
            return false;

        return str(member(env, "data"), "success") == "true";
    }
};

/*******************************************************************************
 * One HTTP API request from one client.
 *
 * Single shot by construction (one object, one request): buildHttpResponse()
 * accumulates into resHeaders and flips conn_close, so a second request on the
 * same client would read the leftovers of the first (JsonApiCharacterization.h,
 * point (b)).
 ******************************************************************************/
class HttpLogin: public ProxiedSession<JsonApiHandlerHttp>
{
public:
    using ProxiedSession<JsonApiHandlerHttp>::ProxiedSession;

    //Sends one authenticated API request and answers true when it was served.
    //A refused login - wrong password OR throttled address - is a 400 with an
    //HTML body, and the two are byte identical on purpose.
    bool attempt(const std::string &user, const std::string &pass)
    {
        handler->processApi(Json{{ "action", "get_mcp_info" },
                                 { "cn_user", user },
                                 { "cn_pass", pass }}.dump(),
                            Params());

        if (sent.size() != 1u)
            return false;

        return sent.front().compare(0, 15, "HTTP/1.0 200 OK") == 0;
    }
};

class JsonApiThrottleIdentityTest: public JsonApiCharacterizationTest {};

} //namespace

/*******************************************************************************
 * THE FIX - two different clients get two buckets
 ******************************************************************************/

TEST_F(JsonApiThrottleIdentityTest, WsTwoClientsBehindTheProxyGetTheirOwnBucket)
{
    //PINS THE FIX. Client A burns one failed login; client B, a different
    //machine behind the same haproxy, logs in with the right password and is
    //served. Before T3.24 this expectation was EXPECT_FALSE: B was locked out
    //by A's failure, because both clients answered the proxy address.
    {
        WsLogin attacker(kClientA);
        EXPECT_FALSE(attacker.attempt(apiUser(), "wrong"));
    }

    WsLogin victim(kClientB);
    EXPECT_TRUE(victim.attempt(apiUser(), apiPassword()))
            << "another client's failed login is still blocking this one";
}

TEST_F(JsonApiThrottleIdentityTest, HttpTwoClientsBehindTheProxyGetTheirOwnBucket)
{
    //PINS THE FIX ON THE OTHER TRANSPORT. JsonApiHandlerHttp::clientIp() was
    //the same three lines as the WS one and had the same defect: the HTTP login
    //path collapsed into the same single bucket. Also EXPECT_FALSE before.
    {
        HttpLogin attacker(kClientA);
        EXPECT_FALSE(attacker.attempt(apiUser(), "wrong"));
    }

    HttpLogin victim(kClientB);
    EXPECT_TRUE(victim.attempt(apiUser(), apiPassword()))
            << "another client's failed login is still blocking this one";
}

/*******************************************************************************
 * ACQUIS - the throttle still bites the client that failed
 ******************************************************************************/

TEST_F(JsonApiThrottleIdentityTest, WsTheClientThatFailedIsStillBlocked)
{
    //PINS AN ACQUIS, true before and after the fix. Whatever identity the
    //throttle keys on, the client that just failed must be refused inside its
    //backoff window even with the right password - otherwise the fix would have
    //turned the throttle off instead of scoping it.
    {
        WsLogin first(kClientA);
        EXPECT_FALSE(first.attempt(apiUser(), "wrong"));
    }

    WsLogin second(kClientA);
    EXPECT_FALSE(second.attempt(apiUser(), apiPassword()))
            << "the throttle stopped blocking the client that failed";
}

TEST_F(JsonApiThrottleIdentityTest, HttpTheClientThatFailedIsStillBlocked)
{
    //PINS AN ACQUIS, same reasoning, HTTP transport.
    {
        HttpLogin first(kClientA);
        EXPECT_FALSE(first.attempt(apiUser(), "wrong"));
    }

    HttpLogin second(kClientA);
    EXPECT_FALSE(second.attempt(apiUser(), apiPassword()))
            << "the throttle stopped blocking the client that failed";
}

TEST_F(JsonApiThrottleIdentityTest, OneClientCarriesItsBucketAcrossBothTransports)
{
    //PINS AN ACQUIS, and after the fix it also proves the two clientIp()
    //implementations agree: the failure is registered over WS and the block is
    //observed over HTTP, which can only work if both transports name the client
    //the same way. Before T3.24 it held for a degenerate reason: both said
    //"unknown", so it would also have held with only one transport fixed.
    {
        WsLogin first(kClientA);
        EXPECT_FALSE(first.attempt(apiUser(), "wrong"));
    }

    HttpLogin second(kClientA);
    EXPECT_FALSE(second.attempt(apiUser(), apiPassword()))
            << "the WS failure did not reach the HTTP bucket of the same client";
}

/*******************************************************************************
 * ACQUIS - a forged X-Forwarded-For prefix must not buy a fresh bucket
 ******************************************************************************/

TEST_F(JsonApiThrottleIdentityTest, ForgedForwardedForPrefixDoesNotResetTheBucket)
{
    //PINS AN ACQUIS. X-Forwarded-For is a LIST and everything before the last
    //entry is client supplied: only the last entry was written by the trusted
    //hop. A client rotating the prefix at every attempt must therefore stay in
    //the SAME bucket. Green before T3.24 for a degenerate reason (every client
    //shared "unknown"); now it is the real anti-bypass proof of the fix, and it
    //is what TransportLimits::effectiveClientIp() taking rfind(',') buys us.
    //
    //THE LIST MUST HAVE THREE ENTRIES, NOT TWO. With two, the first comma IS
    //the last one, so rfind and find agree and the case pins nothing about
    //which end is read - MEASURED in the T3.24 review, where mutating rfind to
    //find left the two-entry version GREEN. With three, find answers
    //"<middle>, <last>", which differs between the two attempts, so the case
    //goes red. The two prefixes below are the same two addresses in a different
    //ORDER, so only the rfind reading keeps them in one bucket.
    //
    //It does NOT make the throttle unforgeable. Behind haproxy it does: the
    //proxy appends the last entry itself (tests/TransportHardening_test.cpp,
    //ForwardedForLine.*, driven through the production llhttp callbacks). With
    //NO proxy in front, the client supplies the last entry too and picks its own
    //bucket - that is F-XFF-1, and T3.24 is what exposed the throttle to it.
    //Read the trust note on HttpClient::getEffectiveClientIp() first.
    {
        WsLogin first(std::string("203.0.113.42, 192.0.2.1, ") + kClientA);
        EXPECT_FALSE(first.attempt(apiUser(), "wrong"));
    }

    WsLogin second(std::string("192.0.2.1, 203.0.113.42, ") + kClientA);
    EXPECT_FALSE(second.attempt(apiUser(), apiPassword()))
            << "rotating the forged part of X-Forwarded-For bought a new bucket";
}

/*******************************************************************************
 * ACQUIS - no X-Forwarded-For at all: the TCP peer is used
 ******************************************************************************/

TEST_F(JsonApiThrottleIdentityTest, WithoutForwardedForTheBucketIsThePeers)
{
    //PINS AN ACQUIS, and covers the branch nothing else in this file reaches:
    //the fallback of TransportLimits::effectiveClientIp() when the header is
    //absent. Every session here takes ProxiedSession's DEFAULT peer, kProxyPeer
    //= "127.0.0.1" - so the peer is the loopback for all of them, the header is
    //trusted, and the only thing deciding the bucket is whether a header is
    //there at all. That is what makes the two halves readable. (An earlier
    //revision of this comment said the peer was "unknown" for all of them: that
    //was true before T3.39 made the peer part of the decision, and false since.)
    //
    //(a) two header-less clients resolve to the SAME identity (the peer), so
    //    the second is blocked by the first one's failure. That is the fallback
    //    being taken, not a header being read.
    {
        WsLogin first("");
        EXPECT_FALSE(first.attempt(apiUser(), "wrong"));
    }
    {
        WsLogin second("");
        EXPECT_FALSE(second.attempt(apiUser(), apiPassword()))
                << "the header-less clients did not land in the peer's bucket";
    }

    //(b) and a client that DOES carry a header is not in that bucket: the
    //    fallback identity and the header identity are told apart.
    WsLogin proxied(kClientA);
    EXPECT_TRUE(proxied.attempt(apiUser(), apiPassword()))
            << "a proxied client was blocked by a header-less client's failure";
}

/*******************************************************************************
 * T3.39 - WHOSE WORD IS THE HEADER? The two deployments, end to end.
 *
 * Everything above runs behind haproxy, where the header is the proxy's word
 * about the real client and believing it is correct. These cases run the OTHER
 * deployment: a client that reached port 5454 directly from the LAN, where the
 * header is the client's word about itself. T3.24 gave that client the run of
 * the login throttle; these are the two capabilities it took away.
 ******************************************************************************/

TEST_F(JsonApiThrottleIdentityTest, AForgedHeaderFromTheLanCannotChooseItsBucket)
{
    //PINS THE FIX, capability (a): exempting yourself from the backoff. One
    //attacker, one peer, a FRESH forged identity on each attempt. If the header
    //were believed he would get a fresh bucket every time and could brute force
    //the password with no rate limit at all - which is exactly what happened
    //between T3.24 and T3.39, and what this case was red for.
    {
        WsLogin attacker(kClientA, kLanPeer);
        EXPECT_FALSE(attacker.attempt(apiUser(), "wrong"));
    }

    WsLogin sameAttackerNewCostume(kClientB, kLanPeer);
    EXPECT_FALSE(sameAttackerNewCostume.attempt(apiUser(), apiPassword()))
            << "rotating X-Forwarded-For bought a direct client a fresh bucket";
}

TEST_F(JsonApiThrottleIdentityTest, AForgedHeaderFromTheLanCannotThrottleAVictim)
{
    //PINS THE FIX, capability (b): burning somebody ELSE's backoff window. The
    //attacker is on the LAN and wears kClientA, the address of a real user who
    //comes in through the proxy. The failure must be charged to the attacker's
    //own peer, so the victim's next login - correct password, through haproxy,
    //carrying her real address - has to be served.
    {
        WsLogin attackerWearingTheVictim(kClientA, kLanPeer);
        EXPECT_FALSE(attackerWearingTheVictim.attempt(apiUser(), "wrong"));
    }

    WsLogin victim(kClientA, kProxyPeer);
    EXPECT_TRUE(victim.attempt(apiUser(), apiPassword()))
            << "a forged header still charged the backoff to the victim";
}

TEST_F(JsonApiThrottleIdentityTest, HttpForgedHeaderFromTheLanCannotChooseItsBucket)
{
    //PINS THE FIX ON THE OTHER TRANSPORT. Both clientIp() go through the same
    //HttpClient::getEffectiveClientIp(), so a fix that reached only one of them
    //would leave the HTTP login path wide open - that asymmetry is precisely
    //the bug T3.24 had to repair, and it must not come back by the other end.
    {
        HttpLogin attacker(kClientA, kLanPeer);
        EXPECT_FALSE(attacker.attempt(apiUser(), "wrong"));
    }

    HttpLogin sameAttackerNewCostume(kClientB, kLanPeer);
    EXPECT_FALSE(sameAttackerNewCostume.attempt(apiUser(), apiPassword()))
            << "rotating X-Forwarded-For bought a direct HTTP client a bucket";
}

TEST_F(JsonApiThrottleIdentityTest, TwoDirectLanClientsStillGetTheirOwnBucket)
{
    //PINS AN ACQUIS the guard could easily have cost us, and the reason the
    //peer - not a constant - is what answers on the direct path: two DIFFERENT
    //machines on the LAN must still be told apart, or the guard would have
    //traded the header bypass for the very lockout T3.24 removed. Different
    //peers, and neither sends a header at all.
    {
        WsLogin first("", kLanPeer);
        EXPECT_FALSE(first.attempt(apiUser(), "wrong"));
    }

    WsLogin other("", "203.0.113.9");
    EXPECT_TRUE(other.attempt(apiUser(), apiPassword()))
            << "one LAN client's failure locked out a different LAN client";
}

TEST_F(JsonApiThrottleIdentityTest, TheCapAndTheThrottleAgreeOnOneIdentity)
{
    //PINS THE SINGLE SITE. The per-IP connection cap (HttpClient.cpp:193) and
    //the login throttle of both transports read the SAME method, so the guard
    //had to live in one place - TransportLimits::effectiveClientIp() - and not
    //be copied into each caller. That duplication is the exact fault T3.24
    //repaired; this asserts the identity BY VALUE on both deployments, so a
    //guard that answered some third string could not slip through.
    //
    //Peer and header always differ here, and the case says WHICH of the two
    //came out: asserting only that the two sessions differ would be true on
    //both sides of the guard and would pin nothing.
    WsLogin proxied(kClientA, kProxyPeer);
    EXPECT_EQ(kClientA, proxied.identity())
            << "behind haproxy the proxy's word must decide";

    WsLogin direct(kClientA, kLanPeer);
    EXPECT_EQ(kLanPeer, direct.identity())
            << "a direct client still named its own identity";
}

TEST_F(JsonApiThrottleIdentityTest, AnUnknownPeerDoesNotBuyTrust)
{
    //THE TRAP NAMED INSTEAD OF SUFFERED. getClientIp() answers the literal
    //"unknown" when both peer<uvw::IPv4>() and peer<uvw::IPv6>() fail
    //(HttpClient.cpp:700-727). "unknown" is not the loopback, so the header is
    //dropped and all such connections share one bucket. That is the SAFE
    //reading - we decline to believe a header on a connection we cannot name -
    //and widening the guard to accept "unknown" would reopen the whole hole by
    //the service door, for any connection whose address could not be read.
    WsLogin nameless(kClientA, "unknown");
    EXPECT_EQ("unknown", nameless.identity())
            << "an unreadable peer address bought the header its trust back";
}

/*******************************************************************************
 * THE SECOND CALLER, EXERCISED - the per-IP connection cap
 *
 * Everything above drives the login throttle. These two cases drive the OTHER
 * caller of getEffectiveClientIp(), HttpClient::trackPerIpCap(), through the
 * production method itself. Before them, no test in the tree reached
 * trackPerIpCap() at all and the T3.39 acceptance criterion "both callers
 * inherit the guard" rested on reading the source.
 ******************************************************************************/

TEST_F(JsonApiThrottleIdentityTest, TheConnectionCapCountsTheGuardedIdentity)
{
    //PINS THE FIX at the cap, by value on both deployments.
    //
    //Behind haproxy the cap must count the client the proxy named, or one
    //installation's users share a single 50-connection budget (T3.24). From the
    //LAN it must count the PEER, or a direct client empties everybody's budget
    //by rotating a header it writes itself.
    //
    //Asserting the two differ would pass on both sides of the guard and pin
    //nothing, so each half names the string it expects.
    WsLogin proxied(kClientA, kProxyPeer);
    EXPECT_EQ(kClientA, proxied.capIdentity())
            << "behind haproxy the connection cap ignored the proxy's word";

    WsLogin direct(kClientA, kLanPeer);
    EXPECT_EQ(kLanPeer, direct.capIdentity())
            << "a direct client named its own bucket to the connection cap";
}

TEST_F(JsonApiThrottleIdentityTest, TheCapAndTheThrottleCommitToTheSameString)
{
    //PINS THE SINGLE SITE, now on both callers rather than on one plus a
    //reading of the other. On one and the same connection, the identity the
    //login throttle keys on and the identity the connection cap COUNTED must be
    //the same string - not merely both "guarded", the same. A guard applied in
    //one caller and not the other would show up here as a mismatch, on whichever
    //of the two deployments the omission was made.
    {
        WsLogin proxied(kClientA, kProxyPeer);
        EXPECT_EQ(proxied.identity(), proxied.capIdentity())
                << "behind haproxy the cap and the throttle counted two "
                   "different clients on one connection";
    }
    {
        WsLogin direct(kClientA, kLanPeer);
        EXPECT_EQ(direct.identity(), direct.capIdentity())
                << "from the LAN the cap and the throttle counted two "
                   "different clients on one connection";
    }
}
