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
 * The admin messages of docs/08_http_api.md (section Administration), on a
 * real HttpServer bound on loopback, against a fake calaos-container reached
 * through CALAOS_CONTAINER_URL. The configuration and the cache (admin_seen_set)
 * live in a temporary directory, never in ~/.cache/calaos.
 *
 * A request from 127.0.0.1 is what haproxy sends: it is a secure transport
 * when it carries X-Forwarded-Proto: https. The other peers are injected on a
 * real HttpClient, the address being the only thing a socket cannot fake here.
 */

#include <gtest/gtest.h>

#include "Utils.h"
#include "HttpServer.h"
#include "HttpClient.h"
#include "JsonApi.h"
#include "JsonApiHandlerHttp.h"
#include "McpServerManager.h"
#include "AdminState.h"
#include "DaemonClient.h"
#include "NotifManager.h"
#include "HistLogger.h"
#include "EventManager.h"
#include "json.hpp"

#include "FakeDaemon.h"
#include "PushTestUtil.h"

#include "libuvw.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <filesystem>
#include <vector>

using namespace Calaos;
using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::steady_clock;

namespace
{

const char *const kUser = "cb2-household";
const char *const kPass = "cb2-household-password";
const char *const kServiceToken = "cb2-service-token";
const char *const kBearer = "cb2-bearer-token";
const char *const kHosts = "calaos.example.net,box.local";

FakeDaemon fakeDaemon;

struct Env
{
    int serverPort = 0;
    int daemonPort = 0;
    std::string base;
    std::string cache;
    std::string tokenFile;
} env;

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

bool pumpUntil(const std::function<bool()> &done, int ms = 6000)
{
    auto loop = uvw::Loop::getDefault();
    const auto deadline = steady_clock::now() + milliseconds(ms);
    while (steady_clock::now() < deadline)
    {
        if (done())
            return true;
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(milliseconds(1));
    }
    return done();
}

int pickFreePort()
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
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

int connectServer()
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(env.serverPort);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

Json parse(const std::string &text)
{
    return Json::parse(text, nullptr, false);
}

std::string str(const Json &j, const char *key)
{
    if (!j.is_object() || !j.contains(key) || !j[key].is_string())
        return std::string();
    return j[key].get<std::string>();
}

std::string maskedTextFrame(const std::string &payload)
{
    static const unsigned char mask[4] = { 0x12, 0x34, 0x56, 0x78 };
    std::string f;
    f.push_back((char)0x81);
    if (payload.size() < 126)
    {
        f.push_back((char)(0x80 | payload.size()));
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

//A websocket client on a real socket; xfp is the X-Forwarded-Proto of the handshake
class WsClient
{
public:
    ~WsClient()
    {
        closeNow();
        drainLoop(30);
    }

    bool open(const std::string &xfp)
    {
        fd = connectServer();
        if (fd < 0)
            return false;

        std::ostringstream req;
        req << "GET /api HTTP/1.1\r\n"
            << "Host: 127.0.0.1:" << env.serverPort << "\r\n"
            << "Connection: Upgrade\r\n"
            << "Upgrade: websocket\r\n"
            << "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\n"
            << "Sec-WebSocket-Version: 13\r\n";
        if (!xfp.empty())
            req << "X-Forwarded-Proto: " << xfp << "\r\n";
        req << "\r\n";

        const std::string h = req.str();
        if (send(fd, h.data(), h.size(), MSG_NOSIGNAL) < 0)
            return false;

        pumpUntil([this]()
                  {
            pump();
            return handshake.find("\r\n\r\n") != std::string::npos;
        },
                  5000);
        return handshake.find("101") != std::string::npos;
    }

    bool login(const std::string &xfp, const char *user = kUser, const char *pass = kPass)
    {
        if (!open(xfp))
            return false;
        const Json a = ask(Json{ { "msg", "login" }, { "msg_id", "l" }, { "data", { { "cn_user", user }, { "cn_pass", pass } } } });
        return str(a["data"], "success") == "true";
    }

    void post(const Json &request)
    {
        const std::string frame = maskedTextFrame(request.dump());
        send(fd, frame.data(), frame.size(), MSG_NOSIGNAL);
    }

    //The envelope of the next frame, null when nothing came back in time
    Json next(int ms = 6000)
    {
        pumpUntil([this]()
                  {
            pump();
            return !frames.empty();
        },
                  ms);
        if (frames.empty())
            return Json();
        const std::string out = frames.front();
        frames.erase(frames.begin());
        return parse(out);
    }

    Json ask(const Json &request, int ms = 6000)
    {
        post(request);
        return next(ms);
    }

    //The data of the answer to an admin message
    Json admin(const std::string &msg, const Json &data, int ms = 6000)
    {
        const Json env = ask(Json{ { "msg", msg }, { "msg_id", "a" }, { "data", data } }, ms);
        EXPECT_EQ(msg, str(env, "msg"));
        EXPECT_EQ("a", str(env, "msg_id"));
        return env.is_object() && env.contains("data") ? env["data"] : Json();
    }

    void closeNow()
    {
        if (fd >= 0)
            close(fd);
        fd = -1;
    }

private:
    void pump()
    {
        char buf[16384];
        const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n <= 0)
            return;
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

        while (rx.size() >= 2)
        {
            size_t len = (unsigned char)rx[1] & 0x7f;
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
            if (((unsigned char)rx[0] & 0x0f) == 0x1)
                frames.push_back(rx.substr(header, len));
            rx.erase(0, header + len);
        }
    }

    int fd = -1;
    std::string handshake;
    std::string rx;
    std::vector<std::string> frames;
};

//One HTTP POST /api.php on a real socket; answers the raw response
std::string httpPost(const std::string &body, const std::string &xfp)
{
    std::ostringstream req;
    req << "POST /api.php HTTP/1.1\r\n"
        << "Host: 127.0.0.1:" << env.serverPort << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n";
    if (!xfp.empty())
        req << "X-Forwarded-Proto: " << xfp << "\r\n";
    req << "\r\n"
        << body;
    const std::string raw = req.str();

    std::string response;
    const int fd = connectServer();
    if (fd < 0)
        return response;

    send(fd, raw.data(), raw.size(), MSG_NOSIGNAL);
    bool closed = false;
    pumpUntil([&]()
              {
        char buf[65536];
        const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0)
            response.append(buf, (size_t)n);
        else if (n == 0)
            closed = true;
        return closed;
    });
    close(fd);
    drainLoop(20);
    return response;
}

Json httpJson(const Json &request, const std::string &xfp)
{
    Json r = request;
    r["cn_user"] = kUser;
    r["cn_pass"] = kPass;
    const std::string response = httpPost(r.dump(), xfp);
    const size_t sep = response.find("\r\n\r\n");
    return sep == std::string::npos ? Json() : parse(response.substr(sep + 4));
}

//A real HttpClient whose peer address and forwarded proto are chosen by the test
class InjectedPeerClient: public HttpClient
{
public:
    InjectedPeerClient(const std::shared_ptr<uvw::TcpHandle> &h, const std::string &p, const std::string &xfp):
        HttpClient(h),
        handle(h),
        peer(p)
    {
        if (!xfp.empty())
            request_headers["x-forwarded-proto"] = xfp;
    }

    string getClientIp() const override { return peer; }

    std::shared_ptr<uvw::TcpHandle> handle;
    std::string peer;
};

class DirectHttp
{
public:
    DirectHttp(const std::string &peer, const std::string &xfp)
    {
        auto tcp = uvw::Loop::getDefault()->resource<uvw::TcpHandle>();
        client = std::make_shared<InjectedPeerClient>(tcp, peer, xfp);
        handler.reset(new JsonApiHandlerHttp(client.get()));
        handler->sendData.connect([this](const std::string &data)
                                  {
            responses.push_back(data);
        });
    }

    ~DirectHttp()
    {
        handler.reset();
        client->handle->close();
        client.reset();
        drainLoop(10);
    }

    Json send(const std::string &body, const Params &get = Params())
    {
        const size_t before = responses.size();
        handler->processApi(body, get);
        pumpUntil([&]()
                  {
            return responses.size() > before;
        });
        if (responses.size() == before)
            return Json();
        const std::string &r = responses.back();
        const size_t sep = r.find("\r\n\r\n");
        return sep == std::string::npos ? Json() : parse(r.substr(sep + 4));
    }

    std::unique_ptr<JsonApiHandlerHttp> handler;
    std::shared_ptr<InjectedPeerClient> client;
    std::vector<std::string> responses;
};

std::string flagPath()
{
    return env.cache + "/" + AdminState::FlagFileName;
}

bool flagOnDisk()
{
    return ::access(flagPath().c_str(), F_OK) == 0;
}

void syncAdminState()
{
    bool done = false;
    AdminState::Instance().refresh([&done]()
                                   {
        done = true;
    });
    pumpUntil([&done]()
              {
        return done;
    });
}

void writeToken(const std::string &token)
{
    std::ofstream f(env.tokenFile, std::ios::trunc);
    f << token << "\n";
}

void expectError(const Json &payload, const char *code)
{
    EXPECT_EQ("false", str(payload, "success")) << payload.dump();
    EXPECT_EQ(code, str(payload, "error")) << payload.dump();
    EXPECT_FALSE(str(payload, "msg").empty()) << payload.dump();
}

Json unlockData(const char *pass = FakeDaemon::Password)
{
    return Json{ { "admin_pass", pass } };
}

class JsonApiAdminTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        LoginThrottle::clear();
        Utils::set_config_option("cn_user", kUser);
        Utils::set_config_option("cn_pass", kPass);
        Utils::set_config_option("api_allowed_hosts", kHosts);
        writeToken(kBearer);

        fakeDaemon.stop();
        ASSERT_TRUE(fakeDaemon.start(env.daemonPort));
        fakeDaemon.reset(kBearer);
        ::setenv("CALAOS_CONTAINER_URL", env.base.c_str(), 1);
        DaemonClient::Instance().configure();

        ::unlink(flagPath().c_str());
        AdminState::Instance().observeUnreachable();
        AdminState::Instance().loadFlag();
        drainLoop(10);
    }

    void TearDown() override
    {
        drainLoop(30);
    }

    void adminDefined()
    {
        fakeDaemon.setAdminSet(true);
        syncAdminState();
        ASSERT_EQ(AdminState::Mode::Set, AdminState::Instance().mode());
        fakeDaemon.clearLog();
    }
};

} // namespace

/*******************************************************************************
 * Pure rules
 ******************************************************************************/

TEST(JsonApiAdminRules, TheDaemonUrlIsLoopbackOnly)
{
    std::string base;
    EXPECT_TRUE(DaemonClient::parseBaseUrl("http://127.0.0.1:8000", base));
    EXPECT_EQ("http://127.0.0.1:8000", base);
    EXPECT_TRUE(DaemonClient::parseBaseUrl("http://127.0.0.1:8000/", base));
    EXPECT_EQ("http://127.0.0.1:8000", base);
    EXPECT_TRUE(DaemonClient::parseBaseUrl("http://127.0.0.1", base));
    EXPECT_EQ("http://127.0.0.1", base);
    EXPECT_TRUE(DaemonClient::parseBaseUrl("http://[::1]:9000", base));
    EXPECT_EQ("http://[::1]:9000", base);

    for (const char *refused: { "http://localhost:8000", "http://10.0.0.1:8000", "https://127.0.0.1:8000",
                                "http://127.0.0.1.evil.net:8000", "http://127.0.0.1@evil.net",
                                "http://evil.net#@127.0.0.1", "http://127.0.0.1:8000/path",
                                "http://127.0.0.1:0", "http://127.0.0.1:99999", "http://127.0.0.1:80a",
                                "http://127.0.0.1:", "http://127.0.0.1:123456", "http://[::1",
                                "http://[::2]:80", "http://[::1]x", "ftp://127.0.0.1", "" })
        EXPECT_FALSE(DaemonClient::parseBaseUrl(refused, base)) << refused;
}

TEST(JsonApiAdminRules, DaemonAnswersAreClassified)
{
    using K = DaemonReply::Kind;

    EXPECT_EQ(K::Unreachable, DaemonReply::parse(0, "", false).kind);
    EXPECT_EQ(K::Unreachable, DaemonReply::parse(200, "not json", false).kind);
    EXPECT_EQ(K::Unreachable, DaemonReply::parse(500, "{\"error\":true}", false).kind);

    const DaemonReply ok = DaemonReply::parse(200, FakeDaemon::ok({ { "admin_set", true } }), true);
    EXPECT_EQ(K::Ok, ok.kind);
    EXPECT_TRUE(ok.data["admin_set"].get<bool>());

    EXPECT_EQ(K::Error, DaemonReply::parse(401, FakeDaemon::err("bad_session"), false).kind);
    EXPECT_EQ(K::Bearer, DaemonReply::parse(401, FakeDaemon::err("bad_bearer"), false).kind);
    EXPECT_EQ(K::Bearer, DaemonReply::parse(401, "Unauthorized", false).kind);

    EXPECT_EQ(K::TooOld, DaemonReply::parse(404, "404 page not found", false).kind);
    EXPECT_EQ(K::TooOld, DaemonReply::parse(404, FakeDaemon::err("not_found"), true).kind);
    const DaemonReply notFound = DaemonReply::parse(404, FakeDaemon::err("not_found"), false);
    EXPECT_EQ(K::Error, notFound.kind);
    EXPECT_EQ("not_found", notFound.code);

    const DaemonReply limited = DaemonReply::parse(429, FakeDaemon::err("rate_limited", { { "retry_after", 7 } }), false);
    EXPECT_EQ(K::Error, limited.kind);
    EXPECT_EQ("daemon says rate_limited", limited.msg);
    EXPECT_EQ(7, limited.data["retry_after"].get<int>());
}

TEST(JsonApiAdminRules, DaemonErrorsAreTranslated)
{
    auto error = [](const std::string &code, const Json &data = Json(), const std::string &msg = "daemon text")
    {
        DaemonReply r;
        r.kind = DaemonReply::Kind::Error;
        r.code = code;
        r.msg = msg;
        r.data = data;
        return JsonApi::adminErrorFromReply(r);
    };

    for (const char *same: { "bad_session", "bad_password", "password_too_short", "admin_already_set",
                             "upgrade_in_progress", "not_allowed", "rate_limited" })
    {
        const Json j = error(same);
        EXPECT_EQ(same, str(j, "error"));
        EXPECT_NE("daemon text", str(j, "msg")) << "the daemon text leaks for " << same;
    }

    EXPECT_EQ("7", str(error("rate_limited", { { "retry_after", 7 } }), "retry_after"));
    EXPECT_EQ("3f9c", str(error("upgrade_in_progress", { { "job_id", "3f9c" } }), "job_id"));
    EXPECT_EQ("10", str(error("password_too_short", { { "min_length", 10 } }), "min_length"));
    EXPECT_FALSE(error("rate_limited", { { "retry_after", Json::array() } }).contains("retry_after"));

    Utils::set_config_option("api_allowed_hosts", kHosts);
    const Json notSet = error("admin_not_set");
    EXPECT_EQ("admin_not_set", str(notSet, "error"));
    EXPECT_EQ(kHosts, str(notSet, "api_allowed_hosts"));

    EXPECT_EQ("daemon text", str(error("invalid_request"), "msg"));
    EXPECT_EQ("package_not_found", str(error("package_not_found"), "error"));
    EXPECT_FALSE(str(error("invalid_request", Json(), ""), "msg").empty());

    for (const char *internal: { "internal", "not_found", "method_not_allowed", "something_new" })
        EXPECT_EQ("internal", str(error(internal), "error")) << internal;

    DaemonReply r;
    r.kind = DaemonReply::Kind::Bearer;
    EXPECT_EQ("daemon_unreachable", str(JsonApi::adminErrorFromReply(r), "error"));
    r.kind = DaemonReply::Kind::TooOld;
    EXPECT_EQ("daemon_too_old", str(JsonApi::adminErrorFromReply(r), "error"));
    r.kind = DaemonReply::Kind::Ok;
    EXPECT_EQ("internal", str(JsonApi::adminErrorFromReply(r), "error"));
}

TEST(JsonApiAdminRules, SecureTransportIsLoopbackAndForwardedHttps)
{
    EXPECT_TRUE(TransportLimits::isSecureTransport("127.0.0.1", "https"));
    EXPECT_TRUE(TransportLimits::isSecureTransport("::1", "https"));
    EXPECT_FALSE(TransportLimits::isSecureTransport("127.0.0.1", "http"));
    EXPECT_FALSE(TransportLimits::isSecureTransport("127.0.0.1", ""));
    EXPECT_FALSE(TransportLimits::isSecureTransport("192.168.1.20", "https"));
    EXPECT_FALSE(TransportLimits::isSecureTransport("100.64.0.3", "https"));
}

TEST(JsonApiAdminRules, SessionsHaveTheDaemonFormat)
{
    EXPECT_TRUE(JsonApi::isValidAdminSession(FakeDaemon::Session));
    EXPECT_FALSE(JsonApi::isValidAdminSession(""));
    EXPECT_FALSE(JsonApi::isValidAdminSession(std::string(FakeDaemon::Session) + "0"));
    EXPECT_FALSE(JsonApi::isValidAdminSession(std::string(63, 'a')));
    EXPECT_FALSE(JsonApi::isValidAdminSession(std::string(64, 'A')));
    EXPECT_FALSE(JsonApi::isValidAdminSession(std::string(64, 'g')));
    EXPECT_FALSE(JsonApi::isValidAdminSession(std::string(32, 'a') + "/" + std::string(31, 'a')));
}

TEST(JsonApiAdminRules, PollingIsFastOnlyWhileUnknownInTheFirstTwoMinutes)
{
    EXPECT_EQ(5.0, AdminState::pollDelay(0.0, false));
    EXPECT_EQ(5.0, AdminState::pollDelay(119.0, false));
    EXPECT_EQ(60.0, AdminState::pollDelay(120.0, false));
    EXPECT_EQ(60.0, AdminState::pollDelay(0.0, true));
}

TEST(JsonApiAdminRules, DaemonCallTimeouts)
{
    EXPECT_EQ(4, DaemonClient::DefaultTimeout);
    EXPECT_EQ(120, DaemonClient::UpdateCheckTimeout);
    EXPECT_EQ(DaemonClient::DefaultTimeout, DaemonClient::Request().timeout);
}

TEST(JsonApiAdminRules, ServiceScopeRefusesTheAdminMessagesInTheAdminShape)
{
    for (const char *command: { "admin_unlock", "admin_set", "admin" })
    {
        EXPECT_TRUE(JsonApi::isAdminCommand(command));
        const auto &denied = JsonApi::serviceScopeDeniedCommands();
        EXPECT_NE(denied.end(), std::find(denied.begin(), denied.end(), std::string(command))) << command;
    }
    EXPECT_FALSE(JsonApi::isAdminCommand("config"));
}

/*******************************************************************************
 * AdminState and its sticky flag
 ******************************************************************************/

TEST_F(JsonApiAdminTest, BootstrapNeedsHouseholdCredentialsNobodyKnows)
{
    EXPECT_TRUE(JsonApi::householdAllowsBootstrap());

    Utils::set_config_option("cn_user", "user");
    Utils::set_config_option("cn_pass", "pass");
    EXPECT_FALSE(JsonApi::householdAllowsBootstrap());

    Utils::set_config_option("cn_pass", "something else");
    EXPECT_TRUE(JsonApi::householdAllowsBootstrap());

    Utils::del_config_option("cn_pass");
    EXPECT_FALSE(JsonApi::householdAllowsBootstrap()) << "the legacy keys answer then";

    Utils::set_config_option("cn_pass", kPass);
    Utils::del_config_option("cn_user");
    EXPECT_FALSE(JsonApi::householdAllowsBootstrap());
}

TEST_F(JsonApiAdminTest, NoDaemonAndNoFlagIsAsToday)
{
    fakeDaemon.stop();
    syncAdminState();

    EXPECT_EQ(AdminState::Mode::Unavailable, AdminState::Instance().mode());
    EXPECT_FALSE(AdminState::Instance().enforced());
    EXPECT_FALSE(flagOnDisk());

    //config and get_mcp_info at household level, over plain HTTP
    const Json config = httpJson(Json{ { "action", "config" }, { "type", "get" } }, "");
    EXPECT_EQ("true", str(config, "success")) << config.dump();
    EXPECT_TRUE(config.contains("config_files"));

    const Json mcp = httpJson(Json{ { "action", "get_mcp_info" } }, "");
    EXPECT_EQ(McpServerManager::Instance().bearerToken(), str(mcp, "token"));

    WsClient ws;
    ASSERT_TRUE(ws.login("https"));
    expectError(ws.admin("admin_unlock", unlockData()), "admin_unavailable");
    expectError(ws.admin("admin_set", { { "new_pass", "a long admin password" } }), "admin_unavailable");
    expectError(ws.admin("admin", { { "action", "update_status" } }), "admin_unavailable");
    expectError(httpJson(Json{ { "action", "admin_unlock" }, { "admin_pass", "x" } }, "https"), "admin_unavailable");
}

TEST_F(JsonApiAdminTest, TheFlagIsSetWhenTheDaemonSaysSet)
{
    fakeDaemon.setAdminSet(true);
    syncAdminState();

    EXPECT_EQ(AdminState::Mode::Set, AdminState::Instance().mode());
    EXPECT_TRUE(AdminState::Instance().enforced());
    EXPECT_TRUE(flagOnDisk());
    EXPECT_EQ("Bearer " + std::string(kBearer), fakeDaemon.last("/api/v2/admin/status").headers["authorization"]);
    EXPECT_EQ(0u, fakeDaemon.last("/api/v2/admin/status").headers.count("x-calaos-admin-session"));
}

TEST_F(JsonApiAdminTest, StickyFlagWithTheDaemonDownFailsClosed)
{
    adminDefined();
    ASSERT_TRUE(flagOnDisk());

    //calaos_server restarts while calaos-container is down
    fakeDaemon.stop();
    AdminState::Instance().observeUnreachable();
    AdminState::Instance().loadFlag();
    EXPECT_EQ(AdminState::Mode::Closed, AdminState::Instance().mode());
    EXPECT_TRUE(AdminState::Instance().enforced());

    syncAdminState();
    EXPECT_EQ(AdminState::Mode::Closed, AdminState::Instance().mode());
    EXPECT_TRUE(flagOnDisk()) << "an unreachable daemon must not clear the flag";

    const Json session = { { "admin_session", FakeDaemon::Session } };
    expectError(httpJson(Json{ { "action", "config" }, { "type", "get" }, { "admin_session", FakeDaemon::Session } }, "https"),
                "daemon_unreachable");
    expectError(httpJson(Json{ { "action", "config" }, { "type", "get" } }, ""), "https_required");
    expectError(httpJson(Json{ { "action", "get_mcp_info" }, { "admin_session", FakeDaemon::Session } }, "https"),
                "daemon_unreachable");

    WsClient ws;
    ASSERT_TRUE(ws.login("https"));
    expectError(ws.admin("admin_unlock", unlockData()), "daemon_unreachable");
    expectError(ws.admin("admin_set", { { "new_pass", "a long admin password" } }), "daemon_unreachable");
    expectError(ws.admin("admin", { { "action", "mcp_regenerate" }, { "admin_session", FakeDaemon::Session } }),
                "daemon_unreachable");
}

TEST_F(JsonApiAdminTest, ADaemonBackOnlineReopensTheGate)
{
    adminDefined();
    fakeDaemon.stop();
    syncAdminState();
    ASSERT_EQ(AdminState::Mode::Closed, AdminState::Instance().mode());

    ASSERT_TRUE(fakeDaemon.start(env.daemonPort));
    const Json config = httpJson(Json{ { "action", "config" }, { "type", "get" }, { "admin_session", FakeDaemon::Session } },
                                 "https");
    EXPECT_EQ("true", str(config, "success")) << config.dump();
    EXPECT_EQ(AdminState::Mode::Set, AdminState::Instance().mode());
}

TEST_F(JsonApiAdminTest, ADaemonBackWithoutAdminServesConfigAsToday)
{
    adminDefined();
    fakeDaemon.stop();
    syncAdminState();
    ASSERT_EQ(AdminState::Mode::Closed, AdminState::Instance().mode());

    //Reset with calaos-os admin reset while calaos_server could not see it
    ASSERT_TRUE(fakeDaemon.start(env.daemonPort));
    fakeDaemon.setAdminSet(false);
    const Json config = httpJson(Json{ { "action", "config" }, { "type", "get" } }, "");
    EXPECT_EQ("true", str(config, "success")) << config.dump();
    EXPECT_EQ(AdminState::Mode::NotSet, AdminState::Instance().mode());
    EXPECT_FALSE(flagOnDisk());
}

TEST_F(JsonApiAdminTest, PollsRequestedTogetherShareOneCall)
{
    fakeDaemon.setAdminSet(true);
    int done = 0;
    AdminState::Instance().refresh([&done]()
                                   {
        done++;
    });
    AdminState::Instance().refresh([&done]()
                                   {
        done++;
    });
    AdminState::Instance().refresh();
    EXPECT_TRUE(pumpUntil([&done]()
                          {
        return done == 2;
    }));
    EXPECT_EQ(1u, fakeDaemon.count("/api/v2/admin/status"));
}

TEST_F(JsonApiAdminTest, AFlagThatCannotBeWrittenIsReported)
{
    //A directory in its place: not writable, even by root
    ASSERT_EQ(0, ::mkdir(flagPath().c_str(), 0700));
    AdminState::Instance().observe(true);
    EXPECT_TRUE(AdminState::Instance().flagPresent()) << "the state in memory still holds";
    AdminState::Instance().observeUnreachable();
    EXPECT_EQ(AdminState::Mode::Closed, AdminState::Instance().mode());
    ::rmdir(flagPath().c_str());
    AdminState::Instance().observe(false);
    EXPECT_FALSE(flagOnDisk());
}

TEST_F(JsonApiAdminTest, AnOldDaemonClearsTheFlag)
{
    adminDefined();
    ASSERT_TRUE(flagOnDisk());

    fakeDaemon.setOldDaemon(true);
    syncAdminState();

    EXPECT_EQ(AdminState::Mode::TooOld, AdminState::Instance().mode());
    EXPECT_FALSE(flagOnDisk());
    EXPECT_FALSE(AdminState::Instance().enforced());

    WsClient ws;
    ASSERT_TRUE(ws.login("https"));
    expectError(ws.admin("admin_unlock", unlockData()), "daemon_too_old");

    const Json config = httpJson(Json{ { "action", "config" }, { "type", "get" } }, "");
    EXPECT_EQ("true", str(config, "success")) << "an old daemon leaves config as today";
}

TEST_F(JsonApiAdminTest, AdminNotSetClearsTheFlagAndIsAnswered)
{
    adminDefined();
    fakeDaemon.setAdminSet(false);
    syncAdminState();

    EXPECT_EQ(AdminState::Mode::NotSet, AdminState::Instance().mode());
    EXPECT_FALSE(flagOnDisk());

    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    const Json unlock = ws.admin("admin_unlock", unlockData());
    expectError(unlock, "admin_not_set");
    EXPECT_EQ(kHosts, str(unlock, "api_allowed_hosts"));

    expectError(ws.admin("admin", { { "action", "update_status" } }), "admin_not_set");
    expectError(ws.admin("admin_set", { { "new_pass", "a long admin password" }, { "admin_session", "" } }),
                "admin_not_set");

    Utils::del_config_option("api_allowed_hosts");
    EXPECT_EQ("", str(ws.admin("admin_unlock", unlockData()), "api_allowed_hosts"));

    EXPECT_EQ(0u, fakeDaemon.count("/api/v2/admin/verify"));
    EXPECT_EQ("true", str(httpJson(Json{ { "action", "config" }, { "type", "get" } }, ""), "success"));
}

TEST_F(JsonApiAdminTest, StartPollsAtOnceAndStopEndsThePolling)
{
    fakeDaemon.setAdminSet(true);
    AdminState::Instance().start();
    EXPECT_TRUE(pumpUntil([]()
                          {
        return AdminState::Instance().mode() == AdminState::Mode::Set;
    }));
    AdminState::Instance().stop();

    fakeDaemon.clearLog();
    drainLoop(200);
    EXPECT_EQ(0u, fakeDaemon.count("/api/v2/admin/status"));
}

TEST_F(JsonApiAdminTest, TheFastPollFiresWhileTheStateIsUnknown)
{
    fakeDaemon.stop();
    AdminState::Instance().start();
    pumpUntil([]()
              {
        return false;
    },
              300);
    ASSERT_EQ(AdminState::Mode::Unavailable, AdminState::Instance().mode());

    ASSERT_TRUE(fakeDaemon.start(env.daemonPort));
    fakeDaemon.setAdminSet(true);
    //The next poll is 5 s away
    EXPECT_TRUE(pumpUntil([]()
                          {
        return AdminState::Instance().mode() == AdminState::Mode::Set;
    },
                          7000));
    AdminState::Instance().stop();
}

TEST_F(JsonApiAdminTest, ABearerRefusalRereadsTheTokenOnce)
{
    writeToken("an-old-token");
    DaemonClient::Instance().configure();
    writeToken(kBearer);
    fakeDaemon.setAdminSet(true);

    syncAdminState();
    EXPECT_EQ(AdminState::Mode::Set, AdminState::Instance().mode());
    EXPECT_EQ(2u, fakeDaemon.count("/api/v2/admin/status"));
}

TEST_F(JsonApiAdminTest, APersistentBearerRefusalIsDaemonUnreachable)
{
    adminDefined();
    fakeDaemon.setBearer("another-token");

    syncAdminState();
    EXPECT_EQ(AdminState::Mode::Closed, AdminState::Instance().mode()) << "a bearer refusal is an invalid answer";
    EXPECT_TRUE(flagOnDisk());

    WsClient ws;
    ASSERT_TRUE(ws.login("https"));
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/admin/status")
            return false;
        res.body = FakeDaemon::ok({ { "admin_set", true } });
        return true;
    });
    syncAdminState();
    expectError(ws.admin("admin_unlock", unlockData()), "daemon_unreachable");
}

TEST_F(JsonApiAdminTest, ADisabledClientIsUnreachable)
{
    ::setenv("CALAOS_CONTAINER_URL", "http://192.168.1.2:8000", 1);
    DaemonClient::Instance().configure();
    EXPECT_TRUE(DaemonClient::Instance().baseUrl().empty());

    syncAdminState();
    EXPECT_EQ(AdminState::Mode::Unavailable, AdminState::Instance().mode());

    WsClient ws;
    ASSERT_TRUE(ws.login("https"));
    expectError(ws.admin("admin_unlock", unlockData()), "admin_unavailable");
    EXPECT_TRUE(fakeDaemon.requests().empty());

    ::setenv("CALAOS_CONTAINER_URL", env.base.c_str(), 1);
    DaemonClient::Instance().configure();
    EXPECT_EQ(env.base, DaemonClient::Instance().baseUrl());
}

TEST_F(JsonApiAdminTest, ASilentDaemonTimesOut)
{
    adminDefined();
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/admin/verify")
            return false;
        res.delayMs = 8000;
        res.body = FakeDaemon::ok(Json());
        return true;
    });

    WsClient ws;
    ASSERT_TRUE(ws.login("https"));
    const auto start = steady_clock::now();
    expectError(ws.admin("admin_unlock", unlockData(), 7000), "daemon_unreachable");
    const auto took = std::chrono::duration_cast<milliseconds>(steady_clock::now() - start).count();
    EXPECT_GE(took, 3500);
    EXPECT_LT(took, 6500);

    fakeDaemon.stop();
}

/*******************************************************************************
 * admin_unlock
 ******************************************************************************/

TEST_F(JsonApiAdminTest, UnlockAnswersASessionAndKeepsTheSocketOnFailure)
{
    adminDefined();

    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    expectError(ws.admin("admin_unlock", unlockData("wrong password")), "bad_password");

    //The websocket is still open and still household-logged
    const Json home = ws.ask(Json{ { "msg", "get_home" }, { "msg_id", "h" } });
    EXPECT_EQ("get_home", str(home, "msg"));

    LoginThrottle::clear();
    const Json ok = ws.admin("admin_unlock", unlockData());
    EXPECT_EQ("true", str(ok, "success")) << ok.dump();
    EXPECT_EQ(FakeDaemon::Session, str(ok, "admin_session"));
    EXPECT_EQ("900", str(ok, "expires_in"));

    const FakeDaemon::Request verify = fakeDaemon.last("/api/v2/admin/verify");
    EXPECT_EQ("POST", verify.method);
    EXPECT_EQ(Json({ { "password", FakeDaemon::Password } }), parse(verify.body));
    EXPECT_EQ("127.0.0.1", verify.headers.at("x-calaos-client-ip"));
    EXPECT_EQ(0u, verify.headers.count("x-calaos-admin-session"));

    //Without msg_id the answer still comes
    const Json noId = ws.ask(Json{ { "msg", "admin_unlock" }, { "data", unlockData() } });
    EXPECT_EQ("admin_unlock", str(noId, "msg"));
    EXPECT_FALSE(noId.contains("msg_id"));
}

TEST_F(JsonApiAdminTest, UnlockOverHttpReadsTheBodyOnly)
{
    adminDefined();

    const Json ok = httpJson(Json{ { "action", "admin_unlock" }, { "admin_pass", FakeDaemon::Password }, { "recovery", true }, { "password", "smuggled" } },
                             "https");
    EXPECT_EQ("true", str(ok, "success")) << ok.dump();
    EXPECT_EQ(FakeDaemon::Session, str(ok, "admin_session"));
    EXPECT_EQ(Json({ { "password", FakeDaemon::Password } }), parse(fakeDaemon.last("/api/v2/admin/verify").body))
        << "the daemon body is built from an allowlist, never copied from the client";

    expectError(httpJson(Json{ { "action", "admin_unlock" } }, "https"), "invalid_request");
    expectError(httpJson(Json{ { "action", "admin_unlock" }, { "admin_pass", 12 } }, "https"), "invalid_request");

    fakeDaemon.clearLog();
    DirectHttp http("127.0.0.1", "https");
    Params get;
    get.Add("action", "admin_unlock");
    get.Add("cn_user", kUser);
    get.Add("cn_pass", kPass);
    get.Add("admin_pass", FakeDaemon::Password);
    expectError(http.send("not json", get), "invalid_request");
    EXPECT_EQ(0u, fakeDaemon.count("/api/v2/admin/verify"));
}

TEST_F(JsonApiAdminTest, AdminFailuresAreThrottledOnTheirOwnKey)
{
    adminDefined();

    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    expectError(ws.admin("admin_unlock", unlockData("wrong password")), "bad_password");
    const Json limited = ws.admin("admin_unlock", unlockData());
    expectError(limited, "rate_limited");
    EXPECT_EQ("1", str(limited, "retry_after"));
    EXPECT_EQ(1u, fakeDaemon.count("/api/v2/admin/verify")) << "a blocked unlock must not reach the daemon";

    //The household login is not blocked by an admin failure
    EXPECT_FALSE(LoginThrottle::isBlocked("127.0.0.1", Utils::getMainLoopTime()));
    WsClient other;
    EXPECT_TRUE(other.login("https"));

    //A successful unlock clears the admin key
    drainLoop(1100);
    EXPECT_EQ("true", str(ws.admin("admin_unlock", unlockData()), "success"));
    EXPECT_FALSE(LoginThrottle::isBlocked("admin:127.0.0.1", Utils::getMainLoopTime()));
    EXPECT_EQ(0.0, LoginThrottle::retryAfter("admin:127.0.0.1", Utils::getMainLoopTime()));
}

TEST_F(JsonApiAdminTest, DaemonRefusalsOfUnlockAreRelayed)
{
    adminDefined();
    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/admin/verify")
            return false;
        res.status = 429;
        res.body = FakeDaemon::err("rate_limited", { { "retry_after", 42 } });
        return true;
    });
    const Json limited = ws.admin("admin_unlock", unlockData());
    expectError(limited, "rate_limited");
    EXPECT_EQ("42", str(limited, "retry_after"));

    //The daemon is authoritative: an admin reset meanwhile
    fakeDaemon.setOverride(nullptr);
    fakeDaemon.setAdminSet(false);
    expectError(ws.admin("admin_unlock", unlockData()), "admin_not_set");
    EXPECT_EQ(AdminState::Mode::NotSet, AdminState::Instance().mode());

    //A session the server cannot use
    fakeDaemon.setAdminSet(true);
    syncAdminState();
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/admin/verify")
            return false;
        res.body = FakeDaemon::ok({ { "session", "short" }, { "expires_in", 900 } });
        return true;
    });
    expectError(ws.admin("admin_unlock", unlockData()), "internal");
}

/*******************************************************************************
 * Transport
 ******************************************************************************/

TEST_F(JsonApiAdminTest, PlainHttpAndPeersOutsideHaproxyAreRefused)
{
    adminDefined();

    for (const char *xfp: { "", "http" })
    {
        WsClient ws;
        ASSERT_TRUE(ws.login(xfp));
        expectError(ws.admin("admin_unlock", unlockData()), "https_required");
        expectError(ws.admin("admin_set", { { "new_pass", "a long admin password" } }), "https_required");
        expectError(ws.admin("admin", { { "action", "update_status" } }), "https_required");

        //The connection stays open
        EXPECT_EQ("get_home", str(ws.ask(Json{ { "msg", "get_home" }, { "msg_id", "h" } }), "msg"));

        expectError(httpJson(Json{ { "action", "admin_unlock" }, { "admin_pass", FakeDaemon::Password } }, xfp),
                    "https_required");
        expectError(httpJson(Json{ { "action", "config" }, { "type", "get" }, { "admin_session", FakeDaemon::Session } },
                             xfp),
                    "https_required");
    }

    //A LAN or VPN peer saying https is not haproxy
    DirectHttp lan("192.168.1.20", "https");
    expectError(lan.send(Json{ { "action", "admin_unlock" }, { "cn_user", kUser }, { "cn_pass", kPass }, { "admin_pass", FakeDaemon::Password } }.dump()),
                "https_required");
    expectError(lan.send(Json{ { "action", "get_mcp_info" }, { "cn_user", kUser }, { "cn_pass", kPass }, { "admin_session", FakeDaemon::Session } }.dump()),
                "https_required");

    EXPECT_TRUE(fakeDaemon.requests().empty());
}

/*******************************************************************************
 * admin_set
 ******************************************************************************/

TEST_F(JsonApiAdminTest, BootstrapIsRefusedOnDefaultOrLegacyCredentials)
{
    fakeDaemon.setAdminSet(false);
    syncAdminState();

    struct Creds
    {
        const char *cnUser;
        const char *cnPass;
        const char *legacyUser;
        const char *legacyPass;
    };
    for (const Creds &c: { Creds{ "user", "pass", nullptr, nullptr },
                           Creds{ nullptr, nullptr, "legacy", "legacy-password" },
                           Creds{ nullptr, nullptr, "", "" } })
    {
        if (c.cnUser)
        {
            Utils::set_config_option("cn_user", c.cnUser);
            Utils::set_config_option("cn_pass", c.cnPass);
        }
        else
        {
            Utils::del_config_option("cn_user");
            Utils::del_config_option("cn_pass");
            Utils::set_config_option("calaos_user", c.legacyUser);
            Utils::set_config_option("calaos_password", c.legacyPass);
        }

        WsClient ws;
        ASSERT_TRUE(ws.login("https", c.cnUser ? c.cnUser : c.legacyUser, c.cnPass ? c.cnPass : c.legacyPass));
        expectError(ws.admin("admin_set", { { "new_pass", "a long admin password" } }), "household_password_required");
    }

    Utils::del_config_option("calaos_user");
    Utils::del_config_option("calaos_password");
    EXPECT_EQ(0u, fakeDaemon.count("/api/v2/admin/set"));
    EXPECT_FALSE(fakeDaemon.isAdminSet());
}

TEST_F(JsonApiAdminTest, BootstrapThenChange)
{
    fakeDaemon.setAdminSet(false);
    syncAdminState();

    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    expectError(ws.admin("admin_set", Json::object()), "invalid_request");
    expectError(ws.admin("admin_set", { { "new_pass", "short" } }), "password_too_short");
    EXPECT_EQ("10", str(ws.admin("admin_set", { { "new_pass", "short" } }), "min_length"));

    //recovery is the root CLI's mode of the daemon: a client asking for it is never relayed
    const Json boot = ws.admin("admin_set", { { "new_pass", "a long admin password" },
                                              { "recovery", true },
                                              { "password", "smuggled" } });
    EXPECT_EQ("true", str(boot, "success")) << boot.dump();
    EXPECT_EQ("bootstrap", str(boot, "mode"));
    EXPECT_FALSE(boot.contains("admin_session"));
    EXPECT_EQ(AdminState::Mode::Set, AdminState::Instance().mode());
    EXPECT_TRUE(flagOnDisk());

    const FakeDaemon::Request set = fakeDaemon.last("/api/v2/admin/set");
    EXPECT_EQ(Json({ { "password", "a long admin password" } }), parse(set.body));
    EXPECT_EQ(0u, set.headers.count("x-calaos-admin-session"));

    fakeDaemon.clearLog();
    expectError(ws.admin("admin_set", { { "new_pass", "a long admin password" } }), "admin_already_set");
    expectError(ws.admin("admin_set", { { "new_pass", "a long admin password" }, { "recovery", true } }),
                "admin_already_set");
    expectError(ws.admin("admin_set", { { "new_pass", "another long password" }, { "admin_session", "" } }),
                "bad_session");
    expectError(ws.admin("admin_set", { { "new_pass", "another long password" }, { "admin_session", 42 } }),
                "bad_session");
    EXPECT_EQ(0u, fakeDaemon.count("/api/v2/admin/set")) << "refused without the daemon";

    expectError(ws.admin("admin_set", { { "new_pass", "another long password" }, { "admin_session", std::string(64, 'e') } }),
                "bad_session");

    const Json change = ws.admin("admin_set", { { "new_pass", "another long password" },
                                                { "admin_session", FakeDaemon::Session },
                                                { "recovery", true } });
    EXPECT_EQ("true", str(change, "success")) << change.dump();
    EXPECT_EQ("change", str(change, "mode"));
    EXPECT_EQ(FakeDaemon::Session, fakeDaemon.last("/api/v2/admin/set").headers["x-calaos-admin-session"]);
    EXPECT_EQ(Json({ { "password", "another long password" } }), parse(fakeDaemon.last("/api/v2/admin/set").body));

    const Json http = httpJson(Json{ { "action", "admin_set" }, { "new_pass", "a third long password" }, { "admin_session", FakeDaemon::Session } }, "https");
    EXPECT_EQ("change", str(http, "mode")) << http.dump();
}

TEST_F(JsonApiAdminTest, BootstrapRacesAndOddAnswersAreTranslated)
{
    fakeDaemon.setAdminSet(false);
    syncAdminState();
    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    //Somebody else bootstrapped between our poll and our call
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/admin/set")
            return false;
        res.status = 409;
        res.body = FakeDaemon::err("admin_already_set");
        return true;
    });
    expectError(ws.admin("admin_set", { { "new_pass", "a long admin password" } }), "admin_already_set");
    EXPECT_EQ(AdminState::Mode::Set, AdminState::Instance().mode());

    //And an admin reset between our poll and a change
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path == "/api/v2/admin/status")
        {
            res.body = FakeDaemon::ok({ { "admin_set", true } });
            return true;
        }
        res.status = 409;
        res.body = FakeDaemon::err("admin_not_set");
        return true;
    });
    expectError(ws.admin("admin_set", { { "new_pass", "a long admin password" }, { "admin_session", FakeDaemon::Session } }),
                "admin_not_set");
    EXPECT_EQ(AdminState::Mode::NotSet, AdminState::Instance().mode());

    //A success without a mode keeps the one the request implied
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/admin/set")
            return false;
        res.body = FakeDaemon::ok(Json());
        return true;
    });
    EXPECT_EQ("bootstrap", str(ws.admin("admin_set", { { "new_pass", "a long admin password" } }), "mode"));
}

/*******************************************************************************
 * config and get_mcp_info
 ******************************************************************************/

TEST_F(JsonApiAdminTest, ConfigAndMcpInfoNeedASessionOnceTheAdminIsSet)
{
    adminDefined();

    const Json noSession = httpJson(Json{ { "action", "config" }, { "type", "get" } }, "https");
    expectError(noSession, "bad_session");
    EXPECT_EQ("Admin session required", str(noSession, "msg"));
    expectError(httpJson(Json{ { "action", "config" }, { "type", "get" }, { "admin_session", "nothex" } }, "https"),
                "bad_session");
    expectError(httpJson(Json{ { "action", "config" }, { "type", "put" }, { "config_files", Json::object() } }, "https"),
                "bad_session");
    expectError(httpJson(Json{ { "action", "get_mcp_info" } }, "https"), "bad_session");
    EXPECT_TRUE(fakeDaemon.requests().empty()) << "a missing session must never reach the daemon";

    const Json config = httpJson(Json{ { "action", "config" }, { "type", "get" }, { "admin_session", FakeDaemon::Session } },
                                 "https");
    EXPECT_EQ("true", str(config, "success")) << config.dump();
    EXPECT_TRUE(config.contains("config_files"));
    const FakeDaemon::Request check = fakeDaemon.last("/api/v2/admin/status");
    EXPECT_EQ(FakeDaemon::Session, check.headers.at("x-calaos-admin-session"));
    EXPECT_EQ("127.0.0.1", check.headers.at("x-calaos-client-ip"));

    const Json mcp = httpJson(Json{ { "action", "get_mcp_info" }, { "admin_session", FakeDaemon::Session } }, "https");
    EXPECT_EQ(McpServerManager::Instance().bearerToken(), str(mcp, "token"));

    expectError(httpJson(Json{ { "action", "get_mcp_info" }, { "admin_session", std::string(64, 'f') } }, "https"),
                "bad_session");
}

TEST_F(JsonApiAdminTest, ASessionInTheQueryIsNeverRead)
{
    adminDefined();

    DirectHttp http("127.0.0.1", "https");
    Params get;
    get.Add("action", "get_mcp_info");
    get.Add("cn_user", kUser);
    get.Add("cn_pass", kPass);
    get.Add("admin_session", FakeDaemon::Session);
    expectError(http.send("not json", get), "bad_session");
    EXPECT_TRUE(fakeDaemon.requests().empty());
}

TEST_F(JsonApiAdminTest, SessionValidationFollowsTheDaemon)
{
    adminDefined();

    //Admin reset: the session is worthless and the state follows
    fakeDaemon.setOverride([](const FakeDaemon::Request &, FakeDaemon::Response &res)
                           {
        res.body = FakeDaemon::ok({ { "admin_set", false } });
        return true;
    });
    expectError(httpJson(Json{ { "action", "get_mcp_info" }, { "admin_session", FakeDaemon::Session } }, "https"),
                "bad_session");
    EXPECT_EQ(AdminState::Mode::NotSet, AdminState::Instance().mode());

    fakeDaemon.setOverride(nullptr);
    adminDefined();
    fakeDaemon.setOverride([](const FakeDaemon::Request &, FakeDaemon::Response &res)
                           {
        res.status = 404;
        res.body = "404 page not found";
        return true;
    });
    expectError(httpJson(Json{ { "action", "get_mcp_info" }, { "admin_session", FakeDaemon::Session } }, "https"),
                "daemon_too_old");
    EXPECT_EQ(AdminState::Mode::TooOld, AdminState::Instance().mode());
}

TEST_F(JsonApiAdminTest, ChangeCredStaysAtHouseholdLevel)
{
    adminDefined();

    WsClient ws;
    ASSERT_TRUE(ws.login(""));
    const Json answer = ws.ask(Json{ { "msg", "settings" }, { "msg_id", "s" }, { "data", { { "action", "change_cred" }, { "old_user", kUser }, { "old_pw", kPass }, { "new_user", kUser }, { "new_pw", "a new household password" } } } });
    EXPECT_EQ("true", str(answer["data"], "success")) << answer.dump();
    EXPECT_EQ("a new household password", Utils::get_config_option("cn_pass"));
    EXPECT_TRUE(fakeDaemon.requests().empty());
}

/*******************************************************************************
 * The admin message
 ******************************************************************************/

TEST_F(JsonApiAdminTest, TheAdminMessageIsGatedBeforeAnyDaemonCall)
{
    adminDefined();

    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    expectError(ws.admin("admin", Json::object()), "invalid_request");
    expectError(ws.admin("admin", { { "action", 3 } }), "invalid_request");
    expectError(ws.admin("admin", { { "action", "system/install/start" } }), "not_allowed");
    expectError(ws.admin("admin", { { "action", "console_open" }, { "admin_session", FakeDaemon::Session } }),
                "not_allowed");
    expectError(ws.admin("admin", { { "action", "update_check" } }), "bad_session");
    expectError(ws.admin("admin", { { "action", "system_reboot" }, { "admin_session", "" } }), "bad_session");
    EXPECT_TRUE(fakeDaemon.requests().empty());

    //update_status passes the gate without a session; the relay table is not served yet
    const Json status = ws.admin("admin", { { "action", "update_status" } });
    expectError(status, "not_allowed");
    EXPECT_EQ("Not available yet", str(status, "msg"));

    //A data member that is not an object
    expectError(ws.ask(Json{ { "msg", "admin" }, { "msg_id", "x" }, { "data", "update_status" } })["data"],
                "invalid_request");
}

TEST_F(JsonApiAdminTest, McpRegenerateChangesTheTokenAndRestartsTheSidecar)
{
    adminDefined();

    const std::string before = McpServerManager::Instance().bearerToken();
    const std::string starts = env.cache + "/mcp_starts";
    auto startCount = [&starts]()
    {
        std::ifstream f(starts);
        std::string line;
        int n = 0;
        while (std::getline(f, line))
            n++;
        return n;
    };
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return startCount() >= 1;
    }));
    const int startsBefore = startCount();

    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    expectError(ws.admin("admin", { { "action", "mcp_regenerate" }, { "admin_session", std::string(64, 'b') } }),
                "bad_session");
    EXPECT_EQ(before, McpServerManager::Instance().bearerToken());

    const Json ok = ws.admin("admin", { { "action", "mcp_regenerate" }, { "admin_session", FakeDaemon::Session } });
    EXPECT_EQ("true", str(ok, "success")) << ok.dump();
    EXPECT_EQ(Json::object(), ok["result"]);

    const std::string after = McpServerManager::Instance().bearerToken();
    EXPECT_NE(before, after);
    EXPECT_EQ(64u, after.size());
    EXPECT_EQ(after, Utils::get_config_option("mcp_token"));
    EXPECT_TRUE(pumpUntil([&]()
                          {
        return startCount() > startsBefore;
    },
                          8000))
        << "the sidecar was not restarted";
}

TEST_F(JsonApiAdminTest, AnAnswerForAClosedConnectionIsDropped)
{
    adminDefined();
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/admin/verify")
            return false;
        res.delayMs = 300;
        res.body = FakeDaemon::ok({ { "session", FakeDaemon::Session }, { "expires_in", 900 } });
        return true;
    });

    {
        WsClient ws;
        ASSERT_TRUE(ws.login("https"));
        ws.post(Json{ { "msg", "admin_unlock" }, { "msg_id", "gone" }, { "data", unlockData() } });
        drainLoop(50);
        ws.closeNow();
    }
    drainLoop(600);
    EXPECT_EQ(1u, fakeDaemon.count("/api/v2/admin/verify"));

    //Same while the state is being polled
    fakeDaemon.setOverride([](const FakeDaemon::Request &, FakeDaemon::Response &res)
                           {
        res.delayMs = 300;
        res.body = FakeDaemon::ok({ { "admin_set", true } });
        return true;
    });
    AdminState::Instance().observeUnreachable();
    {
        WsClient ws;
        ASSERT_TRUE(ws.login("https"));
        ws.post(Json{ { "msg", "admin_unlock" }, { "msg_id", "gone" }, { "data", unlockData() } });
        drainLoop(50);
        ws.closeNow();
    }
    drainLoop(600);
    EXPECT_EQ(AdminState::Mode::Set, AdminState::Instance().mode());
}

/*******************************************************************************
 * Alerts
 ******************************************************************************/

TEST_F(JsonApiAdminTest, AnAdminSetOutOfBandClosesConfigAtOnce)
{
    fakeDaemon.setAdminSet(false);
    syncAdminState();
    ASSERT_EQ(AdminState::Mode::NotSet, AdminState::Instance().mode());
    ASSERT_EQ("true", str(httpJson(Json{ { "action", "config" }, { "type", "get" } }, ""), "success"));

    //calaos-os admin set, no event, the next poll far away
    fakeDaemon.setAdminSet(true);
    expectError(httpJson(Json{ { "action", "config" }, { "type", "get" } }, ""), "https_required");
    expectError(httpJson(Json{ { "action", "get_mcp_info" } }, ""), "https_required");
    expectError(httpJson(Json{ { "action", "config" }, { "type", "get" } }, "https"), "bad_session");
    EXPECT_EQ(AdminState::Mode::Set, AdminState::Instance().mode());

    //An old daemon in memory is asked again too
    fakeDaemon.setAdminSet(false);
    syncAdminState();
    fakeDaemon.setOldDaemon(true);
    syncAdminState();
    ASSERT_EQ(AdminState::Mode::TooOld, AdminState::Instance().mode());
    fakeDaemon.setOldDaemon(false);
    fakeDaemon.setAdminSet(true);
    expectError(httpJson(Json{ { "action", "get_mcp_info" } }, ""), "https_required");
}

TEST_F(JsonApiAdminTest, DaemonCallsDoNotLogAboveDebug)
{
    fakeDaemon.stop();
    PushTest::StdoutCapture capture;

    syncAdminState();
    WsClient ws;
    ASSERT_TRUE(ws.login("https"));
    expectError(ws.admin("admin_unlock", unlockData()), "admin_unavailable");

    const std::string out = capture.stop();
    std::istringstream lines(out);
    std::string line;
    int loud = 0;
    while (std::getline(lines, line))
    {
        if (line.find("urlutils") != std::string::npos &&
            (line.find("[INF]") != std::string::npos || line.find("[WRN]") != std::string::npos))
            loud++;
    }
    EXPECT_EQ(0, loud) << out;

    //The same failure from an ordinary transfer is a WARNING: the capture is not blind
    PushTest::StdoutCapture contrast;
    bool done = false;
    UrlDownloader *dl = new UrlDownloader(env.base + "/contrast", true);
    dl->m_signalComplete.connect([&done](int)
                                 {
        done = true;
    });
    dl->httpGet();
    pumpUntil([&done]()
              {
        return done;
    });
    const std::string loudOut = contrast.stop();
    EXPECT_NE(std::string::npos, loudOut.find("Transfer failed")) << loudOut;
}

TEST_F(JsonApiAdminTest, ARedirectFromTheDaemonIsNotFollowed)
{
    adminDefined();
    const std::string elsewhere = env.base + "/elsewhere";
    fakeDaemon.setOverride([elsewhere](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/admin/verify")
            return false;
        res.status = 307;
        res.location = elsewhere;
        res.body = "";
        return true;
    });

    WsClient ws;
    ASSERT_TRUE(ws.login("https"));
    expectError(ws.admin("admin_unlock", unlockData()), "daemon_unreachable");
    drainLoop(100);
    EXPECT_EQ(0u, fakeDaemon.count("/elsewhere"));
}

TEST_F(JsonApiAdminTest, PasswordsOverTheDaemonLimitAreRefusedHere)
{
    adminDefined();
    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    const std::string tooLong(1025, 'p');
    expectError(ws.admin("admin_unlock", { { "admin_pass", tooLong } }), "invalid_request");
    expectError(ws.admin("admin_set", { { "new_pass", tooLong }, { "admin_session", FakeDaemon::Session } }),
                "invalid_request");
    EXPECT_TRUE(fakeDaemon.requests().empty());

    expectError(ws.admin("admin_unlock", { { "admin_pass", std::string(1024, 'p') } }), "bad_password");
}

TEST_F(JsonApiAdminTest, AnOldDaemonBehindVerifyOrSetIsRecorded)
{
    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    for (const char *msg: { "admin_unlock", "admin_set" })
    {
        adminDefined();
        fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                               {
            if (req.path == "/api/v2/admin/status")
                return false;
            res.status = 404;
            res.body = "404 page not found";
            return true;
        });
        const Json data = std::string(msg) == "admin_unlock" ? unlockData() : Json{ { "new_pass", "a long admin password" }, { "admin_session", FakeDaemon::Session } };
        expectError(ws.admin(msg, data), "daemon_too_old");
        EXPECT_EQ(AdminState::Mode::TooOld, AdminState::Instance().mode()) << msg;
        fakeDaemon.setOverride(nullptr);
        LoginThrottle::clear();
    }
}

TEST_F(JsonApiAdminTest, AnAlertIsPushedAndStoredOncePerId)
{
    HistLogger::Instance().registerPushToken("cb2-push-token", HistLogger::PUSH_HW_ANDROID);
    drainLoop(200);

    EXPECT_FALSE(AdminState::Instance().recordAlert("upgrade_progress", "0000000000000001"));

    EXPECT_TRUE(AdminState::Instance().recordAlert("admin_set", "00000000000000a1"));
    ASSERT_TRUE(pumpUntil([]()
                          {
        return fakeDaemon.count("/api/push") == 1;
    }));
    const std::string push = fakeDaemon.last("/api/push").body;
    EXPECT_NE(std::string::npos, push.find("cb2-push-token"));
    EXPECT_NE(std::string::npos, push.find("Admin password set"));

    EXPECT_FALSE(AdminState::Instance().recordAlert("admin_set", "00000000000000a1")) << "a replay alerts again";
    EXPECT_TRUE(AdminState::Instance().recordAlert("admin_reset", "00000000000000a2"));
    ASSERT_TRUE(pumpUntil([]()
                          {
        return fakeDaemon.count("/api/push") == 2;
    }));
    EXPECT_NE(std::string::npos, fakeDaemon.last("/api/push").body.find("Admin password removed"));

    bool read = false;
    std::vector<std::string> messages;
    HistLogger::Instance().getEvents(0, 50, [&](bool, string, const vector<HistEvent> &events, int, int)
                                     {
        for (const HistEvent &e: events)
            if (e.event_type == CalaosEvent::EventPushNotification)
                messages.push_back(e.event_raw);
        read = true;
    });
    ASSERT_TRUE(pumpUntil([&read]()
                          {
        return read;
    }));
    ASSERT_EQ(2u, messages.size());

    //An old id leaves the dedup window after enough newer ones
    for (int i = 0; i < 64; i++)
        AdminState::Instance().recordAlert("admin_set", "10000000000000" + std::to_string(10 + i));
    EXPECT_TRUE(AdminState::Instance().recordAlert("admin_set", "00000000000000a1"));
    drainLoop(300);
}

/*
 * Own main: the server, the fake daemon, the configuration and the token file
 * must exist before the singletons read them. A fake calaos_mcp records every
 * start, so a sidecar restart can be seen.
 */
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_cb2_admin_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
        return 1;

    const std::string cfg = std::string(base) + "/config";
    env.cache = std::string(base) + "/cache";
    const std::string bin = std::string(base) + "/bin";
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(env.cache.c_str(), 0700);
    ::mkdir(bin.c_str(), 0700);

    Utils::initConfigOptions(const_cast<char *>(cfg.c_str()), const_cast<char *>(env.cache.c_str()), true);
    Utils::set_config_option("listen_address", "127.0.0.1");
    Utils::set_config_option("cn_user", kUser);
    Utils::set_config_option("cn_pass", kPass);
    Utils::set_config_option("mcp_service_token", kServiceToken);

    {
        const std::string sidecar = bin + "/calaos_mcp";
        std::ofstream f(sidecar);
        f << "#!/bin/sh\necho started >> \"$CALAOS_CACHE_PATH/mcp_starts\"\nexec sleep 300\n";
        f.close();
        ::chmod(sidecar.c_str(), 0700);
    }
    ::setenv("CALAOS_BIN_PREFIX", bin.c_str(), 1);

    env.tokenFile = std::string(base) + "/ct.token";
    writeToken(kBearer);

    //The daemon holds its port before the server picks one
    env.daemonPort = pickFreePort();
    env.base = "http://127.0.0.1:" + std::to_string(env.daemonPort);
    if (!fakeDaemon.start(env.daemonPort))
        return 1;
    env.serverPort = pickFreePort();

    ::setenv("CALAOS_CONTAINER_URL", env.base.c_str(), 1);
    ::setenv("CALAOS_CONTAINER_TOKEN_FILE", env.tokenFile.c_str(), 1);
    NotifManager::setPushServiceUrl(env.base + "/api/push");

    McpServerManager::Instance().start();
    HttpServer::Instance(env.serverPort);
    drainLoop(50);

    const int rc = RUN_ALL_TESTS();

    McpServerManager::Instance().stop();
    fakeDaemon.stop();
    drainLoop(50);
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    return rc;
}
