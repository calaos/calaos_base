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
 * The Origin rules of docs/08_http_api.md (section Origin): the decision
 * itself on injected peers, interfaces and api_allowed_hosts, then the
 * websocket handshake and POST /api.php of a real HttpServer bound on
 * loopback, under every AdminState that matters. The cache holding
 * admin_seen_set is a temporary directory.
 */

#include <gtest/gtest.h>

#include "Utils.h"
#include "HttpServer.h"
#include "AdminState.h"
#include "OriginPolicy.h"

#include "libuvw.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace Calaos;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

namespace
{

const char *const kUser = "cb1-household";
const char *const kPass = "cb1-household-password";

const std::vector<std::string> kInterfaces = {
    "127.0.0.1", "::1", "192.168.1.10", "100.64.0.3", "fe80::1c2a:3bff:fe4d:5e6f"
};
const std::vector<std::string> kAllowed = {
    "calaos.example.net", " Box.Local. ", "dev.example.org:5173"
};

OriginPolicy::Request lan(const std::string &host)
{
    return OriginPolicy::Request{ host, "192.168.1.50", "" };
}

OriginPolicy::Request haproxy(const std::string &host, const std::string &xfp = "https")
{
    return OriginPolicy::Request{ host, "127.0.0.1", xfp };
}

bool accepts(const std::string &origin, const OriginPolicy::Request &req)
{
    return OriginPolicy::accepts(origin, req, kInterfaces, kAllowed);
}

TEST(OriginPolicyParse, OriginIsNormalised)
{
    OriginPolicy::Endpoint e;
    ASSERT_TRUE(OriginPolicy::parseOrigin("HTTPS://Box.DDNS.Example.", e));
    EXPECT_EQ("https", e.scheme);
    EXPECT_EQ("box.ddns.example", e.host);
    EXPECT_EQ("443", e.port);

    ASSERT_TRUE(OriginPolicy::parseOrigin("http://192.168.1.10", e));
    EXPECT_EQ("80", e.port);

    ASSERT_TRUE(OriginPolicy::parseOrigin("http://192.168.1.10:05454", e));
    EXPECT_EQ("5454", e.port);

    ASSERT_TRUE(OriginPolicy::parseOrigin("http://[0:0::1]:5454", e));
    EXPECT_EQ("::1", e.host);
}

TEST(OriginPolicyParse, HostKeepsOnlyTheGivenPort)
{
    OriginPolicy::Endpoint e;
    ASSERT_TRUE(OriginPolicy::parseHost("Box.Example.:8443", e));
    EXPECT_EQ("box.example", e.host);
    EXPECT_EQ("8443", e.port);

    ASSERT_TRUE(OriginPolicy::parseHost("box.example", e));
    EXPECT_EQ("", e.port);

    ASSERT_TRUE(OriginPolicy::parseHost("[::1]:5454", e));
    EXPECT_EQ("::1", e.host);
    EXPECT_EQ("5454", e.port);
}

const std::vector<std::string> kMalformed = {
    "",
    "null",
    "192.168.1.10",
    "http://",
    "ftp://192.168.1.10",
    "http://192.168.1.10/",
    "http://192.168.1.10:5454/app",
    "http://user@192.168.1.10",
    "http://192.168.1.10?x",
    "http://a..b",
    "http://.a",
    "http://[::1",
    "http://[::1]x",
    "http://[nothost]:80",
    "http://::1",
    "http://192.168.1.10:99999",
    "http://192.168.1.10:0",
    "http://192.168.1.10:x",
    "http://ex ample.org",
    std::string("http://exa") + char(1) + "mple.org",
    std::string("http://") + char(0xc3) + char(0xa9) + "t.fr",
};

TEST(OriginPolicyParse, MalformedIsRejected)
{
    OriginPolicy::Endpoint e;
    for (const auto &bad: kMalformed)
        EXPECT_FALSE(OriginPolicy::parseOrigin(bad, e)) << bad;
}

TEST(OriginPolicyMatrix, NullAndForeignAreRefused)
{
    EXPECT_FALSE(accepts("null", lan("192.168.1.10:5454")));
    EXPECT_FALSE(accepts("http://evil.example", lan("192.168.1.10:5454")));
    EXPECT_FALSE(accepts("https://evil.example", haproxy("box.ddns.example")));
    EXPECT_FALSE(accepts("http://192.168.1.99:5454", lan("192.168.1.10:5454")));
}

//A rebound name is both the Origin and the Host: equality alone proves nothing
TEST(OriginPolicyMatrix, ReboundNameIsRefused)
{
    EXPECT_FALSE(accepts("http://rebind.attacker.example:5454", lan("rebind.attacker.example:5454")));
    EXPECT_FALSE(accepts("http://rebind.attacker.example", lan("rebind.attacker.example")));
    //through haproxy, but not over TLS
    EXPECT_FALSE(accepts("http://rebind.attacker.example", haproxy("rebind.attacker.example", "http")));
    EXPECT_FALSE(accepts("http://rebind.attacker.example", haproxy("rebind.attacker.example", "")));
}

TEST(OriginPolicyMatrix, HostEqualityUnderHaproxyHttps)
{
    EXPECT_TRUE(accepts("https://box.ddns.example", haproxy("box.ddns.example")));
    EXPECT_TRUE(accepts("https://box.ddns.example", haproxy("box.ddns.example:443")));
    EXPECT_TRUE(accepts("HTTPS://BOX.ddns.example.", haproxy("box.ddns.example")));
    EXPECT_TRUE(accepts("https://box.ddns.example:8443", haproxy("box.ddns.example:8443")));
    EXPECT_TRUE(accepts("https://box.ddns.example", OriginPolicy::Request{ "box.ddns.example", "::1", "https" }));

    //the port is compared only when Host carries one
    EXPECT_TRUE(accepts("https://box.ddns.example:8443", haproxy("box.ddns.example")));
    EXPECT_FALSE(accepts("https://box.ddns.example", haproxy("box.ddns.example:8443")));

    EXPECT_FALSE(accepts("https://other.ddns.example", haproxy("box.ddns.example")));
    EXPECT_FALSE(accepts("http://box.ddns.example", haproxy("box.ddns.example")));
    EXPECT_FALSE(accepts("https://box.ddns.example", haproxy("box.ddns.example", "http")));
    EXPECT_FALSE(accepts("https://box.ddns.example", haproxy("box.ddns.example", "")));
    EXPECT_FALSE(accepts("https://box.ddns.example", haproxy("")));
    //a LAN client cannot claim the proxy's header
    EXPECT_FALSE(accepts("https://box.ddns.example",
                         OriginPolicy::Request{ "box.ddns.example", "192.168.1.50", "https" }));
}

TEST(OriginPolicyMatrix, InterfaceIps)
{
    EXPECT_TRUE(accepts("http://192.168.1.10:5454", lan("192.168.1.10:5454")));
    EXPECT_TRUE(accepts("http://100.64.0.3:5454", lan("100.64.0.3:5454")));
    EXPECT_TRUE(accepts("http://[fe80::1c2a:3bff:fe4d:5e6f]:5454", lan("[fe80::1c2a:3bff:fe4d:5e6f]:5454")));
    EXPECT_TRUE(accepts("http://192.168.1.10", lan("192.168.1.10:80")));
    EXPECT_TRUE(accepts("http://192.168.1.10:80", lan("192.168.1.10")));
    EXPECT_TRUE(accepts("https://192.168.1.10", haproxy("192.168.1.10")));
    EXPECT_TRUE(accepts("http://[::ffff:192.168.1.10]:5454", lan("192.168.1.10:5454")));

    //another service of the box on another port is another origin
    EXPECT_FALSE(accepts("http://192.168.1.10:8080", lan("192.168.1.10:5454")));
    EXPECT_FALSE(accepts("http://192.168.1.10:5454", lan("garbage host")));
    //the Origin must be the IP the request was sent to
    EXPECT_FALSE(accepts("http://100.64.0.3:5454", lan("192.168.1.10:5454")));
    EXPECT_FALSE(accepts("http://192.168.1.10:5454", lan("")));
}

//Grafana or zigbee2mqtt on another port of the box, calling the API through haproxy
TEST(OriginPolicyMatrix, OtherBoxServiceThroughHaproxyIsRefused)
{
    EXPECT_FALSE(accepts("http://192.168.1.10:3000", haproxy("box.ddns.example")));
    EXPECT_FALSE(accepts("http://192.168.1.10:8080", haproxy("box.ddns.example")));
    EXPECT_FALSE(accepts("http://192.168.1.10:3000", haproxy("192.168.1.10")));
    EXPECT_FALSE(accepts("http://192.168.1.10:3000", haproxy("")));
    EXPECT_FALSE(accepts("http://192.168.1.10:3000", lan("")));
}

TEST(OriginPolicyMatrix, LoopbackIsNotTheBox)
{
    EXPECT_FALSE(accepts("http://127.0.0.1:5454", lan("127.0.0.1:5454")));
    EXPECT_FALSE(accepts("http://[::1]:5454", lan("[::1]:5454")));
    EXPECT_FALSE(accepts("http://127.0.0.1:3000", haproxy("127.0.0.1")));
    //a browser on the box itself goes through haproxy
    EXPECT_TRUE(accepts("https://127.0.0.1", haproxy("127.0.0.1")));

    const auto ips = OriginPolicy::interfaceAddresses();
    EXPECT_EQ(ips.end(), std::find(ips.begin(), ips.end(), "127.0.0.1"));
    EXPECT_EQ(ips.end(), std::find(ips.begin(), ips.end(), "::1"));
}

TEST(OriginPolicyMatrix, NoHostnameNorLocalUnlessAllowed)
{
    EXPECT_FALSE(accepts("http://calaos:5454", lan("calaos:5454")));
    EXPECT_FALSE(accepts("http://calaos.local:5454", lan("calaos.local:5454")));
    EXPECT_FALSE(accepts("http://localhost:5454", lan("localhost:5454")));
}

TEST(OriginPolicyMatrix, AllowedHosts)
{
    EXPECT_TRUE(accepts("http://calaos.example.net:5454", lan("192.168.1.10:5454")));
    EXPECT_TRUE(accepts("https://calaos.example.net", lan("192.168.1.10:5454")));
    EXPECT_TRUE(accepts("http://box.local:5454", lan("box.local:5454")));
    EXPECT_TRUE(accepts("http://dev.example.org:5173", lan("192.168.1.10:5454")));
    EXPECT_FALSE(accepts("http://dev.example.org:3000", lan("192.168.1.10:5454")));
    EXPECT_FALSE(accepts("http://sub.calaos.example.net", lan("192.168.1.10:5454")));
    EXPECT_FALSE(accepts("http://calaos.example.net.evil.example", lan("192.168.1.10:5454")));

    EXPECT_TRUE(OriginPolicy::accepts("http://vpn.example:5454", lan("x:5454"), {},
                                      { "https://VPN.example/" }));

    //unusable entries are skipped, the others still count
    const std::vector<std::string> mixed = { "http://dev.lan:5173/app", "*.lan", "fd00::1", "ok.lan" };
    EXPECT_FALSE(OriginPolicy::accepts("http://dev.lan:5173", lan("x:5454"), {}, mixed));
    EXPECT_TRUE(OriginPolicy::accepts("http://[fd00::1]:5454", lan("x:5454"), {}, mixed));
    EXPECT_TRUE(OriginPolicy::accepts("http://ok.lan:5454", lan("x:5454"), {}, mixed));
}

//A non-loopback address of this machine, empty when it has none
std::string boxIp()
{
    for (const auto &ip: OriginPolicy::interfaceAddresses())
        if (ip.find(':') == std::string::npos)
            return ip;
    return std::string();
}

TEST(OriginPolicyMatrix, DecisionUnderAdminState)
{
    std::unordered_map<std::string, std::string> h;
    h["host"] = "192.168.1.10:5454";

    auto d = OriginPolicy::decide(h, "192.168.1.50", "192.168.1.50", true);
    EXPECT_EQ(OriginPolicy::Verdict::NoOrigin, d.verdict);
    EXPECT_FALSE(d.refused());
    EXPECT_FALSE(d.allowCors());

    h["origin"] = "http://evil.example";
    d = OriginPolicy::decide(h, "192.168.1.50", "192.168.1.50", false);
    EXPECT_EQ(OriginPolicy::Verdict::Refused, d.verdict);
    EXPECT_FALSE(d.refused()) << "without admin it is only logged";
    EXPECT_TRUE(d.allowCors()) << "without admin ACAO reflects as before";

    d = OriginPolicy::decide(h, "192.168.1.50", "192.168.1.50", true);
    EXPECT_TRUE(d.refused());
    EXPECT_FALSE(d.allowCors());

    const std::string ip = boxIp();
    if (ip.empty())
        GTEST_SKIP() << "no non-loopback IPv4 address here";
    h["origin"] = "http://" + ip + ":5454";
    h["host"] = ip + ":5454";
    d = OriginPolicy::decide(h, "192.168.1.50", "192.168.1.50", true);
    EXPECT_EQ(OriginPolicy::Verdict::Accepted, d.verdict);
    EXPECT_TRUE(d.allowCors());
}

/*
 * The real server
 */

struct Env
{
    int serverPort = 0;
    std::string cache;
} env;

bool pumpUntil(const std::function<bool()> &done, int ms = 5000)
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

void drainLoop(int ms)
{
    pumpUntil([]()
              {
        return false;
    },
              ms);
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

struct Reply
{
    int status = 0;
    std::string head;

    bool hasAcao() const { return head.find("\r\nAccess-Control-Allow-Origin:") != std::string::npos; }

    std::string acao() const
    {
        const std::string k = "\r\nAccess-Control-Allow-Origin: ";
        const auto p = head.find(k);
        if (p == std::string::npos)
            return std::string();
        const auto e = head.find("\r\n", p + k.size());
        return head.substr(p + k.size(), e - p - k.size());
    }
};

//Sends one request on a fresh loopback connection and reads the status line and headers
Reply exchange(const std::string &raw)
{
    Reply r;
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(env.serverPort);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0)
    {
        close(fd);
        return r;
    }

    send(fd, raw.data(), raw.size(), MSG_NOSIGNAL);
    std::string in;
    pumpUntil([&]()
              {
        char buf[8192];
        const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0)
            in.append(buf, (size_t)n);
        return in.find("\r\n\r\n") != std::string::npos || n == 0;
    });
    close(fd);
    drainLoop(20);

    const auto end = in.find("\r\n\r\n");
    r.head = in.substr(0, end == std::string::npos ? in.size() : end + 2);
    if (r.head.size() > 12)
        r.status = std::atoi(r.head.substr(9, 3).c_str());
    return r;
}

struct Headers
{
    std::string origin;
    bool withOrigin = false;
    std::string host;
    std::string xfp;
};

Headers from(const std::string &origin)
{
    Headers h;
    h.origin = origin;
    h.withOrigin = true;
    return h;
}

std::string common(const Headers &h)
{
    std::ostringstream o;
    o << "Host: " << (h.host.empty() ? "127.0.0.1:" + std::to_string(env.serverPort) : h.host) << "\r\n";
    if (h.withOrigin)
        o << "Origin: " << h.origin << "\r\n";
    if (!h.xfp.empty())
        o << "X-Forwarded-Proto: " << h.xfp << "\r\n";
    return o.str();
}

Reply wsHandshake(const Headers &h)
{
    std::ostringstream req;
    req << "GET /api HTTP/1.1\r\n"
        << common(h)
        << "Connection: Upgrade\r\n"
        << "Upgrade: websocket\r\n"
        << "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\n"
        << "Sec-WebSocket-Version: 13\r\n\r\n";
    return exchange(req.str());
}

//What calaos_installer sends: a POST with credentials, no Origin
Reply apiPost(const Headers &h)
{
    const std::string body = std::string("{\"action\":\"cb1_no_such_action\",\"cn_user\":\"") + kUser +
                             "\",\"cn_pass\":\"" + kPass + "\"}";
    std::ostringstream req;
    req << "POST /api.php HTTP/1.1\r\n"
        << common(h)
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    return exchange(req.str());
}

Reply preflight(const Headers &h)
{
    std::ostringstream req;
    req << "OPTIONS /api.php HTTP/1.1\r\n"
        << common(h)
        << "Access-Control-Request-Method: POST\r\n"
        << "Connection: close\r\n\r\n";
    return exchange(req.str());
}

std::string serverPort()
{
    return std::to_string(env.serverPort);
}

//The web app opened on http://<box IP>:5454
Headers fromBox()
{
    Headers h = from("http://" + boxIp() + ":" + serverPort());
    h.host = boxIp() + ":" + serverPort();
    return h;
}

Reply staticGet(const Headers &h, const std::string &path = "/app/index.html")
{
    std::ostringstream req;
    req << "GET " << path << " HTTP/1.1\r\n"
        << common(h)
        << "Connection: close\r\n\r\n";
    return exchange(req.str());
}

std::string flagPath()
{
    return env.cache + "/" + AdminState::FlagFileName;
}

class OriginServerTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        Utils::del_config_option("api_allowed_hosts");
        ::unlink(flagPath().c_str());
        AdminState::Instance().observeUnreachable();
        AdminState::Instance().loadFlag();
        ASSERT_EQ(AdminState::Mode::Unavailable, AdminState::Instance().mode());
    }

    void TearDown() override
    {
        Utils::del_config_option("api_allowed_hosts");
        drainLoop(20);
    }

    void adminSet()
    {
        AdminState::Instance().observe(true);
        ASSERT_TRUE(AdminState::Instance().enforced());
    }

    //Sticky flag, daemon gone
    void closedState()
    {
        {
            std::ofstream f(flagPath());
            f << "1\n";
        }
        AdminState::Instance().observeUnreachable();
        AdminState::Instance().loadFlag();
        ASSERT_EQ(AdminState::Mode::Closed, AdminState::Instance().mode());
    }

    void expectRefused(const Headers &h)
    {
        const Reply ws = wsHandshake(h);
        EXPECT_EQ(403, ws.status) << ws.head;
        EXPECT_FALSE(ws.hasAcao()) << ws.head;

        const Reply post = apiPost(h);
        EXPECT_EQ(403, post.status) << post.head;
        EXPECT_FALSE(post.hasAcao()) << post.head;

        const Reply pre = preflight(h);
        EXPECT_EQ(403, pre.status) << pre.head;
        EXPECT_FALSE(pre.hasAcao()) << pre.head;
    }

    void expectAccepted(const Headers &h)
    {
        const Reply ws = wsHandshake(h);
        EXPECT_EQ(101, ws.status) << ws.head;
        EXPECT_EQ(h.origin, ws.acao()) << ws.head;

        const Reply post = apiPost(h);
        EXPECT_EQ(200, post.status) << post.head;
        EXPECT_EQ(h.origin, post.acao()) << post.head;
        EXPECT_NE(std::string::npos, post.head.find("\r\nAccess-Control-Allow-Credentials: false\r\n")) << post.head;
        EXPECT_NE(std::string::npos, post.head.find("\r\nVary: Origin\r\n")) << post.head;

        const Reply pre = preflight(h);
        EXPECT_EQ(200, pre.status) << pre.head;
        EXPECT_EQ(h.origin, pre.acao()) << pre.head;
    }
};

TEST_F(OriginServerTest, WithoutAdminNothingChanges)
{
    for (const auto mode: { 0, 1 })
    {
        if (mode == 1)
        {
            AdminState::Instance().observe(false);
            ASSERT_EQ(AdminState::Mode::NotSet, AdminState::Instance().mode());
        }

        Headers h = from("http://evil.example");
        const Reply ws = wsHandshake(h);
        EXPECT_EQ(101, ws.status) << ws.head;
        EXPECT_EQ("*", ws.acao()) << ws.head;

        const Reply post = apiPost(h);
        EXPECT_EQ(200, post.status) << post.head;
        EXPECT_EQ("http://evil.example", post.acao()) << post.head;

        h.origin = "null";
        EXPECT_EQ(101, wsHandshake(h).status);
        EXPECT_EQ("null", apiPost(h).acao());
    }
}

TEST_F(OriginServerTest, AdminSetRefusesForeignNullAndRebound)
{
    adminSet();

    expectRefused(from("http://evil.example"));
    expectRefused(from("null"));
    expectRefused(from("garbage"));

    Headers rebound = from("http://rebind.attacker.example:" + std::to_string(env.serverPort));
    rebound.host = "rebind.attacker.example:" + std::to_string(env.serverPort);
    expectRefused(rebound);

    rebound.xfp = "http";
    expectRefused(rebound);
}

TEST_F(OriginServerTest, AdminSetAcceptsBoxIp)
{
    if (boxIp().empty())
        GTEST_SKIP() << "no non-loopback IPv4 address here";
    adminSet();
    expectAccepted(fromBox());

    Headers otherPort = fromBox();
    otherPort.origin = "http://" + boxIp() + ":3000";
    expectRefused(otherPort);
}

TEST_F(OriginServerTest, AdminSetRefusesLoopbackOrigin)
{
    adminSet();
    expectRefused(from("http://127.0.0.1:" + serverPort()));
}

//Chromium sends Origin with the module scripts and fonts of the web app
TEST_F(OriginServerTest, StaticFilesAreNeverRefused)
{
    adminSet();

    const Reply page = staticGet(from("http://evil.example"));
    EXPECT_EQ(200, page.status) << page.head;
    EXPECT_FALSE(page.hasAcao()) << page.head;

    const Reply missing = staticGet(from("http://evil.example"), "/app/missing.js");
    EXPECT_EQ(404, missing.status) << missing.head;

    std::ostringstream req;
    req << "OPTIONS /app/index.html HTTP/1.1\r\n"
        << common(from("http://evil.example"))
        << "Access-Control-Request-Method: GET\r\n"
        << "Connection: close\r\n\r\n";
    const Reply pre = exchange(req.str());
    EXPECT_EQ(200, pre.status) << pre.head;
    EXPECT_FALSE(pre.hasAcao()) << pre.head;

    //the WS upgrade is checked on any path
    std::ostringstream ws;
    ws << "GET /echo HTTP/1.1\r\n"
       << common(from("http://evil.example"))
       << "Connection: Upgrade\r\nUpgrade: websocket\r\n"
       << "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\nSec-WebSocket-Version: 13\r\n\r\n";
    EXPECT_EQ(403, exchange(ws.str()).status);
}

TEST_F(OriginServerTest, StaticFilesWithoutAdminUnchanged)
{
    const Reply page = staticGet(from("http://evil.example"));
    EXPECT_EQ(200, page.status) << page.head;
    EXPECT_EQ("http://evil.example", page.acao()) << page.head;
}

TEST_F(OriginServerTest, AdminSetAcceptsHostUnderHaproxyHttps)
{
    adminSet();

    Headers h = from("https://box.ddns.example");
    h.host = "box.ddns.example";
    h.xfp = "https";
    expectAccepted(h);

    h.xfp = "";
    expectRefused(h);
}

TEST_F(OriginServerTest, AdminSetAcceptsAllowedHosts)
{
    adminSet();

    Headers h = from("http://box.local:5173");
    expectRefused(h);

    Utils::set_config_option("api_allowed_hosts", "calaos.example.net, box.local");
    expectAccepted(h);
}

TEST_F(OriginServerTest, NativeClientsWithoutOrigin)
{
    adminSet();

    Headers h;
    const Reply ws = wsHandshake(h);
    EXPECT_EQ(101, ws.status) << ws.head;
    EXPECT_FALSE(ws.hasAcao()) << ws.head;

    const Reply post = apiPost(h);
    EXPECT_EQ(200, post.status) << post.head;
    EXPECT_FALSE(post.hasAcao()) << post.head;
}

TEST_F(OriginServerTest, StickyFlagFailsClosed)
{
    closedState();
    expectRefused(from("http://evil.example"));
    if (!boxIp().empty())
        expectAccepted(fromBox());

    EXPECT_EQ(101, wsHandshake(Headers()).status);
}

TEST_F(OriginServerTest, NoFlagDaemonUnreachableStaysOpen)
{
    ASSERT_EQ(AdminState::Mode::Unavailable, AdminState::Instance().mode());
    const Reply post = apiPost(from("http://evil.example"));
    EXPECT_EQ(200, post.status) << post.head;
    EXPECT_EQ("http://evil.example", post.acao());
}

//Anyone on the LAN can send endless distinct Origins before the bootstrap.
//Last suite: once the log set is full, the other tests log nothing.
TEST(OriginPolicyLog, RefusedOriginLogIsBounded)
{
    std::unordered_map<std::string, std::string> h;
    h["host"] = "192.168.1.10:5454";
    for (int i = 0; i < 100; i++)
    {
        h["origin"] = "http://flood" + std::to_string(i) + ".example";
        const auto d = OriginPolicy::decide(h, "127.0.0.1", "192.168.1.50", false);
        EXPECT_EQ(OriginPolicy::Verdict::Refused, d.verdict);
        EXPECT_FALSE(d.refused());
    }
}

} //namespace

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_cb1_origin_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
        return 1;

    const std::string cfg = std::string(base) + "/config";
    env.cache = std::string(base) + "/cache";
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(env.cache.c_str(), 0700);

    Utils::initConfigOptions(const_cast<char *>(cfg.c_str()), const_cast<char *>(env.cache.c_str()), true);
    Utils::set_config_option("listen_address", "127.0.0.1");
    Utils::set_config_option("cn_user", kUser);
    Utils::set_config_option("cn_pass", kPass);

    const std::string www = std::string(base) + "/www";
    ::mkdir(www.c_str(), 0700);
    std::ofstream(www + "/index.html") << "<html></html>\n";
    Utils::set_config_option("wwwroot", www);

    env.serverPort = pickFreePort();
    HttpServer::Instance(env.serverPort);
    drainLoop(50);

    const int rc = RUN_ALL_TESTS();

    drainLoop(50);
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    return rc;
}
