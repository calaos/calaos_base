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
 * ⭐⭐ WHAT A SIDECAR LAUNCH WRITES TO THE JOURNAL, ON A STOCK INSTALL.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT
 * ---------------------------------------------------------------------------
 * ExternProcServer::startProcess() logged the whole argv, re-joined by spaces,
 * through cInfoDom("process"). `debug_level` defaults to 4, LOG_LEVEL_INFO is
 * 4, and the filter is `level > maxLevelPrintable` - so INFO prints with
 * nothing switched on. MqttWire::encodeConfig() puts the broker `user` and
 * `password` in the JSON that IS that argument. Every launch and every respawn
 * of calaos_mqtt therefore wrote the broker password in clear, and seven
 * controllers respawn without backoff, so an unreachable broker rewrote the
 * line about ten times a second.
 *
 * A journal travels - it leaves in a support bundle, a screenshot, a public
 * bug report - which is what separates this from "whoever reads it is already
 * on the box".
 *
 * ---------------------------------------------------------------------------
 * ⚠️ WHY THIS GOES THROUGH A REAL CONTROLLER
 * ---------------------------------------------------------------------------
 * Nothing here calls startProcess(). A real MqttCtrl is built from a real
 * Params and std::cout is captured across its first launch AND its respawn, so
 * what is asserted is what the shipped path writes. Exchanging the line in
 * IO/ExternProc.cpp back for its previous form turns the first case red.
 *
 * ---------------------------------------------------------------------------
 * ⛔ THE HALF THAT MUST NOT DIE WITH THE SECRET
 * ---------------------------------------------------------------------------
 * Deleting the line would close the leak and blind the diagnosis; the project
 * has just spent a night repairing sidecars nobody could see die. The second
 * case is the counterweight: the journal must still name the executable and
 * the namespace, and carry one line per launch so relaunches stay countable.
 * Removing what is left turns THAT case red and leaves the first one green.
 *
 * ⛔ WHAT THIS DOES NOT PROVE: no broker is involved, and the sidecar's own
 * two logging sites cannot be executed from here - calaos_mqtt is a separate
 * binary and dies on connectSocket() long before it reaches them. They are
 * held by a source tripwire, with the weakness that implies.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <unistd.h>

#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "CalaosCoreFixture.h"
#include "ExternProcSpawnHarness.h"
#include "LogSetup.h"
#include "Logger.h"
#include "MqttCtrl.h"
#include "MqttWire.h"
#include "Params.h"
#include "libuvw.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

#ifndef CALAOS_TOP_SRCDIR
#error "CALAOS_TOP_SRCDIR must be passed by the build (see tests/Makefile.am)"
#endif

bool readShippedSource(const std::string &relative, std::string &out)
{
    const std::string path = std::string(CALAOS_TOP_SRCDIR) + "/" + relative;
    std::ifstream f(path.c_str(), std::ios::in | std::ios::binary);
    if (!f.is_open())
        return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

int countOccurrences(const std::string &haystack, const std::string &needle)
{
    int n = 0;
    for (std::string::size_type p = haystack.find(needle);
         p != std::string::npos;
         p = haystack.find(needle, p + needle.size()))
        n++;
    return n;
}

//Collapse runs of whitespace so a tripwire matches any layout of the same
//tokens instead of going red on a re-indent. Same helper, same reason, as
//core/ExternProcArgv_test.cpp.
std::string collapseWhitespace(const std::string &src)
{
    std::string out;
    out.reserve(src.size());

    bool inRun = false;
    for (std::string::size_type i = 0; i < src.size(); i++)
    {
        const unsigned char c = static_cast<unsigned char>(src[i]);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')
        {
            if (!inRun) out += ' ';
            inRun = true;
        }
        else
        {
            out += src[i];
            inRun = false;
        }
    }

    return out;
}

std::string captureStdout(const std::function<void()> &fn)
{
    std::ostringstream sink;
    std::streambuf *saved = std::cout.rdbuf(sink.rdbuf());
    fn();
    std::cout.rdbuf(saved);
    return sink.str();
}

/*
 * ⚠️ FIXTURE, NOT DECORATION.
 *
 * Both credentials are values no other string of this binary can contain by
 * accident: "calaos" alone would also match the `calaos_mqtt` in the command
 * line and make a leak look like a pass. encodeConfig() emits `user` and
 * `password` only when BOTH keys exist, so the cases assert the encoded
 * configuration really carries them before spawning anything - otherwise a
 * green would mean "the field was dropped", not "the field was withheld".
 */
const char *const kMqttUser     = "courtier-utilisateur";
const char *const kMqttPassword = "mon mot de passe";

Params mqttParams()
{
    Params p;
    p.Add("host", "192.168.7.42");
    p.Add("port", "1884");
    p.Add("keepalive", "45");
    p.Add("user", kMqttUser);
    p.Add("password", kMqttPassword);
    return p;
}

//What one launch of calaos_mqtt wrote, observed once for the whole process.
struct Observation
{
    bool installed = false;
    bool respawned = false;
    std::string log;
    std::vector<std::vector<std::string>> spawns;
};

/*
 * The MqttCtrl is LEAKED and the observation made once.
 *
 * ~MqttCtrl does not delete its ExternProcServer and the default loop can
 * still run the 100 ms respawn timer, so destroying it is a use-after-free.
 * Capturing std::cout once and letting both cases read the same text also
 * keeps them independent of the order gtest picks.
 */
const Observation &theObservation()
{
    static Observation *obs = nullptr;
    if (obs)
        return *obs;

    obs = new Observation();
    obs->installed = ExternProcSpawn::install("calaos_mqtt");
    if (!obs->installed)
        return *obs;

    obs->log = captureStdout([&]()
    {
        Params p = mqttParams();
        new MqttCtrl(p);

        obs->respawned = ExternProcSpawn::runLoopUntil(
            []() { return ExternProcSpawn::launches("calaos_mqtt").size() >= 2; }, 5000);
    });

    obs->spawns = ExternProcSpawn::launches("calaos_mqtt");
    return *obs;
}

const char *const kLaunchMarker = "Starting process:";

} // namespace

class ExternProcLogSecretTest: public CoreFixture {};

/*
 * ⭐⭐ THE CASE THIS SUITE EXISTS FOR: the broker password does not reach the
 * journal, at the level a stock install prints.
 *
 * RED on master, verbatim, and for the right reason: the line streams the
 * whole argv re-joined by spaces and argv[5] is the broker configuration.
 *
 * ⚠️ ANTI-VACUITY FIRST. The `process` domain is asserted printable at INFO
 * and the captured text asserted non-empty before anything is looked for: a
 * quieter environment would otherwise make this case green while measuring
 * nothing. Same shape as RoonArgsTest.TheRefusedHostIsNamedInTheLog.
 */
TEST_F(ExternProcLogSecretTest, TheMqttBrokerPasswordNeverReachesTheLog)
{
    ASSERT_TRUE(Utils::calaosLogger("process")->isLevelEnabled(Logger::LOG_LEVEL_INFO))
        << "the process domain is muted below INFO here, so this case cannot "
           "observe the line it exists to check";

    const Params p = mqttParams();
    const std::string encoded = MqttWire::encodeConfig(p);
    const std::string password = kMqttPassword;
    const std::string user = kMqttUser;

    ASSERT_NE(std::string::npos, encoded.find(password))
        << "encodeConfig() did not carry the password at all, so a green here "
           "would mean nothing: " << encoded;
    ASSERT_NE(std::string::npos, encoded.find(user))
        << "encodeConfig() did not carry the user at all, same reason: " << encoded;

    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_FALSE(obs.spawns.empty())
        << "calaos_mqtt was never spawned at all - the harness is broken, not "
           "the code under test";
    ASSERT_LE(1, countOccurrences(obs.log, kLaunchMarker))
        << "nothing was logged for the launch, so this case cannot say whether "
           "a secret would have been: " << obs.log;

    EXPECT_EQ(std::string::npos, obs.log.find(password))
        << "the broker PASSWORD is in the journal of a stock install. A "
           "journal leaves the box - support bundle, screenshot, public bug "
           "report. Log: " << obs.log;
    EXPECT_EQ(std::string::npos, obs.log.find(user))
        << "the broker USER is in the journal: " << obs.log;
    EXPECT_EQ(std::string::npos, obs.log.find(encoded))
        << "the whole broker configuration is in the journal: " << obs.log;
}

/*
 * ⛔ THE COUNTERWEIGHT: deleting the line would also pass the case above.
 *
 * Whoever reads a journal must still be able to say WHICH sidecar was
 * launched, and HOW MANY times it was relaunched - the respawn without backoff
 * is the symptom that put the previous three tickets on the table. This case
 * is red exactly when the diagnosis dies, and green under the mutation that
 * restores the leak.
 */
TEST_F(ExternProcLogSecretTest, TheLogStillNamesTheSidecarAndCountsItsRelaunches)
{
    ASSERT_TRUE(Utils::calaosLogger("process")->isLevelEnabled(Logger::LOG_LEVEL_INFO))
        << "the process domain is muted below INFO here";

    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_TRUE(obs.respawned)
        << "calaos_mqtt was spawned " << obs.spawns.size()
        << " time(s) in 5s: the respawn never happened, so this case cannot "
           "say whether relaunches stay countable";

    const int announced = countOccurrences(obs.log, kLaunchMarker);

    EXPECT_LE(2, announced)
        << "the journal announces " << announced << " launch(es) for "
        << obs.spawns.size() << " recorded: a relaunch loop would be invisible. "
           "Log: " << obs.log;
    EXPECT_LE(static_cast<int>(obs.spawns.size()), announced)
        << "a launch reached the kernel without being announced. Log: " << obs.log;
    EXPECT_NE(std::string::npos, obs.log.find("calaos_mqtt"))
        << "the journal does not say WHICH sidecar was launched: " << obs.log;
    EXPECT_NE(std::string::npos, obs.log.find("--namespace mqtt"))
        << "the journal does not say which namespace the sidecar serves: "
        << obs.log;
}

/*
 * The sidecar's own two sites, held by a SPELLING and not by an effect.
 *
 * calaos_mqtt is a separate binary and returns on connectSocket() before it
 * ever reaches them, so they cannot be executed from a test process. One is a
 * cError() on the parse failure path, printed at the default level; the other
 * a cDebugDom("mqtt"), one flag away. Both streamed argv[1], which IS the
 * broker configuration.
 *
 * ⚠️ The file is asserted to still MENTION argv[1] first: a rename or a move
 * would otherwise make the count zero and the case vacuously green.
 */
TEST_F(ExternProcLogSecretTest, TripwireSource_TheMqttSidecarNeverStreamsItsConfigurationArgument)
{
    const std::string relative = "src/bin/calaos_server/IO/Mqtt/MqttExternProc_main.cpp";

    std::string raw;
    ASSERT_TRUE(readShippedSource(relative, raw))
        << "could not read " << relative;

    const std::string code = collapseWhitespace(raw);

    ASSERT_LE(1, countOccurrences(code, "argv[1]"))
        << relative << " no longer mentions argv[1] at all: this tripwire is "
           "pointing at the wrong file and proves nothing";

    EXPECT_EQ(0, countOccurrences(code, "<< argv[1]"))
        << "the sidecar streams its configuration argument to a log. argv[1] "
           "is the broker JSON, password included, and the server pipes the "
           "sidecar's stdout back into its own (IO/ExternProc.cpp).";
}

/*
 * Own main instead of gtest_main, same reason as core/ExternProcArgv_test.cpp:
 * skip the static destructors. Destroying an ExternProcServer at process exit
 * does kill(SIGTERM) on the last spawned child and closes libuv handles on a
 * loop this binary has stopped pumping.
 */
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    CalaosTest::ExternProcSpawn::teardown();

    fflush(nullptr);
    _exit(ret);
}
