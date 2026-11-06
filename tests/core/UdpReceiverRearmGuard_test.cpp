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
 * BACK ON A NUMBER THAT IS NO LONGER THE SOCKET IT BOUND.
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
 * that left the socket alone hands the whole queue back. It is fatal when the
 * NUMBER no longer names that socket, and validity is not the question: a free
 * number makes libuv abort() inside epoll_ctl without a word, and a number the
 * next socket() has taken is perfectly valid, blocking, and somebody else's -
 * the read lands in recvmsg() and never returns, taking uv_run and every other
 * input of the box with it. Both halves are measured here, in the same suite,
 * because either one alone reads like the opposite of a defect.
 *
 * ---------------------------------------------------------------------------
 * HOW THE FAULTS ARE PRODUCED, AND THE SEAMS THIS SUITE USES
 * ---------------------------------------------------------------------------
 * epoll keeps its own reference to the file description, so the port stays
 * bound and keeps reporting readable while the read lands on whatever the
 * NUMBER names: a stream socket (ENOTCONN), nothing at all (a free number), or
 * the UDP socket the next socket() call was handed. All three are accidents in
 * this process's own descriptor table, which is the only producer the tree is
 * known to have.
 *
 * Three seams, and each one is declared where it is used:
 *  - healOnLookNumber(): puts the descriptor back from inside the look the
 *    server takes before an attempt. It decides nothing about the recovery -
 *    the server's own look is what finds the socket back - and it buys a heal
 *    that happens between two attempts rather than after a wall-clock delay.
 *  - healOnErrorNumber(): the same, driven by the nth error instead, for the
 *    child whose descriptor comes back once the attempts are already spent.
 *  - pretendTheDescriptorIsOurs(): stands the identity check down. It is the
 *    mutant, kept in the tree: with it, the child whose number was handed out
 *    again FREEZES, which is what says the check carries the weight.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS SUITE REFUSES TO BE
 * ---------------------------------------------------------------------------
 * No skip and no 77. Whether an accident can be produced on this machine is
 * read by CASES THAT GO RED, never by a barrier that would report a clean run
 * having measured nothing. On a machine without an IPv4 loopback this suite
 * fails; that is the choice made, and it is the one the other UDP suites of
 * this tree make.
 *
 * It reads no duration and no rate. Every verdict is a COUNT of journal lines,
 * an input NUMBER, an exit status, or the ATTEMPT NUMBER a line carries. The
 * pumping budget and the alarm each child carries are BOUNDS and never
 * verdicts: a child that hangs has to end as a CODE, because a frozen child
 * and a slow one are otherwise the same thing, and the pair of children that
 * differ only in how long they were pumped must report the same counts.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/prctl.h>
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

const char *const kDiscoverAnswer = "CALAOS_IP ";

//libuv's words for the two errnos this suite produces.
const char *const kNotConnected = "socket is not connected";
const char *const kBadDescriptor = "bad file descriptor";

const char *const kServerError = "UDP server error";
const char *const kNoLongerListening = "no longer listening on port ";
const char *const kListeningAgain = "listening again on port ";

//The two reasons a receiver is given up for good, and they are not the same
//event: one is a fault that outlived the attempts, the other is a number that
//stopped naming the socket there was anything to attempt on.
const char *const kAttemptsSpent = "attempts to listen again all failed";
const char *const kNotOurs = "no longer names the socket it bound";

std::string workDir;

enum Fault
{
    NoFault,
    StreamSocketOverTheDescriptor,
    //The number left free, and the number the next socket() was handed. Both
    //are "the descriptor is gone"; only one of them answers "valid".
    DescriptorClosed,
    DescriptorHandedOutAgain
};

/* Reaches the descriptor libuv polls, the read the server arms, the error the
 * server hears and the look the server takes before an attempt - none of which
 * UDPServer exposes.
 *
 * The two heal seams put the descriptor back from inside something the server
 * was going to do anyway, on the nth occurrence; neither calls a method of the
 * server nor decides anything about the recovery.
 */
class Guard: public UDPServer
{
public:
    using UDPServer::UDPServer;

    int descriptor() const { return (int)handleSrv->fileno(); }
    void armTheReceiveAgain() { handleSrv->recv(); }

    void healOnLookNumber(int nth, int from, int onto)
    {
        nthLook = nth;
        healFrom = from;
        healOnto = onto;
    }

    //Stands the identity check down: the mutant, kept in the tree.
    void standTheIdentityCheckDown() { pretend = true; }

    void healOnErrorNumber(int nth, int from, int onto)
    {
        handleSrv->on<uvw::ErrorEvent>([this, nth, from, onto]
                                       (const uvw::ErrorEvent &, uvw::UDPHandle &)
        {
            if (++seenErrors == nth)
                dup2(from, onto);
        });
    }

    //Closes the number the server polls and lets the next socket() have it,
    //between the error and the attempt. Writes down whether it really got it:
    //an accident that did not happen must not read like a guard that held.
    void reclaimTheNumberOnFirstError(const std::string &saidAt)
    {
        handleSrv->on<uvw::ErrorEvent>([this, saidAt]
                                       (const uvw::ErrorEvent &, uvw::UDPHandle &)
        {
            if (reclaimed)
                return;
            reclaimed = true;

            const int was = (int)handleSrv->fileno();
            close(was);
            const int now = socket(AF_INET, SOCK_DGRAM, 0);
            FILE *f = fopen(saidAt.c_str(), "w");
            if (f)
            {
                fprintf(f, "%s\n", now == was? "YES": "NO");
                fclose(f);
            }
        });
    }

protected:
    bool descriptorIsStillOurs() const override
    {
        if (nthLook > 0 && ++looks == nthLook)
            dup2(healFrom, healOnto);
        return pretend? true: UDPServer::descriptorIsStillOurs();
    }

private:
    mutable int looks = 0;
    int nthLook = 0, healFrom = -1, healOnto = -1;
    int seenErrors = 0;
    bool pretend = false;
    bool reclaimed = false;
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

/* Turns of pumping. The first is only a settle; the one that follows the
 * accident has to outlast the whole bounded recovery, and the long child exists
 * to say that the counts below do not move with it.
 */
const int kSettle = 12;
const int kAfterTheAccident = 300;
const int kPumpedFarLonger = 900;

//Seconds a child may take before something kills it. A bound, never a verdict:
//a child that hangs must end as a CODE, since a frozen child and a slow one
//look the same to everything else.
const unsigned kChildBound = 10;

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

/* Turns of the loop, never a wait on one that will not come. The budget also
 * bounds an unbounded burst, so a tree that produces one fails a count instead
 * of never finishing.
 *
 * It has to outlast the holds between two attempts, which are
 * ExternProcServer::respawnDelay() of the attempts already made: nothing here
 * reads that, the budget is simply longer than the three of them together.
 */
void pump(int turns)
{
    auto loop = uvw::Loop::getDefault();
    for (int i = 0; i < turns; i++)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        usleep(2000);
    }
}

/* The same, for the one child whose outage has to LAST: it puts the descriptor
 * back once the given number of turns have gone by, which is a number of turns
 * and not a reading of a clock. One turn is the usleep above, so the outage is
 * long enough to outlive the first attempt and short enough to be caught by a
 * later one - and with no hold between attempts it outlives all three.
 */
void pumpThroughAnOutage(int turns, int healAfterTurns, int from, int onto)
{
    auto loop = uvw::Loop::getDefault();
    for (int i = 0; i < turns; i++)
    {
        if (i == healAfterTurns)
            dup2(from, onto);
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

/* One child. A fault that heals between two attempts and one that never heals
 * are the same run with one number changed; forceARearm arms the read by hand
 * AFTER the server has given up, which is the only way to price what the guard
 * refuses to do.
 */
struct Ask
{
    Fault fault = NoFault;
    //The look, or the error, the descriptor goes back on. Zero: never.
    int healOnLook = 0;
    int healOnError = 0;
    //Turns of the post-accident pump the descriptor stays away for. Zero: the
    //two seams above decide instead.
    int healAfterTurns = 0;
    bool standTheCheckDown = false;
    bool forceARearm = false;
    int turns = kAfterTheAccident;
};

struct Run
{
    std::string same = kNothing;
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
    //Whether the number really came back to this process, for the one child
    //that asks for it.
    const std::string samePath = base + "/same";
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
        //The child aborts on purpose: no core helper (systemd-coredump, abrt)
        //on the host for it. The signal is unchanged.
        prctl(PR_SET_DUMPABLE, 0);
        alarm(kChildBound);

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
            pump(kSettle);

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

            if (ask.healOnLook > 0)
                server.healOnLookNumber(ask.healOnLook, kept, polled);
            if (ask.healOnError > 0)
                server.healOnErrorNumber(ask.healOnError, kept, polled);
            if (ask.standTheCheckDown)
                server.standTheIdentityCheckDown();
            if (ask.fault == DescriptorHandedOutAgain)
                server.reclaimTheNumberOnFirstError(samePath);

            if (ask.fault == DescriptorClosed)
            {
                close(polled);
            }
            else if (ask.fault != NoFault)
            {
                //A stream socket is what makes the read fail. For the child
                //whose number is handed out again it is only the trigger: the
                //listener above closes the number on that first error and lets
                //the next socket() have it, which is the accident itself.
                const int stream = socket(AF_INET, SOCK_STREAM, 0);
                dup2(stream, polled);
            }

            gStage = 1;
            speak(c, asked, "WAGO KNX 9 true");
            if (ask.healAfterTurns > 0)
                pumpThroughAnOutage(ask.turns, ask.healAfterTurns, kept, polled);
            else
                pump(ask.turns);

            speak(c, asked, "WAGO INT 8 true");
            pump(kSettle);

            speak(c, asked, "CALAOS_DISCOVER");
            pump(kSettle);

            char buf[256];
            const ssize_t n = recv(c, buf, sizeof(buf), 0);
            if (n > 0)
                reply.assign(buf, (size_t)n);

            if (ask.forceARearm)
            {
                server.armTheReceiveAgain();
                pump(kSettle);
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

    std::string reclaimed = slurp(samePath);
    while (!reclaimed.empty() && reclaimed[reclaimed.size() - 1] == '\n')
        reclaimed.erase(reclaimed.size() - 1);
    r.same = reclaimed.empty()? kNothing: reclaimed;

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
    //A number that stops being ours and never comes back, and the same child
    //pumped three times as long. The pair is what says the give-up is said once.
    Run not_ours, not_ours_long;
    //The same fault, the descriptor put back between two attempts - at the
    //first look and at the second, so the attempt count the journal carries is
    //read twice with two different values.
    Run heal_first, heal_second;
    //Identity stood down and the read failing for real: the only way this tree
    //can reach the ceiling, since every accident it can produce is an accident
    //of identity. Healed one error too late, so the price of the bound shows.
    Run spent;
    //The number left free, which is the accident libuv abort()s on.
    Run closed;
    //The same, with the read armed by hand afterwards: the abort the guard is
    //there to avoid, priced in this suite rather than assumed from another.
    Run closed_forced;
    //An outage that lasts far longer than a turn of the loop and is over well
    //before the attempts are: what the holds between attempts buy, and the only
    //child here that goes red if they are taken away.
    Run outage;
    //⭐ The number closed and handed straight to the next socket(). It answers
    //"valid" to anything that only asks whether it exists.
    Run handed_out;
    //⭐ And the same child with the identity check stood down: it freezes in
    //recvmsg() on a stranger's socket and has to be killed.
    Run handed_out_unchecked;
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
        r.not_ours = measure(a, 1);

        a = Ask(); a.fault = StreamSocketOverTheDescriptor;
        a.turns = kPumpedFarLonger;
        r.not_ours_long = measure(a, 2);

        a = Ask(); a.fault = StreamSocketOverTheDescriptor; a.healOnLook = 1;
        r.heal_first = measure(a, 3);

        a = Ask(); a.fault = StreamSocketOverTheDescriptor; a.healOnLook = 2;
        r.heal_second = measure(a, 4);

        a = Ask(); a.fault = StreamSocketOverTheDescriptor;
        a.standTheCheckDown = true; a.healOnError = 4;
        r.spent = measure(a, 5);

        a = Ask(); a.fault = StreamSocketOverTheDescriptor; a.healAfterTurns = 50;
        r.outage = measure(a, 10);

        a = Ask(); a.fault = DescriptorClosed;
        r.closed = measure(a, 6);

        a = Ask(); a.fault = DescriptorClosed; a.forceARearm = true;
        r.closed_forced = measure(a, 7);

        a = Ask(); a.fault = DescriptorHandedOutAgain;
        r.handed_out = measure(a, 8);

        a = Ask(); a.fault = DescriptorHandedOutAgain; a.standTheCheckDown = true;
        r.handed_out_unchecked = measure(a, 9);

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
    EXPECT_EQ("127.0.0.1/7", r.not_ours.before);
    EXPECT_EQ("127.0.0.1/7", r.not_ours_long.before);
    EXPECT_EQ("127.0.0.1/7", r.heal_first.before);
    EXPECT_EQ("127.0.0.1/7", r.heal_second.before);
    EXPECT_EQ("127.0.0.1/7", r.spent.before);
    EXPECT_EQ("127.0.0.1/7", r.outage.before);
    EXPECT_EQ("127.0.0.1/7", r.closed.before);
    EXPECT_EQ("127.0.0.1/7", r.closed_forced.before);
    EXPECT_EQ("127.0.0.1/7", r.handed_out.before);
    EXPECT_EQ("127.0.0.1/7", r.handed_out_unchecked.before);
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

    EXPECT_NE(std::string::npos, r.not_ours.log.find(kNotConnected))
            << "no receive failed: this suite measured nothing at all";
    EXPECT_NE(std::string::npos, r.not_ours_long.log.find(kNotConnected));
    EXPECT_NE(std::string::npos, r.heal_first.log.find(kNotConnected));
    EXPECT_NE(std::string::npos, r.heal_second.log.find(kNotConnected));
    EXPECT_NE(std::string::npos, r.spent.log.find(kNotConnected));
    EXPECT_NE(std::string::npos, r.outage.log.find(kNotConnected));
    EXPECT_NE(std::string::npos, r.closed.log.find(kBadDescriptor));
    EXPECT_NE(std::string::npos, r.closed_forced.log.find(kBadDescriptor));
    EXPECT_EQ(std::string::npos, r.control.log.find(kServerError))
            << "the child that asked for no accident got one anyway";
}

/*
 * ⭐⭐⭐ AND THE ONE ACCIDENT THAT ONLY COUNTS IF THE NUMBER REALLY CAME BACK.
 * Both children closed the number libuv polls and asked the kernel for another
 * socket; both must say the kernel handed them THE SAME NUMBER. Without this,
 * the two cases below are a guard congratulating itself on an accident that
 * never happened.
 */
TEST(UdpReceiverRearmGuard, TheNumberWasReallyHandedOutAgainInBothChildren)
{
    const Runs &r = theRuns();

    EXPECT_EQ("YES", r.handed_out.same)
            << "the closed number was not handed back to this process, so "
               "nothing here measured a descriptor that belongs to a stranger";
    EXPECT_EQ("YES", r.handed_out_unchecked.same);
    EXPECT_NE(std::string::npos, r.handed_out.log.find(kNotConnected));
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
 * ⭐⭐⭐ THE ONE THAT DECIDES EVERYTHING. A number closed and handed straight to
 * the next socket() answers "valid" to anything that only asks whether it
 * exists - and the socket behind it is blocking and belongs to somebody else,
 * so a read armed on it never returns and uv_run never turns again. The child
 * that looks at IDENTITY refuses the attempt, says so, and EXITS.
 *
 * Its partner is what prices that, and it is the mutant kept in the tree: the
 * same accident with the identity check stood down freezes and has to be
 * killed. So the exit status above is a freeze that did not happen, and not a
 * machine on which the freeze cannot happen at all.
 */
TEST(UdpReceiverRearmGuard, ANumberHandedOutAgainIsNeverArmedAndTheLoopKeepsTurning)
{
    const Runs &r = theRuns();

    EXPECT_EQ(0, r.handed_out.exitCode)
            << "the child did not exit on its own: the read was armed over a "
               "socket that is not ours and the whole loop is stuck in it";
    EXPECT_NE(std::string::npos, r.handed_out.log.find(kNotOurs))
            << "the receiver was given up without saying that the number had "
               "stopped naming the socket it bound";
    EXPECT_EQ(1u, countOf(r.handed_out.log, kNoLongerListening));

    EXPECT_EQ(-SIGALRM, r.handed_out_unchecked.exitCode)
            << "the same accident with the identity check stood down did NOT "
               "freeze the child: the check is not what keeps the loop alive "
               "here, and nothing in this suite holds it";
    EXPECT_EQ(std::string::npos, r.handed_out_unchecked.log.find(kNoLongerListening))
            << "the child that froze said it had given up first: it was killed "
               "somewhere else than on the read it was not supposed to arm";
}

/*
 * ⭐⭐ WHAT COMING BACK BUYS. The descriptor was back by the time the server
 * looked, the server armed its own read again, and everything that arrived
 * while it was deaf is still in the socket: it comes back IN THE ORDER it
 * arrived, and the discovery posted after it is answered. Nothing here waits on
 * a duration - the heal happens on a look, which is a count.
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
 * ⭐⭐ AND WHAT THE HOLD BETWEEN TWO ATTEMPTS BUYS. This child's descriptor
 * stays away for FIFTY turns of the loop - a shortage a box really can have -
 * and comes back well before the attempts are spent. It is recovered.
 *
 * ⛔ With the attempts crowded into consecutive turns, as they were before the
 * hold was taken from ExternProcServer::respawnDelay(), all three are over
 * inside a millisecond and this child stays deaf. That is the whole of what the
 * hold is for, and this is the case that says so. The verdict is still what
 * came back and not how long anything took.
 */
TEST(UdpReceiverRearmGuard, AnOutageLongerThanATurnOfTheLoopIsStillRecoveredFrom)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/9+127.0.0.1/8", r.outage.during)
            << "an outage a few turns long outlived every attempt: the "
               "attempts are crowded into one instant and bound nothing real";
    EXPECT_NE(std::string::npos, r.outage.log.find(kListeningAgain));
    EXPECT_EQ(std::string::npos, r.outage.log.find(kNoLongerListening));
    EXPECT_EQ(0, r.outage.exitCode);
}

/*
 * ⭐ AND THE JOURNAL SAYS HOW MANY ATTEMPTS IT TOOK - the VALUE, not merely a
 * line in the right place. Two children, the same recovery, a descriptor back
 * one look apart: each names its own attempt count and neither names the
 * other's. A recovery line with a constant in it satisfies one of the two and
 * fails the other.
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
 * ⛔ AND WHAT THE BOUND COSTS, WHICH HAS TO BE SAID TOO. The identity check
 * stood down so that the READ is what keeps failing: the attempts run out, and
 * the descriptor that comes back one error too late buys nothing. That is the
 * price of bounding the attempts rather than retrying for ever, and it is also
 * the only path in this tree that reaches the ceiling at all - every accident
 * the tree can really produce is an accident of identity.
 */
TEST(UdpReceiverRearmGuard, AFaultThatHealsAfterTheAttemptsAreSpentIsNotRecovered)
{
    const Runs &r = theRuns();

    EXPECT_EQ(kNothing, r.spent.during)
            << "the receiver came back after its attempts were spent: the "
               "ceiling is not bounding anything";
    EXPECT_EQ(std::string::npos, r.spent.log.find(kListeningAgain));
    EXPECT_NE(std::string::npos, r.spent.log.find(kAttemptsSpent));
    EXPECT_EQ(std::string::npos, r.spent.log.find(kNotOurs))
            << "a read that kept failing on a socket that was ours was "
               "reported as a number that had stopped being ours";
    EXPECT_EQ(0, r.spent.exitCode);
}

/*
 * ⛔⭐⭐ THE OTHER HALF OF THE GUARD. Over a number that was left FREE, the
 * receiver does not arm its read either: libuv hands that number to epoll_ctl,
 * gets EBADF and calls abort() without a word. The child lives.
 *
 * Its partner prices it the same way the freeze above is priced: the same
 * child, the same free number, one read armed BY HAND afterwards, and it dies
 * on SIGABRT having already said it went deaf.
 */
TEST(UdpReceiverRearmGuard, AFreeNumberIsNeverArmedAgainAndTheProcessLives)
{
    const Runs &r = theRuns();

    EXPECT_EQ(0, r.closed.exitCode)
            << "the receiver armed its read again over a free number: libuv "
               "aborts on that, and a deaf box became a dead process";
    EXPECT_EQ(-SIGABRT, r.closed_forced.exitCode)
            << "arming the read by hand over the same free number did NOT kill "
               "the child: the abort this guard avoids is not the one measured "
               "here";
    EXPECT_EQ(1u, countOf(r.closed_forced.log, kNoLongerListening))
            << "the child died before or after the give-up, not on the read "
               "that was forced after it";
}

/*
 * ⭐⭐ AND IT SAYS WHICH OF THE TWO HAPPENED. "Given up" is not one event: a
 * fault that outlived the attempts and a number that stopped naming the socket
 * are different accidents and want different answers from whoever reads the
 * journal. Each child carries its own reason and neither carries the other's -
 * a give-up line with one constant reason in it passes one of these and fails
 * the other.
 */
TEST(UdpReceiverRearmGuard, TheGiveUpSaysWhyItWillNotComeBack)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.spent.log.find(kAttemptsSpent))
            << "the box gave the receiver up without saying that it had tried";
    EXPECT_EQ(std::string::npos, r.spent.log.find(kNotOurs));

    EXPECT_NE(std::string::npos, r.not_ours.log.find(kNotOurs))
            << "the receiver was given up over a number that is not ours "
               "without saying that this is why it will not be armed again";
    EXPECT_EQ(std::string::npos, r.not_ours.log.find(kAttemptsSpent))
            << "a number that had stopped being ours was reported as attempts "
               "that were spent: the guard is not what stopped the recovery";

    EXPECT_NE(std::string::npos, r.closed.log.find(kNotOurs));
    EXPECT_EQ(std::string::npos, r.closed.log.find(kAttemptsSpent));
}

/*
 * WHAT THE JOURNAL OWES ITS READER, AND IT OWES IT ONCE. The give-up names the
 * port the kernel handed THIS run and the three things it takes away; then
 * nothing mentions the receiver again however long the process lives. The
 * child pumped three times as long says the same single line - this counts, it
 * does not time - and the attempts cost the journal nothing: one error line,
 * one give-up, whether the receiver tried three times or not at all.
 */
TEST(UdpReceiverRearmGuard, TheBoxSaysItWentDeafOnceWhateverItTried)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos,
              r.not_ours.log.find(kNoLongerListening +
                                  Utils::to_string(r.not_ours.port)))
            << "the receiver was given up without saying so, or for another port";
    EXPECT_NE(std::string::npos, r.not_ours.log.find("CALAOS_DISCOVER"));
    EXPECT_NE(std::string::npos, r.not_ours.log.find("WAGO INT"));
    EXPECT_NE(std::string::npos, r.not_ours.log.find("WAGO KNX"));

    EXPECT_EQ(1u, countOf(r.not_ours.log, kNoLongerListening));
    EXPECT_EQ(1u, countOf(r.not_ours.log, kServerError))
            << "coming back cost one journal line per attempt: the ceiling "
               "bounds the attempts, and the attempts must not spend the "
               "journal";

    EXPECT_EQ(countOf(r.not_ours.log, kNoLongerListening),
              countOf(r.not_ours_long.log, kNoLongerListening))
            << "the count of give-ups moved with the number of loop turns: it "
               "is a rate, and nothing here may read one";
    EXPECT_EQ(countOf(r.not_ours.log, kServerError),
              countOf(r.not_ours_long.log, kServerError));

    EXPECT_EQ(1u, countOf(r.spent.log, kNoLongerListening));
    EXPECT_EQ(1u, countOf(r.spent.log, kServerError));
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

    EXPECT_EQ(kNothing, r.not_ours.during);
    EXPECT_EQ(kNothing, r.not_ours.reply);
    EXPECT_EQ(kNothing, r.closed.during);
    EXPECT_EQ(kNothing, r.closed.reply);
    EXPECT_EQ(kNothing, r.handed_out.during);
    EXPECT_EQ(kNothing, r.handed_out.reply);
    EXPECT_EQ(0, r.not_ours.exitCode);
    EXPECT_EQ(0, r.not_ours_long.exitCode);
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
