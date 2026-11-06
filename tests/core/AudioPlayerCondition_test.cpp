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

//A rule on an audio player matches the LAST event the player reported, for
//both condition types the installer can build.

#include "CalaosConfig.h"
#include "CalaosCoreFixture.h"
#include "AudioPlayer.h"
#include "ConditionOutput.h"
#include "JsonApi.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

class TestPlayer: public AudioPlayer
{
public:
    TestPlayer(Params &p):
        AudioPlayer(p)
    {
    }

    void emit(int status) { set_status(status); }
};

struct EventCase
{
    int status;
    const char *name;
};

const EventCase events[] = {
    { AudioPlay, "onplay" },
    { AudioPause, "onpause" },
    { AudioStop, "onstop" },
    { AudioSongChange, "onsongchange" },
    { AudioPlaylistChange, "onplaylistchange" },
    { AudioVolumeChange, "onvolumechange" },
    { AudioError, "onerror" },
};

} //namespace

class AudioPlayerConditionTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        reset();
    }

    void reset()
    {
        clearCoreState();
        loadConfig();
        io(ID_STRING)->set_value(std::string(""));
        Params p = { { "id", "player" }, { "name", "player" }, { "type", "Test" }, { "enabled", "true" } };
        player = new TestPlayer(p);
        firstRoom()->AddIO(player);
        ListeRoom::Instance().addIOHash(player);
    }

    static std::string rule(const std::string &kind, const std::string &oper, const std::string &val)
    {
        const std::string attrs = " id=\"player\" oper=\"" + oper + "\" val=\"" + val + "\" />";
        const std::string cond = (kind == "output" ? "<calaos:output" : "<calaos:input") + attrs;
        return "<calaos:rule name=\"r\" type=\"OR\">"
               "<calaos:condition type=\"" +
               kind + "\" trigger=\"true\">" + cond +
               "</calaos:condition>"
               "<calaos:action type=\"standard\">"
               "<calaos:output id=\"" +
               std::string(ID_STRING) + "\" val=\"fired\" />"
                                        "</calaos:action></calaos:rule>";
    }

    std::string load(const std::string &kind, const std::string &oper, const std::string &val)
    {
        testing::internal::CaptureStdout();
        EXPECT_NE(addRuleFromXml(rule(kind, oper, val)), nullptr);
        return testing::internal::GetCapturedStdout();
    }

    std::string fired()
    {
        return io(ID_STRING)->get_value_string();
    }

    TestPlayer *player = nullptr;
};

TEST_F(AudioPlayerConditionTest, GetValueStringIsTheLastEvent)
{
    for (const auto &e: events)
    {
        player->emit(e.status);
        EXPECT_EQ(player->get_value_string(), e.name);
    }
}

TEST_F(AudioPlayerConditionTest, EveryEventFiresItsRuleThroughBothConditionTypes)
{
    for (const char *kind: { "standard", "output" })
    {
        for (const auto &e: events)
        {
            reset();
            const std::string log = load(kind, "==", e.name);
            EXPECT_EQ(log.find("never be true"), std::string::npos)
                << kind << " " << e.name << "\n"
                << log;

            for (const auto &other: events)
            {
                if (other.status == e.status)
                    continue;
                player->emit(other.status);
                EXPECT_NE(fired(), "fired") << kind << " " << e.name << " fired on " << other.name;
            }
            player->emit(e.status);
            EXPECT_EQ(fired(), "fired") << kind << " " << e.name;
        }
    }
}

TEST_F(AudioPlayerConditionTest, NotEqualsHoldsOnAnyOtherEvent)
{
    for (const char *kind: { "standard", "output" })
    {
        reset();
        load(kind, "!=", "onplay");
        player->emit(AudioPlay);
        EXPECT_NE(fired(), "fired") << kind;
        player->emit(AudioPause);
        EXPECT_EQ(fired(), "fired") << kind;
    }
}

TEST_F(AudioPlayerConditionTest, ConditionMatchesTheLastEventOnly)
{
    for (const char *kind: { "standard", "output" })
    {
        reset();
        ASSERT_NE(addRuleFromXml(rule(kind, "==", "onplay")), nullptr);
        player->emit(AudioPlay);
        ASSERT_EQ(fired(), "fired") << kind;

        io(ID_STRING)->set_value(std::string("reset"));
        player->emit(AudioVolumeChange);
        EXPECT_NE(fired(), "fired") << kind << ": a later volume change replaces onplay";
    }
}

//Still-playing case: the player is only a guard, another IO triggers the rule.
TEST_F(AudioPlayerConditionTest, NonTriggerPlayerConditionIsEvaluatedWhenAnotherIoTriggers)
{
    for (const char *kind: { "standard", "output" })
    {
        reset();
        const std::string guard =
            std::string(kind) == "output" ? "<calaos:condition type=\"output\" trigger=\"false\">"
                                            "<calaos:output id=\"player\" oper=\"==\" val=\"onplay\" />"
                                          : "<calaos:condition type=\"standard\" trigger=\"false\">"
                                            "<calaos:input id=\"player\" oper=\"==\" val=\"onplay\" />";
        std::string xml = "<calaos:rule name=\"g\" type=\"AND\">" + guard;
        xml += "</calaos:condition>";
        xml += "<calaos:condition type=\"standard\" trigger=\"true\">";
        xml += "<calaos:input id=\"" + std::string(ID_BOOL_IN) + "\" oper=\"==\" val=\"true\" />";
        xml += "</calaos:condition>";
        xml += "<calaos:action type=\"standard\"><calaos:output id=\"" + std::string(ID_STRING);
        xml += "\" val=\"fired\" /></calaos:action></calaos:rule>";
        ASSERT_NE(addRuleFromXml(xml), nullptr) << kind;

        player->emit(AudioPlay);
        io(ID_BOOL_IN)->set_value(true);
        EXPECT_EQ(fired(), "fired") << kind << ": playing";

        io(ID_BOOL_IN)->set_value(false);
        io(ID_STRING)->set_value(std::string(""));
        player->emit(AudioVolumeChange);
        io(ID_BOOL_IN)->set_value(true);
        EXPECT_NE(fired(), "fired") << kind << ": last event is onvolumechange";
    }
}

namespace
{

const Json *findById(const Json &j, const std::string &id)
{
    if (j.is_object() && j.contains("id") && j["id"] == id)
        return &j;
    if (j.is_object() || j.is_array())
    {
        for (const auto &c: j)
        {
            if (const Json *r = findById(c, id))
                return r;
        }
    }
    return nullptr;
}

} //namespace

TEST_F(AudioPlayerConditionTest, GetIoAndGetHomeReportTheLastEventAsState)
{
    JsonApi api;
    EXPECT_EQ(api.buildJsonGetIO({ "player" })["player"]["state"], "");

    player->emit(AudioPlay);
    EXPECT_EQ(api.buildJsonGetIO({ "player" })["player"]["state"], "onplay");

    player->emit(AudioVolumeChange);
    EXPECT_EQ(api.buildJsonGetIO({ "player" })["player"]["state"], "onvolumechange");

    const Json home = api.buildJsonHome();
    const Json *item = findById(home, "player");
    ASSERT_NE(item, nullptr) << home.dump();
    EXPECT_EQ((*item)["state"], "onvolumechange");
}
