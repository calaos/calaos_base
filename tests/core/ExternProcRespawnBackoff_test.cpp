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
 * ⭐⭐ HOW OFTEN A SIDECAR THAT KEEPS DYING IS RELAUNCHED.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT
 * ---------------------------------------------------------------------------
 * ExternProcServer published the end of its child as a signal carrying
 * nothing, and the eight controllers subscribed to it relaunched on the spot.
 * A broker that is simply switched off therefore produced a launch every
 * ~110 ms - the 100 ms deferral of the signal plus the few milliseconds the
 * sidecar needs to fail - forever, with three journal lines each time and no
 * end in sight. The exit status existed and named the cause; nobody read it.
 *
 * ---------------------------------------------------------------------------
 * ⭐⭐ WHY NOTHING HERE MEASURES A DURATION
 * ---------------------------------------------------------------------------
 * A case that asserts "the second launch came at least 200 ms after the first"
 * fails on a loaded machine and says nothing about the code. Every assertion
 * below is a COUNT OF LAUNCHES inside a fixed budget, and it is bounded from
 * ABOVE: load can only make fewer launches happen, never more, so the only way
 * to break the bound is to relaunch too fast. The two lower bounds that do
 * exist are of the same shape reversed - a duration that must be AT LEAST as
 * long as the child slept, which load can only lengthen.
 *
 * The ramp and the reset rule themselves are pure functions of the failure
 * count, the status and the run length, and are read directly: no clock, no
 * loop, no child.
 *
 * ---------------------------------------------------------------------------
 * ⛔ THE COUNTERWEIGHTS
 * ---------------------------------------------------------------------------
 * A transport that stopped relaunching altogether would satisfy every upper
 * bound here, so each of them is paired: the throttled case demands that a
 * relaunch still happen, and the clean-exit case demands that a child which
 * ends with status 0 keeps the original cadence. Never abandoning is the
 * decision this suite guards, as much as slowing down is.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "ExternProc.h"
#include "core/CalaosCoreFixture.h"
#include "core/ExternProcSpawnHarness.h"

using namespace CalaosTest;

namespace
{

/*
 * The window each counting case pumps the loop for, and the launches the ramp
 * allows inside it.
 *
 * With the ramp, launches land at 0, 0.2, 0.5 and 1.0 s (each cycle is the
 * 100 ms signal deferral plus the hold) and the fifth would land at 1.9 s.
 * Without it they land every ~0.11 s. The budget sits between the fourth and
 * the fifth, so the bound separates the two by a factor of three and not by a
 * few milliseconds.
 */
const int kRampWindowMs = 1600;
const int kRampMaxLaunches = 4;

//Long enough for the unthrottled cadence to produce many launches, short
//enough that a suite which never relaunches fails instead of hanging.
const size_t kCadenceLaunches = 8;
const int kCadenceBudgetMs = 5000;

//What the failing stand-in answers. Not 1: a status that appears nowhere else
//in the tree cannot be confused with a generic failure.
const int kStandInExitCode = 3;

//How long the "it ran for a while" stand-in stays up, and the floor a case may
//demand back. The floor is under the sleep because a process can only be slow.
const double kStandInHoldSeconds = 0.3;
const double kStandInHoldFloorSeconds = 0.25;

std::string standInPath(const std::string &name)
{
    return ExternProcSpawn::sandboxDir() + "/" + name;
}

/*
 * The shape of every controller of the tree: relaunch from the exit signal,
 * with the same arguments, forever. Nothing here decides when - that is the
 * whole question the suite asks of the transport.
 *
 * ⚠️ HEAP, AND LEAKED, like the server it drives. The exit of the child that
 * terminate() signals is published 100 ms later, i.e. after the case that
 * armed it has returned; a stack instance is then read at an address the next
 * case has reused for its own, and the two loops drive each other's server.
 * Measured, not feared.
 */
struct RespawnLoop
{
    ExternProcServer *server = nullptr;
    bool stopped = false;
    //Calls to startProcess(), which is NOT the number of launches: a held
    //relaunch is asked for long before the child appears. Only the stand-in's
    //own journal says when one really did.
    int asked = 0;

    void drive(const std::string &binName)
    {
        const std::string exe = standInPath(binName);
        server = new ExternProcServer("backoff");
        server->processExited.connect([this, exe]()
        {
            if (stopped)
                return;
            asked++;
            server->startProcess(exe, "backoff");
        });
        asked++;
        server->startProcess(exe, "backoff");
    }

    //Before the case returns, or the loop keeps spawning into every case that
    //follows: terminate() gives back the child and drops the held relaunch,
    //but the exit it provokes would otherwise be answered by another start.
    void stop()
    {
        stopped = true;
        if (server)
            server->terminate();
    }
};

class ExternProcRespawnBackoffTest: public CoreFixture
{
};

} //namespace

/******************************************************************************
 * THE RAMP, READ WITHOUT A CLOCK.
 ******************************************************************************/

TEST_F(ExternProcRespawnBackoffTest, TheFirstLaunchIsNeverHeld)
{
    EXPECT_DOUBLE_EQ(0.0, ExternProcServer::respawnDelay(0))
        << "a server that has failed nothing yet is made to wait before its "
           "very first spawn";
    EXPECT_DOUBLE_EQ(0.0, ExternProcServer::respawnDelay(-1))
        << "a negative count must read as no failure at all, not as a hold";
}

TEST_F(ExternProcRespawnBackoffTest, TheRampStartsAtTheOldCadenceAndGrows)
{
    EXPECT_DOUBLE_EQ(ExternProcServer::kRespawnDelayMin,
                     ExternProcServer::respawnDelay(1))
        << "the first relaunch after a failure no longer costs what it always "
           "cost: a single failure is not a reason to slow anything down";

    for (int n = 2; n <= 40; n++)
        ASSERT_GE(ExternProcServer::respawnDelay(n),
                  ExternProcServer::respawnDelay(n - 1))
            << "the hold went DOWN between " << n - 1 << " and " << n
            << " consecutive failures";

    EXPECT_GT(ExternProcServer::respawnDelay(4),
              ExternProcServer::respawnDelay(1))
        << "the hold is the same after four failures as after one: this is a "
           "fixed delay wearing the shape of a ramp";
}

TEST_F(ExternProcRespawnBackoffTest, TheRampReachesItsCeilingAndStopsThere)
{
    EXPECT_DOUBLE_EQ(0.1, ExternProcServer::kRespawnDelayMin);
    EXPECT_DOUBLE_EQ(30.0, ExternProcServer::kRespawnDelayMax)
        << "the ceiling is not the one that was decided: a sidecar whose "
           "broker comes back must be picked up within half a minute";

    for (int n = 0; n <= 200; n++)
        ASSERT_LE(ExternProcServer::respawnDelay(n),
                  ExternProcServer::kRespawnDelayMax)
            << "the hold passed the ceiling after " << n << " failures";

    //A count no ramp reaches in practice: an unbounded doubling overflows to
    //infinity here, and a saturating one answers the ceiling.
    EXPECT_DOUBLE_EQ(ExternProcServer::kRespawnDelayMax,
                     ExternProcServer::respawnDelay(1000000))
        << "the plateau is not flat: the ramp is still climbing where nothing "
           "should be climbing anymore";
    EXPECT_DOUBLE_EQ(ExternProcServer::respawnDelay(60),
                     ExternProcServer::respawnDelay(1000000));
}

/******************************************************************************
 * WHAT CLEARS THE RAMP, READ WITHOUT A CLOCK.
 ******************************************************************************/

TEST_F(ExternProcRespawnBackoffTest, AVoluntaryStopIsNotAFailure)
{
    //terminate() signals the child, and a SIGTERM leaves status 0. Counting it
    //would make every ordinary shutdown push the next start into the ramp.
    EXPECT_EQ(0, ExternProcServer::nextFailureCount(0, 0, 0.0));
    EXPECT_EQ(0, ExternProcServer::nextFailureCount(9, 0, 0.0))
        << "a clean exit after nine failures left the ramp where it was";
    EXPECT_EQ(0, ExternProcServer::nextFailureCount(9, 0, 3600.0));
}

TEST_F(ExternProcRespawnBackoffTest, ConsecutiveFailuresClimb)
{
    EXPECT_EQ(1, ExternProcServer::nextFailureCount(0, 1, 0.0));
    EXPECT_EQ(2, ExternProcServer::nextFailureCount(1, 1, 0.0));
    EXPECT_EQ(10, ExternProcServer::nextFailureCount(9, kStandInExitCode, 0.0));

    //Just under the reset: the failure that follows a run of almost long
    //enough is still the same incident.
    EXPECT_EQ(10, ExternProcServer::nextFailureCount(
                      9, 1, ExternProcServer::kRespawnResetSeconds - 0.001));
}

TEST_F(ExternProcRespawnBackoffTest, ARunLongEnoughStartsTheRampOver)
{
    //The point of the whole rule: a sidecar that worked for an hour and then
    //died must be picked up at once, not at the ceiling it left behind.
    EXPECT_EQ(1, ExternProcServer::nextFailureCount(9, 1, 3600.0));
    EXPECT_DOUBLE_EQ(ExternProcServer::kRespawnDelayMin,
                     ExternProcServer::respawnDelay(
                         ExternProcServer::nextFailureCount(40, 1, 3600.0)))
        << "an hour of service still costs the next relaunch the ceiling";

    EXPECT_EQ(1, ExternProcServer::nextFailureCount(
                     9, 1, ExternProcServer::kRespawnResetSeconds));

    EXPECT_DOUBLE_EQ(ExternProcServer::kRespawnDelayMax,
                     ExternProcServer::kRespawnResetSeconds)
        << "the reset threshold and the ceiling drifted apart: a sidecar can "
           "now die often enough to escape the ramp and still be looping";
}

/******************************************************************************
 * ⭐ AND WHAT A REAL CHILD ACTUALLY GETS, COUNTED.
 ******************************************************************************/

TEST_F(ExternProcRespawnBackoffTest, AFailingSidecarIsSlowedDownButNeverDropped)
{
    loadConfig();

    const std::string bin = "calaos_backoff_fail";
    ASSERT_TRUE(ExternProcSpawn::install(bin, kStandInExitCode));

    RespawnLoop &loop = *new RespawnLoop();
    loop.drive(bin);
    ExternProcSpawn::runLoopUntil([]() { return false; }, kRampWindowMs);

    const int launched = static_cast<int>(ExternProcSpawn::launches(bin).size());
    const int64_t status = loop.server->lastExitStatus();
    const int failures = loop.server->respawnFailures();
    loop.stop();

    EXPECT_LE(launched, kRampMaxLaunches)
        << launched << " launches in " << kRampWindowMs << " ms: a "
           "sidecar that fails on every start is being relaunched as fast as "
           "the loop allows";
    EXPECT_GE(launched, 2)
        << "the sidecar was never relaunched at all: slowing down was traded "
           "for giving up, which is the one answer that was refused";

    EXPECT_EQ(kStandInExitCode, status)
        << "the status the child left behind did not reach the transport, so "
           "nothing can tell a failure from a voluntary stop";
    EXPECT_GE(failures, 2)
        << "the failures were not counted, so the ramp above is being fed a "
           "number that never moves";
}

TEST_F(ExternProcRespawnBackoffTest, ACleanExitKeepsTheOriginalCadence)
{
    loadConfig();

    const std::string bin = "calaos_backoff_clean";
    ASSERT_TRUE(ExternProcSpawn::install(bin, 0));

    RespawnLoop &loop = *new RespawnLoop();
    loop.drive(bin);
    const bool reached = ExternProcSpawn::runLoopUntil(
        [&bin]() {
            return ExternProcSpawn::launches(bin).size() >= kCadenceLaunches;
        },
        kCadenceBudgetMs);

    const size_t launched = ExternProcSpawn::launches(bin).size();
    const int failures = loop.server->respawnFailures();
    loop.stop();

    EXPECT_TRUE(reached)
        << "only " << launched
        << " launches of a sidecar exiting 0 in "
        << kCadenceBudgetMs << " ms: a clean stop is being counted as a "
           "failure, and every ordinary shutdown now drags the next start "
           "into the ramp";
    EXPECT_EQ(0, failures)
        << "a status of 0 left the failure count at " << failures;
}

TEST_F(ExternProcRespawnBackoffTest, TheRunIsMeasuredFromTheSpawnToTheExit)
{
    loadConfig();

    //The reset rule reads a duration, and a call site handing it zero would be
    //invisible to every case above: this is the half that watches the input.
    const std::string bin = "calaos_backoff_slow";
    ASSERT_TRUE(ExternProcSpawn::install(bin, kStandInExitCode,
                                         kStandInHoldSeconds));

    RespawnLoop &loop = *new RespawnLoop();
    loop.drive(bin);
    const bool reached = ExternProcSpawn::runLoopUntil(
        [&loop]() { return loop.asked >= 2; }, kCadenceBudgetMs);

    const double ran = loop.server->lastRunSeconds();
    const int failures = loop.server->respawnFailures();
    loop.stop();

    ASSERT_TRUE(reached)
        << "the stand-in never exited within " << kCadenceBudgetMs << " ms";
    EXPECT_GE(ran, kStandInHoldFloorSeconds)
        << "a child that stayed up " << kStandInHoldSeconds << " s is recorded "
           "as having run " << ran << " s";
    EXPECT_EQ(1, failures)
        << "a run of " << kStandInHoldSeconds << " s is well under the reset "
           "threshold, so the failure that ended it must still climb";
}

/*
 * Own main instead of gtest_main, same reason as core/ExternProcArgv_test.cpp:
 * destroying an ExternProcServer at process exit signals the last spawned
 * child and closes libuv handles on a loop nothing pumps anymore.
 */
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    ExternProcSpawn::teardown();

    fflush(nullptr);
    _exit(ret);
}
