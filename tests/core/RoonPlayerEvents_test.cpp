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

//A Roon zone reports its seek position about once a second: that must not
//replay "onplay" to a rule while the play state is unchanged.

#include <chrono>

#include "libuvw.h"

#include "CalaosCoreFixture.h"
#include "RoonPlayer.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

std::string zoneState(const std::string &state, int seek, int length = 100,
                      const std::string &title = "A")
{
    return R"({"zone_id":"rz","state":")" + state + R"(","now_playing":{"seek_position":)" +
           std::to_string(seek) + R"(,"length":)" + std::to_string(length) +
           R"(,"one_line":{"line1":")" + title + R"("}}})";
}

//RoonPlayer arms its zone subscription 10 s after construction: block on the
//loop until that timer has fired.
void waitForSubscription()
{
    auto loop = uvw::Loop::getDefault();
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(10500);
    while (std::chrono::steady_clock::now() < end)
        loop->run<uvw::Loop::Mode::ONCE>();
}

} //namespace

class RoonPlayerEventsTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig();
        io(ID_STRING)->set_value(std::string(""));

        Params p = { { "id", "roon" }, { "name", "roon" }, { "type", "Roon" }, { "enabled", "true" },
                     { "zone_id", "rz" }, { "host", "127.0.0.1" } };
        player = new RoonPlayer(p);
        firstRoom()->AddIO(player);
        ListeRoom::Instance().addIOHash(player);

        const std::string rule =
            "<calaos:rule name=\"r\" type=\"OR\">"
            "<calaos:condition type=\"standard\" trigger=\"true\">"
            "<calaos:input id=\"roon\" oper=\"==\" val=\"onplay\" /></calaos:condition>"
            "<calaos:action type=\"standard\">"
            "<calaos:output id=\"" +
            std::string(ID_STRING) + "\" val=\"fired\" /></calaos:action></calaos:rule>";
        ASSERT_NE(addRuleFromXml(rule), nullptr);

    }

    static void zone(const std::string &json)
    {
        RoonCtrl::Instance("127.0.0.1", 9330)->processNewMessage(json);
    }

    std::string fired() { return io(ID_STRING)->get_value_string(); }
    void reset() { io(ID_STRING)->set_value(std::string("")); }

    RoonPlayer *player = nullptr;
};

TEST_F(RoonPlayerEventsTest, ASeekingZoneDoesNotReplayOnplayButAResumeDoes)
{
    waitForSubscription();

    zone(zoneState("playing", 0));
    EXPECT_EQ(player->get_value_string(), "onsongchange");
    zone(zoneState("paused", 5));
    EXPECT_EQ(player->get_value_string(), "onpause");
    zone(zoneState("playing", 5));
    EXPECT_EQ(player->get_value_string(), "onplay");
    EXPECT_EQ(fired(), "fired");

    reset();
    for (int seek = 6; seek < 10; seek++)
    {
        zone(zoneState("playing", seek));
        EXPECT_EQ(fired(), "") << "seek " << seek;
    }

    zone(zoneState("paused", 6));
    reset();
    zone(zoneState("playing", 6));
    EXPECT_EQ(fired(), "fired");
}
