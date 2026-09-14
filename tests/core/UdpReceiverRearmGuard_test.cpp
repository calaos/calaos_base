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
 * A UDP RECEIVER THAT COMES BACK FROM A FAULT THAT HEALS, AND REFUSES TO COME
 * BACK FROM ONE THAT TOOK ITS DESCRIPTOR.
 *
 * ---------------------------------------------------------------------------
 * THE TWO THINGS THAT MUST BOTH HOLD
 * ---------------------------------------------------------------------------
 * A failed read stops the receiver - without that one bad datagram is one
 * error per turn of the loop - and nothing used to arm it again, so the box
 * stayed deaf for the life of the process while its heartbeat kept telling its
 * supervisor it was well.
 *
 * Arming the read again is worth a bounded number of attempts BECAUSE a fault
 * that left the descriptor alone hands the whole queue back. It is fatal when
 * the fault CLOSED the descriptor: libuv gives the number to epoll_ctl, gets
 * EBADF and calls abort() without a word, so a recovery written without a look
 * at the descriptor turns a deaf box into a dead process, and does it without
 * anyone choosing to. Both halves are measured here, in the same suite,
 * because either one alone reads like the opposite of a defect.
 *
 * ---------------------------------------------------------------------------
 * HOW THE TWO FAULTS ARE PRODUCED, AND THE ONE SEAM THIS SUITE USES
 * ---------------------------------------------------------------------------
 * epoll keeps its own reference to the file description, so the port stays
 * bound and keeps reporting readable while the read lands on whatever the
 * NUMBER names: a stream socket (ENOTCONN, descriptor intact) or nothing at
 * all (EBADF, descriptor gone). Both are accidents in this process's own
 * descriptor table, which is the only producer the tree is known to have.
 *
 * ⚠️ The seam: a child that wants its fault to HEAL subscribes a listener of
 * its own to the server's handle and puts the descriptor back from inside it,
 * on the nth error. It calls no method of the server and decides nothing about
 * the recovery - the server's own attempt is what finds the descriptor
 * healthy. What it buys is a heal that happens between two attempts and not
 * after a wall-clock delay, so nothing here waits on a duration.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS SUITE REFUSES TO BE
 * ---------------------------------------------------------------------------
 * No skip and no 77. Whether either accident can be produced on this machine
 * is read by CASES THAT GO RED, never by a barrier that would report a clean
 * run having measured nothing. On a machine without an IPv4 loopback this
 * suite fails; that is the choice made, and it is the one the other UDP suites
 * of this tree make.
 *
 * It reads no duration and no rate. Every verdict is a COUNT of journal lines,
 * an input NUMBER, an exit status, or the ATTEMPT NUMBER a line carries; the
 * pair of children that differ only in how long they were pumped must report
 * the same counts, which is what says the give-up is said once and not once a
 * turn.
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
const char *const kListeningAgain = "listening again on port ";

//The two reasons a receiver is given up for good, and they are not the same
//event: one is a fault that outlived the attempts, the other is a descriptor
//that is not there to attempt anything on.
const char *const kAttemptsSpent = "attempts to listen again all failed";
const char *const kDescriptorGone = "the descriptor it was polling is gone";

std::string workDir;

enum Fault
{
    NoFault,
    StreamSocketOverTheDescriptor,
    DescriptorClosed
};

/* Reaches the descriptor libuv polls, the read the server arms, and the error
 * the server hears - none of which UDPServer exposes.
 *
 * healOnErrorNumber() is the seam described in the header: it puts the
 * descriptor back from inside a listener of its own, on the nth error, and
 * touches nothing else. uvw walks its listeners newest first, so this one runs
 * before the server's: the nth error is therefore reported by a server that
 * already has a healthy descriptor to try again on.
 */
class Guard: public UDPServer
{
public:
    using UDPServer::UDPServer;

    int descriptor() const { return (int)handleSrv->fileno(); }
    void armTheReceiveAgain() { handleSrv->recv(); }

    void healOnErrorNumber(int nth, int from, int onto)
    {
        handleSrv->on<uvw::ErrorEvent>([this, nth, from, onto]
                                       (const uvw::ErrorEvent &, uvw::UDPHandle &)
        {
            if (++seen == nth)
                dup2(from, onto);
        });
    }

private:
    int seen = 0;
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
//Stage 1 accumulates: what comes back after a recovery is a queue and its
//ORDER is part of the measurement. The input NUMBER travels with the address,
//so a datagram read twice cannot pass for the next one.
std::string gCarried[2];
int gStage = 0;

void noteWago(std::string ip, int input, bool, std::string)
{
    const std::string one = (ip.empty()? std::string("<empty>"): ip) + "/" +
                            Utils::to_string(input);
    if (gStage == 1 && gCarried[1] != kNothing)
        gCarried[1] += "+" + one;
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

/* One child. healOnError says at which error the descriptor goes back, so a
 * fault that heals between two attempts and one that never heals are the same
 * run with one number changed; forceARearm arms the read by hand AFTER the
 * server has given up, which is the only way to price the abort the guard
 * avoids.
 */
struct Ask
{
    Fault fault = NoFault;
    int healOnError = 0;
    bool forceARearm = false;
    int turns = 12;
};

struct Run
{
    std::string before = kNothing;
    std::string during = kNothing;
    std::string reply = kNothing;
    std::string log;
    int exitCode = -1;
    int port = 0;
};

Run measure(const Ask &ask, int tag)
{
    Run r;

    const std::string base = workDir + "/run" + std::to_string(tag);
    const std::string cfg = base + "/config";
    const std::string cache = base + "/cache";
    const std::string logPath = base + "/log";
    const std::string outPath = base + "/out";
    //Written before the accident and read whatever becomes of the child: one of
    //them dies on purpose, and a child that dies must still be able to say it
    //was reading first.
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
        //give-up written just before is exactly what says the death followed a
        //stop that had already been decided.
        setvbuf(stdout, NULL, _IONBF, 0);
        std::cout << std::unitbuf;

        Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                                 const_cast<char *>(cache.c_str()), true);
        Utils::set_config_option("debug_level", "5");
        Utils::set_config_option("listen_address", "127.0.0.1");

        gCarried[0] = kNothing;
        gCarried[1] = kNothing;
        gStage = 0;
        Utils::signal_wago.connect(sigc::ptr_fun(&noteWago));

        Guard server(asked);

        int peerPort = 0;
        const int c = correspondent(peerPort);

        std::string reply = kNothing;
        if (c >= 0)
        {
            speak(c, asked, "WAGO INT 7 true");
            pump(ask.turns);

            FILE *b = fopen(beforePath.c_str(), "w");
            if (b)
            {
                fprintf(b, "%s\n", gCarried[0].c_str());
                fclose(b);
            }

            const int polled = server.descriptor();
            //Duplicated first: the file description has to outlive the
            //accident, or the port would be released and the datagrams that
            //prove the deafness would go nowhere.
            const int kept = dup(polled);

            if (ask.healOnError > 0)
                server.healOnErrorNumber(ask.healOnError, kept, polled);

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
            pump(ask.turns);

            speak(c, asked, "WAGO INT 8 true");
            pump(ask.turns);

            speak(c, asked, "CALAOS_DISCOVER");
            pump(ask.turns);

            char buf[256];
            const ssize_t n = recv(c, buf, sizeof(buf), 0);
            if (n > 0)
                reply.assign(buf, (size_t)n);

            if (ask.forceARearm)
            {
                server.armTheReceiveAgain();
                pump(ask.turns);
            }

            close(c);
        }

        std::cout.flush();

        FILE *o = fopen(outPath.c_str(), "w");
        if (o)
        {
            fprintf(o, "%s\n%s\n", gCarried[1].c_str(), reply.c_str());
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
    while (fields.size() < 2)
        fields.push_back(kNothing);

    std::string firstRead = slurp(beforePath);
    while (!firstRead.empty() && firstRead[firstRead.size() - 1] == '\n')
        firstRead.erase(firstRead.size() - 1);
    r.before = firstRead.empty()? kNothing: firstRead;
    r.during = fields[0];
    r.reply = fields[1];
    r.log = slurp(logPath);
    return r;
}

struct Runs
{
    //No fault at all: everything below must already work without one.
    Run control;
    //A fault that never heals, and the same one on a child pumped four times
    //as long. The pair is what says the give-up is said once.
    Run deaf, deaf_long;
    //The same fault, healed between two attempts - at the first error and at
    //the second, so the attempt count the journal carries is read twice with
    //two different values.
    Run heal_first, heal_second;
    //And healed one error too late: the attempts are spent, so the box stays
    //deaf on a descriptor that has become healthy again. The price of a bound.
    Run heal_too_late;
    //The descriptor taken away, which is the fault a recovery must refuse.
    Run closed;
    //The same, with the read armed by hand afterwards: the abort the guard is
    //there to avoid, priced in this suite rather than assumed from another.
    Run closed_forced;
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
        r.deaf = measure(a, 1);

        a = Ask(); a.fault = StreamSocketOverTheDescriptor; a.turns = 48;
        r.deaf_long = measure(a, 2);

        a = Ask(); a.fault = StreamSocketOverTheDescriptor; a.healOnError = 1;
        r.heal_first = measure(a, 3);

        a = Ask(); a.fault = StreamSocketOverTheDescriptor; a.healOnError = 2;
        r.heal_second = measure(a, 4);

        a = Ask(); a.fault = StreamSocketOverTheDescriptor; a.healOnError = 4;
        r.heal_too_late = measure(a, 5);

        a = Ask(); a.fault = DescriptorClosed;
        r.closed = measure(a, 6);

        a = Ask(); a.fault = DescriptorClosed; a.forceARearm = true;
        r.closed_forced = measure(a, 7);

        return r;
    }();
    return runs;
}

} //namespace

/*
 * THE FIXTURE, BEFORE ANYTHING IS CONCLUDED FROM IT. Every child read an input
 * before its descriptor was touched. A child that never received the first one
 * would report the same "-" further down for a reason that has nothing to do
 * with any of this.
 */
TEST(UdpReceiverRearmGuard, EveryChildWasReadingBeforeTheDescriptorAccident)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/7", r.control.before);
    EXPECT_EQ("127.0.0.1/7", r.deaf.before);
    EXPECT_EQ("127.0.0.1/7", r.deaf_long.before);
    EXPECT_EQ("127.0.0.1/7", r.heal_first.before);
    EXPECT_EQ("127.0.0.1/7", r.heal_second.before);
    EXPECT_EQ("127.0.0.1/7", r.heal_too_late.before);
    EXPECT_EQ("127.0.0.1/7", r.closed.before);
    EXPECT_EQ("127.0.0.1/7", r.closed_forced.before);
}

/*
 * THE SENTINEL. Every child that asked for an accident got one, and libuv's own
 * word for it is in its journal. Nothing below may be read as "the recovery
 * worked" or "the guard held" until this case is green: a run that produced no
 * error at all would otherwise report a receiver that never stopped as a
 * receiver that came back.
 */
TEST(UdpReceiverRearmGuard, TheAccidentIsRealInEveryChildThatAskedForOne)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.deaf.log.find(kNotConnected))
            << "no receive failed: this suite measured nothing at all";
    EXPECT_NE(std::string::npos, r.deaf_long.log.find(kNotConnected));
    EXPECT_NE(std::string::npos, r.heal_first.log.find(kNotConnected));
    EXPECT_NE(std::string::npos, r.heal_second.log.find(kNotConnected));
    EXPECT_NE(std::string::npos, r.heal_too_late.log.find(kNotConnected));
    EXPECT_NE(std::string::npos, r.closed.log.find(kBadDescriptor));
    EXPECT_NE(std::string::npos, r.closed_forced.log.find(kBadDescriptor));
    EXPECT_EQ(std::string::npos, r.control.log.find(kServerError))
            << "the child that asked for no accident got one anyway";
}

/*
 * THE WITNESS, AND THE HAY THAT MUST NOT CARRY THE TARGET. The same steps
 * without a fault: the exchange itself works, so every "-" below is a server
 * that stopped listening rather than a machine that never delivered anything -
 * and a box that was never deaf says neither of the two things this suite
 * reads.
 */
TEST(UdpReceiverRearmGuard, WithoutTheAccidentTheSameExchangeIsServedAndNothingIsSaid)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/9+127.0.0.1/8", r.control.during);
    EXPECT_EQ(0u, r.control.reply.find(kDiscoverAnswer));
    EXPECT_EQ(std::string::npos, r.control.log.find(kNoLongerListening));
    EXPECT_EQ(std::string::npos, r.control.log.find(kListeningAgain));
    EXPECT_EQ(0, r.control.exitCode);
}

/*
 * ⭐⭐ WHAT COMING BACK BUYS. The fault healed between two attempts, the server
 * armed its own read again, and everything that arrived while it was deaf is
 * still in the socket: it comes back IN THE ORDER it arrived, and the discovery
 * posted after it is answered. Nothing here waits on a duration - the heal
 * happens on an error, which is a count.
 */
TEST(UdpReceiverRearmGuard, ARecoveredReceiverHandsBackEveryInputInOrder)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/9+127.0.0.1/8", r.heal_first.during)
            << "the receiver did not come back, or it came back having lost "
               "what was queued behind the datagram that broke it";
    EXPECT_EQ(0u, r.heal_first.reply.find(kDiscoverAnswer))
            << "the box reads again but no longer answers a discovery";

    EXPECT_EQ("127.0.0.1/9+127.0.0.1/8", r.heal_second.during);
    EXPECT_EQ(0u, r.heal_second.reply.find(kDiscoverAnswer));

    EXPECT_EQ(0, r.heal_first.exitCode);
    EXPECT_EQ(0, r.heal_second.exitCode);
}

/*
 * ⭐ AND THE JOURNAL SAYS HOW MANY ATTEMPTS IT TOOK - the VALUE, not merely a
 * line in the right place. Two children, the same recovery, a fault healed one
 * error apart: each names its own attempt count and neither names the other's.
 * A recovery line with a constant in it satisfies one of the two and fails the
 * other.
 */
TEST(UdpReceiverRearmGuard, TheRecoveryLineSaysHowManyAttemptsItTook)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos,
              r.heal_first.log.find(kListeningAgain +
                                    Utils::to_string(r.heal_first.port) +
                                    " after 1 attempt(s)"))
            << "the recovery was not reported, was reported for another port, "
               "or did not say how many attempts it cost";
    EXPECT_EQ(std::string::npos, r.heal_first.log.find("after 2 attempt(s)"));

    EXPECT_NE(std::string::npos,
              r.heal_second.log.find(kListeningAgain +
                                     Utils::to_string(r.heal_second.port) +
                                     " after 2 attempt(s)"));
    EXPECT_EQ(std::string::npos, r.heal_second.log.find("after 1 attempt(s)"));
}

/*
 * ⛔ AND WHAT THE BOUND COSTS, WHICH HAS TO BE SAID TOO. The same fault healed
 * one error too late: the attempts are spent, so the box stays deaf on a
 * descriptor that is healthy again. That is the price of bounding the attempts
 * rather than retrying for ever, and the journal is where it has to show.
 */
TEST(UdpReceiverRearmGuard, AFaultThatHealsAfterTheAttemptsAreSpentIsNotRecovered)
{
    const Runs &r = theRuns();

    EXPECT_EQ(kNothing, r.heal_too_late.during)
            << "the receiver came back after its attempts were spent: the "
               "ceiling is not bounding anything";
    EXPECT_EQ(std::string::npos, r.heal_too_late.log.find(kListeningAgain));
    EXPECT_NE(std::string::npos, r.heal_too_late.log.find(kAttemptsSpent));
    EXPECT_EQ(0, r.heal_too_late.exitCode);
}

/*
 * ⛔⭐⭐ THE GUARD. Over a descriptor that names nothing, the receiver does NOT
 * arm its read again: it looks at the descriptor first, finds it gone, and
 * stops there. The child lives.
 *
 * Its partner is what prices that. The same child, the same closed descriptor,
 * one read armed BY HAND afterwards: libuv hands the closed number to
 * epoll_ctl, gets EBADF and calls abort() without a word, and the child dies on
 * SIGABRT after having said it went deaf. So the exit status above is the
 * abort that did not happen, and not a machine on which the abort cannot
 * happen at all.
 */
TEST(UdpReceiverRearmGuard, AClosedDescriptorIsNeverArmedAgainAndTheProcessLives)
{
    const Runs &r = theRuns();

    EXPECT_EQ(0, r.closed.exitCode)
            << "the receiver armed its read again over a closed descriptor: "
               "libuv aborts on that, and a deaf box became a dead process";
    EXPECT_EQ(-SIGABRT, r.closed_forced.exitCode)
            << "arming the read by hand over the same closed descriptor did "
               "NOT kill the child: the abort this guard avoids is not the one "
               "measured here";
    EXPECT_EQ(1u, countOf(r.closed_forced.log, kNoLongerListening))
            << "the child died before or after the give-up, not on the read "
               "that was forced after it";
}

/*
 * ⭐⭐ AND IT SAYS WHICH OF THE TWO HAPPENED. "Given up" is not one event: a
 * fault that outlived the attempts and a descriptor that is not there any more
 * are different accidents and want different answers from whoever reads the
 * journal. Each child carries its own reason and neither carries the other's -
 * a give-up line with one constant reason in it passes one of these and fails
 * the other.
 */
TEST(UdpReceiverRearmGuard, TheGiveUpSaysWhyItWillNotComeBack)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.deaf.log.find(kAttemptsSpent))
            << "the box gave the receiver up without saying that it had tried";
    EXPECT_EQ(std::string::npos, r.deaf.log.find(kDescriptorGone))
            << "a fault that left the descriptor alone was reported as a "
               "descriptor that had gone away";

    EXPECT_NE(std::string::npos, r.closed.log.find(kDescriptorGone))
            << "the receiver was given up over a closed descriptor without "
               "saying that this is why it will not be armed again";
    EXPECT_EQ(std::string::npos, r.closed.log.find(kAttemptsSpent))
            << "a descriptor that had gone away was reported as attempts that "
               "were spent: the guard is not what stopped the recovery";
}

/*
 * WHAT THE JOURNAL OWES ITS READER, AND IT OWES IT ONCE. The give-up names the
 * port the kernel handed THIS run and the three things it takes away; then
 * nothing mentions the receiver again however long the process lives. The
 * child pumped four times as long says the same single line - this counts, it
 * does not time - and the attempts cost the journal nothing: one error line,
 * one give-up, whether the receiver tried three times or not at all.
 */
TEST(UdpReceiverRearmGuard, TheBoxSaysItWentDeafOnceWhateverItTried)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos,
              r.deaf.log.find(kNoLongerListening + Utils::to_string(r.deaf.port)))
            << "the receiver was given up without saying so, or for another port";
    EXPECT_NE(std::string::npos, r.deaf.log.find("CALAOS_DISCOVER"));
    EXPECT_NE(std::string::npos, r.deaf.log.find("WAGO INT"));
    EXPECT_NE(std::string::npos, r.deaf.log.find("WAGO KNX"));

    EXPECT_EQ(1u, countOf(r.deaf.log, kNoLongerListening));
    EXPECT_EQ(1u, countOf(r.deaf.log, kServerError))
            << "coming back cost one journal line per attempt: the ceiling "
               "bounds the attempts, and the attempts must not spend the "
               "journal";

    EXPECT_EQ(countOf(r.deaf.log, kNoLongerListening),
              countOf(r.deaf_long.log, kNoLongerListening))
            << "the count of give-ups moved with the number of loop turns: it "
               "is a rate, and nothing here may read one";
    EXPECT_EQ(countOf(r.deaf.log, kServerError),
              countOf(r.deaf_long.log, kServerError));

    EXPECT_EQ(1u, countOf(r.closed.log, kNoLongerListening));
    EXPECT_EQ(1u, countOf(r.closed.log, kServerError));
}

/*
 * AND THE COST OF THE DEAFNESS ITSELF IS UNCHANGED. A receiver that could not
 * be recovered still takes every input the box has with it: Wago, KNX, and the
 * discovery an installer looks for it with. The recovery does not soften this,
 * it only decides how often it happens.
 */
TEST(UdpReceiverRearmGuard, AReceiverGivenUpStillCostsEveryInputTheBoxHas)
{
    const Runs &r = theRuns();

    EXPECT_EQ(kNothing, r.deaf.during);
    EXPECT_EQ(kNothing, r.deaf.reply);
    EXPECT_EQ(kNothing, r.closed.during);
    EXPECT_EQ(kNothing, r.closed.reply);
    EXPECT_EQ(0, r.deaf.exitCode);
    EXPECT_EQ(0, r.deaf_long.exitCode);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_udprearmguard_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
    {
        std::cerr << "could not create a work directory" << std::endl;
        return 1;
    }
    workDir = base;

    return RUN_ALL_TESTS();
}
