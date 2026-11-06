/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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

//A Reolink switch and an A/V receiver IO reach their controller singleton
//from their destructor, and ~ListeRoom() destroys the IOs still in a room at
//exit. The controller must still be there, although it is only built by the
//first IO, after ListeRoom.
//A forked child builds ListeRoom first, as calaos_server's main() does, then
//the IOs, then exits. Destructors of probes write one byte each on a pipe:
//the parent reads the order in which things died. Under AddressSanitizer a
//controller destroyed first also aborts the child.

#include <gtest/gtest.h>

#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "AVRManager.h"
#include "AVReceiver.h"
#include "IOBase.h"
#include "ListeRoom.h"
#include "ReolinkCtrl.h"
#include "ReolinkInputSwitch.h"
#include "Room.h"

using namespace Calaos;

namespace
{

int probeFd = -1;

void mark(char c)
{
    if (probeFd >= 0 && ::write(probeFd, &c, 1) != 1)
        probeFd = -1;
}

struct Probe
{
    explicit Probe(char c):
        tag(c)
    {
    }
    ~Probe() { mark(tag); }
    char tag;
};

class ProbeIO: public IOBase
{
public:
    explicit ProbeIO(Params &p):
        IOBase(p, IOBase::IO_INPUT)
    {
    }
    ~ProbeIO() override { mark('I'); }
    DATA_TYPE get_type() override { return TSTRING; }
};

//The room holds the probe first: rooms destroy their IOs front to back.
Room *roomWithProbe(const std::string &base)
{
    const std::string cfg = base + "/config";
    const std::string cache = base + "/cache";
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(cache.c_str(), 0700);
    Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                             const_cast<char *>(cache.c_str()), true);

    Room *room = new Room("Salle", "salon", 0);
    ListeRoom::Instance().Add(room);

    Params p = { { "id", "fb5_teardown_probe" }, { "name", "Sonde" } };
    room->AddIO(new ProbeIO(p));

    return room;
}

//'C' when the controller's registry dies, never if the controller outlives
//the process.
[[noreturn]] void reolinkChild(const std::string &base)
{
    Room *room = roomWithProbe(base);

    Params p = { { "id", "fb5_reolink" },
                 { "name", "Camera" },
                 { "hostname", "192.0.2.41" },
                 { "username", "fb5" },
                 { "password", "fb5pass" },
                 { "event_type", "motion" } };
    room->AddIO(new ReolinkInputSwitch(p));

    auto probe = std::make_shared<Probe>('C');
    ReolinkCtrl::Instance().registerCamera(ReolinkTypes::Hostname("192.0.2.42"),
                                           ReolinkTypes::Username("fb5"),
                                           ReolinkTypes::Password("fb5pass"),
                                           ReolinkTypes::EventType("motion"),
                                           [probe](const ReolinkTypes::Hostname &,
                                                   const ReolinkTypes::EventType &,
                                                   const ReolinkTypes::EventData &)
                                           {
                                           });
    probe.reset();

    //exit(), not _exit(): the static destructors are the subject.
    std::exit(0);
}

//'A' when the receiver dies.
[[noreturn]] void avrChild(const std::string &base)
{
    Room *room = roomWithProbe(base);

    Params p = { { "id", "fb5_avr" },
                 { "name", "Ampli" },
                 { "model", "denon" },
                 { "host", "192.0.2.43" } };
    room->AddIO(new IOAVReceiver(p));

    AVReceiver *receiver = AVRManager::Instance().getReceiver("192.0.2.43");
    if (!receiver)
        _exit(2);

    auto probe = std::make_shared<Probe>('A');
    receiver->state_changed_2.connect([probe](string, string)
                                      {
                                      });
    probe.reset();

    std::exit(0);
}

struct ChildResult
{
    bool timedOut = false;
    bool exited = false;
    int code = -1;
    int signal = 0;
    std::string order;
};

ChildResult runInChild(const std::function<void(const std::string &)> &child)
{
    ChildResult r;

    char tmpl[] = "/tmp/calaos_fb5_teardown_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
        return r;

    int fds[2];
    if (::pipe(fds) != 0)
        return r;

    //Otherwise the child flushes gtest's buffered output a second time.
    std::fflush(nullptr);
    const pid_t pid = ::fork();
    if (pid == 0)
    {
        ::close(fds[0]);
        probeFd = fds[1];
        child(base);
    }
    ::close(fds[1]);

    for (;;)
    {
        struct pollfd pfd = { fds[0], POLLIN, 0 };
        if (::poll(&pfd, 1, 30000) <= 0)
        {
            r.timedOut = true;
            ::kill(pid, SIGKILL);
            break;
        }
        char c;
        if (::read(fds[0], &c, 1) != 1)
            break;
        r.order += c;
    }
    ::close(fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    r.exited = WIFEXITED(status);
    r.code = r.exited ? WEXITSTATUS(status) : -1;
    r.signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;

    std::error_code ec;
    std::filesystem::remove_all(base, ec);

    return r;
}

} //namespace

TEST(ControllerTeardownOrderTest, AReolinkSwitchLeftAtExitFindsItsController)
{
    const ChildResult r = runInChild(reolinkChild);

    ASSERT_FALSE(r.timedOut) << "order seen: " << r.order;
    ASSERT_TRUE(r.exited) << "the child died on signal " << r.signal << ", order seen: " << r.order;
    EXPECT_EQ(r.code, 0);

    const size_t io = r.order.find('I');
    const size_t ctrl = r.order.find('C');
    ASSERT_NE(io, std::string::npos) << r.order;
    EXPECT_TRUE(ctrl == std::string::npos || ctrl > io)
        << "ReolinkCtrl was destroyed while a room still held a Reolink switch: " << r.order;
}

TEST(ControllerTeardownOrderTest, AnAvReceiverLeftAtExitFindsItsManager)
{
    const ChildResult r = runInChild(avrChild);

    ASSERT_FALSE(r.timedOut) << "order seen: " << r.order;
    ASSERT_TRUE(r.exited) << "the child died on signal " << r.signal << ", order seen: " << r.order;
    EXPECT_EQ(r.code, 0);

    //The receiver is released by the last IO using it, after the probe.
    EXPECT_EQ(r.order, "IA")
        << "AVRManager deleted the receiver while a room still held its IO";
}
