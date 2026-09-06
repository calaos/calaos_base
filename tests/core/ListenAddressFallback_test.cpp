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
 * WHERE THE SERVER ACTUALLY LISTENS, WHEN `listen_address` IS NOT USABLE.
 *
 * ---------------------------------------------------------------------------
 * THE THREE FORMS, AND WHY THE THIRD IS THE WORST
 * ---------------------------------------------------------------------------
 * `listen_address` is the key an operator writes to CONFINE the server to one
 * network. Three values it can hold are not the address the server ends up on:
 *
 *   - an IPv6 literal. Bound with uvw's default (IPv4) template it landed on
 *     0.0.0.0: a listen written to narrow, widened to every interface. Closed
 *     before this suite existed, kept here so it stays closed.
 *   - a value that is neither family - a typo, a host name. uv_ip4_addr()
 *     zeroes its output BEFORE reporting it could not read the literal, and
 *     uvw drops that return code: same widening, silently.
 *   - an IPv4 or IPv6 literal that is well formed but ABSENT from the machine.
 *     bind() fails, and the failure is published before anything listens for
 *     it; listen() then auto-binds the handle on the wildcard address AND AN
 *     EPHEMERAL PORT. The server is not only open to every interface, it is
 *     on a port nobody chose - its owner included. "It stopped answering" is
 *     the whole symptom, and no line explained it.
 *
 * ---------------------------------------------------------------------------
 * WHY EACH CASE IS A SEPARATE PROCESS
 * ---------------------------------------------------------------------------
 * HttpServer is a singleton bound once per process, so one process measures
 * one value. Each case is therefore a fork() that configures itself, builds
 * the server, and reports what it got. Ports are reserved by the parent and
 * handed down, so "the port that was asked for" is a number this suite chose.
 *
 * What is reported is read off the DESCRIPTOR - the listening socket is found
 * by SO_ACCEPTCONN among /proc/self/fd and its family, address and port come
 * from getsockname(). Nothing here asks the library where it thinks it bound:
 * the whole defect is that the library's answer and the kernel's differ.
 *
 * WHAT THIS SUITE ASSUMES OF THE MACHINE, out loud, because a case green for
 * the wrong reason is worse than a red one:
 *   - the kernel has IPv6, and `::1` is on the loopback;
 *   - 192.0.2.1 (RFC 5737) and 2001:db8::1 (RFC 3849) are NOT addresses of
 *     this machine. They are documentation ranges, but a machine that had
 *     configured one would turn the two heaviest cases into measurements of
 *     nothing - so main() binds them and skips (77) if either answers.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "HttpServer.h"
#include "Logger.h"
#include "libuvw.h"

namespace
{

//Documentation ranges (RFC 5737, RFC 3849): well formed, routable-looking, and
//not meant to exist anywhere. main() proves they do not exist HERE.
const char *const kAbsentV4 = "192.0.2.1";
const char *const kAbsentV6 = "2001:db8::1";
//Neither family, and not a host name this code is allowed to resolve either.
const char *const kUnreadable = "nonsense-typo";

std::string workDir;

struct Outcome
{
    //Number of listening TCP sockets the child owned. Anything but one and the
    //rest of the line describes the wrong socket.
    int listeners = 0;
    int family = 0;
    std::string addr;
    int port = 0;
    int asked = 0;
    std::string log;
};

//A port this suite chose, released before the child is forked. The child is
//the only user of it until it exits, so the window cannot be shared.
int reservePort()
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return 0;

    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;

    int port = 0;
    if (bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0)
    {
        socklen_t len = sizeof(a);
        if (getsockname(fd, reinterpret_cast<sockaddr *>(&a), &len) == 0)
            port = ntohs(a.sin_port);
    }
    close(fd);
    return port;
}

bool addressExistsHere(int family, const char *literal)
{
    const int fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    bool ok = false;
    if (family == AF_INET)
    {
        sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = 0;
        ok = inet_pton(AF_INET, literal, &a.sin_addr) == 1 &&
             bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0;
    }
    else
    {
        sockaddr_in6 a;
        memset(&a, 0, sizeof(a));
        a.sin6_family = AF_INET6;
        a.sin6_port = 0;
        ok = inet_pton(AF_INET6, literal, &a.sin6_addr) == 1 &&
             bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0;
    }
    close(fd);
    return ok;
}

//The listening socket of this process, taken from the kernel. SO_ACCEPTCONN
//names it without ambiguity, and getsockname() says where it really is.
void describeListener(int &listeners, int &family, std::string &addr, int &port)
{
    listeners = 0;
    family = 0;
    port = 0;
    addr.clear();

    DIR *d = opendir("/proc/self/fd");
    if (!d)
        return;

    const int skip = dirfd(d);
    struct dirent *e;
    while ((e = readdir(d)) != nullptr)
    {
        const int fd = atoi(e->d_name);
        if (fd <= 0 || fd == skip)
            continue;

        int type = 0;
        socklen_t len = sizeof(type);
        if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) != 0 ||
            type != SOCK_STREAM)
            continue;

        int accepting = 0;
        len = sizeof(accepting);
        if (getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &accepting, &len) != 0 ||
            !accepting)
            continue;

        sockaddr_storage ss;
        socklen_t sl = sizeof(ss);
        memset(&ss, 0, sizeof(ss));
        if (getsockname(fd, reinterpret_cast<sockaddr *>(&ss), &sl) != 0)
            continue;

        char text[INET6_ADDRSTRLEN] = { 0 };
        if (ss.ss_family == AF_INET6)
        {
            const sockaddr_in6 *a = reinterpret_cast<const sockaddr_in6 *>(&ss);
            inet_ntop(AF_INET6, &a->sin6_addr, text, sizeof(text));
            port = ntohs(a->sin6_port);
        }
        else if (ss.ss_family == AF_INET)
        {
            const sockaddr_in *a = reinterpret_cast<const sockaddr_in *>(&ss);
            inet_ntop(AF_INET, &a->sin_addr, text, sizeof(text));
            port = ntohs(a->sin_port);
        }
        else
            continue;

        family = ss.ss_family;
        addr = text;
        listeners++;
    }
    closedir(d);
}

std::string slurp(const std::string &path)
{
    std::ifstream in(path.c_str());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

Outcome measure(const std::string &listenAddress, int tag)
{
    Outcome r;

    const std::string base = workDir + "/case" + std::to_string(tag);
    const std::string cfg = base + "/config";
    const std::string cache = base + "/cache";
    const std::string logPath = base + "/log";
    const std::string outPath = base + "/out";
    ::mkdir(base.c_str(), 0700);
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(cache.c_str(), 0700);

    r.asked = reservePort();
    if (r.asked <= 0)
        return r;

    const pid_t pid = fork();
    if (pid == 0)
    {
        const int lf = open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (lf >= 0)
        {
            dup2(lf, STDOUT_FILENO);
            close(lf);
        }

        Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                                 const_cast<char *>(cache.c_str()), true);
        Utils::set_config_option("debug_level", "5");
        Utils::set_config_option("listen_address", listenAddress);

        HttpServer::Instance(r.asked);
        std::cout.flush();

        int listeners = 0, family = 0, port = 0;
        std::string addr;
        describeListener(listeners, family, addr, port);

        FILE *o = fopen(outPath.c_str(), "w");
        if (o)
        {
            fprintf(o, "%d %d %s %d\n", listeners, family,
                    addr.empty()? "-": addr.c_str(), port);
            fclose(o);
        }
        _exit(0);
    }

    if (pid < 0)
        return r;

    int status = 0;
    waitpid(pid, &status, 0);

    std::istringstream in(slurp(outPath));
    std::string addr;
    in >> r.listeners >> r.family >> addr >> r.port;
    if (addr != "-")
        r.addr = addr;
    r.log = slurp(logPath);
    return r;
}

struct Runs
{
    Outcome wildcard, presentV4, presentV6, unreadable, absentV4, absentV6;
};

const Runs &theRuns()
{
    static Runs runs = []()
    {
        Runs r;
        r.wildcard = measure("0.0.0.0", 0);
        r.presentV4 = measure("127.0.0.1", 1);
        r.presentV6 = measure("::1", 2);
        r.unreadable = measure(kUnreadable, 3);
        r.absentV4 = measure(kAbsentV4, 4);
        r.absentV6 = measure(kAbsentV6, 5);
        return r;
    }();
    return runs;
}

//The one phrase every fallback line carries, so a case cannot pass on a
//warning that names something else.
const char *const kFallbackTail = "listening on 0.0.0.0 (every interface) instead";

} //namespace

/*
 * THE FIXTURE, BEFORE ANYTHING IS CONCLUDED FROM IT. Every case below reads a
 * family, an address and a port off one socket; if a child had none, or two,
 * the numbers would describe something else and every case would still have
 * something to compare.
 */
TEST(ListenAddressFallback, EveryCaseBuiltExactlyOneListeningSocket)
{
    const Runs &r = theRuns();

    EXPECT_EQ(1, r.wildcard.listeners);
    EXPECT_EQ(1, r.presentV4.listeners);
    EXPECT_EQ(1, r.presentV6.listeners);
    EXPECT_EQ(1, r.unreadable.listeners);
    EXPECT_EQ(1, r.absentV4.listeners);
    EXPECT_EQ(1, r.absentV6.listeners);

    EXPECT_LT(0, r.wildcard.asked);
    EXPECT_LT(0, r.absentV4.asked);
}

/*
 * THE COUNTERWEIGHT. Two addresses that DO exist here must be bound as
 * written, on both families: a server that fell back always would satisfy
 * every fallback case below and confine nothing.
 */
TEST(ListenAddressFallback, AnAddressOfThisMachineIsBoundAsWritten)
{
    const Runs &r = theRuns();

    EXPECT_EQ(AF_INET, r.presentV4.family);
    EXPECT_EQ("127.0.0.1", r.presentV4.addr);
    EXPECT_EQ(r.presentV4.asked, r.presentV4.port);
    EXPECT_EQ(std::string::npos, r.presentV4.log.find(kFallbackTail));

    EXPECT_EQ(AF_INET6, r.presentV6.family);
    EXPECT_EQ("::1", r.presentV6.addr);
    EXPECT_EQ(r.presentV6.asked, r.presentV6.port);
    EXPECT_EQ(std::string::npos, r.presentV6.log.find(kFallbackTail));

    EXPECT_EQ(AF_INET, r.wildcard.family);
    EXPECT_EQ("0.0.0.0", r.wildcard.addr);
    EXPECT_EQ(r.wildcard.asked, r.wildcard.port);
    EXPECT_EQ(std::string::npos, r.wildcard.log.find(kFallbackTail));
}

/*
 * FORM 1: a value that is neither family. The socket is the same one the
 * defect produced - the decision was to widen rather than refuse to start,
 * because a typo must not turn a home automation box into a brick. What
 * changes is that the operator is told, and told WHICH value was refused.
 */
TEST(ListenAddressFallback, AnUnreadableAddressWidensAndSaysSo)
{
    const Runs &r = theRuns();

    EXPECT_EQ(AF_INET, r.unreadable.family);
    EXPECT_EQ("0.0.0.0", r.unreadable.addr);
    EXPECT_EQ(r.unreadable.asked, r.unreadable.port);

    EXPECT_NE(std::string::npos, r.unreadable.log.find(kUnreadable))
            << "the fallback did not name the value it refused";
    EXPECT_NE(std::string::npos, r.unreadable.log.find(kFallbackTail));
}

/*
 * FORM 2, AND THE HEAVY ONE: an address that is well formed and absent. The
 * port is the whole point. Auto-bound, the server answers on a number nobody
 * chose and no client can guess; a wide listen at least stays where it was
 * configured.
 */
TEST(ListenAddressFallback, AnAbsentIpv4AddressKeepsTheConfiguredPort)
{
    const Runs &r = theRuns();

    EXPECT_EQ(r.absentV4.asked, r.absentV4.port)
            << "the listen was auto-bound to an ephemeral port: the server is "
               "not where it was configured, and nobody can find it";
    EXPECT_EQ(AF_INET, r.absentV4.family);
    EXPECT_EQ("0.0.0.0", r.absentV4.addr);

    EXPECT_NE(std::string::npos, r.absentV4.log.find(kAbsentV4))
            << "the fallback did not name the value it refused";
    EXPECT_NE(std::string::npos, r.absentV4.log.find(kFallbackTail));
}

TEST(ListenAddressFallback, AnAbsentIpv6AddressKeepsTheConfiguredPort)
{
    const Runs &r = theRuns();

    EXPECT_EQ(r.absentV6.asked, r.absentV6.port)
            << "the listen was auto-bound to an ephemeral port";
    EXPECT_EQ(AF_INET, r.absentV6.family);
    EXPECT_EQ("0.0.0.0", r.absentV6.addr);

    EXPECT_NE(std::string::npos, r.absentV6.log.find(kAbsentV6))
            << "the fallback did not name the value it refused";
    EXPECT_NE(std::string::npos, r.absentV6.log.find(kFallbackTail));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_listenaddr_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
    {
        std::cerr << "could not create a work directory" << std::endl;
        return 1;
    }
    workDir = base;

    //Said out loud rather than left to cases that would then measure nothing.
    const int probe = socket(AF_INET6, SOCK_STREAM, 0);
    if (probe < 0)
    {
        std::cerr << "no IPv6 on this machine, nothing to measure" << std::endl;
        return 77;
    }
    close(probe);

    if (!addressExistsHere(AF_INET, "127.0.0.1") ||
        !addressExistsHere(AF_INET6, "::1"))
    {
        std::cerr << "no loopback address to bind, nothing to measure" << std::endl;
        return 77;
    }

    if (addressExistsHere(AF_INET, kAbsentV4) ||
        addressExistsHere(AF_INET6, kAbsentV6))
    {
        std::cerr << "a documentation address is configured on this machine, "
                     "the absent-address cases would measure nothing" << std::endl;
        return 77;
    }

    return RUN_ALL_TESTS();
}
