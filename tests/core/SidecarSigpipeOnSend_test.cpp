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
 * WHAT HAPPENS TO A SIDECAR THAT WRITES TO A PEER WHICH IS NO LONGER LISTENING.
 *
 * A send() without MSG_NOSIGNAL raises SIGPIPE, whose default action is to
 * kill. No sidecar installs a handler, and the disposition calaos_server sets
 * for itself does not travel: uv_spawn resets every one of them to SIG_DFL in
 * the child. So the sidecar dies of signal 13 on an ordinary write, and the
 * respawn ramp - which counts any signal but its own SIGTERM as a crash -
 * reads that as a crash.
 *
 * WHY shutdown(SHUT_RD) AND NOT close(). Closing the peer is a RACE, and one
 * that the read side usually wins: close() makes the sidecar's recv() answer
 * ECONNRESET at the same moment it makes its send() fail, and whichever lands
 * first decides how the run ends (measured on this tree: 20 closes, 20 runs
 * ended by the read side). shutdown(SHUT_RD) on the server end sets
 * SEND_SHUTDOWN on the sidecar's socket and nothing else: its recv() never
 * reports an end of stream, so the write is the ONLY door left, and the ending
 * is the same every time.
 *
 * WHY calaos_1wire, AND ONLY IT. It is the one shipped sidecar whose loop
 * writes unprompted - readTimeout() sends a reading every second, and
 * procMain() sends one more before entering the loop. calaos_wago's
 * readTimeout() only reconnects, calaos_ola's is empty, calaos_knx's polls
 * knxd; those three write only in answer to a frame, which is another
 * exchange and another suite. Nothing here is 1wire-specific in the code under
 * test: sendMessage() belongs to ExternProcClient and all six share it.
 *
 * THE WRITE THAT FAILS BEFORE THE LOOP IS EVER ENTERED is a case of its own, and
 * the one the fix exists for: procMain() sends a first reading before calling
 * run(), so a peer already deaf when that send happens leaves no loop to fail in
 * later. Reaching it means deafening the peer while the sidecar is provably
 * still inside connect() - a filler connection on a backlog of zero parks it
 * there, and /proc says so. That peer also hangs up, so a sidecar that entered
 * the loop despite the failed write leaves by the orderly door and exits zero.
 *
 * THE ENDING IS READ FROM waitpid(), AND THE DOOR FROM THE SIDECAR'S OWN LOG.
 * The status alone cannot say whether a non-zero exit came from the write or
 * from somewhere else, so each case also checks which of the two lines the
 * sidecar printed, and that the other one is absent. A run that measured nothing
 * must not read as a run that found nothing: the cases that need the sidecar to
 * have written first count the bytes and fail without them, and the case that
 * needs it NOT to have written yet fails unless /proc showed it parked.
 *
 * NO EXCLUSION GUARD, DELIBERATELY. calaos_1wire is built in every
 * configuration - no automake condition guards it - so a missing binary is a
 * broken tree, not a configuration to skip over, and it is asserted as such.
 * --use-w1 keeps setup() away from owfs, which no build machine has; the
 * reading it then sends is an empty list, which is a frame like any other.
 *
 * Bounded on the wall clock, never on iterations.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

#ifndef CALAOS_1WIRE_SIDECAR
#error "CALAOS_1WIRE_SIDECAR must be passed by the build (see tests/Makefile.am)"
#endif

//What the server does to the connection once the sidecar has spoken once.
enum PeerBehaviour
{
    GoDeaf,      //shutdown(SHUT_RD): the sidecar's writes fail, its reads do not
    HangUp,      //shutdown(SHUT_WR): the orderly stop, the counterweight here
    KeepReading, //nothing: the control, where the sidecar must still be alive
    DeafOnArrival//deaf AND hung up before the sidecar has written anything at all
};

//The two doors, named by what the sidecar prints when it leaves through one.
const char kWriteFailed[] = "Error writing to socket";
const char kServerHungUp[] = "Server closed the connection";

struct SidecarRun
{
    bool spawned = false;
    bool parkedInConnect = false;
    bool connected = false;
    bool exited = false;
    int exitCode = -1;
    int termSignal = 0;
    ssize_t bytesBeforeBehaviour = 0;
    std::string log;
};

//Where the sidecar is, read from the kernel and not guessed from a clock: a
//child parked inside connect() has provably not written yet, which is the one
//moment at which the peer can be made deaf ahead of the very first send().
bool parkedInConnect(pid_t pid)
{
    char path[64];
    ::snprintf(path, sizeof(path), "/proc/%d/syscall", (int)pid);

    const int fd = ::open(path, O_RDONLY);
    if (fd < 0)
        return false;

    char buf[128];
    const ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    if (n <= 0)
        return false;
    buf[n] = '\0';

    return ::strtol(buf, NULL, 10) == SYS_connect;
}

int elapsedMs(const std::chrono::steady_clock::time_point &t0)
{
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0).count();
}

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

SidecarRun runSidecar(PeerBehaviour behaviour, int budgetMs)
{
    SidecarRun out;

    char dir[] = "/tmp/calaos_sigpipeXXXXXX";
    if (::mkdtemp(dir) == NULL)
        return out;
    const std::string sockpath = std::string(dir) + "/s";

    const int srv = ::socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    ::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    ::strncpy(addr.sun_path, sockpath.c_str(), sizeof(addr.sun_path) - 1);
    //A backlog of zero already holds one connection too many once the filler
    //below is in: the sidecar's connect() then blocks instead of returning.
    if (srv < 0 || ::bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        ::listen(srv, behaviour == DeafOnArrival? 0: 1) != 0)
    {
        if (srv >= 0) ::close(srv);
        ::rmdir(dir);
        return out;
    }

    int filler = -1;
    if (behaviour == DeafOnArrival)
    {
        filler = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (filler < 0 || ::connect(filler, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        {
            if (filler >= 0) ::close(filler);
            ::close(srv);
            ::unlink(sockpath.c_str());
            ::rmdir(dir);
            return out;
        }
    }

    int pipefd[2];
    if (::pipe(pipefd) != 0)
    {
        if (filler >= 0) ::close(filler);
        ::close(srv);
        ::unlink(sockpath.c_str());
        ::rmdir(dir);
        return out;
    }

    const char *binary = CALAOS_1WIRE_SIDECAR;
    std::vector<std::string> argStorage;
    argStorage.push_back(binary);
    argStorage.push_back("--socket");
    argStorage.push_back(sockpath);
    argStorage.push_back("--namespace");
    argStorage.push_back("1wire");
    argStorage.push_back("--use-w1");

    std::vector<char *> argv;
    for (std::size_t i = 0; i < argStorage.size(); i++)
        argv.push_back(const_cast<char *>(argStorage[i].c_str()));
    argv.push_back(NULL);

    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    const pid_t pid = ::fork();
    if (pid == 0)
    {
        ::close(pipefd[0]);
        ::dup2(pipefd[1], 1);
        ::dup2(pipefd[1], 2);
        ::execv(binary, argv.data());
        ::_exit(127);
    }
    ::close(pipefd[1]);

    if (pid < 0)
    {
        ::close(pipefd[0]);
        if (filler >= 0) ::close(filler);
        ::close(srv);
        ::unlink(sockpath.c_str());
        ::rmdir(dir);
        return out;
    }
    out.spawned = true;

    int cli = -1;
    if (behaviour == DeafOnArrival)
    {
        while (!out.parkedInConnect && elapsedMs(t0) < budgetMs)
        {
            if (parkedInConnect(pid))
                out.parkedInConnect = true;
            else
                ::usleep(200);
        }

        //Accepting the filler is what hands the queue slot back, so from here
        //the sidecar is on its way out of connect(): the two accepts spin
        //rather than sleep, and the peer is deaf before the sidecar runs again.
        ::fcntl(srv, F_SETFL, ::fcntl(srv, F_GETFL, 0) | O_NONBLOCK);

        int queued = -1;
        while (queued < 0 && elapsedMs(t0) < budgetMs)
            queued = ::accept(srv, NULL, NULL);
        while (cli < 0 && elapsedMs(t0) < budgetMs)
            cli = ::accept(srv, NULL, NULL);

        //SHUT_RDWR, and not the SHUT_RD of the other cases: the deaf half makes
        //the pending first write fail, and the hung-up half gives a sidecar that
        //reached the loop anyway a door to leave by with a status of zero. It is
        //not the close() of the ticket - no reset, no ECONNRESET to race with.
        if (cli >= 0)
            ::shutdown(cli, SHUT_RDWR);
        if (queued >= 0)
            ::close(queued);
    }
    else if (waitReadable(srv, budgetMs))
        cli = ::accept(srv, NULL, NULL);
    out.connected = cli >= 0;

    const int logFlags = ::fcntl(pipefd[0], F_GETFL, 0);
    ::fcntl(pipefd[0], F_SETFL, logFlags | O_NONBLOCK);

    /*
     * Waiting for the sidecar to speak BEFORE changing the connection is what
     * makes the rest of the run mean something: a peer that goes deaf before
     * anything was ever written measures the startup, not the write.
     */
    if (cli >= 0 && behaviour != DeafOnArrival)
    {
        const int cliFlags = ::fcntl(cli, F_GETFL, 0);
        ::fcntl(cli, F_SETFL, cliFlags | O_NONBLOCK);

        while (out.bytesBeforeBehaviour == 0 && elapsedMs(t0) < budgetMs / 2)
        {
            char sink[4096];
            const ssize_t n = ::recv(cli, sink, sizeof(sink), MSG_DONTWAIT);
            if (n > 0)
                out.bytesBeforeBehaviour += n;
            else
                ::usleep(5 * 1000);
        }

        if (behaviour == GoDeaf)
            ::shutdown(cli, SHUT_RD);
        else if (behaviour == HangUp)
            ::shutdown(cli, SHUT_WR);
    }

    int status = 0;
    while (elapsedMs(t0) < budgetMs)
    {
        char buf[4096];
        const ssize_t n = ::read(pipefd[0], buf, sizeof(buf));
        if (n > 0)
            out.log.append(buf, buf + n);

        //Drained and dropped: a full socket would stall the sidecar's send()
        //instead of letting it fail, which is the opposite of what is measured.
        //After SHUT_RD this answers EINVAL and the drain is the kernel's.
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
    if (filler >= 0) ::close(filler);
    ::close(srv);
    ::unlink(sockpath.c_str());
    ::rmdir(dir);

    return out;
}

const int kBudgetMs = 20000;
//Two send cycles of calaos_1wire, which is what the control needs to show that
//a sidecar nobody disturbed does not leave on its own.
const int kAliveBudgetMs = 3000;

//Everything a run must satisfy before its status means anything at all.
void assertMeasuredSomething(const SidecarRun &run)
{
    ASSERT_TRUE(run.spawned) << CALAOS_1WIRE_SIDECAR;
    ASSERT_TRUE(run.connected) << "the sidecar never reached the socket: " << run.log;
    ASSERT_LT(0, run.bytesBeforeBehaviour)
            << "the sidecar wrote nothing before the peer changed, so the write "
               "path was never reached: " << run.log;
}

SidecarRun deafRun()
{
    return runSidecar(GoDeaf, kBudgetMs);
}

}

//The defect itself. Signal 13 is what a parent used to read, and what the
//respawn ramp counts as a crash.
TEST(SidecarSigpipeOnSend, AOneWireSidecarIsNotKilledBySignalWhenThePeerStopsReading)
{
    const SidecarRun run = deafRun();

    assertMeasuredSomething(run);
    ASSERT_TRUE(run.exited) << "still running after " << kBudgetMs << "ms: " << run.log;

    EXPECT_NE(SIGPIPE, run.termSignal) << run.log;
    EXPECT_EQ(0, run.termSignal) << run.log;
}

//The other half: killing the sidecar is not fixed by letting it carry on as if
//nothing had happened. A write that failed ends the run, and it ends it as a
//failure - which is what the read side already answers to the very same event,
//a peer that closed giving ECONNRESET there.
TEST(SidecarSigpipeOnSend, AWriteThatFailedIsReportedAsAFailedRun)
{
    const SidecarRun run = deafRun();

    assertMeasuredSomething(run);
    ASSERT_TRUE(run.exited) << "still running after " << kBudgetMs << "ms: " << run.log;
    ASSERT_EQ(0, run.termSignal) << run.log;

    EXPECT_NE(0, run.exitCode) << run.log;
}

//Told apart from the status on purpose: a sidecar that logs the failed write
//and carries on has no status at all, and "no status" must never read as
//"status zero". This is the case that separates a write nobody reported from a
//write that killed the process.
TEST(SidecarSigpipeOnSend, ASidecarWhosePeerWentDeafDoesNotSpinForever)
{
    const SidecarRun run = deafRun();

    assertMeasuredSomething(run);

    EXPECT_TRUE(run.exited) << "still writing into a deaf peer after "
                            << kBudgetMs << "ms: " << run.log;
}

//Which door was taken, not merely that one was: a non-zero status says nothing
//on its own about where it came from.
TEST(SidecarSigpipeOnSend, ADeafPeerIsLeftByTheWriteDoorAndNotByTheReadDoor)
{
    const SidecarRun run = deafRun();

    assertMeasuredSomething(run);
    ASSERT_TRUE(run.exited) << "still running after " << kBudgetMs << "ms: " << run.log;

    EXPECT_NE(std::string::npos, run.log.find(kWriteFailed)) << run.log;
    EXPECT_EQ(std::string::npos, run.log.find(kServerHungUp)) << run.log;
}

//The counterweight. A server that hangs up is how a sidecar is meant to stop,
//and it must still exit zero: a fix answering "failed" on every way out of the
//loop would feed the respawn ramp with sidecars that did nothing wrong.
TEST(SidecarSigpipeOnSend, AServerThatHangsUpIsStillAVoluntaryStop)
{
    const SidecarRun run = runSidecar(HangUp, kBudgetMs);

    assertMeasuredSomething(run);
    ASSERT_TRUE(run.exited) << "still running after " << kBudgetMs << "ms: " << run.log;

    EXPECT_EQ(0, run.termSignal) << run.log;
    EXPECT_EQ(0, run.exitCode) << run.log;
    EXPECT_NE(std::string::npos, run.log.find(kServerHungUp)) << run.log;
    EXPECT_EQ(std::string::npos, run.log.find(kWriteFailed)) << run.log;
}

//The discriminator, on one binary and one pair of runs: same executable, same
//arguments, two peers, and nothing but the status is read.
TEST(SidecarSigpipeOnSend, TheTwoEndingsOfOneSidecarAreToldApartByTheStatusAlone)
{
    const SidecarRun deaf = deafRun();
    const SidecarRun orderly = runSidecar(HangUp, kBudgetMs);

    ASSERT_TRUE(deaf.exited) << deaf.log;
    ASSERT_TRUE(orderly.exited) << orderly.log;

    EXPECT_EQ(0, orderly.termSignal) << orderly.log;
    EXPECT_EQ(0, deaf.termSignal) << deaf.log;
    EXPECT_EQ(0, orderly.exitCode) << orderly.log;
    EXPECT_NE(orderly.exitCode, deaf.exitCode) << deaf.log;
}

//The control. Without it, a sidecar that left on its own - a failed scan, a
//missing library - would answer every expectation above for a reason none of
//them names.
TEST(SidecarSigpipeOnSend, ASidecarWhosePeerKeepsReadingStaysAlive)
{
    const SidecarRun run = runSidecar(KeepReading, kAliveBudgetMs);

    assertMeasuredSomething(run);

    EXPECT_FALSE(run.exited) << "it left on its own after " << kAliveBudgetMs
                             << "ms, so nothing above is caused by the peer: "
                             << run.log;
}

//The sensor, stated as a case of its own: the runs above are only worth
//reading because the sidecar really does write on this socket.
TEST(SidecarSigpipeOnSend, TheSidecarWritesBeforeThePeerIsChanged)
{
    const SidecarRun run = deafRun();

    ASSERT_TRUE(run.connected) << run.log;
    EXPECT_LT(0, run.bytesBeforeBehaviour) << run.log;
}

//The case the fix exists for, and the only one where the reset it moved is
//observable: the first send() fails before run() is entered, so a reset at the
//top of the loop would hand that failure a clean loop to fail in again - and the
//peer's hang-up would then be read as the orderly stop it is not.
TEST(SidecarSigpipeOnSend, AWriteThatFailedBeforeTheLoopDoesNotBecomeAnOrderlyStop)
{
    const SidecarRun run = runSidecar(DeafOnArrival, kBudgetMs);

    ASSERT_TRUE(run.spawned) << CALAOS_1WIRE_SIDECAR;
    ASSERT_TRUE(run.parkedInConnect)
            << "the peer was never made deaf ahead of the first write, so this "
               "run measured something else: " << run.log;
    ASSERT_TRUE(run.connected) << "the sidecar never reached the socket: " << run.log;
    ASSERT_TRUE(run.exited) << "still running after " << kBudgetMs << "ms: " << run.log;

    EXPECT_EQ(0, run.termSignal) << run.log;
    EXPECT_EQ(1, run.exitCode) << run.log;
    EXPECT_NE(std::string::npos, run.log.find(kWriteFailed)) << run.log;
    EXPECT_EQ(std::string::npos, run.log.find(kServerHungUp)) << run.log;
}
