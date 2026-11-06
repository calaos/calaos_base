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
 * ⭐⭐ A SIDECAR THAT CRASHES, AND WHERE ITS UPTIME IS COUNTED FROM.
 *
 * ---------------------------------------------------------------------------
 * THE TWO DEFECTS
 * ---------------------------------------------------------------------------
 * 1. The relaunch ramp decided on the exit STATUS alone, and a zero status
 *    there meant "voluntary stop". Under Linux a child killed by SIGSEGV,
 *    SIGABRT or the OOM killer leaves status 0 and its signal beside it, so a
 *    sidecar that CRASHES in a loop kept the cadence of before the ramp - for
 *    every family, calaos_mqtt included, i.e. the only one the ramp was ever
 *    measured to slow down.
 *
 * 2. The reset rule ("a run this long is a new incident") reads a duration,
 *    and nothing held the point that duration is counted FROM. Counting it
 *    from the moment the relaunch was ASKED FOR rather than from the spawn
 *    folds the hold itself into the child's service time: at the ceiling every
 *    child would look like it served half a minute, the ramp would reset on
 *    every turn, and the cadence would silently return to one launch per
 *    0.2 s. The rule was pinned as a FUNCTION and never as a COMPOSITION.
 *
 * ---------------------------------------------------------------------------
 * ⭐⭐ WHAT IS MEASURED, AND WITH WHAT
 * ---------------------------------------------------------------------------
 * Same discipline as core/ExternProcRespawnBackoff_test.cpp: launches are
 * COUNTED inside a fixed budget and bounded from ABOVE, because load can only
 * make fewer launches happen, never more. No case states a launches-per-minute
 * figure, which is a wall clock reading and worthless on a shared machine.
 *
 * The one duration read here is bounded from above as well - and that
 * direction IS load-sensitive - so it is posed against a hold of several
 * hundred milliseconds rather than against the 100 ms of a first failure, and
 * it carries its own floor so that a case which measured nothing cannot pass
 * for a case that found nothing.
 *
 * ---------------------------------------------------------------------------
 * ⛔ THE COUNTERWEIGHTS
 * ---------------------------------------------------------------------------
 * A transport that gave up relaunching would satisfy every upper bound below,
 * so each one is paired with a demand that a relaunch still happened; and the
 * stand-in of the crash case is checked to have left status 0, or the case
 * would be measuring an ordinary non-zero exit and proving nothing.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <csignal>
#include <fstream>
#include <string>
#include <sys/stat.h>

#include "ExternProc.h"
#include "core/CalaosCoreFixture.h"
#include "core/ExternProcSpawnHarness.h"

using namespace CalaosTest;

namespace
{

//The window a counting case pumps the loop for, and the launches the ramp
//allows inside it. Launches land at 0, 0.2, 0.5 and 1.0 s with the ramp (the
//100 ms signal deferral plus the hold) and every ~0.11 s without it, so the
//budget separates the two by a factor of three.
const int kRampWindowMs = 1600;
const int kRampMaxLaunches = 4;

//Failures to reach before reading the uptime. respawnDelay(4) is 0.8 s, which
//is the hold the launch under test waits out; a stand-in that dies at once
//serves for a few milliseconds, so truth and mutant sit two orders of
//magnitude apart.
const int kUptimeFailures = 4;
const int kUptimeBudgetMs = 8000;

//What the plain failing stand-in answers. Same value as the sibling suite:
//a status that appears nowhere else cannot be confused with a generic failure.
const int kStandInExitCode = 3;

std::string standInPath(const std::string &name)
{
    return ExternProcSpawn::sandboxDir() + "/" + name;
}

/*
 * A stand-in that dies the way a crash does: journal its argv, then kill
 * ITSELF with the signal asked for. `kill -N $$` and not `exit N` is the whole
 * point - the kernel then reports status 0 with the signal beside it, which is
 * exactly the shape the transport used to read as a clean stop.
 *
 * Written here rather than in core/ExternProcSpawnHarness.h so that no other
 * suite's stand-ins change shape; the name is registered with the harness so
 * teardown() still removes the script and its journal.
 */
bool installCrashingStandIn(const std::string &binName, int sig)
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
             "kill -" << sig << " $$\n"
             //Reached only if the signal was ignored, and a plain exit there
             //would let the case pass on a status it was not testing.
             "sleep 5\n";
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
 * The shape of every controller of the tree: relaunch from the exit signal,
 * with the same arguments, forever.
 *
 * ⚠️ HEAP, AND LEAKED, like the server it drives - the exit of the child that
 * terminate() signals is published 100 ms later, after the case that armed it
 * has returned, and a stack instance would then be read at an address the next
 * case has reused.
 */
struct RespawnLoop
{
    ExternProcServer *server = nullptr;
    bool stopped = false;
    int asked = 0;

    void drive(const std::string &binName)
    {
        const std::string exe = standInPath(binName);
        server = new ExternProcServer("crashback");
        server->processExited.connect([this, exe]()
        {
            if (stopped)
                return;
            asked++;
            server->startProcess(exe, "crashback");
        });
        asked++;
        server->startProcess(exe, "crashback");
    }

    void stop()
    {
        stopped = true;
        if (server)
            server->terminate();
    }
};

class ExternProcCrashBackoffTest: public CoreFixture
{
};

} //namespace

/******************************************************************************
 * ⭐ A CRASH IS A FAILURE, READ WITHOUT A CLOCK.
 ******************************************************************************/

TEST_F(ExternProcCrashBackoffTest, ADeathBySignalCountsAsAFailure)
{
    //Status 0 in all three: that is what the kernel reports for a child that
    //never chose to exit, and the whole defect was reading that zero alone.
    EXPECT_EQ(1, ExternProcServer::nextFailureCount(0, 0, SIGSEGV, 0.0))
        << "a segmentation fault left the failure count at zero, so a sidecar "
           "that crashes on every start is relaunched as fast as the loop "
           "allows - forever";
    EXPECT_EQ(4, ExternProcServer::nextFailureCount(3, 0, SIGABRT, 0.0))
        << "an abort after three failures did not climb the ramp";
    EXPECT_EQ(1, ExternProcServer::nextFailureCount(0, 0, SIGKILL, 0.0))
        << "a child taken by the OOM killer is not counted, and an out of "
           "memory loop is exactly the loop that must slow down";

    //The ramp is what the count feeds, so state the consequence too: a hold
    //equal to the minimum is the cadence of before the ramp existed.
    EXPECT_GT(ExternProcServer::respawnDelay(
                  ExternProcServer::nextFailureCount(3, 0, SIGSEGV, 0.0)),
              ExternProcServer::kRespawnDelayMin)
        << "four crashes in a row still cost the shortest hold there is";
}

TEST_F(ExternProcCrashBackoffTest, TheSignalOfTerminateIsStillAVoluntaryStop)
{
    //terminate() sends SIGTERM. Counting it would drag every ordinary
    //shutdown into the ramp, which is the property the sibling suite guards.
    EXPECT_EQ(0, ExternProcServer::nextFailureCount(9, 0, SIGTERM, 0.0))
        << "the SIGTERM of terminate() was counted as a crash";
    EXPECT_EQ(0, ExternProcServer::nextFailureCount(9, 0, SIGTERM, 3600.0));

    //⚠️ The status and the signal are two ints side by side, and swapping them
    //at the call site changes no value in any case above: SIGTERM is the one
    //argument for which the two orders disagree - as a signal it clears the
    //count, as a status it is a failure like any other.
    EXPECT_EQ(10, ExternProcServer::nextFailureCount(9, SIGTERM, 0, 0.0))
        << "a status of " << SIGTERM << " was read as the signal of a "
           "voluntary stop: the two arguments are interchangeable, so nothing "
           "says which of them the call site passes where";
}

TEST_F(ExternProcCrashBackoffTest, ARunLongEnoughStartsTheRampOverAfterACrashToo)
{
    EXPECT_EQ(1, ExternProcServer::nextFailureCount(9, 0, SIGSEGV, 3600.0))
        << "a sidecar that served for an hour and then crashed is picked up "
           "at the ceiling it left behind";
    EXPECT_EQ(1, ExternProcServer::nextFailureCount(
                     9, 0, SIGSEGV, ExternProcServer::kRespawnResetSeconds));
    EXPECT_EQ(10, ExternProcServer::nextFailureCount(
                      9, 0, SIGABRT,
                      ExternProcServer::kRespawnResetSeconds - 0.001))
        << "a crash just under the reset threshold is the same incident "
           "repeating, and must still climb";
}

/******************************************************************************
 * ⭐ AND WHAT A CHILD THAT REALLY CRASHES GETS, COUNTED.
 ******************************************************************************/

TEST_F(ExternProcCrashBackoffTest, ASidecarKilledBySignalIsSlowedDownButNeverDropped)
{
    loadConfig();

    const std::string bin = "calaos_crash_kill";
    ASSERT_TRUE(installCrashingStandIn(bin, SIGKILL));

    RespawnLoop &loop = *new RespawnLoop();
    loop.drive(bin);
    ExternProcSpawn::runLoopUntil([]() { return false; }, kRampWindowMs);

    const int launched = static_cast<int>(ExternProcSpawn::launches(bin).size());
    const int64_t status = loop.server->lastExitStatus();
    const int signalled = loop.server->lastTermSignal();
    const int failures = loop.server->respawnFailures();
    loop.stop();

    //⚠️ THE FIXTURE FIRST. A stand-in that exited with a code instead of dying
    //by a signal would make every assertion below pass while measuring the
    //case that already worked.
    ASSERT_EQ(SIGKILL, signalled)
        << "the stand-in did not die by SIGKILL, so this case is not about a "
           "crash at all";
    ASSERT_EQ(0, status)
        << "the child left status " << status << ": the whole defect is that a "
           "crash leaves a ZERO there, and this fixture no longer reproduces it";

    EXPECT_LE(launched, kRampMaxLaunches)
        << launched << " launches in " << kRampWindowMs << " ms: a sidecar "
           "killed by a signal is relaunched as fast as the loop allows, so a "
           "crash loop costs exactly what it cost before the ramp";
    EXPECT_GE(launched, 2)
        << "the sidecar was never relaunched at all: slowing down was traded "
           "for giving up";
    EXPECT_GE(failures, 2)
        << "the crashes were not counted, so the ramp is fed a number that "
           "never moves";
}

TEST_F(ExternProcCrashBackoffTest, TheUptimeIsCountedFromTheSpawnAndNotFromTheRequest)
{
    loadConfig();

    /*
     * The reset rule reads lastRunSeconds(), and every pure case above hands
     * it a number directly: none of them sees WHERE the transport counts that
     * number from. This case drives the real loop until a launch has waited
     * out a hold of several hundred milliseconds, and demands that the hold
     * did not end up inside the child's service time.
     */
    const std::string bin = "calaos_crash_uptime";
    ASSERT_TRUE(ExternProcSpawn::install(bin, kStandInExitCode));

    RespawnLoop &loop = *new RespawnLoop();
    loop.drive(bin);

    const bool climbed = ExternProcSpawn::runLoopUntil(
        [&loop]() { return loop.server->respawnFailures() >= kUptimeFailures; },
        kUptimeBudgetMs);

    //The hold the NEXT launch waits out, read from the ramp itself rather than
    //written down, so the two cannot drift apart.
    const double hold = ExternProcServer::respawnDelay(kUptimeFailures);

    const bool ranAgain = climbed && ExternProcSpawn::runLoopUntil(
        [&loop]() { return loop.server->respawnFailures() > kUptimeFailures; },
        kUptimeBudgetMs);

    const double ran = loop.server->lastRunSeconds();
    loop.stop();

    //⚠️ A case that never got the loop this far would read a run of 0 s and
    //satisfy the upper bound below without measuring anything.
    ASSERT_TRUE(climbed) << "the ramp never reached " << kUptimeFailures
                         << " failures within " << kUptimeBudgetMs << " ms";
    ASSERT_TRUE(ranAgain)
        << "the launch held for " << hold << " s never happened or never "
           "ended: nothing was relaunched after the ramp started to bite";
    ASSERT_GT(hold, 0.0) << "the ramp holds nothing after " << kUptimeFailures
                         << " failures, so this case waits for no hold at all";
    EXPECT_GT(ran, 0.0)
        << "the run recorded for a child that really ran is zero, so the "
           "duration the reset rule reads is not measured at all";

    EXPECT_LT(ran, hold)
        << "a child that died at once is credited with " << ran << " s of "
           "service after a hold of " << hold << " s: the uptime is counted "
           "from the moment the relaunch was ASKED FOR, so at the ceiling "
           "every child looks like it served half a minute and the ramp "
           "resets on every turn";
}

/*
 * Own main instead of gtest_main, same reason as
 * core/ExternProcRespawnBackoff_test.cpp: destroying an ExternProcServer at
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
