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
 * CHARACTERIZATION COMMIT. Every assertion below records what the tree does
 * TODAY, defect included. The two cases named *ShareOneBucket* pin a DEFECT and
 * are meant to go RED as soon as it is fixed; the others pin an ACQUIS that the
 * fix must not break. Each case says which of the two it is, in its own body.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT
 * ---------------------------------------------------------------------------
 * LoginThrottle is keyed on the string returned by JsonApi::clientIp(), and
 * BOTH transports return the TCP peer address:
 *      JsonApiHandlerWS.cpp:45-51    -> httpClient->getClientIp()
 *      JsonApiHandlerHttp.cpp:55-61  -> httpClient->getClientIp()
 * calaos_server always sits behind haproxy in calaos-os (DECISIONS.md,
 * "Throttle de login derrière haproxy"), so that peer is the PROXY, the same
 * address for every user of the installation. One bucket for everybody:
 *   - an attacker who burns the backoff window locks out every legitimate user,
 *   - and his own budget is diluted by everybody else's traffic.
 *
 * The helper that answers the real question already exists and is already used,
 * ten lines away, by the per-IP CONNECTION cap:
 *      HttpClient.cpp:200-203 -> TransportLimits::effectiveClientIp(xff, peer)
 * so the connection cap identifies the client correctly while the login
 * throttle, on the very same connection, does not.
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
 * DEFECT - two different clients share one bucket
 ******************************************************************************/

TEST_F(JsonApiThrottleIdentityTest, WsTwoClientsBehindTheProxyShareOneBucket)
{
    //PINS A DEFECT. Client A burns one failed login; client B, a different
    //machine behind the same haproxy, is then refused although its credentials
    //are correct. Once clientIp() reads X-Forwarded-For this must become
    //"B is accepted" - this case is written to flip.
    {
        WsLogin attacker(kClientA);
        EXPECT_FALSE(attacker.attempt(apiUser(), "wrong"));
    }

    WsLogin victim(kClientB);
    EXPECT_FALSE(victim.attempt(apiUser(), apiPassword()))
            << "a second client behind the proxy would already be told apart";
}

TEST_F(JsonApiThrottleIdentityTest, HttpTwoClientsBehindTheProxyShareOneBucket)
{
    //PINS THE SAME DEFECT ON THE OTHER TRANSPORT. JsonApiHandlerHttp::clientIp()
    //is the same three lines as the WS one, so the HTTP login path collapses
    //into the same single bucket. Written to flip.
    {
        HttpLogin attacker(kClientA);
        EXPECT_FALSE(attacker.attempt(apiUser(), "wrong"));
    }

    HttpLogin victim(kClientB);
    EXPECT_FALSE(victim.attempt(apiUser(), apiPassword()))
            << "a second client behind the proxy would already be told apart";
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
    //the same way. Today it holds for the wrong reason (both say "unknown").
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
    //the SAME bucket. Green today for a degenerate reason (every client shares
    //"unknown"); after the fix it is the real anti-bypass proof, and it is what
    //TransportLimits::effectiveClientIp() taking rfind(',') buys us.
    {
        WsLogin first(std::string("203.0.113.42, ") + kClientA);
        EXPECT_FALSE(first.attempt(apiUser(), "wrong"));
    }

    WsLogin second(std::string("192.0.2.1, ") + kClientA);
    EXPECT_FALSE(second.attempt(apiUser(), apiPassword()))
            << "rotating the forged part of X-Forwarded-For bought a new bucket";
}
