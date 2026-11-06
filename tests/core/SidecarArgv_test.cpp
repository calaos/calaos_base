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
 * WHAT THE WAGO, KNX, OLA AND REOLINK SIDECARS ARE LAUNCHED WITH.
 *
 * ---------------------------------------------------------------------------
 * WHY THESE FOUR AND NOT THE OTHER TWO
 * ---------------------------------------------------------------------------
 * The six sidecars that carry a configuration argument had their command line
 * converted from a re-split string to a vector of arguments. MQTT, OneWire and
 * Roon came out of that with a case reading the argv the kernel handed the
 * child; these four came out with nothing at all. Worse for two of them: no
 * test binary linked IO/OLA/OLACtrl.o or IO/Reolink/ReolinkCtrl.o, so no
 * mutation of either could ever have been red - the gap was structural, not an
 * oversight of coverage.
 *
 * ---------------------------------------------------------------------------
 * WHY EVERY CASE STARTS FROM A REAL CONTROLLER
 * ---------------------------------------------------------------------------
 * Nothing here calls startProcess(). A guard exercised by direct call and
 * never wired to the site that ships it is a green suite over a live defect,
 * and that is what happened six times in a row in this series. Each fixture
 * builds the production controller - WagoMap, KNXCtrl, OLACtrl, ReolinkCtrl -
 * lets its respawn run, and reads back the argv the KERNEL gave the child.
 *
 * ---------------------------------------------------------------------------
 * WHAT EACH SIDECAR ACTUALLY ASKS FOR
 * ---------------------------------------------------------------------------
 * The expectations below are the sidecars' own reading code, not a restatement
 * of the assembling code:
 *
 *   calaos_wago     `if (argc > 1) wago_host = argv[1];`
 *                   `if (argc > 2) Utils::from_string(argv[2], wago_port);`
 *                   Two POSITIONALS, host first. Swapping them is silent.
 *   calaos_knx      `argvOptionParam(argv, argv + argc, "--server")`, which
 *                   compares a WHOLE argv to "--server" and returns the NEXT
 *                   one; `argvOptionCheck(..., "--internal-monitor-bus")`,
 *                   which compares a whole argv too. A flag glued to its value
 *                   matches neither.
 *   calaos_ola      `if (argc > 1) Utils::from_string(argv[1], universe);`
 *                   with `unsigned int universe = 0;` as the default. An empty
 *                   argument is not "no argument": it parses as 0 only by
 *                   accident of from_string(), and it hides the default.
 *   calaos_reolink  argparse with EXACTLY --socket and --namespace declared.
 *                   argparse rejects any unknown option and any positional, so
 *                   one extra argument makes the sidecar exit(2) forever.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS DOES NOT PROVE
 * ---------------------------------------------------------------------------
 * That any of these sidecars does something correct with the argv it gets. The
 * recorders are stand-ins: no KNX bus, no Wago PLC, no camera and no olad take
 * part, and the arguments are never parsed by the real programs.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <unistd.h>

#include <map>
#include <string>
#include <vector>

#include "CalaosCoreFixture.h"
#include "ExternProcSpawnHarness.h"
#include "KNXCtrl.h"
#include "OLACtrl.h"
#include "ReolinkCtrl.h"
#include "StringUtils.h"
#include "WagoMap.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

typedef std::vector<std::string> Argv;
typedef std::map<Argv, int> ShapeCounts;

/*
 * The five argv startProcess() emits before anything a caller adds: the
 * executable, --socket <path it picks itself>, --namespace <name>.
 */
const std::size_t kFixedArgc = 5;

//How long a fixture waits for a controller to launch its sidecar and relaunch
//it. WagoMap ramps its respawn backoff from one second, which is the slowest
//of the four; the others relaunch immediately.
const int kRespawnBudgetMs = 8000;

Argv tailOf(const Argv &argv)
{
    if (argv.size() < kFixedArgc)
        return Argv();
    return Argv(argv.begin() + kFixedArgc, argv.end());
}

//Every argument tail the sidecar was launched with, and how many times each.
//The tail is what a controller chose; the head is startProcess()'s own.
ShapeCounts shapesOf(const std::string &binName)
{
    ShapeCounts out;
    const std::vector<Argv> spawns = ExternProcSpawn::launches(binName);
    for (std::vector<Argv>::const_iterator it = spawns.begin(); it != spawns.end(); ++it)
        out[tailOf(*it)]++;
    return out;
}

std::string render(const Argv &argv)
{
    if (argv.empty())
        return "<no argument>";

    std::string s;
    for (Argv::const_iterator it = argv.begin(); it != argv.end(); ++it)
        s += "<" + *it + ">";
    return s;
}

std::string render(const ShapeCounts &shapes)
{
    std::string s;
    for (ShapeCounts::const_iterator it = shapes.begin(); it != shapes.end(); ++it)
        s += render(it->first) + " x" + Utils::to_string(it->second) + " ";
    return s.empty()? "<nothing was ever launched>" : s;
}

bool tailCarries(const Argv &tail, const std::string &field)
{
    for (Argv::const_iterator it = tail.begin(); it != tail.end(); ++it)
        if (*it == field)
            return true;
    return false;
}

//The launches whose tail carries `field`, used where one journal records two
//controllers and the field is what tells them apart.
std::vector<Argv> launchesCarrying(const std::string &binName, const std::string &field)
{
    std::vector<Argv> out;
    const std::vector<Argv> spawns = ExternProcSpawn::launches(binName);
    for (std::vector<Argv>::const_iterator it = spawns.begin(); it != spawns.end(); ++it)
        if (tailCarries(tailOf(*it), field))
            out.push_back(*it);
    return out;
}

/*
 * The recorder writes one line per launch and cuts fields on US: an argument
 * carrying either byte would corrupt the journal instead of failing a case.
 */
::testing::AssertionResult recordable(const std::string &s, const char *what)
{
    if (s.find('\n') != std::string::npos)
        return ::testing::AssertionFailure() << what << " carries a newline";
    if (s.find(ExternProcSpawn::kArgSep) != std::string::npos)
        return ::testing::AssertionFailure() << what << " carries the field separator";
    return ::testing::AssertionSuccess();
}

//What startProcess() puts in front, checked on every launch a case reads: a
//namespace landing elsewhere is exactly the accident these cases exist for.
::testing::AssertionResult headIsFor(const Argv &argv, const std::string &name)
{
    if (argv.size() < kFixedArgc)
        return ::testing::AssertionFailure()
               << "argv has only " << argv.size() << " fields: " << render(argv);
    if (argv[1] != "--socket")
        return ::testing::AssertionFailure() << "argv[1] is <" << argv[1] << ">, not --socket";
    if (argv[3] != "--namespace")
        return ::testing::AssertionFailure() << "argv[3] is <" << argv[3] << ">, not --namespace";
    if (argv[4] != name)
        return ::testing::AssertionFailure()
               << "the namespace is <" << argv[4] << ">, not <" << name << ">";
    return ::testing::AssertionSuccess();
}

//No shape outside `allowed` was ever launched. A case that only looked for the
//shape it wants would stay green next to a second, wrong one.
::testing::AssertionResult noShapeOutside(const ShapeCounts &shapes,
                                          const std::vector<Argv> &allowed)
{
    for (ShapeCounts::const_iterator it = shapes.begin(); it != shapes.end(); ++it)
    {
        bool ok = false;
        for (std::vector<Argv>::const_iterator a = allowed.begin(); a != allowed.end(); ++a)
            ok = ok || (*a == it->first);

        if (!ok)
            return ::testing::AssertionFailure()
                   << "an unexpected argument list was sent: " << render(it->first)
                   << " (all of them: " << render(shapes) << ")";
    }
    return ::testing::AssertionSuccess();
}

/*******************************************************************************
 * The fixtures, and why the controllers are leaked.
 *
 * All four keep respawning their sidecar for the life of the process and park
 * timers and libuv handles on the default loop; destroying one while the loop
 * can still run its respawn is a use-after-free. Production keeps them for the
 * life of the server anyway, and the journals must outlive a single case.
 ******************************************************************************/

//TEST-NET-1 (RFC 5737): syntactically valid, never local, so the UDP bind
//WagoMap does in its constructor cannot collide with a parallel `make check`
//peer over the fixed WAGO_LISTEN_PORT.
const char *const kWagoHost = "192.0.2.61";

//Two Modbus ports, one per WagoMap, because both write to the same journal:
//the port is what attributes a line to the controller that produced it, so the
//two must never be equal.
const char *const kWagoConfiguredPort = "5021";
const char *const kWagoUnsetHostPort = "5022";

const char *const kKnxHost = "192.0.2.71";

//Not 0: that is calaos_ola's own default, and a fixture sitting on it could
//not tell a universe that crossed from one that never did.
const char *const kOlaUniverse = "7";

void buildWagoMaps()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    int port = 0;
    Utils::from_string(kWagoConfiguredPort, port);
    WagoMap::Instance(kWagoHost, port);

    Utils::from_string(kWagoUnsetHostPort, port);
    WagoMap::Instance("", port);
}

void buildOlaCtrls()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    OLACtrl::Instance(kOlaUniverse);
    OLACtrl::Instance("");
}

} // namespace

/*******************************************************************************
 * WAGO - two positionals, host then port.
 ******************************************************************************/

class WagoSidecarArgvTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();

        ASSERT_TRUE(ExternProcSpawn::install("calaos_wago"))
            << "could not set up the CALAOS_BIN_PREFIX sandbox";

        ASSERT_STRNE(kWagoConfiguredPort, kWagoUnsetHostPort)
            << "both WagoMaps write to the same journal and the port is what "
               "tells their launches apart";
        ASSERT_STRNE("", kWagoHost)
            << "the configured host is empty, so this fixture cannot tell the "
               "configured case from the unset one";
        ASSERT_TRUE(recordable(kWagoHost, "the Wago host"));

        buildWagoMaps();

        const bool respawned = ExternProcSpawn::runLoopUntil([]()
        {
            return launchesCarrying("calaos_wago", kWagoConfiguredPort).size() >= 2 &&
                   launchesCarrying("calaos_wago", kWagoUnsetHostPort).size() >= 2;
        }, kRespawnBudgetMs);

        ASSERT_TRUE(respawned)
            << "one of the two WagoMaps did not launch and relaunch its sidecar: "
            << render(shapesOf("calaos_wago"))
            << ". A case reading an empty journal would be green for no reason.";
    }
};

/*
 * Swapping the two positionals hands the PORT to the sidecar where it reads
 * the host and the host where it reads the port: Wago is entirely out of
 * service and every argument is still well formed.
 */
TEST_F(WagoSidecarArgvTest, TheHostAndThePortCrossAsTwoPositionalsInThatOrder)
{
    const std::vector<Argv> spawns = launchesCarrying("calaos_wago", kWagoConfiguredPort);
    const Argv expected = { kWagoHost, kWagoConfiguredPort };

    for (std::size_t i = 0; i < spawns.size(); i++)
    {
        EXPECT_TRUE(headIsFor(spawns[i], "wago")) << "launch #" << (i + 1);
        EXPECT_EQ(expected, tailOf(spawns[i]))
            << "launch #" << (i + 1) << " of " << spawns.size()
            << ": calaos_wago reads argv[1] as the host and argv[2] as the "
               "port, so it got " << render(tailOf(spawns[i]));
    }
}

/*
 * An unset host is dropped from the argument list rather than sent empty,
 * which leaves the PORT in argv[1] - where the sidecar looks for the host.
 * Pinned as it stands so that changing it is a decision and not an accident:
 * the reproduction of this behaviour was deliberate.
 */
TEST_F(WagoSidecarArgvTest, AnUnsetHostLeavesThePortAloneWhereTheSidecarReadsTheHost)
{
    const std::vector<Argv> spawns = launchesCarrying("calaos_wago", kWagoUnsetHostPort);
    const Argv expected = { kWagoUnsetHostPort };

    for (std::size_t i = 0; i < spawns.size(); i++)
    {
        EXPECT_TRUE(headIsFor(spawns[i], "wago")) << "launch #" << (i + 1);
        EXPECT_EQ(expected, tailOf(spawns[i]))
            << "launch #" << (i + 1) << " of " << spawns.size()
            << ": the unset host used to vanish in the re-split, leaving the "
               "port alone; it got " << render(tailOf(spawns[i]));
    }
}

/*******************************************************************************
 * KNX - a flag and its value are two argv, and the monitor flag stands alone.
 ******************************************************************************/

class KnxSidecarArgvTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();

        ASSERT_TRUE(ExternProcSpawn::install("calaos_knx"))
            << "could not set up the CALAOS_BIN_PREFIX sandbox";
        ASSERT_TRUE(recordable(kKnxHost, "the KNX host"));

        KNXCtrl::Instance(kKnxHost);

        //One KNXCtrl runs TWO sidecars, both named knx and both writing here,
        //so four launches is the least that says both have relaunched.
        const bool respawned = ExternProcSpawn::runLoopUntil([]()
        {
            return ExternProcSpawn::launches("calaos_knx").size() >= 4 &&
                   shapesOf("calaos_knx").size() >= 2;
        }, kRespawnBudgetMs);

        ASSERT_TRUE(respawned)
            << "the command and monitor sidecars did not both launch and "
               "relaunch: " << render(shapesOf("calaos_knx"));
    }
};

/*
 * argvOptionParam() compares a whole argv to "--server" and answers the NEXT
 * one. Glued into "--server ip:h" the option is simply not there, the sidecar
 * falls back to its own default server, and nothing in the launch looks wrong.
 */
TEST_F(KnxSidecarArgvTest, TheServerFlagAndItsAddressCrossAsTwoSeparateArguments)
{
    const ShapeCounts shapes = shapesOf("calaos_knx");
    const Argv expected = { "--server", std::string("ip:") + kKnxHost };

    ASSERT_TRUE(shapes.find(expected) != shapes.end())
        << "the command sidecar was never launched with " << render(expected)
        << "; what was sent: " << render(shapes);
    EXPECT_GE(shapes.find(expected)->second, 2)
        << "the command sidecar carried that pair on its first launch only";
}

/*
 * The monitor flag is read by argvOptionCheck(), which also compares a whole
 * argv: appended to the previous argument it never arms the monitor mode, and
 * the two sidecars silently become the same one.
 */
TEST_F(KnxSidecarArgvTest, TheMonitorFlagCrossesAloneAheadOfTheServerPair)
{
    const ShapeCounts shapes = shapesOf("calaos_knx");
    const Argv command = { "--server", std::string("ip:") + kKnxHost };
    const Argv monitor = { "--internal-monitor-bus", "--server", std::string("ip:") + kKnxHost };

    ASSERT_TRUE(shapes.find(monitor) != shapes.end())
        << "the monitor sidecar was never launched with " << render(monitor)
        << "; what was sent: " << render(shapes);
    EXPECT_GE(shapes.find(monitor)->second, 2)
        << "the monitor sidecar carried its flag on its first launch only";

    const std::vector<Argv> allowed = { command, monitor };
    EXPECT_TRUE(noShapeOutside(shapes, allowed));

    const std::vector<Argv> spawns = ExternProcSpawn::launches("calaos_knx");
    for (std::size_t i = 0; i < spawns.size(); i++)
        EXPECT_TRUE(headIsFor(spawns[i], "knx")) << "launch #" << (i + 1);
}

/*******************************************************************************
 * OLA - one argument, or none at all.
 ******************************************************************************/

class OlaSidecarArgvTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();

        ASSERT_TRUE(ExternProcSpawn::install("calaos_ola"))
            << "could not set up the CALAOS_BIN_PREFIX sandbox";
        ASSERT_TRUE(recordable(kOlaUniverse, "the OLA universe"));

        buildOlaCtrls();

        //Both OLACtrls write here and the unset one sends nothing, so there is
        //no field to attribute a line by: what says both are alive is that two
        //different argument lists have been seen, four launches in.
        const bool respawned = ExternProcSpawn::runLoopUntil([]()
        {
            return ExternProcSpawn::launches("calaos_ola").size() >= 4 &&
                   shapesOf("calaos_ola").size() >= 2;
        }, kRespawnBudgetMs);

        ASSERT_TRUE(respawned)
            << "the two OLACtrls did not both launch and relaunch: "
            << render(shapesOf("calaos_ola"));
    }
};

TEST_F(OlaSidecarArgvTest, AConfiguredUniverseCrossesAsTheOnlyArgument)
{
    const ShapeCounts shapes = shapesOf("calaos_ola");
    const Argv expected = { kOlaUniverse };

    ASSERT_TRUE(shapes.find(expected) != shapes.end())
        << "no launch carried the configured universe alone; what was sent: "
        << render(shapes);
    EXPECT_GE(shapes.find(expected)->second, 2)
        << "the configured universe crossed on the first launch only";

    const std::vector<Argv> allowed = { Argv(), expected };
    EXPECT_TRUE(noShapeOutside(shapes, allowed));
}

/*
 * An unset universe sends NO argument, not an empty one. calaos_ola guards its
 * read with `argc > 1`, so an empty argv[1] replaces its default by whatever
 * from_string() leaves in the variable - a difference nothing downstream can
 * see, and the reason the empty argument must not appear.
 */
TEST_F(OlaSidecarArgvTest, AnUnsetUniverseSendsNoArgumentAtAll)
{
    const ShapeCounts shapes = shapesOf("calaos_ola");

    ASSERT_TRUE(shapes.find(Argv()) != shapes.end())
        << "every launch carried an argument, so the unset universe was sent "
           "as one; what was sent: " << render(shapes);
    EXPECT_GE(shapes.find(Argv())->second, 2)
        << "the argument-less launch happened once and the respawn added one";

    const std::vector<Argv> allowed = { Argv(), Argv(1, kOlaUniverse) };
    EXPECT_TRUE(noShapeOutside(shapes, allowed));

    const std::vector<Argv> spawns = ExternProcSpawn::launches("calaos_ola");
    for (std::size_t i = 0; i < spawns.size(); i++)
        EXPECT_TRUE(headIsFor(spawns[i], "ola")) << "launch #" << (i + 1);
}

/*******************************************************************************
 * REOLINK - nothing after the namespace.
 ******************************************************************************/

class ReolinkSidecarArgvTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();

        ASSERT_TRUE(ExternProcSpawn::install("calaos_reolink"))
            << "could not set up the CALAOS_BIN_PREFIX sandbox";

        ReolinkCtrl::Instance();

        const bool respawned = ExternProcSpawn::runLoopUntil([]()
        {
            return ExternProcSpawn::launches("calaos_reolink").size() >= 2;
        }, kRespawnBudgetMs);

        ASSERT_TRUE(respawned)
            << "calaos_reolink was launched "
            << ExternProcSpawn::launches("calaos_reolink").size()
            << " time(s): without the respawn this suite says nothing about "
               "what the second launch carries";
    }
};

/*
 * The python sidecar declares --socket and --namespace and nothing else.
 * argparse rejects any unknown option and any positional, so ONE extra
 * argument makes it exit(2) on every launch, forever - the credentials it
 * needs travel over the socket, not here, and there is nothing left to add.
 */
TEST_F(ReolinkSidecarArgvTest, NothingIsSentAfterTheNamespace)
{
    const std::vector<Argv> spawns = ExternProcSpawn::launches("calaos_reolink");

    for (std::size_t i = 0; i < spawns.size(); i++)
    {
        EXPECT_TRUE(headIsFor(spawns[i], "reolink")) << "launch #" << (i + 1);
        EXPECT_EQ(Argv(), tailOf(spawns[i]))
            << "launch #" << (i + 1) << " of " << spawns.size()
            << ": argparse would reject " << render(tailOf(spawns[i]));
        EXPECT_EQ(kFixedArgc, spawns[i].size())
            << "launch #" << (i + 1) << ": " << render(spawns[i]);
    }
}

/*
 * Own main instead of gtest_main, same reason as core/ExternProcArgv_test:
 * destroying an ExternProcServer at process exit sends SIGTERM to the last
 * spawned child and closes libuv handles on a loop this binary has stopped
 * pumping, which once took the automake harness down with Error 143 after the
 * tests had passed. _exit() skips that, and teardown() gives back the sandbox,
 * the unix sockets and the children.
 */
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    CalaosTest::ExternProcSpawn::teardown();

    fflush(nullptr);
    _exit(ret);
}
