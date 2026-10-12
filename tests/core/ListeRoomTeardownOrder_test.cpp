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

//~ListeRoom() destroys the rooms still alive at exit, and each ~Room() drops
//its IOs from ListeRule and announces them through EventManager. Both are
//function-local statics too, so they must outlive ListeRoom whatever order a
//program touches them in, not only in the order calaos_server's main() forces.
//
//The interesting moment is the atexit chain of a process that never touched
//any of the three, hence a forked child of a binary that never does either.
//Destructors of probes write one byte each on a pipe: the parent reads the
//order in which things died.

#include <gtest/gtest.h>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <memory>
#include <string>

#include "EventManager.h"
#include "IOBase.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "Room.h"
#include "Rule.h"

using namespace Calaos;

namespace
{

int probeFd = -1;

void mark(char c)
{
    if (probeFd >= 0 && ::write(probeFd, &c, 1) != 1)
        probeFd = -1;
}

//Last IO of the room: when it dies, every IO before it already went through
//ListeRule and EventManager.
class ProbeIO: public IOBase
{
public:
    explicit ProbeIO(Params &p): IOBase(p, IOBase::IO_INPUT) {}
    ~ProbeIO() override { mark('I'); }
    DATA_TYPE get_type() override { return TSTRING; }
};

class ProbeRule: public Rule
{
public:
    ProbeRule(): Rule("probe", "teardown order") {}
    ~ProbeRule() override { mark('R'); }
};

struct EventManagerProbe
{
    ~EventManagerProbe() { mark('E'); }
};

[[noreturn]] void runChild(int fd, const std::string &base)
{
    probeFd = fd;

    const std::string cfg = base + "/config";
    const std::string cache = base + "/cache";
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(cache.c_str(), 0700);
    Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                             const_cast<char *>(cache.c_str()), true);

    //ListeRoom first: the order of IncomingLogStockLevel_test, which left this
    //very room alive at exit.
    Room *room = new Room("Salle", "salon", 0);
    ListeRoom::Instance().Add(room);

    //Registers itself in ListeRule's polling list from its constructor.
    ListeRoom::Instance().createIO({ { "type", "InPlageHoraire" },
                                     { "id", "teardown_time_range" },
                                     { "name", "Horaire" } }, room);

    Params p = { { "id", "teardown_probe" }, { "name", "Sonde" } };
    room->AddIO(new ProbeIO(p));

    ListeRule::Instance().Add(new ProbeRule());

    //The slot, and the probe it captures, die with EventManager::newEvent.
    auto probe = std::make_shared<EventManagerProbe>();
    EventManager::Instance().newEvent.connect([probe](const CalaosEvent &) {});
    probe.reset();

    //exit(), not _exit(): the static destructors are the subject.
    std::exit(0);
}

}

TEST(ListeRoomTeardownOrderTest, RoomsLeftAtExitDieBeforeTheRuleListAndTheEventManager)
{
    char tmpl[] = "/tmp/calaos_teardown_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    ASSERT_NE(base, nullptr);

    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);

    const pid_t pid = ::fork();
    ASSERT_GE(pid, 0);
    if (pid == 0)
    {
        ::close(fds[0]);
        runChild(fds[1], base);
    }

    ::close(fds[1]);

    std::string order;
    char c;
    while (::read(fds[0], &c, 1) == 1)
        order += c;
    ::close(fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    std::error_code ec;
    std::filesystem::remove_all(base, ec);

    //Under AddressSanitizer the use after free aborts the child; without it
    //the heap may survive, which is why the order is asserted as well.
    ASSERT_TRUE(WIFEXITED(status)) << "the child died at exit, order seen: " << order;
    EXPECT_EQ(WEXITSTATUS(status), 0);

    ASSERT_EQ(order.size(), 3u) << order;
    ASSERT_NE(std::string::npos, order.find('I')) << order;
    ASSERT_NE(std::string::npos, order.find('R')) << order;
    ASSERT_NE(std::string::npos, order.find('E')) << order;
    EXPECT_LT(order.find('I'), order.find('R'))
        << "ListeRule was destroyed while a room still had IOs to drop from it: " << order;
    EXPECT_LT(order.find('I'), order.find('E'))
        << "EventManager was destroyed while a room still had IOs to announce: " << order;
}
