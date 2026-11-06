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
 * WHAT THE SERVER DOES WITH WHAT ITS SIDECARS PRINT.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT
 * ---------------------------------------------------------------------------
 * ExternProcServer copied every complete line of a child's stdout onto its own
 * std::cout and every line of its stderr onto std::cerr, verbatim. The only
 * level filter such a line ever met was the CHILD's: debug_level on this side
 * had no grip on it, and a bare write on the child side met nothing at all.
 * The line also arrived with no domain and no level, so nothing downstream
 * could tell it from a bare write of the server itself.
 *
 * ---------------------------------------------------------------------------
 * WHAT THE HAYSTACK IS, AND WHY IT IS NOT THE JOURNAL
 * ---------------------------------------------------------------------------
 * Every case here reads the WHOLE standard output and the WHOLE standard error
 * of this process, captured at the streambuf. Taking "the lines of the
 * journal" for haystack would make the defect invisible by construction: a
 * line written outside the journal is exactly what is being hunted, and it
 * would never enter such a haystack. What separates the two is read off the
 * text: a journal line opens with its level and its domain, a bare write does
 * not.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS GOES THROUGH REAL CONTROLLERS
 * ---------------------------------------------------------------------------
 * Nothing here calls the relay. Three shipped controllers are built - OwCtrl,
 * WagoMap and KNXCtrl - each spawns its sidecar through the shipped
 * startProcess(), and the stand-ins they reach through CALAOS_BIN_PREFIX are
 * ordinary processes writing on their own descriptors. KNXCtrl is there for
 * one reason: it runs TWO sidecars and hands both of them the same
 * --namespace, so it is the only place where the speaker cannot be named by
 * the namespace.
 *
 * The level half cannot be observed in this process - Logger fills its domain
 * map once, lazily - so it is measured in a forked child that builds a fourth
 * controller under a stock configuration and reads back its own output.
 *
 * WHAT THIS DOES NOT PROVE: no PLC, no KNX bus and no owfs are involved. The
 * stand-ins print and sleep; what a real driver would have printed is a
 * question for its own suite.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "ConfigStore.h"
#include "ExternProc.h"
#include "KNXCtrl.h"
#include "LogSetup.h"
#include "Logger.h"
#include "OLACtrl.h"
#include "OWCtrl.h"
#include "WagoMap.h"
#include "libuvw.h"

using namespace Calaos;

namespace
{

/*
 * FIXTURE, NOT DECORATION.
 *
 * Every needle is spelled out of the alphabet the journal draws its own
 * identifiers in - no hexadecimal digit anywhere in the two documents the
 * ceiling below is measured on - because a drawn address printed by the
 * journal itself could otherwise lengthen a measured run and turn an equality
 * into a lottery. The calibration case at the end of the file holds that.
 */
const char *const kOrdinaryLine = "ligne-ordinaire-imprimee-par-un-sidecar";
const char *const kStderrLine   = "ligne-imprimee-sur-la-sortie-erreur";

//The two halves of a line the child writes in two goes, with a pause in
//between: a unix pipe promises nothing about where a write is cut, and the
//relay reads chunks, never lines.
const char *const kSplitHead = "moitie-de-ligne-avant-la-coupure-";
const char *const kSplitTail = "moitie-de-ligne-apres-la-coupure";

//What a child says on its way out, without the end of line the reader waits
//for. It is the line that says why it died.
const char *const kLastGasp = "derniere-ligne-avant-le-plantage";

//The two KNX sidecars. One KNXCtrl runs both and gives both the same
//--namespace, so only the prefix its ExternProcServer was built with can tell
//them apart.
const char *const kKnxCommandLine = "ligne-du-sidecar-de-commande";
const char *const kKnxMonitorLine = "ligne-du-sidecar-moniteur";

//How the relay names a speaker and a stream, as the journal prints it.
const char *const kOneWireSpeaker    = "1wire stdout: ";
const char *const kOneWireErrSpeaker = "1wire stderr: ";
const char *const kWagoSpeaker       = "wago stdout: ";
const char *const kKnxSpeaker        = "knx stdout: ";
const char *const kKnxMonitorSpeaker = "knx_monitor stdout: ";

//The prefix Logger puts in front of everything it prints, level then domain.
//Colour is off here: nothing of this process is a terminal.
const char *const kJournalDebugMark = "[DBG] process ";

const int kBudgetMs = 12000;

//An over-long line: 1024 bytes, more than the relay is willing to carry.
std::string longLine()
{
    std::string s = "tres-longue-ligne-";
    while (s.size() < 1024)
        s += "zyxwvutsrqponm-";
    s.resize(1024);
    return s;
}

//A stream with no end of line anywhere in it: 1024 bytes above 0x7f, so no
//newline can appear by accident and no byte of it is a hexadecimal digit.
std::string binaryBlob()
{
    std::string s;
    for (int i = 0; i < 1024; i++)
        s.push_back(static_cast<char>(0x80 + (i % 0x70)));
    return s;
}

//The two documents the ceiling covers, in one place: several cases read them,
//and a list that drifts between them would hold one property on a fixture the
//other no longer measures.
struct Doc { const char *label; std::string text; };

std::vector<Doc> measuredDocuments()
{
    std::vector<Doc> v;
    v.push_back(Doc{ "over-long line", longLine() });
    v.push_back(Doc{ "stream without any end of line", binaryBlob() });
    return v;
}

/*
 * THE LONGEST RUN OF A DOCUMENT THE OUTPUT GIVES BACK.
 *
 * Looking for the whole document is looking for a value chosen in advance, and
 * a value search only sees the slice it was spelled for: a relay publishing
 * the first half of a line carries half the line and cites no needle. What is
 * asked is HOW MUCH of it came back, wherever it was cut.
 */
std::string longestEchoRun(const std::string &log, const std::string &text)
{
    std::string best;
    for (size_t i = 0; i < text.size(); i++)
    {
        size_t len = best.size() + 1;
        while (i + len <= text.size() &&
               log.find(text.substr(i, len)) != std::string::npos)
        {
            best = text.substr(i, len);
            len++;
        }
    }
    return best;
}

//The longest run of the journal's own alphabet a document carries: addresses
//and fingerprints are printed in it, so such a run can be matched by bytes
//nobody chose.
std::string longestHexRun(const std::string &s)
{
    std::string best, cur;
    for (const char c: s)
    {
        if (::isxdigit(static_cast<unsigned char>(c)))
        {
            cur += c;
            if (cur.size() > best.size())
                best = cur;
        }
        else
            cur.clear();
    }
    return best;
}

/* One above what the relay really gives back of a document too big for it -
 * measured at every run by the calibration case at the end of the file, never
 * copied from the constant the relay is written with. Everything between the
 * two is a window this suite cannot see into: a relay that grew more generous
 * and a fixture that shrank look the same from here, and the equality is what
 * says which number to write.
 */
const size_t kMaxSidecarEcho = 513;

//The one sandbox of this process. Empty when mkdtemp() fails, so a case FAILS
//instead of quietly pointing CALAOS_BIN_PREFIX at the real prefix.
const std::string &sandboxDir()
{
    static std::string dir = []() -> std::string
    {
        char tmpl[] = "/tmp/calaos_sidecar_journal_XXXXXX";
        const char *d = ::mkdtemp(tmpl);
        return d? std::string(d) : std::string();
    }();

    return dir;
}

bool exists(const std::string &path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

bool writeScript(const std::string &path, const std::string &body)
{
    {
        std::ofstream f(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
        if (!f.is_open())
            return false;
        f << "#!/bin/sh\n" << body;
        if (!f.good())
            return false;
    }

    return ::chmod(path.c_str(), 0755) == 0;
}

bool writeFile(const std::string &path, const std::string &content)
{
    std::ofstream f(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
    if (!f.is_open())
        return false;
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return f.good();
}

/*
 * The three stand-ins, and the documents they print.
 *
 * The bytes come out of a file rather than out of a shell escape: the blob is
 * binary and the long line is 1024 bytes, and what must reach the pipe is
 * those bytes and not a shell's idea of them.
 *
 * Each one drops a witness file when it has printed: the observation waits on
 * THOSE, never on what came back, or a tree that publishes nothing would time
 * out instead of failing the cases that exist to say so.
 */
bool installStandIns()
{
    if (sandboxDir().empty())
        return false;

    const std::string dir = sandboxDir();
    const std::string doc = dir + "/onewire.doc";

    if (!writeFile(doc, std::string(kOrdinaryLine) + "\n" + longLine() + "\n" +
                        binaryBlob()))
        return false;

    //The pause is what forces the split: without it both halves land in one
    //read and the framing question is never asked.
    if (!writeScript(dir + "/calaos_1wire",
                     std::string("echo $$ > '") + dir + "/1wire.pid'\n"
                     "printf '%s' '" + kSplitHead + "'\n"
                     "sleep 1\n"
                     "printf '%s\\n' '" + kSplitTail + "'\n"
                     "printf '%s\\n' '" + kStderrLine + "' >&2\n"
                     "cat '" + doc + "'\n"
                     "touch '" + dir + "/1wire.ran'\n"
                     "exec sleep 120\n"))
        return false;

    //Prints its last words WITHOUT an end of line, then dies with a status.
    //WagoMap relaunches it, so the witness counts the deaths.
    if (!writeScript(dir + "/calaos_wago",
                     std::string("printf '%s' '") + kLastGasp + "'\n"
                     "echo x >> '" + dir + "/wago.deaths'\n"
                     "exit 3\n"))
        return false;

    //One binary, two sidecars, one --namespace. The monitor is the one that
    //gets --internal-monitor-bus.
    if (!writeScript(dir + "/calaos_knx",
                     std::string("case \" $* \" in\n"
                     "  *' --internal-monitor-bus '*)\n"
                     "    echo $$ > '") + dir + "/knx_monitor.pid'\n"
                     "    printf '%s\\n' '" + kKnxMonitorLine + "'\n"
                     "    touch '" + dir + "/knx_monitor.ran'\n"
                     "    ;;\n"
                     "  *)\n"
                     "    echo $$ > '" + dir + "/knx_command.pid'\n"
                     "    printf '%s\\n' '" + kKnxCommandLine + "'\n"
                     "    touch '" + dir + "/knx_command.ran'\n"
                     "    ;;\n"
                     "esac\n"
                     "exec sleep 120\n"))
        return false;

    return ::setenv("CALAOS_BIN_PREFIX", dir.c_str(), 1) == 0;
}

//Run the default loop until pred() holds, with a wall clock deadline: a
//regression must be able to fail a case, never to hang `make check`.
bool pumpLoopUntil(const std::function<bool()> &pred, int timeoutMs)
{
    auto loop = uvw::Loop::getDefault();
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);

    for (;;)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();

        if (pred())
            return true;
        if (std::chrono::steady_clock::now() > deadline)
            return false;

        ::usleep(2000);
    }
}

int countLines(const std::string &path)
{
    std::ifstream f(path.c_str());
    std::string line;
    int n = 0;
    while (std::getline(f, line))
        n++;
    return n;
}

//The lines of the captured output, in order. Splitting is done here and not
//by the assertions, so a case can ask WHERE a needle sits and not merely
//whether it is somewhere in the text.
std::vector<std::string> linesOf(const std::string &text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
        out.push_back(line);
    return out;
}

//The lines of `text` that carry `needle`, whatever else they carry.
std::vector<std::string> linesCarrying(const std::string &text,
                                       const std::string &needle)
{
    std::vector<std::string> out;
    for (const std::string &l: linesOf(text))
        if (l.find(needle) != std::string::npos)
            out.push_back(l);
    return out;
}

struct Observation
{
    bool installed = false;
    bool oneWireSpoke = false;
    bool knxCommandSpoke = false;
    bool knxMonitorSpoke = false;
    int wagoDeaths = 0;
    std::string out;
    std::string err;
};

/*
 * THE ONE OBSERVATION OF THIS PROCESS.
 *
 * The three controllers are singletons and none of them is ever destroyed;
 * they also relaunch their sidecar for as long as the loop turns. Capturing
 * once and letting every case read the same text keeps the cases independent
 * of the order gtest picks, and stops the relaunch loop as soon as the loop
 * stops being pumped.
 */
const Observation &theObservation()
{
    static Observation *obs = nullptr;
    if (obs)
        return *obs;

    obs = new Observation();
    obs->installed = installStandIns();
    if (!obs->installed)
        return *obs;

    const std::string dir = sandboxDir();

    std::ostringstream outSink, errSink;
    std::streambuf *savedOut = std::cout.rdbuf(outSink.rdbuf());
    std::streambuf *savedErr = std::cerr.rdbuf(errSink.rdbuf());

    OwCtrl::Instance("");
    WagoMap::Instance("192.0.2.7", 502);
    KNXCtrl::Instance("192.0.2.8");

    //Waited on the witnesses the stand-ins drop, never on what came back.
    pumpLoopUntil([&dir]()
    {
        return exists(dir + "/1wire.ran") &&
               exists(dir + "/knx_command.ran") &&
               exists(dir + "/knx_monitor.ran") &&
               countLines(dir + "/wago.deaths") >= 3;
    }, kBudgetMs);

    //One more turn: the witness is dropped by the child, and what it printed
    //just before is still in flight on the pipe.
    pumpLoopUntil([]() { return false; }, 1500);

    std::cout.rdbuf(savedOut);
    std::cerr.rdbuf(savedErr);

    obs->oneWireSpoke = exists(dir + "/1wire.ran");
    obs->knxCommandSpoke = exists(dir + "/knx_command.ran");
    obs->knxMonitorSpoke = exists(dir + "/knx_monitor.ran");
    obs->wagoDeaths = countLines(dir + "/wago.deaths");
    obs->out = outSink.str();
    obs->err = errSink.str();

    return *obs;
}

/*
 * WHAT A STOCK INSTALL HEARS, MEASURED IN A CHILD.
 *
 * Logger fills its domain map once and never re-reads it, so a process that
 * has raised debug_level cannot go back and ask what silence looks like. The
 * child builds a fourth controller - OLACtrl, used nowhere else here - under a
 * configuration nobody touched, and reads back its own standard output.
 */
struct StockProbe
{
    bool ran = false;
    bool sidecarSpoke = false;
    bool echoed = false;
};

StockProbe &stockProbe()
{
    static StockProbe probe;
    return probe;
}

const char *const kStockLine = "ligne-vue-sur-une-installation-de-serie";

void measureStockInstall()
{
    int fds[2];
    if (::pipe(fds) != 0)
        return;

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close(fds[0]);
        ::close(fds[1]);
        return;
    }

    if (pid == 0)
    {
        ::close(fds[0]);

        unsigned char answer = 0;
        char tmpl[] = "/tmp/calaos_sidecar_stock_XXXXXX";
        const char *base = ::mkdtemp(tmpl);
        if (base)
        {
            const std::string dir = base;
            const std::string cfg = dir + "/config";
            const std::string cache = dir + "/cache";
            ::mkdir(cfg.c_str(), 0700);
            ::mkdir(cache.c_str(), 0700);

            //Nothing is set afterwards: this is a stock install.
            Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                                     const_cast<char *>(cache.c_str()), true);

            const bool ok =
                writeScript(dir + "/calaos_ola",
                            std::string("printf '%s\\n' '") + kStockLine + "'\n"
                            "touch '" + dir + "/ola.ran'\n"
                            "exec sleep 30\n") &&
                ::setenv("CALAOS_BIN_PREFIX", dir.c_str(), 1) == 0;

            if (ok)
            {
                std::ostringstream sink;
                std::streambuf *saved = std::cout.rdbuf(sink.rdbuf());

                OLACtrl::Instance("7");
                pumpLoopUntil([&dir]() { return exists(dir + "/ola.ran"); }, 8000);
                pumpLoopUntil([]() { return false; }, 1000);

                std::cout.rdbuf(saved);

                answer = 0x4;
                if (exists(dir + "/ola.ran"))
                    answer |= 0x1;
                if (sink.str().find(kStockLine) != std::string::npos)
                    answer |= 0x2;
            }
        }

        if (::write(fds[1], &answer, 1) != 1)
            answer = 0;
        ::close(fds[1]);
        fflush(nullptr);
        _exit(0);
    }

    ::close(fds[1]);

    unsigned char answer = 0;
    const ssize_t got = ::read(fds[0], &answer, 1);
    ::close(fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    if (got == 1 && (answer & 0x4))
    {
        stockProbe().ran = true;
        stockProbe().sidecarSpoke = (answer & 0x1) != 0;
        stockProbe().echoed = (answer & 0x2) != 0;
    }
}

} // namespace

class SidecarOutputJournalTest: public ::testing::Test {};

/*
 * THE CASE THIS SUITE EXISTS FOR: a sidecar line is a journal line.
 *
 * ANTI-VACUITY FIRST. The sandbox is asserted, the stand-in is asserted to
 * have really printed, and the needle is asserted to be somewhere in the
 * output before anything is said about WHERE - a controller that never
 * launched would otherwise make this case green while measuring nothing.
 *
 * The needle is then held to a POSITION, not to presence: every line that
 * carries it must open with the level and the domain the journal prints. A
 * bare copy onto std::cout carries the needle just as well.
 */
TEST_F(SidecarOutputJournalTest, EveryLineASidecarPrintsArrivesAsAJournalLine)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_TRUE(obs.oneWireSpoke)
        << "the 1wire stand-in never printed, so nothing here is measured";

    const std::vector<std::string> carrying =
        linesCarrying(obs.out, kOrdinaryLine);

    ASSERT_FALSE(carrying.empty())
        << "what the sidecar printed did not reach the standard output of this "
           "process at all, so the relay is silent and not merely unfiltered";

    for (const std::string &l: carrying)
    {
        EXPECT_EQ(0u, l.find(kJournalDebugMark))
            << "the line arrived without the level and the domain the journal "
               "puts in front of everything it prints, so no filter downstream "
               "can act on it: " << l;
        EXPECT_NE(std::string::npos, l.find(kOneWireSpeaker))
            << "the line does not name the sidecar that spoke: " << l;
    }
}

/*
 * THE OTHER DESCRIPTOR. A sidecar's stderr was copied onto the server's, which
 * is a second way out of the journal and not a level of its own: the two pipes
 * are all that ever told them apart.
 */
TEST_F(SidecarOutputJournalTest, WhatASidecarWritesOnItsErrorStreamAlsoGoesToTheJournal)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_TRUE(obs.oneWireSpoke)
        << "the 1wire stand-in never printed, so nothing here is measured";

    EXPECT_EQ(std::string::npos, obs.err.find(kStderrLine))
        << "the standard error of this process still carries what the sidecar "
           "wrote on its own: " << obs.err;

    const std::vector<std::string> carrying =
        linesCarrying(obs.out, kStderrLine);

    ASSERT_FALSE(carrying.empty())
        << "the error stream of the sidecar was dropped instead of being "
           "journalled: a driver that only complains on stderr would go silent";

    for (const std::string &l: carrying)
    {
        EXPECT_EQ(0u, l.find(kJournalDebugMark)) << l;
        EXPECT_NE(std::string::npos, l.find(kOneWireErrSpeaker))
            << "the line does not say which stream it came from: " << l;
    }
}

/*
 * THE FRAMING. A pipe hands over chunks; a line written in two goes must come
 * back as ONE line, and the half that arrived first must not have been
 * published on its own.
 */
TEST_F(SidecarOutputJournalTest, ALineCutBetweenTwoReadsComesBackWhole)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_TRUE(obs.oneWireSpoke)
        << "the 1wire stand-in never printed, so nothing here is measured";

    const std::string whole = std::string(kSplitHead) + kSplitTail;

    const std::vector<std::string> joined = linesCarrying(obs.out, whole);
    ASSERT_FALSE(joined.empty())
        << "the two halves never came back as one line: " << obs.out;

    for (const std::string &l: joined)
        EXPECT_EQ(0u, l.find(kJournalDebugMark)) << l;

    for (const std::string &l: linesCarrying(obs.out, kSplitHead))
        EXPECT_NE(std::string::npos, l.find(kSplitTail))
            << "the first half was published before its end of line arrived, "
               "so a line of a sidecar can be cut in two by nothing but the "
               "size of a read: " << l;
}

/*
 * ⭐ THE LAST LINE BEFORE A CRASH.
 *
 * A child that dies mid-word never sends the end of line the reader waits for,
 * and that word is the one that says why it died. It used to stay in the
 * buffer and go down with the server's next relaunch, which is the one line an
 * operator would have wanted.
 */
TEST_F(SidecarOutputJournalTest, TheLastWordsOfAChildThatDiesMidLineSurvive)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_GE(obs.wagoDeaths, 3)
        << "the wago stand-in did not die and relaunch, so the crash this case "
           "exists for never happened";

    const std::vector<std::string> carrying = linesCarrying(obs.out, kLastGasp);

    ASSERT_FALSE(carrying.empty())
        << "what the child printed just before dying never left the buffer: "
           "the line that says why a sidecar died is the one being lost";

    for (const std::string &l: carrying)
    {
        EXPECT_EQ(0u, l.find(kJournalDebugMark)) << l;
        EXPECT_NE(std::string::npos, l.find(kWagoSpeaker))
            << "the line does not name the sidecar that died: " << l;
    }
}

/*
 * ⭐ THE SHARED NAMESPACE OF KNX.
 *
 * One KNXCtrl runs two sidecars and hands both of them --namespace knx, so the
 * namespace cannot name the speaker. What names it is the prefix its
 * ExternProcServer was built with, and the two differ there and only there.
 */
TEST_F(SidecarOutputJournalTest, TheTwoKnxSidecarsAreTellableApartInTheJournal)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_TRUE(obs.knxCommandSpoke && obs.knxMonitorSpoke)
        << "both KNX sidecars must have printed for this case to say anything";

    const std::vector<std::string> cmd = linesCarrying(obs.out, kKnxCommandLine);
    const std::vector<std::string> mon = linesCarrying(obs.out, kKnxMonitorLine);

    ASSERT_FALSE(cmd.empty()) << "the command sidecar was not heard";
    ASSERT_FALSE(mon.empty()) << "the monitor sidecar was not heard";

    for (const std::string &l: cmd)
    {
        EXPECT_EQ(0u, l.find(kJournalDebugMark)) << l;
        EXPECT_NE(std::string::npos, l.find(kKnxSpeaker))
            << "the command sidecar is not named: " << l;
        EXPECT_EQ(std::string::npos, l.find(kKnxMonitorSpeaker))
            << "the command sidecar is named as the monitor: " << l;
    }

    for (const std::string &l: mon)
    {
        EXPECT_EQ(0u, l.find(kJournalDebugMark)) << l;
        EXPECT_NE(std::string::npos, l.find(kKnxMonitorSpeaker))
            << "the monitor sidecar is not named, so the two sidecars of one "
               "controller are indistinguishable: " << l;
        EXPECT_EQ(std::string::npos, l.find(kKnxSpeaker))
            << "the monitor is named as the command sidecar: " << l;
    }
}

/*
 * ⭐ THE LEVEL, WHICH IS THE WHOLE POINT.
 *
 * On a stock install the server says nothing of what its sidecars print, and
 * raising debug_level brings it back. The child that measures this really
 * launches a controller and really reads its own output: asking Logger whether
 * the domain is printable would only measure a setting.
 */
TEST_F(SidecarOutputJournalTest, AStockInstallHearsNothingFromASidecar)
{
    ASSERT_TRUE(stockProbe().ran)
        << "the stock probe could not run, so nothing here is measured";
    ASSERT_TRUE(stockProbe().sidecarSpoke)
        << "the stand-in never printed in the child, so a silence there would "
           "mean nothing";

    EXPECT_FALSE(stockProbe().echoed)
        << "what the sidecar printed reached the standard output of a server "
           "nobody asked to be verbose: the only level filter it met was the "
           "child's, and debug_level has no grip on it";
}

/*
 * ⭐ THE CEILING ITSELF, HELD TO WHAT THIS SUITE MEASURES AT EVERY RUN.
 *
 * A bound is only as narrow as the number it was cut for, and that number
 * drifts both ways with nobody watching: a relay that grew more generous
 * widens the blind window while every assertion above stays green, and a
 * ceiling left wider than what the relay really gives back is space no one
 * asked for. The equality turns both into a red that says which number to
 * write, and makes ONE more byte of echo than the tree really produces a
 * failure.
 */
TEST_F(SidecarOutputJournalTest, TheCeilingIsHeldToWhatTheRelayReallyGivesBack)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_TRUE(obs.oneWireSpoke)
        << "the 1wire stand-in never printed, so there is no overlap to pin";

    //A measure that cannot report a run reads as a clean zero below.
    ASSERT_EQ("bcdef", longestEchoRun("zzbcdefzz", "abcdefg"));
    ASSERT_EQ("", longestEchoRun("zzz", "abc"));

    std::string worst, worstLabel;
    for (const Doc &d: measuredDocuments())
    {
        const std::string run = longestEchoRun(obs.out, d.text);
        if (run.size() > worst.size())
        {
            worst = run;
            worstLabel = d.label;
        }
    }

    EXPECT_EQ(kMaxSidecarEcho, worst.size() + 1)
        << "the ceiling is " << kMaxSidecarEcho << " while the relay gives "
           "back at most " << worst.size() << " bytes of a document too big "
           "for it, on the " << worstLabel << ". Everything between the two is "
           "a window this suite cannot see into: either the relay has widened "
           "it or the fixture has, and the ceiling to write is "
        << (worst.size() + 1) << ".";
}

/*
 * THE SAME BOUND, SEEN FROM THE OTHER SIDE: what is left out must be said.
 *
 * A truncation nobody announces reads as a short line, and a short line reads
 * as a sidecar that had nothing more to say. The two forms are cut for
 * different reasons and each says which.
 */
TEST_F(SidecarOutputJournalTest, WhatTheRelayCutsIsSaidOnTheLineThatWasCut)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_TRUE(obs.oneWireSpoke)
        << "the 1wire stand-in never printed, so nothing here is measured";

    const std::string head = longLine().substr(0, 64);
    const std::vector<std::string> cut = linesCarrying(obs.out, head);

    ASSERT_FALSE(cut.empty()) << "the over-long line never came back at all";

    for (const std::string &l: cut)
    {
        EXPECT_EQ(0u, l.find(kJournalDebugMark)) << l;
        EXPECT_NE(std::string::npos, l.find("bytes cut]"))
            << "the line was carried without saying that anything was left "
               "out, so a truncation is indistinguishable from a short line: "
            << l;
    }

    const std::string blobHead = binaryBlob().substr(0, 64);
    const std::vector<std::string> unterminated = linesCarrying(obs.out, blobHead);

    ASSERT_FALSE(unterminated.empty())
        << "a stream that never sends an end of line came back as nothing at "
           "all: the buffer holding it grows for as long as the child runs";

    for (const std::string &l: unterminated)
        EXPECT_NE(std::string::npos, l.find("[no end of line]"))
            << "the journal does not say the line had no end: " << l;
}

/*
 * THE ALPHABET THE JOURNAL DRAWS IN, HELD OUT OF THE FIXTURE.
 *
 * The equality above is only reproducible if the bytes nobody chose cannot
 * lengthen a run: the journal prints its own identifiers in hexadecimal, so a
 * document carrying a hexadecimal run as long as the measured echo makes the
 * ceiling a lottery, and its red an intermittent one nobody can reproduce. The
 * bound belongs on EVERY form the measure hunts - the over-long line and the
 * stream without an end of line - and not on the needle a human wrote.
 */
TEST_F(SidecarOutputJournalTest, NoMeasuredDocumentCarriesTheJournalsAlphabetThatFar)
{
    //A measure that cannot report a run reads as a clean zero below.
    ASSERT_EQ("beef", longestHexRun("zzbeefzz"));
    ASSERT_EQ("", longestHexRun("zz"));

    for (const Doc &d: measuredDocuments())
    {
        const std::string run = longestHexRun(d.text);
        EXPECT_LT(run.size(), kMaxSidecarEcho)
            << "the " << d.label << " carries " << run.size() << " bytes of "
               "the alphabet the journal draws its own identifiers in, which "
               "the ceiling of " << kMaxSidecarEcho << " does not absorb: a "
               "draw can match them and the equality becomes a lottery.";
    }
}

/*
 * Own main instead of gtest_main, for three reasons.
 *
 * The stock probe has to fork before this process has a loop, a child or a
 * raised level to duplicate.
 *
 * The level has to be set before the first log line: the domain map of Logger
 * is filled once, lazily, and never re-read.
 *
 * And the exit is by _exit(): destroying an ExternProcServer at process exit
 * does kill(SIGTERM) on the last spawned child and closes libuv handles on a
 * loop this binary has stopped pumping.
 */
namespace
{

void killByPidFile(const std::string &path)
{
    std::ifstream f(path.c_str());
    long pid = 0;
    if (f >> pid && pid > 0)
        ::kill(static_cast<pid_t>(pid), SIGTERM);
}

} // namespace

int main(int argc, char **argv)
{
    if (!installStandIns())
    {
        std::cerr << "could not set up the sandbox\n";
        return 1;
    }

    measureStockInstall();

    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_sidecar_journal_cfg_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (base)
    {
        const std::string cfg = std::string(base) + "/config";
        const std::string cache = std::string(base) + "/cache";
        ::mkdir(cfg.c_str(), 0700);
        ::mkdir(cache.c_str(), 0700);

        Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                                 const_cast<char *>(cache.c_str()), true);
        Utils::set_config_option("debug_level", "5");
    }

    const int ret = RUN_ALL_TESTS();

    //The stand-ins outlive us otherwise: ~ExternProcServer, which is what
    //signals them, never runs behind _exit().
    const std::string dir = sandboxDir();
    if (!dir.empty())
    {
        killByPidFile(dir + "/1wire.pid");
        killByPidFile(dir + "/knx_command.pid");
        killByPidFile(dir + "/knx_monitor.pid");
    }

    fflush(nullptr);
    _exit(ret);
}
