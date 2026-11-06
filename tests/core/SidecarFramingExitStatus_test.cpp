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
 * WHAT A PARENT LEARNS WHEN THE BYTES IT SENT WERE NOT A FRAME.
 *
 * The parent writes five bytes - 0x21 and a length above the 4 MiB cap - on the
 * socket a controller serves. The sidecar's framing state machine flags the
 * error, its main loop leaves, and what is read here is the EXIT STATUS the
 * parent gets from waitpid(). No log line is an oracle: a build that changed
 * nothing about the status would keep every message it prints.
 *
 * WHY THIS PATH AND NOT AN INTERPOSED select(). The failing select() of
 * core/SidecarLoopExitStatus_test needs an LD_PRELOAD because no shipped
 * sidecar registers a descriptor of its own, so nothing outside the process can
 * make that branch run. Here the input comes from the PARENT, over the socket
 * the sidecar is built to read: the path is reachable with production code
 * untouched, and it is the one an installation can actually take.
 *
 * TWO OPPOSITE ENDINGS, AND BOTH ARE LOAD BEARING. Not every way out of the
 * loop is a failure: a server that hangs up is how a sidecar is meant to stop,
 * and it must still exit zero. A fix that answered "failed" on every exit would
 * feed the respawn ramp with sidecars that did nothing wrong, so each family is
 * run twice against the same binary - invalid frame, then hangup - and the pair
 * is asserted together. The pair also guards the reader itself: a status reader
 * stuck on one value fails one half or the other, whichever value it is stuck
 * on.
 *
 * THE SENSOR SAYS WHETHER IT MEASURED ANYTHING. A sidecar that had already died
 * before the five bytes were written would exit for a reason this file never
 * chose, and would pass for a sidecar that answered the frame. So the invalid
 * frame cases assert that the child was still alive when the write happened and
 * that all five bytes left, and the hangup cases assert that nothing at all was
 * written - the two behaviours are told apart by the harness, not assumed.
 *
 * NO RUNTIME SKIP. calaos_script, calaos_wago and calaos_1wire are built
 * unconditionally, and the two conditional families are compiled in exactly
 * where their binary is built (same automake condition). A missing binary is
 * therefore a broken build, and it reddens here instead of excusing itself.
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

//0x21 is TypeMessage; 0xFFFFFFFF is an announced payload far above the cap the
//framing state machine enforces.
const char kInvalidFrame[] = "\x21\xFF\xFF\xFF\xFF";
const std::size_t kInvalidFrameLen = 5;

//What the parent does once the sidecar has connected.
enum ParentBehaviour
{
    SendInvalidFrame,
    HangUp
};

/*
 * calaos_ola refuses to start when olad does not answer, and would then never
 * reach run() at all. StreamingClient::Setup() only opens a TCP connection, so
 * a listener that never accepts is enough. The port is olad's own and cannot be
 * chosen: a machine that really runs olad answers on it instead, which serves
 * as well, so a failed bind is not an error here.
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
    bool aliveWhenParentSpoke = false;
    ssize_t bytesSent = 0;
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
 * Built by hand rather than inherited: an LD_PRELOAD left over from another
 * suite of this tree would put a second reason to die in this child, and the
 * status would no longer name the frame.
 */
std::vector<char *> childEnv(std::vector<std::string> &storage)
{
    for (char **e = environ; e && *e; e++)
    {
        if (::strncmp(*e, "LD_PRELOAD=", 11) == 0 ||
            ::strncmp(*e, "CALAOS_SELECT_FAIL_AFTER=", 25) == 0)
            continue;
        storage.push_back(*e);
    }

    std::vector<char *> out;
    for (std::size_t i = 0; i < storage.size(); i++)
        out.push_back(const_cast<char *>(storage[i].c_str()));
    out.push_back(NULL);

    return out;
}

SidecarRun runSidecar(const std::string &binary, const std::string &ns,
                      const std::vector<std::string> &extraArgs,
                      bool fakeOlad, ParentBehaviour behaviour, int budgetMs)
{
    SidecarRun out;

    const int olad = fakeOlad? openFakeOlad(): -1;

    char dir[] = "/tmp/calaos_framingXXXXXX";
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
    std::vector<char *> envp = childEnv(envStorage);

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

    if (cli >= 0)
    {
        //Read before the parent speaks, so that "the child was already gone"
        //can never be reported as "the child answered the frame".
        int probe = 0;
        out.aliveWhenParentSpoke = ::waitpid(pid, &probe, WNOHANG) == 0;

        if (behaviour == SendInvalidFrame)
        {
            //Socket kept open on purpose: the only thing the sidecar can leave
            //on here is the frame, never an end of stream.
            out.bytesSent = ::send(cli, kInvalidFrame, kInvalidFrameLen, MSG_NOSIGNAL);
        }
        else
        {
            /*
             * HALF closed, not closed: calaos_1wire sends its first reading
             * before entering the loop, and a fully closed peer answers that
             * send with a RST, so the sidecar dies of SIGPIPE and has no status
             * left to measure.
             */
            ::shutdown(cli, SHUT_WR);
        }

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

void expectInvalidFrameIsReported(const Family &f)
{
    ASSERT_EQ(0, ::access(f.binary, X_OK))
            << "built unconditionally, or compiled in only where it is built: "
            << f.binary;

    const SidecarRun run = runSidecar(f.binary, f.ns, f.args, f.fakeOlad,
                                      SendInvalidFrame, kBudgetMs);

    ASSERT_TRUE(run.spawned) << f.binary;
    ASSERT_TRUE(run.connected) << "the sidecar never reached the socket: " << run.log;
    ASSERT_TRUE(run.aliveWhenParentSpoke)
            << "it was already gone before the frame was written, so the frame "
               "measured nothing: " << run.log;
    ASSERT_EQ((ssize_t)kInvalidFrameLen, run.bytesSent)
            << "the frame never left the parent: " << run.log;
    //Stated apart from the status: a sidecar still running at the deadline has
    //no status at all, and "no status" must never read as "status zero".
    ASSERT_TRUE(run.exited) << "still running after " << kBudgetMs << "ms: " << run.log;
    ASSERT_EQ(0, run.termSignal) << run.log;

    EXPECT_NE(0, run.exitCode) << run.log;
}

void expectHangupIsZero(const Family &f)
{
    ASSERT_EQ(0, ::access(f.binary, X_OK))
            << "built unconditionally, or compiled in only where it is built: "
            << f.binary;

    const SidecarRun run = runSidecar(f.binary, f.ns, f.args, f.fakeOlad,
                                      HangUp, kBudgetMs);

    ASSERT_TRUE(run.spawned) << f.binary;
    ASSERT_TRUE(run.connected) << "the sidecar never reached the socket: " << run.log;
    ASSERT_EQ(0, run.bytesSent) << "nothing is written on this ending: " << run.log;
    ASSERT_TRUE(run.exited) << "still running after " << kBudgetMs << "ms: " << run.log;
    ASSERT_EQ(0, run.termSignal) << run.log;

    EXPECT_EQ(0, run.exitCode) << run.log;
}

}

TEST(SidecarFramingExitStatus, AScriptSidecarSentAnInvalidFrameExitsNonZero)
{
    expectInvalidFrameIsReported(script());
}

TEST(SidecarFramingExitStatus, AScriptSidecarWhoseServerHungUpExitsZero)
{
    expectHangupIsZero(script());
}

TEST(SidecarFramingExitStatus, AWagoSidecarSentAnInvalidFrameExitsNonZero)
{
    expectInvalidFrameIsReported(wago());
}

TEST(SidecarFramingExitStatus, AWagoSidecarWhoseServerHungUpExitsZero)
{
    expectHangupIsZero(wago());
}

TEST(SidecarFramingExitStatus, AOneWireSidecarSentAnInvalidFrameExitsNonZero)
{
    expectInvalidFrameIsReported(onewire());
}

TEST(SidecarFramingExitStatus, AOneWireSidecarWhoseServerHungUpExitsZero)
{
    expectHangupIsZero(onewire());
}

#ifdef CALAOS_KNX_SIDECAR
TEST(SidecarFramingExitStatus, AKnxSidecarSentAnInvalidFrameExitsNonZero)
{
    expectInvalidFrameIsReported(knx());
}

TEST(SidecarFramingExitStatus, AKnxSidecarWhoseServerHungUpExitsZero)
{
    expectHangupIsZero(knx());
}
#endif

#ifdef CALAOS_OLA_SIDECAR
TEST(SidecarFramingExitStatus, AnOlaSidecarSentAnInvalidFrameExitsNonZero)
{
    expectInvalidFrameIsReported(ola());
}

TEST(SidecarFramingExitStatus, AnOlaSidecarWhoseServerHungUpExitsZero)
{
    expectHangupIsZero(ola());
}
#endif

/*
 * The discriminator, on one binary and one pair of runs: same executable, same
 * arguments, two things the parent can do, and nothing but the status read.
 * This is what a fix answering "failed" on every way out of the loop breaks -
 * it would make the orderly half non zero and the two statuses equal again.
 */
TEST(SidecarFramingExitStatus, TheTwoEndingsOfOneSidecarAreToldApartByTheStatusAlone)
{
    const Family f = script();
    ASSERT_EQ(0, ::access(f.binary, X_OK)) << f.binary;

    const SidecarRun framed = runSidecar(f.binary, f.ns, f.args, f.fakeOlad,
                                         SendInvalidFrame, kBudgetMs);
    const SidecarRun orderly = runSidecar(f.binary, f.ns, f.args, f.fakeOlad,
                                          HangUp, kBudgetMs);

    ASSERT_TRUE(framed.exited) << framed.log;
    ASSERT_TRUE(orderly.exited) << orderly.log;
    ASSERT_EQ((ssize_t)kInvalidFrameLen, framed.bytesSent) << framed.log;
    ASSERT_EQ(0, orderly.bytesSent) << orderly.log;

    EXPECT_EQ(0, orderly.exitCode) << orderly.log;
    EXPECT_NE(orderly.exitCode, framed.exitCode) << framed.log;
}
