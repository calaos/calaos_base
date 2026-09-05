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
 * ---------------------------------------------------------------------------
 * ⭐ AND WHAT IT DOES WHEN THE BROKER IS NOT THERE
 * ---------------------------------------------------------------------------
 * The three ways a broker goes missing are three different code paths and
 * only one of them used to be visible: a refused port fails inside
 * connect_async() and is named (wrongly, but named), while an unreachable host
 * and a broker that drops mid-session fail AFTER connect_async() has answered
 * success - libmosquitto closes its socket, and the descriptor the sidecar
 * registered in its main loop is stale from then on. All three are exercised
 * here against the shipped binary, and each one is pinned on BOTH halves of
 * what an operator needs: a non zero exit status and a line naming the cause.
 *
 * ⛔ WHAT THIS DOES NOT PROVE: that the sidecar talks to a real broker. The
 * only case that gets as far as a session uses four hand written bytes of
 * CONNACK; nothing here publishes or subscribes.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
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

Params brokerParamsAt(const std::string &host, int port)
{
    Params p;
    p.Add("host", host);
    p.Add("port", Utils::to_string(port));
    p.Add("keepalive", "45");
    p.Add("user", "courtier-utilisateur");
    p.Add("password", "mot de passe");
    return p;
}

Params brokerParams(int port)
{
    return brokerParamsAt("127.0.0.1", port);
}

/*
 * A loopback port that answers nothing at all: bound and listened to so that
 * the kernel really handed it out, then closed, so a connect to it is refused
 * on the spot instead of racing another service that might own it.
 */
int closedLoopbackPort()
{
    int port = 0;
    const int fd = openLoopbackListener(port);
    if (fd < 0)
        return -1;
    ::close(fd);
    return port;
}

/*
 * How long the shipped sidecar waits for its configuration before giving up -
 * READ FROM THE SHIPPED DECLARATION, never spelled again here. A second copy
 * of a bound is a bound that can drift, and a drift fails the case on the
 * harness's clock instead of on the code.
 */
const int kSidecarWaitMs = MqttWire::configWaitMs();

struct SidecarRun
{
    bool exited = false;
    long exitedAfterMs = 0;
    //-1 until the child has been reaped, and only the raw launcher below can
    //fill it: ExternProcServer::processExited carries no status at all. It also
    //stays -1 when the child died on a signal, which is why the cases ask for a
    //status ABOVE zero: "not zero" would take a crash for a reported failure.
    int exitCode = -1;
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

/*
 * The same sidecar, spawned WITHOUT ExternProcServer so that the bytes of the
 * socket can be cut where the test wants them.
 *
 * ExternProcServer only ever offers a whole message, so nothing driven through
 * it can say what happens when a configuration arrives in two reads - and a
 * unix stream promises nothing about where a write ends up being cut. This
 * writes the frames by hand, in the chunks it is given: one byte of opcode,
 * four of big endian length, then the payload, which is the framing of
 * IO/ExternProc.cpp and is pinned there.
 */
/*
 * A four byte broker, pumped from the same loop that watches the child.
 *
 * ⚠️ It is a FIXTURE WITH AN ORACLE, not a decoration: the cases that use it
 * assert that it really saw a CONNECT and really answered a CONNACK before it
 * dropped the connection. Without that, a sidecar which never reached the
 * broker at all would produce the same exit as one whose session collapsed,
 * and the case would be measuring the wrong failure.
 *
 * Single threaded on purpose - the sidecar is a separate process, so a state
 * machine driven every few milliseconds is enough and nothing here can race.
 */
struct FakeBroker
{
    int listenFd = -1;
    int port = 0;
    int conn = -1;
    bool sawConnect = false;
    bool sentConnack = false;
    bool dropped = false;
    long holdMs = 400;              //how long the session lives after CONNACK
    //CONNACK return code: 0 accepts the session, 5 is the refusal a wrong
    //broker password produces
    int connackRc = 0;
    std::chrono::steady_clock::time_point connackAt;
};

void setNonBlocking(int fd)
{
    const int fl = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

bool openFakeBroker(FakeBroker &b)
{
    b.listenFd = openLoopbackListener(b.port);
    if (b.listenFd < 0)
        return false;
    setNonBlocking(b.listenFd);
    return true;
}

void pumpFakeBroker(FakeBroker &b)
{
    if (b.listenFd < 0 || b.dropped)
        return;

    if (b.conn < 0)
    {
        const int c = ::accept(b.listenFd, NULL, NULL);
        if (c < 0)
            return;
        b.conn = c;
        setNonBlocking(b.conn);
    }

    if (!b.sentConnack)
    {
        char buf[512];
        const ssize_t n = ::recv(b.conn, buf, sizeof(buf), 0);
        if (n <= 0)
            return;
        b.sawConnect = true;
        //MQTT 3.1.1 CONNACK, session not present, then the return code
        const char connack[4] = { 0x20, 0x02, 0x00, char(b.connackRc) };
        b.sentConnack = ::send(b.conn, connack, sizeof(connack), MSG_NOSIGNAL) == 4;
        b.connackAt = std::chrono::steady_clock::now();
        return;
    }

    const long alive = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - b.connackAt).count());
    if (alive >= b.holdMs)
    {
        ::close(b.conn);
        b.conn = -1;
        b.dropped = true;
    }
}

void closeFakeBroker(FakeBroker &b)
{
    if (b.conn >= 0) ::close(b.conn);
    if (b.listenFd >= 0) ::close(b.listenFd);
    b.conn = b.listenFd = -1;
}

std::string frameOf(const std::string &payload)
{
    std::string f;
    f += char(0x21);                                   //TypeMessage
    const uint32_t n = static_cast<uint32_t>(payload.size());
    f += char((n >> 24) & 0xFF);
    f += char((n >> 16) & 0xFF);
    f += char((n >> 8) & 0xFF);
    f += char(n & 0xFF);
    f += payload;
    return f;
}

SidecarRun runSidecarRaw(const std::vector<std::string> &chunks, int budgetMs,
                         FakeBroker *broker = NULL)
{
    SidecarRun out;

    char dir[] = "/tmp/calaos_rawwaitXXXXXX";
    if (::mkdtemp(dir) == NULL)
        return out;
    const std::string sockpath = std::string(dir) + "/s";

    const int srv = ::socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    ::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    ::strncpy(addr.sun_path, sockpath.c_str(), sizeof(addr.sun_path) - 1);
    if (srv < 0 || ::bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        ::listen(srv, 1) != 0)
    {
        if (srv >= 0) ::close(srv);
        return out;
    }

    int pipefd[2];
    if (::pipe(pipefd) != 0)
    {
        ::close(srv);
        return out;
    }

    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    const pid_t pid = ::fork();
    if (pid == 0)
    {
        ::close(pipefd[0]);
        ::dup2(pipefd[1], 1);
        ::dup2(pipefd[1], 2);
        ::execl(CALAOS_MQTT_SIDECAR, CALAOS_MQTT_SIDECAR,
                "--socket", sockpath.c_str(), "--namespace", "mqtt", (char *)NULL);
        ::_exit(127);
    }
    ::close(pipefd[1]);

    const int cli = ::accept(srv, NULL, NULL);
    if (cli >= 0)
    {
        for (std::size_t i = 0; i < chunks.size(); i++)
        {
            if (::send(cli, chunks[i].data(), chunks[i].size(), MSG_NOSIGNAL) < 0)
                break;
            //A pause between two chunks is what makes them two READS on the
            //other side; without it the kernel may hand them over as one and
            //the case would measure nothing.
            if (i + 1 < chunks.size())
                ::usleep(250 * 1000);
        }
    }

    //Drain the child's stdout while waiting for it, so that a sidecar which
    //fills the pipe cannot deadlock this loop instead of failing the case.
    const int flags = ::fcntl(pipefd[0], F_GETFL, 0);
    ::fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    int status = 0;
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0).count() < budgetMs)
    {
        char buf[4096];
        const ssize_t n = ::read(pipefd[0], buf, sizeof(buf));
        if (n > 0)
            out.log.append(buf, buf + n);

        if (broker)
            pumpFakeBroker(*broker);

        if (::waitpid(pid, &status, WNOHANG) == pid)
        {
            out.exited = true;
            out.exitCode = WIFEXITED(status)? WEXITSTATUS(status): -1;
            out.exitedAfterMs = static_cast<long>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count());
            break;
        }
        ::usleep(5 * 1000);
    }

    if (!out.exited)
    {
        ::kill(pid, SIGKILL);
        ::waitpid(pid, &status, 0);
    }

    for (;;)
    {
        char buf[4096];
        const ssize_t n = ::read(pipefd[0], buf, sizeof(buf));
        if (n <= 0)
            break;
        out.log.append(buf, buf + n);
    }

    ::close(pipefd[0]);
    if (cli >= 0) ::close(cli);
    ::close(srv);
    ::unlink(sockpath.c_str());
    ::rmdir(dir);
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
    //The two bounds below separate "waited then gave up" from "left at once"
    //only while the announced wait is long enough to tell them apart. Reading
    //the bound from the shipped declaration removes the drift; it does not
    //remove a declaration shrunk to nothing, which would make them vacuous.
    ASSERT_GE(kSidecarWaitMs, 1000)
        << "the announced wait is " << kSidecarWaitMs
        << " ms, too short for the two bounds of this case to mean anything";

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
 * The FIRST message is refused when it is not the configuration.
 *
 * Both kinds of server-to-sidecar message are flat json objects, so the only
 * thing that tells them apart is the "action" key. Without a case here, that
 * key is decided by MqttWire alone and nothing says the sidecar reads it: a
 * build that took whatever came first for its configuration would talk to
 * 127.0.0.1:1883 with no credentials, in silence, and every case of this tree
 * would stay green - measured, by dropping the check and finding no red.
 */
TEST_F(MqttSidecarConfigWaitTest, APublishRequestArrivingFirstIsRefusedAndNotTakenForAConfiguration)
{
    const std::string publish = MqttWire::encodeMessage("maison/salon/store/set", "OPEN");

    Params shape;
    ASSERT_TRUE(MqttWire::decodeMessage(publish, shape));
    ASSERT_FALSE(shape.Exists("action"))
        << "a publish request now carries the configuration marker, so this "
           "case can no longer say anything: " << publish;

    const SidecarRun r = runSidecar(std::vector<std::string>(1, publish), kSidecarWaitMs * 3);

    ASSERT_TRUE(r.exited)
        << "a publish request sent before any configuration left the sidecar "
           "running: it took it for a configuration on its defaults and is "
           "talking to the wrong broker. Log: " << r.log;
    EXPECT_LT(r.exitedAfterMs, kSidecarWaitMs)
        << "the sidecar sat on the deadline instead of answering the message "
           "it had already received. Log: " << r.log;
    EXPECT_TRUE(logCarries(r, "Expected the broker configuration as the first message"))
        << "nothing says the first message was not the configuration. Log: "
        << r.log;
    EXPECT_FALSE(logCarries(r, "maison/salon/store/set"))
        << "the sidecar published the message it refused. Log: " << r.log;
}

/*
 * A configuration cut in two READS is still one configuration.
 *
 * A unix stream promises nothing about where a write is cut, and this is the
 * one message whose loss is silent: the sidecar would give up on its deadline
 * and the relaunch loop would turn for ever with the server writing the
 * configuration correctly every time. Nothing driven through ExternProcServer
 * can say this - it only ever offers whole messages - so the frame is written
 * by hand, split INSIDE its five byte header and again inside its payload.
 *
 * ⚠️ The two-chunk split is asserted to be a real one before it is used: a cut
 * that fell outside the frame, or a frame short enough to have no inside,
 * would make this case green while measuring one write.
 */
TEST_F(MqttSidecarConfigWaitTest, AConfigurationCutAcrossTwoReadsIsStillAssembled)
{
    int port = 0;
    const int listener = openLoopbackListener(port);
    ASSERT_GE(listener, 0) << "could not open a loopback listener";

    const std::string wire = frameOf(MqttWire::encodeConfig(brokerParams(port)));
    const std::string::size_type cut = 3;   //inside the 5 byte header

    ASSERT_GT(wire.size(), cut + 1)
        << "the frame is too short to be cut inside";
    ASSERT_EQ(0x21, static_cast<unsigned char>(wire[0]))
        << "the hand written frame does not carry the opcode IO/ExternProc.cpp "
           "expects, so a green here would mean the sidecar ignored it";

    std::vector<std::string> chunks;
    chunks.push_back(wire.substr(0, cut));
    chunks.push_back(wire.substr(cut));

    const SidecarRun r = runSidecarRaw(chunks, kSidecarWaitMs + 2000);

    EXPECT_FALSE(r.exited)
        << "the sidecar left after " << r.exitedAfterMs
        << " ms although its whole configuration had been written: a "
           "configuration cut between two reads is lost, and the relaunch loop "
           "that follows is silent on the server side. Log: " << r.log;
    EXPECT_FALSE(logCarries(r, "waiting for its configuration"))
        << "the sidecar gave up on its deadline, i.e. it never assembled the "
           "two halves of the frame. Log: " << r.log;
    EXPECT_TRUE(logCarries(r, "Connect to : 127.0.0.1"))
        << "nothing says the configuration was applied at all. Log: " << r.log;
    EXPECT_FALSE(logCarries(r, "mot de passe"))
        << "the sidecar published the configuration it assembled. Log: " << r.log;

    ::close(listener);
}

/*
 * ⭐⭐ AN UNREACHABLE BROKER - the stock case of a box whose broker is simply
 * not switched on.
 *
 * connect_async() answers SUCCESS here: the TCP connect is only STARTED, and
 * it fails afterwards, inside libmosquitto, which closes its socket. The
 * descriptor the sidecar handed to its main loop is stale from that instant.
 *
 * ⚠️ THE FIXTURE IS CHECKED BEFORE IT IS BELIEVED. A documentation address
 * (RFC 5737 TEST-NET-1) can fail either way depending on the machine's
 * routing: synchronously, inside connect_async(), which is the OTHER path and
 * is already loud on master; or asynchronously, which is this one. The line
 * that connect_async() prints when it answered success is what tells them
 * apart, so it is asserted first - without it this case could go green while
 * measuring the path it is not about.
 *
 * RED on master: exit status 0, and a journal whose only line is that same
 * "Connect to :". The server relaunches with no backoff and prints
 * "process exited, restarting..." at WARNING every turn, so what an operator
 * gets is ten lines a second that never say why.
 */
TEST_F(MqttSidecarConfigWaitTest, AnUnreachableBrokerEndsTheSidecarWithACauseAndANonZeroStatus)
{
    const std::string wire = frameOf(MqttWire::encodeConfig(brokerParamsAt("192.0.2.42", 1883)));

    const SidecarRun r = runSidecarRaw(std::vector<std::string>(1, wire), kSidecarWaitMs + 3000);

    ASSERT_TRUE(logCarries(r, "Connect to : 192.0.2.42"))
        << "connect_async() did not answer success on this machine, so the "
           "failure was synchronous and this case is measuring the wrong path. "
           "Log: " << r.log;

    ASSERT_TRUE(r.exited)
        << "the sidecar stayed alive with a broker it never reached. Log: " << r.log;
    EXPECT_GT(r.exitCode, 0)
        << "the sidecar left with status " << r.exitCode
        << ": calaos_server cannot tell this from a clean shutdown, and neither "
           "can whoever reads the journal. Log: " << r.log;
    EXPECT_TRUE(logCarries(r, "Lost the connection to the broker"))
        << "nothing in the journal names the broker as the reason the sidecar "
           "left. Log: " << r.log;
    EXPECT_FALSE(logCarries(r, "mot de passe"))
        << "the sidecar published its broker password while reporting the "
           "failure. Log: " << r.log;
}

/*
 * ⭐ A REFUSED PORT - and the message must name the refusal.
 *
 * This one fails INSIDE connect_async(), which answers MOSQ_ERR_ERRNO. That
 * code is 14, and reading it with ::strerror() - the errno table - gives
 * EFAULT, "Bad address": a diagnosis that sends the operator looking for a
 * memory fault instead of a broker that is not listening.
 *
 * RED on master on the message alone: the status was already non zero here,
 * which is precisely why this path was never the one that hurt.
 */
TEST_F(MqttSidecarConfigWaitTest, ARefusedPortIsNamedARefusalAndNotAnUnrelatedErrno)
{
    const int port = closedLoopbackPort();
    ASSERT_GT(port, 0) << "could not reserve a loopback port";

    const std::string wire = frameOf(MqttWire::encodeConfig(brokerParamsAt("127.0.0.1", port)));

    const SidecarRun r = runSidecarRaw(std::vector<std::string>(1, wire), kSidecarWaitMs + 3000);

    ASSERT_TRUE(r.exited)
        << "the sidecar stayed alive on a refused port. Log: " << r.log;
    ASSERT_TRUE(logCarries(r, "Error connecting"))
        << "the sidecar never reached the connection error at all, so what "
           "this case asserts below would say nothing. Log: " << r.log;

    EXPECT_GT(r.exitCode, 0)
        << "a broker that refuses the connection is not a clean shutdown. Log: "
        << r.log;
    EXPECT_TRUE(logCarries(r, "Connection refused"))
        << "the journal does not name the refusal. Log: " << r.log;
    EXPECT_FALSE(logCarries(r, "Bad address"))
        << "the journal blames a memory fault for a broker that is not "
           "listening: the mosquitto return code was read with the errno "
           "table. Log: " << r.log;
    EXPECT_FALSE(logCarries(r, "mot de passe"))
        << "the sidecar published its broker password while reporting the "
           "failure. Log: " << r.log;
}

/*
 * ⭐⭐ THE BROKER GOES AWAY MID SESSION - the frequent one in production, and
 * the one no ticket had looked at.
 *
 * The session is real as far as this end is concerned: the fixture reads the
 * CONNECT packet and answers a CONNACK, so libmosquitto is connected, then it
 * drops the connection. libmosquitto closes its socket exactly as it does for
 * a connect that failed late, and the sidecar is left with the same stale
 * descriptor - which is why this case and the unreachable one above must both
 * be here: they are the same defect reached from two different states.
 *
 * ⚠️ ANTI VACUITY IN THE FIXTURE, NOT IN THE LOG: what proves the session
 * existed is that the broker saw a CONNECT and sent its CONNACK, and both are
 * asserted. Reading it from the sidecar's journal instead would need the
 * DEBUG level, i.e. a different haystack from the one every other case here
 * measures.
 *
 * RED on master: exit status 0, not one line about the broker.
 */
TEST_F(MqttSidecarConfigWaitTest, ABrokerThatDropsMidSessionEndsTheSidecarWithACauseAndANonZeroStatus)
{
    FakeBroker broker;
    ASSERT_TRUE(openFakeBroker(broker)) << "could not open the fake broker";

    const std::string wire =
        frameOf(MqttWire::encodeConfig(brokerParamsAt("127.0.0.1", broker.port)));

    const SidecarRun r =
        runSidecarRaw(std::vector<std::string>(1, wire), kSidecarWaitMs + 3000, &broker);

    EXPECT_TRUE(broker.sawConnect)
        << "the fake broker never received a CONNECT, so no session was ever "
           "established and this case is measuring a connect failure. Log: "
        << r.log;
    ASSERT_TRUE(broker.sentConnack)
        << "the fake broker never answered a CONNACK, so nothing here says "
           "what happens to an ESTABLISHED session. Log: " << r.log;
    ASSERT_TRUE(broker.dropped)
        << "the fake broker never dropped the connection. Log: " << r.log;

    ASSERT_TRUE(r.exited)
        << "the sidecar kept running with a broker that had gone away: it "
           "polls a descriptor libmosquitto has closed and nothing reconnects. "
           "Log: " << r.log;
    EXPECT_FALSE(logCarries(r, "waiting for its configuration"))
        << "the sidecar left on its configuration deadline, so it never got as "
           "far as the broker. Log: " << r.log;

    EXPECT_GT(r.exitCode, 0)
        << "the sidecar left with status " << r.exitCode
        << " after its broker went away, which calaos_server cannot tell from "
           "a clean shutdown. Log: " << r.log;
    EXPECT_TRUE(logCarries(r, "Lost the connection to the broker"))
        << "nothing in the journal names the broker as the reason the sidecar "
           "left. Log: " << r.log;
    EXPECT_FALSE(logCarries(r, "mot de passe"))
        << "the sidecar published its broker password while reporting the "
           "failure. Log: " << r.log;

    closeFakeBroker(broker);
}

/*
 * ⭐ A BROKER THAT REFUSES THE CREDENTIALS SAYS SO, AT A LEVEL A STOCK INSTALL
 * PRINTS.
 *
 * The CONNACK return code is not an errno and never was. Read with the errno
 * table, code 5 - "not authorised", i.e. the wrong broker password, the single
 * likeliest configuration mistake here - came out as "Input/output error", and
 * it came out at DEBUG, so on a stock install it came out not at all.
 *
 * ⚠️ This case says nothing about the exit status on purpose: what a refused
 * CONNACK must produce is a NAME, and keeping the two subjects apart is what
 * makes a mutation of either table land on one case rather than on all of them.
 */
TEST_F(MqttSidecarConfigWaitTest, ABrokerRefusingTheCredentialsNamesTheRefusal)
{
    FakeBroker broker;
    ASSERT_TRUE(openFakeBroker(broker)) << "could not open the fake broker";
    broker.connackRc = 5;           //not authorised
    broker.holdMs = 4000;           //the sidecar leaves on its own, long before

    const std::string wire =
        frameOf(MqttWire::encodeConfig(brokerParamsAt("127.0.0.1", broker.port)));

    const SidecarRun r =
        runSidecarRaw(std::vector<std::string>(1, wire), kSidecarWaitMs + 3000, &broker);

    ASSERT_TRUE(broker.sentConnack)
        << "the fake broker never answered a CONNACK, so nothing here was "
           "refused at all. Log: " << r.log;

    EXPECT_TRUE(logCarries(r, "The broker refused the connection"))
        << "a broker that turned the credentials down left no trace an "
           "operator can read. Log: " << r.log;
    EXPECT_TRUE(logCarries(r, "not authorised"))
        << "the journal does not say WHICH refusal the broker answered, which "
           "is the difference between a wrong password and a broker that does "
           "not speak this protocol version. Log: " << r.log;
    EXPECT_FALSE(logCarries(r, "Input/output error"))
        << "the CONNACK code was read with the errno table. Log: " << r.log;
    EXPECT_FALSE(logCarries(r, "mot de passe"))
        << "the sidecar published its broker password while reporting the "
           "refusal. Log: " << r.log;

    closeFakeBroker(broker);
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
