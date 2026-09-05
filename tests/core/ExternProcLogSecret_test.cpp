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

/*
 * Drop comments and the inside of literals, so a tripwire that forbids a
 * SPELLING answers about the code and not about the prose around it. A
 * tripwire that goes red because someone described the defect it guards is a
 * tripwire the next maintainer disarms, and this one forbids `argv[` in the
 * very file whose comments explain why the argv is empty.
 */
std::string stripCommentsAndLiterals(const std::string &src)
{
    std::string out;
    out.reserve(src.size());

    enum { Code, Line, Block, Str, Chr } st = Code;

    for (std::string::size_type i = 0; i < src.size(); i++)
    {
        const char c = src[i];
        const char n = (i + 1 < src.size())?src[i + 1]:'\0';

        switch (st)
        {
        case Code:
            if (c == '/' && n == '/') { st = Line;  i++; }
            else if (c == '/' && n == '*') { st = Block; i++; }
            else if (c == '"')  { st = Str; out += c; }
            else if (c == '\'') { st = Chr; out += c; }
            else out += c;
            break;
        case Line:
            if (c == '\n') { st = Code; out += c; }
            break;
        case Block:
            if (c == '*' && n == '/') { st = Code; i++; out += ' '; }
            else if (c == '\n') out += c;
            break;
        case Str:
        case Chr:
            if (c == '\\') { i++; }
            else if ((st == Str && c == '"') || (st == Chr && c == '\'')) { st = Code; out += c; }
            break;
        }
    }

    return out;
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
 *
 * The announced argument COUNT is checked against what the kernel received,
 * because a count that is compared to nothing is decoration: swapping
 * args.size() for cmd.size() used to publish 6 where 1 was true, in silence.
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

    //A count nobody compares to anything is a number, not a measurement. The
    //5 is the fixed head startProcess() prepends - exe, --socket, <path>,
    //--namespace, <name> - so what the kernel received minus that head is
    //what the driver actually handed over.
    ASSERT_LE(static_cast<size_t>(5), obs.spawns[0].size())
        << "the kernel did not receive even the fixed head of the command "
           "line, so the announced count cannot be checked against anything";

    const std::string announced_count =
        "(" + std::to_string(obs.spawns[0].size() - 5) + " argument(s))";

    EXPECT_NE(std::string::npos, obs.log.find(announced_count))
        << "the journal does not announce " << announced_count << " for the "
        << obs.spawns[0].size() << " argv the kernel received: a dropped or "
           "an extra argument would leave no trace at all. Log: " << obs.log;
}

/*
 * ⭐ THE CLASS IS CLOSED BY CONSTRUCTION, and this states it.
 *
 * The sidecar used to stream argv[1] - the broker JSON, password included - at
 * two sites, one of them on the failure path and printed by default. Removing
 * those two lines only ever held two lines: any reformulation reopened the
 * leak, and the number of sites that could touch an argv is not bounded. What
 * closes the class is that the configuration is no longer in the argv at all,
 * so the sidecar reads NO argument.
 *
 * ⚠️ The file is asserted to still be the one that defines setup(), so that a
 * rename or a move cannot make the count zero and this case vacuously green.
 *
 * ⚠️ COMMENTS ARE NOT CODE, and this one reads only the code. The file's own
 * comments explain why the argv is empty; counting them too would make the
 * tripwire red on a maintainer who described the defect, which is the kind of
 * red that gets a tripwire deleted rather than obeyed. The stripper is
 * exercised on a fixture first, so that a stripper that returned nothing at
 * all could not make this case vacuously green.
 *
 * ⚠️ A spelling, not an effect - with the weakness that implies. What the
 * sidecar really does with the configuration it now waits for is
 * core/MqttSidecarConfigWait_test, which runs the shipped binary.
 */
TEST_F(ExternProcLogSecretTest, TripwireSource_TheMqttSidecarReadsNoArgumentAtAll)
{
    const std::string probe =
        "//a comment naming argv[1]\n"
        "/* a block naming argv[2] */\n"
        "const char *s = \"a literal naming argv[3]\";\n"
        "int n = argv[4];\n";
    const std::string stripped = collapseWhitespace(stripCommentsAndLiterals(probe));
    ASSERT_EQ(1, countOccurrences(stripped, "argv["))
        << "the comment and literal stripper does not keep exactly the one "
           "occurrence that is code: " << stripped;
    ASSERT_NE(std::string::npos, stripped.find("int n = argv[4];"))
        << "the stripper ate the code it was meant to keep: " << stripped;

    const std::string relative = "src/bin/calaos_server/IO/Mqtt/MqttExternProc_main.cpp";

    std::string raw;
    ASSERT_TRUE(readShippedSource(relative, raw))
        << "could not read " << relative;

    const std::string code = collapseWhitespace(stripCommentsAndLiterals(raw));

    ASSERT_LE(1, countOccurrences(code, "bool MqttProcess::setup(int &argc, char **&argv)"))
        << relative << " no longer defines setup(): this tripwire is pointing "
           "at the wrong file and proves nothing";

    EXPECT_EQ(0, countOccurrences(code, "argv["))
        << "the sidecar reads an argument again. The broker configuration is "
           "what used to be there, /proc/<pid>/cmdline is mode 444, and every "
           "site that touches an argv carrying a secret is a leak site.";
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
