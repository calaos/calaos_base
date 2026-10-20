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

/* The socket WagoMap sends its UDP requests from, and receives the PLC's
 * answers on: where it is bound, and what an error on it does. */

#include <gtest/gtest.h>

#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <memory>

#include "Constants.h"
#include "WagoMap.h"
#include "core/CalaosCoreFixture.h"
#include "core/ExternProcSpawnHarness.h"
#include "libuvw.h"

using namespace Calaos;

namespace
{

//TEST-NET-1 (RFC 5737): never a local address, like a real PLC.
const char *const kHostBound = "192.0.2.201";
const char *const kHostError = "192.0.2.202";
const char *const kHostDestroyed = "192.0.2.203";
const int kWagoPort = 502;

//Well past the first step of the reopen ramp.
const int kReopenBudgetMs = 10000;

struct WagoMapProbe: WagoMap
{
    static std::shared_ptr<uvw::UDPHandle> socket(const WagoMap &map)
    {
        return map.*(&WagoMapProbe::handleSrv);
    }

    static void sendHead(WagoMap &map)
    {
        (map.*(&WagoMapProbe::UDPCommand_cb))();
    }
};

int openUdpSockets()
{
    int n = 0;
    uvw::Loop::getDefault()->walk([&n](uvw::BaseHandle &h)
    {
        if (h.type() == uvw::HandleType::UDP && !h.closing())
            n++;
    });
    return n;
}

class WagoUdpSocketTest: public CalaosTest::CoreFixture
{
};

} //namespace

TEST_F(WagoUdpSocketTest, TheRequestSocketIsBoundOnAnEphemeralWildcardPort)
{
    auto sock = WagoMapProbe::socket(WagoMap::Instance(kHostBound, kWagoPort));
    ASSERT_TRUE(sock);

    const uvw::Addr local = sock->sock();
    EXPECT_EQ("0.0.0.0", local.ip);
    EXPECT_NE(0u, local.port)
        << "the socket is not bound: binding to the PLC's address was refused";
    EXPECT_NE((unsigned)WAGO_LISTEN_PORT, local.port)
        << "this port belongs to the UDPServer that receives PLC events";
}

TEST_F(WagoUdpSocketTest, AnErrorClosesTheSocketAndReopensALaterOne)
{
    WagoMap &map = WagoMap::Instance(kHostError, kWagoPort);
    auto before = WagoMapProbe::socket(map);
    ASSERT_TRUE(before);

    map.udpProcessError();

    EXPECT_TRUE(before->closing()) << "the failed socket was left open";
    EXPECT_FALSE(WagoMapProbe::socket(map));

    //A request sent meanwhile is dropped, not sent on a missing socket.
    WagoMapProbe::sendHead(map);

    ASSERT_TRUE(CalaosTest::ExternProcSpawn::runLoopUntil([&map]()
    {
        return WagoMapProbe::socket(map) != nullptr;
    }, kReopenBudgetMs)) << "no socket was reopened after the error";

    auto after = WagoMapProbe::socket(map);
    EXPECT_NE(before.get(), after.get());
    EXPECT_NE(0u, after->sock().port);
}

TEST_F(WagoUdpSocketTest, DestroyingTheMapCancelsAPendingReopen)
{
    WagoMap &map = WagoMap::Instance(kHostDestroyed, kWagoPort);
    std::weak_ptr<uvw::UDPHandle> closed = WagoMapProbe::socket(map);
    map.udpProcessError();

    WagoMap::stopAllWagoMaps();
    ASSERT_EQ(0, openUdpSockets()) << "a destroyed map left its socket open";

    //Past the reopen delay: a timer left behind would run on the freed map.
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    CalaosTest::ExternProcSpawn::runLoopUntil([&end]()
    {
        return std::chrono::steady_clock::now() > end;
    }, kReopenBudgetMs);

    EXPECT_TRUE(closed.expired()) << "the closed socket was never released";
    EXPECT_EQ(0, openUdpSockets()) << "a socket was reopened for a destroyed map";
}

//Same exit as core/WagoRespawnRamp_test: a WagoMap left alive at exit would
//signal its sidecar and close handles on a loop nothing pumps anymore.
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    CalaosTest::ExternProcSpawn::reapChildren(1000);
    CalaosTest::ExternProcSpawn::removeOwnSockets();

    fflush(nullptr);
    _exit(ret);
}
