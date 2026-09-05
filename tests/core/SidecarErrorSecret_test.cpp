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
 * WHAT A CONTROLLER REPUBLISHES OF AN ERROR FRAME ITS SIDECAR BUILT.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT
 * ---------------------------------------------------------------------------
 * ReolinkCtrl copied p["message"] into cErrorDom("reolink"). The python end
 * fills that key on its connection failure path with the text of an exception
 * raised by the camera library, by the call that had just authenticated with
 * the camera credentials. Nothing in this tree constrains that text - it
 * crosses a dependency - and ERROR prints on a stock install, one level above
 * the leak T3.81 closed.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS GOES THROUGH A REAL CONTROLLER
 * ---------------------------------------------------------------------------
 * Nothing here calls the message handler. A real ReolinkCtrl is built, a peer
 * connects to the unix socket its ExternProcServer bound - which is all a
 * sidecar is on this wire - and sends framed messages. What is asserted is
 * what the shipped path writes to std::cout, so a value republished under any
 * spelling is caught, and a mutation of IO/Reolink/ReolinkCtrl.cpp turns these
 * cases red.
 *
 * ---------------------------------------------------------------------------
 * THE HALF THAT MUST NOT DIE WITH THE TEXT
 * ---------------------------------------------------------------------------
 * A camera that cannot be reached is a common failure and this line is what
 * says so. Dropping it would pass the first case and blind an operator. The
 * second case is that counterweight: an error must still say that one
 * happened, on which controller, of which nature, and for which camera - all
 * of them values this end can name, none of them text a dependency wrote.
 *
 * WHAT THIS DOES NOT PROVE: no camera and no journal of a real install are
 * involved. The stand-in sidecar is a shell script that sleeps, so the python
 * side of the same wire is not exercised by anything here.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

#include "ConfigStore.h"
#include "ExternProc.h"
#include "LogSetup.h"
#include "Logger.h"
#include "ReolinkCtrl.h"
#include "ReolinkTypes.h"
#include "libuvw.h"

using namespace Calaos;

namespace
{

/*
 * FIXTURE, NOT DECORATION.
 *
 * kVendorText is shaped like what a camera library raises when an
 * authenticated call fails: it names the account and the password it was
 * given, and the url it used. The frame really carrying it is asserted before
 * anything is read back, otherwise a green would mean "the sidecar sent
 * nothing", not "the server withheld it".
 *
 * The needles also sit in `code` and in `hostname`: a fix that only stops
 * copying one key would leave the other two, and they come from the same
 * unconstrained sender.
 */
const char *const kVendorText =
    "Login failed for operateur-camera / mon mot de passe camera at "
    "https://operateur-camera:mon%20mot%20de%20passe@192.168.7.51/cgi-bin/api.cgi";
const char *const kVendorCode = "jeton-de-code-fabrique-par-le-sidecar";
const char *const kVendorHost = "hote-jamais-enregistre-par-ce-controleur";

const char *const kCameraHost     = "192.168.7.51";
const char *const kCameraUser     = "operateur-camera";
const char *const kCameraPassword = "mon mot de passe camera";
const char *const kEventType      = "motion";

//A code of the wire vocabulary. What this end publishes is the code itself:
//a word from a closed list, never text the sender chose.
const char *const kKnownCode = "camera_connect_failed";

const char *const kSidecarName = "calaos_reolink";
const char *const kNamespace   = "reolink";

//The marker of the line under test, and nothing else: it must not be a word
//that also appears on a launch line or on a registration line.
const char *const kErrorMarker = "Reolink process error";

int countOccurrences(const std::string &haystack, const std::string &needle)
{
    int n = 0;
    for (std::string::size_type p = haystack.find(needle);
         p != std::string::npos;
         p = haystack.find(needle, p + needle.size()))
        n++;
    return n;
}

//Whether SOME single line carries all three needles. Looking for them anywhere
//in the log would be satisfied by three unrelated lines.
bool someLineHasAll(const std::string &log, const std::string &a,
                    const std::string &b, const std::string &c)
{
    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line))
    {
        if (line.find(a) != std::string::npos &&
            line.find(b) != std::string::npos &&
            line.find(c) != std::string::npos)
            return true;
    }
    return false;
}

/*
 * WHAT A BOX PRINTS WITH NOBODY TOUCHING ANYTHING, measured in a child.
 *
 * The Logger fills its domain map once and never re-reads it, so a process
 * that has raised the level can no longer observe the default. The child never
 * raises it, and has to be forked before this process owns an event loop or a
 * spawned sidecar to duplicate.
 */
struct DefaultLevelProbe
{
    bool ran = false;
    bool errorPrinted = false;
    bool debugPrinted = false;
};

DefaultLevelProbe &defaultLevelProbe()
{
    static DefaultLevelProbe probe;
    return probe;
}

void measureStockLogLevel()
{
    int fds[2];
    if (::pipe(fds) != 0)
        return;

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close(fds[0]);
        ::close(fds[1]);
        return;
    }

    if (pid == 0)
    {
        ::close(fds[0]);

        unsigned char answer = 0;
        char tmpl[] = "/tmp/calaos_sidecarerror_stock_XXXXXX";
        const char *base = ::mkdtemp(tmpl);
        if (base)
        {
            const std::string cfg = std::string(base) + "/config";
            const std::string cache = std::string(base) + "/cache";
            ::mkdir(cfg.c_str(), 0700);
            ::mkdir(cache.c_str(), 0700);

            //Nothing is set afterwards: this is a stock install.
            Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                                     const_cast<char *>(cache.c_str()), true);

            answer = 0x4;
            if (Utils::calaosLogger(kNamespace)->isLevelEnabled(Logger::LOG_LEVEL_ERROR))
                answer |= 0x1;
            if (Utils::calaosLogger(kNamespace)->isLevelEnabled(Logger::LOG_LEVEL_DEBUG))
                answer |= 0x2;
        }

        if (::write(fds[1], &answer, 1) != 1)
            answer = 0;
        ::close(fds[1]);
        _exit(0);
    }

    ::close(fds[1]);

    unsigned char answer = 0;
    const ssize_t got = ::read(fds[0], &answer, 1);
    ::close(fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    if (got == 1 && (answer & 0x4))
    {
        defaultLevelProbe().ran = true;
        defaultLevelProbe().errorPrinted = (answer & 0x1) != 0;
        defaultLevelProbe().debugPrinted = (answer & 0x2) != 0;
    }
}

//The one sandbox of this process. Empty when mkdtemp() fails, so a case FAILS
//instead of quietly pointing CALAOS_BIN_PREFIX at the real prefix.
const std::string &sandboxDir()
{
    static std::string dir = []() -> std::string
    {
        char tmpl[] = "/tmp/calaos_sidecar_error_XXXXXX";
        const char *d = ::mkdtemp(tmpl);
        return d? std::string(d) : std::string();
    }();

    return dir;
}

std::string pidFilePath()
{
    return sandboxDir() + "/" + kSidecarName + ".pid";
}

/*
 * A STAND-IN THAT STAYS ALIVE. ReolinkCtrl relaunches on every exit and clears
 * its registrations doing so; a sidecar that returned at once would make the
 * observation depend on winning a race. It writes its pid so main() can end
 * it: this process leaves by _exit() and ~ExternProcServer never runs.
 */
bool installSidecarStandIn()
{
    if (sandboxDir().empty())
        return false;

    const std::string script = sandboxDir() + "/" + kSidecarName;

    {
        std::ofstream f(script.c_str(), std::ios::out | std::ios::trunc);
        if (!f.is_open())
            return false;
        f << "#!/bin/sh\n"
             "echo $$ > '" << pidFilePath() << "'\n"
             "exec sleep 120\n";
        if (!f.good())
            return false;
    }

    if (::chmod(script.c_str(), 0755) != 0)
        return false;

    return ::setenv("CALAOS_BIN_PREFIX", sandboxDir().c_str(), 1) == 0;
}

//Run the default loop until pred() holds, with a wall clock deadline: a
//regression must be able to fail a case, never to hang `make check`.
bool pumpLoopUntil(const std::function<bool()> &pred, int timeoutMs)
{
    auto loop = uvw::Loop::getDefault();
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);

    for (;;)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();

        if (pred())
            return true;
        if (std::chrono::steady_clock::now() > deadline)
            return false;

        ::usleep(2000);
    }
}

/*
 * The socket ExternProcServer bound for the reolink namespace.
 *
 * sockpath is private and carries a random uuid, but its shape is fixed and it
 * ends with our own pid, so the suffix cannot pick up a socket belonging to
 * another test binary of the same `make check -jN`.
 */
std::string findReolinkSocket()
{
    const std::string prefix = "calaos_proc_";
    const std::string suffix = std::string("_") + kNamespace + "_" +
                               std::to_string(static_cast<long>(::getpid()));

    DIR *d = ::opendir("/tmp");
    if (!d)
        return std::string();

    std::string found;
    for (struct dirent *e = ::readdir(d); e; e = ::readdir(d))
    {
        const std::string name = e->d_name;
        if (name.size() <= prefix.size() + suffix.size())
            continue;
        if (name.compare(0, prefix.size(), prefix) != 0)
            continue;
        if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;

        found = "/tmp/" + name;
        break;
    }

    ::closedir(d);
    return found;
}

//A sidecar, reduced to what a sidecar is on this wire: a peer of the unix
//socket that speaks the 5 byte header of ExternProcMessage.
class FakeSidecar
{
public:
    bool connectTo(const std::string &path)
    {
        if (path.empty() || path.size() >= sizeof(((struct sockaddr_un *)0)->sun_path))
            return false;

        fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            return false;

        struct sockaddr_un remote;
        remote.sun_family = AF_UNIX;
        ::strncpy(remote.sun_path, path.c_str(), sizeof(remote.sun_path) - 1);
        remote.sun_path[sizeof(remote.sun_path) - 1] = '\0';

        if (::connect(fd, (struct sockaddr *)&remote,
                      ::strlen(remote.sun_path) + sizeof(remote.sun_family)) != 0)
        {
            ::close(fd);
            fd = -1;
            return false;
        }

        return true;
    }

    bool sendFrame(const std::string &payload) const
    {
        if (fd < 0)
            return false;

        std::string frame;
        frame.push_back(static_cast<char>(0x21)); //ExternProcMessage::TypeMessage
        const uint32_t len = static_cast<uint32_t>(payload.size());
        frame.push_back(static_cast<char>(len >> 24));
        frame.push_back(static_cast<char>(len >> 16));
        frame.push_back(static_cast<char>(len >> 8));
        frame.push_back(static_cast<char>(len));
        frame.append(payload);

        return ::send(fd, frame.c_str(), frame.size(), 0) ==
               static_cast<ssize_t>(frame.size());
    }

private:
    int fd = -1;
};

//The two error frames, built once so a case can assert what was really sent
//before it reads back what was printed.
std::string hostileErrorFrame()
{
    return std::string("{\"status\":\"error\",\"code\":\"") + kVendorCode +
           "\",\"hostname\":\"" + kVendorHost +
           "\",\"message\":\"" + kVendorText + "\"}";
}

std::string wellFormedErrorFrame()
{
    return std::string("{\"status\":\"error\",\"code\":\"") + kKnownCode +
           "\",\"hostname\":\"" + kCameraHost + "\"}";
}

struct Observation
{
    bool installed = false;
    bool peerConnected = false;
    bool connectedSeen = false;
    bool bothErrorsSeen = false;
    std::string sockpath;
    std::string log;
};

/*
 * The ReolinkCtrl is a singleton and is never destroyed. Capturing std::cout
 * once and letting every case read the same text also keeps them independent
 * of the order gtest picks.
 */
const Observation &theObservation()
{
    static Observation *obs = nullptr;
    if (obs)
        return *obs;

    obs = new Observation();
    obs->installed = installSidecarStandIn();
    if (!obs->installed)
        return *obs;

    //Redirected by hand rather than through a helper: the pump below has to
    //read what has been written so far, so the sink must stay reachable while
    //the loop runs.
    std::ostringstream sink;
    std::streambuf *saved = std::cout.rdbuf(sink.rdbuf());

    ReolinkCtrl &ctrl = ReolinkCtrl::Instance();

    obs->sockpath = findReolinkSocket();

    static FakeSidecar sidecar;
    obs->peerConnected = sidecar.connectTo(obs->sockpath);
    if (obs->peerConnected)
    {
        //Let the server accept the peer before anything is written to it.
        pumpLoopUntil([]() { return false; }, 300);

        //One camera really registered: it is what makes the hostname of the
        //well formed frame a value this end can vouch for, and the hostname of
        //the hostile frame one it cannot.
        ctrl.registerCamera(ReolinkTypes::Hostname(kCameraHost),
                            ReolinkTypes::Username(kCameraUser),
                            ReolinkTypes::Password(kCameraPassword),
                            ReolinkTypes::EventType(kEventType),
                            [](const ReolinkTypes::Hostname &,
                               const ReolinkTypes::EventType &,
                               const ReolinkTypes::EventData &) {});

        sidecar.sendFrame("{\"status\":\"connected\"}");
        obs->connectedSeen =
            pumpLoopUntil([&ctrl]() { return ctrl.isConnected(); }, 5000);

        if (obs->connectedSeen)
        {
            sidecar.sendFrame(hostileErrorFrame());
            sidecar.sendFrame(wellFormedErrorFrame());

            obs->bothErrorsSeen = pumpLoopUntil([&sink]()
            {
                return countOccurrences(sink.str(), kErrorMarker) >= 2;
            }, 5000);
        }
    }

    std::cout.rdbuf(saved);
    obs->log = sink.str();

    return *obs;
}

} // namespace

class SidecarErrorSecretTest: public ::testing::Test {};

/*
 * THE CASE THIS SUITE EXISTS FOR: nothing the sidecar wrote into an error
 * frame reaches the journal.
 *
 * ANTI-VACUITY FIRST. The reolink domain is asserted printable at ERROR, the
 * frame is asserted to really carry the three needles, and two error lines are
 * required before anything is looked for - a quieter environment or a dropped
 * frame would otherwise make this case green while measuring nothing.
 */
TEST_F(SidecarErrorSecretTest, NothingTheSidecarWroteInAnErrorFrameReachesTheLog)
{
    ASSERT_TRUE(Utils::calaosLogger(kNamespace)->isLevelEnabled(Logger::LOG_LEVEL_ERROR))
        << "the reolink domain is muted below ERROR here, so this case cannot "
           "observe the line it exists to check";

    const std::string frame = hostileErrorFrame();
    const std::string text = kVendorText;
    const std::string code = kVendorCode;
    const std::string host = kVendorHost;

    ASSERT_NE(std::string::npos, frame.find(text))
        << "the frame does not carry the vendor text at all, so a green here "
           "would mean nothing: " << frame;
    ASSERT_NE(std::string::npos, frame.find(code))
        << "the frame does not carry the vendor code at all: " << frame;
    ASSERT_NE(std::string::npos, frame.find(host))
        << "the frame does not carry the unknown hostname at all: " << frame;

    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_FALSE(obs.sockpath.empty())
        << "no ExternProcServer socket was found for the reolink namespace - "
           "the harness is broken, not the code under test";
    ASSERT_TRUE(obs.peerConnected)
        << "the stand-in sidecar could not connect to " << obs.sockpath;
    ASSERT_TRUE(obs.connectedSeen)
        << "the controller never reached its connected state. Log: " << obs.log;
    ASSERT_TRUE(obs.bothErrorsSeen)
        << "fewer than two error lines were written, so this case cannot say "
           "whether a secret would have been: " << obs.log;

    EXPECT_EQ(std::string::npos, obs.log.find(text))
        << "the text the sidecar built is in the journal. It is the message of "
           "an exception raised by the camera library, on the call that had "
           "just authenticated, and ERROR prints on a stock install. Log: "
        << obs.log;
    EXPECT_EQ(std::string::npos, obs.log.find(std::string(kCameraPassword)))
        << "the camera PASSWORD is in the journal: the vendor text carries it "
           "and a truncated republication would leak it just as well. Log: "
        << obs.log;
    EXPECT_EQ(std::string::npos, obs.log.find(code))
        << "a code this end does not know is republished verbatim: the sender "
           "is then free to choose what the journal says. Log: " << obs.log;
    EXPECT_EQ(std::string::npos, obs.log.find(host))
        << "a hostname no camera of this controller was registered under is "
           "republished verbatim. Log: " << obs.log;
}

/*
 * THE COUNTERWEIGHT: deleting the line would also pass the case above.
 *
 * A camera that stops answering is a common failure and this is the line that
 * reports it. What must survive is the kind of error, the camera it happened
 * on, and the fact that something was withheld - the byte count is compared to
 * the text really sent, because a number nobody compares to anything is not a
 * measurement.
 */
TEST_F(SidecarErrorSecretTest, TheLogStillSaysWhichCameraFailedAndOfWhatKind)
{
    ASSERT_TRUE(Utils::calaosLogger(kNamespace)->isLevelEnabled(Logger::LOG_LEVEL_ERROR))
        << "the reolink domain is muted below ERROR here";

    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_TRUE(obs.bothErrorsSeen)
        << "no error was reported at all, so this case cannot say whether the "
           "diagnosis survives. Log: " << obs.log;

    EXPECT_TRUE(someLineHasAll(obs.log, kErrorMarker, kNamespace, kKnownCode))
        << "the error line does not say which controller failed and of what "
           "kind: an unreachable camera becomes indistinguishable from any "
           "other failure. Log: " << obs.log;
    EXPECT_TRUE(someLineHasAll(obs.log, kErrorMarker, kKnownCode, kCameraHost))
        << "the error line does not name the camera it happened on, although "
           "this controller registered it itself. Log: " << obs.log;

    const std::string withheld = std::to_string(std::string(kVendorText).size());
    EXPECT_TRUE(someLineHasAll(obs.log, kErrorMarker, "unspecified", withheld))
        << "the line written for the frame this end could not name does not "
           "say that " << withheld << " bytes were dropped: a sidecar sending "
           "prose and one sending nothing look the same. Log: " << obs.log;
}

/*
 * THE LEVEL THIS TICKET RESTS ON, MEASURED INSTEAD OF ASSERTED.
 *
 * "This line prints on a stock install" is the whole severity, and this
 * process raises the level in its own main() and can therefore not see it. A
 * child that never raised it is asked instead. The DEBUG half is the contrast:
 * it says the ERROR half is not an artefact of a domain nobody filters.
 */
TEST_F(SidecarErrorSecretTest, AStockInstallPrintsTheReolinkErrorLine)
{
    const DefaultLevelProbe &probe = defaultLevelProbe();

    ASSERT_TRUE(probe.ran)
        << "the stock-level child could not be forked or answered nothing, so "
           "this case measures no level at all";

    EXPECT_TRUE(probe.errorPrinted)
        << "the reolink domain does not print at ERROR on a stock install, so "
           "the line this ticket emptied was never the default-level defect it "
           "is filed as";
    EXPECT_FALSE(probe.debugPrinted)
        << "the reolink domain prints at DEBUG on a stock install: every other "
           "line of this controller, the event dumps included, leaves with the "
           "journal too";
}

/*
 * Own main instead of gtest_main, for two reasons.
 *
 * The level has to be set before the first log line of the process: the domain
 * map of Logger is filled once, lazily, and never re-read.
 *
 * And the exit is by _exit(): destroying an ExternProcServer at process exit
 * does kill(SIGTERM) on the last spawned child and closes libuv handles on a
 * loop this binary has stopped pumping.
 */
int main(int argc, char **argv)
{
    //Before anything raises the level, and before there is a loop or a child
    //to duplicate.
    measureStockLogLevel();

    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_sidecarerror_cfg_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (base)
    {
        const std::string cfg = std::string(base) + "/config";
        const std::string cache = std::string(base) + "/cache";
        ::mkdir(cfg.c_str(), 0700);
        ::mkdir(cache.c_str(), 0700);

        Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                                 const_cast<char *>(cache.c_str()), true);
        Utils::set_config_option("debug_level", "5");
    }

    const int ret = RUN_ALL_TESTS();

    //The stand-in sidecar outlives us otherwise: ~ExternProcServer, which is
    //what signals it, never runs behind _exit().
    std::ifstream pidf(pidFilePath().c_str());
    long pid = 0;
    if (pidf >> pid && pid > 0)
        ::kill(static_cast<pid_t>(pid), SIGTERM);

    if (!sandboxDir().empty())
    {
        ::unlink((sandboxDir() + "/" + kSidecarName).c_str());
        ::unlink(pidFilePath().c_str());
        ::rmdir(sandboxDir().c_str());
    }

    fflush(nullptr);
    _exit(ret);
}
