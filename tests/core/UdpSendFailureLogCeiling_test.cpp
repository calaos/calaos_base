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
 * WHO DECIDES HOW MANY LINES THE JOURNAL GETS.
 *
 * ---------------------------------------------------------------------------
 * THE PATH THIS MEASURES
 * ---------------------------------------------------------------------------
 * UDPServer answers the source of the datagram it just read, and UDP discovery
 * needs no credentials, so the correspondents it answers are chosen by whoever
 * speaks on the local network. A listener that writes one line per refused
 * answer therefore hands that same network the length of the journal: n refused
 * answers, n lines, and nothing in the box bounds n.
 *
 * The counterweight is that a refused answer is useful news. A ceiling that
 * says nothing at all would trade a talkative journal for a mute one, which is
 * worse: the FIRST refusal must still be written, and a failure that lasts must
 * not fall silent for good.
 *
 * ---------------------------------------------------------------------------
 * WHY THE CEILING LIVES IN THE SEND BRANCH AND NOWHERE ELSE
 * ---------------------------------------------------------------------------
 * The one listener sees two unrelated accidents: a refused send, and a socket
 * that no longer works. The second one gives the receiver up - discovery, Wago
 * and KNX inputs all stop there - and says so in two lines. Those two lines are
 * the last thing the box writes before going deaf, and they are worth more than
 * every refusal line put together. A ceiling applied to the listener as a whole
 * would be free to swallow them. One case here holds exactly that: a run whose
 * send ramp is deep is then made to lose its receiver, and the stop must still
 * be in the journal.
 *
 * ---------------------------------------------------------------------------
 * HOW THE TWO ACCIDENTS ARE PRODUCED
 * ---------------------------------------------------------------------------
 * A refused answer: the server binds 127.0.0.1 and is asked to answer an
 * address of the documentation range (RFC 5737). The kernel refuses to route a
 * loopback source off-link and sendmsg() returns EINVAL - a correspondent that
 * was reachable when it asked and is not when the answer leaves.
 *
 * A lost receiver: a stream socket is put over the descriptor libuv polls, so
 * the next read returns ENOTCONN. The cause is INJECTED, not produced by any
 * peer; an unconnected UDP socket is never told about ICMP.
 *
 * Each refusal goes to its OWN address, 192.0.2.1 upwards, so the journal says
 * WHICH refusals were reported rather than how many: reporting the first few,
 * the last few, or every tenth would all name different peers.
 *
 * No skip. Whether the two accidents can be produced on this machine is read by
 * cases that go red, never by a barrier that would report a clean run after
 * measuring nothing.
 *
 * WHAT THIS SUITE ASSUMES OF THE MACHINE, out loud:
 *   - 127.0.0.1 can be bound;
 *   - 192.0.2.0/24 is not configured here, and a datagram sent to it from a
 *     socket bound to 127.0.0.1 is refused.
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
#include <deque>
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

//How many answers the burst run has refused. Large enough that a journal
//without a ceiling is unmistakable, and small enough that 192.0.2.0/24 gives
//each one its own correspondent.
const int kBurstRefusals = 130;

//What a slot reports when nothing at all reached it. A sensor that measured
//nothing must not read like a sensor that found nothing.
const char *const kNothing = "-";

const char *const kDiscoverAnswer = "CALAOS_IP ";

//The opening of a refused-send line, and libuv's word for EINVAL. Both the
//shape before this ticket and the shape after it carry them.
const char *const kSendFailed = "UDP send to ";
const char *const kRefusalReason = "invalid argument";

//The two halves of what the owner writes when it gives the receiver up.
const char *const kServerError = "UDP server error";
const char *const kNoLongerListening = "no longer listening on port ";

std::string workDir;

std::string peerAddress(int nth)
{
    return "192.0.2." + Utils::to_string(nth);
}

//Reaches the send the server uses, with a destination this suite picks, and the
//descriptor libuv polls. Nothing else in the tree can hand UDPServer a
//correspondent that has gone away, nor break its receive.
class Discovery: public UDPServer
{
public:
    using UDPServer::UDPServer;

    void answer(const std::string &ip, unsigned int p)
    {
        //Held past the call: uvw does not copy the buffer, and a send queued
        //behind another one is still reading it when this returns.
        kept.push_back("CALAOS_IP 127.0.0.1");
        sendTo(ip, p, kept.back());
    }

    int descriptor() const { return (int)handleSrv->fileno(); }

private:
    std::deque<std::string> kept;
};

int reservePort()
{
    return calaos_test::ReservedPort::reserve(SOCK_DGRAM);
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
//The input NUMBER travels with the address, so a datagram read twice cannot
//pass for the next one.
std::string gCarried[3];
int gStage = 0;

void noteWago(std::string ip, int input, bool, std::string)
{
    gCarried[gStage] = (ip.empty()? std::string("<empty>"): ip) + "/" +
                       Utils::to_string(input);
}

//Turns of the loop, never blocking on one that will not come: a case that hangs
//is a case nobody reads. It also bounds what a tree writing one line per turn
//could produce, so such a tree fails a count instead of never finishing.
void pump()
{
    auto loop = uvw::Loop::getDefault();
    for (int i = 0; i < 40; i++)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        usleep(2000);
    }
}

/* The same, long enough to outlast the bounded recovery a broken receiver now
 * goes through before it says it has gone deaf: the holds between two attempts
 * are ExternProcServer::respawnDelay() of the attempts already made. A BOUND,
 * not a verdict - nothing below reads how long anything took.
 */
void pumpThroughTheRecovery()
{
    for (int i = 0; i < 8; i++)
        pump();
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
    std::string before = kNothing;
    std::string after = kNothing;
    std::string last = kNothing;
    std::string reply = kNothing;
    std::string log;
    int exitCode = -1;
    int port = 0;
};

/* One child, one server, one correspondent, the same five steps whatever the
 * run refuses. howMany answers are posted back to back, each to its own
 * correspondent; breakTheReceiver then takes the receiver away once no send is
 * outstanding any more, which is the only way the stop branch is reached at
 * all.
 */
Run measure(int howMany, bool breakTheReceiver, int tag)
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

            speak(c, asked, "CALAOS_DISCOVER");
            pump();

            char buf[256];
            const ssize_t n = recv(c, buf, sizeof(buf), 0);
            if (n > 0)
                reply.assign(buf, (size_t)n);

            //Back to back, so every one of them is outstanding before the first
            //failure is reported.
            for (int i = 1; i <= howMany; i++)
                discovery.answer(peerAddress(i), peerPort);
            pump();
            pump();

            gStage = 1;
            speak(c, asked, "WAGO KNX 9 true");
            pump();

            if (breakTheReceiver)
            {
                //Duplicated first: dup2 over the last descriptor naming the UDP
                //socket would destroy the description, release the port, and
                //the datagram meant to reach the broken read would go nowhere.
                const int polled = discovery.descriptor();
                const int kept = dup(polled);
                const int stream = socket(AF_INET, SOCK_STREAM, 0);
                dup2(stream, polled);
                (void)kept;
            }

            gStage = 2;
            speak(c, asked, "WAGO INT 8 true");
            pumpThroughTheRecovery();

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
    r.exitCode = WIFEXITED(status)? WEXITSTATUS(status): -WTERMSIG(status);

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
    Run burst, single, control;
};

const Runs &theRuns()
{
    static Runs runs = []()
    {
        Runs r;
        r.burst = measure(kBurstRefusals, true, 0);
        r.single = measure(1, false, 1);
        r.control = measure(0, false, 2);
        return r;
    }();
    return runs;
}

} //namespace

/*
 * THE FIXTURE, BEFORE ANYTHING IS CONCLUDED FROM IT. All three children were
 * serving before any answer was refused; a child that never received the first
 * input would report the same "-" further down for a reason that has nothing to
 * do with this ticket.
 */
TEST(UdpSendFailureLogCeiling, EveryServerWasServingBeforeTheRefusedAnswers)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/7", r.burst.before);
    EXPECT_EQ("127.0.0.1/7", r.single.before);
    EXPECT_EQ("127.0.0.1/7", r.control.before);

    EXPECT_EQ(0u, r.burst.reply.find(kDiscoverAnswer));
    EXPECT_EQ(0u, r.single.reply.find(kDiscoverAnswer));
    EXPECT_EQ(0u, r.control.reply.find(kDiscoverAnswer));
}

/*
 * AND NONE OF THEM DIED. Every count below is a journal a live process wrote,
 * never a child that stopped writing.
 */
TEST(UdpSendFailureLogCeiling, TheCeilingCostsNoChild)
{
    const Runs &r = theRuns();

    EXPECT_EQ(0, r.burst.exitCode);
    EXPECT_EQ(0, r.single.exitCode);
    EXPECT_EQ(0, r.control.exitCode);
}

/*
 * AND THE ANSWERS WERE REALLY REFUSED. Without this the whole suite would be
 * counting the lines of a box that had nothing to report.
 */
TEST(UdpSendFailureLogCeiling, TheAnswersWereReallyRefused)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.burst.log.find(kRefusalReason))
            << "no answer was refused: this suite measured nothing at all";
    EXPECT_NE(std::string::npos, r.single.log.find(kRefusalReason));
    EXPECT_EQ(std::string::npos, r.control.log.find(kRefusalReason));
}

/*
 * THE COUNTERWEIGHT, AND IT COMES FIRST. A ceiling that made the box quiet
 * about the accident would be a worse defect than the one this ticket closes:
 * one refused answer is still exactly one line, naming its correspondent.
 */
TEST(UdpSendFailureLogCeiling, TheFirstRefusalIsAlwaysWritten)
{
    const Runs &r = theRuns();

    EXPECT_EQ(1u, countOf(r.single.log, kSendFailed))
            << "a single refused answer is no longer reported at all: the "
               "journal went mute instead of bounded";
    EXPECT_NE(std::string::npos, r.single.log.find(peerAddress(1) + ":"))
            << "the refusal was reported without saying who could not be "
               "reached";
}

/*
 * THE DEFECT ITSELF. The correspondents are picked by whoever speaks on the
 * network, so one line per refused answer is a journal whose length the network
 * writes. This counts lines, it never times them.
 */
TEST(UdpSendFailureLogCeiling, ManyRefusedAnswersDoNotCostAsManyLines)
{
    const Runs &r = theRuns();

    EXPECT_EQ(9u, countOf(r.burst.log, kSendFailed))
            << kBurstRefusals << " refused answers wrote one line each: it is "
            << "the network, not the box, that decides how long the journal is";
}

/*
 * WHICH ONES SURVIVED, AND THAT IS NOT THE SAME QUESTION AS HOW MANY. Each
 * refusal went to its own address, so the journal names the refusals it kept.
 * Reporting the first nine, the last nine or every fifteenth would all pass a
 * count of nine and fail here.
 */
TEST(UdpSendFailureLogCeiling, TheKeptLinesAreTheOpeningFewThenTheDoublings)
{
    const Runs &r = theRuns();

    const int kept[] = { 1, 2, 3, 4, 8, 16, 32, 64, 128 };
    for (int n: kept)
        EXPECT_NE(std::string::npos, r.burst.log.find(peerAddress(n) + ":"))
                << "refusal " << n << " was dropped: the line that should have "
                   "been kept at this count is missing";

    const int dropped[] = { 5, 6, 7, 65, 127, 129, 130 };
    for (int n: dropped)
        EXPECT_EQ(std::string::npos, r.burst.log.find(peerAddress(n) + ":"))
                << "refusal " << n << " was reported: the kept lines are not "
                   "the ones the ceiling claims to keep";
}

/*
 * BOUNDED IS NOT EXTINGUISHED, AND THE READER HAS TO BE ABLE TO TELL. The last
 * line the burst kept carries the running count, so eight lines behind a
 * hundred and thirty refusals cannot be read as a box that only had eight.
 */
TEST(UdpSendFailureLogCeiling, TheKeptLineSaysHowManyWentUnsaid)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.burst.log.find("128 refused so far"))
            << "the journal reports a bounded number of refusals without ever "
               "saying how many there were";
    EXPECT_NE(std::string::npos, r.burst.log.find("1 refused so far"));
}

/*
 * AND THE CEILING DID NOT COST THE RECEIVER EITHER. The socket that carries a
 * refused answer is the socket every Wago and KNX input arrives on; a ceiling
 * written across the whole listener could take that branch with it.
 */
TEST(UdpSendFailureLogCeiling, TheBurstDoesNotCostTheNextInput)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/9", r.burst.after)
            << kBurstRefusals << " unreachable correspondents stopped the "
            << "receiver: no Wago or KNX input reaches the box again";
}

/*
 * WHAT THE CEILING MUST NEVER SWALLOW. The two lines of the stop are the last
 * thing the box writes before it stops hearing anything at all, and here they
 * are asked for on the run whose send ramp is deepest - the one where a ceiling
 * placed over the listener as a whole, rather than inside the send branch,
 * would have nothing left to spend on them.
 */
TEST(UdpSendFailureLogCeiling, TheCeilingDoesNotSilenceTheStop)
{
    const Runs &r = theRuns();

    EXPECT_EQ(kNothing, r.burst.last)
            << "the receiver was not lost, so this case is asking for a stop "
               "that had no reason to be written";
    EXPECT_NE(std::string::npos, r.burst.log.find(kServerError))
            << "the box went deaf without a word: the ceiling of the send "
               "branch swallowed the line that says so";
    EXPECT_NE(std::string::npos,
              r.burst.log.find(kNoLongerListening + Utils::to_string(r.burst.port)))
            << "the stop was reported without saying which port stops, or for "
               "another port";
}

/*
 * THE WITNESS, AND THE HAY THAT MUST NOT CARRY THE TARGET. Same five steps, no
 * refused answer and no accident: everything the cases above read must already
 * work here, or they would be measuring the machine rather than the server.
 */
TEST(UdpSendFailureLogCeiling, WithoutARefusedAnswerNothingIsSaidAndNothingIsLost)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/9", r.control.after);
    EXPECT_EQ("127.0.0.1/8", r.control.last);
    EXPECT_EQ(0u, countOf(r.control.log, kSendFailed));
    EXPECT_EQ(std::string::npos, r.control.log.find("192.0.2."));
    EXPECT_EQ(std::string::npos, r.control.log.find(kServerError));
    EXPECT_EQ(std::string::npos, r.control.log.find(kNoLongerListening));

    //And the single run keeps its receiver: one refusal is one peer's business.
    EXPECT_EQ("127.0.0.1/9", r.single.after);
    EXPECT_EQ("127.0.0.1/8", r.single.last);
    EXPECT_EQ(std::string::npos, r.single.log.find(kServerError));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_udpsendceil_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
    {
        std::cerr << "could not create a work directory" << std::endl;
        return 1;
    }
    workDir = base;

    return RUN_ALL_TESTS();
}
