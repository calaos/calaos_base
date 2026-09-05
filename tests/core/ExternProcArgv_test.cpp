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
 * ⭐⭐ WHAT REACHES A SIDECAR'S argv, OBSERVED THROUGH THE PRODUCTION PATH.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT
 * ---------------------------------------------------------------------------
 * ExternProcServer::startProcess() used to take its arguments as ONE STRING,
 * concatenate it into a command line and hand that line to
 * Utils::CStrArray, which re-splits it on the space. No quoting, no escaping:
 * every space inside a value coming from the configuration became an argument
 * boundary. T3.28a closed the Roon case at the source by refusing a host that
 * carries a space; that answer does not exist for an MQTT password, where a
 * space is ordinary. Refusing it would be worse than the defect.
 *
 * On master a password of `mon mot de passe` makes calaos_mqtt see argc == 5
 * where MqttExternProc_main.cpp demands 2: it prints "Unable to read
 * configuration", exits, and the 100 ms respawn of ExternProcServer relaunches
 * it, forever.
 *
 * ⚠️ THE MQTT HALF HAS SINCE MOVED OFF THE argv ALTOGETHER. The broker
 * configuration is now the first message of the socket, because
 * /proc/<pid>/cmdline published its password to every account of the machine;
 * what is left here is that the launch carries nothing of its own. Where the
 * configuration goes instead is core/MqttConfigTransport_test.
 *
 * ---------------------------------------------------------------------------
 * ⚠️ WHY EVERY CASE HERE GOES THROUGH A REAL CONTROLLER
 * ---------------------------------------------------------------------------
 * The three merges before this one each found a guard that was exercised ONLY
 * by direct call and never wired to the site that ships it - a green suite
 * over a live defect. Nothing below calls startProcess(): the MQTT case builds
 * a real MqttCtrl from a real Params and the OneWire case a real OwCtrl, and
 * both read back what the KERNEL handed the child. A mutation of MqttCtrl.cpp
 * or OWCtrl.cpp turns them red.
 *
 * ---------------------------------------------------------------------------
 * ⛔ THE HALF THAT MUST NOT MOVE
 * ---------------------------------------------------------------------------
 * `ow_args` is documented as a LIST of owfs arguments and OWTemp.cpp prefixes
 * `"--use-w1 "` to it, space included; OWExternProc_main.cpp then joins argv[1
 * ..] back together. The re-split is load bearing there and nowhere else, so
 * the OneWire case pins the argv byte for byte and is GREEN ON BOTH SIDES of
 * the fix - it is the non-regression witness, not a defect report.
 *
 * ⛔ WHAT THIS DOES NOT PROVE: that the sidecars do the right thing with the
 * argv they get. The recorders are stand-ins; no broker and no 1-wire bus were
 * involved.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <unistd.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "CalaosCoreFixture.h"
#include "ExternProcSpawnHarness.h"
#include "MqttCtrl.h"
#include "MqttWire.h"
#include "OWCtrl.h"
#include "Params.h"
#include "StringUtils.h"
#include "libuvw.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

#ifndef CALAOS_TOP_SRCDIR
#error "CALAOS_TOP_SRCDIR must be passed by the build (see tests/Makefile.am)"
#endif

//Read a shipped source file. Answers false when the file cannot be opened, so
//a mis-wired path FAILS the case instead of quietly asserting on "".
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
//core/RoonArgs_test.cpp.
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

/*
 * The four arguments startProcess() always emits before anything a caller
 * adds: the executable, --socket <path it picks itself>, --namespace <name>.
 * ExternProcClient strips the two flag pairs, so the sidecar's own argc is
 * `argv.size() - 4`.
 */
const std::size_t kFixedArgc = 5;
const std::size_t kStrippedByBase = 4;

/*
 * ⚠️ FIXTURE, NOT DECORATION.
 *
 * The password is the whole point: three spaces, and an unremarkable one that
 * any installer would accept. `user` carries none, so a case that came out
 * green because the password had been dropped altogether rather than carried
 * whole would still have to explain where it went - which is why the encoded
 * configuration is asserted to CONTAIN it before anything is spawned.
 *
 * host/port/keepalive are deliberately off the built-in defaults
 * (127.0.0.1/1883/120): a fixture sitting on them cannot tell "the configured
 * value crossed" from "a default was substituted".
 */
const char *const kMqttPassword = "mon mot de passe";

Params mqttParams()
{
    Params p;
    p.Add("host", "192.168.7.42");
    p.Add("port", "1884");
    p.Add("keepalive", "45");
    p.Add("user", "calaos");
    p.Add("password", kMqttPassword);
    return p;
}

/*
 * The controllers are LEAKED, on purpose, and built once per process.
 *
 * ~MqttCtrl does not delete its ExternProcServer and OwCtrl::Instance() hands
 * back a process-lifetime shared_ptr; destroying either while the default loop
 * can still run the 100 ms respawn timer is a use-after-free. Keeping them
 * alive for the life of the process is also what makes these cases replayable
 * under --gtest_repeat: the journals simply keep growing and every line in
 * them is still checked.
 */
MqttCtrl *theMqttCtrl()
{
    static MqttCtrl *ctrl = nullptr;
    if (!ctrl)
    {
        Params p = mqttParams();
        ctrl = new MqttCtrl(p);
    }
    return ctrl;
}

/*
 * ⚠️ FIXTURE, NOT DECORATION - the OneWire half.
 *
 * This is the shape OWTemp.cpp produces: `"--use-w1 "` prefixed to the raw
 * `ow_args` of the configuration. The runs of two spaces and the trailing one
 * are there because Utils::split() collapses them and drops the empty tokens,
 * so they are exactly the inputs on which a hand-written argument list is most
 * likely to disagree with the re-split it replaces.
 */
const char *const kOwArgs = "--use-w1   -u  --foo bar ";

std::shared_ptr<OwCtrl> theOwCtrl()
{
    return OwCtrl::Instance(kOwArgs);
}

//No argument of any fixture here may carry a newline: the recorder writes one
//line per launch, and a newline would silently split a record in two.
void assertRecordable(const std::string &s, const char *what)
{
    ASSERT_EQ(std::string::npos, s.find('\n'))
        << what << " carries a newline, which the spawn journal cannot record";
}

} // namespace

class ExternProcArgvTest: public CoreFixture {};

/*
 * ⭐⭐ THE MQTT SIDECAR IS LAUNCHED WITH NOTHING OF ITS OWN.
 *
 * The broker configuration used to be argv[1] - and it carries the password,
 * which /proc/<pid>/cmdline (mode 444) then published to every account of the
 * machine for the whole life of the sidecar. It travels on the socket now, so
 * what this case pins is that the argument list is EXACTLY the fixed head.
 *
 * ⚠️ EVERY LAUNCH IS CHECKED, NOT "AT LEAST ONE". The respawn is the half that
 * kept the previous defect alive; an assertion happy with one good line would
 * be green on a first launch that is right followed by respawns that are wrong.
 *
 * ⚠️ ANTI-VACUITY: the configuration is asserted to still carry the password
 * before anything is spawned, so that a green here cannot mean "the field was
 * dropped". Where it goes instead is core/MqttConfigTransport_test.
 */
TEST_F(ExternProcArgvTest, TheMqttSidecarIsLaunchedWithNoArgumentOfItsOwn)
{
    ASSERT_TRUE(ExternProcSpawn::install("calaos_mqtt"))
        << "could not set up the CALAOS_BIN_PREFIX sandbox";

    const Params p = mqttParams();
    const std::string password = kMqttPassword;
    const std::string encoded = MqttWire::encodeConfig(p);

    ASSERT_NE(std::string::npos, password.find(' '))
        << "the fixture password carries no space, so this case cannot say "
           "anything about the defect it grew out of";
    ASSERT_NE(std::string::npos, encoded.find(password))
        << "encodeConfig() did not carry the password at all, so a green here "
           "would mean nothing: " << encoded;
    assertRecordable(encoded, "the encoded MQTT configuration");

    ASSERT_TRUE(theMqttCtrl() != nullptr);

    const bool respawned = ExternProcSpawn::runLoopUntil(
        []() { return ExternProcSpawn::launches("calaos_mqtt").size() >= 2; }, 5000);

    const std::vector<std::vector<std::string>> spawns =
        ExternProcSpawn::launches("calaos_mqtt");

    ASSERT_FALSE(spawns.empty())
        << "calaos_mqtt was never spawned at all - the harness is broken, not "
           "the code under test";
    ASSERT_TRUE(respawned)
        << "the sidecar was spawned " << spawns.size()
        << " time(s) in 5s: the respawn never happened, so this case cannot "
           "say anything about the arguments it carries";

    for (std::vector<std::vector<std::string>>::size_type i = 0; i < spawns.size(); i++)
    {
        const std::vector<std::string> &argv = spawns[i];

        ASSERT_EQ(kFixedArgc, argv.size())
            << "launch #" << (i + 1) << " of " << spawns.size()
            << ": the sidecar was handed " << (argv.size() - kFixedArgc)
            << " argument(s). The broker configuration belongs on the socket, "
               "not in a command line every account of the box can read.";

        EXPECT_EQ("--namespace", argv[3]);
        EXPECT_EQ("mqtt",        argv[4]);
        EXPECT_EQ(1u, argv.size() - kStrippedByBase)
            << "launch #" << (i + 1) << ": calaos_mqtt would see argc "
            << (argv.size() - kStrippedByBase) << " and takes no argument";
    }
}

/*
 * ⛔ NON-REGRESSION, GREEN ON BOTH SIDES: `ow_args` is a LIST, and it must keep
 * cutting exactly where it used to.
 *
 * OWTemp.cpp:41-42 documents the parameter as owfs arguments ("you can use -u
 * to use the USB owfs drivers") and :53-55 prefixes `"--use-w1 "` to it, space
 * included; OWExternProc_main.cpp:266-267 then joins argv[1..] back into one
 * owfs init string. Handing that field over as a SINGLE argument would break
 * OneWire - which is why this ticket is not a mechanical rewrite of its
 * callers.
 *
 * ⚠️ The expectation is spelled out AND cross-checked against Utils::split(),
 * the splitter that used to do this work. The literal alone would not notice a
 * change of splitting rule; the split alone would move with a mutation of
 * Utils::split() and agree with itself.
 */
TEST_F(ExternProcArgvTest, TheOneWireArgumentListStillReachesTheSidecarAsSeparateArguments)
{
    ASSERT_TRUE(ExternProcSpawn::install("calaos_1wire"))
        << "could not set up the CALAOS_BIN_PREFIX sandbox";

    const std::string args = kOwArgs;
    ASSERT_NE(std::string::npos, args.find("  "))
        << "the fixture carries no run of spaces, so it cannot say whether the "
           "empty tokens are still dropped";
    ASSERT_EQ(' ', args[args.size() - 1])
        << "the fixture carries no trailing space, same reason";
    assertRecordable(args, "the ow_args fixture");

    const std::vector<std::string> expected = { "--use-w1", "-u", "--foo", "bar" };

    std::vector<std::string> byTheOldSplitter;
    Utils::split(args, byTheOldSplitter, " ");
    ASSERT_EQ(expected, byTheOldSplitter)
        << "the literal expectation and Utils::split() disagree: one of the "
           "two is wrong and this case cannot arbitrate";

    ASSERT_TRUE(theOwCtrl() != nullptr);

    const bool respawned = ExternProcSpawn::runLoopUntil(
        []() { return ExternProcSpawn::launches("calaos_1wire").size() >= 2; }, 5000);

    const std::vector<std::vector<std::string>> spawns =
        ExternProcSpawn::launches("calaos_1wire");

    ASSERT_FALSE(spawns.empty())
        << "calaos_1wire was never spawned at all - the harness is broken, not "
           "the code under test";
    ASSERT_TRUE(respawned)
        << "the sidecar was spawned " << spawns.size()
        << " time(s) in 5s: the respawn never happened";

    for (std::vector<std::vector<std::string>>::size_type i = 0; i < spawns.size(); i++)
    {
        const std::vector<std::string> &argv = spawns[i];

        ASSERT_EQ(kFixedArgc + expected.size(), argv.size())
            << "launch #" << (i + 1) << " of " << spawns.size()
            << ": ow_args no longer reaches calaos_1wire as "
            << expected.size() << " separate arguments";

        EXPECT_EQ("--namespace", argv[3]);
        EXPECT_EQ("1wire",       argv[4]);

        for (std::size_t k = 0; k < expected.size(); k++)
        {
            EXPECT_EQ(expected[k], argv[kFixedArgc + k])
                << "launch #" << (i + 1) << ", owfs argument #" << k;
        }
    }
}

/*
 * ⭐ THE OLD SURFACE IS GONE, not merely unused.
 *
 * A string argument that startProcess() re-splits is the defect itself: as
 * long as the overload exists, the next caller reopens the class by writing
 * the obvious thing. The type system is what really closes it - a std::string
 * does not convert to a std::vector<std::string>, so every old call site is a
 * compile error rather than a silent regression - and this oracle only states
 * the intent so that a maintainer restoring the string form is told why not.
 *
 * ⚠️ It pins a SPELLING, not an effect, with the weakness that implies: a loud
 * red naming the form it wants, never a silent survival.
 */
TEST_F(ExternProcArgvTest, TripwireSource_TheSidecarArgumentsAreAVectorAndNotAStringToResplit)
{
    std::string raw;
    ASSERT_TRUE(readShippedSource("src/bin/calaos_server/IO/ExternProc.h", raw))
        << "could not read the shipped header";

    const std::string code = collapseWhitespace(raw);

    EXPECT_EQ(1, countOccurrences(code, "void startProcess("))
        << "ExternProcServer must declare exactly one startProcess()";
    EXPECT_EQ(1, countOccurrences(
                  code,
                  "void startProcess(const string &process, const string &name, "
                  "const vector<string> &args = vector<string>());"))
        << "startProcess() must take its arguments as a vector. A string is "
           "re-split on the space by Utils::CStrArray, which is the defect "
           "T3.78 closed: any value carrying a space becomes several argv.";
    EXPECT_EQ(0, countOccurrences(code, "const string &args"))
        << "the string form of startProcess() is back: the next caller will "
           "reopen the class";
}

/*
 * Own main instead of gtest_main, same reason as tests/core/KnxIo_test.cpp and
 * tests/core/RoonArgs_test.cpp: skip the static destructors.
 *
 * Destroying an ExternProcServer at process exit does kill(SIGTERM) on the
 * last spawned child and closes libuv handles on a loop this binary has
 * stopped pumping. KnxIo_test documents what that once cost: a SIGTERM to the
 * whole process group taking down the automake harness with Error 143 AFTER
 * the tests had passed.
 *
 * ⚠️ _exit() skips destructors, so teardown() is what gives back the sandbox,
 * the unix sockets ~ExternProcServer never unlinked, and the last children.
 */
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    CalaosTest::ExternProcSpawn::teardown();

    fflush(nullptr);
    _exit(ret);
}
