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

//A thermostat leaves a registry of live thermostats behind (for the warning on
//outside writes). ~ListeRoom() destroys the thermostats still in their room at
//exit, and each one drops itself from that registry: it must still be there,
//whatever the order in which the program built it relative to ListeRoom.
//A forked child that builds ListeRoom first, as calaos_server's main() does,
//then its first thermostat, then exits. Under AddressSanitizer a registry
//destroyed before ListeRoom aborts the child.

#include <gtest/gtest.h>

#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "InternalThermostat.h"
#include "ListeRoom.h"
#include "Room.h"

using namespace Calaos;

namespace
{

[[noreturn]] void runChild(const std::string &base)
{
    const std::string cfg = base + "/config";
    const std::string cache = base + "/cache";
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(cache.c_str(), 0700);
    Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                             const_cast<char *>(cache.c_str()), true);

    Room *room = new Room("Salle", "salon", 0);
    ListeRoom::Instance().Add(room);

    Params p = { { "id", "teardown_thermostat" }, { "name", "Thermostat" } };
    room->AddIO(new InternalThermostat(p));

    //exit(), not _exit(): the static destructors are the subject.
    std::exit(0);
}

} //namespace

TEST(ThermostatTeardownOrderTest, AThermostatLeftAtExitFindsItsRegistry)
{
    char tmpl[] = "/tmp/calaos_thermostat_teardown_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    ASSERT_NE(base, nullptr);

    //Otherwise the child flushes gtest's buffered output a second time.
    std::fflush(nullptr);
    const pid_t pid = ::fork();
    ASSERT_GE(pid, 0);
    if (pid == 0)
        runChild(base);

    int status = 0;
    ::waitpid(pid, &status, 0);

    std::error_code ec;
    std::filesystem::remove_all(base, ec);

    ASSERT_TRUE(WIFEXITED(status)) << "the child died at exit";
    EXPECT_EQ(WEXITSTATUS(status), 0);
}
