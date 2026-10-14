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

//Every Modbus or UDP request a WagoMap queues holds the reply slot of its
//caller. Once the request is answered, or the map is destroyed with the
//request still pending, nothing may keep that slot. Each slot here holds a
//shared_ptr; its weak_ptr expires when the last copy of the slot is gone.
//This is also what LeakSanitizer reported at the end of every Wago binary.
//
//One host per case, TEST-NET-1 (RFC 5737): bind() fails and no PLC is needed.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "CalaosCoreFixture.h"
#include "WagoMap.h"

using namespace Calaos;

namespace
{

WagoUdp_cb udpSlot(std::shared_ptr<int> keep)
{
    return [keep](bool, WagoTypes::UdpCommand, WagoTypes::UdpResult)
    {
    };
}

MultiBits_cb bitsSlot(std::shared_ptr<int> keep)
{
    return [keep](bool, WagoTypes::Address, WagoTypes::Count, vector<bool> &)
    {
    };
}

MultiWords_cb wordsSlot(std::shared_ptr<int> keep)
{
    return [keep](bool, WagoTypes::Address, WagoTypes::Count, vector<UWord> &)
    {
    };
}

} //namespace

class WagoCommandReleaseTest: public CalaosTest::CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig();
    }
};

TEST_F(WagoCommandReleaseTest, AnAnsweredUdpCommandReleasesItsReplySlot)
{
    WagoMap &map = WagoMap::Instance("192.0.2.71", 502);

    auto keep = std::make_shared<int>(0);
    std::weak_ptr<int> watch = keep;
    map.SendUDPCommand("WAGO_FB5_PROBE", udpSlot(keep));
    keep.reset();
    ASSERT_FALSE(watch.expired());

    //The version query of the constructor is ahead in the queue: time every
    //request out, the probe last.
    for (int i = 0; i < 8; i++)
        map.udpRequest_cb(false, "");

    EXPECT_TRUE(watch.expired()) << "the reply slot outlived its answer";
}

TEST_F(WagoCommandReleaseTest, AnAnsweredModbusReadReleasesItsReplySlot)
{
    WagoMap &map = WagoMap::Instance("192.0.2.72", 502);

    auto keep = std::make_shared<int>(0);
    std::weak_ptr<int> watch = keep;
    map.read_bits(WagoTypes::Address(0), WagoTypes::Count(1), bitsSlot(keep));
    keep.reset();
    ASSERT_FALSE(watch.expired());

    struct Probe: WagoMap
    {
        static void answerAll(WagoMap &m)
        {
            std::vector<std::string> ids;
            for (const auto &entry: m.*(&Probe::mbus_commands))
                ids.push_back(entry.first);
            for (const std::string &id: ids)
            {
                Json reply = { { "id", id },
                               { "status", "true" },
                               { "address", "0" },
                               { "count", "1" },
                               { "values", Json::array({ "1" }) } };
                (m.*(&Probe::processNewMessage))(reply.dump());
            }
        }
    };
    Probe::answerAll(map);

    EXPECT_TRUE(watch.expired()) << "the reply slot outlived its answer";
}

TEST_F(WagoCommandReleaseTest, ADestroyedMapReleasesThePendingReplySlots)
{
    WagoMap &map = WagoMap::Instance("192.0.2.73", 502);

    auto keep = std::make_shared<int>(0);
    std::weak_ptr<int> watch = keep;
    map.SendUDPCommand("WAGO_FB5_PENDING", udpSlot(keep));
    map.read_bits(WagoTypes::Address(0), WagoTypes::Count(1), bitsSlot(keep));
    map.read_words(WagoTypes::Address(0), WagoTypes::Count(1), wordsSlot(keep));
    keep.reset();
    ASSERT_FALSE(watch.expired());

    WagoMap::stopAllWagoMaps();

    EXPECT_TRUE(watch.expired()) << "a pending reply slot outlived its map";
}
