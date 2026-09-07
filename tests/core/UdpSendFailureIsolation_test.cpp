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
 * WHAT ONE UNDELIVERABLE ANSWER COSTS THE REST OF THE BOX.
 *
 * ---------------------------------------------------------------------------
 * THE TWO FAILURES THAT ARRIVE THROUGH THE SAME DOOR
 * ---------------------------------------------------------------------------
 * uvw's UDPHandle::send() hands its send request's failure back to the HANDLE:
 * the request's ErrorEvent is republished there, as the very event a failed
 * RECEIVE uses. The owner's listener therefore sees one event for two unrelated
 * accidents - a correspondent that cannot be reached, and a socket that no
 * longer works - and it used to answer both with stop(), once, without rearming.
 *
 * A refused answer is one correspondent's business. Stopping the handle is
 * everything else's: discovery (CALAOS_DISCOVER), Wago inputs (WAGO INT) and
 * KNX inputs (WAGO KNX) all arrive on that one socket, and none of them comes
 * back until the process is restarted. One line in the journal, naming none of
 * the three.
 *
 * ---------------------------------------------------------------------------
 * HOW THE REFUSAL IS PRODUCED HERE, WITHOUT A FAMILY DEFECT
 * ---------------------------------------------------------------------------
 * The server binds 127.0.0.1 and is then asked to answer 192.0.2.1 (RFC 5737).
 * The kernel refuses to route a loopback source to an off-link destination and
 * sendmsg() returns EINVAL - which is exactly the shape of the accident this
 * suite is about: a correspondent that was reachable when it asked and is not
 * when the answer leaves, because the route or the interface went away in
 * between. Nothing about the address FAMILY is wrong, so this is not a rerun of
 * the send<IPv4>-on-an-IPv6-socket defect that used to cause the same event.
 *
 * main() proves the refusal happens on THIS machine before any case runs, and
 * skips (77) if it does not: a suite that cannot produce the accident it
 * measures would report the fix as present when it is absent.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS READ, AND WHERE FROM
 * ---------------------------------------------------------------------------
 * A real UDPServer in a forked child, a real correspondent on the loopback, and
 * what the correspondent reads AT ITS OWN DESCRIPTOR. Whether an input still
 * arrives is taken from the signal UDPServer emits for the rest of the tree,
 * carrying the input number, so a repeat of an earlier datagram cannot pass for
 * the next one. The sentinel "-" separates "nothing arrived" from "arrived and
 * was read wrong".
 *
 * The first two undeliverable answers are posted back to back, without a turn
 * of the loop between them, so both are outstanding at once and the order in
 * which they are reported is a measurement rather than a coincidence. A third
 * is posted after an answer that DID leave, which is the only thing that says
 * whether a send is still counted as outstanding once it has succeeded.
 *
 * WHAT THIS SUITE ASSUMES OF THE MACHINE, out loud:
 *   - 127.0.0.1 can be bound;
 *   - 192.0.2.1, 192.0.2.2 and 192.0.2.3 are not addresses of this machine,
 *     and a datagram sent to them from a socket bound to 127.0.0.1 is
 *     refused.
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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
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

//Documentation range (RFC 5737): well formed, off-link, and not meant to exist
//anywhere. main() proves they do not exist HERE.
const char *const kUnreachableA = "192.0.2.1";
const char *const kUnreachableB = "192.0.2.2";
const char *const kUnreachableC = "192.0.2.3";

//What a slot reports when nothing at all reached it. A sensor that measured
//nothing must not read like a sensor that found nothing.
const char *const kNothing = "-";

//The answer a CALAOS_DISCOVER is owed. An installer, the mobile application and
//the wall screens have nothing else to find the box with.
const char *const kDiscoverAnswer = "CALAOS_IP ";

//The word libuv gives EINVAL. Both the shape before this ticket and the shape
//after it carry it, so a fixture case can assert the accident happened without
//depending on the wording of either.
const char *const kRefusalReason = "invalid argument";

std::string workDir;

//Reaches the send the server itself uses, with a destination this suite picks.
//Nothing else in the tree can hand UDPServer a correspondent that has gone
//away: the address it answers is the source of the datagram it just read.
class Discovery: public UDPServer
{
public:
    using UDPServer::UDPServer;

    void answer(const std::string &ip, unsigned int p)
    {
        //Held past the call: uvw does not copy the buffer, and a send that
        //waits behind another one is still reading it when this returns.
        kept.push_back("CALAOS_IP 127.0.0.1");
        sendTo(ip, p, kept.back());
    }

private:
    std::deque<std::string> kept;
};

//A port this suite chose, released before the child is forked. The child is the
//only user of it until it exits, so the window cannot be shared.
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

//Whether the accident this suite is built on happens on this machine at all.
bool aLoopbackSourceIsRefusedTowards(const char *literal)
{
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return false;

    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0)
    {
        close(fd);
        return false;
    }

    sockaddr_in d;
    memset(&d, 0, sizeof(d));
    d.sin_family = AF_INET;
    d.sin_port = htons(4646);
    inet_pton(AF_INET, literal, &d.sin_addr);
    const ssize_t n = sendto(fd, "?", 1, 0,
                             reinterpret_cast<sockaddr *>(&d), sizeof(d));
    close(fd);
    return n < 0;
}

std::string slurp(const std::string &path)
{
    std::ifstream in(path.c_str());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

//Filled in the child by the signal UDPServer emits for a Wago input. The input
//NUMBER travels with the address, so a datagram read twice cannot pass for the
//next one.
std::string gCarried[3];
int gStage = 0;

void noteWago(std::string ip, int input, bool, std::string)
{
    gCarried[gStage] = (ip.empty()? std::string("<empty>"): ip) + "/" +
                       Utils::to_string(input);
}

//Turns of the loop, never blocking on one that will not come: a case that hangs
//is a case nobody reads.
void pump()
{
    auto loop = uvw::Loop::getDefault();
    for (int i = 0; i < 40; i++)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        usleep(2000);
    }
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
    //What the rest of the tree was handed, before the refused answers and
    //after them, and the last input of all - the one that outlives a discovery
    //the server DID manage to answer.
    std::string before = kNothing;
    std::string after = kNothing;
    std::string last = kNothing;
    //The answer to CALAOS_DISCOVER, read at the correspondent's descriptor.
    std::string reply = kNothing;
    std::string log;
};

/* One child, one server, one correspondent, and the same six steps whether or
 * not the two undeliverable answers are posted. The control run is what says
 * the exchange itself works: without it, a red case could only mean "this
 * machine cannot do UDP".
 */
Run measure(bool refuseTwoAnswers, int tag)
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
        gStage = 0;
        Utils::signal_wago.connect(sigc::ptr_fun(&noteWago));

        Discovery discovery(asked);

        int peerPort = 0;
        const int c = correspondent(peerPort);

        std::string reply = kNothing;
        if (c >= 0)
        {
            speak(c, asked, "WAGO INT 7 true");
            pump();

            if (refuseTwoAnswers)
            {
                //Back to back: the second is still queued when the first fails.
                discovery.answer(kUnreachableA, peerPort);
                discovery.answer(kUnreachableB, peerPort);
                pump();
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

            if (refuseTwoAnswers)
            {
                //After one answer that did leave, so that a send still counted
                //as outstanding would put this refusal on the wrong peer.
                discovery.answer(kUnreachableC, peerPort);
                pump();
            }
            close(c);
        }

        std::cout.flush();

        FILE *o = fopen(outPath.c_str(), "w");
        if (o)
        {
            fprintf(o, "%s\n%s\n%s\n%s\n", gCarried[0].c_str(),
                    gCarried[1].c_str(), reply.c_str(), gCarried[2].c_str());
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
    while (fields.size() < 4)
        fields.push_back(kNothing);

    r.before = fields[0];
    r.after = fields[1];
    r.reply = fields[2];
    r.last = fields[3];
    r.log = slurp(logPath);
    return r;
}

struct Runs
{
    Run refused, control;
};

const Runs &theRuns()
{
    static Runs runs = []()
    {
        Runs r;
        r.refused = measure(true, 0);
        r.control = measure(false, 1);
        return r;
    }();
    return runs;
}

} //namespace

/*
 * THE FIXTURE, BEFORE ANYTHING IS CONCLUDED FROM IT. Both children read an
 * input before anything could go wrong; a child that never received the first
 * one would report the same "-" further down for a reason that has nothing to
 * do with this ticket.
 */
TEST(UdpSendFailureIsolation, BothServersWereReadingBeforeTheRefusedAnswers)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/7", r.refused.before);
    EXPECT_EQ("127.0.0.1/7", r.control.before);
}

/*
 * AND THE ACCIDENT REALLY HAPPENED. Worded so that it holds whatever the owner
 * decides to say about it: libuv's word for EINVAL is in the line either way.
 * The control run must not carry it, or the two runs would differ in nothing.
 */
TEST(UdpSendFailureIsolation, TheAnswersWereReallyRefused)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.refused.log.find(kRefusalReason))
            << "no answer was refused: this suite measured nothing at all";
    EXPECT_EQ(std::string::npos, r.control.log.find(kRefusalReason));
}

/*
 * THE COST, AND THE WHOLE POINT. The socket that carries the refused answer is
 * the socket every Wago and KNX input arrives on.
 */
TEST(UdpSendFailureIsolation, ARefusedAnswerDoesNotCostTheNextInput)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/9", r.refused.after)
            << "one unreachable correspondent stopped the receiver: no Wago or "
               "KNX input reaches the box again until it is restarted";
}

/*
 * The same loss seen from the other side: an installer looking for the box.
 */
TEST(UdpSendFailureIsolation, ARefusedAnswerDoesNotCostTheDiscoveryAnswer)
{
    const Runs &r = theRuns();

    EXPECT_EQ(0u, r.refused.reply.find(kDiscoverAnswer))
            << "the box no longer answers CALAOS_DISCOVER: nothing on the "
               "network can find it any more";
    EXPECT_EQ("127.0.0.1/8", r.refused.last);
}

/*
 * AND WHAT THE JOURNAL OWES ITS READER. "UDP server error" named neither the
 * correspondent that could not be reached nor the port that kept working.
 */
TEST(UdpSendFailureIsolation, ARefusedAnswerNamesItsCorrespondent)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.refused.log.find(kUnreachableA))
            << "the failure was reported without saying who could not be "
               "reached";
}

/*
 * A second refusal is a second correspondent. once<> heard the first one and
 * nothing after it, so a box losing two peers reported one.
 */
TEST(UdpSendFailureIsolation, EveryRefusedAnswerIsReportedAndNotJustTheFirst)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.refused.log.find(kUnreachableB))
            << "only the first refusal was reported";
}

/*
 * Which refusal belongs to which correspondent. Both answers are outstanding at
 * the same time, so naming them in the order they were posted is the only thing
 * that ties a line to a peer rather than to the most recent send.
 */
TEST(UdpSendFailureIsolation, TwoOutstandingAnswersAreNamedInTheOrderPosted)
{
    const Runs &r = theRuns();

    const size_t firstPeer = r.refused.log.find(kUnreachableA);
    const size_t secondPeer = r.refused.log.find(kUnreachableB);

    ASSERT_NE(std::string::npos, firstPeer);
    ASSERT_NE(std::string::npos, secondPeer);
    EXPECT_LT(firstPeer, secondPeer)
            << "the refusals were attributed to the wrong correspondents";
}

/*
 * AND AFTER AN ANSWER THAT DID LEAVE. A refusal is attributed to the datagram
 * that is still owed a completion; a datagram already delivered and still
 * counted would hand its own peer to the next accident.
 */
TEST(UdpSendFailureIsolation, ARefusalAfterADeliveredAnswerNamesItsOwnCorrespondent)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.refused.log.find(kUnreachableC))
            << "the third refusal was reported against another correspondent";
}

/*
 * THE WITNESS. Same exchange, no refused answer: everything the cases above ask
 * for must already be true, or they would be measuring the machine and not the
 * server.
 */
TEST(UdpSendFailureIsolation, WithoutARefusedAnswerNothingIsLost)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/9", r.control.after);
    EXPECT_EQ("127.0.0.1/8", r.control.last);
    EXPECT_EQ(0u, r.control.reply.find(kDiscoverAnswer));
    EXPECT_EQ(std::string::npos, r.control.log.find(kUnreachableA));
    EXPECT_EQ(std::string::npos, r.control.log.find(kUnreachableC));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_udpsendfail_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
    {
        std::cerr << "could not create a work directory" << std::endl;
        return 1;
    }
    workDir = base;

    //Said out loud rather than left to cases that would then measure nothing.
    if (!addressExistsHere("127.0.0.1"))
    {
        std::cerr << "no IPv4 loopback to bind, nothing to measure" << std::endl;
        return 77;
    }

    if (addressExistsHere(kUnreachableA) || addressExistsHere(kUnreachableB) ||
        addressExistsHere(kUnreachableC))
    {
        std::cerr << "a documentation address is configured on this machine, "
                     "no answer would be refused" << std::endl;
        return 77;
    }

    if (!aLoopbackSourceIsRefusedTowards(kUnreachableA) ||
        !aLoopbackSourceIsRefusedTowards(kUnreachableB) ||
        !aLoopbackSourceIsRefusedTowards(kUnreachableC))
    {
        std::cerr << "this machine routes a loopback source off-link, the "
                     "refused answer cannot be produced" << std::endl;
        return 77;
    }

    return RUN_ALL_TESTS();
}
