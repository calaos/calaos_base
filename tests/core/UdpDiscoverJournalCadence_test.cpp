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
 * THE OTHER JOURNAL LINE OF processRequest(), AND WHAT A CEILING THAT NEVER
 * REARMS IS WORTH.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS MEASURED HERE, IN TWO HALVES
 * ---------------------------------------------------------------------------
 * Both halves read the SAME property - who decides how many lines the journal
 * gets - on the two places of UDPServer where the answer is "whoever speaks on
 * the network", and CALAOS_DISCOVER asks for no credentials.
 *
 * FIRST HALF, the receive path. A discovery whose source no local address
 * reaches wrote one ERROR line per datagram, with no ceiling and no stop. It
 * sits ten lines below the send branch a previous ticket bounded, in the same
 * function, and nothing read it.
 *
 * SECOND HALF, and it is a characterisation, not a repair. The send ramp counts
 * refusals for the life of the process and never clears the count. The only
 * caller of sendTo() is the answer to an unauthenticated discovery, so the
 * count is a thing the network writes: a peer drives it to a power of two, and
 * the NEXT failure - a different correspondent, a real one, lost for a real
 * reason - is not written at all. A ceiling that cannot be rearmed is not a
 * ceiling, it is a switch with an outside handle. The cases below pin that
 * silence rather than repair it, because the rearming that would repair it is
 * an open arbitration.
 *
 * ---------------------------------------------------------------------------
 * HOW EACH ACCIDENT IS PRODUCED, AND WHAT IS INJECTED
 * ---------------------------------------------------------------------------
 * A discovery nothing can answer: GetLocalIPFor() opens a UDP socket to learn
 * which local address reaches the correspondent, so a box with no descriptor
 * left has no local address for ANY source. The soft RLIMIT_NOFILE is lowered
 * to the lowest free descriptor for the length of the flood, which is the one
 * shape that cannot fail to bite - and the discover path burns a descriptor per
 * datagram, so the state is one the box reaches on its own.
 *
 * A refused answer: the server binds 127.0.0.1 and is told to answer an address
 * of the documentation range, which the kernel refuses to route from a loopback
 * source (EINVAL).
 *
 * EVERY DATAGRAM OF THE FLOOD HAS ITS OWN SOURCE, 127.1.0.1 upwards, so the
 * journal says WHICH discoveries were reported and not merely how many: keeping
 * the first eight, the last eight or every sixteenth all pass a count of eight
 * and name different correspondents. The second half names its late failure on
 * a different range again (198.51.100.7), so "the ceiling was bought" and "the
 * next failure was reported" can never be the same string.
 *
 * No skip, no 77. Whether either accident can be produced on this machine is
 * read by cases that go RED, never by a barrier that would report a clean run
 * after measuring nothing.
 *
 * WHAT THIS SUITE ASSUMES OF THE MACHINE, out loud:
 *   - 127.0.0.1 can be bound, and so can other addresses of 127/8;
 *   - RLIMIT_NOFILE can be lowered and restored by the process itself;
 *   - 192.0.2.0/24 and 198.51.100.0/24 are not configured here, and a datagram
 *     sent to them from a socket bound to 127.0.0.1 is refused.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/resource.h>
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

//Datagrams the flood sends, and answers the purchase run has refused. Large
//enough that one line each is unmistakable next to one line per doubling, and
//small enough that each one still gets its own correspondent.
const int kFlood = 130;

//Answered/refused pairs of the alternating run. Twenty refusals put the last
//doubling at sixteen, a count no other run of this suite writes, so no case can
//read the wrong run's journal and pass.
const int kAlternations = 20;

//What a slot reports when nothing at all reached it. A sensor that measured
//nothing must not read like a sensor that found nothing.
const char *const kNothing = "-";

const char *const kDiscoverAnswer = "CALAOS_IP ";

//The line this ticket bounds, and the line its callee writes on the same path.
//The second one is NOT bounded by anything: it is read here so that the census
//it belongs to is measured rather than asserted.
const char *const kNoInterface = "No interface found corresponding to network";
const char *const kCalleeCannotAsk = "Can't create socket";

//The opening of a refused-send line, and libuv's word for EINVAL.
const char *const kSendFailed = "UDP send to ";
const char *const kRefusalReason = "invalid argument";

//The correspondent of the late failure of the second half. A range of its own,
//so no case can confuse it with one of the refusals that bought the silence.
const char *const kLatePeer = "198.51.100.7";

std::string workDir;

//Sources of the flood. 127/8 is local in its entirety, so each datagram can
//come from its own address without configuring anything.
std::string floodSource(int nth)
{
    return "127.1.0." + Utils::to_string(nth);
}

std::string boughtPeer(int nth)
{
    return "192.0.2." + Utils::to_string(nth);
}

//Reaches the send the server uses with a destination this suite picks. Nothing
//else in the tree can hand UDPServer a correspondent that has gone away; the
//receive half below needs no seam at all and uses none.
class Discovery: public UDPServer
{
public:
    using UDPServer::UDPServer;

    void answer(const std::string &ip, unsigned int p)
    {
        kept.push_back("CALAOS_IP 127.0.0.1");
        sendTo(ip, p, kept.back());
    }

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
std::string gCarried[2];
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

//A socket bound to ip, on a port the kernel picks. Used both for the sentinel
//correspondent and for the flood sources.
int speaker(const std::string &ip, int &port)
{
    const int c = socket(AF_INET, SOCK_DGRAM, 0);
    if (c < 0)
        return -1;

    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    inet_pton(AF_INET, ip.c_str(), &a.sin_addr);
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

/* No descriptor can be opened while this object lives, and every descriptor
 * already open keeps working.
 *
 * dup() hands back the lowest free number, so a soft limit set to that number
 * forbids every later allocation without touching what is open. Lowering the
 * limit is the only form that cannot fail to bite: exhausting by allocation
 * depends on what the limit happens to be.
 */
class NoDescriptorLeft
{
public:
    NoDescriptorLeft(): armed(false)
    {
        if (getrlimit(RLIMIT_NOFILE, &saved) != 0)
            return;

        const int probe = dup(STDIN_FILENO);
        if (probe < 0)
            return;
        close(probe);

        rlimit low = saved;
        low.rlim_cur = (rlim_t)probe;
        armed = setrlimit(RLIMIT_NOFILE, &low) == 0;
    }

    ~NoDescriptorLeft()
    {
        if (armed)
            setrlimit(RLIMIT_NOFILE, &saved);
    }

    bool ok() const { return armed; }

private:
    rlimit saved;
    bool armed;
};

struct Run
{
    std::string before = kNothing;
    std::string after = kNothing;
    std::string reply = kNothing;
    std::string armed = kNothing;
    std::string log;
    int exitCode = -1;
    int port = 0;
};

/* The five steps every child of this suite runs, whatever it floods with.
 *
 * howMany discoveries arrive from howMany distinct sources; starved says
 * whether the box still has a descriptor to learn a local address with.
 * refusals and late drive the second half instead, through the send seam.
 */
Run measure(int tag, int howMany, bool starved, int refusals, bool late,
            int pairs)
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
        gStage = 0;
        Utils::signal_wago.connect(sigc::ptr_fun(&noteWago));

        Discovery discovery(asked);

        int sentinelPort = 0;
        const int c = speaker("127.0.0.1", sentinelPort);

        std::string reply = kNothing;
        std::string armed = kNothing;
        if (c >= 0)
        {
            speak(c, asked, "WAGO INT 7 true");
            pump();

            //Before anything is starved or refused: the answer path works on
            //this machine, and the journal below is that of a serving box.
            speak(c, asked, "CALAOS_DISCOVER");
            pump();

            char buf[256];
            const ssize_t n = recv(c, buf, sizeof(buf), 0);
            if (n > 0)
                reply.assign(buf, (size_t)n);

            //Every source of the flood, opened while descriptors are still
            //free. One address each, so the journal names its survivors.
            std::vector<int> sources;
            for (int i = 1; i <= howMany; i++)
            {
                int ignored = 0;
                const int s = speaker(floodSource(i), ignored);
                if (s >= 0)
                    sources.push_back(s);
            }

            if (!sources.empty())
            {
                if (starved)
                {
                    NoDescriptorLeft starvation;
                    armed = starvation.ok()? "starved": "not-starved";
                    for (size_t i = 0; i < sources.size(); i++)
                        speak(sources[i], asked, "CALAOS_DISCOVER");
                    pump();
                    pump();
                }
                else
                {
                    armed = "fed";
                    for (size_t i = 0; i < sources.size(); i++)
                        speak(sources[i], asked, "CALAOS_DISCOVER");
                    pump();
                    pump();
                }
            }

            for (size_t i = 0; i < sources.size(); i++)
                close(sources[i]);

            /* One answer that leaves, one that is refused, over and over.
             * Both halves are the peer's to choose: it asks from a source the
             * box routes to, then from one it does not, and UDP asks it for
             * nothing to prove either. This is the traffic a count cleared on a
             * delivered answer was argued to break down under, and the argument
             * was never played.
             *
             * The pump between the two is not tidiness: libuv sends the healthy
             * prefix of its queue, then charges the first faulty datagram and
             * everything still queued behind it. Position in the queue decides,
             * not batch membership, so without the drain only the first answer
             * of the run would leave and every later one would be refused with
             * the refusal ahead of it.
             */
            for (int i = 1; i <= pairs; i++)
            {
                discovery.answer("127.0.0.1", sentinelPort);
                pump();
                discovery.answer(boughtPeer(i), sentinelPort);
                pump();
            }

            //Every one of these is unroutable, so each is charged wherever it
            //sits in the queue. Posting them in one go only saves the loop
            //turns; it is not what buys the silence.
            for (int i = 1; i <= refusals; i++)
                discovery.answer(boughtPeer(i), sentinelPort);
            pump();
            pump();

            //And then one more correspondent, lost for its own reason, long
            //after the count was driven up.
            if (late)
            {
                discovery.answer(kLatePeer, sentinelPort);
                pump();
                pump();
            }

            gStage = 1;
            speak(c, asked, "WAGO KNX 9 true");
            pump();

            close(c);
        }

        std::cout.flush();

        FILE *o = fopen(outPath.c_str(), "w");
        if (o)
        {
            fprintf(o, "%s\n%s\n%s\n%s\n", gCarried[0].c_str(),
                    gCarried[1].c_str(), reply.c_str(), armed.c_str());
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
    r.armed = fields[3];
    r.log = slurp(logPath);
    return r;
}

struct Runs
{
    //First half: the receive path.
    Run flood, one, served;
    //Second half: the ceiling that never rearms.
    Run bought, fresh, alternating;
};

const Runs &theRuns()
{
    static Runs runs = []()
    {
        Runs r;
        r.flood = measure(0, kFlood, true, 0, false, 0);
        r.one = measure(1, 1, true, 0, false, 0);
        r.served = measure(2, kFlood, false, 0, false, 0);
        r.bought = measure(3, 0, false, kFlood, true, 0);
        r.fresh = measure(4, 0, false, 0, true, 0);
        r.alternating = measure(5, 0, false, 0, false, kAlternations);
        return r;
    }();
    return runs;
}

} //namespace

/*
 * THE FIXTURE, BEFORE ANYTHING IS CONCLUDED FROM IT. Every child was serving,
 * and every child answered a discovery, BEFORE it was starved or refused
 * anything: a child that never got that far would report the same "-" further
 * down for a reason that has nothing to do with either half.
 */
TEST(UdpDiscoverJournalCadence, EveryServerWasAnsweringBeforeItWasStarved)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/7", r.flood.before);
    EXPECT_EQ("127.0.0.1/7", r.one.before);
    EXPECT_EQ("127.0.0.1/7", r.served.before);
    EXPECT_EQ("127.0.0.1/7", r.bought.before);
    EXPECT_EQ("127.0.0.1/7", r.fresh.before);
    EXPECT_EQ("127.0.0.1/7", r.alternating.before);

    EXPECT_EQ(0u, r.flood.reply.find(kDiscoverAnswer));
    EXPECT_EQ(0u, r.one.reply.find(kDiscoverAnswer));
    EXPECT_EQ(0u, r.served.reply.find(kDiscoverAnswer));
    EXPECT_EQ(0u, r.bought.reply.find(kDiscoverAnswer));
    EXPECT_EQ(0u, r.fresh.reply.find(kDiscoverAnswer));
    EXPECT_EQ(0u, r.alternating.reply.find(kDiscoverAnswer));
}

/*
 * AND NONE OF THEM DIED. Every count below is a journal a live process wrote,
 * never a child that stopped writing.
 */
TEST(UdpDiscoverJournalCadence, NeitherCeilingCostsAChild)
{
    const Runs &r = theRuns();

    EXPECT_EQ(0, r.flood.exitCode);
    EXPECT_EQ(0, r.one.exitCode);
    EXPECT_EQ(0, r.served.exitCode);
    EXPECT_EQ(0, r.bought.exitCode);
    EXPECT_EQ(0, r.fresh.exitCode);
    EXPECT_EQ(0, r.alternating.exitCode);
}

/*
 * AND THE BOX REALLY HAD NOTHING LEFT TO ASK WITH. Without this the whole first
 * half would be counting the lines of a box that had nothing to report - the
 * shape of a sensor that measures nothing and reads like one that found
 * nothing. The callee's own line is the witness: it is written from inside
 * GetLocalIPFor(), on the path this suite drives.
 */
TEST(UdpDiscoverJournalCadence, TheDiscoveriesWereReallyUnanswerable)
{
    const Runs &r = theRuns();

    EXPECT_EQ("starved", r.flood.armed)
            << "the descriptor limit could not be lowered: this suite measured "
               "nothing at all";
    EXPECT_EQ("starved", r.one.armed);
    EXPECT_EQ("fed", r.served.armed);

    EXPECT_NE(std::string::npos, r.flood.log.find(kCalleeCannotAsk))
            << "no discovery was left unanswerable: the flood was served";
    EXPECT_EQ(std::string::npos, r.served.log.find(kCalleeCannotAsk));
}

/*
 * THE COUNTERWEIGHT, AND IT COMES FIRST. A ceiling that made the box quiet
 * about a discovery it cannot answer would be a worse defect than the one this
 * ticket closes: one such discovery is still exactly one line, naming the
 * source nothing reaches.
 */
TEST(UdpDiscoverJournalCadence, TheFirstUnanswerableDiscoveryIsAlwaysWritten)
{
    const Runs &r = theRuns();

    EXPECT_EQ(1u, countOf(r.one.log, kNoInterface))
            << "a single unanswerable discovery is no longer reported at all: "
               "the journal went mute instead of bounded";
    EXPECT_NE(std::string::npos, r.one.log.find(floodSource(1) + " ("))
            << "the discovery was reported without saying whose network could "
               "not be matched";
}

/*
 * THE DEFECT OF THE FIRST HALF. The sources are picked by whoever speaks on the
 * network and discovery asks for no credentials, so one line per datagram is a
 * journal whose length the network writes. This counts lines, it never times
 * them.
 */
TEST(UdpDiscoverJournalCadence, ManyUnanswerableDiscoveriesDoNotCostAsManyLines)
{
    const Runs &r = theRuns();

    EXPECT_EQ(8u, countOf(r.flood.log, kNoInterface))
            << kFlood << " unanswerable discoveries wrote one line each: it is "
            << "the network, not the box, that decides how long the journal is";
}

/*
 * WHICH ONES SURVIVED, AND THAT IS NOT THE SAME QUESTION AS HOW MANY. Each
 * datagram of the flood came from its own address, so the journal names the
 * discoveries it kept. Reporting the first eight, the last eight or every
 * sixteenth would all pass a count of eight and fail here.
 */
TEST(UdpDiscoverJournalCadence, TheKeptLinesAreTheDoublings)
{
    const Runs &r = theRuns();

    const int kept[] = { 1, 2, 4, 8, 16, 32, 64, 128 };
    for (int n: kept)
        EXPECT_NE(std::string::npos, r.flood.log.find(floodSource(n) + " ("))
                << "discovery " << n << " was dropped: the line that should "
                   "have been kept at this count is missing";

    const int dropped[] = { 3, 5, 7, 65, 127, 129, 130 };
    for (int n: dropped)
        EXPECT_EQ(std::string::npos, r.flood.log.find(floodSource(n) + " ("))
                << "discovery " << n << " was reported: the kept lines are not "
                   "the ones the ceiling claims to keep";
}

/*
 * BOUNDED IS NOT EXTINGUISHED, AND THE READER HAS TO BE ABLE TO TELL. The last
 * line the flood kept carries the running count, so eight lines behind a
 * hundred and thirty datagrams cannot be read as a box that only saw eight. The
 * two runs pin the VALUE and not merely the presence of the count: a constant
 * in its place satisfies at most one of them.
 */
TEST(UdpDiscoverJournalCadence, TheKeptLineSaysHowManyWentUnsaid)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.flood.log.find("(128 unanswerable so far"))
            << "the journal reports a bounded number of unanswerable "
               "discoveries without ever saying how many there were";
    EXPECT_NE(std::string::npos, r.flood.log.find("(1 unanswerable so far"));
    EXPECT_NE(std::string::npos, r.one.log.find("(1 unanswerable so far"));
    EXPECT_EQ(std::string::npos, r.one.log.find("(128 unanswerable so far"));
}

/*
 * ⛔ AND THE CLASS IS NOT CLOSED BY THIS CEILING. GetLocalIPFor() is called
 * once per discovery and writes its own line from the same starvation, with no
 * ceiling of its own: the flood still costs one line per datagram there. This
 * is the census result, measured rather than asserted - closing the member
 * named in the ticket leaves its callee's twin exactly as it was.
 */
TEST(UdpDiscoverJournalCadence, TheCalleeOfTheSamePathIsStillUnbounded)
{
    const Runs &r = theRuns();

    EXPECT_EQ((size_t)kFlood, countOf(r.flood.log, kCalleeCannotAsk))
            << "the line GetLocalIPFor() writes per datagram is no longer one "
               "per datagram: if it gained a ceiling, this census entry is "
               "stale and the suite has to be re-read, not relaxed";
}

/*
 * AND THE FLOOD DID NOT COST THE RECEIVER. The socket that carries a discovery
 * is the socket every Wago and KNX input arrives on.
 */
TEST(UdpDiscoverJournalCadence, TheFloodDoesNotCostTheNextInput)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/9", r.flood.after)
            << kFlood << " unanswerable discoveries stopped the receiver: no "
            << "Wago or KNX input reaches the box again";
    EXPECT_EQ("127.0.0.1/9", r.one.after);
}

/*
 * THE WITNESS OF THE FIRST HALF, AND THE HAY THAT MUST NOT CARRY THE TARGET.
 * The same flood, from the same hundred and thirty sources, on a box that still
 * has a descriptor: every one of them is answered and not one line is written.
 */
TEST(UdpDiscoverJournalCadence, AServedFloodSaysNothingAndLosesNothing)
{
    const Runs &r = theRuns();

    EXPECT_EQ(0u, countOf(r.served.log, kNoInterface));
    EXPECT_EQ(std::string::npos, r.served.log.find("unanswerable so far"));
    EXPECT_EQ("127.0.0.1/9", r.served.after);
    EXPECT_EQ(0u, countOf(r.served.log, kSendFailed));
}

/*
 * ⛔ SECOND HALF, STAGE ONE: THE REFUSALS REALLY HAPPENED, AND THEY ARE
 * COUNTED. A case that reads "the ceiling bounds" is green on a tree where
 * nothing was ever refused, so the count comes before the conclusion. The
 * journal carries it: the ramp kept the hundred and twenty-eighth refusal and
 * said so.
 */
TEST(UdpDiscoverJournalCadence, TheCeilingWasBoughtByRefusalsThatReallyHappened)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.bought.log.find(kRefusalReason))
            << "no answer was refused: nothing was bought and the next case "
               "would be reading a machine, not a ceiling";
    EXPECT_NE(std::string::npos, r.bought.log.find("(128 refused so far"))
            << "the ramp did not reach 128 refusals, so the gap to the next "
               "kept count is not the one this case relies on";
    EXPECT_EQ(9u, countOf(r.bought.log, kSendFailed));
}

/*
 * ⛔⭐ SECOND HALF, STAGE TWO - THE DEFECT, AND THIS CASE PINS IT RATHER THAN
 * REPAIRING IT.
 *
 * A correspondent lost for its own reason, long after an unauthenticated peer
 * drove the count to a power of two, is not written at all. The count is
 * cumulative for the life of the process, so what the journal says about a
 * failure happening NOW was decided by what happened before it.
 *
 * ⚠️ Read this case as a characterisation: the day the count is rearmed, it
 * goes red, and the right answer is to turn it into its opposite - not to
 * relax it. Its partner below is what makes it mean anything.
 */
TEST(UdpDiscoverJournalCadence, ALaterRealFailureIsSilentOnceTheCeilingWasBought)
{
    const Runs &r = theRuns();

    EXPECT_EQ(std::string::npos, r.bought.log.find(kLatePeer))
            << "the late failure WAS written: the count no longer carries the "
               "whole history of the process, and this characterisation is out "
               "of date - turn it into its opposite, do not delete it";
}

/*
 * ⭐ AND ITS PARTNER, WITHOUT WHICH THE SILENCE ABOVE PROVES NOTHING. The very
 * same failure, to the very same correspondent, on a box whose count nobody
 * drove up: it IS written, and it names the peer. So the silence above is the
 * work of the accumulated count and not of anything the machine refuses to do.
 */
TEST(UdpDiscoverJournalCadence, TheSameFailureIsWrittenWhenNothingWasBought)
{
    const Runs &r = theRuns();

    EXPECT_EQ(1u, countOf(r.fresh.log, kSendFailed))
            << "the late failure was not reported even on a fresh box: this "
               "suite cannot conclude anything about the silence above";
    EXPECT_NE(std::string::npos, r.fresh.log.find(kLatePeer));
    EXPECT_NE(std::string::npos, r.fresh.log.find("(1 refused so far"));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_udpdiscovercad_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
    {
        std::cerr << "could not create a work directory" << std::endl;
        return 1;
    }
    workDir = base;

    return RUN_ALL_TESTS();
}

/*
 * ⛔⭐⭐ WHAT THE CUMULATIVE COUNT BUYS, AND THE PRICE OF THE OBVIOUS
 * ALTERNATIVE - THE HALF THAT WAS ARGUED AND NEVER PLAYED.
 *
 * Clearing the count on an answer that LEAVES is the form anyone reaches for
 * first, and it was set aside by reasoning alone: a peer that alternates a
 * source the box routes to with one it does not would clear the count between
 * every refusal and get one line each - the very journal the ceiling exists to
 * bound. The traffic is produced here, twenty pairs of it, and the count is the
 * whole point of the case: six lines, and the deepest of them says SIXTEEN and
 * not thirty-two, so half the answers really did leave. Clear the count on a
 * delivered answer and this run writes twenty lines instead of six.
 */
TEST(UdpDiscoverJournalCadence, AlternatingTrafficIsStillBoundedAndHalfOfItLeft)
{
    const Runs &r = theRuns();

    EXPECT_EQ(6u, countOf(r.alternating.log, kSendFailed))
            << kAlternations << " refusals alternated with as many delivered "
            << "answers cost one line each: the count is being cleared by "
               "traffic the peer chooses";
    EXPECT_NE(std::string::npos, r.alternating.log.find("(16 refused so far"))
            << "the ramp did not reach 16 refusals";
    EXPECT_EQ(std::string::npos, r.alternating.log.find("(32 refused so far"))
            << "the answers meant to be delivered were refused too, so this "
               "run alternated nothing and measures the same thing as the "
               "purchase run";
    EXPECT_NE(std::string::npos, r.alternating.log.find(boughtPeer(16) + ":"))
            << "the deepest kept line does not name the refusal it counted";
}
