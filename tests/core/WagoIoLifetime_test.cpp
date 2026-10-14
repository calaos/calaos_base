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
 ******************************************************************************/

/******************************************************************************
 * A WagoMap is shared by every IO of a PLC and outlives them. A deleted Wago
 * IO (deleteIO, a device discarded at load, a configuration reload) must
 * leave nothing behind on it: neither its onWagoConnected slot nor the reply
 * slot of a request still in flight. Each case checks it under ASan by
 * replaying the event after the delete.
 *
 * One host per case, TEST-NET-1 (RFC 5737): bind() fails, no PLC is needed
 * and parallel `make check` peers never share a WagoMap.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "CalaosCoreFixture.h"
#include "WagoMap.h"

using namespace Calaos;

namespace
{

//Reaches the pending Modbus requests through pointers to member formed on a
//derived class; nothing is ever constructed from this type.
struct WagoMapProbe: WagoMap
{
    static size_t pending(WagoMap &map)
    {
        return (map.*(&WagoMapProbe::mbus_commands)).size();
    }

    //Answer every pending Modbus request, the way calaos_wago would
    static void answerAll(WagoMap &map, bool status)
    {
        std::vector<std::string> ids;
        for (const auto &entry: map.*(&WagoMapProbe::mbus_commands))
            ids.push_back(entry.first);

        for (const std::string &id: ids)
        {
            Json reply = { { "id", id },
                           { "status", status ? "true" : "false" },
                           { "address", "0" },
                           { "count", "1" },
                           { "values", Json::array({ "1" }) } };
            (map.*(&WagoMapProbe::processNewMessage))(reply.dump());
        }
    }
};

std::string hostFor(const std::string &suffix)
{
    return "192.0.2." + suffix;
}

WagoMap &mapOf(const std::string &host)
{
    return WagoMap::Instance(host, 502);
}

Params wagoParams(const std::string &type, const std::string &id, const std::string &host)
{
    return { { "type", type },
             { "id", id },
             { "name", type },
             { "host", host },
             { "var", "12" },
             { "enabled", "true" },
             { "visible", "true" } };
}

//The four types that read themselves back when the PLC (re)connects
const std::vector<std::string> &reconnectTypes()
{
    static const std::vector<std::string> types = {
        "WIAnalog",
        "WITemp",
        "WOAnalog",
        "WODigital",
    };
    return types;
}

} //namespace

class WagoIoLifetimeTest: public CalaosTest::CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig();
    }
};

TEST_F(WagoIoLifetimeTest, AReconnectReadsEveryLiveWagoIoBack)
{
    int n = 0;
    for (const std::string &type: reconnectTypes())
    {
        const std::string host = hostFor(Utils::to_string(10 + n));
        IOBase *o = createIO(wagoParams(type, "fb4_live_" + Utils::to_string(n++), host));
        ASSERT_NE(o, nullptr) << type;

        const size_t before = WagoMapProbe::pending(mapOf(host));
        mapOf(host).onWagoConnected.emit();
        EXPECT_EQ(WagoMapProbe::pending(mapOf(host)), before + 1) << type;
    }
}

TEST_F(WagoIoLifetimeTest, ADeletedWagoIoIsNotCalledWhenThePlcReconnects)
{
    int n = 0;
    for (const std::string &type: reconnectTypes())
    {
        const std::string host = hostFor(Utils::to_string(20 + n));
        const size_t slotsBefore = mapOf(host).onWagoConnected.size();

        IOBase *o = createIO(wagoParams(type, "fb4_gone_" + Utils::to_string(n++), host));
        ASSERT_NE(o, nullptr) << type;
        ASSERT_TRUE(deleteIO(o)) << type;

        EXPECT_EQ(mapOf(host).onWagoConnected.size(), slotsBefore)
            << type << " is still connected to WagoMap::onWagoConnected";
        mapOf(host).onWagoConnected.emit();
    }
}

//The reply of a read issued for an IO that died meanwhile
TEST_F(WagoIoLifetimeTest, ADeletedWagoIoIgnoresTheReplyToItsPendingRead)
{
    int n = 0;
    for (const std::string &type: reconnectTypes())
    {
        const std::string host = hostFor(Utils::to_string(30 + n));
        IOBase *o = createIO(wagoParams(type, "fb4_read_" + Utils::to_string(n++), host));
        ASSERT_NE(o, nullptr) << type;

        mapOf(host).onWagoConnected.emit();
        ASSERT_GT(WagoMapProbe::pending(mapOf(host)), 0u) << type;

        ASSERT_TRUE(deleteIO(o)) << type;
        WagoMapProbe::answerAll(mapOf(host), true);
    }
}

//The reply of a write issued for an IO that died meanwhile
TEST_F(WagoIoLifetimeTest, ADeletedWagoOutputIgnoresTheReplyToItsPendingWrite)
{
    const std::vector<std::pair<std::string, std::string>> outputs = {
        { "WODigital", "true" },
        { "WOAnalog", "42" },
        { "WOVolet", "up" },
    };

    int n = 0;
    for (const auto &[type, order]: outputs)
    {
        const std::string host = hostFor(Utils::to_string(40 + n));
        Params p = wagoParams(type, "fb4_write_" + Utils::to_string(n++), host);
        p.Add("var_up", "7");
        p.Add("var_down", "8");
        p.Add("time", "60");

        IOBase *o = createIO(p);
        ASSERT_NE(o, nullptr) << type;

        const size_t before = WagoMapProbe::pending(mapOf(host));
        ASSERT_TRUE(o->set_value(order)) << type;
        ASSERT_GT(WagoMapProbe::pending(mapOf(host)), before) << type;

        ASSERT_TRUE(deleteIO(o)) << type;
        WagoMapProbe::answerAll(mapOf(host), false);
    }
}

//A DALI read is queued at construction and answered over UDP, possibly after
//the ballast is gone (a device discarded at load).
TEST_F(WagoIoLifetimeTest, ADeletedDaliBallastIgnoresTheReplyToItsPendingRead)
{
    const std::string host = hostFor("50");
    Params dali = { { "type", "WODali" },
                    { "id", "fb4_dali" },
                    { "name", "dali" },
                    { "host", host },
                    { "line", "1" },
                    { "address", "61" },
                    { "group", "0" },
                    { "fade_time", "1" },
                    { "enabled", "true" },
                    { "visible", "true" } };
    Params rgb = { { "type", "WODaliRVB" },
                   { "id", "fb4_dali_rgb" },
                   { "name", "dali rgb" },
                   { "host", host },
                   { "rline", "1" },
                   { "raddress", "62" },
                   { "rgroup", "0" },
                   { "rfade_time", "1" },
                   { "gline", "1" },
                   { "gaddress", "63" },
                   { "ggroup", "0" },
                   { "gfade_time", "1" },
                   { "bline", "1" },
                   { "baddress", "64" },
                   { "bgroup", "0" },
                   { "bfade_time", "1" },
                   { "enabled", "true" },
                   { "visible", "true" } };

    IOBase *a = createIO(dali);
    IOBase *b = createIO(rgb);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_TRUE(deleteIO(a));
    ASSERT_TRUE(deleteIO(b));

    WagoMap &map = mapOf(host);
    map.udpRequest_cb(true, "WAGO_GET_VERSION 3.0 750-849");
    for (int i = 0; i < 4; i++)
        map.udpRequest_cb(true, "REPLY 1 77");
}
