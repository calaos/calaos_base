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
 * WHAT A PARENT LEARNS FROM A SIDECAR WHOSE MAIN LOOP FAILED.
 *
 * ExternProcClient::run() answers false when it left on a failed select(),
 * which is never a normal end. What a parent can act on is not that bool but
 * the child's EXIT STATUS, and the two are joined only by the sidecar's
 * procMain(). A case that called procMain() and read its return would prove
 * nothing here, since the whole defect is that the value went nowhere: so
 * every case below execs the SHIPPED BINARY, serves the socket a controller
 * would serve, and reads back what waitpid() gives the parent.
 *
 * WHY AN INTERPOSED select(). No sidecar but calaos_mqtt ever calls
 * appendFd(), so the only descriptor their loop watches is the server socket,
 * and a server can only close it - which is an ORDERLY end. The failing branch
 * is therefore unreachable from outside the process, and
 * core/selectfail_preload.c is what makes it reachable without touching a line
 * of production code. It announces its injection on stderr, so an
 * interposition that failed to load cannot pass for a sidecar that survived.
 *
 * THE TWO ENDINGS ARE TOLD APART BY THE STATUS, NEVER BY A LOG LINE. Each
 * family is run twice against the same binary - once ending on a failed loop,
 * once on a server that hangs up - and what is asserted is that the first
 * status is non zero and the second is zero. A case reading the journal would
 * stay green on a build that changed nothing about the status.
 *
 * WHY calaos_mqtt IS NOT HERE, THOUGH IT IS THE SIXTH SIDECAR. Its setup()
 * waits for its configuration in a select() loop of its own, so an interposed
 * select() that fails from the first call makes it leave BEFORE procMain() is
 * ever entered. A case built that way would have read a non-zero status that
 * says nothing about the main loop - it was measured doing exactly that - and
 * a sensor that measures nothing passes for one that finds nothing. What
 * calaos_mqtt does with its configuration and its broker is pinned by
 * core/MqttSidecarConfigWait_test, which is also where its own `ok? 0: 1`
 * belongs - nothing holds it today.
 *
 * WHAT THIS DOES NOT PROVE: that a select() of a shipped sidecar can fail in
 * production. It cannot today - none of the five registers a descriptor of its
 * own - so what is pinned is that the loop's answer reaches the status, never
 * that anything makes the loop answer false on a real installation.
 *
 * Bounded on the wall clock, never on iterations: a regression must be able to
 * fail a case, never to hang `make check`.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <link.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern char **environ;

namespace
{

#ifndef CALAOS_SELECTFAIL_PRELOAD
#error "CALAOS_SELECTFAIL_PRELOAD must be passed by the build (see tests/Makefile.am)"
#endif

const char kInjectionMarker[] = "calaos-selectfail: EBADF injected";

//How a run ends: on a main loop that failed, or on a server that hung up.
enum Ending
{
    EndOnFailedLoop,
    EndOnServerHangup
};

/*
 * calaos_ola refuses to start when olad does not answer, and would then never
 * reach run() at all. StreamingClient::Setup() only opens a TCP connection, so
 * a listener that never accepts is enough - the kernel backlog completes the
 * handshake. The port is olad's own and cannot be chosen: a container that
 * really runs olad answers on it instead, which is just as good, so a failed
 * bind is not an error here.
 */
int openFakeOlad()
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    const int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in addr;
    ::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    addr.sin_port = ::htons(9010);

    if (::bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        ::listen(fd, 8) != 0)
    {
        ::close(fd);
        return -1;
    }

    return fd;
}

struct SidecarRun
{
    bool spawned = false;
    bool connected = false;
    bool exited = false;
    int exitCode = -1;
    int termSignal = 0;
    std::string log;
};

bool waitReadable(int fd, int budgetMs)
{
    struct timeval tv;
    tv.tv_sec = budgetMs / 1000;
    tv.tv_usec = (budgetMs % 1000) * 1000;

    fd_set r;
    FD_ZERO(&r);
    FD_SET(fd, &r);

    return ::select(fd + 1, &r, NULL, NULL, &tv) > 0;
}

/*
 * A sidecar built with a shared ASan runtime refuses to start unless that
 * runtime comes first among its libraries, and LD_PRELOAD comes before its own
 * dependencies: the runtime this suite runs under is preloaded ahead of the
 * interposition. Empty when there is none.
 */
std::string sanitizerRuntime()
{
    std::string path;
    ::dl_iterate_phdr([](struct dl_phdr_info *info, size_t, void *data) -> int
                      {
        const char *name = info->dlpi_name;
        if (!name || !::strstr(name, "/libasan.so"))
            return 0;
        *static_cast<std::string *>(data) = name;
        return 1;
    },
                      &path);
    return path;
}

/*
 * The child's environment is built by hand rather than inherited with a
 * setenv(): the point of the injection is that it applies to THIS exec and to
 * nothing else in the suite.
 */
std::vector<char *> childEnv(std::vector<std::string> &storage, bool inject)
{
    for (char **e = environ; e && *e; e++)
    {
        if (::strncmp(*e, "LD_PRELOAD=", 11) == 0 ||
            ::strncmp(*e, "CALAOS_SELECT_FAIL_AFTER=", 25) == 0)
            continue;
        storage.push_back(*e);
    }

    if (inject)
    {
        const std::string runtime = sanitizerRuntime();
        storage.push_back(std::string("LD_PRELOAD=") +
                          (runtime.empty() ? "" : runtime + ":") +
                          CALAOS_SELECTFAIL_PRELOAD);
        storage.push_back("CALAOS_SELECT_FAIL_AFTER=0");
    }

    std::vector<char *> out;
    for (std::size_t i = 0; i < storage.size(); i++)
        out.push_back(const_cast<char *>(storage[i].c_str()));
    out.push_back(NULL);

    return out;
}

SidecarRun runSidecar(const std::string &binary, const std::string &ns,
                      const std::vector<std::string> &extraArgs,
                      bool fakeOlad, Ending ending, int budgetMs)
{
    SidecarRun out;

    const int olad = fakeOlad? openFakeOlad(): -1;

    char dir[] = "/tmp/calaos_loopexitXXXXXX";
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
        if (olad >= 0) ::close(olad);
        ::rmdir(dir);
        return out;
    }

    int pipefd[2];
    if (::pipe(pipefd) != 0)
    {
        ::close(srv);
        if (olad >= 0) ::close(olad);
        ::unlink(sockpath.c_str());
        ::rmdir(dir);
        return out;
    }

    std::vector<std::string> argStorage;
    argStorage.push_back(binary);
    argStorage.push_back("--socket");
    argStorage.push_back(sockpath);
    argStorage.push_back("--namespace");
    argStorage.push_back(ns);
    for (std::size_t i = 0; i < extraArgs.size(); i++)
        argStorage.push_back(extraArgs[i]);

    std::vector<char *> argv;
    for (std::size_t i = 0; i < argStorage.size(); i++)
        argv.push_back(const_cast<char *>(argStorage[i].c_str()));
    argv.push_back(NULL);

    std::vector<std::string> envStorage;
    std::vector<char *> envp = childEnv(envStorage, ending == EndOnFailedLoop);

    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    const pid_t pid = ::fork();
    if (pid == 0)
    {
        ::close(pipefd[0]);
        ::dup2(pipefd[1], 1);
        ::dup2(pipefd[1], 2);
        ::execve(binary.c_str(), argv.data(), envp.data());
        ::_exit(127);
    }
    ::close(pipefd[1]);

    if (pid < 0)
    {
        ::close(pipefd[0]);
        ::close(srv);
        if (olad >= 0) ::close(olad);
        ::unlink(sockpath.c_str());
        ::rmdir(dir);
        return out;
    }
    out.spawned = true;

    int cli = -1;
    if (waitReadable(srv, budgetMs))
        cli = ::accept(srv, NULL, NULL);
    out.connected = cli >= 0;

    /*
     * The orderly end a server can really produce: it hangs up, the sidecar's
     * recv() answers 0 and the loop leaves by its normal door.
     * HALF closed, not closed: calaos_1wire sends its first reading before
     * entering the loop, and a fully closed peer answers that send with a RST,
     * so the sidecar dies of SIGPIPE and has no status left to measure.
     */
    if (ending == EndOnServerHangup && cli >= 0)
    {
        ::shutdown(cli, SHUT_WR);
        const int cflags = ::fcntl(cli, F_GETFL, 0);
        ::fcntl(cli, F_SETFL, cflags | O_NONBLOCK);
    }

    //Drain while waiting, so a sidecar that fills the pipe fails the case
    //instead of deadlocking it.
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

        //Drained and dropped: what a sidecar says on the wire is not an oracle
        //here, but a full socket would block its send() instead of its loop.
        if (cli >= 0)
        {
            char sink[4096];
            while (::recv(cli, sink, sizeof(sink), MSG_DONTWAIT) > 0)
                ;
        }

        if (::waitpid(pid, &status, WNOHANG) == pid)
        {
            out.exited = true;
            out.exitCode = WIFEXITED(status)? WEXITSTATUS(status): -1;
            out.termSignal = WIFSIGNALED(status)? WTERMSIG(status): 0;
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
    if (olad >= 0) ::close(olad);
    ::unlink(sockpath.c_str());
    ::rmdir(dir);

    return out;
}

bool sidecarPresent(const std::string &binary)
{
    return ::access(binary.c_str(), X_OK) == 0;
}

const int kBudgetMs = 20000;

struct Family
{
    const char *binary;
    const char *ns;
    std::vector<std::string> args;
    bool fakeOlad;
};

Family script() { return Family{ CALAOS_SCRIPT_SIDECAR, "script", {}, false }; }
Family wago() { return Family{ CALAOS_WAGO_SIDECAR, "wago", {}, false }; }
//--use-w1 keeps setup() away from owfs, which no build machine has.
Family onewire() { return Family{ CALAOS_1WIRE_SIDECAR, "1wire", { "--use-w1" }, false }; }
#ifdef CALAOS_KNX_SIDECAR
Family knx() { return Family{ CALAOS_KNX_SIDECAR, "knx", {}, false }; }
#endif
#ifdef CALAOS_OLA_SIDECAR
Family ola() { return Family{ CALAOS_OLA_SIDECAR, "ola", {}, true }; }
#endif

void expectFailedLoopIsReported(const Family &f)
{
    if (!sidecarPresent(f.binary))
        GTEST_SKIP() << "not built in this configuration: " << f.binary;

    const SidecarRun run = runSidecar(f.binary, f.ns, f.args, f.fakeOlad,
                                      EndOnFailedLoop, kBudgetMs);

    ASSERT_TRUE(run.spawned) << f.binary;
    ASSERT_TRUE(run.connected) << "the sidecar never reached the socket: " << run.log;
    //Stated apart from the status: a sidecar still running at the deadline has
    //no status at all, and "no status" must never read as "status zero".
    ASSERT_TRUE(run.exited) << "still running after " << kBudgetMs << "ms: " << run.log;
    ASSERT_EQ(0, run.termSignal) << run.log;
    ASSERT_NE(std::string::npos, run.log.find(kInjectionMarker))
            << "select() was never made to fail, so nothing was measured: " << run.log;

    EXPECT_NE(0, run.exitCode) << run.log;
}

void expectOrderlyEndIsZero(const Family &f)
{
    if (!sidecarPresent(f.binary))
        GTEST_SKIP() << "not built in this configuration: " << f.binary;

    const SidecarRun run = runSidecar(f.binary, f.ns, f.args, f.fakeOlad,
                                      EndOnServerHangup, kBudgetMs);

    ASSERT_TRUE(run.spawned) << f.binary;
    ASSERT_TRUE(run.connected) << "the sidecar never reached the socket: " << run.log;
    ASSERT_TRUE(run.exited) << "still running after " << kBudgetMs << "ms: " << run.log;
    ASSERT_EQ(0, run.termSignal) << run.log;
    ASSERT_EQ(std::string::npos, run.log.find(kInjectionMarker))
            << "no injection was asked for here: " << run.log;

    EXPECT_EQ(0, run.exitCode) << run.log;
}

}

TEST(SidecarLoopExitStatus, AScriptSidecarThatLeftOnAFailedLoopExitsNonZero)
{
    expectFailedLoopIsReported(script());
}

TEST(SidecarLoopExitStatus, AScriptSidecarWhoseServerHungUpExitsZero)
{
    expectOrderlyEndIsZero(script());
}

TEST(SidecarLoopExitStatus, AWagoSidecarThatLeftOnAFailedLoopExitsNonZero)
{
    expectFailedLoopIsReported(wago());
}

TEST(SidecarLoopExitStatus, AWagoSidecarWhoseServerHungUpExitsZero)
{
    expectOrderlyEndIsZero(wago());
}

TEST(SidecarLoopExitStatus, AOneWireSidecarThatLeftOnAFailedLoopExitsNonZero)
{
    expectFailedLoopIsReported(onewire());
}

TEST(SidecarLoopExitStatus, AOneWireSidecarWhoseServerHungUpExitsZero)
{
    expectOrderlyEndIsZero(onewire());
}

#ifdef CALAOS_KNX_SIDECAR
TEST(SidecarLoopExitStatus, AKnxSidecarThatLeftOnAFailedLoopExitsNonZero)
{
    expectFailedLoopIsReported(knx());
}

TEST(SidecarLoopExitStatus, AKnxSidecarWhoseServerHungUpExitsZero)
{
    expectOrderlyEndIsZero(knx());
}
#endif

#ifdef CALAOS_OLA_SIDECAR
TEST(SidecarLoopExitStatus, AnOlaSidecarThatLeftOnAFailedLoopExitsNonZero)
{
    expectFailedLoopIsReported(ola());
}

TEST(SidecarLoopExitStatus, AnOlaSidecarWhoseServerHungUpExitsZero)
{
    expectOrderlyEndIsZero(ola());
}
#endif

//The discriminator itself, on one binary and one pair of runs: same
//executable, same arguments, two endings, and nothing but the status is read.
TEST(SidecarLoopExitStatus, TheTwoEndingsOfOneSidecarAreToldApartByTheStatusAlone)
{
    const Family f = script();
    if (!sidecarPresent(f.binary))
        GTEST_SKIP() << "not built in this configuration: " << f.binary;

    const SidecarRun failed = runSidecar(f.binary, f.ns, f.args, f.fakeOlad,
                                        EndOnFailedLoop, kBudgetMs);
    const SidecarRun orderly = runSidecar(f.binary, f.ns, f.args, f.fakeOlad,
                                          EndOnServerHangup, kBudgetMs);

    ASSERT_TRUE(failed.exited) << failed.log;
    ASSERT_TRUE(orderly.exited) << orderly.log;

    EXPECT_EQ(0, orderly.exitCode) << orderly.log;
    EXPECT_NE(orderly.exitCode, failed.exitCode) << failed.log;
}

//Guards the sensor, not the code under test: without this, an interposition
//that never loaded would make every non-zero expectation above fail for a
//reason no message names.
TEST(SidecarLoopExitStatus, TheInterpositionIsOnlyThereWhenItIsAskedFor)
{
    const Family f = script();
    if (!sidecarPresent(f.binary))
        GTEST_SKIP() << "not built in this configuration: " << f.binary;

    const SidecarRun injected = runSidecar(f.binary, f.ns, f.args, f.fakeOlad,
                                          EndOnFailedLoop, kBudgetMs);
    const SidecarRun plain = runSidecar(f.binary, f.ns, f.args, f.fakeOlad,
                                        EndOnServerHangup, kBudgetMs);

    EXPECT_NE(std::string::npos, injected.log.find(kInjectionMarker)) << injected.log;
    EXPECT_EQ(std::string::npos, plain.log.find(kInjectionMarker)) << plain.log;
}
