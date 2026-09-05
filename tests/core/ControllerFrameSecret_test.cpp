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
 * WHAT FOUR CONTROLLERS REPUBLISH OF A FRAME THEY COULD NOT PARSE.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT
 * ---------------------------------------------------------------------------
 * MqttCtrl, KNXCtrl, ScriptExec and OwCtrl each wrote the whole incoming
 * payload into a cWarningDom() on their parse failure path. WARNING prints on
 * a stock install - the filter is level > maxLevelPrintable(domain) and the
 * fallback of Logger::maxLevelPrintable() is INFO - so those bytes left with
 * the journal without anybody enabling anything.
 *
 * The bytes of a frame that does not parse were written by the same sender as
 * the bytes of one that does, and this end has by definition failed to read
 * them: what they carry is not knowable at that point. Two of these wires do
 * carry secrets of the installation on their nominal path - an MQTT payload is
 * whatever a broker publishes, and a Lua set_param value is whatever a script
 * writes into an IO parameter, a camera password included.
 *
 * ---------------------------------------------------------------------------
 * THE ONEWIRE CASE, AND WHY A SENSOR SPELLED ON `<< msg` IS BLIND TO IT
 * ---------------------------------------------------------------------------
 * OwCtrl did not name the frame at all: it logged e.what(), of an exception
 * built two lines above by concatenating the frame into its message. And the
 * concatenation is only half of it - the parser's own exception quotes the
 * token it choked on, so e.what() carries the frame even where this tree never
 * concatenated anything. The two paths are exercised separately here: a frame
 * that parses but is not an array reaches the first, a frame that does not
 * parse reaches the second.
 *
 * ---------------------------------------------------------------------------
 * WHAT THE ASSERTIONS ACTUALLY BOUND
 * ---------------------------------------------------------------------------
 * Looking for a needle only sees the slice it was spelled for: a line
 * publishing the first or the last bytes of a frame carries the frame and
 * cites no needle, and a secret that travels percent encoded is invisible to a
 * search for its clear form. So each frame carries its needle BOTH ways, and
 * the assertion that carries the case asks the journal how long a RUN of the
 * frame it gives back and fixes a ceiling, instead of naming a value.
 *
 * WHAT MUST NOT DIE WITH THE BYTES: a frame that cannot be read is exactly
 * when an operator needs to know a sidecar has drifted. That an unreadable
 * frame arrived, on which controller, and of what size, is required of every
 * one of the five.
 *
 * WHAT THIS DOES NOT PROVE: no broker, no KNX bus, no owfs and no Lua
 * interpreter are involved. The stand-in sidecars are shell scripts that
 * sleep, and the chain proven stops at what std::cout of the server receives.
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
#include <cctype>
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
#include "KNXCtrl.h"
#include "LogSetup.h"
#include "Logger.h"
#include "MqttCtrl.h"
#include "OWCtrl.h"
#include "Params.h"
#include "ScriptExec.h"
#include "libuvw.h"

using namespace Calaos;

namespace
{

/*
 * THE FIVE WIRES, AND THE NEEDLE EACH FRAME REALLY CARRIES.
 *
 * Every needle appears in its frame twice, in clear and percent encoded: the
 * encoded form is what a credential looks like once it has been put inside a
 * url, and a search for the clear form does not see it.
 *
 * The needles share as little text as possible with each other so that a leak
 * on one wire cannot redden the case of another - the red sets of a mutation
 * are only readable if they are independent.
 *
 * And none of them ends in a hexadecimal tail. The journal publishes
 * identifiers of its own in that alphabet - an object address, a request
 * fingerprint - so a hexadecimal run in a needle can be matched by bytes
 * nobody chose, which turns the ceiling into a lottery no run reproduces. The
 * percent encoded form carries that risk more than the clear one: `%20` in
 * front of a hexadecimal word lengthens the run by two.
 */
struct Wire
{
    const char *domain;   //logger domain, and what identifies the line
    const char *sidecar;  //executable name the controller launches
    const char *ns;       //namespace ExternProcServer puts in its socket name
    const char *plain;    //the needle as a human would write it
    const char *encoded;  //the same needle inside a url
    std::string frame;    //the bytes really put on the wire
    bool peerConnected = false;
    int  failureLines = 0;
};

//Not JSON at all, or JSON of the wrong shape: this is the failure path, and it
//is the one two ends reach first when they drift apart.
std::vector<Wire> &wires()
{
    static std::vector<Wire> w = {
        { "mqtt", "calaos_mqtt", "mqtt",
          "courtier abonne pivoine", "courtier%20abonne%20pivoine",
          "{\"topic\":\"maison/portail\",\"payload\":\"courtier abonne pivoine "
          "mqtt://q1:courtier%20abonne%20pivoine@10.9.4.2/\"" },

        { "knx", "calaos_knx", "knx_monitor",
          "passerelle bus tourmaline", "passerelle%20bus%20tourmaline",
          "{\"type\":\"event\",\"group_addr\":\"1/2/3\",\"value\":\"passerelle bus tourmaline "
          "ldap://w2:passerelle%20bus%20tourmaline@192.168.55.3/\"" },

        { "lua", "calaos_script", "lua",
          "consigne parametre vermeil", "consigne%20parametre%20vermeil",
          "{\"msg\":\"set_param\",\"data\":{\"champ\":\"consigne parametre vermeil "
          "ftp://e3:consigne%20parametre%20vermeil@172.31.8.4/\"" },

        //Valid json, wrong shape: the branch where the exception used to be
        //built by concatenating the frame into its own message.
        { "1wire", "calaos_1wire", "1wire",
          "sonde tableau grelinette", "sonde%20tableau%20grelinette",
          "{\"identifiant\":\"28.AAA\",\"releve\":\"sonde tableau grelinette\","
          "\"lien\":\"ow://r4:sonde%20tableau%20grelinette@10.200.6.5/\"}" },

        /* Not json at all, and the malformation is inside a string: nothing is
         * concatenated here, the PARSER quotes the token it choked on. The
         * clear needle sits outside that token and the percent encoded one
         * inside it - a sensor spelled on the clear form stays green while the
         * credential leaves.
         */
        { "1wire", "calaos_1wire", "1wire",
          "capteur jeton velours", "capteur%20jeton%20velours",
          "[{\"rom\":\"3B.BBB\",\"mesure\":\"capteur jeton velours\","
          "\"url\":\"nfs://t5:capteur%20jeton%20velours@169.254.9.6/\\q\"}]" },
    };

    return w;
}

/*
 * THE SIXTH FRAME, AND IT IS NOT A FRAME THAT FAILED TO PARSE ON ARRIVAL.
 *
 * MqttCtrl parses a stored payload a SECOND time, on the read path every mqtt
 * IO with a `path` takes, and published the whole payload there too. That one
 * is not reached by a sidecar drifting: it is reached by a device publishing
 * something that is not json on a topic an IO reads, which is ordinary.
 *
 * It arrives through the real wire like the others - a well formed frame whose
 * payload happens not to be json - and is read back through the public entry
 * point every MqttInput* and MqttOutput* calls.
 */
const char *const kReadTopic   = "maison/collecteur";
const char *const kReadPlain   = "graphe brut myrtille";
const char *const kReadEncoded = "graphe%20brut%20myrtille";

std::string mqttReadPayload()
{
    return std::string(kReadPlain) + " smb://y6:" + kReadEncoded + "@203.0.113.7/";
}

std::string mqttReadFrame()
{
    return std::string("{\"topic\":\"") + kReadTopic + "\",\"payload\":\"" +
           mqttReadPayload() + "\"}";
}

//Common to the wording before this fix and after it on that read path.
const char *const kReadFailureMarker = "Error parsing";

//Common to the wording of every one of the four controllers before this fix
//and after it: a case must not go red because the sentence was rephrased, and
//the pump must find the line on both.
const char *const kFailureMarker = "Error parsing json";

//The broker credentials of the fixture. They are what makes an mqtt payload a
//channel this installation has secrets on, and they are handed to the
//controller the way io.xml does.
const char *const kBrokerUser     = "operateur-courtier";
const char *const kBrokerPassword = "mot de passe du courtier";

/*
 * THE LONGEST RUN OF A FRAME THE JOURNAL GIVES BACK.
 *
 * A value search only sees the slice it was spelled for. Cutting the frame at
 * 64 bytes was measured to redden such a search and cutting it at 24 was not,
 * although both put the account inside a url in the journal. What must not
 * reach a journal is any RUN of the frame, wherever it was cut, so the log is
 * asked how much it gives back.
 */
std::string longestEchoRun(const std::string &log, const std::string &text)
{
    std::string best;
    for (size_t i = 0; i < text.size(); i++)
    {
        size_t len = best.size() + 1;
        while (i + len <= text.size() &&
               log.find(text.substr(i, len)) != std::string::npos)
        {
            best = text.substr(i, len);
            len++;
        }
    }
    return best;
}

size_t longestEcho(const std::string &log, const std::string &text)
{
    return longestEchoRun(log, text).size();
}

//Every document the ceiling covers, in one place: three cases read them, and a
//list that drifts between them would hold one property on a fixture the others
//no longer measure.
struct Doc { std::string label; std::string text; };

std::vector<Doc> measuredDocuments()
{
    std::vector<Doc> docs;
    for (size_t i = 0; i < wires().size(); i++)
        docs.push_back({ std::string(wires()[i].domain) + " #" + std::to_string(i),
                         wires()[i].frame });
    docs.push_back({ "mqtt read path", mqttReadPayload() });
    return docs;
}

//The longest run of the journal's own alphabet a frame carries: an address and
//a fingerprint are printed in it, so such a run can be matched by bytes nobody
//chose.
std::string longestHexRun(const std::string &s)
{
    std::string best, cur;
    for (const char c: s)
    {
        if (::isxdigit(static_cast<unsigned char>(c)))
        {
            cur += c;
            if (cur.size() > best.size())
                best = cur;
        }
        else
            cur.clear();
    }
    return best;
}

/* One above the overlap the corrected tree really produces, and the suite
 * re-measures that overlap at every run rather than trusting this line: see
 * the calibration case at the end of the file for why a ceiling nobody
 * re-measures drifts in both directions.
 */
const size_t kMaxEcho = 8;

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

int countLinesWith(const std::string &log, const std::string &a, const std::string &b)
{
    int n = 0;
    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line))
    {
        if (line.find(a) != std::string::npos && line.find(b) != std::string::npos)
            n++;
    }
    return n;
}

/*
 * WHAT A BOX PRINTS WITH NOBODY TOUCHING ANYTHING, measured in a child.
 *
 * The whole severity of this ticket is "WARNING prints by default", and the
 * Logger fills its domain map once and never re-reads it: a process that has
 * raised the level can no longer observe the default. The child is forked
 * before this process owns an event loop or a spawned sidecar to duplicate,
 * and it never raises anything. The DEBUG half is the contrast - it says the
 * WARNING half is not an artefact of a domain nobody filters.
 */
struct DefaultLevelProbe
{
    bool ran = false;
    bool warningPrinted[5] = { false, false, false, false, false };
    bool debugPrinted[5] = { false, false, false, false, false };
};

DefaultLevelProbe &defaultLevelProbe()
{
    static DefaultLevelProbe probe;
    return probe;
}

void measureStockLogLevel()
{
    const size_t n = wires().size();

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

        std::vector<unsigned char> answer(n + 1, 0);
        char tmpl[] = "/tmp/calaos_ctrlframe_stock_XXXXXX";
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

            answer[0] = 0x4;
            for (size_t i = 0; i < n; i++)
            {
                unsigned char a = 0;
                if (Utils::calaosLogger(wires()[i].domain)->isLevelEnabled(Logger::LOG_LEVEL_WARNING))
                    a |= 0x1;
                if (Utils::calaosLogger(wires()[i].domain)->isLevelEnabled(Logger::LOG_LEVEL_DEBUG))
                    a |= 0x2;
                answer[i + 1] = a;
            }
        }

        if (::write(fds[1], answer.data(), answer.size()) !=
            static_cast<ssize_t>(answer.size()))
            answer[0] = 0;
        ::close(fds[1]);
        _exit(0);
    }

    ::close(fds[1]);

    std::vector<unsigned char> answer(n + 1, 0);
    const ssize_t got = ::read(fds[0], answer.data(), answer.size());
    ::close(fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    if (got == static_cast<ssize_t>(n + 1) && (answer[0] & 0x4))
    {
        defaultLevelProbe().ran = true;
        for (size_t i = 0; i < n; i++)
        {
            defaultLevelProbe().warningPrinted[i] = (answer[i + 1] & 0x1) != 0;
            defaultLevelProbe().debugPrinted[i] = (answer[i + 1] & 0x2) != 0;
        }
    }
}

//The one sandbox of this process. Empty when mkdtemp() fails, so a case FAILS
//instead of quietly pointing CALAOS_BIN_PREFIX at the real prefix.
const std::string &sandboxDir()
{
    static std::string dir = []() -> std::string
    {
        char tmpl[] = "/tmp/calaos_ctrl_frame_XXXXXX";
        const char *d = ::mkdtemp(tmpl);
        return d? std::string(d) : std::string();
    }();

    return dir;
}

std::string pidFilePath()
{
    return sandboxDir() + "/sidecars.pid";
}

/*
 * STAND-INS THAT STAY ALIVE. Every one of these controllers relaunches its
 * child on exit, and a sidecar that returned at once would make the
 * observation depend on winning a race. They append their pid so main() can
 * end them: this process leaves by _exit() and ~ExternProcServer never runs.
 */
bool installSidecarStandIns()
{
    if (sandboxDir().empty())
        return false;

    for (const Wire &w: wires())
    {
        const std::string script = sandboxDir() + "/" + w.sidecar;

        {
            std::ofstream f(script.c_str(), std::ios::out | std::ios::trunc);
            if (!f.is_open())
                return false;
            f << "#!/bin/sh\n"
                 "echo $$ >> '" << pidFilePath() << "'\n"
                 "exec sleep 120\n";
            if (!f.good())
                return false;
        }

        if (::chmod(script.c_str(), 0755) != 0)
            return false;
    }

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
 * The socket an ExternProcServer bound for one namespace.
 *
 * sockpath is private and carries a random uuid, but its shape is fixed and it
 * ends with our own pid, so the suffix cannot pick up a socket belonging to
 * another test binary of the same `make check -jN`. The knx command channel
 * and the knx monitor channel are distinguished by that same suffix.
 */
std::string findSocket(const std::string &ns)
{
    const std::string prefix = "calaos_proc_";
    const std::string suffix = std::string("_") + ns + "_" +
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

struct Observation
{
    bool installed = false;
    //What the controller hands back for the stored payload when no path is
    //asked for: it proves the bytes really reached the object before the read
    //path is asked to parse them.
    std::string readEcho;
    bool readErr = false;
    std::string log;
};

/*
 * The four controllers are built once and never destroyed - three of them are
 * behind a singleton and the Lua one owns its own process. Capturing
 * std::cout once and letting every case read the same text also keeps the
 * cases independent of the order gtest picks.
 */
const Observation &theObservation()
{
    static Observation *obs = nullptr;
    if (obs)
        return *obs;

    obs = new Observation();
    obs->installed = installSidecarStandIns();
    if (!obs->installed)
        return *obs;

    //Redirected by hand rather than through a helper: the pump below has to
    //read what has been written so far, so the sink must stay reachable while
    //the loop runs.
    std::ostringstream sink;
    std::streambuf *saved = std::cout.rdbuf(sink.rdbuf());

    //A broker configuration the way io.xml carries one, credentials included.
    Params mqttParams;
    mqttParams.Add("host", "127.0.0.1");
    mqttParams.Add("port", "1883");
    mqttParams.Add("user", kBrokerUser);
    mqttParams.Add("password", kBrokerPassword);

    static MqttCtrl *mqtt = new MqttCtrl(mqttParams);
    static shared_ptr<KNXCtrl> knx = KNXCtrl::Instance("127.0.0.1");
    (void)knx;
    static shared_ptr<OwCtrl> ow = OwCtrl::Instance("--fake-adapter");
    (void)ow;
    static ExternProcServer *lua =
        ScriptExec::ExecuteScriptDetached("return true", [](bool) {}, Params());
    (void)lua;

    //Let every server bind and accept before anything is written to it.
    pumpLoopUntil([]() { return false; }, 500);

    static std::vector<FakeSidecar> peers(wires().size());
    for (size_t i = 0; i < wires().size(); i++)
    {
        Wire &w = wires()[i];
        w.peerConnected = peers[i].connectTo(findSocket(w.ns));
    }

    pumpLoopUntil([]() { return false; }, 500);

    for (size_t i = 0; i < wires().size(); i++)
    {
        if (wires()[i].peerConnected)
            peers[i].sendFrame(wires()[i].frame);
    }

    //A well formed frame whose payload is not json, so it is stored and only
    //fails when the read path parses it.
    if (wires()[0].peerConnected)
        peers[0].sendFrame(mqttReadFrame());

    //One failure line per frame sent, and the onewire controller is handed two
    //of them - on its two distinct failure branches - so it owes two.
    pumpLoopUntil([&sink]()
    {
        const std::string sofar = sink.str();
        for (const Wire &w: wires())
        {
            int owed = 0;
            for (const Wire &other: wires())
                if (std::string(other.domain) == w.domain)
                    owed++;

            if (countLinesWith(sofar, kFailureMarker, w.domain) < owed)
                return false;
        }
        return true;
    }, 8000);

    //Let the well formed frame be stored before it is read back.
    pumpLoopUntil([]()
    {
        Params probe;
        probe.Add("topic", kReadTopic);
        probe.Add("path", "");
        bool e = false;
        return mqtt->getValue(probe, e, "topic", "path") == mqttReadPayload();
    }, 5000);

    //An empty path hands back the stored payload untouched: what comes out
    //here is the anti-vacuity of the read case, because "no message for this
    //topic" and "this payload does not parse" both raise the same flag.
    Params echoParams;
    echoParams.Add("topic", kReadTopic);
    echoParams.Add("path", "");
    bool echoErr = false;
    obs->readEcho = mqtt->getValue(echoParams, echoErr, "topic", "path");

    Params readParams;
    readParams.Add("topic", kReadTopic);
    readParams.Add("path", "capteur/valeur");
    obs->readErr = false;
    mqtt->getValue(readParams, obs->readErr, "topic", "path");

    std::cout.rdbuf(saved);
    obs->log = sink.str();

    for (Wire &w: wires())
        w.failureLines = countLinesWith(obs->log, kFailureMarker, w.domain);

    return *obs;
}

} // namespace

/*
 * ONE GTEST CASE PER WIRE, not one loop over five.
 *
 * The whole point of a counter mutation campaign is that the set of cases a
 * mutation reddens says WHICH site it hit; a single looping case answers "the
 * loop" to every one of them.
 */
class ControllerFrameSecretTest: public ::testing::TestWithParam<size_t> {};

/*
 * THE CASE THIS SUITE EXISTS FOR: nothing of a frame a controller could not
 * parse reaches the journal.
 *
 * ANTI-VACUITY FIRST. The domain is asserted printable at WARNING, the frame
 * is asserted to really carry its needle in both forms, and the failure line
 * is required to have been written before anything is looked for - a quieter
 * environment or a dropped frame would otherwise make this case green while
 * measuring nothing.
 */
TEST_P(ControllerFrameSecretTest, NothingOfAFrameThatDidNotParseReachesTheLog)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";

    const Wire &w = wires()[GetParam()];
    SCOPED_TRACE(std::string(w.domain) + " frame #" + std::to_string(GetParam()));

    ASSERT_TRUE(Utils::calaosLogger(w.domain)->isLevelEnabled(Logger::LOG_LEVEL_WARNING))
        << "the " << w.domain << " domain is muted below WARNING here, so this "
           "case cannot observe the line it exists to check";

    ASSERT_NE(std::string::npos, w.frame.find(w.plain))
        << "the frame does not carry its needle in clear at all, so a green "
           "here would mean nothing: " << w.frame;
    ASSERT_NE(std::string::npos, w.frame.find(w.encoded))
        << "the frame does not carry its needle percent encoded at all: "
        << w.frame;

    ASSERT_TRUE(w.peerConnected)
        << "no peer reached the " << w.ns << " socket, so nothing was ever "
           "handed to this controller. Log: " << obs.log;
    ASSERT_GE(w.failureLines, 1)
        << "no parse failure was reported on the " << w.domain << " domain, so "
           "this case cannot say whether the frame would have been quoted. "
           "Log: " << obs.log;

    EXPECT_EQ(std::string::npos, obs.log.find(w.plain))
        << "the payload of a frame this end could not read is in the journal, "
           "and WARNING prints on a stock install. Log: " << obs.log;
    EXPECT_EQ(std::string::npos, obs.log.find(w.encoded))
        << "the payload is in the journal in its percent encoded form, which a "
           "search for the clear form does not see. Log: " << obs.log;

    //What carries this case: a line publishing the head or the tail of the
    //frame leaves both searches above green while carrying the account inside
    //the url.
    const size_t echo = longestEcho(obs.log, w.frame);
    EXPECT_LT(echo, kMaxEcho)
        << "the journal gives back " << echo << " consecutive bytes of a frame "
           "this end could not read, which no line of this controller has a "
           "reason to carry. Log: " << obs.log;
}

/*
 * THE COUNTERWEIGHT: deleting the line would also pass the case above.
 *
 * A sidecar that has started talking nonsense is the reason this line exists.
 * What must survive is that an unreadable frame arrived, on which controller,
 * and how big it was - the size is compared to the frame really sent, because
 * a number nobody compares to anything is not a measurement.
 */
TEST_P(ControllerFrameSecretTest, AnUnreadableFrameIsStillReportedWithItsSize)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";

    const Wire &w = wires()[GetParam()];
    SCOPED_TRACE(std::string(w.domain) + " frame #" + std::to_string(GetParam()));

    ASSERT_TRUE(w.peerConnected)
        << "no peer reached the " << w.ns << " socket. Log: " << obs.log;

    const std::string size = std::to_string(w.frame.size()) + " bytes";
    EXPECT_TRUE(someLineHasAll(obs.log, kFailureMarker, w.domain, size))
        << "the line written for an unreadable frame does not say how many "
           "bytes it was: a sidecar that has started talking nonsense is then "
           "indistinguishable from one that has gone quiet. Log: " << obs.log;

    //The onewire controller is handed two frames, on its two distinct failure
    //branches, and owes a line for each.
    if (std::string(w.domain) == "1wire")
    {
        EXPECT_GE(countLinesWith(obs.log, kFailureMarker, "1wire"), 2)
            << "the onewire controller reported fewer than its two unreadable "
               "frames: one of its two failure branches is silent. Log: "
            << obs.log;
    }
}

INSTANTIATE_TEST_SUITE_P(EachWire, ControllerFrameSecretTest,
                         ::testing::Range<size_t>(0, 5));

class MqttReadPathTest: public ::testing::Test {};

/*
 * THE SIXTH SITE, AND IT IS NOT REACHED BY A WIRE DRIFTING.
 *
 * MqttCtrl parses a stored payload a second time, on the read path every mqtt
 * IO with a `path` takes, and published the whole payload there too. Any
 * device publishing something that is not json on a topic an IO reads lands
 * here, which is ordinary rather than exceptional.
 *
 * ANTI-VACUITY: "no message for this topic" and "this payload does not parse"
 * raise the same flag, so the payload is first read back with an empty path -
 * the shortcut that hands it over untouched - and compared to what was sent.
 */
TEST_F(MqttReadPathTest, TheMqttReadPathDoesNotQuoteThePayload)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";

    const std::string payload = mqttReadPayload();
    ASSERT_NE(std::string::npos, payload.find(kReadPlain));
    ASSERT_NE(std::string::npos, payload.find(kReadEncoded));
    ASSERT_EQ(payload, obs.readEcho)
        << "the payload never reached the controller through the wire, so the "
           "read below parsed nothing and this case would measure nothing";
    ASSERT_TRUE(obs.readErr)
        << "the read path did not report a failure on a payload that is not "
           "json, so it never entered the branch this case exists to check";

    EXPECT_EQ(std::string::npos, obs.log.find(kReadPlain))
        << "a broker payload that is not json is in the journal, and this path "
           "is taken on an ordinary read, not on a sidecar drifting. Log: "
        << obs.log;
    EXPECT_EQ(std::string::npos, obs.log.find(kReadEncoded))
        << "the payload is in the journal percent encoded. Log: " << obs.log;

    const size_t echo = longestEcho(obs.log, payload);
    EXPECT_LT(echo, kMaxEcho)
        << "the journal gives back " << echo << " consecutive bytes of a broker "
           "payload. Log: " << obs.log;
}

TEST_F(MqttReadPathTest, TheMqttReadPathStillReportsTheSize)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";
    ASSERT_TRUE(obs.readErr)
        << "the read path reported no failure, so this case cannot say whether "
           "the failure would have been visible";

    const std::string size = std::to_string(mqttReadPayload().size()) + " bytes";
    EXPECT_TRUE(someLineHasAll(obs.log, kReadFailureMarker, "mqtt", size))
        << "a payload an mqtt IO could not read is dropped without a word of "
           "its size, and without naming the controller: an IO reading a device "
           "that has changed its format then goes quiet with no trace. Log: "
        << obs.log;
}

class StockLevelTest: public ::testing::Test {};

/*
 * THE LEVEL THE WHOLE TICKET RESTS ON, MEASURED INSTEAD OF ASSERTED.
 *
 * "This line prints on a stock install" is the severity, and it comes from the
 * LOG_LEVEL_INFO fallback of Logger::maxLevelPrintable(), not from a written
 * option. A child that never touched a configuration is asked.
 */
TEST_F(StockLevelTest, AStockInstallPrintsTheseParseFailureLines)
{
    const DefaultLevelProbe &probe = defaultLevelProbe();

    ASSERT_TRUE(probe.ran)
        << "the stock-level child could not be forked or answered nothing, so "
           "this case measures no level at all";

    for (size_t i = 0; i < wires().size(); i++)
    {
        SCOPED_TRACE(wires()[i].domain);

        EXPECT_TRUE(probe.warningPrinted[i])
            << "the " << wires()[i].domain << " domain does not print at "
               "WARNING on a stock install, so this line was never the "
               "default-level defect this ticket is filed as";
        EXPECT_FALSE(probe.debugPrinted[i])
            << "the " << wires()[i].domain << " domain prints at DEBUG on a "
               "stock install: every other line of this controller, the frame "
               "dumps included, leaves with the journal too";
    }
}

/*
 * THE CEILING ITSELF, HELD TO WHAT THIS SUITE MEASURES AT EVERY RUN.
 *
 * A bounded sensor is only as narrow as the number above the noise it was cut
 * for, and that number drifts both ways with nobody watching: a fixture that
 * gains a shared word widens the blind window while every assertion stays
 * green, and a ceiling left wider than the run it was cut for is blind space
 * no one asked for. Pinning the equality turns both into a red that says which
 * number to write, and makes ONE more byte of echo than the tree really
 * produces a failure - well under the ceiling the cases above enforce.
 */
class EchoCeilingTest: public ::testing::Test {};

TEST_F(EchoCeilingTest, TheCeilingIsHeldToTheOverlapThisSuiteMeasures)
{
    const Observation &obs = theObservation();
    ASSERT_TRUE(obs.installed) << "could not set up the CALAOS_BIN_PREFIX sandbox";

    //A measure that cannot report a run reads as a clean zero everywhere below.
    ASSERT_EQ("bcdef", longestEchoRun("zzbcdefzz", "abcdefg"));
    ASSERT_EQ("", longestEchoRun("zzz", "abc"));

    std::string worst, worstLabel;
    const auto keep = [&worst, &worstLabel](const std::string &run,
                                            const std::string &label)
    {
        if (run.size() > worst.size())
        {
            worst = run;
            worstLabel = label;
        }
    };

    for (const Wire &w: wires())
    {
        ASSERT_TRUE(w.peerConnected)
            << "no peer reached the " << w.ns << " socket, so the overlap this "
               "case exists to pin was not produced at all";
    }

    for (const Doc &d: measuredDocuments())
        keep(longestEchoRun(obs.log, d.text), d.label);

    EXPECT_EQ(kMaxEcho, worst.size() + 1)
        << "the ceiling is " << kMaxEcho << " while this tree gives back at "
           "most " << worst.size() << " bytes of a frame, at " << worstLabel
        << ", on the run \"" << worst << "\". Everything between the two is a "
           "window this suite cannot see into: either a leak has widened the "
           "overlap, or the fixture has, and the ceiling to write is "
        << (worst.size() + 1) << ".";
}

/*
 * THE FIXTURE, HELD TO THE SAME CEILING.
 *
 * A run two frames share is republished by whichever wire leaks first, so it
 * raises the bound of every neighbour that carries it: the red set then names
 * the fixture instead of the wire.
 */
TEST_F(EchoCeilingTest, NoTwoFramesShareARunTheCeilingWouldNotAbsorb)
{
    const std::vector<Doc> docs = measuredDocuments();

    for (size_t i = 0; i < docs.size(); i++)
    {
        for (size_t j = i + 1; j < docs.size(); j++)
        {
            const std::string run = longestEchoRun(docs[i].text, docs[j].text);
            EXPECT_LT(run.size(), kMaxEcho)
                << docs[i].label << " and " << docs[j].label << " share \""
                << run << "\", " << run.size() << " bytes, which the ceiling of "
                << kMaxEcho << " does not absorb: a leak at either one reddens "
                   "the bound of the other and the red set stops naming a wire.";
        }
    }
}

/*
 * THE ALPHABET THE JOURNAL DRAWS IN, HELD OUT OF THE FIXTURE.
 *
 * The equality above is only reproducible if the bytes nobody chose cannot
 * lengthen a run: an object address and a request fingerprint are printed in
 * hexadecimal, so a frame carrying a hexadecimal run as long as the measured
 * overlap makes the ceiling a lottery, and its red an intermittent one nobody
 * can reproduce. What is bounded is the form the measure really hunts - the
 * frame as it goes on the wire, percent encoding included - and not the needle
 * a human wrote: it is the encoding pattern that carries the possible overlap.
 */
TEST_F(EchoCeilingTest, NoFrameCarriesTheJournalsAlphabetThatFar)
{
    //A measure that cannot report a run reads as a clean zero below.
    ASSERT_EQ("beef", longestHexRun("zzbeefzz"));
    ASSERT_EQ("", longestHexRun("zz"));

    for (const Doc &d: measuredDocuments())
    {
        const std::string run = longestHexRun(d.text);
        EXPECT_LT(run.size(), kMaxEcho)
            << d.label << " carries \"" << run << "\", " << run.size()
            << " bytes of the alphabet the journal draws its own identifiers "
               "in, which the ceiling of " << kMaxEcho << " does not absorb: a "
               "draw can match them and the equality above becomes a lottery.";
    }
}

/*
 * Own main instead of gtest_main, for two reasons.
 *
 * The level is deliberately NOT raised: this suite reads what a stock box
 * writes, and the Logger domain map is filled once and never re-read, so
 * raising it here would put the DEBUG dumps of these same controllers - which
 * this ticket does not close - into the text every case reads.
 *
 * And the exit is by _exit(): destroying an ExternProcServer at process exit
 * does kill(SIGTERM) on the last spawned child and closes libuv handles on a
 * loop this binary has stopped pumping.
 */
int main(int argc, char **argv)
{
    //Before anything has a loop or a child to duplicate.
    measureStockLogLevel();

    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_ctrlframe_cfg_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (base)
    {
        const std::string cfg = std::string(base) + "/config";
        const std::string cache = std::string(base) + "/cache";
        ::mkdir(cfg.c_str(), 0700);
        ::mkdir(cache.c_str(), 0700);

        Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                                 const_cast<char *>(cache.c_str()), true);
    }

    const int ret = RUN_ALL_TESTS();

    //The stand-in sidecars outlive us otherwise: ~ExternProcServer, which is
    //what signals them, never runs behind _exit().
    {
        std::ifstream pidf(pidFilePath().c_str());
        long pid = 0;
        while (pidf >> pid)
        {
            if (pid > 0)
                ::kill(static_cast<pid_t>(pid), SIGTERM);
        }
    }

    if (!sandboxDir().empty())
    {
        for (const Wire &w: wires())
            ::unlink((sandboxDir() + "/" + w.sidecar).c_str());
        ::unlink(pidFilePath().c_str());
        ::rmdir(sandboxDir().c_str());
    }

    fflush(nullptr);
    _exit(ret);
}
