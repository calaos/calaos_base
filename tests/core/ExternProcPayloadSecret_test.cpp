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
 * ⭐⭐ WHAT THE SIDECAR TRANSPORT WRITES ABOUT THE MESSAGES IT CARRIES.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT
 * ---------------------------------------------------------------------------
 * ExternProcServer::sendMessage() streamed the whole outgoing payload, and
 * processData() the whole incoming one. The camera registration message of
 * ReolinkCtrl carries the camera `username` and `password` in clear, so the
 * transport published them - while the header of ReolinkWire.h says "Never
 * log the message itself", ReolinkCtrl.cpp says "never log it, here or
 * anywhere downstream", and the python end really redacts. The transport
 * contradicted all three because it does not know what it carries: it sees a
 * std::string, not fields.
 *
 * DEBUG, so not printed on a stock install - which is the level an operator
 * switches on before pasting a journal into a public bug report, and the
 * server puts its own level in the sidecar's environment and pipes the
 * sidecar's stdout back into its own.
 *
 * ---------------------------------------------------------------------------
 * ⚠️ WHY THIS GOES THROUGH A REAL CONTROLLER
 * ---------------------------------------------------------------------------
 * Nothing here calls sendMessage(). A real ReolinkCtrl is built, a peer
 * connects to the unix socket its ExternProcServer bound - which is all a
 * sidecar is - and announces itself; the controller then registers its camera
 * through its own doRegisterCamera(). What is asserted is what the shipped
 * path writes to std::cout. Exchanging the line in IO/ExternProc.cpp back for
 * its previous form turns the first case red.
 *
 * ---------------------------------------------------------------------------
 * ⛔ THE HALF THAT MUST NOT DIE WITH THE SECRET
 * ---------------------------------------------------------------------------
 * Deleting the lines would close the leak and blind the diagnosis. Whoever
 * reads a journal must still be able to say that a message was exchanged,
 * with WHICH sidecar, of WHICH frame type, and how big it was - the byte
 * count is what tells a stuck exchange from a busy one, and it is what the
 * python end logs too. The second case is that counterweight.
 *
 * ⛔ WHAT THIS DOES NOT PROVE: no camera and no journal of a real install are
 * involved, and the hifi rose token of the fourth case is held by a source
 * tripwire - reaching that line needs a stubbed HTTPS answer from the
 * amplifier, which no harness of this tree provides.
 *
 * The two levels the severity rests on are measured by the last case, in a
 * forked child: this process raises the level in its own main() and can no
 * longer see the one a box ships with.
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
#include <vector>

#include "ConfigStore.h"
#include "ExternProc.h"
#include "LogSetup.h"
#include "Logger.h"
#include "ReolinkCtrl.h"
#include "ReolinkTypes.h"
#include "ReolinkWire.h"
#include "libuvw.h"

using namespace Calaos;

namespace
{

#ifndef CALAOS_TOP_SRCDIR
#error "CALAOS_TOP_SRCDIR must be passed by the build (see tests/Makefile.am)"
#endif

/*
 * ⚠️ FIXTURE, NOT DECORATION.
 *
 * The three needles are values no other string of this binary can contain by
 * accident: "reolink" alone would also match the sidecar name and make a leak
 * look like a pass. buildRegisterMessage() is asserted to really carry the
 * two credentials before anything is sent, otherwise a green would mean "the
 * field was dropped", not "the field was withheld".
 */
const char *const kCameraHost     = "192.168.7.51";
const char *const kCameraUser     = "operateur-camera";
const char *const kCameraPassword = "mon mot de passe camera";
const char *const kEventType      = "motion";

//A field the server reads from no message: what the transport dumps of an
//incoming frame is then the only way it could reach a journal.
const char *const kInboundField   = "vendor_diagnostic";
const char *const kInboundSecret  = "jeton-entrant-du-sidecar";

const char *const kSidecarName    = "calaos_reolink";
const char *const kNamespace      = "reolink";

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

//Whether SOME single line carries both needles. The sidecar name also appears
//on the launch line of every run, so looking for it anywhere in the log would
//be satisfied by a line that has nothing to do with the exchange.
bool someLineHasBoth(const std::string &log, const std::string &a, const std::string &b)
{
    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line))
    {
        if (line.find(a) != std::string::npos && line.find(b) != std::string::npos)
            return true;
    }
    return false;
}

//Collapse runs of whitespace so a source tripwire matches any layout of the
//same tokens instead of going red on a re-indent, and so a statement spread
//over several lines is one string. Same helper, same reason, as
//core/ExternProcLogSecret_test.cpp.
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
 * ⚠️ WHAT A BOX PRINTS WITH NOBODY TOUCHING ANYTHING, measured in a child.
 *
 * The Logger fills its domain map once, from debug_level, and never re-reads
 * it: a process that has raised the level can no longer observe the default.
 * The child never raises it, so it sees the shipped one - and it has to be
 * forked before this process has an event loop or a spawned sidecar to
 * duplicate.
 */
struct DefaultLevelProbe
{
    bool ran = false;
    bool infoPrinted = false;
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
        char tmpl[] = "/tmp/calaos_payloadsecret_stock_XXXXXX";
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
            if (Utils::calaosLogger("hifirose")->isLevelEnabled(Logger::LOG_LEVEL_INFO))
                answer |= 0x1;
            if (Utils::calaosLogger("process")->isLevelEnabled(Logger::LOG_LEVEL_DEBUG))
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
        defaultLevelProbe().infoPrinted = (answer & 0x1) != 0;
        defaultLevelProbe().debugPrinted = (answer & 0x2) != 0;
    }
}

std::string captureStdout(const std::function<void()> &fn)
{
    std::ostringstream sink;
    std::streambuf *saved = std::cout.rdbuf(sink.rdbuf());
    fn();
    std::cout.rdbuf(saved);
    return sink.str();
}

//The one sandbox of this process. Empty when mkdtemp() fails, so a case FAILS
//instead of quietly pointing CALAOS_BIN_PREFIX at the real prefix.
const std::string &sandboxDir()
{
    static std::string dir = []() -> std::string
    {
        char tmpl[] = "/tmp/calaos_externproc_payload_XXXXXX";
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
 * ⚠️ A STAND-IN THAT STAYS ALIVE, unlike the recorder of
 * core/ExternProcSpawnHarness.h. ReolinkCtrl relaunches on every exit and
 * clears its registrations doing so; a sidecar that returns at once would
 * make the observation depend on winning a 100 ms race. It writes its pid so
 * main() can end it: this process leaves by _exit() and ~ExternProcServer,
 * which is what signals the child, never runs.
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
 * sockpath is private and carries a random uuid, but its shape is fixed and
 * it ends with our own pid - so matching on the suffix cannot pick up a
 * socket belonging to another test binary of the same `make check -jN`.
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

    void closeIt()
    {
        if (fd >= 0)
            ::close(fd);
        fd = -1;
    }

private:
    int fd = -1;
};

//What one camera registration wrote, observed once for the whole process.
struct Observation
{
    bool installed = false;
    bool peerConnected = false;
    bool registrationSent = false;
    std::string sockpath;
    std::string log;
};

/*
 * The ReolinkCtrl is a singleton and is never destroyed: ~ReolinkCtrl does not
 * delete its ExternProcServer, and the default loop can still hold its
 * handles. Capturing std::cout once and letting every case read the same text
 * also keeps them independent of the order gtest picks.
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

    obs->log = captureStdout([&]()
    {
        ReolinkCtrl &ctrl = ReolinkCtrl::Instance();

        obs->sockpath = findReolinkSocket();

        static FakeSidecar sidecar;
        obs->peerConnected = sidecar.connectTo(obs->sockpath);
        if (!obs->peerConnected)
            return;

        //Let the server accept the peer before anything is written to it.
        pumpLoopUntil([]() { return false; }, 300);

        //The camera is registered while the sidecar has not announced itself,
        //so the message really leaves through registerAllCameras() - the path
        //a server takes on every (re)connection of the sidecar.
        ctrl.registerCamera(ReolinkTypes::Hostname(kCameraHost),
                            ReolinkTypes::Username(kCameraUser),
                            ReolinkTypes::Password(kCameraPassword),
                            ReolinkTypes::EventType(kEventType),
                            [](const ReolinkTypes::Hostname &,
                               const ReolinkTypes::EventType &,
                               const ReolinkTypes::EventData &) {});

        const std::string announce =
            std::string("{\"status\":\"connected\",\"") + kInboundField +
            "\":\"" + kInboundSecret + "\"}";
        sidecar.sendFrame(announce);

        obs->registrationSent =
            pumpLoopUntil([&ctrl]() { return ctrl.isConnected(); }, 5000);
    });

    return *obs;
}

const char *const kWriteMarker = "client writing";
const char *const kFrameMarker = "Got a new frame";

} // namespace

class ExternProcPayloadSecretTest: public ::testing::Test {};

/*
 * ⭐⭐ THE CASE THIS SUITE EXISTS FOR: the camera password does not reach the
 * journal of an operator who switched DEBUG on.
 *
 * RED on master, verbatim, and for the right reason: sendMessage() streams the
 * payload, and the payload IS the registration message.
 *
 * ⚠️ ANTI-VACUITY FIRST. The `process` domain is asserted printable at DEBUG
 * and at least one write line is required before anything is looked for: a
 * quieter environment would otherwise make this case green while measuring
 * nothing.
 */
TEST_F(ExternProcPayloadSecretTest, TheReolinkCameraPasswordNeverReachesTheLog)
{
    ASSERT_TRUE(Utils::calaosLogger("process")->isLevelEnabled(Logger::LOG_LEVEL_DEBUG))
        << "the process domain is muted below DEBUG here, so this case cannot "
           "observe the line it exists to check";

    const std::string message = ReolinkWire::buildRegisterMessage(
        ReolinkTypes::Hostname(kCameraHost),
        ReolinkTypes::Username(kCameraUser),
        ReolinkTypes::Password(kCameraPassword),
        ReolinkTypes::EventType(kEventType));

    const std::string password = kCameraPassword;
    const std::string user = kCameraUser;

    ASSERT_NE(std::string::npos, message.find(password))
        << "buildRegisterMessage() did not carry the password at all, so a "
           "green here would mean nothing: " << message;
    ASSERT_NE(std::string::npos, message.find(user))
        << "buildRegisterMessage() did not carry the user at all, same "
           "reason: " << message;

    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_FALSE(obs.sockpath.empty())
        << "no ExternProcServer socket was found for the reolink namespace - "
           "the harness is broken, not the code under test";
    ASSERT_TRUE(obs.peerConnected)
        << "the stand-in sidecar could not connect to " << obs.sockpath;
    ASSERT_TRUE(obs.registrationSent)
        << "the controller never reached its connected state, so no "
           "registration was ever handed to the transport. Log: " << obs.log;
    ASSERT_LE(1, countOccurrences(obs.log, kWriteMarker))
        << "nothing was logged for the outgoing message, so this case cannot "
           "say whether a secret would have been: " << obs.log;

    EXPECT_EQ(std::string::npos, obs.log.find(password))
        << "the camera PASSWORD is in the journal. ReolinkWire.h says \"Never "
           "log the message itself\", ReolinkCtrl.cpp says \"never log it, "
           "here or anywhere downstream\", and the python end redacts. Log: "
        << obs.log;
    EXPECT_EQ(std::string::npos, obs.log.find(user))
        << "the camera USER is in the journal: " << obs.log;
    EXPECT_EQ(std::string::npos, obs.log.find(message))
        << "the whole registration message is in the journal: " << obs.log;
}

/*
 * ⛔ THE COUNTERWEIGHT: deleting the lines would also pass the case above.
 *
 * Whoever reads a journal must still be able to say that a message was
 * exchanged, WITH WHICH sidecar and of WHICH frame type, and how big it was -
 * a byte count is what separates a stuck exchange from a busy one, and
 * processData() has published one since before this ticket. This case is red
 * exactly when that diagnosis dies, and green under the mutation that
 * restores the leak.
 */
TEST_F(ExternProcPayloadSecretTest, TheLogStillNamesTheSidecarAndSizesItsFrames)
{
    ASSERT_TRUE(Utils::calaosLogger("process")->isLevelEnabled(Logger::LOG_LEVEL_DEBUG))
        << "the process domain is muted below DEBUG here";

    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_TRUE(obs.registrationSent)
        << "no message was exchanged at all, so this case cannot say whether "
           "the diagnosis survives. Log: " << obs.log;

    EXPECT_LE(1, countOccurrences(obs.log, kWriteMarker))
        << "nothing at all is written when a message leaves for a sidecar: an "
           "exchange that stops would be invisible. Log: " << obs.log;
    EXPECT_LE(1, countOccurrences(obs.log, kFrameMarker))
        << "nothing at all is written when a frame arrives from a sidecar. "
           "Log: " << obs.log;
    EXPECT_TRUE(someLineHasBoth(obs.log, kWriteMarker, kNamespace))
        << "the line written when a message LEAVES does not say which sidecar "
           "it left for: " << obs.log;
    EXPECT_TRUE(someLineHasBoth(obs.log, kFrameMarker, kNamespace))
        << "the line written when a frame ARRIVES does not say which sidecar "
           "it came from: " << obs.log;

    //The size of the registration message, which is what the transport can
    //publish about a payload it does not understand.
    const std::string message = ReolinkWire::buildRegisterMessage(
        ReolinkTypes::Hostname(kCameraHost),
        ReolinkTypes::Username(kCameraUser),
        ReolinkTypes::Password(kCameraPassword),
        ReolinkTypes::EventType(kEventType));

    EXPECT_NE(std::string::npos, obs.log.find(std::to_string(message.size())))
        << "the journal does not carry the " << message.size()
        << " bytes of the message that left: a truncated or an empty payload "
           "would leave no trace at all. Log: " << obs.log;

    //A frame type nobody compares to anything is a number, not a measurement:
    //the expected value comes from the enum the framing itself uses.
    const std::string frameType =
        "opcode " + std::to_string(static_cast<int>(ExternProcMessage::TypeMessage));

    EXPECT_NE(std::string::npos, obs.log.find(frameType))
        << "the journal does not say which kind of frame was exchanged ("
        << frameType << "): a frame the peer will reject as invalid would "
           "look exactly like a good one. Log: " << obs.log;
}

/*
 * The other half of the same wire: what the transport dumps of an INCOMING
 * frame.
 *
 * ReolinkCtrl refuses to log the message it receives, in those words, because
 * it is "the mirror of a channel that carries credentials" - and the
 * transport underneath dumped the same bytes. The field used here is one the
 * server reads from no message, so the transport is the ONLY thing that could
 * put it in a journal: no other site can make this case vacuously green.
 */
TEST_F(ExternProcPayloadSecretTest, AnIncomingFieldTheServerNeverReadsIsNotDumpedByTheTransport)
{
    ASSERT_TRUE(Utils::calaosLogger("process")->isLevelEnabled(Logger::LOG_LEVEL_DEBUG))
        << "the process domain is muted below DEBUG here";

    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_TRUE(obs.registrationSent)
        << "the announce frame was never processed, so nothing was received. "
           "Log: " << obs.log;
    ASSERT_LE(1, countOccurrences(obs.log, kFrameMarker))
        << "nothing was logged for the incoming frame, so this case cannot "
           "say whether its content would have been: " << obs.log;

    EXPECT_EQ(std::string::npos, obs.log.find(kInboundSecret))
        << "the transport dumps the content of an incoming frame. A sidecar "
           "answer carries whatever the driver put in it, error messages "
           "included. Log: " << obs.log;
}

/*
 * The hifi rose device token, held by a SPELLING and not by an effect.
 *
 * Reaching AVRRose::registerDevice()'s answer handler needs a stubbed HTTPS
 * response from the amplifier, which no harness of this tree provides. What
 * is checked instead is that no logging statement of the shipped file streams
 * the token, whatever the layout: the statements are cut on the semicolon of
 * a whitespace-collapsed copy, so a line break inside one changes nothing.
 *
 * ⚠️ The file is asserted to still MENTION roseToken first: a rename or a move
 * would otherwise make this case vacuously green.
 *
 * ⛔ Its blind spot, named: a copy under another name (`auto t = roseToken;`)
 * then streamed would pass. That is the same class as the tripwires that hold
 * the sidecar mains, and it is not closable by one more spelling.
 */
TEST_F(ExternProcPayloadSecretTest, TripwireSource_TheHifiRoseTokenIsNeverStreamedToALogLine)
{
    const std::string relative = "src/bin/calaos_server/Audio/AVRRose.cpp";

    std::string raw;
    ASSERT_TRUE(readShippedSource(relative, raw)) << "could not read " << relative;

    const std::string code = collapseWhitespace(raw);

    ASSERT_LE(1, countOccurrences(code, "roseToken"))
        << relative << " no longer mentions roseToken at all: this tripwire is "
           "pointing at the wrong file and proves nothing";

    static const char *const kLogMacros[] = {
        "cDebugDom(", "cInfoDom(", "cWarningDom(", "cErrorDom(", "cCriticalDom(",
        "cDebug()", "cInfo()", "cWarning()", "cError()", "cCritical()"
    };

    std::vector<std::string> offenders;
    std::string::size_type start = 0;
    while (start < code.size())
    {
        std::string::size_type end = code.find(';', start);
        if (end == std::string::npos)
            end = code.size();

        const std::string statement = code.substr(start, end - start);
        start = end + 1;

        if (statement.find("roseToken") == std::string::npos)
            continue;

        for (const char *macro: kLogMacros)
        {
            if (statement.find(macro) != std::string::npos)
            {
                offenders.push_back(statement);
                break;
            }
        }
    }

    std::string report;
    for (const std::string &s: offenders)
        report += "\n  " + s;

    EXPECT_TRUE(offenders.empty())
        << "a logging statement of " << relative << " streams the device "
           "token. cInfoDom prints on a stock install - debug_level defaults "
           "to 4 and LOG_LEVEL_INFO is 4 - so the token leaves with the "
           "journal, at every registration:" << report;
}

/*
 * ⭐ THE TWO LEVELS THIS TICKET RESTS ON, MEASURED INSTEAD OF ASSERTED.
 *
 * "The token leaves on a stock install" and "the payload lines only leave once
 * an operator turns DEBUG on" are the two halves of the severity, and both
 * were prose everywhere else in this suite - which raises the level in its own
 * main() and can therefore observe neither. A child that never raised it is
 * asked instead. Lower the shipped default and the second half goes red; raise
 * it and the first half survives while the payload lines start leaving on
 * their own.
 */
TEST_F(ExternProcPayloadSecretTest, AStockInstallPrintsTheTokenLineAndNotThePayloadLines)
{
    const DefaultLevelProbe &probe = defaultLevelProbe();

    ASSERT_TRUE(probe.ran)
        << "the stock-level child could not be forked or answered nothing, so "
           "this case measures no level at all";

    EXPECT_TRUE(probe.infoPrinted)
        << "the hifirose domain does not print at INFO on a stock install, so "
           "the line this ticket emptied was never the default-level defect it "
           "is filed as";
    EXPECT_FALSE(probe.debugPrinted)
        << "the process domain prints at DEBUG on a stock install: the two "
           "transport lines are then the same severity as the token line was, "
           "and the payload leaves with no operator having switched anything on";
}

/*
 * Own main instead of gtest_main, for two reasons.
 *
 * DEBUG has to be on before the first log line of the process: the domain map
 * of Logger is filled once, lazily, and never re-read. This is the setting an
 * operator switches on before pasting a journal into a bug report, which is
 * the whole scenario.
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

    char tmpl[] = "/tmp/calaos_payloadsecret_cfg_XXXXXX";
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
