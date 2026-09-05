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
 * ⭐⭐ WHAT THE REAL calaos_mqtt DOES WHEN ITS CONFIGURATION COMES BY SOCKET.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS SUITE EXISTS AT ALL
 * ---------------------------------------------------------------------------
 * Moving the broker credentials out of the argv moves the "configuration
 * unreadable" failure with them: it used to be an argc test in main(), it is
 * now a wait on a socket. A wait has three ways to go wrong that an argument
 * did not have - the configuration never arrives, it arrives twice, it arrives
 * malformed - and one of them is what a PARTIAL UPDATE looks like: a server
 * older than its sidecar hands the configuration in an argv this build no
 * longer reads, so nothing ever arrives.
 *
 * ⭐ THE REAL BINARY IS LAUNCHED, THROUGH THE REAL TRANSPORT. calaos_mqtt has
 * a main(), so nothing links it and nothing can call into it; every earlier
 * statement about its behaviour in this tree is a source tripwire, which pins
 * a spelling and not an effect. Here an ExternProcServer spawns the shipped
 * binary, writes on the socket what a controller would write, and the sidecar's
 * own stdout - which the server pipes into its own - is what is read back.
 *
 * ---------------------------------------------------------------------------
 * ⚠️ BOUNDED ON WALL CLOCK, NEVER ON ITERATIONS
 * ---------------------------------------------------------------------------
 * A regression must be able to fail a case, never to hang `make check`. Every
 * wait below has a deadline and a case that says what it was waiting for.
 *
 * ⛔ WHAT THIS DOES NOT PROVE: that the sidecar talks to a broker. No broker
 * is involved - the only case that configures one points it at a loopback
 * socket that completes the handshake and then says nothing, which is the
 * state a stock install is in while its broker is silent.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "CalaosCoreFixture.h"
#include "ExternProcSpawnHarness.h"
#include "ExternProc.h"
#include "MqttWire.h"
#include "Params.h"
#include "libuvw.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

#ifndef CALAOS_MQTT_SIDECAR
#error "CALAOS_MQTT_SIDECAR must be passed by the build (see tests/Makefile.am)"
#endif

/*
 * ⚠️ FIXTURE, NOT DECORATION - AND THE PORT MUST REALLY ANSWER.
 *
 * libmosquitto starts a NON BLOCKING connect: to a host that is merely
 * unreachable it answers MOSQ_ERR_CONN_PENDING, which the sidecar treats as a
 * failure and leaves on. A listener on the loopback is what makes the
 * three-way handshake complete inside connect(), so connect_async() answers
 * MOSQ_ERR_SUCCESS and the sidecar stays up waiting for a CONNACK that never
 * comes - the state a stock install is in while its broker is silent.
 *
 * The listener never accepts: the kernel backlog completes the handshake on
 * its own, and accepting would add a peer this suite has no use for.
 */
int openLoopbackListener(int &port)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_in addr;
    ::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    socklen_t len = sizeof(addr);
    if (::bind(fd, (struct sockaddr *)&addr, len) != 0 ||
        ::listen(fd, 8) != 0 ||
        ::getsockname(fd, (struct sockaddr *)&addr, &len) != 0)
    {
        ::close(fd);
        return -1;
    }

    port = ::ntohs(addr.sin_port);
    return fd;
}

Params brokerParams(int port)
{
    Params p;
    p.Add("host", "127.0.0.1");
    p.Add("port", Utils::to_string(port));
    p.Add("keepalive", "45");
    p.Add("user", "courtier-utilisateur");
    p.Add("password", "mot de passe");
    return p;
}

/*
 * How long the shipped sidecar waits for its configuration before giving up.
 * Kept in step with kConfigWaitMs of IO/Mqtt/MqttExternProc_main.cpp; a case
 * that outran it would fail on the harness's clock instead of on the code.
 */
const int kSidecarWaitMs = 5000;

struct SidecarRun
{
    bool exited = false;
    long exitedAfterMs = 0;
    std::string log;
};

//Heap state, LEAKED: the server keeps libuv handles on the default loop and
//its deferred processExited fires 100 ms after the child is gone, which is
//after the function that armed it has returned. A stack capture there is a
//use-after-free that only shows under load.
struct RunState
{
    ExternProcServer *server = nullptr;
    bool exited = false;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point exitedAt;
};

/*
 * Spawn the shipped sidecar, write `messages` on the socket as soon as it
 * connects, and watch until it exits or the budget runs out.
 *
 * std::cout is captured across the whole run: ExternProcServer pipes the
 * child's stdout into it line by line, so what comes back is the sidecar's own
 * journal, the one a stock install would print.
 */
SidecarRun runSidecar(const std::vector<std::string> &messages, int budgetMs)
{
    SidecarRun out;

    std::ostringstream sink;
    std::streambuf *saved = std::cout.rdbuf(sink.rdbuf());

    RunState *st = new RunState();
    st->server = new ExternProcServer("mqtt");
    st->started = std::chrono::steady_clock::now();

    st->server->processConnected.connect([st, messages]()
    {
        for (std::vector<std::string>::const_iterator it = messages.begin();
             it != messages.end(); ++it)
            st->server->sendMessage(*it);
    });

    st->server->processExited.connect([st]()
    {
        if (!st->exited)
        {
            st->exited = true;
            st->exitedAt = std::chrono::steady_clock::now();
        }
    });

    st->server->startProcess(CALAOS_MQTT_SIDECAR, "mqtt");

    ExternProcSpawn::runLoopUntil([st]() { return st->exited; }, budgetMs);

    out.exited = st->exited;
    if (st->exited)
        out.exitedAfterMs = static_cast<long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                st->exitedAt - st->started).count());

    //Give back the child before reading the log: terminate() signals it, and a
    //last turn of the loop drains what it wrote on its way out.
    st->server->terminate();
    ExternProcSpawn::runLoopUntil([]() { return false; }, 300);

    std::cout.rdbuf(saved);
    out.log = sink.str();
    return out;
}

bool logCarries(const SidecarRun &r, const std::string &needle)
{
    return r.log.find(needle) != std::string::npos;
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

} // namespace

class MqttSidecarConfigWaitTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();

        if (::access(CALAOS_MQTT_SIDECAR, X_OK) != 0)
            GTEST_SKIP() << "calaos_mqtt was not built (no libmosquitto), so "
                            "nothing here can be executed";
    }
};

/*
 * ⭐⭐ THE PARTIAL UPDATE: a configuration that never arrives must END the
 * sidecar, loudly, and only after it has really waited.
 *
 * RED on master, and for the right reason: master reads the configuration in
 * main(), sees argc 1 and exits within milliseconds - it never waits for
 * anything, so the lower bound is what fails.
 *
 * ⚠️ TWO BOUNDS, NOT ONE. Without the lower bound, a sidecar that exited
 * immediately would pass; without the upper one, a sidecar that waited for
 * ever would hang instead of failing. What is between them is the only
 * behaviour that both waits and gives up.
 *
 * ⛔ AND IT DOES LOOP, WHICH IS SAID HERE RATHER THAN HIDDEN: the server
 * relaunches an exited sidecar with no backoff (E4.5d), so this path is a
 * relaunch loop - one turn every kConfigWaitMs instead of one every 100 ms,
 * each turn printing the line below. Silence was the alternative, and silence
 * is what a partial update must never be.
 */
TEST_F(MqttSidecarConfigWaitTest, AConfigurationThatNeverArrivesEndsTheSidecarAfterItHasWaited)
{
    const SidecarRun r = runSidecar(std::vector<std::string>(), kSidecarWaitMs * 3);

    ASSERT_TRUE(r.exited)
        << "the sidecar was still running " << (kSidecarWaitMs * 3)
        << " ms after being launched with no configuration: it waits for ever "
           "and says nothing, which is what a partial update would look like. "
           "Log: " << r.log;

    EXPECT_GE(r.exitedAfterMs, kSidecarWaitMs / 2)
        << "the sidecar gave up after " << r.exitedAfterMs
        << " ms, so it did not wait for its configuration at all. Log: " << r.log;
    EXPECT_LE(r.exitedAfterMs, kSidecarWaitMs * 2)
        << "the sidecar took " << r.exitedAfterMs << " ms to give up, well past "
           "the " << kSidecarWaitMs << " ms it announces";

    EXPECT_TRUE(logCarries(r, "waiting for its configuration"))
        << "nothing in the journal says why the sidecar left, so an operator "
           "sees a relaunch loop with no cause. Log: " << r.log;
}

/*
 * A configuration that cannot be read ends the sidecar too - and FAST, on the
 * message rather than on the deadline.
 *
 * ⚠️ The diagnosis names a byte count and nothing else. That message is the
 * one that carries the broker password when it is well formed, and the server
 * pipes this stdout straight into its own journal: publishing what could not
 * be parsed is how the previous ticket's leak would come back through the
 * error path.
 */
TEST_F(MqttSidecarConfigWaitTest, AMalformedConfigurationEndsTheSidecarOnTheMessageNotOnTheDeadline)
{
    const std::string garbage = "{\"host\": this is not json";
    const SidecarRun r = runSidecar(std::vector<std::string>(1, garbage), kSidecarWaitMs * 3);

    ASSERT_TRUE(r.exited)
        << "a configuration that cannot be parsed left the sidecar running. "
           "Log: " << r.log;
    EXPECT_LT(r.exitedAfterMs, kSidecarWaitMs)
        << "the sidecar took " << r.exitedAfterMs
        << " ms, i.e. it sat on the deadline instead of answering the message "
           "it had already received. Log: " << r.log;

    EXPECT_TRUE(logCarries(r, "configuration"))
        << "the journal does not say the configuration was the problem. Log: "
        << r.log;
    EXPECT_FALSE(logCarries(r, "this is not json"))
        << "the sidecar published the message it could not read. That message "
           "is the one carrying the broker password when it is well formed, "
           "and the server pipes this stdout into its own journal. Log: "
        << r.log;
}

/*
 * A second configuration is REFUSED, and the sidecar stays alive.
 *
 * Applying it would move a live client to another broker under the same client
 * id; exiting on it would hand the relaunch loop a second way in. Neither: it
 * is named once and dropped.
 *
 * RED on master, and for the right reason: master exits on argc before it can
 * receive anything at all.
 */
TEST_F(MqttSidecarConfigWaitTest, ASecondConfigurationIsRefusedAndTheSidecarStaysAlive)
{
    int port = 0;
    const int listener = openLoopbackListener(port);
    ASSERT_GE(listener, 0) << "could not open a loopback listener";

    const std::string cfg = MqttWire::encodeConfig(brokerParams(port));
    std::vector<std::string> twice;
    twice.push_back(cfg);
    twice.push_back(cfg);

    const SidecarRun r = runSidecar(twice, kSidecarWaitMs + 1500);

    EXPECT_FALSE(r.exited)
        << "the sidecar left after " << r.exitedAfterMs
        << " ms although it had been configured: a second configuration must "
           "not be a way out of the process. Log: " << r.log;

    EXPECT_EQ(1, countOccurrences(r.log, "second broker configuration"))
        << "the second configuration was not named exactly once. Log: " << r.log;
    EXPECT_FALSE(logCarries(r, "mot de passe"))
        << "the sidecar published its own configuration while refusing the "
           "duplicate. Log: " << r.log;

    ::close(listener);
}

/*
 * Own main instead of gtest_main, same reason as core/ExternProcArgv_test.cpp:
 * destroying an ExternProcServer at process exit sends SIGTERM to the last
 * spawned child and closes libuv handles on a loop this binary has stopped
 * pumping. teardown() gives back the children and the unix sockets that
 * ~ExternProcServer never got to unlink.
 */
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    CalaosTest::ExternProcSpawn::teardown();

    fflush(nullptr);
    _exit(ret);
}
