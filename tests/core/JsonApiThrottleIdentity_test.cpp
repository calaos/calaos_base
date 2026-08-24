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

/*******************************************************************************
 * A connection whose request carried an X-Forwarded-For line.
 *
 * HttpClient inherits HttpParsing::RequestState protected, so a subclass can
 * seed request_headers exactly the way llhttp's on_header_value callback would
 * have (HttpClient.h:265 and :301 write request_headers[lower(field)] = value,
 * i.e. the LAST line of a repeated header wins - which is haproxy's, appended
 * after any line the client itself supplied).
 *
 * The TCP handle is never connected, so getClientIp() answers "unknown" for
 * every client here: that is exactly the production situation behind a proxy,
 * where the peer address is the same for everybody.
 ******************************************************************************/
class ProxiedClient: public HttpClient
{
public:
    ProxiedClient(const std::shared_ptr<uvw::TcpHandle> &h, const std::string &xff):
        HttpClient(h),
        handle(h)
    {
        if (!xff.empty())
            request_headers["x-forwarded-for"] = xff;
    }

    std::shared_ptr<uvw::TcpHandle> handle;
};

//Builds client + handler on the default loop, and tears them down in the order
//HttpTestRequest documents (handler first, it holds a raw pointer on the
//client; then close the handle and pump so uv runs the close callbacks).
template<typename HandlerT>
class ProxiedSession
{
public:
    explicit ProxiedSession(const std::string &xff)
    {
        auto loop = uvw::Loop::getDefault();
        tcp = loop->resource<uvw::TcpHandle>();
        client.reset(new ProxiedClient(tcp, xff));
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
    //absent. Every session here runs on an unconnected socket, so the peer is
    //"unknown" for all of them - which is what makes the two halves readable.
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
