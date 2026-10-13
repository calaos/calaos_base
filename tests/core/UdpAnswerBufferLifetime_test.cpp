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
 * WHAT THE BOX ACTUALLY PUTS ON THE WIRE WHEN IT IS ASKED TO INTRODUCE ITSELF
 * THREE TIMES IN A ROW.
 *
 * ---------------------------------------------------------------------------
 * WHY THREE, AND WHY IT IS THE PEER'S BYTES THAT ARE READ
 * ---------------------------------------------------------------------------
 * uvw's send(const sockaddr &, char *, unsigned int) neither copies nor owns
 * the buffer: its deleter is a no-op and uv_buf_init just points at the bytes.
 * libuv sends the FIRST datagram inline, from inside uv_udp_send; anything
 * posted before the loop turns again finds a non-empty send queue and is only
 * read at the next turn. So a caller that hands over a buffer it then destroys
 * is right once and wrong afterwards.
 *
 * One CALAOS_DISCOVER therefore proves nothing: the answer leaves before the
 * buffer dies. THREE, posted back to back, are all read in one receive burst
 * (libuv takes up to 32 datagrams per readable event), answered in the same
 * turn, and the second and third answers are the ones whose buffer is already
 * gone when libuv reads it.
 *
 * What is measured is what each PEER holds at its own descriptor, byte for
 * byte, in hexadecimal. Not a length: the payload this server owes is
 * "CALAOS_IP 127.0.0.1", nineteen bytes, and the freed-heap bytes observed in
 * its place are nineteen bytes too. Not a substring either: a disclosure that
 * happened to begin with the right prefix would pass. The whole datagram is
 * compared to the whole expected payload.
 *
 * ---------------------------------------------------------------------------
 * THREE PEERS, NOT ONE, AND WHY
 * ---------------------------------------------------------------------------
 * Each of the three requests comes from its own socket, so each answer is
 * identified by the descriptor it lands on rather than by its rank in a queue.
 * Two answers swapped between peers would then be a different measurement and
 * not the same one, which a single shared socket could not tell apart.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS SUITE ASSUMES OF THE MACHINE, out loud
 * ---------------------------------------------------------------------------
 *   - 127.0.0.1 can be bound, and is the address the server picks to answer a
 *     correspondent on 127.0.0.1;
 *   - libuv holds a second back-to-back send until the next turn of the loop.
 * Both are measured by a probe child BEFORE any case runs, and the suite exits
 * 77 rather than reporting a defect it cannot produce as absent.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Logger.h"
#include "UDPServer.h"
#include "libuvw.h"
#include "ReservedPort.h"

namespace
{

//What a slot reports when nothing at all reached it. A sensor that measured
//nothing must not read like a sensor that found nothing.
const char *const kNothing = "-";

//The whole answer a CALAOS_DISCOVER from the loopback is owed, exactly.
const char *const kExpectedAnswer = "CALAOS_IP 127.0.0.1";

//The address the probe child must find, or the expected answer above is not
//the answer this machine's server would build.
const char *const kLoopback = "127.0.0.1";

std::string workDir;

std::string hex(const char *p, size_t n)
{
    static const char *const digits = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; i++)
    {
        const unsigned char c = (unsigned char)p[i];
        out += digits[c >> 4];
        out += digits[c & 0x0f];
    }
    return out;
}

std::string hex(const std::string &s)
{
    return hex(s.data(), s.size());
}

std::string slurp(const std::string &path)
{
    std::ifstream in(path.c_str());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

//A port this suite chose, released before the child is forked. The child is
//the only user of it until it exits, so the window cannot be shared.
int reservePort()
{
    return calaos_test::ReservedPort::reserve(SOCK_DGRAM);
}

bool addressExistsHere(const char *literal)
{
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return false;

    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = 0;
    const bool ok = inet_pton(AF_INET, literal, &a.sin_addr) == 1 &&
                    bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0;
    close(fd);
    return ok;
}

//A correspondent on the IPv4 loopback, ready to be read at its descriptor.
int correspondent(int &port)
{
    const int c = socket(AF_INET, SOCK_DGRAM, 0);
    if (c < 0)
        return -1;

    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    inet_pton(AF_INET, kLoopback, &a.sin_addr);
    socklen_t l = sizeof(a);
    if (bind(c, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0 ||
        getsockname(c, reinterpret_cast<sockaddr *>(&a), &l) != 0)
    {
        close(c);
        return -1;
    }

    port = ntohs(a.sin_port);
    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 400000;
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return c;
}

void speak(int fd, int port, const std::string &text)
{
    sockaddr_in d;
    memset(&d, 0, sizeof(d));
    d.sin_family = AF_INET;
    d.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, kLoopback, &d.sin_addr);
    sendto(fd, text.data(), text.size(), 0,
           reinterpret_cast<sockaddr *>(&d), sizeof(d));
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

//Whole datagram, in hex, or kNothing.
std::string readOne(int fd)
{
    char buf[512];
    const ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n < 0)
        return kNothing;
    return hex(buf, (size_t)n);
}

int drain(int fd)
{
    char buf[512];
    int n = 0;
    while (recv(fd, buf, sizeof(buf), MSG_DONTWAIT) >= 0)
        n++;
    return n;
}

/*----------------------------------------------------------------------------
 * THE PROBE CHILD: does this machine produce the accident at all?
 *
 * Two sends posted back to back on a bare uvw handle, with no turn of the loop
 * between them and no UDPServer involved. libuv sends the first from inside
 * uv_udp_send and only queues the second, so exactly one datagram is readable
 * before the loop runs. If both are already there, nothing on this machine is
 * ever read after its buffer could die, and every case below would be green
 * for a reason that has nothing to do with the server.
 *--------------------------------------------------------------------------*/
struct Probe
{
    std::string localIp = kNothing;
    int readableBeforeTheTurn = -1;
    int readableAfterTheTurn = -1;
};

Probe runProbe()
{
    Probe p;

    const std::string base = workDir + "/probe";
    const std::string cfg = base + "/config";
    const std::string cache = base + "/cache";
    const std::string outPath = base + "/out";
    ::mkdir(base.c_str(), 0700);
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(cache.c_str(), 0700);

    const pid_t pid = fork();
    if (pid == 0)
    {
        Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                                 const_cast<char *>(cache.c_str()), true);

        const std::string localIp = TCPSocket::GetLocalIPFor(kLoopback);

        int peerPort = 0;
        const int c = correspondent(peerPort);
        int before = -1, after = -1;

        if (c >= 0)
        {
            auto loop = uvw::Loop::getDefault();
            auto h = loop->resource<uvw::UDPHandle>();
            Calaos::bindListenAddress(*h, kLoopback, 0,
                                      uvw::UDPHandle::Bind::REUSEADDR);

            //Outlive both sends on purpose: this probe measures WHEN libuv
            //reads a buffer, not what happens to a buffer that died.
            static std::string first = "PROBE-ONE";
            static std::string second = "PROBE-TWO";

            Calaos::sendDatagram(*h, kLoopback, peerPort,
                                 (char *)first.c_str(), first.length());
            Calaos::sendDatagram(*h, kLoopback, peerPort,
                                 (char *)second.c_str(), second.length());

            before = drain(c);
            pump();
            after = drain(c);
            close(c);
        }

        FILE *o = fopen(outPath.c_str(), "w");
        if (o)
        {
            fprintf(o, "%s\n%d\n%d\n", localIp.c_str(), before, after);
            fclose(o);
        }
        _exit(0);
    }

    if (pid < 0)
        return p;

    int status = 0;
    waitpid(pid, &status, 0);

    std::istringstream in(slurp(outPath));
    std::string line;
    std::vector<std::string> fields;
    while (std::getline(in, line))
        fields.push_back(line);
    if (fields.size() < 3)
        return p;

    p.localIp = fields[0];
    p.readableBeforeTheTurn = atoi(fields[1].c_str());
    p.readableAfterTheTurn = atoi(fields[2].c_str());
    return p;
}

/*----------------------------------------------------------------------------
 * THE MEASUREMENT CHILD: a real UDPServer, three peers, one burst.
 *--------------------------------------------------------------------------*/
struct Burst
{
    //What each of the three peers holds at its own descriptor, in hex.
    std::string first = kNothing;
    std::string second = kNothing;
    std::string third = kNothing;
    //Anything the three peers received beyond their one answer.
    int extras = -1;
    //A discovery asked alone, long after the burst: the send queue is empty,
    //libuv reads the buffer inline, and this is right even when the burst is
    //not. It is the control that says the exchange itself works.
    std::string alone = kNothing;
};

Burst measure()
{
    Burst r;

    const std::string base = workDir + "/run";
    const std::string cfg = base + "/config";
    const std::string cache = base + "/cache";
    const std::string logPath = base + "/log";
    const std::string outPath = base + "/out";
    ::mkdir(base.c_str(), 0700);
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(cache.c_str(), 0700);

    const int asked = reservePort();
    if (asked <= 0)
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
        Utils::set_config_option("listen_address", kLoopback);

        UDPServer discovery(asked);

        int pa = 0, pb = 0, pc = 0, pd = 0;
        const int a = correspondent(pa);
        const int b = correspondent(pb);
        const int c = correspondent(pc);
        const int d = correspondent(pd);

        std::string first = kNothing, second = kNothing, third = kNothing;
        std::string alone = kNothing;
        int extras = -1;

        if (a >= 0 && b >= 0 && c >= 0 && d >= 0)
        {
            //Back to back, three sockets, no turn of the loop between them:
            //one receive burst, three answers posted in the same turn.
            speak(a, asked, "CALAOS_DISCOVER");
            speak(b, asked, "CALAOS_DISCOVER");
            speak(c, asked, "CALAOS_DISCOVER");
            pump();

            first = readOne(a);
            second = readOne(b);
            third = readOne(c);
            extras = drain(a) + drain(b) + drain(c);

            speak(d, asked, "CALAOS_DISCOVER");
            pump();
            alone = readOne(d);

            close(a);
            close(b);
            close(c);
            close(d);
        }

        std::cout.flush();

        FILE *o = fopen(outPath.c_str(), "w");
        if (o)
        {
            fprintf(o, "%s\n%s\n%s\n%d\n%s\n", first.c_str(), second.c_str(),
                    third.c_str(), extras, alone.c_str());
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
    if (fields.size() < 5)
        return r;

    r.first = fields[0];
    r.second = fields[1];
    r.third = fields[2];
    r.extras = atoi(fields[3].c_str());
    r.alone = fields[4];
    return r;
}

Probe gProbe;

const Burst &theBurst()
{
    static Burst run = measure();
    return run;
}

} //namespace

/*
 * THE FIXTURE, BEFORE ANYTHING IS CONCLUDED FROM IT. The address the server
 * answers with is the one the expected payload below is written around.
 */
TEST(UdpAnswerBufferLifetime, TheServerAnswersWithTheLoopbackAddress)
{
    EXPECT_EQ(kLoopback, gProbe.localIp);
}

/*
 * AND THE MECHANISM IS PRESENT ON THIS MACHINE. Without this, three answers
 * that all came out right would say nothing: they would only say that libuv
 * never had to hold a buffer.
 */
TEST(UdpAnswerBufferLifetime, LibuvHoldsASecondBackToBackSendUntilTheNextTurn)
{
    EXPECT_EQ(1, gProbe.readableBeforeTheTurn)
            << "libuv did not queue the second send: the accident this suite "
               "measures cannot happen here, and every case would be green "
               "for it";
    EXPECT_EQ(1, gProbe.readableAfterTheTurn)
            << "the queued send never left";
}

/*
 * THE CONTROL. One discovery, asked alone, with an empty send queue: libuv
 * reads the buffer before sendTo() returns, so this is right whatever the
 * lifetime of that buffer is. A red here means the exchange itself is broken
 * and the cases below are measuring the machine.
 */
TEST(UdpAnswerBufferLifetime, ADiscoveryAskedAloneIsAnsweredWithThePayload)
{
    EXPECT_EQ(hex(kExpectedAnswer), theBurst().alone);
}

/*
 * Every peer of the burst was answered exactly once. A peer that received
 * nothing would report kNothing further down for a reason that has nothing to
 * do with this ticket, and a duplicate would let one right answer stand in for
 * a wrong one.
 */
TEST(UdpAnswerBufferLifetime, EachPeerOfTheBurstWasAnsweredExactlyOnce)
{
    const Burst &r = theBurst();

    EXPECT_NE(kNothing, r.first);
    EXPECT_NE(kNothing, r.second);
    EXPECT_NE(kNothing, r.third);
    EXPECT_EQ(0, r.extras);
}

/*
 * The first answer of the burst leaves from inside uv_udp_send, before the
 * buffer it was given can die. It is right on both shapes of the code, and it
 * is what makes the two cases below a measurement of the LATER answers rather
 * than of discovery in general.
 */
TEST(UdpAnswerBufferLifetime, TheFirstPeerOfTheBurstGetsThePayload)
{
    EXPECT_EQ(hex(kExpectedAnswer), theBurst().first);
}

/*
 * AND THE WHOLE POINT. The second answer is read by libuv after the string it
 * was built in is gone. What the peer holds is compared whole and in hex: the
 * bytes seen in its place are as long as the payload, so nothing about a
 * length, a prefix or a non-empty datagram would tell them apart.
 */
TEST(UdpAnswerBufferLifetime, TheSecondPeerOfTheBurstGetsThePayloadAndNotFreedMemory)
{
    EXPECT_EQ(hex(kExpectedAnswer), theBurst().second)
            << "the second answer carried the contents of a freed buffer to an "
               "unauthenticated peer on the local network";
}

/*
 * The third, whose buffer has been gone for one more turn. Its own socket, so
 * it cannot be the second answer counted twice.
 */
TEST(UdpAnswerBufferLifetime, TheThirdPeerOfTheBurstGetsThePayloadAndNotFreedMemory)
{
    EXPECT_EQ(hex(kExpectedAnswer), theBurst().third)
            << "the third answer carried the contents of a freed buffer to an "
               "unauthenticated peer on the local network";
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_udpanswerbuf_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
    {
        std::cerr << "could not create a work directory" << std::endl;
        return 1;
    }
    workDir = base;

    //Said out loud rather than left to cases that would then measure nothing.
    if (!addressExistsHere(kLoopback))
    {
        std::cerr << "no IPv4 loopback to bind, nothing to measure" << std::endl;
        return 77;
    }

    gProbe = runProbe();

    if (gProbe.localIp != kLoopback)
    {
        std::cerr << "the server answers a loopback correspondent with \""
                  << gProbe.localIp << "\", not " << kLoopback
                  << ": the expected payload is not the one it would build"
                  << std::endl;
        return 77;
    }

    if (gProbe.readableBeforeTheTurn != 1 || gProbe.readableAfterTheTurn != 1)
    {
        std::cerr << "libuv did not hold the second back-to-back send here ("
                  << gProbe.readableBeforeTheTurn << " then "
                  << gProbe.readableAfterTheTurn
                  << " readable): the accident cannot be produced" << std::endl;
        return 77;
    }

    return RUN_ALL_TESTS();
}
