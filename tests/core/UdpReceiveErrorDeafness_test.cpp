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
 * WHAT A FAILED RECEIVE COSTS, AND WHY THE HANDLE IS STOPPED FOR IT.
 *
 * ---------------------------------------------------------------------------
 * WHERE THE ERROR COMES FROM
 * ---------------------------------------------------------------------------
 * An unconnected UDP socket is never told about ICMP: a read returns EAGAIN and
 * nothing else, so no peer on the network can make this server's receive fail.
 * What can is an accident in this process's own descriptor table - a descriptor
 * closed twice, or reused while libuv still polls it. epoll keeps its own
 * reference to the file description, so the port stays bound and keeps reporting
 * readable; the read then lands on whatever the descriptor number now names.
 *
 * This suite produces that, three ways, and reads what the box loses:
 *   - a stream socket put over the descriptor  -> recvmsg gives ENOTCONN;
 *   - the descriptor simply closed             -> recvmsg gives EBADF;
 *   - the descriptor restored afterwards       -> the reads resume.
 *
 * The cause is INJECTED. Nothing on the network produces it, and this suite
 * does not pretend otherwise.
 *
 * ---------------------------------------------------------------------------
 * WHY THE STOP IS NOT A DEFECT
 * ---------------------------------------------------------------------------
 * libuv's poll is level triggered and uv__udp_recvmsg does not disarm itself on
 * an error: the same read fails again on every turn of the loop, for as long as
 * the datagram sits unread. Measured on a bare libuv handle, one datagram and a
 * broken descriptor produced 509226 error events - and with a listener that
 * writes a line per event, that is 509226 lines. Stopping the handle is what
 * turns that into one. The cost is that the box never listens again, and the
 * recovery case below shows the socket itself was never the thing that was lost.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS SUITE REFUSES TO BE
 * ---------------------------------------------------------------------------
 * No skip: whether the accident can be produced on this machine is read by
 * CASES that go red, not by a barrier that would report a clean run when it
 * measured nothing. Whether the box still hears is taken from the signal
 * UDPServer emits for the rest of the tree, carrying the input NUMBER, so a
 * replayed datagram cannot pass for the next one; "-" separates "nothing
 * arrived" from "arrived and was read wrong". A control child runs the same
 * six steps without any accident, and the child's exit status is read, so
 * "the box went deaf" is never confused with "the child died".
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

namespace
{

//What a slot reports when nothing at all reached it. A sensor that measured
//nothing must not read like a sensor that found nothing.
const char *const kNothing = "-";

const char *const kDiscoverAnswer = "CALAOS_IP ";

//libuv's words for the two errnos this suite produces. They are in the journal
//whatever the owner decides to say around them.
const char *const kNotConnected = "socket is not connected";
const char *const kBadDescriptor = "bad file descriptor";

//The two halves of what the owner writes when it gives the receiver up.
const char *const kServerError = "UDP server error";
const char *const kNoLongerListening = "no longer listening on port ";

std::string workDir;

enum Accident
{
    NoAccident,
    StreamSocketOverTheDescriptor,
    DescriptorClosed,
    DescriptorRestored
};

//Reaches the descriptor libuv polls and the receive it arms, neither of which
//UDPServer exposes. Nothing else in the tree can break a receive: the socket is
//healthy for as long as this process leaves its descriptor table alone.
class Deafness: public UDPServer
{
public:
    using UDPServer::UDPServer;

    int descriptor() const { return (int)handleSrv->fileno(); }
    void armTheReceiveAgain() { handleSrv->recv(); }
};

int reservePort()
{
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
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

std::string slurp(const std::string &path)
{
    std::ifstream in(path.c_str());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

size_t countOf(const std::string &hay, const std::string &needle)
{
    size_t n = 0;
    for (size_t at = hay.find(needle); at != std::string::npos;
         at = hay.find(needle, at + needle.size()))
        n++;
    return n;
}

//Filled in the child by the signal UDPServer emits for a Wago or KNX input.
//Stage 3 accumulates, because what comes back after a recovery is a queue and
//its ORDER is part of the measurement.
std::string gCarried[4];
int gStage = 0;

void noteWago(std::string ip, int input, bool, std::string)
{
    const std::string one = (ip.empty()? std::string("<empty>"): ip) + "/" +
                            Utils::to_string(input);
    if (gStage == 3 && gCarried[3] != kNothing)
        gCarried[3] += "+" + one;
    else
        gCarried[gStage] = one;
}

//Turns of the loop, never blocking on one that will not come: a case that hangs
//is a case nobody reads. It also bounds the error burst a tree without the stop
//would produce, so that such a tree fails the count instead of never finishing.
void pump()
{
    auto loop = uvw::Loop::getDefault();
    for (int i = 0; i < 40; i++)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        usleep(2000);
    }
}

int correspondent(int &port)
{
    const int c = socket(AF_INET, SOCK_DGRAM, 0);
    if (c < 0)
        return -1;

    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
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
    inet_pton(AF_INET, "127.0.0.1", &d.sin_addr);
    sendto(fd, text.data(), text.size(), 0,
           reinterpret_cast<sockaddr *>(&d), sizeof(d));
}

struct Run
{
    //What the rest of the tree was handed at each step, and the answer read at
    //the correspondent's own descriptor.
    std::string before = kNothing;
    std::string after = kNothing;
    std::string last = kNothing;
    std::string recovered = kNothing;
    std::string reply = kNothing;
    std::string log;
    int exitCode = -1;
    int port = 0;
};

/* One child, one server, one correspondent, the same six steps whichever
 * accident is asked for. The descriptor is duplicated before it is broken: the
 * file description has to outlive the accident, or the port would be released
 * and the datagrams that prove the deafness would go nowhere.
 */
Run measure(Accident accident, int tag)
{
    Run r;

    const std::string base = workDir + "/run" + std::to_string(tag);
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
    r.port = asked;

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
        Utils::set_config_option("listen_address", "127.0.0.1");

        gCarried[0] = kNothing;
        gCarried[1] = kNothing;
        gCarried[2] = kNothing;
        gCarried[3] = kNothing;
        gStage = 0;
        Utils::signal_wago.connect(sigc::ptr_fun(&noteWago));

        Deafness server(asked);

        int peerPort = 0;
        const int c = correspondent(peerPort);

        std::string reply = kNothing;
        if (c >= 0)
        {
            speak(c, asked, "WAGO INT 7 true");
            pump();

            const int polled = server.descriptor();
            const int kept = dup(polled);

            if (accident == StreamSocketOverTheDescriptor ||
                accident == DescriptorRestored)
            {
                const int stream = socket(AF_INET, SOCK_STREAM, 0);
                dup2(stream, polled);
            }
            else if (accident == DescriptorClosed)
            {
                close(polled);
            }

            gStage = 1;
            speak(c, asked, "WAGO KNX 9 true");
            pump();

            speak(c, asked, "CALAOS_DISCOVER");
            pump();

            char buf[256];
            const ssize_t n = recv(c, buf, sizeof(buf), 0);
            if (n > 0)
                reply.assign(buf, (size_t)n);

            gStage = 2;
            speak(c, asked, "WAGO INT 8 true");
            pump();

            if (accident == DescriptorRestored)
            {
                //The policy gave the receiver up; the socket never went
                //anywhere. Putting it back and arming the read again is what
                //says which of the two the box actually lost.
                dup2(kept, polled);
                gStage = 3;
                server.armTheReceiveAgain();
                speak(c, asked, "WAGO KNX 11 true");
                pump();
            }

            close(c);
        }

        std::cout.flush();

        FILE *o = fopen(outPath.c_str(), "w");
        if (o)
        {
            fprintf(o, "%s\n%s\n%s\n%s\n%s\n", gCarried[0].c_str(),
                    gCarried[1].c_str(), reply.c_str(), gCarried[2].c_str(),
                    gCarried[3].c_str());
            fclose(o);
        }
        _exit(0);
    }

    if (pid < 0)
        return r;

    int status = 0;
    waitpid(pid, &status, 0);
    r.exitCode = WIFEXITED(status)? WEXITSTATUS(status): -WTERMSIG(status);

    std::istringstream in(slurp(outPath));
    std::string line;
    std::vector<std::string> fields;
    while (std::getline(in, line))
        fields.push_back(line);
    while (fields.size() < 5)
        fields.push_back(kNothing);

    r.before = fields[0];
    r.after = fields[1];
    r.reply = fields[2];
    r.last = fields[3];
    r.recovered = fields[4];
    r.log = slurp(logPath);
    return r;
}

struct Runs
{
    Run broken, control, closed, restored;
};

const Runs &theRuns()
{
    static Runs runs = []()
    {
        Runs r;
        r.broken = measure(StreamSocketOverTheDescriptor, 0);
        r.control = measure(NoAccident, 1);
        r.closed = measure(DescriptorClosed, 2);
        r.restored = measure(DescriptorRestored, 3);
        return r;
    }();
    return runs;
}

} //namespace

/*
 * THE FIXTURE, BEFORE ANYTHING IS CONCLUDED FROM IT. All four children read an
 * input before the descriptor was touched; a child that never received the
 * first one would report the same "-" further down for a reason that has
 * nothing to do with this ticket.
 */
TEST(UdpReceiveErrorDeafness, EveryServerWasReadingBeforeTheDescriptorAccident)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/7", r.broken.before);
    EXPECT_EQ("127.0.0.1/7", r.control.before);
    EXPECT_EQ("127.0.0.1/7", r.closed.before);
    EXPECT_EQ("127.0.0.1/7", r.restored.before);
}

/*
 * AND NONE OF THEM DIED. Every "nothing arrived" below is a server that stopped
 * listening, never a child that is no longer there to listen.
 */
TEST(UdpReceiveErrorDeafness, TheAccidentCostsTheReceiverAndNotTheProcess)
{
    const Runs &r = theRuns();

    EXPECT_EQ(0, r.broken.exitCode);
    EXPECT_EQ(0, r.control.exitCode);
    EXPECT_EQ(0, r.closed.exitCode);
    EXPECT_EQ(0, r.restored.exitCode);
}

/*
 * THE ERROR IS REAL AND IT REACHED THE OWNER. Without this the whole suite
 * would be reading a server that is deaf for some other reason entirely.
 */
TEST(UdpReceiveErrorDeafness, AStreamSocketOverTheDescriptorIsAReceiveError)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.broken.log.find(kNotConnected))
            << "no receive failed: this suite measured nothing at all";
    EXPECT_NE(std::string::npos, r.broken.log.find(kServerError));
}

/*
 * AND CLOSING THE DESCRIPTOR IS ONE TOO. epoll holds its own reference to the
 * file description, so the port stays bound and keeps reporting readable: the
 * read that follows lands on a descriptor number that names nothing.
 */
TEST(UdpReceiveErrorDeafness, AClosedDescriptorIsAReceiveErrorAndNotSilence)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.closed.log.find(kBadDescriptor))
            << "closing the polled descriptor produced no error at all";
    EXPECT_NE(std::string::npos, r.closed.log.find(kServerError));
}

/*
 * THE COST. One failed read and every input the box has arrives on that socket
 * and stops arriving: Wago, KNX, and the discovery an installer looks for it
 * with.
 */
TEST(UdpReceiveErrorDeafness, AReceiveErrorCostsEveryInputTheBoxHas)
{
    const Runs &r = theRuns();

    EXPECT_EQ(kNothing, r.broken.after);
    EXPECT_EQ(kNothing, r.broken.last);
    EXPECT_EQ(kNothing, r.broken.reply);
}

/*
 * THE WITNESS, AND THE HAY THAT MUST NOT CARRY THE TARGET. Same six steps, no
 * accident: everything above must already work, or the cases would be measuring
 * the machine rather than the server.
 */
TEST(UdpReceiveErrorDeafness, WithoutTheAccidentTheSameExchangeIsServed)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/9", r.control.after);
    EXPECT_EQ("127.0.0.1/8", r.control.last);
    EXPECT_EQ(0u, r.control.reply.find(kDiscoverAnswer));
    EXPECT_EQ(std::string::npos, r.control.log.find(kServerError));
    EXPECT_EQ(std::string::npos, r.control.log.find(kNoLongerListening));
}

/*
 * WHAT THE JOURNAL OWES ITS READER. The port is the one the kernel handed this
 * run, so a line that named any other port would not pass for this one.
 */
TEST(UdpReceiveErrorDeafness, TheStopSaysWhichPortAndWhatItTakesAway)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos,
              r.broken.log.find(kNoLongerListening + Utils::to_string(r.broken.port)))
            << "the receiver was given up without saying so, or for another port";
    EXPECT_NE(std::string::npos, r.broken.log.find("CALAOS_DISCOVER"));
    EXPECT_NE(std::string::npos, r.broken.log.find("WAGO INT"));
    EXPECT_NE(std::string::npos, r.broken.log.find("WAGO KNX"));
}

/*
 * THE REASON THE STOP IS THERE. libuv's poll is level triggered and its read
 * does not disarm on an error, so the same failure comes back on every turn for
 * as long as the datagram is unread. Stopping the handle is what makes that one
 * line instead of one line per turn; a tree that dropped the stop would write
 * as many as the loop ran. This counts, it does not time.
 */
TEST(UdpReceiveErrorDeafness, TheReceiveErrorIsReportedOnceAndNotOncePerLoopTurn)
{
    const Runs &r = theRuns();

    EXPECT_EQ(1u, countOf(r.broken.log, kServerError))
            << "the failed read repeats on every turn of the loop: without the "
               "stop it is the network, not the box, that decides how many "
               "lines the journal gets";
    EXPECT_EQ(1u, countOf(r.closed.log, kServerError));
}

/*
 * CHARACTERISATION, and it is meant to turn red the day the arbitration is
 * settled: what the box lost is the policy's decision, not the socket. Put the
 * descriptor back, arm the read again by hand, and everything that arrived
 * during the deafness is still there, in the order it arrived, followed by what
 * came after. UDPServer never does this - nothing in the tree re-arms a stopped
 * receiver.
 */
TEST(UdpReceiveErrorDeafness, NothingThatArrivedDuringTheDeafnessWasLostByTheSocket)
{
    const Runs &r = theRuns();

    EXPECT_EQ(kNothing, r.restored.after)
            << "the restored child was never deaf, so it measures nothing";
    EXPECT_EQ("127.0.0.1/9+127.0.0.1/8+127.0.0.1/11", r.restored.recovered)
            << "arming the read again did not bring the queued inputs back in "
               "the order they arrived";
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_udprecverr_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
    {
        std::cerr << "could not create a work directory" << std::endl;
        return 1;
    }
    workDir = base;

    return RUN_ALL_TESTS();
}
