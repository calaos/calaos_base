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

//A Squeezebox opens its two LMS connections from its constructor. Deleted
//while they are still connecting, it must leave nothing listening on them:
//the refusal that follows would otherwise run on the freed player and arm
//its reconnect timer. Nothing listens on port 1 of the loopback, so the
//connections are refused as soon as the loop runs.

#include <gtest/gtest.h>

#include <chrono>
#include <set>
#include <thread>

#include "CalaosCoreFixture.h"
#include "libuvw.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

//Timers armed now, by handle: a reconnect timer of an earlier case may
//expire in the middle of a later one.
std::set<const void *> armedTimers()
{
    std::set<const void *> armed;
    uvw::Loop::getDefault()->walk([&armed](uvw::BaseHandle &handle)
                                  {
        if (handle.type() == uvw::HandleType::TIMER &&
            handle.active() && !handle.closing())
            armed.insert(&handle);
    });
    return armed;
}

size_t newlyArmed(const std::set<const void *> &before)
{
    size_t n = 0;
    for (const void *h: armedTimers())
        if (!before.count(h))
            n++;
    return n;
}

std::set<const void *> openConnections()
{
    std::set<const void *> open;
    uvw::Loop::getDefault()->walk([&open](uvw::BaseHandle &handle)
                                  {
        if (handle.type() == uvw::HandleType::TCP && !handle.closing())
            open.insert(&handle);
    });
    return open;
}

void pumpFor(int ms)
{
    auto loop = uvw::Loop::getDefault();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

Params squeezeboxParams(const std::string &id)
{
    return { { "type", "Squeezebox" },
             { "id", id },
             { "name", id },
             { "host", "127.0.0.1" },
             { "port_cli", "1" },
             { "port_web", "9000" },
             { "enabled", "true" },
             { "visible", "true" } };
}

} //namespace

class SqueezeboxLifetimeTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig();
        pumpFor(100);
    }
};

//The witness: a refused connection does reach the player.
TEST_F(SqueezeboxLifetimeTest, ARefusedPlayerSchedulesItsReconnection)
{
    const auto before = armedTimers();

    IOBase *o = createIO(squeezeboxParams("fb5_squeezebox_live"));
    ASSERT_NE(o, nullptr);
    pumpFor(300);

    EXPECT_GT(newlyArmed(before), 0u);

    ASSERT_TRUE(deleteIO(o));
    pumpFor(100);
}

TEST_F(SqueezeboxLifetimeTest, APlayerDeletedWhileConnectingIsNotCalledByItsConnections)
{
    const auto before = armedTimers();
    const auto connections = openConnections();

    IOBase *o = createIO(squeezeboxParams("fb5_squeezebox_gone"));
    ASSERT_NE(o, nullptr);
    ASSERT_TRUE(deleteIO(o));
    pumpFor(300);

    EXPECT_EQ(newlyArmed(before), 0u)
        << "a deleted player armed its reconnection";
    for (const void *h: openConnections())
        EXPECT_TRUE(connections.count(h)) << "a deleted player left a connection open";
}
