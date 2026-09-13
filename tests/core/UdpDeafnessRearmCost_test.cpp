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
 * WHAT RE-ARMING A GIVEN-UP UDP RECEIVER WOULD COST.
 *
 * ---------------------------------------------------------------------------
 * THE QUESTION THIS SUITE MEASURES, AND DOES NOT ANSWER
 * ---------------------------------------------------------------------------
 * UDPServer gives its receiver up on a failed read and nothing ever arms it
 * again, so the box is deaf for the life of the process. Three conducts are
 * defensible - come back later, stay deaf and say so, or die - and the choice
 * is not one a test makes. What a test can do is price them, which is all this
 * suite does. It changes nothing and asserts nothing about which is right.
 *
 * ---------------------------------------------------------------------------
 * HOW THE FAULT IS PRODUCED
 * ---------------------------------------------------------------------------
 * No peer on the network can make an unconnected UDP socket's read fail: a
 * read returns EAGAIN and nothing else. What can is an accident in this
 * process's own descriptor table. epoll holds its own reference to the file
 * description, so the port stays bound and keeps reporting readable while the
 * read lands on whatever the descriptor number now names: a stream socket
 * (ENOTCONN), or nothing at all (EBADF). The cause is INJECTED here, and the
 * suite says so rather than dressing it as a network event.
 *
 * The re-arm is injected too. It is done from the test, a turn of the loop
 * after the give-up and never from inside the error callback, because that is
 * the only shape a deferred recovery could have - and the point is to learn
 * what such a shape costs before one is written into the server.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS SUITE REFUSES TO BE
 * ---------------------------------------------------------------------------
 * No skip. Whether the accident can be produced on this machine is read by
 * CASES THAT GO RED, never by a barrier that would report a clean run having
 * measured nothing: a child that asks for an accident and does not get one
 * fails the sentinel case below before any conclusion is drawn from its
 * silence. On a machine without an IPv4 loopback this suite fails; that is the
 * choice made.
 *
 * It reads no duration and no rate. Every verdict is a COUNT of journal lines
 * or an input NUMBER, and the two children that differ only in how long they
 * were pumped must report the SAME count - which is what says the cost of a
 * bounded recovery follows its attempts and not the clock.
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

//libuv's words for the two errnos this suite produces.
const char *const kNotConnected = "socket is not connected";
const char *const kBadDescriptor = "bad file descriptor";

const char *const kServerError = "UDP server error";
const char *const kNoLongerListening = "no longer listening on port ";

std::string workDir;

enum Fault
{
    NoFault,
    StreamSocketOverTheDescriptor,
    DescriptorClosed
};

//Reaches the descriptor libuv polls and the receive UDPServer arms once and
//never again. Neither is exposed, and nothing else in the tree can break a
//receive or repair one.
class Rearm: public UDPServer
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
//The recovery stage accumulates: what comes back after a repair is a queue and
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

//Turns of the loop, never a wait on one that will not come. The budget also
//bounds an unbounded burst, so a tree that produces one fails a count instead
//of never finishing.
void pump(int turns)
{
    auto loop = uvw::Loop::getDefault();
    for (int i = 0; i < turns; i++)
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

//One child. A fault is put over the polled descriptor, then the receive is
//armed again `arms` times, each on its own later turn; `repairAt` puts the
//descriptor back before that attempt, so a fault that heals and a fault that
//does not are the same run with one number changed.
struct Ask
{
    Fault fault = NoFault;
    int arms = 0;
    int repairAt = -1;    //1-based attempt, -1 for a fault that never heals
    int turnsPerArm = 12;
};

struct Run
{
    std::string before = kNothing;
    std::string after = kNothing;
    std::string last = kNothing;
    std::string recovered = kNothing;
    std::string reply = kNothing;
    std::string log;
    int exitCode = -1;
    int port = 0;
    int arms = 0;
};

Run measure(const Ask &ask, int tag)
{
    Run r;
    r.arms = ask.arms;

    const std::string base = workDir + "/run" + std::to_string(tag);
    const std::string cfg = base + "/config";
    const std::string cache = base + "/cache";
    const std::string logPath = base + "/log";
    const std::string outPath = base + "/out";
    //Written before the accident and read whatever becomes of the child: one of
    //them dies, and a child that dies must still be able to say it was reading
    //first.
    const std::string beforePath = base + "/before";
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
        //A child that dies inside libuv takes its buffers with it, and the
        //give-up line written just before is exactly what says the death
        //followed a real stop.
        setvbuf(stdout, NULL, _IONBF, 0);
        std::cout << std::unitbuf;

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

        Rearm server(asked);

        int peerPort = 0;
        const int c = correspondent(peerPort);

        std::string reply = kNothing;
        if (c >= 0)
        {
            speak(c, asked, "WAGO INT 7 true");
            pump(ask.turnsPerArm);

            FILE *b = fopen(beforePath.c_str(), "w");
            if (b)
            {
                fprintf(b, "%s\n", gCarried[0].c_str());
                fclose(b);
            }

            const int polled = server.descriptor();
            const int kept = dup(polled);

            if (ask.fault == StreamSocketOverTheDescriptor)
            {
                const int stream = socket(AF_INET, SOCK_STREAM, 0);
                dup2(stream, polled);
            }
            else if (ask.fault == DescriptorClosed)
            {
                close(polled);
            }

            gStage = 1;
            speak(c, asked, "WAGO KNX 9 true");
            pump(ask.turnsPerArm);

            speak(c, asked, "CALAOS_DISCOVER");
            pump(ask.turnsPerArm);

            char buf[256];
            const ssize_t n = recv(c, buf, sizeof(buf), 0);
            if (n > 0)
                reply.assign(buf, (size_t)n);

            gStage = 2;
            speak(c, asked, "WAGO INT 8 true");
            pump(ask.turnsPerArm);

            for (int a = 1; a <= ask.arms; a++)
            {
                if (a == ask.repairAt)
                {
                    dup2(kept, polled);
                    gStage = 3;
                    speak(c, asked, "WAGO KNX 11 true");
                }
                server.armTheReceiveAgain();
                pump(ask.turnsPerArm);
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

    std::string firstRead = slurp(beforePath);
    while (!firstRead.empty() && firstRead[firstRead.size() - 1] == '\n')
        firstRead.erase(firstRead.size() - 1);
    r.before = firstRead.empty()? kNothing: firstRead;
    r.after = fields[1];
    r.reply = fields[2];
    r.last = fields[3];
    r.recovered = fields[4];
    r.log = slurp(logPath);
    return r;
}

struct Runs
{
    //No fault at all: everything below must already work without one.
    Run control;
    //The conduct master ships: give the receiver up, never come back.
    Run given_up;
    //A fault that never heals, re-armed a bounded number of times. Twice, the
    //second pumped four times as long: the pair is the whole point.
    Run ceiling_short, ceiling_long;
    //The same, re-armed four times as often.
    Run ceiling_wide;
    //A fault that heals between two attempts.
    Run heals;
    //The same conduct over a descriptor that names nothing.
    Run closed_given_up, closed_rearmed;
};

const Runs &theRuns()
{
    static Runs runs = []()
    {
        Runs r;
        Ask a;

        a = Ask(); a.fault = NoFault;
        r.control = measure(a, 0);

        a = Ask(); a.fault = StreamSocketOverTheDescriptor;
        r.given_up = measure(a, 1);

        a = Ask(); a.fault = StreamSocketOverTheDescriptor; a.arms = 3;
        r.ceiling_short = measure(a, 2);

        a = Ask(); a.fault = StreamSocketOverTheDescriptor; a.arms = 3;
        a.turnsPerArm = 48;
        r.ceiling_long = measure(a, 3);

        a = Ask(); a.fault = StreamSocketOverTheDescriptor; a.arms = 12;
        r.ceiling_wide = measure(a, 4);

        a = Ask(); a.fault = StreamSocketOverTheDescriptor; a.arms = 3;
        a.repairAt = 3;
        r.heals = measure(a, 5);

        a = Ask(); a.fault = DescriptorClosed;
        r.closed_given_up = measure(a, 6);

        a = Ask(); a.fault = DescriptorClosed; a.arms = 1;
        r.closed_rearmed = measure(a, 7);

        return r;
    }();
    return runs;
}

} //namespace

/*
 * THE FIXTURE, BEFORE ANYTHING IS CONCLUDED FROM IT. Every child read an input
 * before its descriptor was touched. A child that never received the first one
 * would report the same "-" further down for a reason that has nothing to do
 * with this ticket.
 */
TEST(UdpDeafnessRearmCost, EveryChildWasReadingBeforeTheDescriptorAccident)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/7", r.control.before);
    EXPECT_EQ("127.0.0.1/7", r.given_up.before);
    EXPECT_EQ("127.0.0.1/7", r.ceiling_short.before);
    EXPECT_EQ("127.0.0.1/7", r.ceiling_long.before);
    EXPECT_EQ("127.0.0.1/7", r.ceiling_wide.before);
    EXPECT_EQ("127.0.0.1/7", r.heals.before);
    EXPECT_EQ("127.0.0.1/7", r.closed_given_up.before);
    EXPECT_EQ("127.0.0.1/7", r.closed_rearmed.before);
}

/*
 * THE SENTINEL. Every child that asked for an accident got one, and libuv's own
 * word for it is in its journal. Nothing below may be read as "the recovery
 * worked" or "the deafness held" until this case is green: a run that produced
 * no error at all would otherwise report a receiver that never stopped as a
 * receiver that came back.
 */
TEST(UdpDeafnessRearmCost, TheAccidentIsRealInEveryChildThatAskedForOne)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.given_up.log.find(kNotConnected))
            << "no receive failed: this suite measured nothing at all";
    EXPECT_NE(std::string::npos, r.ceiling_short.log.find(kNotConnected));
    EXPECT_NE(std::string::npos, r.ceiling_long.log.find(kNotConnected));
    EXPECT_NE(std::string::npos, r.ceiling_wide.log.find(kNotConnected));
    EXPECT_NE(std::string::npos, r.heals.log.find(kNotConnected));
    EXPECT_NE(std::string::npos, r.closed_given_up.log.find(kBadDescriptor));
    EXPECT_NE(std::string::npos, r.closed_rearmed.log.find(kBadDescriptor));
    EXPECT_EQ(std::string::npos, r.control.log.find(kServerError))
            << "the child that asked for no accident got one anyway";
}

/*
 * THE WITNESS, AND THE HAY THAT MUST NOT CARRY THE TARGET. The same steps
 * without a fault: the exchange itself works, so every "-" below is a server
 * that stopped listening rather than a machine that never delivered anything.
 */
TEST(UdpDeafnessRearmCost, WithoutTheAccidentTheSameExchangeIsServed)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/9", r.control.after);
    EXPECT_EQ("127.0.0.1/8", r.control.last);
    EXPECT_EQ(0u, r.control.reply.find(kDiscoverAnswer));
    EXPECT_EQ(std::string::npos, r.control.log.find(kNoLongerListening));
    EXPECT_EQ(0, r.control.exitCode);
}

/*
 * THE PRICE OF THE CONDUCT THAT SHIPS. One give-up, one journal line, the
 * process still there and every input gone: the box does not die of it, it goes
 * on running without discovery, Wago or KNX.
 */
TEST(UdpDeafnessRearmCost, TheConductThatShipsCostsOneGiveUpAndKeepsTheProcess)
{
    const Runs &r = theRuns();

    EXPECT_EQ(1u, countOf(r.given_up.log, kServerError));
    EXPECT_EQ(0, r.given_up.exitCode);
    EXPECT_EQ(kNothing, r.given_up.after);
    EXPECT_EQ(kNothing, r.given_up.last);
    EXPECT_EQ(kNothing, r.given_up.reply);
}

/*
 * AND WHAT IT SAYS, WHICH IS ONCE. The give-up names this run's port and the
 * three things it takes away, and then the journal never mentions the receiver
 * again however long the process lives - the four-times-longer child says the
 * same single line. An operator who was not reading at that moment has nothing
 * left to find.
 */
TEST(UdpDeafnessRearmCost, TheBoxSaysItWentDeafOnceAndNeverMentionsItAgain)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos,
              r.given_up.log.find(kNoLongerListening + Utils::to_string(r.given_up.port)))
            << "the receiver was given up without saying so, or for another port";
    EXPECT_NE(std::string::npos, r.given_up.log.find("CALAOS_DISCOVER"));
    EXPECT_NE(std::string::npos, r.given_up.log.find("WAGO INT"));
    EXPECT_NE(std::string::npos, r.given_up.log.find("WAGO KNX"));
    EXPECT_EQ(1u, countOf(r.given_up.log, kNoLongerListening));
    EXPECT_EQ(1u, countOf(r.closed_given_up.log, kNoLongerListening));
}

/*
 * THE PRICE OF COMING BACK. Every attempt against a fault that has not healed
 * buys one more failed read and one more give-up: the count is the attempts
 * plus the first one, exactly, so four times the attempts is four times the
 * journal. An unbounded retry is the deluge again, slowed to the rate of
 * whatever schedules it.
 */
TEST(UdpDeafnessRearmCost, EveryRearmAgainstAFaultThatHasNotHealedBuysOneMoreGiveUp)
{
    const Runs &r = theRuns();

    EXPECT_EQ((size_t)(r.ceiling_short.arms + 1), countOf(r.ceiling_short.log, kServerError));
    EXPECT_EQ((size_t)(r.ceiling_wide.arms + 1), countOf(r.ceiling_wide.log, kServerError));
    EXPECT_LT(countOf(r.ceiling_short.log, kServerError),
              countOf(r.ceiling_wide.log, kServerError));
}

/*
 * AND WHY A CEILING OF ATTEMPTS IS A BOUND WHILE A DELAY IS NOT. Two children,
 * the same three attempts, one pumped four times as long: the same count. What
 * bounds the journal is how many times one comes back, never how long one waits
 * between two - so the only structural bound is a number of attempts.
 */
TEST(UdpDeafnessRearmCost, TheCostOfComingBackFollowsTheAttemptsAndNotTheTurns)
{
    const Runs &r = theRuns();

    EXPECT_EQ(r.ceiling_short.arms, r.ceiling_long.arms);
    EXPECT_EQ(countOf(r.ceiling_short.log, kServerError),
              countOf(r.ceiling_long.log, kServerError))
            << "the count of give-ups moved with the number of loop turns: it is "
               "a rate, and nothing here may read one";
}

/*
 * WHAT COMING BACK BUYS, THE ONE TIME IT BUYS ANYTHING. The descriptor put back
 * before the third attempt: everything that arrived during the deafness is
 * still in the socket and comes back in the order it arrived, followed by what
 * came after. Two failed attempts were paid for it.
 */
TEST(UdpDeafnessRearmCost, ARearmRecoversEveryInputInOrderOnceTheFaultHasHealed)
{
    const Runs &r = theRuns();

    EXPECT_EQ(kNothing, r.heals.after)
            << "the child was never deaf, so its recovery measures nothing";
    EXPECT_EQ("127.0.0.1/9+127.0.0.1/8+127.0.0.1/11", r.heals.recovered)
            << "arming the read again did not bring the queued inputs back in "
               "the order they arrived";
    EXPECT_EQ(3u, countOf(r.heals.log, kServerError));
    EXPECT_EQ(0, r.heals.exitCode);
}

/*
 * THE PRICE NOBODY BUDGETED FOR. Over a descriptor that names nothing, arming
 * the read again does not fail - libuv hands the closed number back to
 * epoll_ctl, gets EBADF and calls abort(). The child dies on SIGABRT after
 * saying it went deaf, while the child that stayed deaf over the same fault
 * exits cleanly. A deferred recovery would therefore turn one of the three
 * producers into a dead process, and it would do it without anyone choosing to.
 */
TEST(UdpDeafnessRearmCost, ARearmOverAClosedDescriptorKillsTheProcess)
{
    const Runs &r = theRuns();

    EXPECT_EQ(0, r.closed_given_up.exitCode)
            << "staying deaf over a closed descriptor already cost the process";
    EXPECT_EQ(-SIGABRT, r.closed_rearmed.exitCode)
            << "coming back over a closed descriptor did not kill the child: "
               "the price below is not the one measured";
    EXPECT_EQ(1u, countOf(r.closed_rearmed.log, kServerError))
            << "the child died before or after the give-up, not on the attempt "
               "that followed it";
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_udprearmcost_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
    {
        std::cerr << "could not create a work directory" << std::endl;
        return 1;
    }
    workDir = base;

    return RUN_ALL_TESTS();
}
