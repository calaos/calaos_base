/******************************************************************************
 **  Copyright (c) 2026, Calaos. All Rights Reserved.
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

/*
 * Klf200Ctrl, Klf200OutputShutterSmart and Klf200InputSwitch through the
 * shipped path: a real controller spawns a stand-in calaos_klf200 (a shell
 * script that sleeps, found through CALAOS_BIN_PREFIX), and a fake sidecar
 * connects to the unix socket the controller bound, reads the frames it is
 * sent and answers with the frames the python end would send. Same
 * arrangement as core/SidecarErrorSecret_test.cpp.
 *
 * The controller is a never destroyed singleton: every case starts by
 * putting the link in the state it needs (process restarted, process
 * connected, gateway up) instead of relying on the order of the cases.
 */

#include <gtest/gtest.h>

#include <dirent.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "CalaosCoreFixture.h"
#include "IOChangeHub.h"
#include "Klf200Ctrl.h"
#include "Klf200InputSwitch.h"
#include "Klf200OutputShutterSmart.h"
#include "OutputShutter.h"
#include "libuvw.h"

using namespace Calaos;
using CalaosTest::CoreFixture;

namespace
{

const char *const kHost = "192.0.2.80";
const char *const kPassword = "mot-de-passe-klf";
const char *const kOtherPassword = "autre-mot-de-passe-klf";

const std::string &sandboxDir()
{
    static std::string dir = []() -> std::string
    {
        char tmpl[] = "/tmp/calaos_klf200_ctrl_XXXXXX";
        const char *d = ::mkdtemp(tmpl);
        return d ? std::string(d) : std::string();
    }();
    return dir;
}

std::string pidFilePath()
{
    return sandboxDir() + "/calaos_klf200.pid";
}

std::string readPid()
{
    std::ifstream f(pidFilePath());
    std::string pid;
    f >> pid;
    return pid;
}

bool installSidecarStandIn()
{
    if (sandboxDir().empty())
        return false;

    const std::string script = sandboxDir() + "/calaos_klf200";
    {
        std::ofstream f(script.c_str(), std::ios::out | std::ios::trunc);
        if (!f.is_open())
            return false;
        //A clean exit on SIGTERM: ExternProcServer counts a death by signal
        //as a crash and slows the next relaunch down, a status 0 resets it
        f << "#!/bin/sh\n"
             "echo $$ > '"
          << pidFilePath() << "'\n"
                              "sleep 120 &\n"
                              "trap 'kill $! 2>/dev/null; exit 0' TERM\n"
                              "wait\n";
        if (!f.good())
            return false;
    }
    if (::chmod(script.c_str(), 0755) != 0)
        return false;

    return ::setenv("CALAOS_BIN_PREFIX", sandboxDir().c_str(), 1) == 0;
}

//Run the default loop until pred() holds, bounded on wall clock
bool pumpLoopUntil(const std::function<bool()> &pred, int timeoutMs)
{
    auto loop = uvw::Loop::getDefault();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
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

void pumpLoopFor(int ms)
{
    pumpLoopUntil([]()
                  {
        return false;
    },
                  ms);
}

std::string findSocket()
{
    const std::string prefix = "calaos_proc_";
    const std::string suffix = std::string("_klf200_") + std::to_string(static_cast<long>(::getpid()));

    DIR *d = ::opendir("/tmp");
    if (!d)
        return std::string();

    std::string found;
    for (struct dirent *e = ::readdir(d); e; e = ::readdir(d))
    {
        const std::string name = e->d_name;
        if (name.size() > prefix.size() + suffix.size() &&
            name.compare(0, prefix.size(), prefix) == 0 &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
            found = "/tmp/" + name;
            break;
        }
    }
    ::closedir(d);
    return found;
}

//What a sidecar is on this wire: a peer of the unix socket speaking the
//5 byte header of ExternProcMessage, both ways.
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

        if (::connect(fd, (struct sockaddr *)&remote, ::strlen(remote.sun_path) + sizeof(remote.sun_family)) != 0)
        {
            ::close(fd);
            fd = -1;
            return false;
        }
        return true;
    }

    bool connected() const { return fd >= 0; }

    void send(const std::string &payload) const
    {
        std::string frame;
        frame.push_back(static_cast<char>(0x21));
        const uint32_t len = static_cast<uint32_t>(payload.size());
        frame.push_back(static_cast<char>(len >> 24));
        frame.push_back(static_cast<char>(len >> 16));
        frame.push_back(static_cast<char>(len >> 8));
        frame.push_back(static_cast<char>(len));
        frame.append(payload);
        ASSERT_EQ(static_cast<ssize_t>(frame.size()), ::send(fd, frame.c_str(), frame.size(), 0));
    }

    //Every complete frame received so far, without blocking
    std::vector<std::string> poll()
    {
        char buf[4096];
        for (;;)
        {
            const ssize_t n = ::recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
            if (n <= 0)
                break;
            pending.append(buf, n);
        }

        std::vector<std::string> frames;
        while (pending.size() >= 5)
        {
            const uint32_t len = (uint32_t(uint8_t(pending[1])) << 24) | (uint32_t(uint8_t(pending[2])) << 16) |
                                 (uint32_t(uint8_t(pending[3])) << 8) | uint32_t(uint8_t(pending[4]));
            if (pending.size() < 5 + len)
                break;
            frames.push_back(pending.substr(5, len));
            pending.erase(0, 5 + len);
        }
        return frames;
    }

private:
    int fd = -1;
    std::string pending;
};

FakeSidecar &sidecar()
{
    static FakeSidecar *s = nullptr;
    if (s)
        return *s;

    s = new FakeSidecar();
    Klf200Ctrl::Instance();
    if (s->connectTo(findSocket()))
        pumpLoopFor(200); //let the server accept the peer
    return *s;
}

std::string registerFrame(const std::string &password, int node, bool rain)
{
    return std::string("{\"action\":\"register\",\"host\":\"") + kHost + "\",\"node_id\":" + std::to_string(node) +
           ",\"password\":\"" + password + "\",\"rain\":" + (rain ? "true" : "false") + "}";
}

std::string positionFrame(int node, int position)
{
    return std::string("{\"action\":\"set_position\",\"host\":\"") + kHost + "\",\"node_id\":" + std::to_string(node) +
           ",\"position\":" + std::to_string(position) + "}";
}

std::string stopFrame(int node)
{
    return std::string("{\"action\":\"stop\",\"host\":\"") + kHost + "\",\"node_id\":" + std::to_string(node) + "}";
}

std::string nodeEvent(int node, int position, const std::string &moving, const std::string &type = "RollerShutter")
{
    return std::string("{\"event\":\"node\",\"host\":\"") + kHost + "\",\"node_id\":" + std::to_string(node) +
           ",\"position\":" + std::to_string(position) + ",\"moving\":\"" + moving + "\",\"node_type\":\"" + type +
           "\",\"name\":\"Bureau\",\"rain_sensor\":" + (type == "Window" ? "true" : "false") + "}";
}

std::string gatewayStatus(bool up, const std::string &host = kHost)
{
    return "{\"status\":\"gateway\",\"host\":\"" + host + "\",\"connected\":" + (up ? "true" : "false") + "}";
}

class Klf200CtrlTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        ASSERT_FALSE(sandboxDir().empty());
        ASSERT_TRUE(sidecar().connected()) << "the fake sidecar could not reach the controller socket";
        loadConfig();
        restartProcess();
        if (HasFatalFailure())
            return;
        frames(); //start every case with an empty inbox
    }

    void TearDown() override
    {
        captureStop();
        CoreFixture::TearDown();
    }

    /* Stop the stand-in: the controller hears the exit (links down) and
     * relaunches it. The fake sidecar stays connected to the socket. */
    void restartProcess()
    {
        std::string before;
        ASSERT_TRUE(pumpLoopUntil([&]()
                                  {
            before = readPid();
            return !before.empty();
        },
                                  5000));
        ::kill(std::stoi(before), SIGTERM);
        ASSERT_TRUE(pumpLoopUntil([&]()
                                  {
            const std::string p = readPid();
            return !p.empty() && p != before;
        },
                                  10000))
            << "the controller did not relaunch the stand-in";
    }

    //The process connects: every registered node is sent again
    void processConnected()
    {
        sidecar().send("{\"status\":\"connected\"}");
        pumpLoopFor(50);
    }

    void gatewayUp(bool up = true, const std::string &host = kHost)
    {
        sidecar().send(gatewayStatus(up, host));
        pumpLoopFor(50);
    }

    void deliver(const std::string &frame)
    {
        sidecar().send(frame);
        pumpLoopFor(50);
    }

    //Frames sent by the controller since the last call
    std::vector<std::string> frames(int ms = 50)
    {
        pumpLoopFor(ms);
        return sidecar().poll();
    }

    IOBase *shutter(const std::string &id, int node, const std::string &password = kPassword)
    {
        return createIO({ { "type", "Klf200OutputShutterSmart" },
                          { "id", id },
                          { "name", id },
                          { "host", kHost },
                          { "password", password },
                          { "node_id", std::to_string(node) } });
    }

    IOBase *rainSensor(const std::string &id, int node, const std::string &password = kPassword)
    {
        return createIO({ { "type", "Klf200InputSwitch" },
                          { "id", id },
                          { "name", id },
                          { "host", kHost },
                          { "password", password },
                          { "node_id", std::to_string(node) } });
    }

    void captureStart()
    {
        captured.str("");
        saved = std::cout.rdbuf(captured.rdbuf());
    }

    std::string captureStop()
    {
        if (saved)
        {
            std::cout.rdbuf(saved);
            saved = nullptr;
        }
        return captured.str();
    }

    std::ostringstream captured;
    std::streambuf *saved = nullptr;
};

using Frames = std::vector<std::string>;

} //namespace

TEST_F(Klf200CtrlTest, RegistrationWaitsForTheProcessThenSendsTheMergedRecord)
{
    ASSERT_NE(nullptr, shutter("klf_s1", 3));
    captureStart();
    ASSERT_NE(nullptr, rainSensor("klf_r1", 3, kOtherPassword));
    const std::string log = captureStop();

    //Same gateway, another password: said, never shown
    EXPECT_NE(std::string::npos, log.find("password differs from first registration")) << log;
    EXPECT_EQ(std::string::npos, log.find(kOtherPassword)) << log;
    EXPECT_EQ(std::string::npos, log.find(kPassword)) << log;

    EXPECT_EQ(Frames(), frames()) << "nothing can be sent before the process connects";

    processConnected();
    EXPECT_EQ(Frames({ registerFrame(kPassword, 3, true) }), frames());

    //A new IO while the process runs: the stored record (first password,
    //rain kept), not what this IO carried
    ASSERT_NE(nullptr, shutter("klf_s2", 3, kOtherPassword));
    EXPECT_EQ(Frames({ registerFrame(kPassword, 3, true) }), frames());
}

TEST_F(Klf200CtrlTest, OrdersAreRefusedWhileTheLinkIsDown)
{
    IOBase *s = shutter("klf_down", 4);
    ASSERT_NE(nullptr, s);
    const std::string cmdBefore = s->get_command_string();

    //Process down
    EXPECT_FALSE(s->set_value(std::string("up")));

    //Process up, gateway session down
    processConnected();
    frames();
    EXPECT_FALSE(s->set_value(std::string("down")));
    EXPECT_FALSE(s->set_value(std::string("stop")));

    //Up, then down again
    gatewayUp();
    gatewayUp(false);
    EXPECT_FALSE(s->set_value(std::string("set 30")));

    //A gateway this end never registered does not open the link
    gatewayUp(true, "192.0.2.99");
    EXPECT_FALSE(s->set_value(std::string("set 30")));

    EXPECT_EQ(Frames(), frames());
    EXPECT_EQ(cmdBefore, s->get_command_string()) << "a refused order must not be published";
}

TEST_F(Klf200CtrlTest, OrdersBecomeAbsolutePositions)
{
    IOBase *s = shutter("klf_orders", 5);
    ASSERT_NE(nullptr, s);
    processConnected();
    gatewayUp();
    frames();

    deliver(nodeEvent(5, 40, "stop"));
    EXPECT_EQ(40, (int)s->get_value_double());

    EXPECT_TRUE(s->set_value(std::string("up")));
    EXPECT_EQ("up", s->get_command_string());
    EXPECT_TRUE(s->set_value(std::string("down")));
    EXPECT_TRUE(s->set_value(std::string("stop")));
    EXPECT_TRUE(s->set_value(std::string("set 25")));
    EXPECT_EQ("set 25", s->get_command_string());
    EXPECT_TRUE(s->set_value(std::string("set 250")));
    EXPECT_EQ("set 100", s->get_command_string());
    EXPECT_TRUE(s->set_value(std::string("up 10")));
    EXPECT_TRUE(s->set_value(std::string("down 10")));
    EXPECT_TRUE(s->set_value(std::string("up 500")));
    EXPECT_EQ("up 100", s->get_command_string()) << "the percentage is clamped before it is published";
    EXPECT_TRUE(s->set_value(std::string("down 500")));
    EXPECT_EQ("down 100", s->get_command_string());

    EXPECT_EQ(Frames({ positionFrame(5, 0),
                       positionFrame(5, 100),
                       stopFrame(5),
                       positionFrame(5, 25),
                       positionFrame(5, 100),
                       positionFrame(5, 30),
                       positionFrame(5, 50),
                       positionFrame(5, 0),
                       positionFrame(5, 100) }),
              frames());

    //Refused without a frame
    const std::string cmd = s->get_command_string();
    for (const char *bad: { "set x", "up x", "down x", "set_state x", "impulse up 200", "calibrate", "fly" })
        EXPECT_FALSE(s->set_value(std::string(bad))) << bad;
    EXPECT_EQ(cmd, s->get_command_string());
    EXPECT_EQ(Frames(), frames());

    //set_state moves the IO only
    EXPECT_TRUE(s->set_value(std::string("set_state 70")));
    EXPECT_EQ(70, (int)s->get_value_double());
    EXPECT_EQ(Frames(), frames());
}

TEST_F(Klf200CtrlTest, ToggleFollowsTheDirectionTheGatewayReports)
{
    IOBase *s = shutter("klf_toggle", 6);
    ASSERT_NE(nullptr, s);
    processConnected();
    gatewayUp();
    frames();

    //Stopped, last direction up (the default): toggle closes
    EXPECT_TRUE(s->set_value(std::string("toggle")));
    EXPECT_EQ(Frames({ positionFrame(6, 100) }), frames());

    //Moving: toggle stops
    deliver(nodeEvent(6, 30, "down"));
    EXPECT_TRUE(s->set_value(std::string("toggle")));
    EXPECT_EQ(Frames({ stopFrame(6) }), frames());

    //Stopped after going down: toggle opens
    deliver(nodeEvent(6, 60, "stop"));
    EXPECT_TRUE(s->set_value(std::string("toggle")));
    EXPECT_EQ(Frames({ positionFrame(6, 0) }), frames());
}

TEST_F(Klf200CtrlTest, ACachedDirectionIsNotTrustedAfterARestart)
{
    IOBase *s = shutter("klf_cache", 7);
    ASSERT_NE(nullptr, s);
    processConnected();
    gatewayUp();
    deliver(nodeEvent(7, 30, "down")); //cached as moving down
    ASSERT_EQ("down 30", s->get_value_string());
    ASSERT_TRUE(deleteIO(s));
    frames();

    //Same id again: the cache says "moving down", only the gateway knows.
    //Stopped with last direction down, toggle opens instead of stopping.
    s = shutter("klf_cache", 7);
    ASSERT_NE(nullptr, s);
    EXPECT_EQ("stop 30", s->get_value_string());
    frames();
    EXPECT_TRUE(s->set_value(std::string("toggle")));
    EXPECT_EQ(Frames({ positionFrame(7, 0) }), frames());
}

TEST_F(Klf200CtrlTest, NodeEventsMoveTheIOAndOnlyAChangeIsPublished)
{
    IOBase *s = shutter("klf_events", 8);
    ASSERT_NE(nullptr, s);
    processConnected();
    gatewayUp();

    int notified = 0;
    auto sub = IOChangeHub::Instance().subscribe("klf_events", [&notified](IOBase &)
                                                 {
        notified++;
    });

    deliver(nodeEvent(8, 20, "up"));
    EXPECT_EQ("up 20", s->get_value_string());
    EXPECT_EQ(1, notified);

    deliver(nodeEvent(8, 20, "up"));
    EXPECT_EQ(1, notified) << "an identical report must not be published again";

    deliver(nodeEvent(8, 0, "stop"));
    EXPECT_EQ("stop 0", s->get_value_string());
    EXPECT_EQ(2, notified);

    //Unknown position: the direction alone is applied
    deliver(nodeEvent(8, -1, "down"));
    EXPECT_EQ("down 0", s->get_value_string());
    EXPECT_EQ(3, notified);

    //A rain event and an event of another node are not for this IO
    deliver(std::string("{\"event\":\"rain\",\"host\":\"") + kHost + "\",\"node_id\":8,\"rain\":true}");
    deliver(nodeEvent(9, 50, "stop"));
    EXPECT_EQ(3, notified);

    //After a set_state, the same report brings the real position back
    deliver(nodeEvent(8, 10, "stop"));
    ASSERT_TRUE(s->set_value(std::string("set_state 70")));
    const int afterSetState = notified;
    deliver(nodeEvent(8, 10, "stop"));
    EXPECT_EQ("stop 10", s->get_value_string());
    EXPECT_EQ(afterSetState + 1, notified);
}

TEST_F(Klf200CtrlTest, OnlyAWindowGetsAStyleAndAChosenOneStays)
{
    IOBase *s = shutter("klf_window", 10);
    ASSERT_NE(nullptr, s);
    IOBase *roller = shutter("klf_roller", 16);
    ASSERT_NE(nullptr, roller);
    IOBase *chosen = createIO({ { "type", "Klf200OutputShutterSmart" },
                                { "id", "klf_chosen" },
                                { "name", "klf_chosen" },
                                { "host", kHost },
                                { "password", kPassword },
                                { "node_id", "11" },
                                { "io_style", "shutter" } });
    ASSERT_NE(nullptr, chosen);
    EXPECT_FALSE(s->get_params().Exists("io_style"));

    processConnected();
    deliver(nodeEvent(10, 0, "stop", "Window"));
    EXPECT_EQ("window", s->get_param("io_style"));

    //A roller shutter stays without a style, like any other shutter
    deliver(nodeEvent(16, 0, "stop", "RollerShutter"));
    EXPECT_FALSE(roller->get_params().Exists("io_style"));

    //A style the user chose is never overwritten
    deliver(nodeEvent(11, 0, "stop", "Window"));
    EXPECT_EQ("shutter", chosen->get_param("io_style"));
}

/* Apps were not changed with this driver: an existing shutter must look
 * exactly as before, no io_style in io.xml nor in the JSON API. */
TEST_F(Klf200CtrlTest, AnExistingShutterGetsNoStyle)
{
    Params plainParams = { { "id", "plain" }, { "name", "plain" } };
    Params smartParams = { { "id", "smart" }, { "name", "smart" } };
    std::unique_ptr<Calaos::OutputShutter> plain(new Calaos::OutputShutter(plainParams));
    std::unique_ptr<Calaos::OutputShutterSmart> smart(new Calaos::OutputShutterSmart(smartParams));

    for (IOBase *io: { static_cast<IOBase *>(plain.get()), static_cast<IOBase *>(smart.get()) })
    {
        EXPECT_FALSE(io->get_params().Exists("io_style")) << io->get_param("id");

        bool documented = false;
        const Json doc = io->getDoc()->genDocJson();
        for (const auto &p: doc.at("parameters"))
        {
            if (p.at("name").get<std::string>() != "io_style")
                continue;
            documented = true;
            EXPECT_EQ("false", p.at("mandatory").get<std::string>()) << io->get_param("id");
            EXPECT_EQ("", p.value("default", std::string())) << io->get_param("id");
        }
        EXPECT_TRUE(documented) << io->get_param("id");
    }
}

TEST_F(Klf200CtrlTest, RainEventsDriveTheSwitch)
{
    IOBase *r = rainSensor("klf_rain", 12);
    ASSERT_NE(nullptr, r);
    processConnected();
    EXPECT_EQ(Frames({ registerFrame(kPassword, 12, true) }), frames());
    EXPECT_FALSE(r->get_value_bool());

    deliver(std::string("{\"event\":\"rain\",\"host\":\"") + kHost + "\",\"node_id\":12,\"rain\":true}");
    EXPECT_TRUE(r->get_value_bool());
    deliver(std::string("{\"event\":\"rain\",\"host\":\"") + kHost + "\",\"node_id\":12,\"rain\":false}");
    EXPECT_FALSE(r->get_value_bool());

    //A node without sensor is said once
    captureStart();
    deliver(nodeEvent(12, 0, "stop", "RollerShutter"));
    deliver(nodeEvent(12, 10, "stop", "RollerShutter"));
    const std::string log = captureStop();
    size_t count = 0;
    for (size_t p = log.find("has no rain sensor"); p != std::string::npos; p = log.find("has no rain sensor", p + 1))
        count++;
    EXPECT_EQ(1u, count) << log;
    EXPECT_FALSE(r->get_value_bool());
}

TEST_F(Klf200CtrlTest, AnIOWithoutItsParametersStaysOutOfTheWire)
{
    processConnected();
    gatewayUp();
    frames();

    IOBase *noHost = createIO({ { "type", "Klf200OutputShutterSmart" }, { "id", "klf_nohost" }, { "name", "a" }, { "password", kPassword }, { "node_id", "1" } });
    IOBase *noPassword = createIO({ { "type", "Klf200OutputShutterSmart" }, { "id", "klf_nopw" }, { "name", "b" }, { "host", kHost }, { "node_id", "1" } });
    IOBase *noNode = createIO({ { "type", "Klf200OutputShutterSmart" }, { "id", "klf_nonode" }, { "name", "c" }, { "host", kHost }, { "password", kPassword }, { "node_id", "x" } });
    IOBase *rainNoHost = createIO({ { "type", "Klf200InputSwitch" }, { "id", "klf_rnohost" }, { "name", "d" }, { "password", kPassword }, { "node_id", "1" } });
    IOBase *rainNoPassword = createIO({ { "type", "Klf200InputSwitch" }, { "id", "klf_rnopw" }, { "name", "e" }, { "host", kHost }, { "node_id", "1" } });
    IOBase *rainNoNode = createIO({ { "type", "Klf200InputSwitch" }, { "id", "klf_rnonode" }, { "name", "f" }, { "host", kHost }, { "password", kPassword } });
    for (IOBase *io: { noHost, noPassword, noNode, rainNoHost, rainNoPassword, rainNoNode })
        ASSERT_NE(nullptr, io);

    captureStart();
    EXPECT_FALSE(noHost->set_value(std::string("up")));
    EXPECT_FALSE(noPassword->set_value(std::string("down")));
    EXPECT_FALSE(noNode->set_value(std::string("set_state 20")));
    const std::string log = captureStop();
    EXPECT_NE(std::string::npos, log.find("not configured")) << log;

    EXPECT_EQ(Frames(), frames());
    EXPECT_FALSE(rainNoHost->get_value_bool());
}

TEST_F(Klf200CtrlTest, TheLastIOOfANodeUnregistersIt)
{
    IOBase *s = shutter("klf_unreg_s", 13);
    IOBase *r = rainSensor("klf_unreg_r", 13);
    ASSERT_NE(nullptr, s);
    ASSERT_NE(nullptr, r);
    processConnected();
    frames();

    ASSERT_TRUE(deleteIO(s));
    EXPECT_EQ(Frames(), frames()) << "the rain sensor still uses node 13";

    ASSERT_TRUE(deleteIO(r));
    EXPECT_EQ(Frames({ std::string("{\"action\":\"unregister\",\"host\":\"") + kHost + "\",\"node_id\":13}" }), frames());

    //Once gone, a new password is the one sent
    ASSERT_NE(nullptr, shutter("klf_unreg_new", 13, kOtherPassword));
    EXPECT_EQ(Frames({ registerFrame(kOtherPassword, 13, false) }), frames());
}

TEST_F(Klf200CtrlTest, StatusFramesAreLoggedWithOnlyWhatThisEndCanVouchFor)
{
    ASSERT_NE(nullptr, shutter("klf_status", 14));
    processConnected();

    captureStart();
    deliver(std::string("{\"status\":\"error\",\"host\":\"") + kHost +
            "\",\"code\":\"node_not_found\",\"node_id\":14,\"message\":\"texte-de-pyvlx\"}");
    deliver("{\"status\":\"error\",\"host\":\"hote-inconnu\",\"code\":\"code-invente\",\"message\":\"x\"}");
    deliver(gatewayStatus(true, "hote-inconnu"));
    deliver(gatewayStatus(true));
    deliver("{not json");
    deliver(std::string("{\"event\":\"node\",\"host\":\"") + kHost + "\",\"node_id\":2.5}");
    deliver("{\"status\":\"something\"}");
    const std::string log = captureStop();

    EXPECT_NE(std::string::npos, log.find("Klf200 process error: node_not_found, gateway " + std::string(kHost) + ", node 14"))
        << log;
    EXPECT_NE(std::string::npos, log.find("Klf200 process error: unspecified, gateway unknown")) << log;
    EXPECT_NE(std::string::npos, log.find("KLF 200 unknown connected")) << log;
    EXPECT_NE(std::string::npos, log.find("KLF 200 " + std::string(kHost) + " connected")) << log;
    EXPECT_NE(std::string::npos, log.find("Error parsing json message (9 bytes)")) << log;
    EXPECT_NE(std::string::npos, log.find("Event without a valid node_id, dropped")) << log;
    for (const char *needle: { "texte-de-pyvlx", "hote-inconnu", "code-invente" })
        EXPECT_EQ(std::string::npos, log.find(needle)) << needle << " reached the log:\n"
                                                       << log;
}

TEST_F(Klf200CtrlTest, TheIODocOffersOnlyWhatSetValueAccepts)
{
    IOBase *s = shutter("klf_doc", 15);
    ASSERT_NE(nullptr, s);
    ASSERT_NE(nullptr, s->getDoc());

    const Json doc = s->getDoc()->genDocJson();
    std::vector<std::string> actions;
    for (const auto &a: doc.at("actions"))
        actions.push_back(a.at("name").get<std::string>());

    for (const char *gone: { "impulse up 200", "impulse down 200", "calibrate" })
        EXPECT_EQ(actions.end(), std::find(actions.begin(), actions.end(), gone)) << gone;
    for (const char *kept: { "up", "down", "stop", "toggle", "set 50", "up 5", "down 5", "set_state 50" })
        EXPECT_NE(actions.end(), std::find(actions.begin(), actions.end(), kept)) << kept;

    //Every documented action is accepted once the link is up
    processConnected();
    gatewayUp();
    for (const auto &name: actions)
        EXPECT_TRUE(s->set_value(name)) << name;
}

int main(int argc, char **argv)
{
    //Before anything reaches Prefix or the controller
    if (!installSidecarStandIn())
    {
        std::cerr << "cannot install the calaos_klf200 stand-in" << std::endl;
        return 1;
    }

    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    const std::string pid = readPid();
    if (!pid.empty())
        ::kill(std::stoi(pid), SIGTERM);
    std::error_code ec;
    std::filesystem::remove_all(sandboxDir(), ec);
    _exit(ret); //the controller is never destroyed, nothing to wait for
}
