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
 * And one floor below the third: the widening itself can fail, on a port
 * something else already holds. libuv keeps that refusal back from bind() and
 * hands it to listen(), so the code has to listen for it there too. Nothing
 * is bound then, which is honest - as long as it is not announced as a
 * successful listen.
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
 * ---------------------------------------------------------------------------
 * AND WHAT THE DISCOVERY SERVER READS AND WRITES ONCE IT IS BOUND
 * ---------------------------------------------------------------------------
 * Landing on the right socket is half of it. recv() and send() are templated
 * on the family the same way bind() is, and default to IPv4 the same way: on
 * an IPv6 socket the sender of every datagram was rendered "0.0.0.0" - not
 * empty, not an error, a plausible address - and the answer left as a
 * sockaddr_in the descriptor refuses. The exchange cases below therefore have
 * a real correspondent send real datagrams, and compare what the server acted
 * on with what the correspondent's OWN descriptor says it is.
 *
 * WHAT THIS SUITE ASSUMES OF THE MACHINE, out loud, because a case green for
 * the wrong reason is worse than a red one:
 *   - the kernel has IPv6, and `::1` is on the loopback;
 *   - 192.0.2.1 (RFC 5737) and 2001:db8::1 (RFC 3849) are NOT addresses of
 *     this machine. They are documentation ranges, but a machine that had
 *     configured one would turn the two heaviest cases into measurements of
 *     nothing - so main() binds them and skips (77) if either answers;
 *   - an IPv4 correspondent can reach a socket bound to `::`. main() sends one
 *     rather than reading net.ipv6.bindv6only: what the dual-stack exchange
 *     needs is the delivery, not the setting that usually grants it.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "HttpServer.h"
#include "Logger.h"
#include "UDPServer.h"
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
    //The discovery socket of the same child, read the same way. It answers the
    //same configuration key and had never been measured at all.
    int udpFamily = 0;
    std::string udpAddr;
    int udpPort = 0;
    int udpAsked = 0;
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

//Whether a `::` socket receives from an IPv4 correspondent. Sent rather than
//read off net.ipv6.bindv6only: the delivery is what the dual-stack exchange
//needs, and a setting is only the usual reason it happens.
bool aV4CorrespondentReachesAWildcardV6Socket()
{
    const int srv = socket(AF_INET6, SOCK_DGRAM, 0);
    if (srv < 0)
        return false;

    sockaddr_in6 a;
    memset(&a, 0, sizeof(a));
    a.sin6_family = AF_INET6;
    a.sin6_addr = in6addr_any;
    a.sin6_port = 0;
    socklen_t al = sizeof(a);
    if (bind(srv, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0 ||
        getsockname(srv, reinterpret_cast<sockaddr *>(&a), &al) != 0)
    {
        close(srv);
        return false;
    }

    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 300000;
    setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    const int cli = socket(AF_INET, SOCK_DGRAM, 0);
    bool reached = false;
    if (cli >= 0)
    {
        sockaddr_in d;
        memset(&d, 0, sizeof(d));
        d.sin_family = AF_INET;
        d.sin_port = a.sin6_port;
        inet_pton(AF_INET, "127.0.0.1", &d.sin_addr);
        if (sendto(cli, "?", 1, 0, reinterpret_cast<sockaddr *>(&d), sizeof(d)) == 1)
        {
            char buf[8];
            reached = recv(srv, buf, sizeof(buf), 0) == 1;
        }
        close(cli);
    }

    close(srv);
    return reached;
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

//The datagram socket UDPServer just opened: the one file descriptor of this
//type that was not there a moment ago. Same reading as the listening socket -
//the kernel is asked, not the library.
int newDatagramFd(const std::vector<int> &before)
{
    int found = -1;
    DIR *d = opendir("/proc/self/fd");
    if (!d)
        return -1;

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
            type != SOCK_DGRAM)
            continue;

        if (std::find(before.begin(), before.end(), fd) == before.end())
            found = fd;
    }
    closedir(d);
    return found;
}

std::vector<int> datagramFds()
{
    std::vector<int> out;
    DIR *d = opendir("/proc/self/fd");
    if (!d)
        return out;

    const int skip = dirfd(d);
    struct dirent *e;
    while ((e = readdir(d)) != nullptr)
    {
        const int fd = atoi(e->d_name);
        if (fd <= 0 || fd == skip)
            continue;

        int type = 0;
        socklen_t len = sizeof(type);
        if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) == 0 &&
            type == SOCK_DGRAM)
            out.push_back(fd);
    }
    closedir(d);
    return out;
}

void describeSocket(int fd, int &family, std::string &addr, int &port)
{
    family = 0;
    port = 0;
    addr.clear();
    if (fd < 0)
        return;

    sockaddr_storage ss;
    socklen_t sl = sizeof(ss);
    memset(&ss, 0, sizeof(ss));
    if (getsockname(fd, reinterpret_cast<sockaddr *>(&ss), &sl) != 0)
        return;

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
        return;

    family = ss.ss_family;
    addr = text;
}

std::string slurp(const std::string &path)
{
    std::ifstream in(path.c_str());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

//`occupy` holds the wildcard port before the child starts, so that the
//fallback bind fails in its turn: the one path where widening cannot save the
//listen either.
Outcome measure(const std::string &listenAddress, int tag, bool occupy = false)
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
    r.udpAsked = reservePort();
    if (r.asked <= 0 || r.udpAsked <= 0)
        return r;

    int squat = -1;
    if (occupy)
    {
        squat = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons((uint16_t)r.asked);
        if (squat < 0 ||
            bind(squat, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0 ||
            listen(squat, 1) != 0)
        {
            if (squat >= 0)
                close(squat);
            r.asked = 0;
            return r;
        }
    }

    const pid_t pid = fork();
    if (pid == 0)
    {
        if (squat >= 0)
            close(squat);

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

        const std::vector<int> beforeUdp = datagramFds();
        UDPServer discovery(r.udpAsked);
        (void)discovery;
        std::cout.flush();

        int listeners = 0, family = 0, port = 0;
        std::string addr;
        describeListener(listeners, family, addr, port);

        int uFamily = 0, uPort = 0;
        std::string uAddr;
        describeSocket(newDatagramFd(beforeUdp), uFamily, uAddr, uPort);

        FILE *o = fopen(outPath.c_str(), "w");
        if (o)
        {
            fprintf(o, "%d %d %s %d %d %s %d\n", listeners, family,
                    addr.empty()? "-": addr.c_str(), port,
                    uFamily, uAddr.empty()? "-": uAddr.c_str(), uPort);
            fclose(o);
        }
        _exit(0);
    }

    if (pid < 0)
    {
        if (squat >= 0)
            close(squat);
        return r;
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (squat >= 0)
        close(squat);

    std::istringstream in(slurp(outPath));
    std::string addr, uAddr;
    in >> r.listeners >> r.family >> addr >> r.port
       >> r.udpFamily >> uAddr >> r.udpPort;
    if (addr != "-")
        r.addr = addr;
    if (uAddr != "-")
        r.udpAddr = uAddr;
    r.log = slurp(logPath);
    return r;
}

struct Runs
{
    Outcome wildcard, presentV4, presentV6, unreadable, absentV4, absentV6, taken;
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
        r.taken = measure(kAbsentV4, 6, true);
        return r;
    }();
    return runs;
}

//What a case reports when nothing at all reached it. A sensor that measured
//nothing must not read like a sensor that found nothing.
const char *const kNothing = "-";

//Filled in the child by the signal UDPServer emits for a Wago input. Two
//slots: what it carried before the answer to a discovery was due, and after.
std::string gCarried[2];
int gStage = 0;

void noteWago(std::string ip, int, bool, std::string)
{
    gCarried[gStage] = ip.empty()? std::string("<empty>"): ip;
}

//Turns of the loop, never blocking on one that will not come: a case that
//hangs is a case nobody reads.
void pump()
{
    auto loop = uvw::Loop::getDefault();
    for (int i = 0; i < 40; i++)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        usleep(2000);
    }
}

//A correspondent on the loopback of its own family, ready to be read at its
//descriptor. -1 when the machine cannot host it.
int correspondent(int family, std::string &ip, int &port)
{
    const int c = socket(family, SOCK_DGRAM, 0);
    if (c < 0)
        return -1;

    char text[INET6_ADDRSTRLEN] = { 0 };
    bool ok = false;
    if (family == AF_INET6)
    {
        sockaddr_in6 a;
        memset(&a, 0, sizeof(a));
        a.sin6_family = AF_INET6;
        inet_pton(AF_INET6, "::1", &a.sin6_addr);
        socklen_t l = sizeof(a);
        ok = bind(c, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0 &&
             getsockname(c, reinterpret_cast<sockaddr *>(&a), &l) == 0;
        if (ok)
        {
            inet_ntop(AF_INET6, &a.sin6_addr, text, sizeof(text));
            port = ntohs(a.sin6_port);
        }
    }
    else
    {
        sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        socklen_t l = sizeof(a);
        ok = bind(c, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0 &&
             getsockname(c, reinterpret_cast<sockaddr *>(&a), &l) == 0;
        if (ok)
        {
            inet_ntop(AF_INET, &a.sin_addr, text, sizeof(text));
            port = ntohs(a.sin_port);
        }
    }

    if (!ok)
    {
        close(c);
        return -1;
    }

    ip = text;
    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 400000;
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return c;
}

void speak(int fd, int family, const char *dest, int port, const std::string &text)
{
    if (family == AF_INET6)
    {
        sockaddr_in6 d;
        memset(&d, 0, sizeof(d));
        d.sin6_family = AF_INET6;
        d.sin6_port = htons((uint16_t)port);
        inet_pton(AF_INET6, dest, &d.sin6_addr);
        sendto(fd, text.data(), text.size(), 0,
               reinterpret_cast<sockaddr *>(&d), sizeof(d));
    }
    else
    {
        sockaddr_in d;
        memset(&d, 0, sizeof(d));
        d.sin_family = AF_INET;
        d.sin_port = htons((uint16_t)port);
        inet_pton(AF_INET, dest, &d.sin_addr);
        sendto(fd, text.data(), text.size(), 0,
               reinterpret_cast<sockaddr *>(&d), sizeof(d));
    }
}

struct Exchange
{
    //The discovery socket, read the way everything else here is read.
    int family = 0;
    std::string addr;
    int port = 0;
    int asked = 0;
    //The correspondent, read at ITS OWN descriptor. This is what every case
    //below compares against - never a literal the suite hoped for.
    std::string truthIp;
    int truthPort = 0;
    //What UDPServer handed the rest of the tree, before and after the answer
    //to CALAOS_DISCOVER was due, and the answer itself.
    std::string before = kNothing;
    std::string reply = kNothing;
    std::string after = kNothing;
    std::string log;
};

Exchange exchange(const std::string &listenAddress, const char *dest,
                  int peerFamily, int tag)
{
    Exchange r;

    const std::string base = workDir + "/xchg" + std::to_string(tag);
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

        gCarried[0] = kNothing;
        gCarried[1] = kNothing;
        gStage = 0;
        Utils::signal_wago.connect(sigc::ptr_fun(&noteWago));

        const std::vector<int> beforeUdp = datagramFds();
        UDPServer discovery(r.asked);
        (void)discovery;

        int uFamily = 0, uPort = 0;
        std::string uAddr;
        describeSocket(newDatagramFd(beforeUdp), uFamily, uAddr, uPort);

        std::string peerIp;
        int peerPort = 0;
        const int c = correspondent(peerFamily, peerIp, peerPort);

        std::string reply = kNothing;
        if (c >= 0 && uPort > 0)
        {
            speak(c, peerFamily, dest, uPort, "WAGO INT 7 true");
            pump();

            gStage = 1;
            speak(c, peerFamily, dest, uPort, "CALAOS_DISCOVER");
            pump();

            char buf[256];
            const ssize_t n = recv(c, buf, sizeof(buf), 0);
            if (n > 0)
                reply.assign(buf, (size_t)n);

            //The second input is what says whether the server is still there
            //at all: a refused answer used to take the receiver down with it.
            speak(c, peerFamily, dest, uPort, "WAGO INT 7 true");
            pump();
            close(c);
        }

        std::cout.flush();

        FILE *o = fopen(outPath.c_str(), "w");
        if (o)
        {
            fprintf(o, "%d\n%s\n%d\n%s\n%d\n%s\n%s\n%s\n",
                    uFamily, uAddr.empty()? kNothing: uAddr.c_str(), uPort,
                    peerIp.empty()? kNothing: peerIp.c_str(), peerPort,
                    gCarried[0].c_str(), reply.c_str(), gCarried[1].c_str());
            fclose(o);
        }
        _exit(0);
    }

    if (pid < 0)
        return r;

    int status = 0;
    waitpid(pid, &status, 0);

    std::istringstream in(slurp(outPath));
    std::string line;
    std::vector<std::string> fields;
    while (std::getline(in, line))
        fields.push_back(line);
    while (fields.size() < 8)
        fields.push_back(kNothing);

    r.family = atoi(fields[0].c_str());
    r.addr = fields[1] == kNothing? std::string(): fields[1];
    r.port = atoi(fields[2].c_str());
    r.truthIp = fields[3] == kNothing? std::string(): fields[3];
    r.truthPort = atoi(fields[4].c_str());
    r.before = fields[5];
    r.reply = fields[6];
    r.after = fields[7];
    r.log = slurp(logPath);
    return r;
}

struct Exchanges
{
    Exchange v6, v4, dual;
};

const Exchanges &theExchanges()
{
    static Exchanges x = []()
    {
        Exchanges e;
        e.v6 = exchange("::1", "::1", AF_INET6, 0);
        e.v4 = exchange("127.0.0.1", "127.0.0.1", AF_INET, 1);
        e.dual = exchange("::", "127.0.0.1", AF_INET, 2);
        return e;
    }();
    return x;
}

//The answer a CALAOS_DISCOVER is owed. An installer, the mobile application
//and the wall screens have nothing else to find the box with.
const char *const kDiscoverAnswer = "CALAOS_IP ";

//The one phrase every fallback line carries, so a case cannot pass on a
//warning that names something else.
const char *const kFallbackTail = "listening on 0.0.0.0 (every interface) instead";

//What the server says when even the widened bind found the port taken.
const char *const kNothingListening = "is answering on no address at all";

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

    //And the one child that could not bind at all owns none.
    EXPECT_EQ(0, r.taken.listeners);

    EXPECT_LT(0, r.wildcard.asked);
    EXPECT_LT(0, r.absentV4.asked);
    EXPECT_LT(0, r.taken.asked);
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

/*
 * FORM 2, ONE FLOOR DOWN: the widening itself fails, because the port is
 * already held. There is then no address left to fall back to, and the server
 * ends up listening on nothing at all. That is the honest outcome - but the
 * whole point of this ticket is that it must not be announced as a success,
 * and "Listening on port N" is exactly what used to follow.
 */
TEST(ListenAddressFallback, AFallbackThatCannotBindEitherSaysSoAndClaimsNothing)
{
    const Runs &r = theRuns();

    EXPECT_EQ(0, r.taken.listeners)
            << "the port was held, yet a listening socket appeared";

    EXPECT_NE(std::string::npos, r.taken.log.find(kNothingListening))
            << "the server was left deaf without a word";
    EXPECT_EQ(std::string::npos,
              r.taken.log.find("Listening on port " + std::to_string(r.taken.asked)))
            << "the log announced a port the server does not have";
}

/*
 * THE DISCOVERY SERVER, WHICH READS THE SAME KEY AND WHICH NOTHING HAD EVER
 * MEASURED. It is the half of the listen an installer, a mobile application
 * and the wall screens use to find the box at all; its family was chosen by
 * reasoning and by nothing else.
 */
TEST(ListenAddressFallback, TheDiscoveryPortBindsTheFamilyItWasAsked)
{
    const Runs &r = theRuns();

    EXPECT_EQ(AF_INET, r.wildcard.udpFamily);
    EXPECT_EQ("0.0.0.0", r.wildcard.udpAddr);
    EXPECT_EQ(r.wildcard.udpAsked, r.wildcard.udpPort);

    EXPECT_EQ(AF_INET, r.presentV4.udpFamily);
    EXPECT_EQ("127.0.0.1", r.presentV4.udpAddr);
    EXPECT_EQ(r.presentV4.udpAsked, r.presentV4.udpPort);

    EXPECT_EQ(AF_INET6, r.presentV6.udpFamily);
    EXPECT_EQ("::1", r.presentV6.udpAddr);
    EXPECT_EQ(r.presentV6.udpAsked, r.presentV6.udpPort);
}

/*
 * And it widens the same way, on the same port, for the same reasons: a box
 * whose discovery answers nobody is a box nobody can configure.
 */
TEST(ListenAddressFallback, TheDiscoveryPortWidensLikeTheApiPort)
{
    const Runs &r = theRuns();

    EXPECT_EQ(AF_INET, r.unreadable.udpFamily);
    EXPECT_EQ("0.0.0.0", r.unreadable.udpAddr);
    EXPECT_EQ(r.unreadable.udpAsked, r.unreadable.udpPort);

    EXPECT_EQ(AF_INET, r.absentV4.udpFamily);
    EXPECT_EQ("0.0.0.0", r.absentV4.udpAddr);
    EXPECT_EQ(r.absentV4.udpAsked, r.absentV4.udpPort);

    EXPECT_EQ(AF_INET, r.absentV6.udpFamily);
    EXPECT_EQ("0.0.0.0", r.absentV6.udpAddr);
    EXPECT_EQ(r.absentV6.udpAsked, r.absentV6.udpPort);
}

/*
 * THE EXCHANGE FIXTURE, BEFORE ANYTHING IS CONCLUDED FROM IT. Every case below
 * compares an address the server acted on with an address read at the
 * correspondent's own descriptor. If no datagram had reached the server at
 * all, those cases would be comparing two absences and could not tell the
 * difference between a wrong reading and no reading.
 */
TEST(ListenAddressFallback, EveryExchangeReachedTheServerItWasAimedAt)
{
    const Exchanges &x = theExchanges();

    EXPECT_EQ(AF_INET6, x.v6.family);
    EXPECT_EQ("::1", x.v6.addr);
    EXPECT_EQ(x.v6.asked, x.v6.port);
    EXPECT_EQ("::1", x.v6.truthIp);

    EXPECT_EQ(AF_INET, x.v4.family);
    EXPECT_EQ("127.0.0.1", x.v4.addr);
    EXPECT_EQ(x.v4.asked, x.v4.port);
    EXPECT_EQ("127.0.0.1", x.v4.truthIp);

    EXPECT_EQ(AF_INET6, x.dual.family);
    EXPECT_EQ("::", x.dual.addr);
    EXPECT_EQ(x.dual.asked, x.dual.port);
    EXPECT_EQ("127.0.0.1", x.dual.truthIp);

    EXPECT_LT(0, x.v6.truthPort);
    EXPECT_LT(0, x.v4.truthPort);
    EXPECT_LT(0, x.dual.truthPort);

    EXPECT_NE(kNothing, x.v6.before) << "no datagram reached processRequest";
    EXPECT_NE(kNothing, x.v4.before) << "no datagram reached processRequest";
    EXPECT_NE(kNothing, x.dual.before) << "no datagram reached processRequest";
}

/*
 * WHAT A WAGO OR KNX INPUT CARRIES. The address is not a label: it is the
 * whole of the match, `ip == host`, and a wrong one silently belongs to no
 * configured equipment. The log still says "received input", so the operator
 * is told the datagram arrived and never told it went nowhere.
 */
TEST(ListenAddressFallback, AnInputCarriesTheAddressItReallyCameFrom)
{
    const Exchanges &x = theExchanges();

    EXPECT_EQ(x.v6.truthIp, x.v6.before)
            << "an IPv6 correspondent was read as something else: every Wago "
               "and KNX input pushed to this server belongs to no host";
    EXPECT_EQ(x.v4.truthIp, x.v4.before);
    EXPECT_EQ(x.dual.truthIp, x.dual.before)
            << "a dual-stack listen must name a correspondent the way an IPv4 "
               "listen does, or the same box changes identity with the key";
}

/*
 * THE ANSWER. CALAOS_DISCOVER is how calaos_installer, the mobile application
 * and the wall screens find the box at all; there is no second route. It has
 * to leave on the family the socket is bound to, whatever family the address
 * it is aimed at reads in.
 */
TEST(ListenAddressFallback, TheDiscoveryAnswerReachesItsCorrespondent)
{
    const Exchanges &x = theExchanges();

    EXPECT_EQ(0u, x.v6.reply.find(kDiscoverAnswer))
            << "the box did not answer an IPv6 correspondent: nothing on the "
               "network can find it any more";
    EXPECT_EQ(0u, x.v4.reply.find(kDiscoverAnswer));
    EXPECT_EQ(0u, x.dual.reply.find(kDiscoverAnswer));
}

/*
 * AND THE PART THAT OUTLIVES THE ANSWER. A send() refused by the descriptor
 * publishes its refusal on the handle, where the owner's error listener stops
 * it - so one unanswerable discovery took the whole receiver with it, inputs
 * included, for the life of the process.
 *
 * Whether the second input carries the RIGHT address is the case above; all
 * this one asks is whether it arrived at all, so that the two do not go red
 * together for the same reason and neither of them measures alone.
 */
TEST(ListenAddressFallback, TheServerStillReadsAfterAnsweringADiscovery)
{
    const Exchanges &x = theExchanges();

    EXPECT_NE(kNothing, x.v6.after)
            << "the server stopped reading after one discovery: no input "
               "reaches it again until it is restarted";
    EXPECT_NE(kNothing, x.v4.after);
    EXPECT_NE(kNothing, x.dual.after);

    EXPECT_EQ(std::string::npos, x.v6.log.find("UDP server error"))
            << "the answer was refused by the descriptor";
    EXPECT_EQ(std::string::npos, x.dual.log.find("UDP server error"));
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

    if (!aV4CorrespondentReachesAWildcardV6Socket())
    {
        std::cerr << "an IPv4 correspondent cannot reach a socket bound to ::, "
                     "the dual-stack exchange would measure nothing" << std::endl;
        return 77;
    }

    return RUN_ALL_TESTS();
}
