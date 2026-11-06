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
 * THE TWO LISTENS THAT NOBODY READ BACK: THE HIFI ROSE NOTIFICATION PORT AND
 * THE SIDECAR PIPE.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS MEASURED, AND WHERE IT IS READ
 * ---------------------------------------------------------------------------
 * Everything below is read off the DESCRIPTOR: the listening socket is found
 * by SO_ACCEPTCONN among /proc/self/fd, and its family, address and port come
 * from getsockname(). Nothing asks the library where it believes it bound -
 * the whole defect is that the library's answer and the kernel's differ, and
 * that the constructor's own log line only ever quoted the library.
 *
 * The log is read too, but never alone: a case that only read the log would
 * pass on a server that prints the right sentence and listens nowhere, which
 * is precisely the shipped behaviour these cases were written against.
 *
 * ---------------------------------------------------------------------------
 * WHY EACH CASE IS A SEPARATE PROCESS
 * ---------------------------------------------------------------------------
 * Both objects bind at construction on a process-wide libuv loop, and the
 * notification port is a compile-time constant: one process can only measure
 * one value of listen_address. Each case is therefore a fork() that configures
 * itself, builds the object, reads its own descriptors and reports.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS SUITE ASSUMES OF THE MACHINE, out loud, because a case green for
 * the wrong reason is worse than a red one:
 *   - the kernel has IPv6 and ::1 is on the loopback;
 *   - 192.0.2.1 (RFC 5737) is NOT an address of this machine - a machine that
 *     had configured it would turn the absent-address case into a measurement
 *     of nothing;
 *   - port 9284 is free. It is hard-coded in the production code, so a machine
 *     already holding it would make every notification case measure another
 *     process's socket.
 * main() proves all three and exits 77 - a visible SKIP - otherwise.
 *
 * ⚠️ THE PIPE FAILURE IS PROVOKED BY DESCRIPTOR EXHAUSTION, one of the three
 * causes the ticket names; a full or read-only /tmp reaches the same call the
 * same way but is not what runs here. What is measured is that the refusal is
 * heard and named - not the list of things that can produce it.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "AVRRoseNotifServer.h"
#include "ExternProc.h"
#include "Logger.h"
#include "libuvw.h"

namespace
{

//The port AVRRoseNotifServer has hard-coded. Repeated here on purpose: if the
//production constant moves, TheNotificationPortIsTheOneTheDeviceIsToldToUse
//goes red instead of the suite quietly measuring another port.
const int kNotifPort = 9284;

//Documentation range (RFC 5737): well formed, routable-looking, and not meant
//to exist anywhere. main() proves it does not exist HERE.
const char *const kAbsentV4 = "192.0.2.1";
//Neither family, and not a host name this code is allowed to resolve either.
const char *const kUnreadable = "nonsense-typo";


std::string workDir;

struct Outcome
{
    //A child that died in its constructor writes no answer, and an unwritten
    //answer parses as zero listeners - which is what half of these cases
    //expect. Both facts are therefore reported and asserted before anything
    //is read from the numbers.
    bool exited = false;
    bool answered = false;
    //Number of listening sockets the child owned. Anything but the expected
    //count and the rest of the line describes the wrong socket.
    int listeners = 0;
    int family = 0;
    std::string addr;
    int port = 0;
    std::string log;
};

bool addressExistsHere(int family, const char *literal)
{
    const int fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    bool ok = false;
    if (family == AF_INET)
    {
        sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = 0;
        ok = inet_pton(AF_INET, literal, &a.sin_addr) == 1 &&
             bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0;
    }
    else
    {
        sockaddr_in6 a;
        memset(&a, 0, sizeof(a));
        a.sin6_family = AF_INET6;
        a.sin6_port = 0;
        ok = inet_pton(AF_INET6, literal, &a.sin6_addr) == 1 &&
             bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0;
    }
    close(fd);
    return ok;
}

//Holds 0.0.0.0:port the way a second calaos_server or a neighbour would.
//Returns -1 when the port could not be taken, which main() reads as "this
//machine cannot host the measurement".
int holdTcpPort(int port)
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)port);
    if (bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0 ||
        listen(fd, 1) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

//The listening sockets of this process, taken from the kernel. SO_ACCEPTCONN
//names them without ambiguity, and getsockname() says where they really are.
//`want` selects the family family: AF_UNIX for the sidecar pipe, anything else
//for the two IP families of the notification port.
void describeListeners(bool wantUnix, int &listeners, int &family,
                       std::string &addr, int &port)
{
    listeners = 0;
    family = 0;
    port = 0;
    addr.clear();

    DIR *d = opendir("/proc/self/fd");
    if (!d)
        return;

    const int skip = dirfd(d);
    struct dirent *e;
    while ((e = readdir(d)) != nullptr)
    {
        const int fd = atoi(e->d_name);
        if (fd <= 0 || fd == skip)
            continue;

        int type = 0;
        socklen_t len = sizeof(type);
        if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) != 0 ||
            type != SOCK_STREAM)
            continue;

        int accepting = 0;
        len = sizeof(accepting);
        if (getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &accepting, &len) != 0 ||
            !accepting)
            continue;

        sockaddr_storage ss;
        socklen_t sl = sizeof(ss);
        memset(&ss, 0, sizeof(ss));
        if (getsockname(fd, reinterpret_cast<sockaddr *>(&ss), &sl) != 0)
            continue;

        char text[INET6_ADDRSTRLEN] = { 0 };
        std::string found;
        int foundPort = 0;

        if (ss.ss_family == AF_UNIX)
        {
            if (!wantUnix)
                continue;
            const sockaddr_un *a = reinterpret_cast<const sockaddr_un *>(&ss);
            //An abstract or unnamed socket has no path to compare: reported as
            //empty rather than as a byte salad.
            if (sl > (socklen_t)offsetof(sockaddr_un, sun_path) && a->sun_path[0] != '\0')
                found = a->sun_path;
        }
        else if (ss.ss_family == AF_INET6)
        {
            if (wantUnix)
                continue;
            const sockaddr_in6 *a = reinterpret_cast<const sockaddr_in6 *>(&ss);
            inet_ntop(AF_INET6, &a->sin6_addr, text, sizeof(text));
            found = text;
            foundPort = ntohs(a->sin6_port);
        }
        else if (ss.ss_family == AF_INET)
        {
            if (wantUnix)
                continue;
            const sockaddr_in *a = reinterpret_cast<const sockaddr_in *>(&ss);
            inet_ntop(AF_INET, &a->sin_addr, text, sizeof(text));
            found = text;
            foundPort = ntohs(a->sin_port);
        }
        else
            continue;

        family = ss.ss_family;
        addr = found;
        port = foundPort;
        listeners++;
    }
    closedir(d);
}

std::string slurp(const std::string &path)
{
    std::ifstream in(path.c_str());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

//Everything a child does before it builds anything: its own config directory,
//its own log file on stdout, debug level 5 so every domain prints.
void prepareChild(const std::string &base, const std::string &logPath)
{
    const std::string cfg = base + "/config";
    const std::string cache = base + "/cache";

    const int lf = open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (lf >= 0)
    {
        dup2(lf, STDOUT_FILENO);
        close(lf);
    }

    Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                             const_cast<char *>(cache.c_str()), true);
    Utils::set_config_option("debug_level", "5");
}

//`occupy` holds the notification port before the child starts, the way a
//second server or a neighbour would: the one path where the listen cannot
//land anywhere at all.
Outcome measureNotif(const std::string &listenAddress, int tag, bool occupy = false)
{
    Outcome r;

    const std::string base = workDir + "/notif" + std::to_string(tag);
    const std::string logPath = base + "/log";
    const std::string outPath = base + "/out";
    ::mkdir(base.c_str(), 0700);
    ::mkdir((base + "/config").c_str(), 0700);
    ::mkdir((base + "/cache").c_str(), 0700);

    int squat = -1;
    if (occupy)
    {
        squat = holdTcpPort(kNotifPort);
        if (squat < 0)
        {
            r.listeners = -1;
            return r;
        }
    }

    const pid_t pid = fork();
    if (pid == 0)
    {
        if (squat >= 0)
            close(squat);

        prepareChild(base, logPath);
        Utils::set_config_option("listen_address", listenAddress);

        //Never destroyed: the descriptors have to still be open when they are
        //read, and this process exits immediately afterwards.
        new Calaos::AVRRoseNotifServer();
        std::cout.flush();

        int listeners = 0, family = 0, port = 0;
        std::string addr;
        describeListeners(false, listeners, family, addr, port);

        FILE *o = fopen(outPath.c_str(), "w");
        if (o)
        {
            fprintf(o, "%d %d %s %d\n", listeners, family,
                    addr.empty()? "-": addr.c_str(), port);
            fclose(o);
        }
        _exit(0);
    }

    if (pid < 0)
    {
        if (squat >= 0)
            close(squat);
        r.listeners = -1;
        return r;
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (squat >= 0)
        close(squat);

    r.exited = WIFEXITED(status) && WEXITSTATUS(status) == 0;

    std::istringstream in(slurp(outPath));
    std::string addr;
    r.answered = bool(in >> r.listeners >> r.family >> addr >> r.port);
    if (addr != "-")
        r.addr = addr;
    r.log = slurp(logPath);
    return r;
}

//⚠️ The default loop is held in a shared_ptr for as long as the child needs
//it: uvw closes the loop when the last handle on it goes, and a rebuilt one
//is not usable. A warm-up call whose result is dropped crashes what follows.
//
//`starve` leaves the child with no free descriptor at all, which is one of the
//three causes the ticket names for a pipe that cannot listen. Everything that
//needs a descriptor of its own - the libuv loop, the log, the result file - is
//opened BEFORE the starvation, so the only call that can fail is the socket()
//behind uv_pipe_bind.
Outcome measurePipe(const std::string &prefix, int tag, bool starve = false)
{
    Outcome r;

    const std::string base = workDir + "/pipe" + std::to_string(tag);
    const std::string logPath = base + "/log";
    const std::string outPath = base + "/out";
    ::mkdir(base.c_str(), 0700);
    ::mkdir((base + "/config").c_str(), 0700);
    ::mkdir((base + "/cache").c_str(), 0700);

    const pid_t pid = fork();
    if (pid == 0)
    {
        prepareChild(base, logPath);

        std::shared_ptr<uvw::Loop> keepLoop;
        std::vector<int> hogs;
        if (starve)
        {
            //The event loop and the logger both allocate on first use.
            keepLoop = uvw::Loop::getDefault();
            cDebugDom("process") << "descriptors about to be exhausted";
            std::cout.flush();

            rlimit rl;
            rl.rlim_cur = 64;
            rl.rlim_max = 64;
            setrlimit(RLIMIT_NOFILE, &rl);
            for (;;)
            {
                const int f = dup(STDERR_FILENO);
                if (f < 0)
                    break;
                hogs.push_back(f);
            }
        }

        //Never destroyed: ~ExternProcServer closes the handle and unlinks the
        //socket file, and this process exits immediately after reading it.
        new ExternProcServer(prefix);

        //Room to read the descriptors back and to write the answer down.
        for (int i = 0; i < 8 && !hogs.empty(); i++)
        {
            close(hogs.back());
            hogs.pop_back();
        }
        std::cout.flush();

        int listeners = 0, family = 0, port = 0;
        std::string addr;
        describeListeners(true, listeners, family, addr, port);

        FILE *o = fopen(outPath.c_str(), "w");
        if (o)
        {
            fprintf(o, "%d %d %s %d\n", listeners, family,
                    addr.empty()? "-": addr.c_str(), port);
            fclose(o);
        }
        _exit(0);
    }

    if (pid < 0)
    {
        r.listeners = -1;
        return r;
    }

    int status = 0;
    waitpid(pid, &status, 0);

    r.exited = WIFEXITED(status) && WEXITSTATUS(status) == 0;

    std::istringstream in(slurp(outPath));
    std::string addr;
    r.answered = bool(in >> r.listeners >> r.family >> addr >> r.port);
    if (addr != "-")
        r.addr = addr;
    r.log = slurp(logPath);
    return r;
}

struct Runs
{
    Outcome wildcard, loopback4, loopback6, unreadable, absent4, taken;
    Outcome pipeOk, pipeRefused;
};

const Runs &theRuns()
{
    static Runs runs = []()
    {
        Runs r;
        r.wildcard = measureNotif("0.0.0.0", 0);
        r.loopback4 = measureNotif("127.0.0.1", 1);
        r.loopback6 = measureNotif("::1", 2);
        r.unreadable = measureNotif(kUnreadable, 3);
        r.absent4 = measureNotif(kAbsentV4, 4);
        r.taken = measureNotif("0.0.0.0", 5, true);
        r.pipeOk = measurePipe("calaos_wago", 0);
        r.pipeRefused = measurePipe("calaos_wago", 1, true);
        return r;
    }();
    return runs;
}

//The one phrase every widening carries, shared with the API and discovery
//servers so a log can be searched for it once.
const char *const kWidened = "listening on 0.0.0.0 (every interface) instead";

//What the notification server announces when it did get its port.
const char *const kNotifSuccess = "Push notification server listening on";

//What it must say instead when it did not.
const char *const kNotifDeaf = "no push notification can be received";

//What the sidecar pipe must say when it could not listen.
const char *const kPipeDeaf = "cannot listen on";

} //namespace

/*
 * THE FIXTURE, BEFORE ANYTHING IS CONCLUDED FROM IT. Every case below reads a
 * family, an address and a port off one socket; a child with none, or with
 * two, would still give every case something to compare.
 */
TEST(NotifAndPipeListen, EveryChildRanToTheEndAndWroteItsAnswer)
{
    const Runs &r = theRuns();

    const Outcome *all[] = { &r.wildcard, &r.loopback4, &r.loopback6,
                             &r.unreadable, &r.absent4, &r.taken,
                             &r.pipeOk, &r.pipeRefused };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
    {
        EXPECT_TRUE(all[i]->exited) << "child " << i << " did not exit cleanly";
        EXPECT_TRUE(all[i]->answered) << "child " << i << " wrote no answer";
    }
}

TEST(NotifAndPipeListen, EveryChildOwnedTheListenerItsCaseDescribes)
{
    const Runs &r = theRuns();

    EXPECT_EQ(1, r.wildcard.listeners);
    EXPECT_EQ(1, r.loopback4.listeners);
    EXPECT_EQ(1, r.loopback6.listeners);
    EXPECT_EQ(1, r.unreadable.listeners);
    EXPECT_EQ(1, r.absent4.listeners);
    EXPECT_EQ(1, r.pipeOk.listeners);

    //The two children that could not bind at all own none.
    EXPECT_EQ(0, r.taken.listeners);
    EXPECT_EQ(0, r.pipeRefused.listeners);
}

/*
 * THE COUNTERWEIGHT, AND THE HALF THAT MUST NOT MOVE. The shipped default is
 * every interface, and it is what an installation with an amplifier on the LAN
 * depends on: confining the listen must be something the operator ASKED for,
 * never the new default.
 */
TEST(NotifAndPipeListen, TheDefaultListenStaysOnEveryInterface)
{
    const Runs &r = theRuns();

    EXPECT_EQ(AF_INET, r.wildcard.family);
    EXPECT_EQ("0.0.0.0", r.wildcard.addr);
    EXPECT_EQ(std::string::npos, r.wildcard.log.find(kWidened))
            << "the shipped default was reported as a fallback";
}

/*
 * THE PORT IS THE HALF OF THE ADDRESS THE AMPLIFIER IS TOLD TO USE. It is a
 * compile-time constant of the production code and the device is registered
 * against it; a listen that landed anywhere else would be unreachable whatever
 * address it carries.
 */
TEST(NotifAndPipeListen, TheNotificationPortIsTheOneTheDeviceIsToldToUse)
{
    const Runs &r = theRuns();

    EXPECT_EQ(kNotifPort, r.wildcard.port);
    EXPECT_EQ(kNotifPort, r.loopback4.port);
    EXPECT_EQ(kNotifPort, r.loopback6.port);
    EXPECT_EQ(kNotifPort, r.unreadable.port);
    EXPECT_EQ(kNotifPort, r.absent4.port);
}

/*
 * THE DEFECT ITSELF: an operator who narrowed the listen got the notification
 * port on every interface anyway, and no line said so. Read off the socket,
 * because the constructor's own line never named an address at all.
 */
TEST(NotifAndPipeListen, ANarrowedListenAddressConfinesTheNotificationPort)
{
    const Runs &r = theRuns();

    EXPECT_EQ(AF_INET, r.loopback4.family);
    EXPECT_EQ("127.0.0.1", r.loopback4.addr)
            << "listen_address was narrowed and the notification port is still "
               "open on every interface";

    EXPECT_EQ(AF_INET6, r.loopback6.family);
    EXPECT_EQ("::1", r.loopback6.addr);
}

/*
 * AND THE CONFINEMENT IS SAID OUT LOUD, because it is the one place where
 * honouring listen_address COSTS something: a HiFi Rose amplifier pushes TO
 * this port from the LAN, so a listen confined away from it silently demotes
 * push notifications to the 30 s fallback poll. That trade is the operator's
 * to make - but only if they are told they made it.
 */
TEST(NotifAndPipeListen, AConfinedNotificationListenNamesTheAddressItBound)
{
    const Runs &r = theRuns();

    EXPECT_NE(std::string::npos, r.loopback4.log.find("127.0.0.1"))
            << "the notification listen was confined without naming where to";
    EXPECT_NE(std::string::npos, r.loopback6.log.find("::1"));

    //The address is in the success line itself, not in some earlier warning.
    const size_t line = r.loopback4.log.find(kNotifSuccess);
    ASSERT_NE(std::string::npos, line);
    const size_t end = r.loopback4.log.find('\n', line);
    ASSERT_NE(std::string::npos, end);
    EXPECT_NE(std::string::npos,
              r.loopback4.log.substr(line, end - line).find("127.0.0.1"))
            << "the line that announces the listen does not say where it is";
}

/*
 * THE TWO UNUSABLE FORMS WIDEN LIKE THE API PORT, on the same port and for the
 * same reason: a typo must not turn a home automation box into a brick. What
 * changes is that the refused value is named.
 */
TEST(NotifAndPipeListen, AnUnusableListenAddressWidensAndSaysWhichValueItRefused)
{
    const Runs &r = theRuns();

    EXPECT_EQ(AF_INET, r.unreadable.family);
    EXPECT_EQ("0.0.0.0", r.unreadable.addr);
    EXPECT_NE(std::string::npos, r.unreadable.log.find(kUnreadable));
    EXPECT_NE(std::string::npos, r.unreadable.log.find(kWidened));

    EXPECT_EQ(AF_INET, r.absent4.family);
    EXPECT_EQ("0.0.0.0", r.absent4.addr);
    EXPECT_NE(std::string::npos, r.absent4.log.find(kAbsentV4));
    EXPECT_NE(std::string::npos, r.absent4.log.find(kWidened));
}

/*
 * THE SECOND DEFECT, AND THE ONE THE FICHE ASSERTED: the ErrorEvent listener
 * was subscribed AFTER bind and after listen, so a port already held was
 * published to nobody - and the constructor announced the listen anyway. The
 * socket says there is none.
 */
TEST(NotifAndPipeListen, ANotificationPortThatCouldNotBeBoundIsNotAnnounced)
{
    const Runs &r = theRuns();

    EXPECT_EQ(0, r.taken.listeners)
            << "the port was held, yet a listening socket appeared";

    EXPECT_EQ(std::string::npos, r.taken.log.find(kNotifSuccess))
            << "the constructor announced a listen this process does not have";
    EXPECT_NE(std::string::npos, r.taken.log.find(kNotifDeaf))
            << "the notification port was lost without a word";
}

/*
 * THE SIDECAR PIPE, THE HALF WITH NO LISTENER AT ALL. This is the counterweight
 * for the case below: an ordinary prefix must still produce one listening unix
 * socket, under the path the helper is handed on its command line.
 */
TEST(NotifAndPipeListen, AnOrdinarySidecarPipeListensUnderTheAdvertisedPath)
{
    const Runs &r = theRuns();

    EXPECT_EQ(AF_UNIX, r.pipeOk.family);
    EXPECT_EQ(0u, r.pipeOk.addr.rfind("/tmp/calaos_proc_", 0))
            << "the pipe listened somewhere the helper is not told about";
    EXPECT_NE(std::string::npos, r.pipeOk.addr.find("calaos_wago"));
}

/*
 * AND ITS DEFECT: bind() then listen() with no ErrorEvent subscribed anywhere.
 * A refusal leaves the pipe deaf, every sidecar of that family unable to
 * connect, and the only trace was a debug line announcing the very path
 * nothing is listening on.
 */
TEST(NotifAndPipeListen, ASidecarPipeThatCouldNotListenSaysSo)
{
    const Runs &r = theRuns();

    EXPECT_EQ(0, r.pipeRefused.listeners)
            << "the name was refused, yet a listening socket appeared";
    EXPECT_NE(std::string::npos, r.pipeRefused.log.find(kPipeDeaf))
            << "the sidecar pipe was lost without a word";
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_notiflisten_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
    {
        std::cerr << "could not create a work directory" << std::endl;
        return 1;
    }
    workDir = base;

    //Said out loud rather than left to cases that would then measure nothing.
    const int probe = socket(AF_INET6, SOCK_STREAM, 0);
    if (probe < 0)
    {
        std::cerr << "no IPv6 on this machine, nothing to measure" << std::endl;
        return 77;
    }
    close(probe);

    if (!addressExistsHere(AF_INET, "127.0.0.1") ||
        !addressExistsHere(AF_INET6, "::1"))
    {
        std::cerr << "no loopback address to bind, nothing to measure" << std::endl;
        return 77;
    }

    if (addressExistsHere(AF_INET, kAbsentV4))
    {
        std::cerr << "a documentation address is configured on this machine, "
                     "the absent-address case would measure nothing" << std::endl;
        return 77;
    }

    //The notification port is hard-coded in the production code: a machine
    //already holding it would have every case measure a foreign socket.
    const int held = holdTcpPort(kNotifPort);
    if (held < 0)
    {
        std::cerr << "port " << kNotifPort << " is already in use here, "
                     "every notification case would measure another process"
                  << std::endl;
        return 77;
    }
    close(held);

    return RUN_ALL_TESTS();
}
