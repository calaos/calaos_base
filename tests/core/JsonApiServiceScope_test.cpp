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

/*
 * THE SERVICE SCOPE IS A PROPERTY OF THE SESSION, NOT OF THE TRANSPORT.
 *
 * The eight commands a service session may not run were guarded eight times
 * inside one transport's dispatch chain, and the other transport did not carry
 * the notion at all. What holds them now is a single rule on the session, read
 * by every dispatch.
 *
 * WHAT IS REAL HERE, AND WHAT IS NOT.
 *   - The websocket half runs on a REAL HttpServer bound on loopback: a real
 *     TCP socket, a real handshake, a real login_service against the token
 *     McpServerManager holds, and real masked frames. Nothing calls a dispatch
 *     directly, because a guard exercised by a direct call says nothing about
 *     the path a client takes.
 *   - The http half CANNOT be driven that way, and the reason is the finding
 *     of this ticket rather than a shortcut: no HTTP entry opens a service
 *     session. That absence is itself pinned below, so it cannot quietly stop
 *     being true; the guard on that side is exercised on the shipped handler
 *     through a real HttpClient, one request, no fake dispatch.
 *
 * THE LIST DRIVES THE CASES. Every loop walks
 * JsonApi::serviceScopeDeniedCommands() and also asserts its exact content:
 * a rule that loses a name would otherwise shrink the loop instead of
 * reddening it, and a rule that renames one would refuse a command nobody
 * sends.
 */

#include <gtest/gtest.h>

#include "Utils.h"
#include "HttpServer.h"
#include "HttpClient.h"
#include "JsonApi.h"
#include "JsonApiHandlerHttp.h"
#include "McpServerManager.h"
#include "json.hpp"

#include "libuvw.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Calaos;
using std::chrono::steady_clock;
using std::chrono::milliseconds;
using std::chrono::seconds;

namespace
{

const char *const kAdminUser = "t360-admin";
const char *const kAdminPass = "t360-admin-password";
const char *const kServiceToken = "t360-service-token";

//The eight, spelled out here and compared with the shipped rule. Two lists
//that must agree: the production one drives the loops, this one proves the
//production one still says what the decision said.
const char *const kExpectedDenied[] =
{ "set_param", "del_param", "audio_db", "set_timerange", "autoscenario",
  "eventlog", "register_push", "settings" };

//The contrast. Without it, moving a command out of the guarded set would leave
//every refusal case green.
const char *const kAllowedCommands[] =
{ "get_home", "get_state", "get_states", "query", "get_param", "get_io",
  "audio", "get_timerange" };

int &serverPort()
{
    static int p = 0;
    return p;
}

void drainLoop(int ms)
{
    auto loop = uvw::Loop::getDefault();
    const auto deadline = steady_clock::now() + milliseconds(ms);
    while (steady_clock::now() < deadline)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(milliseconds(1));
    }
}

int pickFreePort()
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return 0;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;

    int port = 0;
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) == 0)
    {
        socklen_t len = sizeof(a);
        if (getsockname(fd, (struct sockaddr *)&a, &len) == 0)
            port = ntohs(a.sin_port);
    }
    close(fd);
    return port;
}

int connectLoopback()
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(serverPort());

    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

//A client frame is masked, always. The payloads here stay well under 64 KiB.
std::string maskedTextFrame(const std::string &payload)
{
    static const unsigned char mask[4] = { 0x37, 0xfa, 0x21, 0x3d };

    std::string f;
    f.push_back((char)0x81);
    if (payload.size() < 126)
    {
        f.push_back((char)(0x80 | (payload.size() & 0x7f)));
    }
    else
    {
        f.push_back((char)(0x80 | 126));
        f.push_back((char)((payload.size() >> 8) & 0xff));
        f.push_back((char)(payload.size() & 0xff));
    }
    for (int i = 0; i < 4; i++)
        f.push_back((char)mask[i]);
    for (size_t i = 0; i < payload.size(); i++)
        f.push_back((char)(payload[i] ^ mask[i % 4]));
    return f;
}

/*
 * A real websocket client on a real socket.
 *
 * The server frames it reads back are unmasked, so the reader below is the
 * server-to-client direction only. It pumps the uv loop while it waits: the
 * server runs in this very process, on this very thread.
 */
class WsClient
{
public:
    ~WsClient()
    {
        if (fd >= 0)
            close(fd);
        drainLoop(30);
    }

    bool open()
    {
        fd = connectLoopback();
        if (fd < 0)
            return false;

        std::ostringstream req;
        req << "GET /api HTTP/1.1\r\n"
            << "Host: 127.0.0.1:" << serverPort() << "\r\n"
            << "Connection: Upgrade\r\n"
            << "Upgrade: websocket\r\n"
            << "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\n"
            << "Sec-WebSocket-Version: 13\r\n\r\n";

        const std::string h = req.str();
        if (send(fd, h.data(), h.size(), MSG_NOSIGNAL) < 0)
            return false;

        const auto deadline = steady_clock::now() + seconds(5);
        while (steady_clock::now() < deadline)
        {
            pump();
            if (handshake.find("\r\n\r\n") != std::string::npos)
                break;
        }
        return handshake.find("101") != std::string::npos;
    }

    //Sends one request and answers the payload of the first frame that comes
    //back, or an empty string when the server said nothing.
    std::string ask(const Json &request, int ms = 1500)
    {
        const std::string frame = maskedTextFrame(request.dump());
        if (send(fd, frame.data(), frame.size(), MSG_NOSIGNAL) < 0)
            return std::string();

        const auto deadline = steady_clock::now() + milliseconds(ms);
        while (steady_clock::now() < deadline)
        {
            pump();
            if (!frames.empty())
            {
                const std::string out = frames.front();
                frames.erase(frames.begin());
                return out;
            }
        }
        return std::string();
    }

private:
    void pump()
    {
        auto loop = uvw::Loop::getDefault();
        loop->run<uvw::Loop::Mode::NOWAIT>();

        char buf[8192];
        const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0)
        {
            if (handshake.find("\r\n\r\n") == std::string::npos)
            {
                handshake.append(buf, (size_t)n);
                const size_t end = handshake.find("\r\n\r\n");
                if (end != std::string::npos)
                {
                    rx.append(handshake, end + 4, std::string::npos);
                    handshake.erase(end + 4);
                }
            }
            else
            {
                rx.append(buf, (size_t)n);
            }
            decode();
        }
        std::this_thread::sleep_for(milliseconds(1));
    }

    //Server frames carry no mask bit.
    void decode()
    {
        while (rx.size() >= 2)
        {
            const unsigned char b1 = (unsigned char)rx[1];
            size_t len = b1 & 0x7f;
            size_t header = 2;

            if (len == 126)
            {
                if (rx.size() < 4)
                    return;
                len = ((unsigned char)rx[2] << 8) | (unsigned char)rx[3];
                header = 4;
            }
            else if (len == 127)
            {
                if (rx.size() < 10)
                    return;
                len = 0;
                for (int i = 2; i < 10; i++)
                    len = (len << 8) | (unsigned char)rx[i];
                header = 10;
            }

            if (rx.size() < header + len)
                return;

            const unsigned char opcode = (unsigned char)rx[0] & 0x0f;
            if (opcode == 0x1)
                frames.push_back(rx.substr(header, len));
            rx.erase(0, header + len);
        }
    }

    int fd = -1;
    std::string handshake;
    std::string rx;
    std::vector<std::string> frames;
};

/*
 * One HTTP request over a real socket, answered by a real HttpServer.
 */
struct HttpExchange
{
    bool connected = false;
    std::string response;
};

std::string httpPost(const std::string &body, bool keepAlive = false)
{
    std::ostringstream req;
    req << "POST /api.php HTTP/1.1\r\n"
        << "Host: 127.0.0.1:" << serverPort() << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: " << (keepAlive? "keep-alive": "close") << "\r\n"
        << "\r\n" << body;
    return req.str();
}

HttpExchange httpExchange(const std::string &raw)
{
    HttpExchange ex;

    const int fd = connectLoopback();
    if (fd < 0)
        return ex;
    ex.connected = true;

    if (send(fd, raw.data(), raw.size(), MSG_NOSIGNAL) >= 0)
    {
        auto loop = uvw::Loop::getDefault();
        const auto deadline = steady_clock::now() + seconds(5);
        while (steady_clock::now() < deadline)
        {
            loop->run<uvw::Loop::Mode::NOWAIT>();

            char buf[8192];
            const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
            if (n > 0)
                ex.response.append(buf, (size_t)n);

            if (ex.response.find("\r\n\r\n") != std::string::npos)
                break;

            std::this_thread::sleep_for(milliseconds(1));
        }
    }

    close(fd);
    drainLoop(80);
    return ex;
}

std::string httpBody(const std::string &response)
{
    const size_t sep = response.find("\r\n\r\n");
    if (sep == std::string::npos)
        return std::string();
    return response.substr(sep + 4);
}

Json parse(const std::string &text)
{
    return Json::parse(text, nullptr, false);
}

std::string str(const Json &j, const char *key)
{
    if (!j.is_object())
        return std::string();
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string())
        return std::string();
    return it->get<std::string>();
}

/*
 * The shipped HTTP handler, on a real HttpClient, in service scope.
 *
 * A subclass only to enter the scope, exactly as RemoteUIWebSocketHandler
 * subclasses the websocket handler to reach the session state of its base.
 * There is no HTTP verb that opens a service session - see
 * NoHttpEntryOpensAServiceSession - so this is the only way the guard on that
 * side can be exercised at all, and saying so is part of the ticket.
 */
class HttpServiceSession: public JsonApiHandlerHttp
{
public:
    explicit HttpServiceSession(HttpClient *c): JsonApiHandlerHttp(c)
    {
        serviceScope = true;
    }
};

//A real HttpClient on an unconnected handle: buildHttpResponse() needs one and
//never touches the socket (same construction as the characterization harness).
class LoopbackClient: public HttpClient
{
public:
    explicit LoopbackClient(const std::shared_ptr<uvw::TcpHandle> &h):
        HttpClient(h), handle(h) {}

    std::shared_ptr<uvw::TcpHandle> handle;
};

class HttpDirectSession
{
public:
    HttpDirectSession()
    {
        auto loop = uvw::Loop::getDefault();
        auto tcp = loop->resource<uvw::TcpHandle>();
        client = std::make_shared<LoopbackClient>(tcp);
        handler.reset(new HttpServiceSession(client.get()));
        handler->sendData.connect([this](const std::string &data)
        {
            responses.push_back(data);
        });
    }

    ~HttpDirectSession()
    {
        handler.reset();
        if (client && client->handle)
            client->handle->close();
        client.reset();
        drainLoop(10);
    }

    void send(const Json &request)
    {
        handler->processApi(request.dump(), Params());
    }

    size_t count() const { return responses.size(); }
    std::string last() const
    {
        return responses.empty()? std::string(): responses.back();
    }

private:
    std::shared_ptr<LoopbackClient> client;
    std::unique_ptr<HttpServiceSession> handler;
    std::vector<std::string> responses;
};

Json authenticated(Json request)
{
    request["cn_user"] = kAdminUser;
    request["cn_pass"] = kAdminPass;
    return request;
}

class JsonApiServiceScopeTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        //A failed login blocks the source address for a second, and every case
        //here comes from 127.0.0.1.
        LoginThrottle::clear();
        drainLoop(20);
    }

    void TearDown() override
    {
        drainLoop(50);
    }

    //Opens a real service scoped session on a real socket.
    static bool loginService(WsClient &ws)
    {
        if (!ws.open())
            return false;

        const std::string answer = ws.ask(Json{
            { "msg", "login_service" }, { "msg_id", "svc" },
            { "data", {{ "token", kServiceToken }} }});

        const Json env = parse(answer);
        return str(env, "msg") == "login_service" &&
               str(env["data"], "success") == "true";
    }
};

} // namespace

/*******************************************************************************
 * The rule itself, counted at the source
 ******************************************************************************/

TEST_F(JsonApiServiceScopeTest, TheRuleHoldsExactlyTheEightNamedCommands)
{
    const vector<string> &denied = JsonApi::serviceScopeDeniedCommands();

    ASSERT_EQ(8u, denied.size())
            << "the guarded set is not eight commands any more, and every loop "
               "of this file walks it";

    for (const char *name: kExpectedDenied)
        EXPECT_NE(denied.end(), std::find(denied.begin(), denied.end(), string(name)))
                << name << " left the guarded set";

    //No duplicate would show in the size check alone if a name were dropped and
    //another doubled.
    vector<string> sorted = denied;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(sorted.end(), std::unique(sorted.begin(), sorted.end()))
            << "the guarded set holds the same command twice";

    for (const char *allowed: kAllowedCommands)
        EXPECT_EQ(denied.end(), std::find(denied.begin(), denied.end(), string(allowed)))
                << allowed << " is refused although it only reads";
}

/*******************************************************************************
 * The websocket half, over a real socket
 ******************************************************************************/

TEST_F(JsonApiServiceScopeTest, TheEightAreRefusedOverARealServiceScopedWebsocket)
{
    WsClient ws;
    ASSERT_TRUE(loginService(ws))
            << "no service scoped session was opened, so this case measures nothing";

    const vector<string> &denied = JsonApi::serviceScopeDeniedCommands();
    ASSERT_EQ(8u, denied.size()) << "the loop below would walk a shorter list";

    for (const string &command: denied)
    {
        const std::string answer = ws.ask(Json{
            { "msg", command }, { "msg_id", "sd" },
            { "data", Json::object() }});

        ASSERT_FALSE(answer.empty()) << "no answer at all for " << command;

        const Json env = parse(answer);
        ASSERT_TRUE(env.is_object()) << command << " answered " << answer;

        EXPECT_EQ(command, str(env, "msg"))
                << "the refusal must name the refused command";
        EXPECT_EQ("sd", str(env, "msg_id")) << command;

        const Json data = env.value("data", Json::object());
        EXPECT_EQ("scope denied", str(data, "error")) << command;
        //A refusal is an error payload, never a success flag, and it carries
        //nothing else.
        EXPECT_EQ(1u, data.size()) << command << " answered " << answer;
    }
}

TEST_F(JsonApiServiceScopeTest, TheReadCommandsAreNotRefusedOnTheSameRealSession)
{
    WsClient ws;
    ASSERT_TRUE(loginService(ws))
            << "no service scoped session was opened, so this case measures nothing";

    for (const char *command: kAllowedCommands)
    {
        const std::string answer = ws.ask(Json{
            { "msg", command }, { "msg_id", "sa" },
            { "data", {{ "type", "list" }} }});

        ASSERT_FALSE(answer.empty()) << command << " answered nothing at all";

        const Json env = parse(answer);
        const Json data = env.value("data", Json::object());
        EXPECT_NE("scope denied", str(data, "error"))
                << command << " is refused although it is not in the guarded set";
    }
}

/*******************************************************************************
 * THE MEASUREMENT: no HTTP entry opens a service session
 *
 * The whole weight of "the guard on the HTTP side breaks nobody" rests on this
 * case. It is written so that the day a transport learns to open a service
 * session, this case is what says so.
 ******************************************************************************/

TEST_F(JsonApiServiceScopeTest, NoHttpEntryOpensAServiceSession)
{
    /* Two of the four requests below are refused on purpose, and the throttle
     * keys on the source address: without a clear between them the ones that
     * must be SERVED would be refused for the wrong reason.
     */
    //The websocket verb, sent as an HTTP action, with the real token.
    LoginThrottle::clear();
    const HttpExchange asAction = httpExchange(httpPost(
        authenticated(Json{{ "action", "login_service" },
                           { "token", kServiceToken }}).dump()));
    ASSERT_TRUE(asAction.connected) << "no connection to the server";
    EXPECT_EQ("unknown action", str(parse(httpBody(asAction.response)), "error"))
            << "the HTTP dispatch knows login_service, so it may open a scope";

    //The service token offered where the credentials go.
    LoginThrottle::clear();
    const HttpExchange asCredentials = httpExchange(httpPost(
        Json{{ "action", "get_home" },
             { "cn_user", kServiceToken },
             { "cn_pass", kServiceToken }}.dump()));
    ASSERT_TRUE(asCredentials.connected) << "no connection to the server";
    EXPECT_NE(std::string::npos, asCredentials.response.find("400"))
            << "the service token authenticates an HTTP request";

    //And the same verb without credentials is refused before any dispatch.
    LoginThrottle::clear();
    const HttpExchange bare = httpExchange(httpPost(
        Json{{ "action", "login_service" }, { "token", kServiceToken }}.dump()));
    ASSERT_TRUE(bare.connected) << "no connection to the server";
    EXPECT_NE(std::string::npos, bare.response.find("400"))
            << "an unauthenticated login_service is served over HTTP";

    //Anti-vacuity: an ordinary admin request on the same server is served, so
    //the three refusals above are about the scope and not about a dead server.
    LoginThrottle::clear();
    const HttpExchange admin = httpExchange(httpPost(
        authenticated(Json{{ "action", "get_home" }}).dump()));
    ASSERT_TRUE(admin.connected) << "no connection to the server";
    const Json home = parse(httpBody(admin.response));
    ASSERT_TRUE(home.is_object())
            << "the server did not answer an ordinary admin request, so nothing "
               "above measures a refusal";
    EXPECT_NE("scope denied", str(home, "error"))
            << "an admin HTTP session is refused, the guard leaks out of the scope";
}

/*
 * The guard sits on the path a real HTTP client takes, and only the scope
 * decides. The anti-vacuity above asks get_home, which the rule never names: a
 * guard that refused the eight to EVERY session would leave it green. These two
 * are named by the rule and served by the main dispatch table, so a rule that
 * stopped reading the scope reddens here, over a socket.
 */
TEST_F(JsonApiServiceScopeTest, TheEightAreStillServedToAnAdminOverARealSocket)
{
    for (const char *command: { "set_param", "del_param" })
    {
        ASSERT_NE(JsonApi::serviceScopeDeniedCommands().end(),
                  std::find(JsonApi::serviceScopeDeniedCommands().begin(),
                            JsonApi::serviceScopeDeniedCommands().end(), string(command)))
                << command << " is not in the guarded set, so this case measures nothing";

        LoginThrottle::clear();
        const HttpExchange ex = httpExchange(httpPost(
            authenticated(Json{{ "action", command }, { "id", "no_such_io" }}).dump()));
        ASSERT_TRUE(ex.connected) << "no connection to the server";

        const Json body = parse(httpBody(ex.response));
        ASSERT_TRUE(body.is_object()) << command << " answered " << ex.response;
        EXPECT_NE("scope denied", str(body, "error"))
                << command << " is refused to an admin over a real HTTP request";
    }
}

/*******************************************************************************
 * The HTTP half of the rule
 *
 * Driven on the shipped handler with a real HttpClient rather than over a
 * socket, because the case above measures that no socket can put an HTTP
 * session in this state. See the file header.
 ******************************************************************************/

TEST_F(JsonApiServiceScopeTest, TheHttpDispatchRefusesTheSameEightCommands)
{
    const vector<string> &denied = JsonApi::serviceScopeDeniedCommands();
    ASSERT_EQ(8u, denied.size()) << "the loop below would walk a shorter list";

    for (const string &command: denied)
    {
        HttpDirectSession http;
        http.send(authenticated(Json{{ "action", command }}));

        ASSERT_EQ(1u, http.count()) << "no answer at all for " << command;

        const Json body = parse(httpBody(http.last()));
        ASSERT_TRUE(body.is_object()) << command << " answered " << http.last();

        EXPECT_EQ("scope denied", str(body, "error")) << command;
        EXPECT_EQ(1u, body.size()) << command << " answered " << http.last();
    }
}

TEST_F(JsonApiServiceScopeTest, TheHttpDispatchStillServesTheReadCommandsUnderTheScope)
{
    //The contrast on this transport too: a guard that refused everything would
    //pass the case above.
    for (const char *command: kAllowedCommands)
    {
        HttpDirectSession http;
        http.send(authenticated(Json{{ "action", command }, { "type", "list" }}));

        ASSERT_EQ(1u, http.count()) << command << " answered nothing at all";

        const Json body = parse(httpBody(http.last()));
        EXPECT_NE("scope denied", str(body, "error"))
                << command << " is refused although it is not in the guarded set";
    }
}

/*******************************************************************************
 * One payload, two envelopes
 ******************************************************************************/

TEST_F(JsonApiServiceScopeTest, TheRefusalPayloadIsTheSameBytesOnBothTransports)
{
    WsClient ws;
    ASSERT_TRUE(loginService(ws))
            << "no service scoped session was opened, so this case measures nothing";

    const std::string wsAnswer = ws.ask(Json{
        { "msg", "set_param" }, { "msg_id", "1" }, { "data", Json::object() }});
    ASSERT_FALSE(wsAnswer.empty()) << "the websocket session answered nothing";

    HttpDirectSession http;
    http.send(authenticated(Json{{ "action", "set_param" }}));
    ASSERT_EQ(1u, http.count()) << "the HTTP session answered nothing";

    const Json wsData = parse(wsAnswer).value("data", Json::object());
    const Json httpBodyDoc = parse(httpBody(http.last()));

    //Byte for byte, not member by member: a client must not be able to tell
    //which transport it is on from the refusal it gets back.
    EXPECT_EQ(wsData.dump(), httpBodyDoc.dump());
    EXPECT_EQ(JsonApi::scopeDeniedAnswer().dump(), wsData.dump());
}

/*
 * Own main: the listening server has to exist before any case runs, and the
 * credentials and the service token have to be in the configuration before
 * McpServerManager caches them.
 */
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_t360_cfg_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
    {
        std::cerr << "could not create a configuration directory" << std::endl;
        return 1;
    }

    const std::string cfg = std::string(base) + "/config";
    const std::string cache = std::string(base) + "/cache";
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(cache.c_str(), 0700);

    Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                             const_cast<char *>(cache.c_str()), true);
    //Loopback only: this binds a real listening socket.
    Utils::set_config_option("listen_address", "127.0.0.1");
    /* checkCredentials() prefers cn_user/cn_pass when both are set, and the
     * shipped configuration ships them set; writing only calaos_user would
     * leave every request of this file unauthenticated.
     */
    Utils::set_config_option("calaos_user", kAdminUser);
    Utils::set_config_option("calaos_password", kAdminPass);
    Utils::set_config_option("cn_user", kAdminUser);
    Utils::set_config_option("cn_pass", kAdminPass);
    Utils::set_config_option("mcp_service_token", kServiceToken);

    /* start() is what fills the token members; it stat()s calaos_mcp under the
     * bin prefix and returns silently when it is not there, so no sidecar is
     * ever spawned and make check stays hermetic.
     */
    ::setenv("CALAOS_BIN_PREFIX", "/nonexistent/t360/bin", 1);
    McpServerManager::Instance().start();

    serverPort() = pickFreePort();
    if (serverPort() <= 0)
    {
        std::cerr << "could not reserve a loopback port" << std::endl;
        return 1;
    }

    HttpServer::Instance(serverPort());
    drainLoop(50);

    return RUN_ALL_TESTS();
}
