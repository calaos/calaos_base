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
 * THE TWO JOURNAL CEILINGS OF UDPServer ARE COUNTED INSIDE A WINDOW, SO THE
 * SILENCE THEY USED TO SELL CANNOT BE BOUGHT FOR THE LIFE OF THE PROCESS.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT, AND WHY THERE ARE TWO OF IT
 * ---------------------------------------------------------------------------
 * Both ramps write one line per doubling of a count, which is what keeps the
 * journal short while never going mute. Read over the whole life of the
 * process, that count is BUYABLE: the only caller of the send is the answer to
 * an unauthenticated CALAOS_DISCOVER, and the source of a datagram is chosen by
 * whoever speaks on the network, so a third party drives a count to a power of
 * two and the next REAL failure is written nowhere for as long again. The
 * class has two members - refused answers, and discoveries no local address
 * reaches - and closing one of them would leave exactly the hole the other one
 * was opened for.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS MEASURED, IN TWO STAGES, AND NEVER IN ONE
 * ---------------------------------------------------------------------------
 * A case that reads "the ceiling rearms" is GREEN on a tree where nothing was
 * ever refused. So every conclusion below is preceded by a case that requires
 * the purchase to have HAPPENED and COUNTS it: the hundred and twenty-eighth
 * refusal, the hundred and twenty-eighth unanswerable discovery, and the exact
 * number of lines they cost. Only then is the late real failure provoked -
 * once inside the window, where it must still be silent, and once across it,
 * where it must speak.
 *
 * ---------------------------------------------------------------------------
 * THE ONE THING INJECTED, AND IT IS THE CLOCK
 * ---------------------------------------------------------------------------
 * The window is a length of time, and no verdict here reads one. A child that
 * has to cross a window moves the server's OWN monotonic source forward and
 * changes nothing else: the comparison, the constant and the restart are the
 * server's. Nothing waits, and nothing is timed.
 *
 * ⚠️ The other seam is the one the discovery-cadence suite already declares:
 * the refusal half is driven through a subclass whose answer() calls the
 * protected send, because nothing else in the tree can hand UDPServer a
 * correspondent that has gone away. The unanswerable half needs no seam - it
 * is produced by real datagrams to a box with no descriptor left to learn a
 * local address with.
 *
 * No skip and no 77: whether either accident can be produced here is read by
 * cases that go red, never by a barrier that would report a clean run having
 * measured nothing. Every verdict is a COUNT of journal lines or the VALUE a
 * line carries.
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

//What a slot reports when nothing at all reached it. A sensor that measured
//nothing must not read like a sensor that found nothing.
const char *const kNothing = "-";

const char *const kDiscoverAnswer = "CALAOS_IP ";

//The two bounded lines, and the line the callee of the same path writes - the
//last one is read only to say that the starvation really bit.
const char *const kSendFailed = "UDP send to ";
const char *const kRefusalReason = "invalid argument";
const char *const kNoInterface = "No interface found corresponding to network : ";
const char *const kCalleeCannotAsk = "Can't create socket";

//How many of each are bought, and what the two ramps cost for that many.
const int kBought = 130;
const size_t kRefusalLines = 9;       //1,2,3,4,8,16,32,64,128
const size_t kUnanswerableLines = 8;  //1,2,4,8,16,32,64,128

//The correspondent and the source of the LATE failures. Ranges of their own,
//so no case can confuse either with something that bought the silence.
const char *const kLatePeer = "198.51.100.7";
const char *const kLateSource = "127.1.0.200";

std::string workDir;

std::string floodSource(int nth)
{
    return "127.1.0." + Utils::to_string(nth);
}

std::string boughtPeer(int nth)
{
    return "192.0.2." + Utils::to_string(nth);
}

//The journal line as it is written, so "127.1.0.20" can never pass for
//"127.1.0.200" and a peer cannot be found in some other line.
std::string unanswerableLine(const std::string &src)
{
    return std::string(kNoInterface) + src + " (";
}

/* Reaches the send the server uses with a destination this suite picks -
 * nothing else in the tree can hand UDPServer a correspondent that has gone
 * away - and the server's own monotonic source.
 *
 * crossTheWindow() moves that source forward past one window and touches
 * nothing else: the comparison against the constant, the restart and both
 * counts stay the server's. It is what lets a two-stage case be played without
 * waiting, and without any verdict reading a clock.
 */
class Window: public UDPServer
{
public:
    using UDPServer::UDPServer;

    void answer(const std::string &ip, unsigned int p)
    {
        kept.push_back("CALAOS_IP 127.0.0.1");
        sendTo(ip, p, kept.back());
    }

    void crossTheWindow() { offset += JOURNAL_WINDOW_SECONDS + 1.0; }

protected:
    double journalNow() const override
    {
        return UDPServer::journalNow() + offset;
    }

private:
    std::deque<std::string> kept;
    double offset = 0.0;
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

//Turns of the loop, never blocking on one that will not come. It also bounds
//what a tree writing one line per turn could produce, so such a tree fails a
//count instead of never finishing.
void pump()
{
    auto loop = uvw::Loop::getDefault();
    for (int i = 0; i < 40; i++)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        usleep(2000);
    }
}

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
 * already open keeps working. dup() hands back the lowest free number, so a
 * soft limit set to that number forbids every later allocation without
 * touching what is open - the only form that cannot fail to bite.
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

struct Ask
{
    //Send half: how many answers are refused before the late one, and whether
    //the late one happens at all.
    int refusals = 0;
    bool lateRefusal = false;
    //Receive half: how many unanswerable discoveries arrive before the late
    //one, and whether the box is starved at all.
    int unanswerable = 0;
    bool lateDiscovery = false;
    //Whether the window is crossed between the purchase and the late event.
    bool cross = false;
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

Run measure(const Ask &ask, int tag)
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

        Window server(asked);

        int sentinelPort = 0;
        const int c = speaker("127.0.0.1", sentinelPort);

        std::string reply = kNothing;
        std::string armed = kNothing;
        if (c >= 0)
        {
            speak(c, asked, "WAGO INT 7 true");
            pump();

            //Before anything is starved or refused: the answer path works on
            //this machine, and every journal below is that of a serving box.
            speak(c, asked, "CALAOS_DISCOVER");
            pump();

            char buf[256];
            const ssize_t n = recv(c, buf, sizeof(buf), 0);
            if (n > 0)
                reply.assign(buf, (size_t)n);

            const bool starve = ask.unanswerable > 0 || ask.lateDiscovery;
            if (starve)
            {
                //Every source, opened while descriptors are still free. One
                //address each, so the journal names its survivors.
                std::vector<int> sources;
                for (int i = 1; i <= ask.unanswerable; i++)
                {
                    int ignored = 0;
                    const int s = speaker(floodSource(i), ignored);
                    if (s >= 0)
                        sources.push_back(s);
                }

                int ignored = 0;
                const int late = ask.lateDiscovery?
                            speaker(kLateSource, ignored): -1;

                {
                    NoDescriptorLeft starvation;
                    armed = starvation.ok()? "starved": "not-starved";

                    for (size_t i = 0; i < sources.size(); i++)
                        speak(sources[i], asked, "CALAOS_DISCOVER");
                    pump();
                    pump();

                    if (ask.cross)
                        server.crossTheWindow();

                    if (late >= 0)
                    {
                        speak(late, asked, "CALAOS_DISCOVER");
                        pump();
                        pump();
                    }
                }

                for (size_t i = 0; i < sources.size(); i++)
                    close(sources[i]);
                if (late >= 0)
                    close(late);
            }

            if (ask.refusals > 0 || ask.lateRefusal)
            {
                //Every one of these is unroutable, so each is charged wherever
                //it sits in the queue. Posting them in one go only saves the
                //loop turns; it is not what buys the silence.
                for (int i = 1; i <= ask.refusals; i++)
                    server.answer(boughtPeer(i), sentinelPort);
                pump();
                pump();

                if (ask.cross)
                    server.crossTheWindow();

                //And then one more correspondent, lost for its own reason,
                //long after the count was driven up.
                if (ask.lateRefusal)
                {
                    server.answer(kLatePeer, sentinelPort);
                    pump();
                    pump();
                }
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
    //The refused-send ceiling: bought, bought then crossed, and never bought.
    Run sendBought, sendWindow, sendFresh;
    //The unanswerable-discovery ceiling, the same three ways.
    Run discBought, discWindow, discFresh;
    //Neither bought nor provoked: the hay must not carry the target.
    Run witness;
};

const Runs &theRuns()
{
    static Runs runs = []()
    {
        Runs r;
        Ask a;

        a = Ask(); a.refusals = kBought; a.lateRefusal = true;
        r.sendBought = measure(a, 0);

        a = Ask(); a.refusals = kBought; a.lateRefusal = true; a.cross = true;
        r.sendWindow = measure(a, 1);

        a = Ask(); a.lateRefusal = true;
        r.sendFresh = measure(a, 2);

        a = Ask(); a.unanswerable = kBought; a.lateDiscovery = true;
        r.discBought = measure(a, 3);

        a = Ask(); a.unanswerable = kBought; a.lateDiscovery = true;
        a.cross = true;
        r.discWindow = measure(a, 4);

        a = Ask(); a.lateDiscovery = true;
        r.discFresh = measure(a, 5);

        r.witness = measure(Ask(), 6);

        return r;
    }();
    return runs;
}

} //namespace

/*
 * THE FIXTURE, BEFORE ANYTHING IS CONCLUDED FROM IT. Every child was serving,
 * and every child answered a discovery, BEFORE it was starved or refused
 * anything: a child that never got that far would report the same silence
 * further down for a reason that has nothing to do with either ceiling.
 */
TEST(UdpJournalWindowRearm, EveryServerWasAnsweringBeforeAnythingWasBought)
{
    const Runs &r = theRuns();

    EXPECT_EQ("127.0.0.1/7", r.sendBought.before);
    EXPECT_EQ("127.0.0.1/7", r.sendWindow.before);
    EXPECT_EQ("127.0.0.1/7", r.sendFresh.before);
    EXPECT_EQ("127.0.0.1/7", r.discBought.before);
    EXPECT_EQ("127.0.0.1/7", r.discWindow.before);
    EXPECT_EQ("127.0.0.1/7", r.discFresh.before);
    EXPECT_EQ("127.0.0.1/7", r.witness.before);

    EXPECT_EQ(0u, r.sendBought.reply.find(kDiscoverAnswer));
    EXPECT_EQ(0u, r.sendWindow.reply.find(kDiscoverAnswer));
    EXPECT_EQ(0u, r.sendFresh.reply.find(kDiscoverAnswer));
    EXPECT_EQ(0u, r.discBought.reply.find(kDiscoverAnswer));
    EXPECT_EQ(0u, r.discWindow.reply.find(kDiscoverAnswer));
    EXPECT_EQ(0u, r.discFresh.reply.find(kDiscoverAnswer));
    EXPECT_EQ(0u, r.witness.reply.find(kDiscoverAnswer));
}

/*
 * AND NONE OF THEM DIED, AND NONE OF THEM WENT DEAF. Every count below is a
 * journal a live and listening process wrote; "nothing is written" must never
 * turn out to have been "the child is no longer there to write".
 */
TEST(UdpJournalWindowRearm, NeitherCeilingCostsAChildOrItsReceiver)
{
    const Runs &r = theRuns();

    EXPECT_EQ(0, r.sendBought.exitCode);
    EXPECT_EQ(0, r.sendWindow.exitCode);
    EXPECT_EQ(0, r.sendFresh.exitCode);
    EXPECT_EQ(0, r.discBought.exitCode);
    EXPECT_EQ(0, r.discWindow.exitCode);
    EXPECT_EQ(0, r.discFresh.exitCode);
    EXPECT_EQ(0, r.witness.exitCode);

    EXPECT_EQ("127.0.0.1/9", r.sendWindow.after);
    EXPECT_EQ("127.0.0.1/9", r.discWindow.after);
}

/*
 * ⛔ STAGE ONE, AND NOTHING BELOW MAY BE READ WITHOUT IT. Both ceilings were
 * really bought: the answers were really refused, the starvation really bit,
 * and both ramps really reached a hundred and twenty-eight - which the kept
 * lines say themselves. A case that reads "the ceiling rearms" is green on a
 * tree where nothing was ever refused, so the purchase is COUNTED here first.
 */
TEST(UdpJournalWindowRearm, BothCeilingsWereBoughtByEventsThatReallyHappened)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.sendBought.log.find(kRefusalReason))
            << "no answer was refused: nothing was bought and the cases below "
               "would be reading a machine, not a ceiling";
    EXPECT_NE(std::string::npos, r.sendBought.log.find("(128 refused so far"));
    EXPECT_EQ(kRefusalLines, countOf(r.sendBought.log, kSendFailed));

    EXPECT_NE(std::string::npos, r.sendWindow.log.find(kRefusalReason));
    EXPECT_NE(std::string::npos, r.sendWindow.log.find("(128 refused so far"));

    EXPECT_EQ("starved", r.discBought.armed)
            << "the box was not starved: its discoveries were answerable and "
               "nothing was bought";
    EXPECT_NE(std::string::npos, r.discBought.log.find(kCalleeCannotAsk))
            << "the starvation did not reach the path that learns a local "
               "address";
    EXPECT_NE(std::string::npos,
              r.discBought.log.find("(128 unanswerable so far"));
    EXPECT_EQ(kUnanswerableLines, countOf(r.discBought.log, kNoInterface));

    EXPECT_EQ("starved", r.discWindow.armed);
    EXPECT_NE(std::string::npos,
              r.discWindow.log.find("(128 unanswerable so far"));
}

/*
 * STAGE TWO, INSIDE THE WINDOW: NOTHING CHANGES, AND THAT IS ON PURPOSE. A
 * failure that happens while the bought count is still standing is still
 * unwritten. The window takes the silence back after a while; it does not
 * pretend the ramp never bounds anything.
 */
TEST(UdpJournalWindowRearm, ALateFailureInsideTheSameWindowIsStillSilent)
{
    const Runs &r = theRuns();

    EXPECT_EQ(std::string::npos, r.sendBought.log.find(kLatePeer))
            << "the late refusal was written inside the window: the ramp is no "
               "longer bounding anything";
    EXPECT_EQ(kRefusalLines, countOf(r.sendBought.log, kSendFailed));

    EXPECT_EQ(std::string::npos,
              r.discBought.log.find(unanswerableLine(kLateSource)));
    EXPECT_EQ(kUnanswerableLines, countOf(r.discBought.log, kNoInterface));
}

/*
 * ⭐⭐ STAGE TWO, ACROSS THE WINDOW: THE SAME FAILURE, TO THE SAME
 * CORRESPONDENT, ON A BOX WHOSE COUNT WAS DRIVEN UP IN EXACTLY THE SAME WAY -
 * and it is SAID. One line more than the purchase cost, and it names the peer
 * that was lost.
 */
TEST(UdpJournalWindowRearm, AfterTheWindowTheLateRefusalIsSaidAgain)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.sendWindow.log.find(kLatePeer))
            << "the count was never rearmed: a third party still buys the "
               "silence of every later failure";
    EXPECT_EQ(kRefusalLines + 1, countOf(r.sendWindow.log, kSendFailed))
            << "the late refusal cost something other than exactly one line";
}

/*
 * ⭐⭐ AND THE SECOND CEILING IS REARMED TOO. Closing one of the two would
 * leave precisely the hole the other was opened for: the unanswerable-discovery
 * count is bought the same way, by datagrams that need no credentials, and it
 * comes back the same way.
 */
TEST(UdpJournalWindowRearm, AfterTheWindowTheLateUnanswerableDiscoveryIsSaidAgain)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos,
              r.discWindow.log.find(unanswerableLine(kLateSource)))
            << "only one of the two counts was rearmed: the other still sells "
               "the same silence";
    EXPECT_EQ(kUnanswerableLines + 1, countOf(r.discWindow.log, kNoInterface))
            << "the late discovery cost something other than exactly one line";
}

/*
 * ⭐ AND THE VALUE THE LINE CARRIES SAYS THE COUNT RESTARTED. Not that a line
 * is there, and not that it sits in the right place: the number in it. A box
 * that crossed a window says ONE again - twice in the whole run, once for the
 * purchase and once for the late failure - and never goes on at a hundred and
 * thirty-one. A box that did not cross one says ONE exactly once.
 */
TEST(UdpJournalWindowRearm, TheCountRestartsAtOneAndDoesNotCarryOn)
{
    const Runs &r = theRuns();

    EXPECT_EQ(2u, countOf(r.sendWindow.log, "(1 refused so far"));
    EXPECT_EQ(std::string::npos, r.sendWindow.log.find("(131 refused so far"))
            << "the late refusal was written, but as the hundred and "
               "thirty-first: the count was not restarted, the ramp merely "
               "reached another doubling";
    EXPECT_EQ(1u, countOf(r.sendBought.log, "(1 refused so far"));

    EXPECT_EQ(2u, countOf(r.discWindow.log, "(1 unanswerable so far"));
    EXPECT_EQ(std::string::npos,
              r.discWindow.log.find("(131 unanswerable so far"));
    EXPECT_EQ(1u, countOf(r.discBought.log, "(1 unanswerable so far"));
}

/*
 * ⭐ AND THE SAME TWO FAILURES ARE SAID ON A BOX NOBODY DROVE UP - without
 * which the silences above prove nothing about the counts and everything about
 * the machine.
 */
TEST(UdpJournalWindowRearm, TheSameFailuresAreSaidOnABoxNobodyDroveUp)
{
    const Runs &r = theRuns();

    EXPECT_EQ(1u, countOf(r.sendFresh.log, kSendFailed))
            << "the late refusal was not reported even on a fresh box: this "
               "suite cannot conclude anything about the silence above";
    EXPECT_NE(std::string::npos, r.sendFresh.log.find(kLatePeer));
    EXPECT_NE(std::string::npos, r.sendFresh.log.find("(1 refused so far"));

    EXPECT_EQ(1u, countOf(r.discFresh.log, kNoInterface));
    EXPECT_NE(std::string::npos,
              r.discFresh.log.find(unanswerableLine(kLateSource)));
    EXPECT_NE(std::string::npos,
              r.discFresh.log.find("(1 unanswerable so far"));
}

/*
 * ⛔⭐ AND THE WINDOW IS ONE COUNT FOR THE BOX, NOT ONE PER CORRESPONDENT. The
 * purchase was made by a hundred and thirty DISTINCT peers and a hundred and
 * thirty DISTINCT sources - the traffic a third party produces most easily -
 * and it still cost the ramp's count of lines, not one each. A count kept per
 * correspondent closes the silence this suite is about and reopens the far
 * wider one the ramps exist for.
 *
 * Which lines survived is read too, and it is not the same question as how
 * many: every event carries its own address, so keeping the first nine, the
 * last nine or every fifteenth would all pass the counts above and fail here.
 */
TEST(UdpJournalWindowRearm, TheWindowIsOneCountForTheBoxAndNotOnePerCorrespondent)
{
    const Runs &r = theRuns();

    const int keptPeers[] = { 1, 2, 3, 4, 8, 16, 32, 64, 128 };
    for (int n: keptPeers)
        EXPECT_NE(std::string::npos, r.sendWindow.log.find(boughtPeer(n) + ":"))
                << "refusal " << n << " was dropped: the kept lines are not "
                   "the ones the ramp claims to keep";

    const int droppedPeers[] = { 5, 6, 7, 65, 127, 129, 130 };
    for (int n: droppedPeers)
        EXPECT_EQ(std::string::npos, r.sendWindow.log.find(boughtPeer(n) + ":"))
                << "refusal " << n << " was reported: the count is being kept "
                   "per correspondent, and a peer that picks a new address per "
                   "answer gets a line each";

    const int keptSources[] = { 1, 2, 4, 8, 16, 32, 64, 128 };
    for (int n: keptSources)
        EXPECT_NE(std::string::npos,
                  r.discWindow.log.find(unanswerableLine(floodSource(n))))
                << "unanswerable discovery " << n << " was dropped";

    const int droppedSources[] = { 3, 5, 65, 127, 129, 130 };
    for (int n: droppedSources)
        EXPECT_EQ(std::string::npos,
                  r.discWindow.log.find(unanswerableLine(floodSource(n))))
                << "unanswerable discovery " << n << " was reported: the count "
                   "is being kept per source";
}

/*
 * THE WITNESS, AND THE HAY THAT MUST NOT CARRY THE TARGET. The same steps with
 * nothing refused, nothing starved and nothing provoked: not one of the strings
 * every case above looks for is in this journal, so none of them can be found
 * in something a serving box writes anyway.
 */
TEST(UdpJournalWindowRearm, ABoxThatWasNeverRefusedAnythingSaysNoneOfIt)
{
    const Runs &r = theRuns();

    EXPECT_EQ(0u, countOf(r.witness.log, kSendFailed));
    EXPECT_EQ(0u, countOf(r.witness.log, kNoInterface));
    EXPECT_EQ(std::string::npos, r.witness.log.find("refused so far"));
    EXPECT_EQ(std::string::npos, r.witness.log.find("unanswerable so far"));
    EXPECT_EQ(std::string::npos, r.witness.log.find(kLatePeer));
    EXPECT_EQ(std::string::npos, r.witness.log.find(kLateSource));
    EXPECT_EQ("127.0.0.1/9", r.witness.after);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_udpwindow_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
    {
        std::cerr << "could not create a work directory" << std::endl;
        return 1;
    }
    workDir = base;

    return RUN_ALL_TESTS();
}
