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
 * ⭐⭐ BY WHICH CHANNEL THE BROKER CREDENTIALS REACH calaos_mqtt.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT
 * ---------------------------------------------------------------------------
 * The broker configuration was argv[1], and it carries `user` and `password`.
 * /proc/<pid>/cmdline is mode 444: an account with no relation whatsoever to
 * the owner of the process reads that argv back VERBATIM, for the whole life
 * of the sidecar, and `ps` is enough. The previous ticket closed the leak by
 * the JOURNAL; this one is the leak by the KERNEL, which no amount of log
 * hygiene can reach - as long as the secret is in the argv, every site that
 * touches the argv is a leak site and there is no bound on their number.
 *
 * ---------------------------------------------------------------------------
 * ⚠️ WHY EVERY CASE HERE GOES THROUGH A REAL MqttCtrl
 * ---------------------------------------------------------------------------
 * Nothing below calls startProcess() or sendMessage(). One production
 * MqttCtrl is built from a real Params, its respawn is let run, and what is
 * read back is the argv THE KERNEL handed the child - then the socket that
 * same controller is listening on. A guard exercised by direct call and never
 * wired to the shipped site is a green suite over a live defect, six times
 * over in this series.
 *
 * ---------------------------------------------------------------------------
 * ⚠️ TWO FORMS OF THE SAME NEEDLE, AND WHY
 * ---------------------------------------------------------------------------
 * The wire is JSON with ensure_ascii on, so what a secret LOOKS LIKE on the
 * wire is not what the literal looks like: a quote becomes \" and an accent
 * becomes é. A case that searched for the literal alone would be green
 * while the escaped form sat in the argv. `user` is plain ASCII and crosses
 * byte for byte, the password is not - both are searched for, in the form the
 * emitter really writes, and the two forms are asserted to differ so that
 * neither check can be silently the same one twice.
 *
 * ⛔ WHAT THIS DOES NOT PROVE: that calaos_mqtt does anything sensible with
 * the message it now receives. The launched binary is a stand-in that records
 * its argv and exits; what the real sidecar does with the wait, and what it
 * does when the configuration never arrives, arrives twice or arrives
 * malformed, is core/MqttSidecarConfigWait_test.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

#include "CalaosCoreFixture.h"
#include "ExternProcSpawnHarness.h"
#include "ExternProc.h"
#include "MqttCtrl.h"
#include "MqttWire.h"
#include "Params.h"
#include "StringUtils.h"
#include "libuvw.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

//The five argv startProcess() emits before anything a caller adds: the
//executable, --socket <path it picks itself>, --namespace <name>.
const std::size_t kFixedArgc = 5;

/*
 * ⚠️ FIXTURE, NOT DECORATION.
 *
 * `user` is plain ASCII, so the wire carries it byte for byte and the literal
 * is a usable needle. The password carries a quote AND a byte above ASCII, so
 * the wire carries `mot \"de\" passe café` and the literal is NOT a
 * usable needle - which is exactly the form the cases search for.
 *
 * host/port/keepalive sit off the built-in defaults (127.0.0.1/1883/120) so
 * that "the configured value crossed" cannot be confused with "a default was
 * substituted". 192.0.2.x is TEST-NET-1 (RFC 5737): valid, never local.
 */
const char *const kMqttHost      = "192.0.2.42";
const char *const kMqttPort      = "1884";
const char *const kMqttKeepalive = "45";
const char *const kMqttUser      = "courtier-utilisateur";
const char *const kMqttPassword  = "mot \"de\" passe caf\xc3\xa9";

Params mqttParams()
{
    Params p;
    p.Add("host", kMqttHost);
    p.Add("port", kMqttPort);
    p.Add("keepalive", kMqttKeepalive);
    p.Add("user", kMqttUser);
    p.Add("password", kMqttPassword);
    return p;
}

//What the shipped emitter writes for one value, minus its quotes. Derived
//from dumpJson() rather than spelled by hand so that a change of emission
//invariant moves the needle with it instead of leaving it behind.
std::string onTheWire(const std::string &value)
{
    const Json j = value;
    const std::string quoted = MqttWire::dumpJson(j);
    if (quoted.size() < 2)
        return quoted;
    return quoted.substr(1, quoted.size() - 2);
}

std::string render(const std::vector<std::string> &argv)
{
    if (argv.empty())
        return "<no argument>";
    std::string s;
    for (std::vector<std::string>::const_iterator it = argv.begin(); it != argv.end(); ++it)
        s += "<" + *it + ">";
    return s;
}

/*
 * One MqttCtrl for the whole process, LEAKED on purpose.
 *
 * ~MqttCtrl does not delete its ExternProcServer and the default loop can
 * still run the 100 ms respawn; destroying it is a use-after-free. Every case
 * reads the same journal and the same socket, so their order does not matter.
 */
struct Observation
{
    bool installed = false;
    bool respawned = false;
    std::vector<std::vector<std::string>> spawns;
    std::string sockpath;
    std::string firstFramePayload;
    bool gotFrame = false;
};

//Read whatever is readable right now, without blocking: the loop this drives
//is the server's, and a blocking read here would stop it writing.
bool drainInto(int fd, std::string &out)
{
    char buf[4096];
    const ssize_t n = ::recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
    if (n > 0)
    {
        out.append(buf, buf + n);
        return true;
    }
    return false;
}

/*
 * Stand in for the sidecar on the socket side.
 *
 * The recorder installed by the harness records its argv and exits without
 * connecting, so the server has no client and writes nothing. Connecting HERE
 * is what makes the server emit: the accept is what processConnected is, and
 * the first frame that comes back is what this suite is about. The socket
 * path is read from the spawn journal, which is the only place the production
 * code publishes it.
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

    Params p = mqttParams();
    new MqttCtrl(p);

    obs->respawned = ExternProcSpawn::runLoopUntil(
        []() { return ExternProcSpawn::launches("calaos_mqtt").size() >= 2; }, 8000);
    obs->spawns = ExternProcSpawn::launches("calaos_mqtt");

    if (obs->spawns.empty() || obs->spawns[0].size() < kFixedArgc)
        return *obs;

    obs->sockpath = obs->spawns[0][2];

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return *obs;

    struct sockaddr_un remote;
    ::memset(&remote, 0, sizeof(remote));
    remote.sun_family = AF_UNIX;
    if (obs->sockpath.size() >= sizeof(remote.sun_path))
    {
        ::close(fd);
        return *obs;
    }
    ::strncpy(remote.sun_path, obs->sockpath.c_str(), sizeof(remote.sun_path) - 1);

    if (::connect(fd, (struct sockaddr *)&remote, sizeof(remote)) != 0)
    {
        ::close(fd);
        return *obs;
    }

    std::string raw;
    ExternProcMessage frame;

    ExternProcSpawn::runLoopUntil([&]()
    {
        drainInto(fd, raw);
        if (frame.processFrameData(raw) && frame.isValid())
        {
            obs->firstFramePayload = frame.getPayload();
            obs->gotFrame = true;
        }
        return obs->gotFrame;
    }, 8000);

    ::close(fd);
    return *obs;
}

} // namespace

class MqttConfigTransportTest: public CoreFixture {};

/*
 * ⭐⭐ THE CASE THIS SUITE EXISTS FOR: neither credential is anywhere in the
 * argv the kernel received.
 *
 * RED on master, and for the right reason: argv[5] IS the broker
 * configuration, so `ps` publishes the password to every account of the box.
 *
 * ⚠️ ANTI-VACUITY FIRST, in both forms. The wire is asserted to carry the user
 * verbatim and the password ESCAPED, and the two needles asserted different -
 * a fixture whose secret had been dropped, or whose two needles were the same
 * string twice, would otherwise make this case green while measuring nothing.
 *
 * ⚠️ POSITION, NOT ONLY ABSENCE. The argument list is required to be exactly
 * the fixed head: a case happy with "the password is not in there" would stay
 * green next to a second argument carrying it under another shape.
 *
 * ⚠️ EVERY LAUNCH IS CHECKED. The respawn is what multiplied the exposure in
 * the first place; an assertion happy with the first line would be green on a
 * first launch that is right followed by respawns that are wrong.
 */
TEST_F(MqttConfigTransportTest, NeitherBrokerCredentialIsInTheArgvTheKernelReceived)
{
    const Params p = mqttParams();
    const std::string wire = MqttWire::encodeConfig(p);
    const std::string userNeedle = onTheWire(kMqttUser);
    const std::string passwordNeedle = onTheWire(kMqttPassword);

    ASSERT_EQ(std::string(kMqttUser), userNeedle)
        << "the user is escaped on the wire, so the ASCII half of this case "
           "measures nothing it does not already measure with the password";
    ASSERT_NE(std::string(kMqttPassword), passwordNeedle)
        << "the password crosses the wire unescaped, so this case would search "
           "for one form twice instead of two";
    ASSERT_NE(std::string::npos, wire.find(userNeedle))
        << "the wire does not carry the user at all, so a green here would "
           "mean the field was dropped, not withheld: " << wire;
    ASSERT_NE(std::string::npos, wire.find(passwordNeedle))
        << "the wire does not carry the escaped password at all, same reason: "
        << wire;

    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_FALSE(obs.spawns.empty())
        << "calaos_mqtt was never spawned at all - the harness is broken, not "
           "the code under test";
    ASSERT_TRUE(obs.respawned)
        << "calaos_mqtt was spawned " << obs.spawns.size()
        << " time(s): without the respawn this case says nothing about what "
           "the second launch carries";

    for (std::size_t i = 0; i < obs.spawns.size(); i++)
    {
        const std::vector<std::string> &argv = obs.spawns[i];

        ASSERT_EQ(kFixedArgc, argv.size())
            << "launch #" << (i + 1) << " of " << obs.spawns.size()
            << " carries " << (argv.size() - kFixedArgc)
            << " argument(s) of its own: " << render(argv);

        for (std::size_t k = 0; k < argv.size(); k++)
        {
            EXPECT_EQ(std::string::npos, argv[k].find(userNeedle))
                << "launch #" << (i + 1) << ", argv[" << k << "]: the broker "
                   "USER is in /proc/<pid>/cmdline, mode 444, for the whole "
                   "life of the sidecar";
            EXPECT_EQ(std::string::npos, argv[k].find(passwordNeedle))
                << "launch #" << (i + 1) << ", argv[" << k << "]: the broker "
                   "PASSWORD is in /proc/<pid>/cmdline, mode 444, readable by "
                   "any account of the box - `ps` is enough";
            EXPECT_EQ(std::string::npos, argv[k].find(wire))
                << "launch #" << (i + 1) << ", argv[" << k << "]: the whole "
                   "broker configuration is in the argv";
        }
    }
}

/*
 * ⭐ THE OTHER HALF: the configuration did not vanish, it MOVED.
 *
 * A case that only checked the argv would be green on a build that dropped the
 * credentials altogether - the broker would refuse every connection and
 * nothing here would say so. What is asserted is that the very first frame the
 * server writes on the socket, after the sidecar connects, decodes to the
 * configuration with the values of the fixture in it, credentials included.
 *
 * ⚠️ The socket path is not invented here: it is read back from the spawn
 * journal, i.e. from the --socket the production code chose and passed to the
 * child, so a controller that listened somewhere else could not be green.
 *
 * ⚠️ Decoded, not compared byte for byte: the byte form of the wire is pinned
 * by MqttWire_test, and comparing it here again would make this case red for a
 * change of key ordering that has nothing to do with what it measures.
 */
TEST_F(MqttConfigTransportTest, TheBrokerConfigurationCrossesTheSocketAsTheFirstFrame)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_FALSE(obs.spawns.empty())
        << "calaos_mqtt was never spawned at all - the harness is broken";
    ASSERT_FALSE(obs.sockpath.empty())
        << "the launch does not name a socket: " << render(obs.spawns[0]);
    ASSERT_EQ("--socket", obs.spawns[0][1])
        << "argv[1] is not --socket, so argv[2] is not the path this case "
           "connected to: " << render(obs.spawns[0]);

    ASSERT_TRUE(obs.gotFrame)
        << "nothing was written on the socket in 8s after the connection. The "
           "broker configuration is no longer in the argv and it did not "
           "arrive here either, so the sidecar has no way to reach its broker.";

    Params got;
    ASSERT_TRUE(MqttWire::decodeMessage(obs.firstFramePayload, got))
        << "the first frame is not a json object ("
        << obs.firstFramePayload.size() << " bytes)";

    EXPECT_TRUE(got.Exists("action"))
        << "the first frame carries no action, so the sidecar cannot tell it "
           "from a publish request on topic \"\"";
    EXPECT_EQ("config", got["action"]);
    EXPECT_EQ(kMqttHost, got["host"]);
    EXPECT_EQ(kMqttPort, got["port"]);
    EXPECT_EQ(kMqttKeepalive, got["keepalive"]);
    EXPECT_EQ(kMqttUser, got["user"])
        << "the broker user did not reach the sidecar by any channel at all";
    EXPECT_EQ(kMqttPassword, got["password"])
        << "the broker password did not reach the sidecar by any channel at "
           "all: it was not moved, it was dropped";
}

/*
 * The discriminator, on both sides of the same wire.
 *
 * Server to sidecar carries two kinds of flat object - the configuration and a
 * publish request - and the sidecar picks between them by one key. Without it,
 * a configuration with no topic and no payload reads as a publish on topic ""
 * and the broker is never configured at all, in silence.
 */
TEST_F(MqttConfigTransportTest, AConfigurationIsTellableFromAPublishRequest)
{
    Params cfg;
    ASSERT_TRUE(MqttWire::decodeMessage(MqttWire::encodeConfig(mqttParams()), cfg));
    EXPECT_TRUE(cfg.Exists("action"));
    EXPECT_EQ("config", cfg["action"]);

    Params pub;
    ASSERT_TRUE(MqttWire::decodeMessage(
                    MqttWire::encodeMessage("maison/salon/store/set", "OPEN"), pub));
    EXPECT_FALSE(pub.Exists("action"))
        << "a publish request carries the key that means \"this is the "
           "configuration\": the sidecar would refuse every message the server "
           "sends it";
    EXPECT_EQ("maison/salon/store/set", pub["topic"]);
    EXPECT_EQ("OPEN", pub["payload"]);
}

/*
 * Own main instead of gtest_main, same reason as core/ExternProcArgv_test.cpp:
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
