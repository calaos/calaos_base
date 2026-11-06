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
 * WHO SENT THE SIGTERM.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT
 * ---------------------------------------------------------------------------
 * The relaunch ramp exempted SIGTERM by its VALUE: `signal != SIGTERM` meant
 * "voluntary stop", and the same condition gated the journal line. terminate()
 * is not the only sender - a killall, systemd, a supervisor or a sweeping
 * pkill send exactly the same number - so a third party killing a sidecar in a
 * loop got the cadence of before the ramp AND a silent journal, which is the
 * loop the ramp exists to slow down.
 *
 * Decided, and the price is accepted: a SIGTERM the server did not send feeds
 * the ramp and is reported. An administrator who stops a sidecar by hand will
 * therefore see it held back on the next launch; the server relaunches it
 * either way, so what is paid is a delay, never a sidecar that stays down.
 *
 * ---------------------------------------------------------------------------
 * THE TWO OPPOSITE CASES, AND WHY BOTH ARE HERE
 * ---------------------------------------------------------------------------
 * Reading provenance wrong in the other direction is WORSE than the defect: a
 * terminate() mistaken for a stranger's signal would slow down a sidecar the
 * server has just deliberately stopped and print an alert on every ordinary
 * shutdown. Every case demanding that a foreign SIGTERM be counted is
 * therefore paired with one demanding that ours still be free and silent, and
 * the two read the SAME sensor - one answers true, the other false, so a
 * sensor stuck on either value fails half of this file.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS MEASURED, AND WITH WHAT
 * ---------------------------------------------------------------------------
 * Launches are COUNTED inside a fixed window and bounded from ABOVE, the
 * discipline of core/ExternProcRespawnBackoff_test and of
 * core/ExternProcCrashBackoff_test: load can only make fewer launches happen,
 * never more, so a breach of the bound can only come from the code. No case
 * states a rate, and no case reads a duration.
 *
 * The journal case pairs every demand for a line with a demand on the same
 * captured text that another, unrelated line IS there: an absence proved
 * against a dead sink proves nothing. Each server carries its own sidecar
 * name, so a needle is looked for on the line of the server that owns it and
 * a child left over from an earlier case cannot answer for it.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <csignal>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "ExternProc.h"
#include "core/CalaosCoreFixture.h"
#include "core/ExternProcSpawnHarness.h"

using namespace CalaosTest;

namespace
{

//Same window and same ceiling as core/ExternProcCrashBackoff_test, and for the
//same reason: with the ramp launches land at 0, 0.2, 0.5 and 1.0 s, without it
//every ~0.11 s, so the budget separates the two by a factor of three.
const int kRampWindowMs = 1600;
const int kRampMaxLaunches = 4;

const int kBudgetMs = 8000;

//A status that appears nowhere else, so a line carrying it cannot be confused
//with a generic failure.
const int kNoisyExitCode = 3;

std::string standInPath(const std::string &name)
{
    return ExternProcSpawn::sandboxDir() + "/" + name;
}

bool registerStandIn(const std::string &binName, const std::string &body)
{
    if (ExternProcSpawn::sandboxDir().empty())
        return false;

    const std::string script = standInPath(binName);

    {
        std::ofstream f(script.c_str(), std::ios::out | std::ios::trunc);
        if (!f.is_open())
            return false;
        f << "#!/bin/sh\n"
             "sep=$(printf '\\037')\n"
             "rec=\n"
             "for a in \"$0\" \"$@\"; do rec=\"$rec$sep$a\"; done\n"
             "printf '%s\\n' \"$rec\" >> '"
          << ExternProcSpawn::journalPath(binName) << "'\n"
          << body;
        if (!f.good())
            return false;
    }

    if (::chmod(script.c_str(), 0755) != 0)
        return false;

    bool known = false;
    for (const std::string &n: ExternProcSpawn::installed())
        known = known || (n == binName);
    if (!known)
        ExternProcSpawn::installed().push_back(binName);

    return ::setenv("CALAOS_BIN_PREFIX",
                    ExternProcSpawn::sandboxDir().c_str(), 1) == 0;
}

/*
 * A sidecar somebody else kills. `kill -TERM $$` is the whole point: the
 * kernel then reports status 0 with SIGTERM beside it, byte for byte what
 * terminate() leaves behind, and nothing in the event says who asked.
 */
bool installForeignlyKilledStandIn(const std::string &binName)
{
    //The sleep is reached only if the signal was ignored; a plain exit there
    //would let a case pass on a status it is not testing.
    return registerStandIn(binName, "kill -TERM $$\nsleep 5\n");
}

/*
 * A sidecar that stays up until it is signalled. `exec` and not a plain sleep:
 * the process that receives the SIGTERM must BE the sleep, or the shell may
 * answer for it with an ordinary status and the case would never see a signal.
 */
bool installSleepingStandIn(const std::string &binName)
{
    return registerStandIn(binName, "exec sleep 120\n");
}

//A sidecar that fails the ordinary way, used only as proof that a capture is
//listening.
bool installNoisyStandIn(const std::string &binName)
{
    std::ostringstream body;
    body << "exit " << kNoisyExitCode << "\n";
    return registerStandIn(binName, body.str());
}

/*
 * The shape of every controller of the tree: relaunch on the exit signal, with
 * the same arguments, forever, until the server says stop.
 *
 * HEAP, AND LEAKED, like the server it drives: the exit of the child that
 * terminate() signals is published 100 ms later, after the case that armed it
 * has returned, and a stack instance would then be read at an address the next
 * case has reused.
 */
struct Driver
{
    ExternProcServer *server = nullptr;
    bool stopped = false;

    void drive(const std::string &binName, const std::string &procName)
    {
        const std::string exe = standInPath(binName);
        server = new ExternProcServer(procName);
        server->processExited.connect([this, exe, procName]()
        {
            if (stopped)
                return;
            server->startProcess(exe, procName);
        });
        server->startProcess(exe, procName);
    }

    //The voluntary stop of this file: the only SIGTERM the server sends.
    void stop()
    {
        stopped = true;
        if (server)
            server->terminate();
    }
};

int launchCount(const std::string &binName)
{
    return static_cast<int>(ExternProcSpawn::launches(binName).size());
}

std::vector<std::string> linesCarrying(const std::string &text,
                                       const std::string &needle)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;

    while (std::getline(in, line))
        if (line.find(needle) != std::string::npos)
            out.push_back(line);

    return out;
}

class SidecarTerminateProvenanceTest: public CoreFixture
{
};

} //namespace

/******************************************************************************
 * THE POLICY, BOTH WAYS ROUND.
 ******************************************************************************/

TEST_F(SidecarTerminateProvenanceTest, ASigtermWeDidNotSendIsCountedAsACrash)
{
    //Status 0 with SIGTERM beside it: what a killall leaves behind, and what
    //terminate() leaves behind, are the same two numbers.
    EXPECT_EQ(1, ExternProcServer::nextFailureCount(0, 0, SIGTERM, 0.0, false))
        << "a SIGTERM the server never sent left the failure count at zero, so "
           "anyone able to signal the sidecar can relaunch it as fast as the "
           "loop allows, forever";
    EXPECT_EQ(4, ExternProcServer::nextFailureCount(3, 0, SIGTERM, 0.0, false))
        << "a foreign SIGTERM after three failures did not climb the ramp";

    //The count is only worth what the ramp does with it.
    EXPECT_GT(ExternProcServer::respawnDelay(
                  ExternProcServer::nextFailureCount(3, 0, SIGTERM, 0.0, false)),
              ExternProcServer::kRespawnDelayMin)
        << "four foreign SIGTERM in a row still cost the shortest hold there is";

    //The reset rule keeps its say: a sidecar that served long enough is a new
    //incident whoever ended it.
    EXPECT_EQ(1, ExternProcServer::nextFailureCount(
                     9, 0, SIGTERM, ExternProcServer::kRespawnResetSeconds,
                     false))
        << "a sidecar killed from outside after a full service is picked up at "
           "the ceiling it left behind";
}

TEST_F(SidecarTerminateProvenanceTest, TheSigtermWeSendIsStillFreeOfTheRamp)
{
    EXPECT_EQ(0, ExternProcServer::nextFailureCount(9, 0, SIGTERM, 0.0, true))
        << "the SIGTERM of terminate() was counted as a crash, so every "
           "ordinary shutdown now drags the next launch into the ramp";
    EXPECT_EQ(0, ExternProcServer::nextFailureCount(9, 0, SIGTERM, 3600.0, true));
    EXPECT_EQ(0.0, ExternProcServer::respawnDelay(
                       ExternProcServer::nextFailureCount(9, 0, SIGTERM, 0.0,
                                                          true)))
        << "a sidecar the server itself stopped is held back on its next start";

    //The four-argument form is what the ramp's own suites call, and the
    //question it asks has not moved.
    EXPECT_EQ(0, ExternProcServer::nextFailureCount(9, 0, SIGTERM, 0.0))
        << "the default answer changed under callers that pass no provenance";
}

TEST_F(SidecarTerminateProvenanceTest, ProvenanceExemptsNothingButSigterm)
{
    //Asking for a stop does not make a crash a stop: a sidecar that segfaults
    //while the server is shutting it down still crashed.
    EXPECT_EQ(1, ExternProcServer::nextFailureCount(0, 0, SIGSEGV, 0.0, true))
        << "a segmentation fault was excused because the server had asked the "
           "sidecar to stop: the exemption moved from the value to the origin "
           "instead of needing both";
    EXPECT_EQ(1, ExternProcServer::nextFailureCount(0, 0, SIGKILL, 0.0, true));

    //And not asking for a stop does not make an ordinary exit a crash.
    EXPECT_EQ(0, ExternProcServer::nextFailureCount(9, 0, 0, 0.0, false))
        << "a sidecar that exited by itself with a zero status and no signal "
           "at all was counted as a failure";
    EXPECT_EQ(10, ExternProcServer::nextFailureCount(9, 1, 0, 0.0, true))
        << "a non-zero status stopped being a failure as soon as the server "
           "had asked for the stop";
}

/******************************************************************************
 * AND WHAT A SIDECAR REALLY KILLED FROM OUTSIDE GETS, COUNTED.
 ******************************************************************************/

TEST_F(SidecarTerminateProvenanceTest, ASidecarKilledByAForeignSigtermIsSlowedDown)
{
    loadConfig();

    const std::string bin = "calaos_prov_foreign";
    ASSERT_TRUE(installForeignlyKilledStandIn(bin));

    Driver &driver = *new Driver();
    driver.drive(bin, "provforeign");
    ExternProcSpawn::runLoopUntil([]() { return false; }, kRampWindowMs);

    const int launched = launchCount(bin);
    const int64_t status = driver.server->lastExitStatus();
    const int signalled = driver.server->lastTermSignal();
    const int failures = driver.server->respawnFailures();
    driver.stop();

    //THE FIXTURE FIRST. A stand-in that exited with a code instead of dying by
    //SIGTERM would satisfy everything below while measuring the case that
    //already worked.
    ASSERT_EQ(SIGTERM, signalled)
        << "the stand-in did not die by SIGTERM, so this case is not about the "
           "signal whose origin is in question";
    ASSERT_EQ(0, status)
        << "the child left status " << status << ": a death by signal leaves a "
           "ZERO there, and this fixture no longer reproduces it";

    EXPECT_LE(launched, kRampMaxLaunches)
        << launched << " launches in " << kRampWindowMs << " ms: a sidecar "
           "somebody else keeps killing is relaunched as fast as the loop "
           "allows, exactly as before the ramp existed";
    EXPECT_GE(launched, 2)
        << "the sidecar was never relaunched at all: slowing down was traded "
           "for giving up";
    EXPECT_GE(failures, 2)
        << "the foreign SIGTERM were not counted, so the ramp is fed a number "
           "that never moves";
}

TEST_F(SidecarTerminateProvenanceTest, AnOrdinaryRelaunchIsNotReadAsATerminate)
{
    loadConfig();

    /*
     * The mark the transport reads is a counter that startProcess() bumps on
     * EVERY launch, terminate() being only one of its writers. Its value alone
     * therefore says nothing, and a guard reading the counter instead of its
     * movement since this child was spawned would call every relaunch of a
     * respawn loop a voluntary stop. Three launches are enough for the counter
     * to have moved several times with no terminate() anywhere.
     */
    const std::string bin = "calaos_prov_relaunch";
    ASSERT_TRUE(installForeignlyKilledStandIn(bin));

    Driver &driver = *new Driver();
    driver.drive(bin, "provrelaunch");

    const bool relaunched = ExternProcSpawn::runLoopUntil(
        [&bin]() { return launchCount(bin) >= 3; }, kBudgetMs);

    const int signalled = driver.server->lastTermSignal();
    const bool requested = driver.server->lastStopWasRequested();
    driver.stop();

    ASSERT_TRUE(relaunched)
        << "the sidecar was launched " << launchCount(bin) << " times in "
        << kBudgetMs << " ms: the loop this case reads never ran";
    ASSERT_EQ(SIGTERM, signalled)
        << "the stand-in did not die by SIGTERM, so nothing here asks who sent "
           "one";

    EXPECT_FALSE(requested)
        << "a relaunch the server performed by itself was recorded as a stop "
           "the server asked for: the provenance is read from a counter that "
           "every launch moves, so no foreign signal can ever be seen again";
}

TEST_F(SidecarTerminateProvenanceTest, TheStopTheServerAsksForStaysAVoluntaryStop)
{
    loadConfig();

    const std::string bin = "calaos_prov_ours";
    ASSERT_TRUE(installSleepingStandIn(bin));

    Driver &driver = *new Driver();
    driver.drive(bin, "provours");

    const bool up = ExternProcSpawn::runLoopUntil(
        [&bin]() { return launchCount(bin) >= 1; }, kBudgetMs);

    driver.stop();

    const bool gone = up && ExternProcSpawn::runLoopUntil(
        [&driver]() { return driver.server->lastTermSignal() != 0; }, kBudgetMs);

    ASSERT_TRUE(up) << "the sidecar never started, so nothing was stopped";
    ASSERT_TRUE(gone)
        << "the child never reported an exit within " << kBudgetMs << " ms: "
           "this case would be reading the state of a server that stopped "
           "nothing";
    ASSERT_EQ(SIGTERM, driver.server->lastTermSignal())
        << "terminate() did not end the child by SIGTERM, so the signal whose "
           "origin is in question is not the one this case observed";
    ASSERT_EQ(0, driver.server->lastExitStatus());

    EXPECT_TRUE(driver.server->lastStopWasRequested())
        << "the server did not recognise its own terminate(), so an ordinary "
           "shutdown is about to be charged as a crash";
    EXPECT_EQ(0, driver.server->respawnFailures())
        << "stopping a sidecar on purpose fed the relaunch ramp: the next "
           "start of a sidecar nobody complained about is held back";
    EXPECT_EQ(0.0, ExternProcServer::respawnDelay(
                       driver.server->respawnFailures()));
}

/******************************************************************************
 * AND WHAT THE JOURNAL SAYS OF EACH.
 ******************************************************************************/

TEST_F(SidecarTerminateProvenanceTest, AForeignSigtermIsReportedAndOursIsNot)
{
    loadConfig();

    const std::string binForeign = "calaos_prov_log_foreign";
    const std::string binQuiet = "calaos_prov_log_quiet";
    const std::string binNoisy = "calaos_prov_log_noisy";
    ASSERT_TRUE(installForeignlyKilledStandIn(binForeign));
    ASSERT_TRUE(installSleepingStandIn(binQuiet));
    ASSERT_TRUE(installNoisyStandIn(binNoisy));

    //One name per server: a needle is then looked for on the line of the
    //server that owns it, and a child left over from another case cannot
    //answer for it.
    const std::string nameForeign = "provlogforeign";
    const std::string nameQuiet = "provlogquiet";
    const std::string nameNoisy = "provlognoisy";

    std::ostringstream foreignSink;
    std::streambuf *saved = std::cout.rdbuf(foreignSink.rdbuf());

    Driver &foreign = *new Driver();
    foreign.drive(binForeign, nameForeign);
    ExternProcSpawn::runLoopUntil([]() { return false; }, kRampWindowMs);

    std::cout.rdbuf(saved);
    foreign.stop();
    ExternProcSpawn::runLoopUntil([]() { return false; }, 300);

    std::ostringstream ourSink;
    saved = std::cout.rdbuf(ourSink.rdbuf());

    Driver &ours = *new Driver();
    ours.drive(binQuiet, nameQuiet);
    const bool up = ExternProcSpawn::runLoopUntil(
        [&binQuiet]() { return launchCount(binQuiet) >= 1; }, kBudgetMs);
    ours.stop();
    const bool gone = up && ExternProcSpawn::runLoopUntil(
        [&ours]() { return ours.server->lastTermSignal() != 0; }, kBudgetMs);

    //The proof that this sink is listening. Without it, "no alert line" is
    //satisfied just as well by a capture that heard nothing at all.
    Driver &noisy = *new Driver();
    noisy.drive(binNoisy, nameNoisy);
    const bool heard = ExternProcSpawn::runLoopUntil(
        [&ourSink, &nameNoisy]()
        {
            return !linesCarrying(ourSink.str(),
                                  nameNoisy + " exited with status").empty();
        }, kBudgetMs);
    noisy.stop();

    std::cout.rdbuf(saved);

    const std::string foreignOut = foreignSink.str();
    const std::string ourOut = ourSink.str();

    ASSERT_TRUE(up) << "the sidecar of the voluntary stop never started";
    ASSERT_TRUE(gone) << "the child terminate() signalled never reported an exit";
    ASSERT_TRUE(heard)
        << "the second capture never saw the line of a sidecar that exits with "
           "status " << kNoisyExitCode << ", so it is deaf and proves nothing "
           "about what is missing from it";

    EXPECT_FALSE(linesCarrying(foreignOut,
                               nameForeign + " was killed by signal " +
                                   std::to_string(SIGTERM)).empty())
        << "nothing was said of a sidecar killed by a SIGTERM the server never "
           "sent: a third party can kill it in a loop and the journal stays "
           "silent about it\n--- captured ---\n" << foreignOut;

    EXPECT_TRUE(linesCarrying(ourOut, nameQuiet + " was killed by signal").empty())
        << "stopping a sidecar on purpose printed an alert, so every ordinary "
           "shutdown of the server now complains\n--- captured ---\n" << ourOut;
}

/*
 * Own main instead of gtest_main, same reason as
 * core/ExternProcCrashBackoff_test.cpp: destroying an ExternProcServer at
 * process exit signals the last spawned child and closes libuv handles on a
 * loop nothing pumps anymore.
 */
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    ExternProcSpawn::teardown();

    fflush(nullptr);
    _exit(ret);
}
