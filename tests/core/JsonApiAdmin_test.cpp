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
#include "AdminEvents.h"
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

#include <algorithm>
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

std::string requestPaths()
{
    std::string out;
    for (const FakeDaemon::Request &r: fakeDaemon.requests())
        out += r.method + " " + r.path + (r.headers.count("x-calaos-admin-session") ? " (session)" : "") + "\n";
    return out;
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

//A real HttpClient whose peer address and forwarded headers are chosen by the test
class InjectedPeerClient: public HttpClient
{
public:
    InjectedPeerClient(const std::shared_ptr<uvw::TcpHandle> &h, const std::string &p, const std::string &xfp,
                       const std::string &xff):
        HttpClient(h),
        handle(h),
        peer(p)
    {
        if (!xfp.empty())
            request_headers["x-forwarded-proto"] = xfp;
        if (!xff.empty())
            request_headers["x-forwarded-for"] = xff;
    }

    string getClientIp() const override { return peer; }

    std::shared_ptr<uvw::TcpHandle> handle;
    std::string peer;
};

class DirectHttp
{
public:
    DirectHttp(const std::string &peer, const std::string &xfp, const std::string &xff = std::string())
    {
        auto tcp = uvw::Loop::getDefault()->resource<uvw::TcpHandle>();
        client = std::make_shared<InjectedPeerClient>(tcp, peer, xfp, xff);
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
        AdminState::Instance().resetAlertPushes(AdminState::AlertPushInterval);
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
                             "upgrade_in_progress", "not_allowed", "rate_limited", "package_held",
                             "approver_required", "approver_limit", "approver_exists", "approver_not_found",
                             "approver_last", "code_required", "bad_code", "code_expired" })
    {
        const Json j = error(same);
        EXPECT_EQ(same, str(j, "error"));
        EXPECT_NE("daemon text", str(j, "msg")) << "the daemon text leaks for " << same;
    }

    EXPECT_EQ("7", str(error("rate_limited", { { "retry_after", 7 } }), "retry_after"));
    EXPECT_EQ("3f9c", str(error("upgrade_in_progress", { { "job_id", "3f9c" } }), "job_id"));
    EXPECT_EQ("10", str(error("password_too_short", { { "min_length", 10 } }), "min_length"));
    EXPECT_FALSE(error("rate_limited", { { "retry_after", Json::array() } }).contains("retry_after"));
    EXPECT_EQ("0", str(error("bad_code", { { "attempts_left", 0 } }), "attempts_left"));
    EXPECT_EQ("2", str(error("approver_limit", { { "max", 2 } }), "max"));
    EXPECT_FALSE(error("code_expired", { { "attempts_left", 3 } }).contains("attempts_left"));

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
    EXPECT_EQ(30, DaemonClient::RestartTimeout);
    EXPECT_EQ(DaemonClient::DefaultTimeout, DaemonClient::Request().timeout);

    EXPECT_EQ(120, JsonApi::adminRelayTimeout("update_check"));
    EXPECT_EQ(30, JsonApi::adminRelayTimeout("system_restart"));
    for (const char *action: { "update_status", "update_available", "update_upgrade_all", "update_upgrade",
                               "system_reboot", "system_info" })
        EXPECT_EQ(4, JsonApi::adminRelayTimeout(action)) << action;
    for (const char *action: { "approver_list", "approver_candidates", "approver_enroll", "approver_confirm",
                               "approver_remove", "code_request", "code_verify", "console_open" })
        EXPECT_EQ(4, JsonApi::adminRelayTimeout(action)) << action;
    for (const char *other: { "mcp_regenerate", "system_rollback_snapshot", "" })
        EXPECT_EQ(0, JsonApi::adminRelayTimeout(other)) << other;
}

TEST(JsonApiAdminRules, PackageNamesFollowTheDaemonRule)
{
    const std::string newline = std::string("calaos-server") + '\n';
    const std::string accented = std::string("caf") + char(0xc3) + char(0xa9);
    for (const std::string &good: { std::string("calaos-server"), std::string("linux-image-6.1.0+rpt-rpi-v8"),
                                    std::string("a"), std::string("0ad"), std::string("libc6.1"),
                                    std::string(128, 'a') })
        EXPECT_TRUE(JsonApi::isValidPackageName(good)) << good;

    for (const std::string &bad: { std::string(), std::string("-y"), std::string("--allow-downgrades"),
                                   std::string(".hidden"), std::string("+evil"), std::string("foo=1.0"),
                                   std::string("foo/stable"), std::string("foo:amd64"), std::string("a..b"),
                                   std::string(129, 'a'), std::string("calaos server"), newline, accented, std::string("a_b") })
        EXPECT_FALSE(JsonApi::isValidPackageName(bad)) << bad;
}

TEST(JsonApiAdminRules, AdminActionsComeFromTheClosedTable)
{
    for (const char *action: { "update_status", "update_check", "update_available", "update_upgrade_all",
                               "system_reboot", "system_restart", "system_info", "mcp_regenerate" })
        EXPECT_TRUE(JsonApi::adminRequestRefusal(action, Json::object()).is_null()) << action;

    for (const char *action: { "system_rollback_snapshot", "system/reboot", "update_installed", "admin_reset",
                               "network_list", "UPDATE_CHECK" })
        EXPECT_EQ("not_allowed", str(JsonApi::adminRequestRefusal(action, Json::object()), "error")) << action;

    const std::string code = "c000000000000001";
    const std::string id = "a000000000000001";
    for (const auto &ok: std::vector<std::pair<const char *, Json>>{
             { "approver_list", Json::object() },
             { "approver_candidates", Json::object() },
             { "approver_enroll", { { "token_id", "9a41f0c2d3e4b5a6" } } },
             { "approver_enroll", { { "token_id", "9a41f0c2d3e4b5a6" }, { "code_id", code }, { "code", "012345" } } },
             { "approver_confirm", { { "code_id", code }, { "code", "123456" } } },
             { "approver_remove", { { "approver_id", id }, { "code_id", code }, { "code", "123456" } } },
             { "code_request", { { "code_action", "console_open" } } },
             { "code_request", { { "code_action", "approver_enroll" }, { "approver_id", id } } },
             { "code_request", { { "code_action", "admin_password" }, { "code_params", { { "password", "a long password" } } } } },
             { "code_request", { { "code_action", "approver_remove" }, { "code_params", { { "approver_id", id } } } } },
             { "code_verify", { { "code_id", code }, { "code", "123456" } } },
             { "console_open", { { "code_id", code }, { "code", "123456" } } } })
        EXPECT_TRUE(JsonApi::adminRequestRefusal(ok.first, Json{ { "params", ok.second } }).is_null()) << ok.first << " " << ok.second.dump();

    for (const auto &bad: std::vector<std::pair<const char *, Json>>{
             { "approver_enroll", Json::object() },
             { "approver_enroll", { { "token_id", "9A41F0C2D3E4B5A6" } } },
             { "approver_enroll", { { "token", "a-push-token" }, { "device_name", "Pixel" } } },
             { "approver_enroll", { { "token_id", "9a41f0c2d3e4b5a6" }, { "code_id", code } } },
             { "approver_confirm", { { "code_id", code } } },
             { "approver_confirm", { { "code_id", code }, { "code", "12345" } } },
             { "approver_confirm", { { "code_id", code }, { "code", "12345a" } } },
             { "approver_confirm", { { "code_id", "../admin/reset00" }, { "code", "123456" } } },
             { "approver_confirm", { { "code_id", code }, { "code", 123456 } } },
             { "approver_remove", { { "code_id", code }, { "code", "123456" } } },
             { "code_request", Json::object() },
             { "code_request", { { "code_action", "network_config" } } },
             { "code_request", { { "code_action", "console_open" }, { "code_params", "x" } } },
             { "code_request", { { "code_action", "admin_password" } } },
             { "code_request", { { "code_action", "admin_password" }, { "code_params", { { "password", std::string(1025, 'a') } } } } },
             { "code_request", { { "code_action", "approver_remove" }, { "code_params", Json::object() } } },
             { "code_request", { { "code_action", "console_open" }, { "approver_id", "first" } } },
             { "code_verify", Json::object() },
             { "console_open", Json::object() } })
        EXPECT_EQ("invalid_request", str(JsonApi::adminRequestRefusal(bad.first, Json{ { "params", bad.second } }), "error"))
            << bad.first << " " << bad.second.dump();

    const Json upgrade = { { "params", { { "package", "calaos-server" } } } };
    EXPECT_TRUE(JsonApi::adminRequestRefusal("update_upgrade", upgrade).is_null());
    for (const Json &bad: { Json::object(), Json{ { "params", "calaos-server" } }, Json{ { "params", Json::object() } },
                            Json{ { "params", { { "package", 3 } } } }, Json{ { "params", { { "package", "-y" } } } } })
        EXPECT_EQ("invalid_request", str(JsonApi::adminRequestRefusal("update_upgrade", bad), "error")) << bad.dump();
}

namespace
{

//What a household client may see of FakeDaemon::leakyProgress()
Json householdProgress()
{
    return Json{ { "status", "upgrading" }, { "total_percent", 42 }, { "current_step", "unpack" }, { "current_percent", 50 }, { "pkg_index", 1 }, { "pkg_count", 2 }, { "progress", 1 }, { "progress_total", 2 }, { "packages", { { { "state", "unpack" }, { "percent", 50 } }, { { "state", "pending" }, { "percent", 0 } } } }, { "error", "upgrade failed" } };
}

//The inventory, the apt text and the identifiers that must never reach a household client
void expectNothingLeaks(const Json &j)
{
    const std::string text = j.dump();
    for (const char *leak: { "calaos-server", "linux-image", "deb.calaos.fr", "/var/cache", "inventory",
                             "approver", "code_id", "00000000000000", "Get:1" })
        EXPECT_EQ(std::string::npos, text.find(leak)) << leak << " in " << text;
}

} //namespace

TEST(JsonApiAdminRules, TheHouseholdViewOfAnUpgradeHasNoInventory)
{
    const Json progress = AdminEvents::householdStatus(FakeDaemon::leakyProgress());
    EXPECT_EQ(householdProgress(), progress);
    expectNothingLeaks(progress);

    Json texty = FakeDaemon::leakyProgress();
    texty["status"] = "upgrading; rm -rf /";
    texty["current_step"] = std::string(100, 'a');
    texty["total_percent"] = Json::object();
    const Json cleaned = AdminEvents::householdStatus(texty);
    EXPECT_FALSE(cleaned.contains("status"));
    EXPECT_FALSE(cleaned.contains("current_step"));
    EXPECT_FALSE(cleaned.contains("total_percent"));

    EXPECT_EQ(Json::object(), AdminEvents::householdStatus(Json("idle")));
    EXPECT_FALSE(AdminEvents::householdStatus(Json{ { "status", "idle" }, { "error", "" } }).contains("error"));

    const Json job = AdminEvents::householdJob(FakeDaemon::leakyJob());
    EXPECT_EQ((Json{ { "id", "3f9c2a7e5b1d4c80" }, { "kind", "upgrade" }, { "status", "error" }, { "error", "upgrade failed" }, { "reboot_required", false }, { "daemon_restart", false }, { "started_at", "2026-10-22T09:12:03Z" }, { "finished_at", "2026-10-22T09:13:03Z" } }),
              job);
    Json interrupted = FakeDaemon::leakyJob();
    interrupted["error"] = "interrupted";
    EXPECT_EQ("interrupted", str(AdminEvents::householdJob(interrupted), "error"));
    EXPECT_TRUE(AdminEvents::householdJob(Json()).is_null());
}

TEST(JsonApiAdminRules, OnlyTheProgressAndTheAlertsReachHouseholdClients)
{
    const std::string now = "2026-10-23T10:00:00Z";

    const Json progress = AdminEvents::householdEventFor({ { "type", "upgrade_progress" }, { "data", FakeDaemon::leakyProgress() } }, now);
    EXPECT_EQ((Json{ { "type", "upgrade_progress" }, { "ts", now }, { "data", householdProgress() } }), progress);

    const Json phone = { { "device_name", "Pixel 8" }, { "token_id", "9a41f0c2d3e4b5a6" }, { "session_audit_id", "1a2b3c4d" } };
    for (const char *alert: { "admin_set", "admin_reset", "approver_set", "approver_removed", "console_opened" })
    {
        const Json a = AdminEvents::householdEventFor(FakeDaemon::alertEvent(alert, "00000000000000a1", phone), now);
        EXPECT_EQ((Json{ { "type", alert }, { "ts", "2026-10-22T09:14:00Z" } }), a) << "the id and the alert fields stay here";
        const Json noTs = AdminEvents::householdEventFor({ { "type", alert }, { "data", { { "id", "a1" }, { "ts", "<script>" } } } }, now);
        EXPECT_EQ(now, str(noTs, "ts"));
    }

    for (const Json &dropped: { Json{ { "type", "upgrade_job" }, { "data", FakeDaemon::leakyJob() } },
                                Json{ { "type", "push_code" }, { "data", { { "approver_id", "a" }, { "code_id", "c" } } } },
                                Json{ { "type", "something_new" }, { "data", Json::object() } },
                                Json{ { "type", "upgrade_progress" }, { "data", "upgrading" } },
                                Json{ { "type", "upgrade_progress" } }, Json{ { "data", Json::object() } },
                                Json::array(), Json() })
        EXPECT_TRUE(AdminEvents::householdEventFor(dropped, now).is_null()) << dropped.dump();
}

TEST(JsonApiAdminRules, EventsReconnectWithAGrowingDelay)
{
    AdminEvents::Timings t;
    EXPECT_EQ(1.0, AdminEvents::nextBackoff(0.0, t));
    EXPECT_EQ(2.0, AdminEvents::nextBackoff(1.0, t));
    EXPECT_EQ(60.0, AdminEvents::nextBackoff(40.0, t));
    EXPECT_EQ(60.0, AdminEvents::nextBackoff(60.0, t));
    EXPECT_LT(t.ping, 30.0) << "the daemon drops a peer silent for 60 s";
    EXPECT_GT(t.silence, 60.0) << "the daemon pings every 30 s";
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
    EXPECT_TRUE(fakeDaemon.requests().empty()) << requestPaths();

    ::setenv("CALAOS_CONTAINER_URL", env.base.c_str(), 1);
    DaemonClient::Instance().configure();
    EXPECT_EQ(env.base, DaemonClient::Instance().baseUrl());
}

TEST_F(JsonApiAdminTest, ASilentDaemonTimesOut)
{
    adminDefined();
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/system/info")
            return false;
        res.delayMs = 8000;
        res.body = FakeDaemon::ok(Json());
        return true;
    });

    WsClient ws;
    ASSERT_TRUE(ws.login("https"));
    const auto start = steady_clock::now();
    expectError(ws.admin("admin", { { "action", "system_info" }, { "admin_session", FakeDaemon::Session } }, 7000),
                "daemon_unreachable");
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

TEST_F(JsonApiAdminTest, RotatingForwardedForCannotBypassTheAdminThrottle)
{
    adminDefined();

    //A host network process on the loopback, a new X-Forwarded-For per attempt
    auto unlock = [](const std::string &xff)
    {
        DirectHttp h("127.0.0.1", "https", xff);
        return h.send(Json{ { "action", "admin_unlock" }, { "cn_user", kUser }, { "cn_pass", kPass }, { "admin_pass", "wrong password" } }
                          .dump());
    };

    int badPassword = 0, rateLimited = 0;
    for (int i = 0; i < 3 * LoginThrottle::MaxForwardedIdentities; i++)
    {
        const std::string r = str(unlock("10.0.0." + std::to_string(i)), "error");
        if (r == "bad_password")
            badPassword++;
        else if (r == "rate_limited")
            rateLimited++;
    }
    EXPECT_EQ(LoginThrottle::MaxForwardedIdentities + 1, badPassword);
    EXPECT_EQ(2 * LoginThrottle::MaxForwardedIdentities - 1, rateLimited);
    EXPECT_EQ((size_t)LoginThrottle::MaxForwardedIdentities + 1, fakeDaemon.count("/api/v2/admin/verify"));

    //The household login has its own cap: a new name still logs in
    DirectHttp household("127.0.0.1", "https", "198.51.100.7");
    const Json home = household.send(Json{ { "action", "get_home" }, { "cn_user", kUser }, { "cn_pass", kPass } }.dump());
    EXPECT_TRUE(home.is_object() && home.contains("home")) << home.dump();
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

    EXPECT_TRUE(fakeDaemon.requests().empty()) << requestPaths();
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
    EXPECT_TRUE(fakeDaemon.requests().empty()) << "a missing session must never reach the daemon\n"
                                               << requestPaths();

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
    EXPECT_TRUE(fakeDaemon.requests().empty()) << requestPaths();
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
    EXPECT_TRUE(fakeDaemon.requests().empty()) << requestPaths();
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
                "invalid_request");
    expectError(ws.admin("admin", { { "action", "update_check" } }), "bad_session");
    expectError(ws.admin("admin", { { "action", "system_reboot" }, { "admin_session", "" } }), "bad_session");
    EXPECT_TRUE(fakeDaemon.requests().empty()) << requestPaths();

    //update_status passes the gate without a session
    const Json status = ws.admin("admin", { { "action", "update_status" } });
    EXPECT_EQ("true", str(status, "success")) << status.dump();

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
    EXPECT_TRUE(fakeDaemon.requests().empty()) << requestPaths();

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

TEST_F(JsonApiAdminTest, AnAlertIsStoredOncePerIdAndPushed)
{
    HistLogger::Instance().registerPushToken("cb2-push-token", HistLogger::PUSH_HW_ANDROID);
    drainLoop(200);

    EXPECT_FALSE(AdminState::Instance().storeAlert("upgrade_progress", "0000000000000001"));

    bool stored = false;
    EXPECT_TRUE(AdminState::Instance().storeAlert("admin_set", "00000000000000a1", [&stored]()
                                                  {
        stored = true;
    }));
    ASSERT_TRUE(pumpUntil([&stored]()
                          {
        return stored;
    }));
    bool sent = false;
    AdminState::Instance().pushAlert("admin_set", [&sent](bool ok)
                                     {
        sent = ok;
    });
    ASSERT_TRUE(pumpUntil([&sent]()
                          {
        return sent;
    }));
    const std::string push = fakeDaemon.last("/api/push").body;
    EXPECT_NE(std::string::npos, push.find("cb2-push-token"));
    EXPECT_NE(std::string::npos, push.find("Admin password set"));

    EXPECT_FALSE(AdminState::Instance().storeAlert("admin_set", "00000000000000a1")) << "a replay is stored again";
    EXPECT_TRUE(AdminState::Instance().storeAlert("admin_reset", "00000000000000a2"));
    drainLoop(200);

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

    //The ids survive a restart, the oldest leave after enough newer ones
    AdminState::Instance().loadAlerts();
    EXPECT_FALSE(AdminState::Instance().storeAlert("admin_reset", "00000000000000a2"));
    for (size_t i = 0; i < AdminState::MaxStoredAlerts; i++)
        AdminState::Instance().storeAlert("admin_set", "10000000000000" + std::to_string(10 + i));
    AdminState::Instance().loadAlerts();
    EXPECT_TRUE(AdminState::Instance().storeAlert("admin_set", "00000000000000a1"));
    drainLoop(300);

    //What a damaged file holds is not taken for ids
    {
        std::ofstream f(env.cache + "/" + AdminState::AlertsFileName, std::ios::trunc);
        f << "not-an-id\n"
          << std::string(65, 'a') << "\n\n00000000000000b9\n";
    }
    AdminState::Instance().loadAlerts();
    EXPECT_FALSE(AdminState::Instance().storeAlert("admin_set", "00000000000000b9"));
    EXPECT_TRUE(AdminState::Instance().storeAlert("admin_set", "00000000000000a1"));
    drainLoop(300);

    //A longer list keeps its newest ids only
    {
        std::ofstream f(env.cache + "/" + AdminState::AlertsFileName, std::ios::trunc);
        for (int i = 0; i < 70; i++)
            f << "20000000000000" << (10 + i) << "\n";
    }
    AdminState::Instance().loadAlerts();
    EXPECT_FALSE(AdminState::Instance().storeAlert("admin_set", "2000000000000079"));
    EXPECT_TRUE(AdminState::Instance().storeAlert("admin_set", "2000000000000010"));
    drainLoop(300);
}

TEST_F(JsonApiAdminTest, AnAlertListThatCannotBeWrittenIsReported)
{
    const std::string path = env.cache + "/" + AdminState::AlertsFileName + ".tmp";
    ::mkdir(path.c_str(), 0700);
    PushTest::StdoutCapture capture;
    EXPECT_TRUE(AdminState::Instance().storeAlert("admin_set", "00000000000000c9"));
    const std::string out = capture.stop();
    ::rmdir(path.c_str());
    EXPECT_NE(std::string::npos, out.find("an alert replayed after a restart is stored again")) << out;
    drainLoop(200);
}

/*******************************************************************************
 * The relay table
 ******************************************************************************/

namespace
{

//The push notifications of an earlier test may still be on their way to the fake
std::vector<FakeDaemon::Request> daemonCalls()
{
    std::vector<FakeDaemon::Request> out;
    for (const FakeDaemon::Request &r: fakeDaemon.requests())
        if (r.path != "/api/push")
            out.push_back(r);
    return out;
}

} //namespace

TEST_F(JsonApiAdminTest, EveryRelayedActionCallsItsFixedRoute)
{
    adminDefined();
    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    struct Row
    {
        const char *action;
        const char *method;
        const char *path;
    };
    const Row rows[] = {
        { "update_status", "GET", "/api/v2/update/status" },
        { "update_check", "GET", "/api/v2/update/check" },
        { "update_available", "GET", "/api/v2/update/available" },
        { "update_upgrade_all", "POST", "/api/v2/update/upgrade-all" },
        { "update_upgrade", "POST", "/api/v2/update/upgrade" },
        { "system_reboot", "POST", "/api/v2/system/reboot" },
        { "system_restart", "POST", "/api/v2/system/restart" },
        { "system_info", "GET", "/api/v2/system/info" },
    };

    std::map<std::string, Json> results;
    for (const Row &row: rows)
    {
        fakeDaemon.clearLog();
        //Nothing but params.package is taken from the client
        const Json data = { { "action", row.action },
                            { "admin_session", FakeDaemon::Session },
                            { "path", "system/install/start" },
                            { "params", { { "package", "calaos-server" }, { "recovery", true }, { "device", "/dev/sda" } } } };
        const Json answer = ws.admin("admin", data);
        ASSERT_EQ("true", str(answer, "success")) << row.action << " " << answer.dump();
        results[row.action] = answer["result"];

        const auto log = daemonCalls();
        ASSERT_EQ(1u, log.size()) << row.action;
        FakeDaemon::Request req = log[0];
        EXPECT_EQ(row.method, req.method) << row.action;
        EXPECT_EQ(row.path, req.path) << row.action;
        EXPECT_EQ("Bearer " + std::string(kBearer), req.headers["authorization"]);
        EXPECT_EQ(FakeDaemon::Session, req.headers["x-calaos-admin-session"]) << row.action;
        EXPECT_EQ("127.0.0.1", req.headers["x-calaos-client-ip"]) << row.action;

        if (std::string(row.action) == "update_upgrade")
            EXPECT_EQ((Json{ { "package", "calaos-server" } }), parse(req.body));
        else if (std::string(row.method) == "POST")
            EXPECT_EQ("{}", req.body) << row.action;
        else
            EXPECT_TRUE(req.body.empty()) << row.action;
    }

    //result is the daemon's data, with its JSON types
    EXPECT_EQ(FakeDaemon::leakyProgress(), results["update_status"]["progress"]) << "a session opens the full view";
    EXPECT_EQ(FakeDaemon::leakyJob(), results["update_status"]["job"]);
    EXPECT_EQ("4.6.0", str(results["update_check"]["calaos-server"], "version"));
    EXPECT_EQ("upgrade_all", str(results["update_upgrade_all"], "kind"));
    EXPECT_EQ("calaos-server", str(results["update_upgrade"], "package"));
    EXPECT_TRUE(results["update_upgrade"]["reboot_required"].is_boolean());
    EXPECT_TRUE(results["system_reboot"].is_null());
    EXPECT_TRUE(results["system_restart"].is_null());
    EXPECT_EQ(1234, results["system_info"]["uptime"].get<int>());
}

TEST_F(JsonApiAdminTest, UpdateStatusWithoutASessionIsTheHouseholdView)
{
    adminDefined();
    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    for (const Json &data: { Json{ { "action", "update_status" } },
                             Json{ { "action", "update_status" }, { "admin_session", "" } },
                             Json{ { "action", "update_status" }, { "admin_session", "not-a-session" } } })
    {
        fakeDaemon.clearLog();
        const Json answer = ws.admin("admin", data);
        ASSERT_EQ("true", str(answer, "success")) << answer.dump();
        EXPECT_EQ(householdProgress(), answer["result"]["progress"]);
        EXPECT_EQ("upgrade failed", str(answer["result"]["job"], "error"));
        EXPECT_FALSE(answer["result"]["job"].contains("package"));
        expectNothingLeaks(answer);
        EXPECT_EQ(0u, fakeDaemon.last("/api/v2/update/status").headers.count("x-calaos-admin-session"));
    }

    fakeDaemon.setUpdateStatus({ { "status", "idle" }, { "total_percent", 0 } }, nullptr);
    const Json idle = ws.admin("admin", { { "action", "update_status" } });
    EXPECT_EQ((Json{ { "status", "idle" }, { "total_percent", 0 } }), idle["result"]["progress"]);
    EXPECT_TRUE(idle["result"]["job"].is_null());

    //A session the daemon refuses falls back to the household view
    fakeDaemon.setUpdateStatus(FakeDaemon::leakyProgress(), FakeDaemon::leakyJob());
    fakeDaemon.clearLog();
    const Json stale = ws.admin("admin", { { "action", "update_status" }, { "admin_session", std::string(64, 'f') } });
    ASSERT_EQ("true", str(stale, "success")) << stale.dump();
    EXPECT_EQ(householdProgress(), stale["result"]["progress"]);
    expectNothingLeaks(stale);
    const auto calls = daemonCalls();
    ASSERT_EQ(2u, calls.size());
    EXPECT_EQ(std::string(64, 'f'), calls[0].headers.at("x-calaos-admin-session"));
    EXPECT_EQ(0u, calls[1].headers.count("x-calaos-admin-session"));
}

TEST_F(JsonApiAdminTest, AnInvalidPackageNeverReachesTheDaemon)
{
    adminDefined();
    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    for (const std::string &bad: { std::string("-y"), std::string("--allow-downgrades"), std::string(".hidden"),
                                   std::string("+evil"), std::string("foo=1.0"), std::string("foo/stable"),
                                   std::string("foo:amd64"), std::string("a..b"), std::string(129, 'a'),
                                   std::string(), std::string("calaos-server --purge") })
    {
        const Json answer = ws.admin("admin", { { "action", "update_upgrade" },
                                                { "admin_session", FakeDaemon::Session },
                                                { "params", { { "package", bad } } } });
        expectError(answer, "invalid_request");
    }

    expectError(ws.admin("admin", { { "action", "update_upgrade" }, { "admin_session", FakeDaemon::Session } }),
                "invalid_request");
    expectError(ws.admin("admin", { { "action", "update_upgrade" }, { "admin_session", FakeDaemon::Session }, { "params", { { "package", 3 } } } }),
                "invalid_request");
    //The request is checked before the session
    expectError(ws.admin("admin", { { "action", "update_upgrade" }, { "params", { { "package", "-y" } } } }),
                "invalid_request");
    EXPECT_TRUE(daemonCalls().empty());

    //A valid name without a pending update is the daemon's answer, its msg shown as is
    const Json missing = ws.admin("admin", { { "action", "update_upgrade" },
                                             { "admin_session", FakeDaemon::Session },
                                             { "params", { { "package", "linux-image-arm64" } } } });
    expectError(missing, "package_not_found");
    EXPECT_EQ("daemon says package_not_found", str(missing, "msg"));
    //A package on hold: no job, the daemon's own code
    const Json held = ws.admin("admin", { { "action", "update_upgrade" },
                                          { "admin_session", FakeDaemon::Session },
                                          { "params", { { "package", "calaos-held" } } } });
    expectError(held, "package_held");
    EXPECT_EQ("Package is held on the box (apt-mark hold)", str(held, "msg"));
}

TEST_F(JsonApiAdminTest, RelayedErrorsAreTranslated)
{
    adminDefined();
    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/system/reboot")
            return false;
        res.status = 409;
        res.body = FakeDaemon::err("upgrade_in_progress", { { "job_id", "3f9c2a7e5b1d4c80" } });
        return true;
    });
    const Json busy = ws.admin("admin", { { "action", "system_reboot" }, { "admin_session", FakeDaemon::Session } });
    expectError(busy, "upgrade_in_progress");
    EXPECT_EQ("3f9c2a7e5b1d4c80", str(busy, "job_id"));

    expectError(ws.admin("admin", { { "action", "system_info" }, { "admin_session", std::string(64, 'e') } }),
                "bad_session");

    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/update/status")
            return false;
        res.status = 409;
        res.body = FakeDaemon::err("admin_not_set");
        return true;
    });
    expectError(ws.admin("admin", { { "action", "update_status" } }), "admin_not_set");
    EXPECT_EQ(AdminState::Mode::NotSet, AdminState::Instance().mode());

    fakeDaemon.setOverride(nullptr);
    adminDefined();
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/system/info")
            return false;
        res.status = 404;
        res.body = "404 page not found";
        return true;
    });
    expectError(ws.admin("admin", { { "action", "system_info" }, { "admin_session", FakeDaemon::Session } }),
                "daemon_too_old");
    EXPECT_EQ(AdminState::Mode::TooOld, AdminState::Instance().mode());
}

TEST_F(JsonApiAdminTest, ADeepAnswerFromTheDaemonIsAnInvalidAnswer)
{
    const std::string deep = std::string(100000, '[') + std::string(100000, ']');
    EXPECT_EQ(DaemonReply::Kind::Unreachable,
              DaemonReply::parse(200, "{\"error\":false,\"msg\":\"ok\",\"data\":" + deep + "}", false).kind);

    adminDefined();
    fakeDaemon.setOverride([deep](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/update/status")
            return false;
        res.body = "{\"error\":false,\"msg\":\"ok\",\"data\":" + deep + "}";
        return true;
    });
    WsClient ws;
    ASSERT_TRUE(ws.login("https"));
    expectError(ws.admin("admin", { { "action", "update_status" } }), "daemon_unreachable");
}

TEST_F(JsonApiAdminTest, UpdateCheckWaitsLongerThanTheDefault)
{
    adminDefined();
    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    //Longer than the 4 s default, shorter than update_check's own timeout
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        res.delayMs = 4500;
        if (req.path == "/api/v2/update/check" || req.path == "/api/v2/system/info")
            res.body = FakeDaemon::ok(Json::object());
        else
            return false;
        return true;
    });

    EXPECT_EQ("true", str(ws.admin("admin", { { "action", "update_check" }, { "admin_session", FakeDaemon::Session } }, 9000),
                          "success"));
    expectError(ws.admin("admin", { { "action", "system_info" }, { "admin_session", FakeDaemon::Session } }, 9000),
                "daemon_unreachable");
}

TEST_F(JsonApiAdminTest, APersistentBearerRefusalIsLoggedOnce)
{
    adminDefined();
    fakeDaemon.setBearer("another-token");

    PushTest::StdoutCapture capture;
    for (int i = 0; i < 3; i++)
        syncAdminState();
    fakeDaemon.setBearer(kBearer);
    syncAdminState();
    fakeDaemon.setBearer("another-token");
    syncAdminState();
    const std::string out = capture.stop();

    int warnings = 0, rereads = 0;
    std::istringstream lines(out);
    std::string line;
    while (std::getline(lines, line))
    {
        if (line.find("still refuses the bearer") != std::string::npos)
            warnings++;
        if (line.find("reading the token again") != std::string::npos &&
            (line.find("[INF]") != std::string::npos || line.find("[WRN]") != std::string::npos))
            rereads++;
    }
    EXPECT_EQ(2, warnings) << "once per outage, not once per poll: " << out;
    EXPECT_EQ(0, rereads) << out;
    EXPECT_NE(std::string::npos, out.find("accepts the bearer again")) << out;
}

/*******************************************************************************
 * The /events client
 ******************************************************************************/

namespace
{

//A server frame: never masked unless the test wants a protocol error
std::string serverFrame(unsigned char first, const std::string &payload, bool masked = false);

//Every admin_event until last() accepts one, other messages skipped
std::vector<Json> adminEventsUntil(WsClient &ws, const std::function<bool(const Json &)> &last, int ms = 6000)
{
    std::vector<Json> out;
    const auto deadline = steady_clock::now() + milliseconds(ms);
    while (steady_clock::now() < deadline)
    {
        const Json f = ws.next(50);
        if (str(f, "msg") != "admin_event")
            continue;
        out.push_back(f);
        if (last(f))
            break;
    }
    return out;
}

bool isAdminEvent(const Json &f, const char *type)
{
    return str(f, "msg") == "admin_event" && f.contains("data") && str(f["data"], "type") == type;
}

class AdminEventsTest: public JsonApiAdminTest
{
protected:
    void SetUp() override
    {
        JsonApiAdminTest::SetUp();
        AdminEvents::Instance().setTimings(timings());
        AdminState::Instance().resetAlertPushes(0.0);
    }

    static AdminEvents::Timings timings()
    {
        AdminEvents::Timings t;
        t.ping = 0.1;
        t.silence = 0.8;
        t.minBackoff = 0.05;
        t.maxBackoff = 0.3;
        return t;
    }

    void TearDown() override
    {
        AdminEvents::Instance().stop();
        JsonApiAdminTest::TearDown();
    }

    //Waits out the pushes still in flight from earlier tests
    void quietPushes()
    {
        size_t before = fakeDaemon.count("/api/push");
        for (int i = 0; i < 25; i++)
        {
            drainLoop(400);
            const size_t now = fakeDaemon.count("/api/push");
            if (now == before)
                return;
            before = now;
        }
    }

    size_t pushesOf(const char *text)
    {
        size_t n = 0;
        for (const FakeDaemon::Request &r: fakeDaemon.requests())
            if (r.path == "/api/push" && r.body.find(text) != std::string::npos)
                n++;
        return n;
    }

    void startEvents()
    {
        AdminEvents::Instance().start();
        ASSERT_TRUE(pumpUntil([]()
                              {
            return AdminEvents::Instance().connected();
        }));
    }

    size_t acks() { return fakeDaemon.count("/api/v2/admin/alerts/ack"); }

    size_t storedEvents(const char *text)
    {
        bool read = false;
        size_t n = 0;
        HistLogger::Instance().getEvents(0, 1000, [&](bool, string, const vector<HistEvent> &events, int, int)
                                         {
            for (const HistEvent &e: events)
                if (e.event_type == CalaosEvent::EventPushNotification && e.event_raw.find(text) != std::string::npos)
                    n++;
            read = true;
        });
        pumpUntil([&read]()
                  {
            return read;
        });
        return n;
    }
};

} //namespace

TEST_F(AdminEventsTest, TheSubscriptionUsesTheBearerHeader)
{
    startEvents();
    AdminEvents::Instance().start(); //already running: no second stream
    drainLoop(100);
    FakeDaemon::Request req = fakeDaemon.last("/api/v2/events");
    EXPECT_EQ("GET", req.method);
    EXPECT_EQ("Bearer " + std::string(kBearer), req.headers["authorization"]);
    EXPECT_EQ("websocket", req.headers["upgrade"]);
    EXPECT_EQ(1u, fakeDaemon.eventsAccepted());
}

TEST_F(AdminEventsTest, HouseholdClientsSeeOnlyTheFilteredEvents)
{
    WsClient household;
    ASSERT_TRUE(household.login("")); //the plain port gets admin_event too
    WsClient anonymous;
    ASSERT_TRUE(anonymous.open("https"));
    WsClient service;
    ASSERT_TRUE(service.open("https"));
    const Json s = service.ask(Json{ { "msg", "login_service" }, { "msg_id", "l" }, { "data", { { "token", kServiceToken } } } });
    ASSERT_EQ("true", str(s["data"], "success")) << s.dump();

    startEvents();

    //The snapshot of the subscription, filtered the same way
    const Json first = household.next();
    ASSERT_TRUE(isAdminEvent(first, "upgrade_progress")) << first.dump();
    EXPECT_FALSE(first.contains("msg_id"));
    EXPECT_EQ(householdProgress(), first["data"]["data"]);
    EXPECT_EQ(20u, str(first["data"], "ts").size()) << first.dump();
    expectNothingLeaks(first);

    fakeDaemon.pushEvent({ { "type", "upgrade_job" }, { "data", FakeDaemon::leakyJob() } });
    fakeDaemon.pushEvent({ { "type", "push_code" }, { "data", { { "approver_id", "a1" }, { "code_id", "c1" } } } });
    fakeDaemon.pushEvent({ { "type", "something_new" }, { "data", { { "x", "calaos-server" } } } });
    fakeDaemon.pushEvent({ { "type", "upgrade_progress" }, { "data", "calaos-server" } });
    fakeDaemon.pushEvent({ { "type", "admin_set" }, { "data", { { "id", "../not-an-id" } } } });
    fakeDaemon.pushEvent({ { "type", "admin_reset" }, { "data", Json::object() } });
    fakeDaemon.pushEvent({ { "type", "admin_reset" }, { "data", { { "id", std::string(65, 'a') } } } });
    fakeDaemon.pushEvent(nlohmann::json::array({ "calaos-server" }));
    Json done = FakeDaemon::leakyProgress();
    done["status"] = "done";
    fakeDaemon.pushEvent({ { "type", "upgrade_progress" }, { "data", done } });

    const auto events = adminEventsUntil(household, [](const Json &f)
                                         {
        return str(f["data"]["data"], "status") == "done";
    });
    ASSERT_EQ(1u, events.size());
    EXPECT_TRUE(isAdminEvent(events[0], "upgrade_progress"));
    expectNothingLeaks(events[0]);
    //Acked so that the daemon stops replaying them, never recorded nor relayed
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return acks() == 2;
    }));
    std::vector<std::string> acked;
    for (const FakeDaemon::Request &r: fakeDaemon.requests())
        if (r.path == "/api/v2/admin/alerts/ack")
            acked.push_back(parse(r.body)["ids"][0].get<std::string>());
    std::sort(acked.begin(), acked.end());
    EXPECT_EQ((std::vector<std::string>{ "../not-an-id", std::string(65, 'a') }), acked);

    EXPECT_TRUE(adminEventsUntil(
                    anonymous, [](const Json &)
                    {
        return true;
                    },
                    200)
                    .empty());
    EXPECT_TRUE(adminEventsUntil(
                    service, [](const Json &)
                    {
        return true;
                    },
                    200)
                    .empty());
}

TEST_F(AdminEventsTest, AnAlertIsRecordedRelayedAndAcknowledged)
{
    HistLogger::Instance().registerPushToken("cb3-push-token", HistLogger::PUSH_HW_ANDROID);
    quietPushes();
    adminDefined();

    WsClient household;
    ASSERT_TRUE(household.login(""));
    startEvents();
    ASSERT_TRUE(isAdminEvent(household.next(), "upgrade_progress"));
    const size_t polls = fakeDaemon.count("/api/v2/admin/status");

    fakeDaemon.raiseAlert("admin_reset", "00000000000000b1");
    const Json ev = household.next();
    ASSERT_TRUE(isAdminEvent(ev, "admin_reset")) << ev.dump();
    EXPECT_EQ((Json{ { "type", "admin_reset" }, { "ts", "2026-10-22T09:14:00Z" } }), ev["data"]) << "ts only, no id";

    ASSERT_TRUE(pumpUntil([&]()
                          {
        return fakeDaemon.pendingAlerts().empty() && pushesOf("Admin password removed") == 1;
    }));
    const FakeDaemon::Request ack = fakeDaemon.last("/api/v2/admin/alerts/ack");
    EXPECT_EQ("POST", ack.method);
    EXPECT_EQ((Json{ { "ids", { "00000000000000b1" } } }), parse(ack.body));
    EXPECT_EQ(0u, ack.headers.count("x-calaos-admin-session"));
    EXPECT_NE(std::string::npos, fakeDaemon.last("/api/push").body.find("cb3-push-token"));
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return fakeDaemon.count("/api/v2/admin/status") > polls;
    })) << "the alert refreshes AdminState";

    //Delivery is at least once: a second copy is acked again, never pushed again
    drainLoop(300);
    fakeDaemon.pushEvent(FakeDaemon::alertEvent("admin_reset", "00000000000000b1"));
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return acks() == 2;
    }));
    drainLoop(300);
    EXPECT_EQ(1u, pushesOf("Admin password removed"));
    EXPECT_TRUE(adminEventsUntil(
                    household, [](const Json &)
                    {
        return true;
                    },
                    200)
                    .empty());
}

TEST_F(AdminEventsTest, AConsoleOpeningIsStoredPushedToEveryPhoneAndAckedOnce)
{
    HistLogger::Instance().registerPushToken("co2-console-token", HistLogger::PUSH_HW_ANDROID);
    HistLogger::Instance().registerPushToken("co2-other-token", HistLogger::PUSH_HW_IOS);
    quietPushes();

    WsClient household;
    ASSERT_TRUE(household.login(""));
    startEvents();
    ASSERT_TRUE(isAdminEvent(household.next(), "upgrade_progress"));

    const char *text = "Calaos: console opened";
    const size_t storedBefore = storedEvents(text);
    fakeDaemon.raiseAlert("console_opened", "00000000000000d7", { { "session_audit_id", "1a2b3c4d" } });
    const Json ev = household.next();
    ASSERT_TRUE(isAdminEvent(ev, "console_opened")) << ev.dump();
    EXPECT_EQ((Json{ { "type", "console_opened" }, { "ts", "2026-10-22T09:14:00Z" } }), ev["data"]) << "ts only";

    ASSERT_TRUE(pumpUntil([&]()
                          {
        return fakeDaemon.pendingAlerts().empty() && pushesOf(text) == 1;
    }));
    EXPECT_EQ((Json{ { "ids", { "00000000000000d7" } } }), parse(fakeDaemon.last("/api/v2/admin/alerts/ack").body));
    std::string body;
    for (const FakeDaemon::Request &r: fakeDaemon.requests())
        if (r.path == "/api/push" && r.body.find(text) != std::string::npos)
            body = r.body;
    EXPECT_NE(std::string::npos, body.find("co2-console-token"));
    EXPECT_NE(std::string::npos, body.find("co2-other-token")) << "every phone";
    EXPECT_EQ(std::string::npos, body.find("1a2b3c4d")) << "the audit id stays with the daemon";
    EXPECT_EQ(storedBefore + 1, storedEvents(text)) << "the event shown at login, same text";

    //A replayed copy is acked again, never stored, pushed or relayed again
    const size_t acked = acks();
    fakeDaemon.pushEvent(FakeDaemon::alertEvent("console_opened", "00000000000000d7", { { "session_audit_id", "1a2b3c4d" } }));
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return acks() == acked + 1;
    }));
    drainLoop(300);
    EXPECT_EQ(1u, pushesOf(text));
    EXPECT_EQ(storedBefore + 1, storedEvents(text));
    for (const Json &f: adminEventsUntil(
             household, [](const Json &)
             {
        return false;
             },
             300))
        EXPECT_FALSE(isAdminEvent(f, "console_opened")) << "the household saw the alert twice";
    quietPushes();
}

TEST_F(AdminEventsTest, EveryConsoleOpeningIsPushedWithinTheCooldown)
{
    HistLogger::Instance().registerPushToken("co2-console-token", HistLogger::PUSH_HW_ANDROID);
    quietPushes();
    AdminState::Instance().resetAlertPushes(AdminState::AlertPushInterval);
    startEvents();

    const char *text = "Calaos: console opened";
    const size_t before = pushesOf(text);
    fakeDaemon.raiseAlert("console_opened", "00000000000000d8", { { "session_audit_id", "1a2b3c4d" } });
    fakeDaemon.raiseAlert("console_opened", "00000000000000d9", { { "session_audit_id", "5e6f7a8b" } });
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return fakeDaemon.pendingAlerts().empty() && pushesOf(text) == before + 2;
    }));
    drainLoop(300);
    EXPECT_EQ(before + 2, pushesOf(text)) << "an opening right after another still reaches the phones";
    quietPushes();
}

TEST_F(AdminEventsTest, AReplayAfterAReconnectPushesNothingTwice)
{
    HistLogger::Instance().registerPushToken("cb3-push-token", HistLogger::PUSH_HW_ANDROID);
    quietPushes();

    //The daemon does not take the ack: the alert stays pending and is replayed
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/admin/alerts/ack")
            return false;
        res.status = 500;
        res.body = FakeDaemon::err("internal");
        return true;
    });

    WsClient household;
    ASSERT_TRUE(household.login(""));
    startEvents();
    ASSERT_TRUE(isAdminEvent(household.next(), "upgrade_progress"));

    fakeDaemon.raiseAlert("admin_set", "00000000000000c1");
    ASSERT_TRUE(isAdminEvent(household.next(), "admin_set"));
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return acks() == 1 && pushesOf("Admin password set") == 1;
    }));
    drainLoop(300);

    //A copy on the same stream, as after the daemon's subscribe race
    fakeDaemon.pushEvent(FakeDaemon::alertEvent("admin_set", "00000000000000c1"));
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return acks() == 2;
    }));
    drainLoop(200);

    //A daemon restart: the new stream replays the snapshot and the alert
    fakeDaemon.dropEvents();
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return fakeDaemon.eventsAccepted() == 2 && AdminEvents::Instance().connected() && acks() == 3;
    }));
    drainLoop(200);

    fakeDaemon.setOverride(nullptr);
    fakeDaemon.pushEvent(FakeDaemon::alertEvent("admin_set", "00000000000000c1"));
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return fakeDaemon.pendingAlerts().empty();
    }));
    drainLoop(300);

    EXPECT_EQ(1u, pushesOf("Admin password set")) << "one push for one alert";
    for (const Json &f: adminEventsUntil(
             household, [](const Json &)
             {
        return false;
             },
             300))
        EXPECT_FALSE(isAdminEvent(f, "admin_set")) << "the household saw the alert twice";
}

TEST_F(AdminEventsTest, TheStreamComesBackAfterTheDaemon)
{
    startEvents();

    fakeDaemon.dropEvents();
    ASSERT_TRUE(pumpUntil([]()
                          {
        return fakeDaemon.eventsAccepted() == 2 && AdminEvents::Instance().connected();
    }));

    fakeDaemon.stop();
    ASSERT_TRUE(pumpUntil([]()
                          {
        return !AdminEvents::Instance().connected();
    }));
    drainLoop(500);
    ASSERT_TRUE(fakeDaemon.start(env.daemonPort));
    ASSERT_TRUE(pumpUntil([]()
                          {
        return AdminEvents::Instance().connected();
    }));

    //Up for longer than the silence window: the outage is over
    PushTest::StdoutCapture capture;
    drainLoop(1300);
    const std::string out = capture.stop();
    EXPECT_TRUE(AdminEvents::Instance().connected());
    EXPECT_NE(std::string::npos, out.find("event stream again")) << out;
}

TEST_F(AdminEventsTest, AFlappingDaemonIsRetriedSlowerAndSlower)
{
    fakeDaemon.closeEventsAfterSnapshot(true);
    PushTest::StdoutCapture capture;
    AdminEvents::Instance().start();
    drainLoop(1500);
    const std::string out = capture.stop();

    //0.05 s doubled up to 0.3 s: about 7 streams, not one per millisecond
    EXPECT_GE(fakeDaemon.eventsAccepted(), 2u);
    EXPECT_LE(fakeDaemon.eventsAccepted(), 10u);
    size_t lost = 0;
    for (size_t at = out.find("Lost the calaos-container event stream"); at != std::string::npos;
         at = out.find("Lost the calaos-container event stream", at + 1))
        lost++;
    EXPECT_EQ(1u, lost) << out;
}

TEST_F(AdminEventsTest, ADeepEventIsDroppedWithoutHarm)
{
    WsClient household;
    ASSERT_TRUE(household.login(""));
    startEvents();
    ASSERT_TRUE(isAdminEvent(household.next(), "upgrade_progress"));

    //Under the message and frame caps, far over the nesting limit
    const std::string deep = std::string(100000, '[') + std::string(100000, ']');
    fakeDaemon.sendRaw(serverFrame(0x81, "{\"type\":\"upgrade_progress\",\"data\":" + deep + "}"));
    fakeDaemon.sendRaw(serverFrame(0x81, "{\"type\":\"admin_set\",\"data\":{\"id\":" + deep + "}}"));
    fakeDaemon.pushEvent({ { "type", "upgrade_progress" }, { "data", { { "status", "done" } } } });

    const auto events = adminEventsUntil(household, [](const Json &)
                                         {
        return true;
    });
    ASSERT_EQ(1u, events.size());
    EXPECT_EQ("done", str(events[0]["data"]["data"], "status"));
    EXPECT_TRUE(AdminEvents::Instance().connected());
    EXPECT_EQ(1u, fakeDaemon.eventsAccepted());
    EXPECT_EQ(0u, acks());
}

TEST_F(AdminEventsTest, AnAlertFloodSendsOnePushPerType)
{
    HistLogger::Instance().registerPushToken("cb3-push-token", HistLogger::PUSH_HW_ANDROID);
    quietPushes();
    AdminState::Instance().resetAlertPushes(AdminState::AlertPushInterval);
    startEvents();

    //More than the 64 ids remembered
    for (int i = 0; i < 70; i++)
        fakeDaemon.raiseAlert("admin_set", "000000000000e" + std::to_string(100 + i));
    ASSERT_TRUE(pumpUntil([]()
                          {
        return fakeDaemon.pendingAlerts().empty();
    }));
    drainLoop(300);

    EXPECT_EQ(1u, pushesOf("Admin password set")) << "the first alert is pushed, the rest of the window is not";
    EXPECT_EQ(70u, acks());

    fakeDaemon.raiseAlert("admin_reset", "0000000000000f01");
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return pushesOf("Admin password removed") == 1;
    })) << "the cooldown is per type";
}

TEST_F(AdminEventsTest, AFailedPushIsRetriedByATimerWithoutASecondEvent)
{
    HistLogger::Instance().registerPushToken("cb3-push-token", HistLogger::PUSH_HW_ANDROID);
    quietPushes();
    AdminEvents::Timings t = timings();
    t.retryDelay = 2.0;
    AdminEvents::Instance().setTimings(t);

    WsClient household;
    ASSERT_TRUE(household.login(""));
    startEvents();
    ASSERT_TRUE(isAdminEvent(household.next(), "upgrade_progress"));

    //A first alert of the type goes through
    fakeDaemon.raiseAlert("admin_reset", "0000000000000b51");
    ASSERT_TRUE(isAdminEvent(household.next(), "admin_reset"));
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return acks() == 1;
    }));
    drainLoop(200);
    const size_t storedBefore = storedEvents("Admin password removed");

    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/push")
            return false;
        res.status = 503;
        res.body = "{}";
        return true;
    });

    fakeDaemon.raiseAlert("admin_reset", "0000000000000b52");
    ASSERT_TRUE(isAdminEvent(household.next(), "admin_reset"));
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return pushesOf("Admin password removed") == 2;
    }));
    //Acked once stored, the lost push does not hold it
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return acks() == 2 && fakeDaemon.pendingAlerts().empty();
    }));
    //Like the daemon, the stream does not bring the alert back
    fakeDaemon.replayAlerts();
    drainLoop(200);
    EXPECT_EQ(2u, pushesOf("Admin password removed"));

    fakeDaemon.setOverride(nullptr);
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return pushesOf("Admin password removed") == 3;
    })) << "the timer retries it";
    drainLoop(1500);
    EXPECT_EQ(3u, pushesOf("Admin password removed")) << "nothing after a push that went out";
    EXPECT_EQ(2u, acks());

    EXPECT_EQ(storedBefore + 1, storedEvents("Admin password removed")) << "one persistent event";
    EXPECT_TRUE(adminEventsUntil(
                    household, [](const Json &)
                    {
        return true;
                    },
                    200)
                    .empty())
        << "the household saw the alert once";
}

TEST_F(AdminEventsTest, LostPushRetriesBackOffAndStop)
{
    HistLogger::Instance().registerPushToken("cb3-push-token", HistLogger::PUSH_HW_ANDROID);
    quietPushes();
    AdminEvents::Timings t = timings();
    t.retryDelay = 0.1;
    t.maxRetries = 3;
    AdminEvents::Instance().setTimings(t);

    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/push")
            return false;
        res.status = 503;
        res.body = "{}";
        return true;
    });
    startEvents();

    fakeDaemon.raiseAlert("admin_reset", "0000000000000c53");
    //The push, then retries after 0.1, 0.2 and 0.4 s
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return pushesOf("Admin password removed") == 4;
    }));
    drainLoop(1500);
    EXPECT_EQ(4u, pushesOf("Admin password removed"));
    EXPECT_EQ(1u, acks());
    EXPECT_TRUE(fakeDaemon.pendingAlerts().empty()) << "acked once stored";
}

TEST_F(AdminEventsTest, RetriesAreBoundedInNumberOfAlerts)
{
    HistLogger::Instance().registerPushToken("cb3-push-token", HistLogger::PUSH_HW_ANDROID);
    quietPushes();
    //No retry fires until rearmRetries(), however slow the 70 first pushes
    //are, and no watchdog reconnect: a new stream would replay all 70 alerts
    AdminEvents::Timings t = timings();
    t.retryDelay = 3600.0;
    t.maxRetries = 1;
    t.ping = 5.0;
    t.silence = 60.0;
    AdminEvents::Instance().setTimings(t);
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/push")
            return false;
        res.status = 503;
        res.body = "{}";
        return true;
    });
    startEvents();

    //More lost pushes than retry slots: the extra ones are given up
    for (int i = 0; i < 70; i++)
        fakeDaemon.raiseAlert("admin_set", "000000000000f" + std::to_string(100 + i));
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return pushesOf("Admin password set") == 70 && acks() == 70;
    },
                          30000));
    drainLoop(300);
    EXPECT_EQ(64u, AdminEvents::Instance().retrySeries()) << "a full table refuses the 6 last series";

    AdminEvents::Instance().rearmRetries(0.05);
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return pushesOf("Admin password set") == 70 + 64 && AdminEvents::Instance().retrySeries() == 0;
    },
                          15000));
    drainLoop(500);
    EXPECT_EQ(70u + 64u, pushesOf("Admin password set")) << "64 alerts retried once, 6 refused";
    EXPECT_EQ(70u, acks());

    //Those that gave up free their slots
    t.retryDelay = 0.2;
    AdminEvents::Instance().setTimings(t);
    fakeDaemon.raiseAlert("admin_set", "000000000000f200");
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return pushesOf("Admin password set") == 70 + 64 + 2;
    }));
    //The last answer is still on its way
    drainLoop(300);
    EXPECT_EQ(1u, fakeDaemon.eventsAccepted());
}

TEST_F(AdminEventsTest, APushRetryNeedsNoStream)
{
    HistLogger::Instance().registerPushToken("cb3-push-token", HistLogger::PUSH_HW_ANDROID);
    quietPushes();
    AdminEvents::Timings t = timings();
    t.retryDelay = 0.5;
    AdminEvents::Instance().setTimings(t);
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/push")
            return false;
        res.status = 503;
        res.body = "{}";
        return true;
    });
    startEvents();

    fakeDaemon.raiseAlert("admin_reset", "0000000000000c57");
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return pushesOf("Admin password removed") == 1 && acks() == 1;
    }));

    //The alert is acked: no subscription will bring it back, the timer retries alone
    fakeDaemon.refuseEvents(404);
    fakeDaemon.dropEvents();
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return pushesOf("Admin password removed") == 2;
    }));
    EXPECT_FALSE(AdminEvents::Instance().connected());

    //stop() cuts the series
    AdminEvents::Instance().stop();
    drainLoop(1500);
    EXPECT_EQ(2u, pushesOf("Admin password removed"));
}

TEST_F(AdminEventsTest, RestartsWithAnUnreachablePushServiceStoreNothingTwice)
{
    HistLogger::Instance().registerPushToken("cb3-push-token", HistLogger::PUSH_HW_ANDROID);
    quietPushes();
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/push")
            return false;
        res.status = 503;
        res.body = "{}";
        return true;
    });
    const size_t setBefore = storedEvents("Admin password set");
    const size_t resetBefore = storedEvents("Admin password removed");

    WsClient household;
    ASSERT_TRUE(household.login(""));
    startEvents();
    ASSERT_TRUE(isAdminEvent(household.next(), "upgrade_progress"));

    //admin set, then admin reset
    fakeDaemon.raiseAlert("admin_set", "0000000000000f61");
    fakeDaemon.raiseAlert("admin_reset", "0000000000000f62");
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return acks() == 2 && fakeDaemon.pendingAlerts().empty();
    }));

    //Two calaos_server restarts: everything in memory is gone
    for (int restart = 0; restart < 2; restart++)
    {
        AdminEvents::Instance().stop();
        AdminState::Instance().loadAlerts();
        startEvents();
        drainLoop(300);
    }
    EXPECT_EQ(3u, fakeDaemon.eventsAccepted());
    EXPECT_EQ(2u, acks()) << "acked once";
    EXPECT_EQ(setBefore + 1, storedEvents("Admin password set"));
    EXPECT_EQ(resetBefore + 1, storedEvents("Admin password removed"));

    size_t alerts = 0;
    for (const Json &f: adminEventsUntil(
             household, [](const Json &)
             {
        return false;
             },
             500))
        if (isAdminEvent(f, "admin_set") || isAdminEvent(f, "admin_reset"))
            alerts++;
    EXPECT_EQ(2u, alerts) << "relayed once each";
}

TEST_F(AdminEventsTest, AnAlertReplayedAfterARestartIsNotStoredAgain)
{
    //The ack is lost and calaos_server restarts: the daemon replays the alert
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/admin/alerts/ack")
            return false;
        res.status = 500;
        res.body = FakeDaemon::err("internal");
        return true;
    });
    const size_t before = storedEvents("Admin password set");

    WsClient household;
    ASSERT_TRUE(household.login(""));
    startEvents();
    ASSERT_TRUE(isAdminEvent(household.next(), "upgrade_progress"));
    fakeDaemon.raiseAlert("admin_set", "0000000000000f63");
    ASSERT_TRUE(isAdminEvent(household.next(), "admin_set"));
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return acks() == 1;
    }));
    drainLoop(200);
    const size_t pushes = pushesOf("Admin password set");

    fakeDaemon.setOverride(nullptr);
    AdminEvents::Instance().stop();
    AdminState::Instance().loadAlerts();
    startEvents();
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return acks() == 2 && fakeDaemon.pendingAlerts().empty();
    }));
    drainLoop(300);

    EXPECT_EQ(before + 1, storedEvents("Admin password set"));
    EXPECT_EQ(pushes, pushesOf("Admin password set")) << "not pushed again";
    for (const Json &f: adminEventsUntil(
             household, [](const Json &)
             {
        return false;
             },
             300))
        EXPECT_FALSE(isAdminEvent(f, "admin_set")) << "relayed again after the restart";
}

TEST_F(AdminEventsTest, ARetriedPushKeepsTheCooldown)
{
    HistLogger::Instance().registerPushToken("cb3-push-token", HistLogger::PUSH_HW_ANDROID);
    quietPushes();
    AdminState::Instance().resetAlertPushes(AdminState::AlertPushInterval);
    AdminEvents::Timings t = timings();
    t.retryDelay = 3.0;
    AdminEvents::Instance().setTimings(t);

    //Only the first push is lost
    auto failures = std::make_shared<std::atomic<int>>(1);
    fakeDaemon.setOverride([failures](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/push" || failures->fetch_sub(1) <= 0)
            return false;
        res.status = 503;
        res.body = "{}";
        return true;
    });
    startEvents();

    fakeDaemon.raiseAlert("admin_set", "0000000000000d54");
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return pushesOf("Admin password set") == 1;
    }));
    drainLoop(100);
    fakeDaemon.raiseAlert("admin_set", "0000000000000d55");
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return pushesOf("Admin password set") == 2 && acks() == 2;
    }));

    //The retry finds a push sent within the window: not pushed
    PushTest::StdoutCapture capture;
    drainLoop(3500);
    const std::string out = capture.stop();
    EXPECT_NE(std::string::npos, out.find("one was sent less than 600 s ago")) << out;
    EXPECT_EQ(2u, pushesOf("Admin password set"));
}

TEST_F(AdminEventsTest, ALostAckIsRetriedByTheTimerToo)
{
    AdminEvents::Timings t = timings();
    t.retryDelay = 0.3;
    AdminEvents::Instance().setTimings(t);

    auto failures = std::make_shared<std::atomic<int>>(1);
    fakeDaemon.setOverride([failures](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/admin/alerts/ack" || failures->fetch_sub(1) <= 0)
            return false;
        res.status = 500;
        res.body = FakeDaemon::err("internal");
        return true;
    });
    startEvents();

    fakeDaemon.raiseAlert("admin_reset", "0000000000000e56");
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return acks() == 2 && fakeDaemon.pendingAlerts().empty();
    }));
    drainLoop(800);
    EXPECT_EQ(2u, acks());
}

TEST_F(AdminEventsTest, ARefusedUpgradeIsRetriedAndARotatedTokenIsReadAgain)
{
    fakeDaemon.refuseEvents(404);
    AdminEvents::Instance().start();
    ASSERT_TRUE(pumpUntil([]()
                          {
        return fakeDaemon.count("/api/v2/events") >= 2;
    }));
    EXPECT_FALSE(AdminEvents::Instance().connected());

    fakeDaemon.refuseEvents(0);
    ASSERT_TRUE(pumpUntil([]()
                          {
        return AdminEvents::Instance().connected();
    }));

    fakeDaemon.setBearer("rotated-token");
    writeToken("rotated-token");
    fakeDaemon.dropEvents();
    ASSERT_TRUE(pumpUntil([]()
                          {
        return fakeDaemon.eventsAccepted() == 2 && AdminEvents::Instance().connected();
    }));
    EXPECT_EQ("Bearer rotated-token", fakeDaemon.last("/api/v2/events").headers["authorization"]);

    //A token nobody can read: refused after the re-read, retried with a delay
    fakeDaemon.setBearer("nobody-has-it");
    const size_t before = fakeDaemon.count("/api/v2/events");
    fakeDaemon.dropEvents();
    ASSERT_TRUE(pumpUntil([before]()
                          {
        return fakeDaemon.count("/api/v2/events") >= before + 3;
    }));
    EXPECT_FALSE(AdminEvents::Instance().connected());

    fakeDaemon.setBearer("rotated-token");
    ASSERT_TRUE(pumpUntil([]()
                          {
        return AdminEvents::Instance().connected();
    }));
}

TEST_F(AdminEventsTest, PingsGoBothWaysAndASilentDaemonIsLeft)
{
    startEvents();
    ASSERT_TRUE(pumpUntil([]()
                          {
        return fakeDaemon.pingsReceived() >= 2;
    }));

    fakeDaemon.pingEvents();
    ASSERT_TRUE(pumpUntil([]()
                          {
        return fakeDaemon.pongsReceived() >= 1;
    }));
    EXPECT_EQ(1u, fakeDaemon.eventsAccepted());

    fakeDaemon.muteEvents(true);
    ASSERT_TRUE(pumpUntil([]()
                          {
        return fakeDaemon.eventsAccepted() >= 2;
    }));
}

namespace
{

std::string serverFrame(unsigned char first, const std::string &payload, bool masked)
{
    std::string f;
    f.push_back((char)first);
    const unsigned char maskBit = masked ? 0x80 : 0x00;
    if (payload.size() < 126)
    {
        f.push_back((char)(maskBit | payload.size()));
    }
    else if (payload.size() <= 0xffff)
    {
        f.push_back((char)(maskBit | 126));
        f.push_back((char)((payload.size() >> 8) & 0xff));
        f.push_back((char)(payload.size() & 0xff));
    }
    else
    {
        f.push_back((char)(maskBit | 127));
        for (int i = 7; i >= 0; i--)
            f.push_back((char)((payload.size() >> (8 * i)) & 0xff));
    }
    if (masked)
        f += std::string("\x01\x02\x03\x04", 4);
    f += payload; //a wrong unmasking does not matter, the frame is refused
    return f;
}

} //namespace

TEST_F(AdminEventsTest, ABadHandshakeIsLeftAndRetried)
{
    const std::string wrongAccept = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                                    "a line without a colon\r\n"
                                    "Connection: Upgrade\r\nSec-WebSocket-Accept: d3Jvbmc=\r\n\r\n";
    const std::string notWebsocket = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: h2c\r\n\r\n";
    for (const std::string &raw: { wrongAccept, notWebsocket, std::string(20000, 'x'), std::string() })
    {
        fakeDaemon.rawHandshake(true, raw);
        const size_t before = fakeDaemon.count("/api/v2/events");
        AdminEvents::Instance().start();
        ASSERT_TRUE(pumpUntil([before]()
                              {
            return fakeDaemon.count("/api/v2/events") >= before + 2;
        })) << raw.size();
        EXPECT_FALSE(AdminEvents::Instance().connected());
        AdminEvents::Instance().stop();
    }

    fakeDaemon.rawHandshake(false);
    fakeDaemon.refuseEvents(500);
    AdminEvents::Instance().start();
    ASSERT_TRUE(pumpUntil([]()
                          {
        return fakeDaemon.count("/api/v2/events") >= 2;
    }));
    EXPECT_FALSE(AdminEvents::Instance().connected());

    fakeDaemon.refuseEvents(0);
    ASSERT_TRUE(pumpUntil([]()
                          {
        return AdminEvents::Instance().connected();
    }));
}

TEST_F(AdminEventsTest, NoDaemonUrlMeansNoSubscriptionUntilThereIsOne)
{
    ::setenv("CALAOS_CONTAINER_URL", "http://192.168.1.2:8000", 1);
    DaemonClient::Instance().configure();
    AdminEvents::Instance().start();
    drainLoop(100);
    EXPECT_FALSE(AdminEvents::Instance().connected());
    EXPECT_EQ(0u, fakeDaemon.count("/api/v2/events"));

    ::setenv("CALAOS_CONTAINER_URL", env.base.c_str(), 1);
    DaemonClient::Instance().configure();
    ASSERT_TRUE(pumpUntil([]()
                          {
        return AdminEvents::Instance().connected();
    }));
}

TEST_F(AdminEventsTest, AnIpv6DaemonUrlIsDialledOverIpv6)
{
    //Nothing listens on [::1] here: the attempt fails and is retried, like any other
    ::setenv("CALAOS_CONTAINER_URL", ("http://[::1]:" + std::to_string(env.daemonPort)).c_str(), 1);
    DaemonClient::Instance().configure();
    AdminEvents::Instance().start();
    drainLoop(400);
    EXPECT_FALSE(AdminEvents::Instance().connected());
    EXPECT_EQ(0u, fakeDaemon.count("/api/v2/events"));

    AdminEvents::Instance().stop();
    ::setenv("CALAOS_CONTAINER_URL", env.base.c_str(), 1);
    DaemonClient::Instance().configure();
}

TEST_F(AdminEventsTest, AnAckInFlightIsNotSentTwice)
{
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/admin/alerts/ack")
            return false;
        res.delayMs = 600;
        res.body = FakeDaemon::ok({ { "acked", 1 } });
        return true;
    });
    startEvents();

    fakeDaemon.raiseAlert("admin_set", "00000000000000d1");
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return acks() == 1;
    }));
    fakeDaemon.pushEvent(FakeDaemon::alertEvent("admin_set", "00000000000000d1"));
    drainLoop(300);
    EXPECT_EQ(1u, acks());
    drainLoop(500);
}

TEST_F(AdminEventsTest, FramesTheDaemonMaySendAreUnderstood)
{
    //No ping: its pong would land inside the frames written byte by byte below
    AdminEvents::Timings slow;
    AdminEvents::Instance().setTimings(slow);

    WsClient household;
    ASSERT_TRUE(household.login(""));
    startEvents();
    ASSERT_TRUE(isAdminEvent(household.next(), "upgrade_progress"));

    //Ignored: a binary message, an empty text one, an unsolicited pong
    fakeDaemon.sendRaw(serverFrame(0x82, "{\"type\":\"upgrade_progress\",\"data\":{\"status\":\"error\"}}"));
    fakeDaemon.sendRaw(serverFrame(0x81, std::string()));
    fakeDaemon.sendRaw(serverFrame(0x8A, std::string()));

    //A fragmented message, its continuation arriving byte by byte
    const std::string text = Json{ { "type", "upgrade_progress" }, { "data", { { "status", "done" }, { "total_percent", 100 } } } }.dump();
    fakeDaemon.sendRaw(serverFrame(0x01, text.substr(0, 10)));
    const std::string rest = serverFrame(0x80, text.substr(10));
    for (char c: rest)
    {
        fakeDaemon.sendRaw(std::string(1, c));
        drainLoop(2);
    }

    const auto events = adminEventsUntil(household, [](const Json &)
                                         {
        return true;
    });
    ASSERT_EQ(1u, events.size());
    EXPECT_EQ((Json{ { "status", "done" }, { "total_percent", 100 } }), events[0]["data"]["data"]);
    EXPECT_TRUE(AdminEvents::Instance().connected());

    //A close frame from the daemon: the stream is left, then subscribed again
    fakeDaemon.sendRaw(serverFrame(0x88, std::string("\x03\xe9", 2)));
    ASSERT_TRUE(pumpUntil([]()
                          {
        return fakeDaemon.eventsAccepted() == 2 && AdminEvents::Instance().connected();
    }));
}

TEST_F(AdminEventsTest, ProtocolErrorsDropTheStream)
{
    startEvents();

    const std::string big(700 * 1024, 'x');
    const std::vector<std::vector<std::string>> errors = {
        { serverFrame(0x81, "{}", true) },                  //masked by the server
        { serverFrame(0x80, "{}") },                        //a continuation of nothing
        { serverFrame(0x01, "{"), serverFrame(0x81, "}") }, //a new message inside one
        { serverFrame(0xC1, "{}") },                        //RSV bit
        { serverFrame(0x01, big), serverFrame(0x80, big) }, //over MaxMessageSize
    };

    size_t accepted = 1;
    for (const auto &frames: errors)
    {
        for (const std::string &f: frames)
            fakeDaemon.sendRaw(f);
        accepted++;
        ASSERT_TRUE(pumpUntil([accepted]()
                              {
            return fakeDaemon.eventsAccepted() == accepted && AdminEvents::Instance().connected();
        })) << accepted;
    }
}

/*******************************************************************************
 * Approvers and push codes
 ******************************************************************************/

namespace
{

std::vector<PushToken> pushTokens()
{
    bool done = false;
    std::vector<PushToken> out;
    HistLogger::Instance().getPushTokens([&](bool, string, const vector<PushToken> &tokens)
                                         {
        out = tokens;
        done = true;
    });
    pumpUntil([&done]()
              {
        return done;
    });
    return out;
}

const PushToken *tokenNamed(const std::vector<PushToken> &tokens, const std::string &token)
{
    for (const PushToken &t: tokens)
        if (t.token == token)
            return &t;
    return nullptr;
}

const char *const kCode = "c000000000000001";
const char *const kApprover = "a000000000000001";

} //namespace

TEST(JsonApiAdminRules, DeviceNamesAreCutToPrintableText)
{
    EXPECT_EQ("Pixel 8", JsonApi::printableText("Pixel 8", 64));
    EXPECT_EQ("Pixel 8", JsonApi::printableText("  Pixel\t\n   8\r\n ", 64));
    EXPECT_EQ("Téléphone de Léa", JsonApi::printableText("Téléphone de Léa", 64));
    //Bidi overrides and isolates, zero-width, BOM, soft hyphen, tags, C1
    EXPECT_EQ("Pixelevil", JsonApi::printableText("Pixel\xe2\x80\xae\xe2\x81\xa6\xe2\x80\x8b\xef\xbb\xbf\xc2\xad\U000E0041\u0085evil", 64));
    EXPECT_EQ("ab", JsonApi::printableText("a\xe2\x80\xa8"
                                           "b\x7f",
                                           64));
    //Invalid UTF-8: a lone continuation, an overlong '/', a surrogate, a cut sequence
    EXPECT_EQ("ab", JsonApi::printableText("a\x80\xc0\xaf\xed\xa0\x80"
                                           "b\xe2\x82",
                                           64));
    EXPECT_EQ("", JsonApi::printableText("\x01\x02 \xe2\x80\x8f", 64));
    //Fillers and blanks that render as nothing, variation selectors, the Arabic letter mark
    EXPECT_EQ("", JsonApi::printableText("\xe3\x85\xa4\xe1\x85\x9f\xe2\xa0\x80\xef\xb8\x8f\xd8\x9c\xef\xbe\xa0", 64));
    EXPECT_EQ("Pixel 8", JsonApi::printableText("Pixel 8\xe3\x85\xa4\xef\xb8\x8f\xf3\xa0\x84\x80", 64));
    //Every Zs space is a plain one
    EXPECT_EQ("", JsonApi::printableText("\xc2\xa0\xe2\x80\x83\xe3\x80\x80", 64));
    EXPECT_EQ("Pixel 8", JsonApi::printableText("\xc2\xa0Pixel\xc2\xa0\xe2\x80\xaf 8\xe2\x80\x83", 64));

    std::string accents;
    for (int i = 0; i < 70; i++)
        accents += "é";
    const std::string cut = JsonApi::printableText(accents, JsonApi::MaxDeviceName);
    EXPECT_EQ(128u, cut.size()) << "64 code points, never half of one";
    EXPECT_EQ(64u, JsonApi::printableText(std::string(300, 'x'), 64).size());
    EXPECT_EQ(64u, JsonApi::MaxDeviceName);
}

TEST(JsonApiAdminRules, TokenIdsAreTheDaemons)
{
    //First 16 hex characters of SHA-256("abc")
    EXPECT_EQ("ba7816bf8f01cfea", JsonApi::pushTokenId("abc"));
    EXPECT_EQ(16u, JsonApi::pushTokenId("").size());
}

TEST(JsonApiAdminRules, ApproverAlertsNeverStartWithTheName)
{
    const Json data = { { "device_name", "\"Calaos\": your box is hacked, call +33 1\xe2\x80\xae" }, { "token_id", "9a41f0c2d3e4b5a6" } };
    EXPECT_EQ("New approver phone on your Calaos box: \"'Calaos': your box is hacked, call +33 1\"",
              AdminState::alertMessage("approver_set", data));
    EXPECT_EQ("Approver phone removed from your Calaos box: \"Pixel 8\"",
              AdminState::alertMessage("approver_removed", { { "device_name", "Pixel 8" } }));
    EXPECT_EQ("New approver phone on your Calaos box: \"?\"", AdminState::alertMessage("approver_set", Json()));
    //No quote of the name closes the one around it
    EXPECT_EQ("New approver phone on your Calaos box: \"Pixel 8'. Admin password removed. ' ' ' ' ' ' ' ' '\"",
              AdminState::alertMessage("approver_set", { { "device_name", "Pixel 8\u201d. Admin password removed. \u201c \u00ab \u00bb \u201e \u201f \u2033 \u2036 \uff02 \"" } }));
    EXPECT_EQ("Admin password set on your Calaos box", AdminState::alertMessage("admin_set", data));
    EXPECT_EQ("Calaos: console opened", AdminState::alertMessage("console_opened", { { "session_audit_id", "1a2b3c4d" }, { "device_name", "Pixel 8" } }));
    EXPECT_TRUE(AdminState::alertMessage("push_code", data).empty());
}

TEST_F(JsonApiAdminTest, RegisterPushStoresASanitizedDeviceName)
{
    WsClient ws;
    ASSERT_TRUE(ws.login(""));
    ws.post(Json{ { "msg", "register_push" }, { "data", { { "token", "ap1b-name-token" }, { "hardware", "android" }, { "device_name", "  Pixel\xe2\x80\xae\n 8  " } } } });
    const Json http = httpJson(Json{ { "action", "register_push" }, { "cn_user", kUser }, { "cn_pass", kPass }, { "token", "ap1b-name-ios" }, { "hardware", "ios" }, { "device_name", std::string(100, 'n') } }, "");
    EXPECT_EQ("true", str(http, "success")) << http.dump();

    ASSERT_TRUE(pumpUntil([]()
                          {
        return tokenNamed(pushTokens(), "ap1b-name-token") && tokenNamed(pushTokens(), "ap1b-name-ios");
    }));
    auto tokens = pushTokens();
    EXPECT_EQ("Pixel 8", tokenNamed(tokens, "ap1b-name-token")->device_name);
    EXPECT_EQ(std::string(64, 'n'), tokenNamed(tokens, "ap1b-name-ios")->device_name);
    EXPECT_EQ(HistLogger::PUSH_HW_IOS, tokenNamed(tokens, "ap1b-name-ios")->hw_type);

    //An app that sends no name keeps the one it registered
    ws.post(Json{ { "msg", "register_push" }, { "data", { { "token", "ap1b-name-token" }, { "hardware", "android" } } } });
    drainLoop(100);
    EXPECT_EQ("Pixel 8", tokenNamed(pushTokens(), "ap1b-name-token")->device_name);
}

TEST_F(JsonApiAdminTest, ApproverCandidatesNeverCarryAToken)
{
    HistLogger::Instance().registerPushToken("ap1b-candidate-a", HistLogger::PUSH_HW_ANDROID, HistLogger::PUSH_FORMAT_DATA, "Pixel 8");
    HistLogger::Instance().registerPushToken("ap1b-candidate-b", HistLogger::PUSH_HW_IOS);
    adminDefined();
    fakeDaemon.setApprovers(Json::array({ { { "id", kApprover }, { "device_name", "Pixel 8" }, { "token_id", JsonApi::pushTokenId("ap1b-candidate-a") }, { "created_at", "2026-10-23T09:12:03Z" } } }));

    WsClient ws;
    ASSERT_TRUE(ws.login("https"));
    const Json answer = ws.admin("admin", { { "action", "approver_candidates" }, { "admin_session", FakeDaemon::Session } });
    ASSERT_EQ("true", str(answer, "success")) << answer.dump();
    for (const PushToken &t: pushTokens())
        EXPECT_EQ(std::string::npos, answer.dump().find(t.token)) << t.token << " in " << answer.dump();

    const FakeDaemon::Request req = fakeDaemon.last("/api/v2/admin/approvers");
    EXPECT_EQ("GET", req.method);
    EXPECT_EQ(FakeDaemon::Session, req.headers.at("x-calaos-admin-session"));

    std::map<std::string, Json> byId;
    for (const Json &c: answer["result"])
    {
        EXPECT_EQ((std::set<std::string>{ "token_id", "device_name", "platform", "registered_at", "last_seen", "approver" }), [&c]()
                  {
            std::set<std::string> keys;
            for (auto it = c.begin(); it != c.end(); ++it)
                keys.insert(it.key());
            return keys;
        }()) << c.dump();
        byId[str(c, "token_id")] = c;
    }
    const Json a = byId[JsonApi::pushTokenId("ap1b-candidate-a")];
    const Json b = byId[JsonApi::pushTokenId("ap1b-candidate-b")];
    EXPECT_EQ("Pixel 8", str(a, "device_name"));
    EXPECT_TRUE(a["approver"].get<bool>());
    EXPECT_EQ("iOS device", str(b, "device_name")) << "an app that sent no name";
    EXPECT_FALSE(b["approver"].get<bool>());
    EXPECT_EQ(20u, str(a, "last_seen").size());
    EXPECT_EQ('T', str(a, "last_seen")[10]);
    EXPECT_EQ(20u, str(a, "registered_at").size());
    EXPECT_EQ("android", str(a, "platform"));
    EXPECT_EQ("ios", str(b, "platform"));

    //The daemon validates the session first
    expectError(ws.admin("admin", { { "action", "approver_candidates" }, { "admin_session", std::string(64, 'e') } }), "bad_session");
}

TEST_F(JsonApiAdminTest, TheApproverListTellsPhonesOfTheSameNameApart)
{
    HistLogger::Instance().registerPushToken("ap1b-list-ios", HistLogger::PUSH_HW_IOS, HistLogger::PUSH_FORMAT_NOTIFICATION, "iPhone");
    HistLogger::Instance().registerPushToken("ap1b-list-android", HistLogger::PUSH_HW_ANDROID, HistLogger::PUSH_FORMAT_DATA, "iPhone");
    adminDefined();
    const Json daemonList = Json::array({ { { "id", "a000000000000011" }, { "device_name", "iPhone" }, { "token_id", JsonApi::pushTokenId("ap1b-list-ios") }, { "created_at", "2026-10-23T09:12:03Z" } },
                                          { { "id", "a000000000000012" }, { "device_name", "iPhone" }, { "token_id", JsonApi::pushTokenId("ap1b-list-android") }, { "created_at", "2026-10-23T09:13:03Z" } },
                                          { { "id", "a000000000000013" }, { "device_name", "\u200bGone\u202e now" }, { "token_id", "00000000000000ff" }, { "created_at", "2026-10-23T09:14:03Z" }, { "future_field", "kept by the daemon" } } });
    fakeDaemon.setApprovers(daemonList);

    WsClient ws;
    ASSERT_TRUE(ws.login("https"));
    const Json answer = ws.admin("admin", { { "action", "approver_list" }, { "admin_session", FakeDaemon::Session } });
    ASSERT_EQ("true", str(answer, "success")) << answer.dump();
    for (const PushToken &t: pushTokens())
        EXPECT_EQ(std::string::npos, answer.dump().find(t.token)) << t.token;

    const Json &list = answer["result"];
    ASSERT_EQ(3u, list.size()) << answer.dump();
    for (size_t i = 0; i < 3; i++)
        EXPECT_EQ(str(daemonList[i], "id"), str(list[i], "id")) << "the daemon's order and fields";
    EXPECT_EQ("ios", str(list[0], "platform"));
    EXPECT_EQ("android", str(list[1], "platform"));
    EXPECT_EQ(20u, str(list[0], "registered_at").size());
    EXPECT_EQ(20u, str(list[1], "last_seen").size());
    EXPECT_EQ("2026-10-23T09:12:03Z", str(list[0], "created_at"));
    //Its token left push_tokens: nothing to tell, and no code will reach it
    EXPECT_EQ("", str(list[2], "platform"));
    EXPECT_EQ("", str(list[2], "last_seen"));
    //Only the fields known here, the name cleaned as register_push does
    EXPECT_FALSE(list[2].contains("future_field")) << list[2].dump();
    EXPECT_EQ(7u, list[2].size()) << list[2].dump();
    EXPECT_EQ("Gone now", str(list[2], "device_name"));
}

TEST_F(JsonApiAdminTest, EnrolmentResolvesTheTokenItselfAndIgnoresTheClients)
{
    HistLogger::Instance().registerPushToken("ap1b-enrol-token", HistLogger::PUSH_HW_ANDROID, HistLogger::PUSH_FORMAT_DATA, "Phone of Lea");
    HistLogger::Instance().registerPushToken("ap1b-enrol-noname", HistLogger::PUSH_HW_ANDROID);
    adminDefined();
    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    const Json data = { { "action", "approver_enroll" },
                        { "admin_session", FakeDaemon::Session },
                        { "token", "attacker-token" },
                        { "device_name", "Attacker" },
                        { "params", { { "token_id", JsonApi::pushTokenId("ap1b-enrol-token") }, { "token", "attacker-token" }, { "device_name", "Attacker" } } } };
    const Json answer = ws.admin("admin", data);
    ASSERT_EQ("true", str(answer, "success")) << answer.dump();
    EXPECT_EQ("c000000000000001", str(answer["result"], "code_id"));
    EXPECT_EQ(std::string::npos, answer.dump().find("ap1b-enrol-token"));

    FakeDaemon::Request req = fakeDaemon.last("/api/v2/admin/approvers/enroll");
    EXPECT_EQ("POST", req.method);
    EXPECT_EQ(FakeDaemon::Session, req.headers.at("x-calaos-admin-session"));
    EXPECT_EQ((Json{ { "token", "ap1b-enrol-token" }, { "device_name", "Phone of Lea" } }), parse(req.body));

    //With the approval code of an existing approver; a nameless app gets its platform
    Json approved = data;
    approved["params"] = { { "token_id", JsonApi::pushTokenId("ap1b-enrol-noname") }, { "code_id", kCode }, { "code", "123456" } };
    ASSERT_EQ("true", str(ws.admin("admin", approved), "success"));
    EXPECT_EQ((Json{ { "token", "ap1b-enrol-noname" }, { "device_name", "Android device" }, { "code_id", kCode }, { "code", "123456" } }),
              parse(fakeDaemon.last("/api/v2/admin/approvers/enroll").body));

    fakeDaemon.clearLog();
    Json unknown = data;
    unknown["params"] = { { "token_id", JsonApi::pushTokenId("attacker-token") } };
    const Json refused = ws.admin("admin", unknown);
    expectError(refused, "invalid_request");
    EXPECT_EQ("Unknown token_id", str(refused, "msg"));
    unknown["params"] = { { "token", "attacker-token" } };
    expectError(ws.admin("admin", unknown), "invalid_request");
    EXPECT_TRUE(daemonCalls().empty());
}

TEST_F(JsonApiAdminTest, ApproverAndCodeActionsCallTheirFixedRoutes)
{
    adminDefined();
    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    struct Row
    {
        const char *action;
        Json params;
        const char *method;
        const char *path;
        Json body; //null: none
    };
    const Json code = { { "code_id", kCode }, { "code", "123456" } };
    const std::vector<Row> rows = {
        { "approver_list", Json::object(), "GET", "/api/v2/admin/approvers", Json() },
        { "approver_confirm", { { "code_id", kCode }, { "code", "123456" }, { "token", "x" } }, "POST", "/api/v2/admin/approvers/confirm", code },
        { "approver_remove", { { "approver_id", kApprover }, { "code_id", kCode }, { "code", "123456" } }, "POST", "/api/v2/admin/approvers/remove", { { "approver_id", kApprover }, { "code_id", kCode }, { "code", "123456" } } },
        { "code_request", { { "code_action", "console_open" }, { "code_params", { { "password", "smuggled" } } } }, "POST", "/api/v2/admin/codes", { { "action", "console_open" }, { "params", Json::object() } } },
        { "code_request", { { "code_action", "admin_password" }, { "code_params", { { "password", "a new admin password" }, { "recovery", true } } }, { "approver_id", kApprover } }, "POST", "/api/v2/admin/codes", { { "action", "admin_password" }, { "params", { { "password", "a new admin password" } } }, { "approver_id", kApprover } } },
        { "code_request", { { "code_action", "approver_remove" }, { "code_params", { { "approver_id", kApprover } } } }, "POST", "/api/v2/admin/codes", { { "action", "approver_remove" }, { "params", { { "approver_id", kApprover } } } } },
        { "code_verify", code, "POST", "/api/v2/admin/codes/verify", code },
        { "console_open", code, "POST", "/api/v2/admin/codes/verify", code },
    };

    for (const Row &row: rows)
    {
        fakeDaemon.clearLog();
        const Json answer = ws.admin("admin", { { "action", row.action }, { "admin_session", FakeDaemon::Session }, { "path", "admin/reset" }, { "params", row.params } });
        ASSERT_EQ("true", str(answer, "success")) << row.action << " " << answer.dump();

        const auto log = daemonCalls();
        ASSERT_EQ(1u, log.size()) << row.action;
        EXPECT_EQ(row.method, log[0].method) << row.action;
        EXPECT_EQ(row.path, log[0].path) << row.action;
        EXPECT_EQ(FakeDaemon::Session, log[0].headers.at("x-calaos-admin-session")) << row.action;
        EXPECT_EQ("127.0.0.1", log[0].headers.at("x-calaos-client-ip")) << row.action;
        if (row.body.is_null())
        {
            EXPECT_TRUE(log[0].body.empty()) << row.action;
        }
        else
        {
            EXPECT_EQ(row.body, parse(log[0].body)) << row.action;
        }
    }

    const Json grant = ws.admin("admin", { { "action", "console_open" }, { "admin_session", FakeDaemon::Session }, { "params", code } });
    EXPECT_EQ((Json{ { "action", "console_open" }, { "expires_in", 30 } }), grant["result"]);

    //A session-only route is never called without the header
    fakeDaemon.clearLog();
    expectError(ws.admin("admin", { { "action", "approver_list" } }), "bad_session");
    EXPECT_TRUE(daemonCalls().empty());
}

TEST_F(JsonApiAdminTest, TheDaemonsCodeErrorsAreRelayed)
{
    adminDefined();
    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    struct Case
    {
        int status;
        const char *code;
        Json data;
        const char *field;
        const char *value;
    };
    for (const Case &c: std::vector<Case>{ { 403, "bad_code", { { "attempts_left", 3 } }, "attempts_left", "3" },
                                           { 409, "approver_limit", { { "max", 2 } }, "max", "2" },
                                           { 410, "code_expired", Json(), nullptr, nullptr },
                                           { 409, "approver_required", Json(), nullptr, nullptr },
                                           { 403, "code_required", Json(), nullptr, nullptr },
                                           { 409, "approver_exists", Json(), nullptr, nullptr },
                                           { 404, "approver_not_found", Json(), nullptr, nullptr },
                                           { 409, "approver_last", Json(), nullptr, nullptr } })
    {
        fakeDaemon.setOverride([c](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                               {
            if (req.path != "/api/v2/admin/approvers/remove")
                return false;
            res.status = c.status;
            res.body = FakeDaemon::err(c.code, c.data);
            return true;
        });
        const Json answer = ws.admin("admin", { { "action", "approver_remove" }, { "admin_session", FakeDaemon::Session }, { "params", { { "approver_id", kApprover }, { "code_id", kCode }, { "code", "123456" } } } });
        expectError(answer, c.code);
        EXPECT_NE("daemon says " + std::string(c.code), str(answer, "msg"));
        if (c.field)
        {
            EXPECT_EQ(c.value, str(answer, c.field)) << answer.dump();
        }
    }
}

TEST_F(JsonApiAdminTest, APasswordChangeCarriesItsPushCode)
{
    adminDefined();
    WsClient ws;
    ASSERT_TRUE(ws.login("https"));

    const Json change = ws.admin("admin_set", { { "new_pass", "another long password" }, { "admin_session", FakeDaemon::Session }, { "code_id", kCode }, { "code", "654321" }, { "token", "x" } });
    EXPECT_EQ("change", str(change, "mode")) << change.dump();
    EXPECT_EQ((Json{ { "password", "another long password" }, { "code_id", kCode }, { "code", "654321" } }),
              parse(fakeDaemon.last("/api/v2/admin/set").body));

    fakeDaemon.clearLog();
    expectError(ws.admin("admin_set", { { "new_pass", "another long password" }, { "admin_session", FakeDaemon::Session }, { "code_id", kCode } }),
                "invalid_request");
    expectError(ws.admin("admin_set", { { "new_pass", "another long password" }, { "admin_session", FakeDaemon::Session }, { "code_id", kCode }, { "code", "65432" } }),
                "invalid_request");
    EXPECT_TRUE(daemonCalls().empty());

    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/admin/set")
            return false;
        res.status = 409;
        res.body = FakeDaemon::err("approver_required");
        return true;
    });
    expectError(ws.admin("admin_set", { { "new_pass", "another long password" }, { "admin_session", FakeDaemon::Session } }), "approver_required");
    EXPECT_EQ(Json({ { "password", "another long password" } }), parse(fakeDaemon.last("/api/v2/admin/set").body));

    //A bootstrap has no code to carry
    fakeDaemon.setOverride(nullptr);
    fakeDaemon.setAdminSet(false);
    syncAdminState();
    const Json boot = ws.admin("admin_set", { { "new_pass", "a long admin password" }, { "code_id", "x" } });
    EXPECT_EQ("bootstrap", str(boot, "mode")) << boot.dump();
    EXPECT_EQ(Json({ { "password", "a long admin password" } }), parse(fakeDaemon.last("/api/v2/admin/set").body));
}

TEST_F(AdminEventsTest, APushCodeGoesToItsTokenOnlyAndNeverToAClient)
{
    HistLogger::Instance().registerPushToken("ap1b-approver-token", HistLogger::PUSH_HW_ANDROID, HistLogger::PUSH_FORMAT_DATA, "Approver");
    HistLogger::Instance().registerPushToken("ap1b-other-token", HistLogger::PUSH_HW_IOS);
    quietPushes();

    WsClient household;
    ASSERT_TRUE(household.login(""));
    startEvents();
    ASSERT_TRUE(isAdminEvent(household.next(), "upgrade_progress"));

    PushTest::StdoutCapture capture;
    fakeDaemon.setPushPayload(kCode, "ap1b-approver-token", "Calaos admin code 482913: open the web console. Valid 60 s, never share it.");
    fakeDaemon.pushEvent({ { "type", "push_code" }, { "data", { { "approver_id", kApprover }, { "code_id", kCode } } } });
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return pushesOf("482913") == 1;
    }));
    drainLoop(200);
    const std::string log = capture.stop();

    const FakeDaemon::Request fetch = fakeDaemon.last(std::string("/api/v2/admin/push/") + kCode);
    EXPECT_EQ("GET", fetch.method);
    EXPECT_EQ("Bearer " + std::string(kBearer), fetch.headers.at("authorization"));
    EXPECT_EQ(0u, fetch.headers.count("x-calaos-admin-session"));

    std::string body;
    for (const FakeDaemon::Request &r: fakeDaemon.requests())
        if (r.path == "/api/push" && r.body.find("482913") != std::string::npos)
            body = r.body;
    EXPECT_NE(std::string::npos, body.find("ap1b-approver-token"));
    EXPECT_EQ(std::string::npos, body.find("ap1b-other-token")) << "one token only";
    EXPECT_EQ(1u, pushesOf("482913"));

    for (const char *secret: { "482913", "ap1b-approver-token" })
        EXPECT_EQ(std::string::npos, log.find(secret)) << secret << " in " << log;

    EXPECT_TRUE(adminEventsUntil(
                    household, [](const Json &)
                    {
        return true;
                    },
                    300)
                    .empty())
        << "push_code is never forwarded";
    quietPushes();
}

TEST_F(AdminEventsTest, APushCodeThatCannotBeServedIsDropped)
{
    HistLogger::Instance().registerPushToken("ap1b-approver-token", HistLogger::PUSH_HW_ANDROID);
    quietPushes();
    startEvents();
    const size_t pushes = fakeDaemon.count("/api/push");

    auto send = [](const std::string &codeId)
    {
        fakeDaemon.pushEvent({ { "type", "push_code" }, { "data", { { "approver_id", kApprover }, { "code_id", codeId } } } });
    };
    auto fetched = [](const std::string &codeId)
    {
        return pumpUntil([codeId]()
                         {
            return fakeDaemon.count("/api/v2/admin/push/" + codeId) == 1;
        });
    };

    send("../alerts/ack000");
    send("C000000000000001");
    //Unknown to the daemon, a payload without a usable token or text, a token
    //this server does not know
    send("c0000000000000a1");
    fakeDaemon.setPushPayload("c0000000000000a2", "", "Calaos admin code 111111: x");
    send("c0000000000000a2");
    fakeDaemon.setPushPayload("c0000000000000a3", "two words", "Calaos admin code 222222: x");
    send("c0000000000000a3");
    fakeDaemon.setPushPayload("c0000000000000a4", "ap1b-approver-token", "\x01\xe2\x80\xae");
    send("c0000000000000a4");
    fakeDaemon.setPushPayload("c0000000000000a5", "ap1b-unknown-token", "Calaos admin code 333333: x");
    send("c0000000000000a5");
    //Only the daemon's own wording goes out, whatever phone it names
    const std::vector<std::pair<const char *, const char *>> wordings = {
        { "c0000000000000b6", "Your Calaos box was reset, call 0800 000 000" },
        { "c0000000000000b7", "Calaos admin code 44444: x" },
        { "c0000000000000b8", "Calaos admin code 4444444: x" },
        { "c0000000000000b9", "Calaos admin code 44444a: x" },
        { "c0000000000000ba", "Calaos admin code 444444" },
    };
    for (const auto &w: wordings)
    {
        fakeDaemon.setPushPayload(w.first, "ap1b-approver-token", w.second);
        send(w.first);
    }
    for (const auto &w: wordings)
        EXPECT_TRUE(fetched(w.first)) << w.second;
    for (const char *id: { "c0000000000000a1", "c0000000000000a2", "c0000000000000a3", "c0000000000000a4", "c0000000000000a5" })
        EXPECT_TRUE(fetched(id)) << id;
    drainLoop(300);

    //By their texts: a push of an earlier test may still land meanwhile
    for (const char *text: { "111111", "222222", "333333", "Calaos admin code 4444", "call 0800 000 000" })
        EXPECT_EQ(0u, pushesOf(text)) << text;
    EXPECT_GE(fakeDaemon.count("/api/push"), pushes);
    for (const FakeDaemon::Request &r: fakeDaemon.requests())
        EXPECT_EQ(std::string::npos, r.path.find("alerts/ack000")) << r.path;
    EXPECT_EQ(0u, fakeDaemon.count("/api/v2/admin/push/C000000000000001"));
}

TEST_F(AdminEventsTest, PushCodesAreCappedPerHour)
{
    HistLogger::Instance().registerPushToken("ap1b-approver-token", HistLogger::PUSH_HW_ANDROID);
    quietPushes();
    AdminEvents::Timings t = timings();
    t.maxCodePushes = 2;
    AdminEvents::Instance().setTimings(t);
    startEvents();

    auto send = [](const char *id)
    {
        fakeDaemon.pushEvent({ { "type", "push_code" }, { "data", { { "approver_id", kApprover }, { "code_id", id } } } });
    };

    //Fetches that serve nothing, and phones this server does not know, cost nothing
    fakeDaemon.setPushPayload("c0000000000000c3", "ap1b-unknown-token", "Calaos admin code 300003: x");
    for (const char *id: { "c0000000000000c1", "c0000000000000c2", "c0000000000000c3" })
    {
        send(id);
        ASSERT_TRUE(pumpUntil([id]()
                              {
            return fakeDaemon.count(std::string("/api/v2/admin/push/") + id) == 1;
        })) << id;
    }
    drainLoop(200);

    for (const auto &c: std::vector<std::pair<const char *, const char *>>{ { "c0000000000000d1", "100001" },
                                                                            { "c0000000000000d2", "100002" },
                                                                            { "c0000000000000d3", "100003" } })
    {
        fakeDaemon.setPushPayload(c.first, "ap1b-approver-token", std::string("Calaos admin code ") + c.second + ": x");
        send(c.first);
        const std::string code = c.second;
        pumpUntil([&]()
                  {
            return pushesOf(code.c_str()) == 1;
        },
                  1500);
        //The push counts once the push service has answered, which a coverage
        //build on a busy runner takes its time to see
        drainLoop(1500);
    }
    EXPECT_EQ(1u, pushesOf("100001"));
    EXPECT_EQ(1u, pushesOf("100002"));
    EXPECT_EQ(0u, pushesOf("100003"));
    EXPECT_EQ(0u, fakeDaemon.count("/api/v2/admin/push/c0000000000000d3")) << "not even fetched";
}

TEST_F(AdminEventsTest, AnApproverAlertIsPushedToEveryPhoneBehindAFixedPrefix)
{
    HistLogger::Instance().registerPushToken("ap1b-approver-token", HistLogger::PUSH_HW_ANDROID);
    HistLogger::Instance().registerPushToken("ap1b-other-token", HistLogger::PUSH_HW_IOS);
    quietPushes();
    adminDefined();

    WsClient household;
    ASSERT_TRUE(household.login(""));
    startEvents();
    ASSERT_TRUE(isAdminEvent(household.next(), "upgrade_progress"));

    const Json phone = { { "device_name", "Your box is hacked, call 0800" }, { "token_id", "9a41f0c2d3e4b5a6" } };
    fakeDaemon.raiseAlert("approver_set", "00000000000000e1", phone);
    const Json ev = household.next();
    ASSERT_TRUE(isAdminEvent(ev, "approver_set")) << ev.dump();
    EXPECT_EQ((Json{ { "type", "approver_set" }, { "ts", "2026-10-22T09:14:00Z" } }), ev["data"]) << "ts only";

    const char *text = "New approver phone on your Calaos box: \\\"Your box is hacked, call 0800\\\"";
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return fakeDaemon.pendingAlerts().empty() && pushesOf(text) == 1;
    }));
    std::string body;
    for (const FakeDaemon::Request &r: fakeDaemon.requests())
        if (r.path == "/api/push" && r.body.find(text) != std::string::npos)
            body = r.body;
    EXPECT_NE(std::string::npos, body.find("ap1b-approver-token"));
    EXPECT_NE(std::string::npos, body.find("ap1b-other-token")) << "every phone";
    EXPECT_EQ(1u, storedEvents(text)) << "the event shown at login, same text";

    fakeDaemon.raiseAlert("approver_removed", "00000000000000e2", { { "device_name", "Pixel 8" } });
    ASSERT_TRUE(isAdminEvent(household.next(), "approver_removed"));
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return pushesOf("Approver phone removed from your Calaos box: \\\"Pixel 8\\\"") == 1;
    }));
    EXPECT_EQ(1u, storedEvents("Approver phone removed from your Calaos box: \\\"Pixel 8\\\""));
    //No push still in flight when the program exits
    quietPushes();
}

TEST_F(JsonApiAdminTest, EachApproverPhoneHasItsOwnAlertCooldown)
{
    HistLogger::Instance().registerPushToken("ap1b-cooldown-token", HistLogger::PUSH_HW_ANDROID);
    pushTokens();
    AdminState::Instance().resetAlertPushes(AdminState::AlertPushInterval);

    auto push = [](const char *name, const char *tokenId)
    {
        bool done = false;
        AdminState::Instance().pushAlert(
            "approver_set", [&done](bool)
            {
            done = true;
            },
            { { "device_name", name }, { "token_id", tokenId } });
        pumpUntil([&done]()
                  {
            return done;
        });
    };
    auto pushed = [](const char *name)
    {
        size_t n = 0;
        for (const FakeDaemon::Request &r: fakeDaemon.requests())
            if (r.path == "/api/push" && r.body.find(name) != std::string::npos)
                n++;
        return n;
    };

    //A decoy phone first does not silence the next one; the same phone again is
    push("Decoy cooldown", "00000000000000d1");
    push("Real cooldown", "00000000000000d2");
    push("Again cooldown", "00000000000000d1");
    EXPECT_EQ(1u, pushed("Decoy cooldown"));
    EXPECT_EQ(1u, pushed("Real cooldown"));
    EXPECT_EQ(0u, pushed("Again cooldown"));
}

TEST_F(AdminEventsTest, AnApproversTokenKeepsItsEnrolledNameAndFormat)
{
    HistLogger::Instance().registerPushToken("ap1b-locked-token", HistLogger::PUSH_HW_ANDROID, HistLogger::PUSH_FORMAT_DATA, "Phone of Lea");
    quietPushes();
    startEvents();

    const std::string tokenId = JsonApi::pushTokenId("ap1b-locked-token");
    //The vector is kept: tokenNamed points into it
    auto approver = []()
    {
        const std::vector<PushToken> list = pushTokens();
        const PushToken *t = tokenNamed(list, "ap1b-locked-token");
        return t && t->approver;
    };
    fakeDaemon.raiseAlert("approver_set", "00000000000000f1", { { "device_name", "Phone of Lea" }, { "token_id", tokenId } });
    ASSERT_TRUE(pumpUntil(approver));

    //A household client that knows the token cannot rename it nor change its format
    WsClient household;
    ASSERT_TRUE(household.login(""));
    household.post(Json{ { "msg", "register_push" }, { "data", { { "token", "ap1b-locked-token" }, { "hardware", "android" }, { "device_name", "Attacker was here" } } } });
    drainLoop(200);
    const std::vector<PushToken> tokens = pushTokens();
    const PushToken *kept = tokenNamed(tokens, "ap1b-locked-token");
    ASSERT_NE(nullptr, kept);
    EXPECT_EQ("Phone of Lea", kept->device_name);
    EXPECT_EQ(HistLogger::PUSH_FORMAT_DATA, kept->format);

    //Removed, it is an ordinary token again
    fakeDaemon.raiseAlert("approver_removed", "00000000000000f2", { { "device_name", "Phone of Lea" }, { "token_id", tokenId } });
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return !approver();
    }));
    household.post(Json{ { "msg", "register_push" }, { "data", { { "token", "ap1b-locked-token" }, { "hardware", "android" }, { "device_name", "Renamed" } } } });
    ASSERT_TRUE(pumpUntil([]()
                          {
        const std::vector<PushToken> list = pushTokens();
        const PushToken *t = tokenNamed(list, "ap1b-locked-token");
        return t && t->device_name == "Renamed";
    }));
    EXPECT_EQ(HistLogger::PUSH_FORMAT_NOTIFICATION, tokenNamed(pushTokens(), "ap1b-locked-token")->format);
    //The two alerts' pushes must not be in flight when the program exits
    quietPushes();
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

    AdminEvents::Instance().stop();
    AdminState::Instance().stop();
    McpServerManager::Instance().stop();
    fakeDaemon.stop();
    drainLoop(50);
    std::error_code ec;
    std::filesystem::remove_all(base, ec);

    //The singletons' destructors would write into the tree just removed and
    //close uv handles after the loop's users are gone: skip them, as the other
    //server tests do (the coverage shim still dumps the counters)
    fflush(nullptr);
    _exit(rc);
}
