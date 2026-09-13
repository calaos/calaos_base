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
 * THE ONE RELAUNCH RAMP AN OWNER KEEPS FOR ITSELF, AND THE COUNT IT READS.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT
 * ---------------------------------------------------------------------------
 * WagoMap holds its own relaunch hold on top of the transport's, and used to
 * feed it a failure counter of its own, cleared by processConnected. That
 * signal is the IPC SOCKET, not the PLC: calaos_wago's setup() calls
 * connectSocket() as its very first instruction, so every launch that got as
 * far as running the binary cleared the counter. Two consequences, one cause:
 * the hold stayed on its first step forever, and the operator alarm - which
 * only fires on a multiple of RESPAWN_LOG_EVERY - never fired once, because
 * the number it tests never left 1.
 *
 * The counter that does climb is the transport's: the exit status and the
 * signal arrive there, and nothing an IPC connection does touches it.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS MEASURED, AND IN WHICH DIRECTION IT IS BOUND
 * ---------------------------------------------------------------------------
 * The stand-in CONNECTS to the IPC socket before it dies, which is the whole
 * point: without that connection this suite would be measuring the missing
 * binary, the one case where the ramp already climbed. The launch instants are
 * stamped BY THE CHILD, one line per launch, and the case reads the GAPS
 * between them.
 *
 * Every gap is bounded from BELOW, against a floor read from the ramp itself.
 * Load can only make a hold end later, never sooner, so a machine under load
 * can fail no bound here; what it can do is hide a flat ramp, which is why the
 * floors are checked to sit above what a flat ramp produces before they are
 * used.
 *
 * No case states a delay of its own, and none reads a launches-per-minute
 * figure: that is a wall clock reading and worthless on a shared machine.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS NOT MEASURED HERE, AND WHY
 * ---------------------------------------------------------------------------
 * The alarm is not driven end to end. Reaching RESPAWN_LOG_EVERY failures
 * costs the sum of both ramps - about a minute and a half - so the rule is
 * pinned as a function and its INPUT is pinned by the gap case, which is the
 * same number. A suite that waited for the alarm would hold `make check` for
 * longer than every other suite put together.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "ExternProc.h"
#include "WagoMap.h"
#include "core/CalaosCoreFixture.h"
#include "core/ExternProcSpawnHarness.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

//TEST-NET-1 (RFC 5737): syntactically valid and never local, so the UDP bind
//WagoMap does in its constructor cannot collide with a parallel `make check`
//peer over the fixed WAGO_LISTEN_PORT. A different address from the one
//core/SidecarArgv_test uses, for the same reason its two WagoMaps differ.
const char *const kWagoHost = "192.0.2.63";
const int kWagoPort = 5031;

//What the stand-in answers. Non-zero, so the transport counts the exit as a
//failure - a zero there is a voluntary stop and would keep its ramp at rest.
const int kStandInExitCode = 3;

//Four launches give three gaps; the second and the third are the ones that
//separate a ramp that climbs from one stuck on its first step. Nominally that
//is about seven seconds of holds, and under the defect about four.
const std::size_t kLaunchesNeeded = 4;
const int kPumpBudgetMs = 30000;

//What the recorder writes when it was handed something other than the
//--socket pair it expects, so a case cannot read a missing connection as a
//refused one.
const char *const kConnected = "connected";
const char *const kBadArgv = "badargv";

struct Launch
{
    long long stampNs = 0;
    std::string verdict;
    std::string sockpath;
};

std::string journalOf(const std::string &binName)
{
    return ExternProcSpawn::journalPath(binName);
}

/*
 * A stand-in that does what calaos_wago does before anything else: connect to
 * the unix socket handed to it on --socket. It then dies without ever speaking
 * to a PLC, which is the sidecar that cannot reach its automate.
 *
 * ONE LINE PER LAUNCH, WRITTEN ONCE, by a single printf into a file opened
 * O_APPEND - same invariant as core/ExternProcSpawnHarness.h, and needed here
 * too because the connection attempt happens between the stamp and the write.
 * The stamp is taken FIRST so a gap measures the distance between two launches
 * and not between two connection attempts.
 *
 * The verdict of the connection is recorded rather than assumed: a stand-in
 * that failed to connect would turn this suite into the missing-binary case,
 * where the counter already climbed and there is nothing left to find.
 */
bool installConnectingStandIn(const std::string &binName)
{
    if (ExternProcSpawn::sandboxDir().empty())
        return false;

    const std::string script = ExternProcSpawn::sandboxDir() + "/" + binName;

    {
        std::ofstream f(script.c_str(), std::ios::out | std::ios::trunc);
        if (!f.is_open())
            return false;

        f << "#!/bin/sh\n"
             "sep=$(printf '\\037')\n"
             "ts=$(date +%s%N)\n"
             "verdict=" << kBadArgv << "\n"
             "if [ \"$1\" = \"--socket\" ] && [ -n \"$2\" ]; then\n"
             "  verdict=noconnect\n"
             "  if perl -MIO::Socket::UNIX -e "
             "'exit(IO::Socket::UNIX->new(Peer => $ARGV[0]) ? 0 : 1)' "
             "\"$2\" 2>/dev/null; then verdict=" << kConnected << "; fi\n"
             "fi\n"
             "printf '%s%s%s%s%s%s\\n' \"$sep\" \"$ts\" \"$sep\" \"$verdict\" "
             "\"$sep\" \"$2\" >> '" << journalOf(binName) << "'\n"
             "exit " << kStandInExitCode << "\n";

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

std::vector<Launch> readLaunches(const std::string &binName)
{
    std::vector<Launch> out;
    std::ifstream f(journalOf(binName).c_str());
    std::string line;

    while (std::getline(f, line))
    {
        if (line.empty())
            continue;

        std::vector<std::string> fields;
        std::string::size_type pos = 0;

        //The record opens with the separator, so the leading empty field is
        //not a field of its own.
        while (pos < line.size() && line[pos] == ExternProcSpawn::kArgSep)
        {
            const std::string::size_type next =
                line.find(ExternProcSpawn::kArgSep, pos + 1);
            fields.push_back(line.substr(pos + 1,
                                         next == std::string::npos?
                                             std::string::npos : next - pos - 1));
            pos = (next == std::string::npos)? line.size() : next;
        }

        Launch l;
        if (fields.size() > 0)
            l.stampNs = ::atoll(fields[0].c_str());
        if (fields.size() > 1)
            l.verdict = fields[1];
        if (fields.size() > 2)
            l.sockpath = fields[2];

        out.push_back(l);
    }

    return out;
}

double gapSeconds(const std::vector<Launch> &l, std::size_t n)
{
    if (n == 0 || n >= l.size())
        return -1.0;
    return static_cast<double>(l[n].stampNs - l[n - 1].stampNs) / 1e9;
}

//The binary WagoMap launches, and the one this suite shadows.
const char *const kWagoBin = "calaos_wago";

/*
 * Built once and leaked, like every other suite that drives a real controller:
 * WagoMap keeps respawning for the life of the process and parks timers and
 * libuv handles on the default loop, and the journal must outlive a case.
 */
void buildWagoMap()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    WagoMap::Instance(kWagoHost, kWagoPort);
}

class WagoRespawnRampTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();

        ASSERT_TRUE(installConnectingStandIn(kWagoBin))
            << "could not set up the CALAOS_BIN_PREFIX sandbox";

        buildWagoMap();

        //Pumped once for the whole suite: the holds are cumulative and a
        //second case cannot ask the ramp to start over.
        static bool pumped = false;
        if (!pumped)
        {
            pumped = true;
            ExternProcSpawn::runLoopUntil([]()
            {
                return readLaunches(kWagoBin).size() >= kLaunchesNeeded;
            }, kPumpBudgetMs);
        }
    }
};

} //namespace

/******************************************************************************
 * THE PREMISE OF EVERYTHING BELOW: EVERY LAUNCH RECONNECTS.
 *
 * This is the sentence the defect rests on - "calaos_wago connects before it
 * does anything else, so every launch cleared the counter". A stand-in that
 * did not connect would put this suite in the missing-binary case, where the
 * ramp climbed even before the fix, and every measurement after it would be
 * green for a reason that has nothing to do with the code.
 ******************************************************************************/

TEST_F(WagoRespawnRampTest, EveryLaunchReconnectsTheIpcSocketBeforeItDies)
{
    const std::vector<Launch> launches = readLaunches(kWagoBin);

    ASSERT_GE(launches.size(), kLaunchesNeeded)
        << "the sidecar was launched " << launches.size() << " time(s) in "
        << kPumpBudgetMs << " ms: with fewer than " << kLaunchesNeeded
        << " there are no two holds to compare, and a suite that measured "
           "nothing would read like a suite that found nothing";

    for (std::size_t i = 0; i < launches.size(); i++)
    {
        EXPECT_EQ(std::string(kConnected), launches[i].verdict)
            << "launch #" << (i + 1) << " of " << launches.size()
            << " did not reach the IPC socket (" << launches[i].verdict
            << "): this suite is then measuring a sidecar that never started, "
               "which is the one case the ramp always handled";
        EXPECT_FALSE(launches[i].sockpath.empty())
            << "launch #" << (i + 1) << " carries no socket path";
        EXPECT_GT(launches[i].stampNs, 0)
            << "launch #" << (i + 1) << " carries no stamp, so no gap that "
               "ends on it means anything";
    }
}

/******************************************************************************
 * THE RAMP CLIMBS ALTHOUGH THE IPC SOCKET IS RECONNECTED EVERY TIME.
 ******************************************************************************/

TEST_F(WagoRespawnRampTest, TheHoldGrowsAcrossLaunchesThatAllReconnected)
{
    /*
     * THE POSITIVE CONTROL, FIRST. A floor that a flat ramp already clears
     * measures nothing at all. The flat ramp holds respawnDelay(0) before
     * every launch, and the transport adds its own hold and the 100 ms the
     * exit signal is deferred by, so the floor has to sit above that sum or
     * this case would pass on the very shape it exists to reject.
     */
    const double kExitSignalDeferral = 0.1;
    const double flatSecondGap = WagoMap::respawnDelay(0) +
                                 ExternProcServer::respawnDelay(2) +
                                 kExitSignalDeferral;
    const double flatThirdGap = WagoMap::respawnDelay(0) +
                                ExternProcServer::respawnDelay(3) +
                                kExitSignalDeferral;

    ASSERT_GT(WagoMap::respawnDelay(1), flatSecondGap)
        << "the second step of the ramp is not above what a ramp stuck on its "
           "first step produces, so the floor below is met either way";
    ASSERT_GT(WagoMap::respawnDelay(2), flatThirdGap)
        << "the third step of the ramp is not above what a ramp stuck on its "
           "first step produces, so the floor below is met either way";

    const std::vector<Launch> launches = readLaunches(kWagoBin);
    ASSERT_GE(launches.size(), kLaunchesNeeded)
        << "only " << launches.size() << " launch(es) were recorded, so the "
           "gaps this case reads do not exist";

    const double second = gapSeconds(launches, 2);
    const double third = gapSeconds(launches, 3);

    /*
     * Bounded from BELOW against the ramp's own steps: load can only make a
     * hold end later. The hold before launch N follows N-1 counted failures,
     * so it is respawnDelay(N-2).
     */
    EXPECT_GE(second, WagoMap::respawnDelay(1))
        << "the second hold lasted " << second << " s, under the "
        << WagoMap::respawnDelay(1) << " s the ramp's second step asks for: "
           "the count the hold reads was cleared by the IPC connection, so "
           "the ramp never leaves its first step";
    EXPECT_GE(third, WagoMap::respawnDelay(2))
        << "the third hold lasted " << third << " s, under the "
        << WagoMap::respawnDelay(2) << " s the ramp's third step asks for";

    //THE COUNTERWEIGHT. A hold that never ends satisfies every floor above,
    //so a third gap has to exist at all: holding longer is not giving up.
    EXPECT_GT(third, 0.0)
        << "no third gap was measured at all: the sidecar stopped being "
           "relaunched, and slowing down was traded for giving up";
}

/******************************************************************************
 * THE RULE THE ALARM ASKS, READ WITHOUT A CLOCK.
 ******************************************************************************/

TEST_F(WagoRespawnRampTest, TheAlarmIsDueOnAMultipleAndNeverOnACountOfZero)
{
    //Zero is the trap: it IS a multiple, and it is what the transport
    //reports for an exit it read as a voluntary stop. Firing there would put
    //an error line under every ordinary shutdown of the server.
    EXPECT_FALSE(WagoMap::respawnAlarmDue(0))
        << "a count of zero is a stop the transport did not call a failure, "
           "and an alarm there fires on every clean shutdown";
    EXPECT_FALSE(WagoMap::respawnAlarmDue(-1))
        << "a negative count is not a run of failures";

    EXPECT_FALSE(WagoMap::respawnAlarmDue(1));
    EXPECT_FALSE(WagoMap::respawnAlarmDue(WagoMap::RESPAWN_LOG_EVERY - 1));
    EXPECT_TRUE(WagoMap::respawnAlarmDue(WagoMap::RESPAWN_LOG_EVERY))
        << "the alarm skipped its own period";
    EXPECT_TRUE(WagoMap::respawnAlarmDue(2 * WagoMap::RESPAWN_LOG_EVERY))
        << "the alarm fires once and never again";

    //And the period itself, in figures: written through the constant alone,
    //this case would follow a mutation of the constant instead of catching it.
    EXPECT_FALSE(WagoMap::respawnAlarmDue(9));
    EXPECT_TRUE(WagoMap::respawnAlarmDue(10));
}

/******************************************************************************
 * AND WHAT IS LEFT WHEN THE TRANSPORT COUNTED NOTHING.
 *
 * A status of zero with no signal is a voluntary stop to the transport, which
 * then holds nothing at all. calaos_wago produces one whenever its main loop
 * leaves without a select() error - a framing violation, for one - so the hold
 * WagoMap keeps is the only floor under that loop. It must survive the count
 * of zero the transport reports there.
 ******************************************************************************/

TEST_F(WagoRespawnRampTest, AHoldRemainsWhenTheTransportCountedNoFailure)
{
    EXPECT_GT(WagoMap::respawnDelay(-1), 0.0)
        << "a count of zero failures leaves no hold at all, and an exit the "
           "transport reads as voluntary is then relaunched as fast as the "
           "loop allows";
    EXPECT_DOUBLE_EQ(WagoMap::respawnDelay(-1), WagoMap::respawnDelay(0))
        << "the count the ramp is handed is one less than the failures "
           "counted, so the first step has to be what a count of zero gets";
}

/*
 * Own main instead of gtest_main, same reason as core/SidecarArgv_test:
 * destroying an ExternProcServer at process exit signals the last spawned
 * child and closes libuv handles on a loop nothing pumps anymore.
 */
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    CalaosTest::ExternProcSpawn::teardown();

    fflush(nullptr);
    _exit(ret);
}
