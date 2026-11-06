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

/*******************************************************************************
 * WHICH CLIENT THE SERVER THINKS IT IS TALKING TO, WHEN THE PEER IS NOT IPv4.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS IS NOT A DISPLAY QUESTION
 * ---------------------------------------------------------------------------
 * The peer address is the identity of six security decisions: the per-source
 * connection cap, the login backoff of both JSON API transports, the throttle
 * of the MCP sidecar, the provisioning rate limit and its blacklist, the HMAC
 * authentication rate limit, and the localhost gate of the OTA rescan. Read
 * the same for two different clients, it is not a wrong label - it is one
 * shared bucket, and one client's backoff becomes everybody's.
 *
 * So nothing here asserts a string for its own sake. Every case drives the
 * per-source cap, set to one, and asks a behaviour: does the second client get
 * in, or is it refused as if it were the first? The refusal line is read only
 * afterwards, to pin the spelling the identity is rendered in.
 *
 * ---------------------------------------------------------------------------
 * WHY THE SERVER IS BOUND TO `::`
 * ---------------------------------------------------------------------------
 * `listen_address = "::"` is a legal value of a documented key, and it is the
 * only configuration under which a peer of this tree is not AF_INET. One
 * dual-stack listening socket gives three peers that are all AF_INET6 and all
 * distinct: `::1` over IPv6, and two IPv4-mapped ones from 127.0.0.1 and
 * 127.0.0.2 - 127.0.0.0/8 being local in its entirety.
 *
 * WHAT THIS SUITE ASSUMES OF THE MACHINE, and it assumes it out loud because a
 * case that is green for the wrong reason is worse than a red one:
 *   - the kernel has IPv6 and `::1` on the loopback;
 *   - `net.ipv6.bindv6only` is 0, so the `::` socket also accepts IPv4;
 *   - 127.0.0.0/8 is local, so a client can be bound to 127.0.0.2.
 * The first case verifies the second and third by reading the LOCAL end of
 * every client socket: a test that believes it connected from 127.0.0.2 and
 * actually came from 127.0.0.1 would call a shared bucket a private one. If
 * the first assumption does not hold the listening socket cannot be created
 * and main() says so and skips, rather than letting cases pass on nothing.
 *
 * WHAT THIS DOES NOT PROVE: no IPv4 listen is exercised here, so the absence
 * of a regression on the shipped `listen_address = "0.0.0.0"` is carried by
 * the suites that already bind it, not by this one; and no peer outside the
 * loopback is produced, so the refusal half of the trusted-proxy rule stays
 * where it is tested as a pure function.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "HttpServer.h"
#include "Logger.h"
#include "StringUtils.h"
#include "libuvw.h"

using namespace std::chrono;

namespace
{

//The two forwarded identities. Neither is a loopback address, so a line
//carrying one can only come from a header that was believed.
const char *const kForwardedOverV6 = "198.51.100.9";
const char *const kForwardedOverV4 = "198.51.100.10";

int &serverPort()
{
    static int p = 0;
    return p;
}

void drainLoop(int ms)
{
    auto loop = uvw::Loop::getDefault();
    auto deadline = steady_clock::now() + milliseconds(ms);
    while (steady_clock::now() < deadline)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(milliseconds(1));
    }
}

std::string captureStdout(const std::function<void()> &fn)
{
    std::ostringstream sink;
    std::streambuf *saved = std::cout.rdbuf(sink.rdbuf());
    fn();
    std::cout.rdbuf(saved);
    return sink.str();
}

//The client end of one connection, kept open: the cap counts a source only as
//long as its connections live.
struct Conn
{
    int fd = -1;
    //Kept after the socket is closed: fd is reset then, and the fixture check
    //runs once every group has been closed.
    bool connected = false;
    //Read off the socket, not assumed: this is the fixture check.
    std::string local;
    int status = 0;
    std::string log;
};

std::string textOf(const sockaddr_storage &ss)
{
    char buf[INET6_ADDRSTRLEN] = { 0 };
    if (ss.ss_family == AF_INET6)
    {
        const sockaddr_in6 *a = reinterpret_cast<const sockaddr_in6 *>(&ss);
        if (!inet_ntop(AF_INET6, &a->sin6_addr, buf, sizeof(buf)))
            return std::string();
    }
    else if (ss.ss_family == AF_INET)
    {
        const sockaddr_in *a = reinterpret_cast<const sockaddr_in *>(&ss);
        if (!inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf)))
            return std::string();
    }
    else
        return std::string();
    return std::string(buf);
}

void readLocalEnd(Conn &c)
{
    sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    memset(&ss, 0, sizeof(ss));
    if (getsockname(c.fd, reinterpret_cast<sockaddr *>(&ss), &len) == 0)
        c.local = textOf(ss);
}

//An IPv4 client bound to `source`, reaching the dual-stack socket: the server
//sees an AF_INET6 peer carrying an IPv4-mapped address.
Conn openMapped(const char *source)
{
    Conn c;
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return c;

    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = 0;
    if (inet_pton(AF_INET, source, &a.sin_addr) != 1 ||
        bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0)
    {
        close(fd);
        return c;
    }

    sockaddr_in d;
    memset(&d, 0, sizeof(d));
    d.sin_family = AF_INET;
    d.sin_port = htons(serverPort());
    d.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr *>(&d), sizeof(d)) != 0)
    {
        close(fd);
        return c;
    }

    c.fd = fd;
    c.connected = true;
    readLocalEnd(c);
    return c;
}

Conn openV6()
{
    Conn c;
    const int fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0)
        return c;

    sockaddr_in6 d;
    memset(&d, 0, sizeof(d));
    d.sin6_family = AF_INET6;
    d.sin6_port = htons(serverPort());
    d.sin6_addr = in6addr_loopback;
    if (connect(fd, reinterpret_cast<sockaddr *>(&d), sizeof(d)) != 0)
    {
        close(fd);
        return c;
    }

    c.fd = fd;
    c.connected = true;
    readLocalEnd(c);
    return c;
}

int statusOf(const std::string &response)
{
    //"HTTP/1.1 429 Too Many Requests"
    const std::string::size_type sp = response.find(' ');
    if (sp == std::string::npos || response.compare(0, 5, "HTTP/") != 0)
        return 0;
    return atoi(response.c_str() + sp + 1);
}

//Sends one complete request and pumps the loop until the answer comes back.
//The socket stays open: closing it would release the source from the cap.
void speak(Conn &c, const std::string &forwardedFor)
{
    if (c.fd < 0)
        return;

    std::ostringstream req;
    req << "GET /t3-41-sonde HTTP/1.1\r\n"
        << "Host: calaos.test\r\n";
    if (!forwardedFor.empty())
        req << "X-Forwarded-For: " << forwardedFor << "\r\n";
    req << "\r\n";
    const std::string raw = req.str();

    std::string response;
    c.log = captureStdout([&]()
    {
        if (send(c.fd, raw.data(), raw.size(), MSG_NOSIGNAL) < 0)
            return;

        auto loop = uvw::Loop::getDefault();
        const auto deadline = steady_clock::now() + seconds(5);
        while (steady_clock::now() < deadline)
        {
            loop->run<uvw::Loop::Mode::NOWAIT>();

            char buf[4096];
            const ssize_t n = recv(c.fd, buf, sizeof(buf), MSG_DONTWAIT);
            if (n > 0)
                response.append(buf, (size_t)n);

            if (response.find("\r\n\r\n") != std::string::npos)
                break;

            std::this_thread::sleep_for(milliseconds(1));
        }
        drainLoop(50);
    });

    c.status = statusOf(response);
}

void shut(std::vector<Conn *> conns)
{
    for (Conn *c: conns)
    {
        if (c->fd >= 0)
            close(c->fd);
        c->fd = -1;
    }
    drainLoop(200);
}

/*
 * EVERY EXCHANGE OF THIS SUITE, RUN ONCE AND IN ORDER.
 *
 * Each group opens its connections, speaks, then closes them and lets the loop
 * see the closes: the cap counts live connections, so a group that left one
 * open would decide the verdict of the next.
 */
struct Runs
{
    Conn capA, capB;                  //127.0.0.2 twice
    Conn mixV6, mixV4;                //::1, then 127.0.0.2
    Conn v6A, v6B;                    //::1 twice
    Conn xffV6A, xffV6B, xffV6C;      //::1 bare, then ::1 forwarded twice
    Conn xffV4A, xffV4B;              //127.0.0.2 bare, then 127.0.0.2 forwarded
};

const Runs &theRuns()
{
    static Runs runs = []()
    {
        Runs r;

        r.capA = openMapped("127.0.0.2");
        r.capB = openMapped("127.0.0.2");
        speak(r.capA, "");
        speak(r.capB, "");
        shut({ &r.capA, &r.capB });

        r.mixV6 = openV6();
        r.mixV4 = openMapped("127.0.0.2");
        speak(r.mixV6, "");
        speak(r.mixV4, "");
        shut({ &r.mixV6, &r.mixV4 });

        r.v6A = openV6();
        r.v6B = openV6();
        speak(r.v6A, "");
        speak(r.v6B, "");
        shut({ &r.v6A, &r.v6B });

        r.xffV6A = openV6();
        r.xffV6B = openV6();
        r.xffV6C = openV6();
        speak(r.xffV6A, "");
        speak(r.xffV6B, kForwardedOverV6);
        speak(r.xffV6C, kForwardedOverV6);
        shut({ &r.xffV6A, &r.xffV6B, &r.xffV6C });

        r.xffV4A = openMapped("127.0.0.2");
        r.xffV4B = openMapped("127.0.0.2");
        speak(r.xffV4A, "");
        speak(r.xffV4B, kForwardedOverV4);
        shut({ &r.xffV4A, &r.xffV4B });

        return r;
    }();
    return runs;
}

} //namespace

/*
 * THE FIXTURE ITSELF, BEFORE ANYTHING IS CONCLUDED FROM IT.
 *
 * Two of the three peers this suite calls distinct differ only by the address
 * their client end was bound to. If the kernel had ignored that bind, or if
 * 127.0.0.2 were not local, every case below would still be green while
 * measuring one peer twice.
 */
TEST(PeerAddressFamily, TheClientsConnectFromTheAddressesTheSuiteClaims)
{
    const Runs &r = theRuns();

    EXPECT_TRUE(r.capA.connected && r.mixV6.connected && r.v6A.connected)
            << "a client of this suite never reached the server";

    EXPECT_EQ("127.0.0.2", r.capA.local);
    EXPECT_EQ("127.0.0.2", r.capB.local);
    EXPECT_EQ("127.0.0.2", r.mixV4.local);
    EXPECT_EQ("127.0.0.2", r.xffV4A.local);
    EXPECT_EQ("127.0.0.2", r.xffV4B.local);

    EXPECT_EQ("::1", r.mixV6.local);
    EXPECT_EQ("::1", r.v6A.local);
    EXPECT_EQ("::1", r.v6B.local);
    EXPECT_EQ("::1", r.xffV6A.local);

    //Every exchange got an answer: a case reading status 0 would be reading a
    //timeout, not a decision.
    EXPECT_NE(0, r.capA.status);
    EXPECT_NE(0, r.capB.status);
    EXPECT_NE(0, r.mixV6.status);
    EXPECT_NE(0, r.mixV4.status);
    EXPECT_NE(0, r.v6A.status);
    EXPECT_NE(0, r.v6B.status);
    EXPECT_NE(0, r.xffV6A.status);
    EXPECT_NE(0, r.xffV6B.status);
    EXPECT_NE(0, r.xffV6C.status);
    EXPECT_NE(0, r.xffV4A.status);
    EXPECT_NE(0, r.xffV4B.status);
}

/*
 * THE COUNTERWEIGHT. Every other case asserts that a second client is NOT
 * refused; a cap that never fires would grant them all. This one shows the cap
 * armed at one, on a source that really is the same twice.
 */
TEST(PeerAddressFamily, TheCapStillRefusesTheSameSourceTwice)
{
    const Runs &r = theRuns();

    EXPECT_NE(429, r.capA.status);
    EXPECT_EQ(429, r.capB.status);
}

TEST(PeerAddressFamily, TwoDistinctIpv6PeersDoNotShareOneBucket)
{
    const Runs &r = theRuns();

    EXPECT_NE(429, r.mixV6.status);
    EXPECT_NE(429, r.mixV4.status)
            << "an IPv4-mapped peer was counted in the bucket of an IPv6 one";
}

/*
 * THE SPELLING, read where the identity is published rather than where it is
 * computed. `Client <id> already has` pins it whole: no brackets around the
 * literal, no port glued to it, no zone.
 */
TEST(PeerAddressFamily, TheRefusalNamesTheMappedPeerAsItsDottedQuad)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.capB.log.find("Client 127.0.0.2 already has"));
    EXPECT_EQ(std::string::npos, r.capB.log.find("Client 0.0.0.0"));
}

TEST(PeerAddressFamily, TheRefusalNamesAnIpv6PeerByItsLiteral)
{
    const Runs &r = theRuns();

    EXPECT_NE(429, r.v6A.status);
    EXPECT_EQ(429, r.v6B.status);
    EXPECT_NE(std::string::npos, r.v6B.log.find("Client ::1 already has"));
    EXPECT_EQ(std::string::npos, r.v6B.log.find("Client 0.0.0.0"));
}

/*
 * THE TRUSTED PROXY RULE, ON THE TWO SPELLINGS A TCP PEER OF A DUAL-STACK
 * LISTEN CAN CARRY. Believing the forwarded line is what gives the second
 * connection an identity of its own; refusing it would put both in the bucket
 * of the peer and answer 429.
 */
TEST(PeerAddressFamily, TheIpv6LoopbackPeerIsTrustedWithItsForwardedLine)
{
    const Runs &r = theRuns();

    EXPECT_NE(429, r.xffV6A.status);
    EXPECT_NE(429, r.xffV6B.status)
            << "the forwarded line of a ::1 peer was not believed";
    EXPECT_EQ(429, r.xffV6C.status);
    EXPECT_NE(std::string::npos,
              r.xffV6C.log.find(std::string("Client ") + kForwardedOverV6 +
                                " already has"));
}

TEST(PeerAddressFamily, TheMappedLoopbackPeerIsTrustedWithItsForwardedLine)
{
    const Runs &r = theRuns();

    EXPECT_NE(429, r.xffV4A.status);
    EXPECT_NE(429, r.xffV4B.status)
            << "the forwarded line of an IPv4-mapped loopback peer was not believed";
}

/*
 * Own main instead of gtest_main: the listening socket has to exist before any
 * case runs, the per-source cap has to be set before the first read of the
 * option (it is cached once), and the level has to be raised before the first
 * log line of the process.
 */
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_peerfamily_cfg_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
    {
        std::cerr << "could not create a config directory" << std::endl;
        return 1;
    }

    const std::string cfg = std::string(base) + "/config";
    const std::string cache = std::string(base) + "/cache";
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(cache.c_str(), 0700);

    Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                             const_cast<char *>(cache.c_str()), true);
    Utils::set_config_option("debug_level", "5");
    //The whole point of the suite: the one listen address under which a peer
    //of this tree is not AF_INET.
    Utils::set_config_option("listen_address", "::");
    //One connection per source, so that the second connection of a source is a
    //refusal and the second connection of another source is not.
    Utils::set_config_option("max_connections_per_ip", "1");

    //A kernel without IPv6 cannot host this measurement at all. Said out loud
    //rather than left to cases that would then assert on nothing.
    const int probe = socket(AF_INET6, SOCK_STREAM, 0);
    if (probe < 0)
    {
        std::cerr << "no IPv6 on this machine, nothing to measure" << std::endl;
        return 77;
    }
    sockaddr_in6 pa;
    memset(&pa, 0, sizeof(pa));
    pa.sin6_family = AF_INET6;
    pa.sin6_addr = in6addr_any;
    pa.sin6_port = 0;
    int port = 0;
    if (bind(probe, reinterpret_cast<sockaddr *>(&pa), sizeof(pa)) == 0)
    {
        socklen_t len = sizeof(pa);
        if (getsockname(probe, reinterpret_cast<sockaddr *>(&pa), &len) == 0)
            port = ntohs(pa.sin6_port);
    }
    close(probe);

    if (port <= 0)
    {
        std::cerr << "could not reserve a dual-stack port" << std::endl;
        return 77;
    }
    serverPort() = port;

    HttpServer::Instance(serverPort());
    drainLoop(50);

    return RUN_ALL_TESTS();
}
