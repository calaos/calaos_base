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
 * THE ONE PRIVILEGE GATE THAT READS THE PEER ADDRESS.
 *
 * ---------------------------------------------------------------------------
 * WHY A SUITE OF ITS OWN
 * ---------------------------------------------------------------------------
 * Six decisions of this tree are keyed on the peer address; five of them are
 * buckets - a wrong reading tightens a limit or merges two counters. This one
 * is not a bucket: it decides whether an unauthenticated caller may make the
 * server re-read its firmware directory. It is the only place where reading
 * the peer wrong GRANTS something, and it was the argument that settled how a
 * mapped peer is spelled - while no case reached it at all.
 *
 * So nothing here asserts on a string. Every case sends a real POST to
 * /api/v3/ota/rescan over a real socket, from a source address the suite bound
 * itself, and reads the HTTP status the gate produced.
 *
 * ---------------------------------------------------------------------------
 * THE FOUR SOURCES, AND WHAT EACH ONE IS FOR
 * ---------------------------------------------------------------------------
 *   ::1          - the IPv6 loopback, on a dual-stack listen;
 *   127.0.0.1    - reaching the same listen, so the peer is IPv4-mapped: the
 *                  branch that only became live once mapped peers were unmapped;
 *   127.1.0.1    - loopback per RFC 1122, which is 127.0.0.0/8 ENTIRE. The two
 *                  loopback rules of this tree used to disagree here: the
 *                  trusted-proxy rule said /8, this gate said /24;
 *   an address of a real interface - the counterweight. Every other case says
 *                  "accepted"; a gate stuck open would pass all three of them.
 *
 * WHAT THIS SUITE ASSUMES OF THE MACHINE, out loud, because a case green for
 * the wrong reason is worse than a red one, and main() skips (77) rather than
 * let a case assert on nothing:
 *   - the kernel has IPv6 and a dual-stack socket can be bound;
 *   - net.ipv6.bindv6only is 0, so the `::` listen also takes IPv4 - checked,
 *     not assumed: the fixture case reads the local end of every client socket;
 *   - 127.0.0.0/8 is local in its entirety, so 127.1.0.1 can be bound;
 *   - at least one interface carries a non-loopback IPv4 address. This is the
 *     only source that measures a REFUSAL, and a container without one cannot
 *     host that half.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
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
#include "OtaFirmwareManager.h"
#include "libuvw.h"

using namespace std::chrono;

namespace
{

int &serverPort()
{
    static int p = 0;
    return p;
}

//An IPv4 address of a real interface of this machine. Empty when there is
//none, which main() turns into a skip.
std::string &lanAddress()
{
    static std::string ip;
    return ip;
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

struct Conn
{
    int fd = -1;
    bool connected = false;
    //Read off the socket, not assumed: this is the fixture check.
    std::string local;
    int status = 0;
    std::string body;
    std::string log;
};

void readLocalEnd(Conn &c)
{
    sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    memset(&ss, 0, sizeof(ss));
    if (getsockname(c.fd, reinterpret_cast<sockaddr *>(&ss), &len) != 0)
        return;

    char buf[INET6_ADDRSTRLEN] = { 0 };
    if (ss.ss_family == AF_INET6)
    {
        const sockaddr_in6 *a = reinterpret_cast<const sockaddr_in6 *>(&ss);
        if (inet_ntop(AF_INET6, &a->sin6_addr, buf, sizeof(buf)))
            c.local = buf;
    }
    else if (ss.ss_family == AF_INET)
    {
        const sockaddr_in *a = reinterpret_cast<const sockaddr_in *>(&ss);
        if (inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf)))
            c.local = buf;
    }
}

//An IPv4 client bound to `source`, reaching `dest`: the dual-stack listen sees
//an AF_INET6 peer carrying an IPv4-mapped address.
Conn openFrom(const std::string &source, const std::string &dest)
{
    Conn c;
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return c;

    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = 0;
    if (inet_pton(AF_INET, source.c_str(), &a.sin_addr) != 1 ||
        bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0)
    {
        close(fd);
        return c;
    }

    sockaddr_in d;
    memset(&d, 0, sizeof(d));
    d.sin_family = AF_INET;
    d.sin_port = htons(serverPort());
    if (inet_pton(AF_INET, dest.c_str(), &d.sin_addr) != 1 ||
        connect(fd, reinterpret_cast<sockaddr *>(&d), sizeof(d)) != 0)
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
    const std::string::size_type sp = response.find(' ');
    if (sp == std::string::npos || response.compare(0, 5, "HTTP/") != 0)
        return 0;
    return atoi(response.c_str() + sp + 1);
}

//One complete rescan request, and the answer the gate produced.
void rescan(Conn &c)
{
    if (c.fd < 0)
        return;

    const std::string raw =
        "POST /api/v3/ota/rescan HTTP/1.1\r\n"
        "Host: calaos.test\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

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

            if (response.find("\r\n\r\n") != std::string::npos &&
                response.find('}') != std::string::npos)
                break;

            std::this_thread::sleep_for(milliseconds(1));
        }
        drainLoop(50);
    });

    c.status = statusOf(response);
    const std::string::size_type head = response.find("\r\n\r\n");
    if (head != std::string::npos)
        c.body = response.substr(head + 4);
}

void shut(Conn &c)
{
    if (c.fd >= 0)
        close(c.fd);
    c.fd = -1;
    drainLoop(200);
}

/*
 * EVERY EXCHANGE OF THIS SUITE, RUN ONCE AND IN ORDER. Each source speaks
 * alone and its socket is closed before the next one opens: the answer of one
 * gate call must not depend on a connection another case left behind.
 */
struct Runs
{
    Conn v6;          //::1
    Conn mapped;      //127.0.0.1, IPv4-mapped on the dual-stack listen
    Conn slashEight;  //127.1.0.1, loopback per RFC 1122 but not per a /24
    Conn outside;     //a real interface address: the refusal
};

const Runs &theRuns()
{
    static Runs runs = []()
    {
        Runs r;

        r.v6 = openV6();
        rescan(r.v6);
        shut(r.v6);

        r.mapped = openFrom("127.0.0.1", "127.0.0.1");
        rescan(r.mapped);
        shut(r.mapped);

        r.slashEight = openFrom("127.1.0.1", "127.0.0.1");
        rescan(r.slashEight);
        shut(r.slashEight);

        r.outside = openFrom(lanAddress(), lanAddress());
        rescan(r.outside);
        shut(r.outside);

        return r;
    }();
    return runs;
}

//The rescan really ran, as opposed to a 200 produced by anything else on the
//way.
const char *const kDone = "Firmware rescan completed";

} //namespace

/*
 * THE FIXTURE ITSELF, BEFORE ANYTHING IS CONCLUDED FROM IT. Three of the four
 * sources differ only by the address their client end was bound to. A kernel
 * that had ignored one of those binds would make this suite measure the same
 * peer four times, and every case below would still be green.
 */
TEST(OtaRescanGate, TheClientsSpeakFromTheAddressesTheSuiteClaims)
{
    const Runs &r = theRuns();

    EXPECT_TRUE(r.v6.connected);
    EXPECT_TRUE(r.mapped.connected);
    EXPECT_TRUE(r.slashEight.connected);
    EXPECT_TRUE(r.outside.connected);

    EXPECT_EQ("::1", r.v6.local);
    EXPECT_EQ("127.0.0.1", r.mapped.local);
    EXPECT_EQ("127.1.0.1", r.slashEight.local);
    EXPECT_EQ(lanAddress(), r.outside.local);

    //An exchange that timed out reads 0, and a case comparing it to 403 would
    //be reading a timeout for a refusal.
    EXPECT_NE(0, r.v6.status);
    EXPECT_NE(0, r.mapped.status);
    EXPECT_NE(0, r.slashEight.status);
    EXPECT_NE(0, r.outside.status);
}

TEST(OtaRescanGate, TheRescanIsAcceptedFromTheIpv6Loopback)
{
    const Runs &r = theRuns();

    EXPECT_EQ(200, r.v6.status);
    EXPECT_NE(std::string::npos, r.v6.body.find(kDone));
}

TEST(OtaRescanGate, TheRescanIsAcceptedFromTheMappedLoopback)
{
    const Runs &r = theRuns();

    EXPECT_EQ(200, r.mapped.status);
    EXPECT_NE(std::string::npos, r.mapped.body.find(kDone));
}

/*
 * The loopback is 127.0.0.0/8, and both rules of this tree now say so. A gate
 * on /24 called this peer a stranger while the trusted-proxy rule beside it
 * called the same peer the loopback.
 */
TEST(OtaRescanGate, TheRescanIsAcceptedFromTheWholeLoopbackBlock)
{
    const Runs &r = theRuns();

    EXPECT_EQ(200, r.slashEight.status);
    EXPECT_NE(std::string::npos, r.slashEight.body.find(kDone));
}

/*
 * THE COUNTERWEIGHT, and the only case that measures the gate closing.
 */
TEST(OtaRescanGate, TheRescanIsRefusedFromOutsideTheLoopback)
{
    const Runs &r = theRuns();

    EXPECT_EQ(403, r.outside.status);
    EXPECT_EQ(std::string::npos, r.outside.body.find(kDone));
    EXPECT_NE(std::string::npos,
              r.outside.log.find("Rescan request rejected from non-localhost: " +
                                 lanAddress()));
}

/*
 * Own main instead of gtest_main: the listening socket and the firmware
 * manager have to exist before any case runs, and the level has to be raised
 * before the first log line of the process.
 */
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_otagate_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
    {
        std::cerr << "could not create a config directory" << std::endl;
        return 1;
    }

    const std::string cfg = std::string(base) + "/config";
    const std::string cache = std::string(base) + "/cache";
    const std::string firmwares = std::string(base) + "/firmwares";
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(cache.c_str(), 0700);
    ::mkdir(firmwares.c_str(), 0700);

    Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                             const_cast<char *>(cache.c_str()), true);
    Utils::set_config_option("debug_level", "5");
    //One dual-stack listen, so that both an IPv6 and an IPv4-mapped peer reach
    //the same gate.
    Utils::set_config_option("listen_address", "::");
    Utils::set_config_option("ota_enabled", "true");
    Utils::set_config_option("ota_firmware_path", firmwares);

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

    //The refusal half needs a peer that is not the loopback, and this machine
    //is the only source of one.
    ifaddrs *ifa = nullptr;
    if (getifaddrs(&ifa) == 0)
    {
        for (ifaddrs *it = ifa; it && lanAddress().empty(); it = it->ifa_next)
        {
            if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET)
                continue;
            if ((it->ifa_flags & IFF_UP) == 0 || (it->ifa_flags & IFF_LOOPBACK))
                continue;

            char buf[INET_ADDRSTRLEN] = { 0 };
            const sockaddr_in *a = reinterpret_cast<const sockaddr_in *>(it->ifa_addr);
            if (inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf)))
                lanAddress() = buf;
        }
        freeifaddrs(ifa);
    }

    if (lanAddress().empty())
    {
        std::cerr << "no non-loopback IPv4 address on this machine, the refusal "
                     "half cannot be measured" << std::endl;
        return 77;
    }

    OtaFirmwareManager::Instance().init();
    HttpServer::Instance(serverPort());
    drainLoop(50);

    return RUN_ALL_TESTS();
}
