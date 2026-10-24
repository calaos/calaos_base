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
 * /api/console (docs/08_http_api.md, Console) on a real HttpServer bound on
 * loopback, against a fake calaos-container (FakeDaemon, POST admin/console)
 * and a fake console socket (FakeConsole) in a temporary directory reached
 * through CALAOS_CONTAINER_CONSOLE_DIR. A connection from 127.0.0.1 with
 * X-Forwarded-Proto: https is what haproxy sends; the LAN peer is injected on
 * a real WebSocket.
 */

#include <gtest/gtest.h>

#include "Utils.h"
#include "AdminState.h"
#include "ConsoleTunnel.h"
#include "DaemonClient.h"
#include "HttpServer.h"
#include "McpServerManager.h"
#include "WebSocket.h"
#include "json.hpp"

#include "FakeConsole.h"
#include "FakeDaemon.h"

#include "libuvw.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>

using namespace Calaos;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

namespace
{

const char *const kUser = "co2-household";
const char *const kPass = "co2-household-password";
const char *const kServiceToken = "co2-service-token";
const char *const kBearer = "co2-bearer-token";
const char *const kCode = "c0de000000000001";

FakeDaemon fakeDaemon;
FakeConsole fakeConsole;

struct Env
{
    int serverPort = 0;
    int daemonPort = 0;
    std::string base;
    std::string cache;
    std::string tokenFile;
    std::string consoleDir;
} env;

std::string socketPath(const std::string &id = FakeDaemon::DefaultConsoleId)
{
    return env.consoleDir + "/console-" + id + ".sock";
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

std::string str(const Json &j, const char *key)
{
    if (!j.is_object() || !j.contains(key) || !j[key].is_string())
        return std::string();
    return j[key].get<std::string>();
}

//The status of an HTTP answer head, 0 when it is not one
int statusOf(const std::string &head)
{
    if (head.compare(0, 5, "HTTP/") != 0)
        return 0;
    const size_t sp = head.find(' ');
    return sp == std::string::npos ? 0 : std::atoi(head.c_str() + sp + 1);
}

std::string bytes(size_t n, unsigned seed)
{
    std::string s(n, '\0');
    for (size_t i = 0; i < n; i++)
        s[i] = (char)((i * 131 + seed) & 0xff);
    return s;
}

//The browser: a WebSocket client of /api/console on a real socket
class ConsoleClient
{
public:
    ~ConsoleClient()
    {
        closeNow();
        drainLoop(30);
    }

    //The head of the upgrade answer
    std::string connect(const std::string &xfp = "https", const std::string &origin = std::string(), bool protocol = true)
    {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        //A small window, so that a client that stops reading is felt soon
        int small = 16 * 1024;
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &small, sizeof(small));
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(env.serverPort);
        if (::connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0)
            return std::string();

        std::ostringstream req;
        req << "GET /api/console HTTP/1.1\r\n"
            << "Host: 127.0.0.1:" << env.serverPort << "\r\n"
            << "Connection: Upgrade\r\n"
            << "Upgrade: websocket\r\n"
            << "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\n"
            << "Sec-WebSocket-Version: 13\r\n";
        if (protocol)
            req << "Sec-WebSocket-Protocol: chat, calaos-console.v1\r\n";
        if (!xfp.empty())
            req << "X-Forwarded-Proto: " << xfp << "\r\n";
        if (!origin.empty())
            req << "Origin: " << origin << "\r\n";
        req << "\r\n";
        outq = req.str();

        pumpUntil([this]()
                  {
            pump();
            return headDone || tcpClosed;
        },
                  5000);
        return head;
    }

    bool upgraded() const { return head.compare(0, 12, "HTTP/1.1 101") == 0; }
    const std::string &answerHead() const { return head; }

    void sendFrame(int opcode, const std::string &payload, bool fin = true)
    {
        static const unsigned char mask[4] = { 0x12, 0x34, 0x56, 0x78 };
        std::string f;
        f.push_back((char)((fin ? 0x80 : 0) | opcode));
        if (payload.size() < 126)
        {
            f.push_back((char)(0x80 | payload.size()));
        }
        else if (payload.size() <= 0xffff)
        {
            f.push_back((char)(0x80 | 126));
            f.push_back((char)((payload.size() >> 8) & 0xff));
            f.push_back((char)(payload.size() & 0xff));
        }
        else
        {
            f.push_back((char)(0x80 | 127));
            for (int i = 7; i >= 0; i--)
                f.push_back((char)((payload.size() >> (8 * i)) & 0xff));
        }
        f.append((const char *)mask, 4);
        for (size_t i = 0; i < payload.size(); i++)
            f.push_back((char)(payload[i] ^ mask[i % 4]));
        outq += f;
        flush();
    }

    void sendText(const Json &j) { sendFrame(0x1, j.dump()); }
    void sendText(const std::string &s) { sendFrame(0x1, s); }
    void sendBinary(const std::string &s) { sendFrame(0x2, s); }

    void sendClose(uint16_t code)
    {
        std::string p;
        p.push_back((char)(code >> 8));
        p.push_back((char)(code & 0xff));
        sendFrame(0x8, p);
    }

    //Bytes the kernel did not take yet
    size_t unsent() const { return outq.size() - outOff; }

    //The next text message, null when none came in time
    Json nextText(int ms = 6000)
    {
        pumpUntil([this]()
                  {
            pump();
            return !texts.empty();
        },
                  ms);
        if (texts.empty())
            return Json();
        const std::string t = texts.front();
        texts.erase(texts.begin());
        return Json::parse(t, nullptr, false);
    }

    bool hasText()
    {
        pump();
        return !texts.empty();
    }

    const std::string &binary() const { return bin; }
    size_t binaryMessages() const { return binCount; }

    //The code of the server's close frame, 0 when none came in time
    int waitClose(int ms = 6000)
    {
        pumpUntil([this]()
                  {
            pump();
            return closeCode != 0;
        },
                  ms);
        return closeCode;
    }
    const std::string &closeReason() const { return reason; }
    bool disconnected() const { return tcpClosed; }

    void pause(bool p) { paused = p; }

    void pump()
    {
        flush();
        if (fd < 0 || paused)
            return;
        char buf[65536];
        while (true)
        {
            const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
            if (n == 0)
                tcpClosed = true;
            if (n <= 0)
                break;
            rx.append(buf, (size_t)n);
        }
        if (!headDone)
        {
            const size_t end = rx.find("\r\n\r\n");
            if (end == std::string::npos)
            {
                head = rx;
                return;
            }
            head = rx.substr(0, end + 4);
            rx.erase(0, end + 4);
            headDone = true;
        }
        parse();
    }

    void closeNow()
    {
        if (fd >= 0)
            close(fd);
        fd = -1;
    }

private:
    void flush()
    {
        while (fd >= 0 && outOff < outq.size())
        {
            const ssize_t n = ::send(fd, outq.data() + outOff, outq.size() - outOff, MSG_DONTWAIT | MSG_NOSIGNAL);
            if (n <= 0)
                break;
            outOff += (size_t)n;
        }
        if (outOff > 1024 * 1024 || outOff == outq.size())
        {
            outq.erase(0, outOff);
            outOff = 0;
        }
    }

    void parse()
    {
        while (rx.size() >= 2)
        {
            const int opcode = (unsigned char)rx[0] & 0x0f;
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
            const std::string payload = rx.substr(header, len);
            rx.erase(0, header + len);

            if (opcode == 0x1)
                texts.push_back(payload);
            else if (opcode == 0x2)
            {
                bin += payload;
                binCount++;
            }
            else if (opcode == 0x0)
                bin += payload;
            else if (opcode == 0x8 && payload.size() >= 2)
            {
                closeCode = ((unsigned char)payload[0] << 8) | (unsigned char)payload[1];
                reason = payload.substr(2);
            }
        }
    }

    int fd = -1;
    bool paused = false;
    bool headDone = false;
    bool tcpClosed = false;
    std::string head;
    std::string rx;
    std::string outq;
    size_t outOff = 0;
    std::vector<std::string> texts;
    std::string bin;
    size_t binCount = 0;
    int closeCode = 0;
    std::string reason;
};

Json openMessage(const std::string &codeId = kCode)
{
    return Json{ { "type", "open" }, { "cn_user", kUser }, { "cn_pass", kPass }, { "admin_session", FakeDaemon::Session }, { "code_id", codeId }, { "columns", 120 }, { "rows", 40 }, { "term", "xterm-256color" } };
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

size_t consoleCalls()
{
    return fakeDaemon.count("/api/v2/admin/console");
}

class ConsoleTunnelTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        LoginThrottle::clear();
        Utils::set_config_option("cn_user", kUser);
        Utils::set_config_option("cn_pass", kPass);
        writeToken(kBearer);

        fakeConsole.stop();
        fakeDaemon.stop();
        ASSERT_TRUE(fakeDaemon.start(env.daemonPort));
        fakeDaemon.reset(kBearer);
        fakeDaemon.setConsole(env.consoleDir);
        ::setenv("CALAOS_CONTAINER_URL", env.base.c_str(), 1);
        ::setenv("CALAOS_CONTAINER_CONSOLE_DIR", env.consoleDir.c_str(), 1);
        DaemonClient::Instance().configure();
        ConsoleTunnel::setTimings(ConsoleTunnel::Timings());

        ::unlink((env.cache + "/" + AdminState::FlagFileName).c_str());
        AdminState::Instance().observeUnreachable();
        AdminState::Instance().loadFlag();
        drainLoop(10);
    }

    void TearDown() override
    {
        fakeConsole.stop();
        drainLoop(30);
    }

    void adminDefined()
    {
        fakeDaemon.setAdminSet(true);
        syncAdminState();
        ASSERT_EQ(AdminState::Mode::Set, AdminState::Instance().mode());
        fakeDaemon.clearLog();
    }

    //The error of a refused open, after which the browser gets 1008
    Json refusal(ConsoleClient &c, const Json &open)
    {
        c.sendText(open);
        const Json e = c.nextText();
        EXPECT_EQ("error", str(e, "type")) << e.dump();
        EXPECT_EQ("false", str(e, "success")) << e.dump();
        EXPECT_FALSE(str(e, "msg").empty()) << e.dump();
        EXPECT_EQ(1008, c.waitClose()) << e.dump();
        return e;
    }

    //A console opened through the tunnel, its first frame read by the socket
    void attached(ConsoleClient &c, Json &ready)
    {
        adminDefined();
        fakeDaemon.grant(kCode);
        ASSERT_TRUE(fakeConsole.start(socketPath()));
        c.connect();
        ASSERT_TRUE(c.upgraded()) << c.answerHead();
        c.sendText(openMessage());
        ready = c.nextText();
        ASSERT_EQ("ready", str(ready, "type")) << ready.dump();
        ASSERT_TRUE(pumpUntil([]()
                              {
            return !fakeConsole.frames().empty();
        }));
    }
};

std::vector<FakeConsole::Frame> framesOf(int opcode)
{
    std::vector<FakeConsole::Frame> out;
    for (const FakeConsole::Frame &f: fakeConsole.frames())
        if (f.opcode == opcode)
            out.push_back(f);
    return out;
}

//The code of the close frame the socket got, 0 when none
int socketCloseCode()
{
    const auto closes = framesOf(0x8);
    if (closes.empty() || closes.back().payload.size() < 2)
        return 0;
    return ((unsigned char)closes.back().payload[0] << 8) | (unsigned char)closes.back().payload[1];
}

/* A WebSocket fed by hand, whose peer is a LAN address. Its writes go to a
 * real loopback socket, read back by the test. */
class LanWebSocket: public WebSocket
{
public:
    using WebSocket::WebSocket;
    string getClientIp() const override { return "192.168.1.20"; }
};

} // namespace

/*******************************************************************************
 * Pure rules
 ******************************************************************************/

TEST(ConsoleTunnelRules, TheSubprotocolIsOneItemOfTheOfferedList)
{
    EXPECT_TRUE(ConsoleTunnel::offersProtocol("calaos-console.v1"));
    EXPECT_TRUE(ConsoleTunnel::offersProtocol("chat, calaos-console.v1"));
    EXPECT_TRUE(ConsoleTunnel::offersProtocol(" calaos-console.v1 ,chat"));
    EXPECT_FALSE(ConsoleTunnel::offersProtocol(""));
    EXPECT_FALSE(ConsoleTunnel::offersProtocol("calaos-console.v2"));
    EXPECT_FALSE(ConsoleTunnel::offersProtocol("calaos-console.v1x, tty"));
}

TEST(ConsoleTunnelRules, TheSocketIsRebuiltFromTheIdUnderTheDirectory)
{
    EXPECT_EQ("/run/calaos-container/console/console-4fbd81b12ca13a2b.sock",
              ConsoleTunnel::expectedSocket("/run/calaos-container/console", "4fbd81b12ca13a2b"));
    EXPECT_EQ("", ConsoleTunnel::expectedSocket("", "4fbd81b12ca13a2b"));
    for (const char *id: { "", "4fbd81b12ca13a2", "4FBD81B12CA13A2B", "../../etc/passwd", "4fbd81b12ca13a2b/" })
        EXPECT_EQ("", ConsoleTunnel::expectedSocket("/run/calaos-container/console", id)) << id;
}

TEST(ConsoleTunnelRules, TheConsoleDirectoryIsAnAbsoluteCleanPath)
{
    EXPECT_TRUE(DaemonClient::isConsoleDir("/run/calaos-container/console"));
    EXPECT_TRUE(DaemonClient::isConsoleDir("/c"));
    for (const char *dir: { "", "/", "run/console", "/run/console/", "/run//console", "/run/../etc", "/run/.", "/.." })
        EXPECT_FALSE(DaemonClient::isConsoleDir(dir)) << dir;

    ::setenv("CALAOS_CONTAINER_CONSOLE_DIR", "relative/console", 1);
    DaemonClient::Instance().configure();
    EXPECT_EQ("", DaemonClient::Instance().consoleDir());
    ::unsetenv("CALAOS_CONTAINER_CONSOLE_DIR");
    DaemonClient::Instance().configure();
    EXPECT_EQ(DaemonClient::DefaultConsoleDir, DaemonClient::Instance().consoleDir());
}

TEST(ConsoleTunnelRules, TheServiceScopeRefusesTheConsoleInTheAdminShape)
{
    const auto &denied = JsonApi::serviceScopeDeniedCommands();
    EXPECT_NE(denied.end(), std::find(denied.begin(), denied.end(), std::string("console")));
    EXPECT_TRUE(JsonApi::isAdminCommand("console"));
}

/*******************************************************************************
 * The upgrade
 ******************************************************************************/

TEST_F(ConsoleTunnelTest, PlainHttpIsRefusedBeforeTheUpgrade)
{
    adminDefined();
    for (const char *xfp: { "", "http" })
    {
        ConsoleClient c;
        const std::string head = c.connect(xfp);
        EXPECT_EQ(403, statusOf(head)) << xfp << ": " << head;
    }
    EXPECT_EQ(0u, consoleCalls());
}

TEST_F(ConsoleTunnelTest, ALanPeerIsRefusedEvenWithForwardedHttps)
{
    adminDefined();

    const int lfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(0, bind(lfd, (struct sockaddr *)&a, sizeof(a)));
    ASSERT_EQ(0, listen(lfd, 1));
    socklen_t alen = sizeof(a);
    getsockname(lfd, (struct sockaddr *)&a, &alen);

    auto handle = uvw::Loop::getDefault()->resource<uvw::TcpHandle>();
    bool connected = false;
    handle->once<uvw::ConnectEvent>([&connected](const auto &, auto &)
                                    {
        connected = true;
    });
    handle->connect("127.0.0.1", ntohs(a.sin_port));
    ASSERT_TRUE(pumpUntil([&connected]()
                          {
        return connected;
    }));
    const int peer = accept(lfd, nullptr, nullptr);
    ASSERT_GE(peer, 0);

    {
        LanWebSocket ws(handle);
        ws.ProcessData("GET /api/console HTTP/1.1\r\nHost: box.example\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n"
                       "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\nSec-WebSocket-Version: 13\r\n"
                       "Sec-WebSocket-Protocol: calaos-console.v1\r\nX-Forwarded-Proto: https\r\n\r\n");
        std::string answer;
        pumpUntil([&]()
                  {
            char buf[4096];
            const ssize_t n = recv(peer, buf, sizeof(buf), MSG_DONTWAIT);
            if (n > 0)
                answer.append(buf, (size_t)n);
            return answer.find("\r\n\r\n") != std::string::npos;
        });
        EXPECT_EQ(403, statusOf(answer)) << answer;
    }
    handle->close();
    close(peer);
    close(lfd);
    drainLoop(20);
    EXPECT_EQ(0u, consoleCalls());
}

TEST_F(ConsoleTunnelTest, AForeignOriginIsRefusedEvenBeforeAnAdminExists)
{
    syncAdminState();
    ASSERT_NE(AdminState::Mode::Set, AdminState::Instance().mode());

    ConsoleClient c;
    EXPECT_EQ(403, statusOf(c.connect("https", "https://evil.example"))) << c.answerHead();

    //The page of the box itself, behind haproxy
    ConsoleClient same;
    same.connect("https", "https://127.0.0.1:" + std::to_string(env.serverPort));
    EXPECT_TRUE(same.upgraded()) << same.answerHead();
}

TEST_F(ConsoleTunnelTest, TheSubprotocolMustBeOffered)
{
    ConsoleClient c;
    EXPECT_EQ(400, statusOf(c.connect("https", std::string(), false))) << c.answerHead();
}

/*******************************************************************************
 * The open message
 ******************************************************************************/

TEST_F(ConsoleTunnelTest, TheFirstMessageMustBeTheOpenMessage)
{
    adminDefined();

    {
        ConsoleClient c;
        c.connect();
        ASSERT_TRUE(c.upgraded()) << c.answerHead();
        //The 101 names the subprotocol, or a browser drops the connection
        EXPECT_NE(std::string::npos, c.answerHead().find("Sec-WebSocket-Protocol: calaos-console.v1\r\n")) << c.answerHead();
        c.sendBinary("ls\n");
        const Json e = c.nextText();
        EXPECT_EQ("invalid_request", str(e, "error")) << e.dump();
        EXPECT_EQ(1008, c.waitClose());
        EXPECT_EQ("console refused", c.closeReason());
    }

    for (const std::string &bad: { std::string("not json"), Json{ { "type", "input" }, { "data", "ls" } }.dump(), std::string("[1]"),
                                   std::string(ConsoleTunnel::MaxClientMessage + 1, ' ') })
    {
        ConsoleClient c;
        c.connect();
        c.sendText(bad);
        const Json e = c.nextText();
        EXPECT_EQ("invalid_request", str(e, "error")) << bad.substr(0, 40);
        EXPECT_EQ(1008, c.waitClose());
    }
    EXPECT_EQ(0u, consoleCalls());
}

TEST_F(ConsoleTunnelTest, AnOpenMessageThatNeverComesEndsTheConnection)
{
    ConsoleTunnel::Timings t;
    t.open = 0.2;
    ConsoleTunnel::setTimings(t);

    ConsoleClient c;
    c.connect();
    ASSERT_TRUE(c.upgraded());
    const Json e = c.nextText(3000);
    EXPECT_EQ("error", str(e, "type"));
    EXPECT_EQ("invalid_request", str(e, "error")) << e.dump();
    EXPECT_EQ(1008, c.waitClose());
}

TEST_F(ConsoleTunnelTest, WrongHouseholdCredentialsAreRefusedThenThrottled)
{
    adminDefined();

    Json open = openMessage();
    open["cn_pass"] = "wrong";
    {
        ConsoleClient c;
        c.connect();
        EXPECT_EQ("login_failed", str(refusal(c, open), "error"));
    }
    {
        ConsoleClient c;
        c.connect();
        const Json e = refusal(c, openMessage());
        EXPECT_EQ("rate_limited", str(e, "error"));
        EXPECT_FALSE(str(e, "retry_after").empty()) << e.dump();
    }
    EXPECT_EQ(0u, consoleCalls());
}

TEST_F(ConsoleTunnelTest, TheMcpServiceScopeIsRefused)
{
    adminDefined();
    fakeDaemon.grant(kCode);

    Json open = openMessage();
    open.erase("cn_user");
    open.erase("cn_pass");
    open["token"] = kServiceToken;
    {
        ConsoleClient c;
        c.connect();
        const Json e = refusal(c, open);
        EXPECT_EQ("not_allowed", str(e, "error"));
        EXPECT_EQ("scope denied", str(e, "msg"));
    }

    //A wrong service token is a failed login of the service bucket
    open["token"] = "wrong";
    {
        ConsoleClient c;
        c.connect();
        EXPECT_EQ("login_failed", str(refusal(c, open), "error"));
    }
    open["token"] = kServiceToken;
    {
        ConsoleClient c;
        c.connect();
        EXPECT_EQ("rate_limited", str(refusal(c, open), "error"));
    }
    EXPECT_EQ(0u, consoleCalls());
}

TEST_F(ConsoleTunnelTest, EmptyHouseholdCredentialsNeverOpenAConsole)
{
    adminDefined();
    fakeDaemon.grant(kCode);
    Utils::set_config_option("cn_user", "");
    Utils::set_config_option("cn_pass", "");

    Json open = openMessage();
    open["cn_user"] = "";
    open["cn_pass"] = "";
    ConsoleClient c;
    c.connect();
    EXPECT_EQ("login_failed", str(refusal(c, open), "error"));
    EXPECT_EQ(0u, consoleCalls());
}

TEST_F(ConsoleTunnelTest, TheFieldsAreCheckedBeforeTheDaemonIsCalled)
{
    adminDefined();

    std::vector<Json> bad;
    for (const Json &codeId: { Json("c0de"), Json("C0DE000000000001"), Json(42), Json() })
    {
        Json o = openMessage();
        o["code_id"] = codeId;
        bad.push_back(o);
    }
    for (const char *k: { "columns", "rows" })
    {
        for (const Json &v: { Json(0), Json(1001), Json("80"), Json(80.5), Json(-1) })
        {
            Json o = openMessage();
            o[k] = v;
            bad.push_back(o);
        }
        Json o = openMessage();
        o.erase(k);
        bad.push_back(o);
    }
    for (const Json &term: { Json("linux"), Json(""), Json(1), Json() })
    {
        Json o = openMessage();
        o["term"] = term;
        bad.push_back(o);
    }

    for (const Json &o: bad)
    {
        ConsoleClient c;
        c.connect();
        EXPECT_EQ("invalid_request", str(refusal(c, o), "error")) << o.dump();
    }
    EXPECT_EQ(0u, consoleCalls());
}

TEST_F(ConsoleTunnelTest, NoConsoleWithoutAnAdmin)
{
    ConsoleClient c;
    c.connect();
    EXPECT_EQ("admin_not_set", str(refusal(c, openMessage()), "error"));
    EXPECT_EQ(0u, consoleCalls());
}

TEST_F(ConsoleTunnelTest, AMissingOrMalformedSessionNeverReachesTheDaemon)
{
    adminDefined();
    for (const Json &session: { Json(), Json(""), Json("0123"), Json(std::string(64, 'G')) })
    {
        Json o = openMessage();
        if (session.is_null())
            o.erase("admin_session");
        else
            o["admin_session"] = session;
        ConsoleClient c;
        c.connect();
        EXPECT_EQ("bad_session", str(refusal(c, o), "error")) << o.dump();
    }
    EXPECT_EQ(0u, consoleCalls());
}

TEST_F(ConsoleTunnelTest, AnExpiredSessionIsTheDaemonsBadSession)
{
    adminDefined();
    fakeDaemon.grant(kCode);
    Json o = openMessage();
    o["admin_session"] = std::string(64, 'f');
    ConsoleClient c;
    c.connect();
    EXPECT_EQ("bad_session", str(refusal(c, o), "error"));
    EXPECT_EQ(1u, consoleCalls());
    EXPECT_EQ(0u, fakeConsole.connections());
}

TEST_F(ConsoleTunnelTest, TheOpenCallCarriesTheSessionAndOnlyTheValidatedFields)
{
    Json ready;
    ConsoleClient c;
    attached(c, ready);

    const FakeDaemon::Request r = fakeDaemon.last("/api/v2/admin/console");
    EXPECT_EQ("POST", r.method);
    EXPECT_EQ(FakeDaemon::Session, r.headers.at("x-calaos-admin-session"));
    EXPECT_EQ("Bearer " + std::string(kBearer), r.headers.at("authorization"));
    EXPECT_EQ("127.0.0.1", r.headers.at("x-calaos-client-ip"));
    EXPECT_EQ((Json{ { "code_id", kCode }, { "term", "xterm-256color" } }), Json::parse(r.body)) << r.body;

    EXPECT_EQ(FakeDaemon::DefaultConsoleId, str(ready, "console_id"));
    EXPECT_EQ(3600, ready.value("expires_in", 0));
    EXPECT_EQ(900, ready.value("idle_timeout", 0));
    EXPECT_EQ(4u, ready.size()) << ready.dump();
}

TEST_F(ConsoleTunnelTest, AGrantIsTakenOnce)
{
    Json ready;
    {
        ConsoleClient c;
        attached(c, ready);
        c.sendClose(1000);
        c.waitClose();
    }
    drainLoop(50);
    fakeConsole.stop();
    ASSERT_TRUE(fakeConsole.start(socketPath()));

    ConsoleClient again;
    again.connect();
    EXPECT_EQ("code_expired", str(refusal(again, openMessage()), "error"));
    EXPECT_EQ(0u, fakeConsole.connections());
}

TEST_F(ConsoleTunnelTest, TheDaemonsRefusalsAreTranslated)
{
    adminDefined();
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/admin/console")
            return false;
        res.status = 409;
        res.body = FakeDaemon::err("console_limit", { { "max", 2 } });
        return true;
    });
    ConsoleClient c;
    c.connect();
    const Json e = refusal(c, openMessage());
    EXPECT_EQ("console_limit", str(e, "error"));
    EXPECT_EQ("2", str(e, "max"));
}

TEST_F(ConsoleTunnelTest, OnlyASocketOfTheConsoleDirectoryIsConnected)
{
    adminDefined();
    ASSERT_TRUE(fakeConsole.start(socketPath()));

    const Json good = { { "console_id", FakeDaemon::DefaultConsoleId }, { "socket", socketPath() }, { "ws_path", "/ws" }, { "ws_protocol", "calaos-console.v1" } };
    std::vector<Json> answers;
    for (const char *k: { "console_id", "socket", "ws_path", "ws_protocol" })
    {
        Json a = good;
        a.erase(k);
        answers.push_back(a);
    }
    for (const std::pair<const char *, const char *> &kv: std::vector<std::pair<const char *, const char *>>{
             { "socket", "/tmp/evil.sock" },
             { "socket", "/run/calaos-container/console/console-4fbd81b12ca13a2b.sock" },
             { "console_id", "../../../tmp/x" },
             { "ws_path", "/" },
             { "ws_protocol", "tty" } })
    {
        Json a = good;
        a[kv.first] = kv.second;
        answers.push_back(a);
    }

    for (const Json &a: answers)
    {
        fakeDaemon.setOverride([a](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                               {
            if (req.path != "/api/v2/admin/console")
                return false;
            res.body = FakeDaemon::ok(a);
            return true;
        });
        ConsoleClient c;
        c.connect();
        EXPECT_EQ("internal", str(refusal(c, openMessage()), "error")) << a.dump();
    }
    EXPECT_EQ(0u, fakeConsole.connections());
}

TEST_F(ConsoleTunnelTest, ABrowserThatLeavesWhileTheDaemonAnswersAttachesNothing)
{
    adminDefined();
    fakeDaemon.grant(kCode);
    ASSERT_TRUE(fakeConsole.start(socketPath()));
    fakeDaemon.setOverride([](const FakeDaemon::Request &req, FakeDaemon::Response &res)
                           {
        if (req.path != "/api/v2/admin/console")
            return false;
        res.delayMs = 300;
        res.body = FakeDaemon::ok({ { "console_id", FakeDaemon::DefaultConsoleId }, { "socket", socketPath() }, { "ws_path", "/ws" }, { "ws_protocol", "calaos-console.v1" } });
        return true;
    });

    {
        ConsoleClient c;
        c.connect();
        c.sendText(openMessage());
        //Ignored until the console is attached
        c.sendBinary("early");
        ASSERT_TRUE(pumpUntil([]()
                              {
            return consoleCalls() == 1;
        }));
        c.sendClose(1000);
        EXPECT_EQ(1000, c.waitClose());
    }
    drainLoop(600);
    EXPECT_EQ(0u, fakeConsole.connections());
}

/*******************************************************************************
 * The console socket
 ******************************************************************************/

TEST_F(ConsoleTunnelTest, ASocketThatCannotBeAttachedIsReported)
{
    adminDefined();
    ConsoleTunnel::Timings t;
    t.attach = 0.3;
    ConsoleTunnel::setTimings(t);

    struct Case
    {
        bool listening;
        FakeConsole::Mode mode;
        const char *error;
    };
    for (const Case &k: { Case{ false, FakeConsole::Mode::Upgrade, "daemon_unreachable" },
                          Case{ true, FakeConsole::Mode::Silent, "daemon_unreachable" },
                          Case{ true, FakeConsole::Mode::HangUp, "daemon_unreachable" },
                          Case{ true, FakeConsole::Mode::Refuse, "internal" },
                          Case{ true, FakeConsole::Mode::NoProtocol, "internal" },
                          Case{ true, FakeConsole::Mode::Garbage, "internal" } })
    {
        fakeConsole.stop();
        if (k.listening)
        {
            ASSERT_TRUE(fakeConsole.start(socketPath(), k.mode));
        }
        fakeDaemon.grant(kCode);
        ConsoleClient c;
        c.connect();
        EXPECT_EQ(k.error, str(refusal(c, openMessage()), "error")) << (int)k.mode;
        //The socket is left: the daemon ends a console never attached
        if (k.listening && k.mode != FakeConsole::Mode::HangUp)
        {
            EXPECT_TRUE(pumpUntil([]()
                                  {
                return fakeConsole.sawEof();
            },
                                  2000))
                << (int)k.mode;
        }
    }
}

TEST_F(ConsoleTunnelTest, TheUpgradeOfTheSocketCarriesNoCredential)
{
    Json ready;
    ConsoleClient c;
    attached(c, ready);

    const std::string head = fakeConsole.request();
    EXPECT_EQ(0u, head.find("GET /ws HTTP/1.1\r\n")) << head;
    EXPECT_NE(std::string::npos, head.find("Sec-WebSocket-Protocol: calaos-console.v1\r\n")) << head;
    for (const char *secret: { "Authorization", "X-Calaos-Admin-Session", kBearer, FakeDaemon::Session, kPass, kCode })
        EXPECT_EQ(std::string::npos, head.find(secret)) << secret;

    //The first frame is the size, rebuilt: nothing else of the open message
    const FakeConsole::Frame first = fakeConsole.frames().front();
    EXPECT_EQ(0x1, first.opcode);
    EXPECT_TRUE(first.masked);
    EXPECT_EQ((Json{ { "columns", 120 }, { "rows", 40 } }), Json::parse(first.payload));
}

TEST_F(ConsoleTunnelTest, TheTunnelRelaysTheTerminalBothWays)
{
    Json ready;
    ConsoleClient c;
    attached(c, ready);

    //Browser to socket: input as raw bytes, resizes bounded, the rest dropped
    c.sendText(Json{ { "type", "input" }, { "data", "ls -l\n" } });
    c.sendBinary(std::string("\x1b[A", 3));
    c.sendText(Json{ { "type", "resize" }, { "columns", 200 }, { "rows", 50 } });
    for (const Json &bad: { Json{ { "type", "resize" }, { "columns", 0 }, { "rows", 50 } },
                            Json{ { "type", "resize" }, { "columns", 200 }, { "rows", 1001 } },
                            Json{ { "type", "resize" }, { "columns", "200" }, { "rows", 50 } },
                            Json{ { "type", "pause" } }, Json{ { "columns", 10 }, { "rows", 10 } }, Json("text") })
        c.sendText(bad);
    c.sendText(std::string("{not json"));
    const std::string big = bytes(40000, 7);
    c.sendBinary(big);
    c.sendText(Json{ { "type", "input" }, { "data", "" } });
    c.sendText(Json{ { "type", "input" }, { "data", "exit\n" } });

    const std::string expected = std::string("ls -l\n") + std::string("\x1b[A", 3) + big + "exit\n";
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return fakeConsole.inputSize() >= expected.size();
    }));
    EXPECT_EQ(expected, fakeConsole.input());
    //16 KiB frames at most
    for (const FakeConsole::Frame &f: framesOf(0x2))
        EXPECT_LE(f.payload.size(), ConsoleTunnel::InputChunk);
    EXPECT_EQ(6u, framesOf(0x2).size());

    const auto texts = framesOf(0x1);
    ASSERT_EQ(2u, texts.size());
    EXPECT_EQ((Json{ { "type", "resize" }, { "columns", 200 }, { "rows", 50 } }), Json::parse(texts[1].payload));
    for (const FakeConsole::Frame &f: fakeConsole.frames())
        EXPECT_TRUE(f.masked);

    //Socket to browser: binary output, fragments joined, text dropped, pings answered
    fakeConsole.send(0x2, "total 0\r\n");
    fakeConsole.send(0x1, "{\"type\":\"exit\"}");
    fakeConsole.send(0x2, "drw", false);
    fakeConsole.send(0x0, "x\r\n", true);
    fakeConsole.send(0x9, "p1");
    fakeConsole.send(0xA, "");
    fakeConsole.send(0x2, "$ ");
    ASSERT_TRUE(pumpUntil([&]()
                          {
        c.pump();
        return c.binary().size() >= 17;
    }));
    EXPECT_EQ("total 0\r\ndrwx\r\n$ ", c.binary());
    EXPECT_EQ(3u, c.binaryMessages());
    EXPECT_FALSE(c.hasText());
    ASSERT_TRUE(pumpUntil([]()
                          {
        return !framesOf(0xA).empty();
    }));
    EXPECT_EQ("p1", framesOf(0xA).front().payload);
}

TEST_F(ConsoleTunnelTest, TheSocketsCloseIsRelayedWithItsCode)
{
    struct Case
    {
        int sent; //0: the socket hangs up
        int browser;
    };
    for (const Case &k: { Case{ 1000, 1000 }, Case{ 1001, 1001 }, Case{ 1011, 1001 }, Case{ 0, 1001 } })
    {
        Json ready;
        ConsoleClient c;
        attached(c, ready);
        if (k.sent)
            fakeConsole.sendClose((uint16_t)k.sent);
        else
            fakeConsole.hangUp();
        EXPECT_EQ(k.browser, c.waitClose()) << k.sent;
        EXPECT_EQ("console closed", c.closeReason());
        if (k.sent)
        {
            //Echoed as 1000 or 1001 before the socket is shut down
            ASSERT_TRUE(pumpUntil([]()
                                  {
                return fakeConsole.sawEof();
            }));
            EXPECT_EQ(k.browser, socketCloseCode());
        }
        c.sendClose(1000);
        drainLoop(30);
        fakeConsole.stop();
    }
}

TEST_F(ConsoleTunnelTest, TheBrowsersCloseIsRelayed)
{
    struct Case
    {
        int sent; //0: the browser drops the connection
        int socket;
    };
    for (const Case &k: { Case{ 1000, 1000 }, Case{ 1001, 1001 }, Case{ 4000, 1001 }, Case{ 0, 1001 } })
    {
        {
            Json ready;
            ConsoleClient c;
            attached(c, ready);
            if (k.sent)
            {
                c.sendClose((uint16_t)k.sent);
                EXPECT_EQ(k.sent, c.waitClose()) << k.sent;
            }
        }
        ASSERT_TRUE(pumpUntil([]()
                              {
            return fakeConsole.sawEof();
        }))
            << k.sent;
        EXPECT_EQ(k.socket, socketCloseCode()) << k.sent;
        fakeConsole.stop();
    }
}

TEST_F(ConsoleTunnelTest, ASocketThatBreaksTheProtocolEndsTheConsole)
{
    struct Sent
    {
        int opcode;
        std::string payload;
        bool fin;
        bool masked;
    };
    struct Case
    {
        std::vector<Sent> frames;
        int socket;
    };
    const std::vector<Case> cases = {
        { { { 0x2, "masked", true, true } }, 1002 },
        { { { 0x0, "orphan", true, false } }, 1002 },
        { { { 0x3, "reserved", true, false } }, 1002 },
        { { { 0x2, std::string(600 * 1024, 'a'), false, false }, { 0x0, std::string(600 * 1024, 'b'), true, false } }, 1009 },
    };
    for (const Case &k: cases)
    {
        Json ready;
        ConsoleClient c;
        attached(c, ready);
        for (const Sent &f: k.frames)
            fakeConsole.send(f.opcode, f.payload, f.fin, f.masked);
        EXPECT_EQ(1001, c.waitClose());
        ASSERT_TRUE(pumpUntil([]()
                              {
            return fakeConsole.sawEof();
        }));
        EXPECT_EQ(k.socket, socketCloseCode());
        EXPECT_EQ("", c.binary());
        c.sendClose(1000);
        drainLoop(30);
        fakeConsole.stop();
    }
}

TEST_F(ConsoleTunnelTest, ABrowserMessageOverTheDaemonsLimitEndsTheConsole)
{
    for (int opcode: { 0x1, 0x2 })
    {
        Json ready;
        ConsoleClient c;
        attached(c, ready);
        c.sendFrame(opcode, std::string(ConsoleTunnel::MaxClientMessage + 1, 'x'));
        EXPECT_EQ(1009, c.waitClose()) << opcode;
        ASSERT_TRUE(pumpUntil([]()
                              {
            return fakeConsole.sawEof();
        }));
        EXPECT_EQ(1000, socketCloseCode());
        EXPECT_EQ("", fakeConsole.input());
        c.sendClose(1000);
        drainLoop(30);
        fakeConsole.stop();
    }
}

TEST_F(ConsoleTunnelTest, ABrowserThatStopsReadingStopsTheSocketBeingRead)
{
    Json ready;
    ConsoleClient c;
    attached(c, ready);

    const size_t total = 32 * 1024 * 1024;
    const std::string out = bytes(total, 3);
    c.pause(true);
    for (size_t at = 0; at < total; at += 64 * 1024)
        fakeConsole.send(0x2, out.substr(at, 64 * 1024));

    //Nothing is read past the high water mark: the socket keeps its output
    drainLoop(1500);
    EXPECT_GT(fakeConsole.pendingOut(), 0u);

    c.pause(false);
    ASSERT_TRUE(pumpUntil([&]()
                          {
        c.pump();
        return c.binary().size() >= total;
    },
                          30000));
    EXPECT_TRUE(c.binary() == out);
    EXPECT_EQ(0u, fakeConsole.pendingOut());
}

TEST_F(ConsoleTunnelTest, ASocketThatStopsReadingStopsTheBrowserBeingRead)
{
    Json ready;
    ConsoleClient c;
    attached(c, ready);

    const size_t total = 32 * 1024 * 1024;
    const std::string in = bytes(total, 5);
    fakeConsole.pause(true);
    for (size_t at = 0; at < total; at += ConsoleTunnel::MaxClientMessage)
        c.sendBinary(in.substr(at, ConsoleTunnel::MaxClientMessage));

    drainLoop(1500);
    c.pump();
    EXPECT_GT(c.unsent(), 0u);

    fakeConsole.pause(false);
    ASSERT_TRUE(pumpUntil([&]()
                          {
        c.pump();
        return fakeConsole.inputSize() >= total;
    },
                          30000));
    EXPECT_TRUE(fakeConsole.input() == in);
    EXPECT_EQ(0u, c.unsent());
}

TEST_F(ConsoleTunnelTest, AWebSocketThatClosesItselfDetachesTheConsoleAtOnce)
{
    struct Case
    {
        int opcode;
        std::string payload;
        int browser;
        int socket;
    };
    //An empty message and a continuation without a start: the WebSocket
    //closes on its own, and the browser never answers that close here
    for (const Case &k: { Case{ 0x2, "", 1000, 1000 }, Case{ 0x0, "x", 1002, 1001 } })
    {
        Json ready;
        ConsoleClient c;
        attached(c, ready);
        c.sendFrame(k.opcode, k.payload);
        EXPECT_EQ(k.browser, c.waitClose()) << k.opcode;
        ASSERT_TRUE(pumpUntil([]()
                              {
            return fakeConsole.sawEof();
        },
                              2000))
            << k.opcode;
        EXPECT_EQ(k.socket, socketCloseCode());
        fakeConsole.stop();
    }
}

TEST_F(ConsoleTunnelTest, AConsoleThatEndsWhileTheBrowserIsPausedStillReadsItsClose)
{
    Json ready;
    ConsoleClient c;
    attached(c, ready);

    fakeConsole.pause(true);
    const std::string chunk = bytes(ConsoleTunnel::MaxClientMessage, 9);
    for (int i = 0; i < 512 && c.unsent() == 0; i++)
    {
        c.sendBinary(chunk);
        drainLoop(2);
    }
    drainLoop(500);
    c.pump();
    ASSERT_GT(c.unsent(), 0u) << "the browser was never paused, so this case measures nothing";

    fakeConsole.sendClose(1000);
    EXPECT_EQ(1000, c.waitClose());
    c.sendClose(1000);
    const auto start = steady_clock::now();
    EXPECT_TRUE(pumpUntil([&c]()
                          {
        c.pump();
        return c.disconnected();
    },
                          8000));
    EXPECT_LT(std::chrono::duration_cast<milliseconds>(steady_clock::now() - start).count(), 5000);
}

/*
 * Own main: the server, the fake daemon, the configuration, the token file and
 * the console directory must exist before the singletons read them.
 */
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_co2_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
        return 1;

    const std::string cfg = std::string(base) + "/config";
    env.cache = std::string(base) + "/cache";
    env.consoleDir = std::string(base) + "/console";
    const std::string bin = std::string(base) + "/bin";
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(env.cache.c_str(), 0700);
    ::mkdir(env.consoleDir.c_str(), 0700);
    ::mkdir(bin.c_str(), 0700);

    Utils::initConfigOptions(const_cast<char *>(cfg.c_str()), const_cast<char *>(env.cache.c_str()), true);
    Utils::set_config_option("listen_address", "127.0.0.1");
    Utils::set_config_option("cn_user", kUser);
    Utils::set_config_option("cn_pass", kPass);
    Utils::set_config_option("mcp_service_token", kServiceToken);

    {
        const std::string sidecar = bin + "/calaos_mcp";
        std::ofstream f(sidecar);
        f << "#!/bin/sh\nexec sleep 300\n";
        f.close();
        ::chmod(sidecar.c_str(), 0700);
    }
    ::setenv("CALAOS_BIN_PREFIX", bin.c_str(), 1);

    env.tokenFile = std::string(base) + "/ct.token";
    writeToken(kBearer);

    env.daemonPort = pickFreePort();
    env.base = "http://127.0.0.1:" + std::to_string(env.daemonPort);
    if (!fakeDaemon.start(env.daemonPort))
        return 1;
    env.serverPort = pickFreePort();

    ::setenv("CALAOS_CONTAINER_URL", env.base.c_str(), 1);
    ::setenv("CALAOS_CONTAINER_TOKEN_FILE", env.tokenFile.c_str(), 1);
    ::setenv("CALAOS_CONTAINER_CONSOLE_DIR", env.consoleDir.c_str(), 1);

    McpServerManager::Instance().start();
    HttpServer::Instance(env.serverPort);
    drainLoop(50);

    const int rc = RUN_ALL_TESTS();

    AdminState::Instance().stop();
    McpServerManager::Instance().stop();
    fakeConsole.stop();
    fakeDaemon.stop();
    drainLoop(50);
    std::error_code ec;
    std::filesystem::remove_all(base, ec);

    //Same exit as the other server tests: the singletons' destructors would
    //run after the tree and the loop's users are gone
    fflush(nullptr);
    _exit(rc);
}
