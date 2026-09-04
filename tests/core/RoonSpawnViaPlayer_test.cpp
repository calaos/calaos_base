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
 * T3.28b - ⭐ THE SECOND BINARY, AND WHY IT IS A SECOND BINARY.
 *
 * ---------------------------------------------------------------------------
 * THE MUTATION THAT SURVIVED, AND WHAT IT MEANT
 * ---------------------------------------------------------------------------
 * The review of T3.28b exchanged, in the SHIPPED code,
 *
 *     RoonPlayer.cpp:227   RoonCtrl::Instance(host, port);
 *  -> RoonPlayer.cpp:227   RoonCtrl::Instance(host, RoonArgs::DefaultPort);
 *
 * and the whole of core/RoonArgs_test stayed GREEN - 14/14, exit 0. That
 * mutant is not a curiosity: it is the field defect of T3.28 - a sidecar
 * started on a port nobody configured - MOVED ONE CALL SITE FURTHER. A
 * statically configured core on 9331 would silently be looked for on 9330,
 * and every oracle of the tree would have said the ticket was done.
 *
 * It survived because core/RoonArgs_test reaches the controller by calling
 * RoonCtrl::Instance() ITSELF. The launch arguments it observes are then the
 * ones the TEST spelled, and RoonPlayer.cpp:227 is never on the path.
 *
 * ---------------------------------------------------------------------------
 * WHY IT CANNOT BE FIXED INSIDE core/RoonArgs_test
 * ---------------------------------------------------------------------------
 * RoonCtrl::Instance() is a function-local static (RoonPlayer.cpp:86): the
 * FIRST call in a process wins and every later call hands the same object
 * back, arguments ignored. So within one process there is exactly one chance
 * to observe a launch, and the two ways of using it are mutually exclusive:
 *
 *   - call Instance() directly    -> observes the RoonCtrl constructor and the
 *                                    respawn, blind to RoonPlayer.cpp:227;
 *   - build a real RoonPlayer     -> observes RoonPlayer.cpp:227, which is
 *                                    what carries the configuration across.
 *
 * The delivery sheet of T3.28b called that exclusion a reason to cover only
 * the first. It is not: a test BINARY is a process. Splitting the second form
 * into its own binary gives the singleton a fresh process, and both forms are
 * covered at once - no fork, no seam, no test-only hook in src/. There are 87
 * other test binaries in tests/Makefile.am; this is the ordinary shape of the
 * tree, not a device.
 *
 * ⇒ This binary exists for ONE case, deliberately. Adding a second case that
 * needs a differently-configured controller would put us back where we were.
 *
 * ---------------------------------------------------------------------------
 * WHAT IT WATCHES
 * ---------------------------------------------------------------------------
 * A RoonPlayer built from a real Params with a NON-EMPTY zone_id runs
 * RoonPlayer.cpp:198-236 to the end: it resolves host and port out of the
 * configuration (:215, :218) and hands BOTH to RoonCtrl::Instance() (:227).
 * The controller builds the command line from them (:41), launch() spawns
 * (:76), the stand-in calaos_roon records its argv and exits, the 0.1s respawn
 * timer fires and the same path runs again. Every recorded launch is checked.
 *
 * The harness - the CALAOS_BIN_PREFIX sandbox, the recorder, the bounded loop
 * pump, and the teardown that gives back the directory, the socket and the
 * child - is shared with core/RoonArgs_test and documented in
 * core/RoonSpawnHarness.h.
 *
 * ⛔ WHAT THIS STILL DOES NOT PROVE: that calaos_roon does the right thing
 * with those flags. The recorder is a stand-in; no Roon core was involved,
 * here or anywhere in T3.28.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <unistd.h>

#include <string>
#include <vector>

#include "CalaosCoreFixture.h"
#include "Params.h"
#include "RoonArgs.h"
#include "RoonPlayer.h"
#include "RoonSpawnHarness.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/*
 * ⚠️ FIXTURE, NOT DECORATION. The host and the port are DELIBERATELY not the
 * defaults, and that is the whole reason this case can see the mutant it was
 * written for:
 *  - 9331 is one away from the 9330 default, so "the configured value crossed"
 *    and "a default was substituted" cannot be confused. On 9330 the mutation
 *    RoonCtrl::Instance(host, RoonArgs::DefaultPort) would be INVISIBLE.
 *  - an empty host makes buildArgs() answer an empty list, so a silent emitter would
 *    pass; 192.168.7.42 cannot be mistaken for the port either, should the two
 *    ever be permuted.
 *  - zone_id is NON-EMPTY, which is what carries the constructor past the
 *    early return at :220 and into the launch. An empty one is the shape
 *    core/RoonArgs_test uses precisely to stop short of it.
 */
Params staticRoonParams()
{
    Params p;
    p.Add("id", "roon_spawn_via_player");
    p.Add("name", "roon_spawn_via_player");
    p.Add("type", "Roon");
    p.Add("zone_id", "160132a7337c26b01e556c2809514e65d6a0");
    p.Add("host", "192.168.7.42");
    p.Add("port", "9331");
    return p;
}

/*
 * The player is LEAKED, on purpose.
 *
 * RoonPlayer's constructor arms Timer::singleShot(10, [this]{ ... })
 * (RoonPlayer.cpp:230-233) - a lambda, so sigc::trackable does NOT disconnect
 * it when the object dies (FINDINGS.md, the lambda-vs-mem_fun distinction).
 * Destroying the player while the default loop can still run that timer is a
 * use-after-free. This binary keeps it alive for the life of the process,
 * which is also what makes the case replayable under --gtest_repeat.
 */
RoonPlayer *thePlayer()
{
    static RoonPlayer *player = nullptr;
    if (!player)
    {
        Params p = staticRoonParams();
        player = new RoonPlayer(p);
    }
    return player;
}

} // namespace

class RoonSpawnViaPlayerTest: public CoreFixture {};

/*
 * ⭐⭐ THE CASE THIS BINARY EXISTS FOR: what RoonPlayer.cpp:227 hands the
 * controller is the core THE USER CONFIGURED, not a default.
 *
 * ⚠️ EVERY LAUNCH IS CHECKED, NOT "AT LEAST ONE". The defect T3.28 fixed was a
 * FIRST launch that was right followed by a respawn that was wrong; an
 * assertion happy with one good line would have been green on the very bug
 * this suite exists to catch. The loop indexes and names the offending launch.
 *
 * ⚠️ The tail is pinned, not searched for: `--namespace roon` is included so
 * the arguments are pinned AT THE END of the command line and nothing can
 * follow them. Only the `--socket <random path>` prefix is left free, because
 * ExternProcServer picks it.
 */
TEST_F(RoonSpawnViaPlayerTest, TheSidecarIsLaunchedWithTheCoreTheIoWasConfiguredWith)
{
    ASSERT_TRUE(RoonSpawn::install())
        << "could not set up the CALAOS_BIN_PREFIX sandbox";

    RoonPlayer *player = thePlayer();
    ASSERT_TRUE(player != nullptr);

    //Reading the two members back first: if the CONFIGURATION was not resolved,
    //the launch cannot carry it either, and the two failures must not be
    //confused in the log.
    ASSERT_EQ("192.168.7.42", player->hostGet());
    ASSERT_EQ(9331, player->portGet());

    const bool respawned = RoonSpawn::runLoopUntil(
        []() { return RoonSpawn::journalLines().size() >= 2; }, 5000);

    const std::vector<std::string> launches = RoonSpawn::journalLines();

    ASSERT_FALSE(launches.empty())
        << "calaos_roon was never spawned at all - the harness is broken, not "
           "the code under test";
    ASSERT_TRUE(respawned)
        << "the sidecar was spawned " << launches.size()
        << " time(s) in 5s: the respawn never happened, so this case cannot "
           "say anything about the arguments it carries";

    const std::string tail = "--namespace roon --host 192.168.7.42 --port 9331";

    for (std::vector<std::string>::size_type i = 0; i < launches.size(); i++)
    {
        const std::string &argv = launches[i];
        const bool ok = argv.size() >= tail.size() &&
                        argv.compare(argv.size() - tail.size(),
                                     tail.size(), tail) == 0;

        EXPECT_TRUE(ok)
            << "launch #" << (i + 1) << " of " << launches.size()
            << " did not hand calaos_roon the core this IO was configured "
               "with. A port nobody configured is the defect T3.28 fixed, one "
               "call site further on.\n"
            << "  argv            : " << argv << "\n"
            << "  must end with   : " << tail;
    }
}

/*
 * Own main instead of gtest_main, same reason as tests/core/KnxIo_test.cpp and
 * tests/core/RoonArgs_test.cpp: skip the static destructors.
 *
 * The RoonCtrl singleton is a function-local static shared_ptr. Destroying it
 * at process exit runs ~ExternProcServer, which does
 * process_exe->kill(SIGTERM) on the last spawned child
 * (IO/ExternProc.cpp:101-106) and closes libuv handles on a loop this binary
 * has stopped pumping. KnxIo_test documents what that class of teardown
 * already cost once - a SIGTERM to the whole process group taking down the
 * automake harness with Error 143 AFTER the tests had passed.
 *
 * ⚠️ _exit() skips destructors, so RoonSpawn::teardown() is what gives back the
 * sandbox directory, the unix socket ~ExternProcServer never unlinked, and the
 * last unreaped child. See core/RoonSpawnHarness.h.
 *
 * (An object file's main always wins over the one in libgtest_main.)
 */
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    CalaosTest::RoonSpawn::teardown();

    fflush(nullptr);
    _exit(ret);
}
