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
 * RoonCtrl is shared by every RoonPlayer and keeps one callback per zone. A
 * player that is deleted must not be called by the next state of its zone.
 * RoonPlayer derives from sigc::trackable and subscribes with sigc::mem_fun:
 * the controller has to keep that as a sigc::slot for the tracking to hold.
 *
 * No calaos_roon next to the test binary: the spawn fails cleanly and the
 * states are fed through processNewMessage().
 ******************************************************************************/

#include <gtest/gtest.h>

#include <memory>

#include "RoonPlayer.h"

using namespace Calaos;

namespace
{

struct ZoneListener: public sigc::trackable
{
    int calls = 0;
    void stateUpdated(const RoonPlayerState &) { calls++; }
};

shared_ptr<RoonCtrl> roonCtrl()
{
    return RoonCtrl::Instance("127.0.0.1", 9330);
}

}

TEST(RoonZoneSubscriptionTest, AZoneStateReachesItsSubscriber)
{
    ZoneListener listener;
    roonCtrl()->subscribeZone("fb4_zone_live", sigc::mem_fun(listener, &ZoneListener::stateUpdated));

    roonCtrl()->processNewMessage(R"({"zone_id": "fb4_zone_live"})");
    roonCtrl()->processNewMessage(R"({"zone_id": "fb4_zone_other"})");

    EXPECT_EQ(listener.calls, 1);
}

TEST(RoonZoneSubscriptionTest, ADeletedSubscriberIsNotCalledByTheNextState)
{
    auto listener = std::make_unique<ZoneListener>();
    roonCtrl()->subscribeZone("fb4_zone_gone", sigc::mem_fun(*listener, &ZoneListener::stateUpdated));

    roonCtrl()->processNewMessage(R"({"zone_id": "fb4_zone_gone"})");
    ASSERT_EQ(listener->calls, 1);

    listener.reset();
    roonCtrl()->processNewMessage(R"({"zone_id": "fb4_zone_gone"})");
}
