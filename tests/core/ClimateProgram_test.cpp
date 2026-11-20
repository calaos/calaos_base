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

//The schedule program of a climate (docs/17_device_templates.md, "Programme
//horaire des climates"), on InternalThermostat: owned levels, priority,
//application per mode, triggers, persistence and the levels params.

#include "CalaosCoreFixture.h"
#include "EventManager.h"
#include "IO/InPlageHoraire.h"
#include "IOChangeHub.h"
#include "InternalThermostat.h"
#include "ListeRule.h"
#include "libuvw.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

using Clock = std::chrono::steady_clock;

bool pumpUntil(const std::function<bool()> &done, int ms = 5000)
{
    auto loop = uvw::Loop::getDefault();
    const auto t0 = Clock::now();
    while (!done())
    {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() > ms)
            return false;
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

void pumpFor(int ms)
{
    pumpUntil([]()
              {
        return false;
    },
              ms);
}

std::string linkXml(const std::string &name, const std::string &io)
{
    return "      <calaos:link name=\"" + name + "\" io=\"" + io + "\" />\n";
}

std::string paramXml(const std::string &name, const std::string &value)
{
    return "      <calaos:param name=\"" + name + "\" value=\"" + value + "\" />\n";
}

std::string ownedXml(const std::string &type, const std::string &id,
                     const std::string &extra = std::string())
{
    return "      <calaos:internal type=\"" + type + "\" id=\"" + id + "\" name=\"" + id +
           "\" visible=\"false\"" + extra + " />\n";
}

std::string roomIo(const std::string &type, const std::string &id)
{
    return "    <calaos:internal type=\"" + type + "\" id=\"" + id + "\" name=\"" + id + "\" />\n";
}

const char *const SAVED = R"( save="true")";

//Setpoints, action and outputs owned; `cool` adds the range and output_cool.
std::string thermostat(const std::string &id, bool cool, const std::string &params)
{
    std::string inner = ownedXml("InternalInt", id + "_target", SAVED) +
                        ownedXml("InternalString", id + "_action") +
                        ownedXml("InternalBool", id + "_heat") +
                        linkXml("current_temp", id + "_temp") +
                        linkXml("target_temp", id + "_target") + linkXml("action", id + "_action") +
                        linkXml("output_heat", id + "_heat");
    if (cool)
    {
        inner += ownedXml("InternalInt", id + "_low", SAVED) +
                 ownedXml("InternalInt", id + "_high", SAVED) +
                 ownedXml("InternalBool", id + "_cool") + linkXml("target_temp_low", id + "_low") +
                 linkXml("target_temp_high", id + "_high") + linkXml("output_cool", id + "_cool");
    }
    return "    <calaos:device id=\"" + id + "\" type=\"climate\" impl=\"thermostat\" name=\"" + id +
           "\">\n" + inner + params + "    </calaos:device>\n" + roomIo("InternalInt", id + "_temp");
}

std::string levels(int heat, int cool)
{
    return paramXml("heat_levels", std::to_string(heat)) +
           paramXml("cool_levels", std::to_string(cool));
}

const std::vector<std::string> DEVICES = { "prog", "gap", "coolset", "wide", "heatonly",
                                           "big", "neg", "frac", "uniq", "off", "boot",
                                           "zh", "zc", "zr", "zn", "cl", "tg", "tm", "au",
                                           "wi", "rs", "gr", "rd", "ml", "nm", "ow", "vb", "nr", "res" };

std::string house()
{
    std::string ios;
    ios += thermostat("prog", true, levels(2, 2));
    ios += thermostat("gap", true, levels(1, 1) + paramXml("program_min_gap", "2.3"));
    ios += thermostat("coolset", true, levels(0, 1) + paramXml("cool_default_setpoint", "22"));
    //A gap wider than every pair the bounds allow.
    ios += thermostat("wide", true, levels(1, 1) + paramXml("program_min_gap", "40"));
    //No output_cool: cool is not offered, its levels are ignored.
    ios += thermostat("heatonly", false, levels(1, 1));
    ios += thermostat("big", false, levels(9, 0));
    ios += thermostat("neg", false, levels(-1, 0));
    ios += thermostat("frac", false, paramXml("heat_levels", "1.5"));
    //The conventional ids are taken: the program IOs get the smallest free N.
    ios += thermostat("uniq", false,
                      levels(1, 0) + paramXml("heat_default_setpoint", "19"));
    ios += roomIo("InternalBool", "uniq_heat_level1") + roomIo("InternalInt", "uniq_heat_default") +
           roomIo("InternalInt", "uniq_heat_default_0");
    ios += thermostat("off", true, levels(0, 1) + paramXml("cool_default_off", "true"));
    ios += thermostat("boot", false, levels(1, 0) + paramXml("heat_default_setpoint", "18"));
    return ioXmlDocument(roomXml("Salon", "salon", ios));
}

//One device alone in its room.
std::string solo(const std::string &id, bool cool, const std::string &params,
                 const std::string &extraIos = std::string())
{
    return ioXmlDocument(roomXml("Salon", "salon", thermostat(id, cool, params) + extraIos));
}

std::string replaced(std::string text, const std::string &from, const std::string &to)
{
    const size_t at = text.find(from);
    EXPECT_NE(at, std::string::npos) << from;
    if (at != std::string::npos)
        text.replace(at, from.size(), to);
    return text;
}

} //namespace

class ClimateProgramTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        //The state cache outlives a case: nothing may leak from the previous one.
        for (const std::string &id: DEVICES)
        {
            forgetIOState(id);
            for (const char *suffix: { "_target", "_low", "_high" })
                forgetIOState(id + suffix);
        }
        Config::Instance().SaveValueIO("boot", "heat", false);
    }

    void TearDown() override
    {
        CoreFixture::TearDown();
        pumpFor(5);
    }

    //Loads and waits for the start of every thermostat.
    void load(const std::string &xml = house(), const std::string &id = "prog")
    {
        loadConfig(xml, rulesXmlDocument(""));
        waitStarted(id);
    }

    void reload(const std::string &id = "prog")
    {
        Config::Instance().loadStateCache();
        reloadFromDisk();
        waitStarted(id);
    }

    //What the installer does: edit a param of the device in io.xml, then the
    //server loads the file again.
    void changeParam(const std::string &id, const std::string &name, const std::string &from,
                     const std::string &to)
    {
        saveConfig();
        writeConfig(replaced(ioXmlOnDisk(), "name=\"" + name + "\" value=\"" + from + "\"",
                             "name=\"" + name + "\" value=\"" + to + "\""),
                    rulesXmlOnDisk());
        reload(id);
    }

    static void waitStarted(const std::string &id = "prog")
    {
        EXPECT_TRUE(pumpUntil([id]()
                              {
            return io(id + "_action")->hasValue();
        }));
        pumpFor(5);
    }

    static int ioCount() { return ListeRoom::Instance().get_io_count(); }

    //Every id found in the rooms, duplicates included.
    static std::vector<std::string> allIds()
    {
        std::vector<std::string> ids;
        for (int i = 0; i < ListeRoom::Instance().get_io_count(); i++)
            ids.push_back(ListeRoom::Instance().get_io(i)->get_param("id"));
        return ids;
    }

    static void expectUniqueIds()
    {
        std::vector<std::string> ids = allIds();
        std::sort(ids.begin(), ids.end());
        for (size_t i = 1; i < ids.size(); i++)
            EXPECT_NE(ids[i - 1], ids[i]) << "id used by two IOs";
    }

    static InternalThermostat *dev(const std::string &id)
    {
        return ListeRoom::Instance().findIOAs<InternalThermostat>(id);
    }

    static IOBase *must(const std::string &id)
    {
        IOBase *i = io(id);
        EXPECT_NE(i, nullptr) << "no IO " << id;
        return i;
    }

    static double num(const std::string &id) { return must(id) ? io(id)->get_value_double() : -1; }
    static bool on(const std::string &id) { return must(id) && io(id)->get_value_bool(); }
    static void set(const std::string &id, double v)
    {
        if (must(id))
            io(id)->set_value(v);
    }
    static void setBool(const std::string &id, bool v)
    {
        if (must(id))
            io(id)->set_value(v);
    }
    static bool mode(const std::string &id, const std::string &m) { return dev(id)->set_value(m); }

    //The IO the device links under `role`, empty without link.
    static std::string linked(const std::string &id, const std::string &role)
    {
        for (const Device::Link &l: dev(id)->getLinks())
        {
            if (l.name == role)
                return l.io;
        }
        return std::string();
    }

    static InPlageHoraire *range(const std::string &id)
    {
        auto *r = dynamic_cast<InPlageHoraire *>(io(id));
        EXPECT_NE(r, nullptr) << "no range " << id;
        return r;
    }

    static void addOn(InPlageHoraire *r, int wday, TimeRange t)
    {
        switch (wday)
        {
        case TimeRange::MONDAY: r->AddMonday(t); break;
        case TimeRange::TUESDAY: r->AddTuesday(t); break;
        case TimeRange::WEDNESDAY: r->AddWednesday(t); break;
        case TimeRange::THURSDAY: r->AddThursday(t); break;
        case TimeRange::FRIDAY: r->AddFriday(t); break;
        case TimeRange::SATURDAY: r->AddSaturday(t); break;
        default: r->AddSunday(t); break;
        }
    }

    static TimeRange span(const char *sh, const char *sm, const char *ss, const char *eh,
                          const char *em, const char *es)
    {
        TimeRange t;
        t.shour = sh;
        t.smin = sm;
        t.ssec = ss;
        t.ehour = eh;
        t.emin = em;
        t.esec = es;
        return t;
    }

    //Active all day, every day; evaluated now, as the rules loop would.
    static void activate(const std::string &id)
    {
        InPlageHoraire *r = range(id);
        if (!r)
            return;
        r->clear();
        for (int d = TimeRange::SUNDAY; d <= TimeRange::SATURDAY; d++)
            addOn(r, d, span("0", "0", "0", "23", "59", "59"));
        r->hasChanged();
    }

    static void deactivate(const std::string &id)
    {
        InPlageHoraire *r = range(id);
        if (!r)
            return;
        r->clear();
        r->hasChanged();
    }
};

TEST_F(ClimateProgramTest, LevelsCreateTheirOwnedIos)
{
    load();
    const std::vector<std::string> roles = {
        "heat_level1", "heat_level1_setpoint", "heat_level2", "heat_level2_setpoint",
        "cool_level1", "cool_level1_setpoint", "cool_level2", "cool_level2_setpoint",
        "heat_default", "cool_default", "cool_default_off", "program_enabled"
    };
    for (const std::string &role: roles)
    {
        EXPECT_EQ(linked("prog", role), "prog_" + role) << role;
        IOBase *owned = io("prog_" + role);
        ASSERT_NE(owned, nullptr) << role;
        EXPECT_EQ(owned->get_param("visible"), "false") << role;
        EXPECT_NE(owned->get_param("log_history"), "true") << role;
    }
    //Any program gets the maximum of levels, whatever its param says.
    EXPECT_EQ(linked("prog", "heat_level10_setpoint"), "prog_heat_level10_setpoint");
    EXPECT_EQ(linked("prog", "cool_level10"), "prog_cool_level10");
    EXPECT_EQ(linked("prog", "heat_level11"), "");
    EXPECT_EQ(io("prog")->get_param("id"), "prog");

    EXPECT_EQ(num("prog_heat_default"), 17);
    EXPECT_EQ(num("prog_cool_default"), 26);
    EXPECT_FALSE(on("prog_cool_default_off"));
    EXPECT_TRUE(on("prog_program_enabled"));
    //A level starts at the default of its set.
    EXPECT_EQ(num("prog_heat_level2_setpoint"), 17);
    EXPECT_EQ(num("prog_cool_level1_setpoint"), 26);
    //Bounded as the setpoint the program writes.
    EXPECT_EQ(io("prog_heat_level1_setpoint")->get_param("value_max"), "35");
    EXPECT_EQ(io("prog_heat_level1_setpoint")->get_param("save"), "true");
    EXPECT_EQ(io("prog_heat_level1")->get_param("type"), "InPlageHoraire");
    EXPECT_TRUE(on("off_cool_default_off"));

    saveConfig();
    const std::string xml = ioXmlOnDisk();
    for (const std::string &role: roles)
        EXPECT_NE(xml.find("id=\"prog_" + role + "\""), std::string::npos) << role;
}

TEST_F(ClimateProgramTest, NoRuleIsGenerated)
{
    load();
    EXPECT_EQ(ListeRule::Instance().size(), 0);
    saveConfig();
    EXPECT_EQ(rulesXmlOnDisk().find("calaos:rule "), std::string::npos) << rulesXmlOnDisk();
}

TEST_F(ClimateProgramTest, TheFirstActiveLevelWins)
{
    load();
    ASSERT_TRUE(mode("prog", "heat"));
    EXPECT_EQ(num("prog_target"), 17);

    set("prog_heat_level1_setpoint", 21);
    set("prog_heat_level2_setpoint", 19);
    EXPECT_EQ(num("prog_target"), 17) << "an inactive level writes nothing";

    activate("prog_heat_level2");
    EXPECT_EQ(num("prog_target"), 19);
    activate("prog_heat_level1");
    EXPECT_EQ(num("prog_target"), 21);
    deactivate("prog_heat_level1");
    EXPECT_EQ(num("prog_target"), 19);
    deactivate("prog_heat_level2");
    EXPECT_EQ(num("prog_target"), 17);
}

TEST_F(ClimateProgramTest, ARangeOverMidnightKeepsItsLevelAfterMidnight)
{
    load();
    ASSERT_TRUE(mode("prog", "heat"));
    set("prog_heat_level1_setpoint", 22);

    //Only on yesterday, ending one second before its own start: today it is
    //active through its part after midnight.
    time_t t = time(nullptr);
    struct tm now = *localtime(&t);
    InPlageHoraire *r = range("prog_heat_level1");
    ASSERT_NE(r, nullptr);
    addOn(r, (now.tm_wday + 6) % 7, span("23", "59", "59", "23", "59", "58"));
    r->hasChanged();
    if (now.tm_hour == 23 && now.tm_min == 59 && now.tm_sec >= 58)
        GTEST_SKIP() << "the range wraps on its last two seconds";
    EXPECT_TRUE(on("prog_heat_level1"));
    EXPECT_EQ(num("prog_target"), 22);
}

TEST_F(ClimateProgramTest, TheModeSelectsTheSet)
{
    load();
    set("prog_heat_default", 18);
    set("prog_cool_default", 25);
    ASSERT_TRUE(mode("prog", "cool"));
    EXPECT_EQ(num("prog_target"), 25);
    ASSERT_TRUE(mode("prog", "heat"));
    EXPECT_EQ(num("prog_target"), 18);

    //A change of the other set writes nothing.
    set("prog_target", 20);
    set("prog_cool_default", 24);
    activate("prog_cool_level1");
    EXPECT_EQ(num("prog_target"), 20);

    ASSERT_TRUE(mode("prog", "off"));
    set("prog_target", 20);
    set("prog_heat_default", 16);
    EXPECT_EQ(num("prog_target"), 20) << "off writes nothing";
}

TEST_F(ClimateProgramTest, ACommandTargetTempWinsOverTheModeChange)
{
    load();
    ASSERT_TRUE(mode("prog", R"({"mode":"heat","target_temp":23})"));
    EXPECT_EQ(num("prog_target"), 23);
}

TEST_F(ClimateProgramTest, CoolDefaultOffWritesTheHighestSetpoint)
{
    load();
    ASSERT_TRUE(mode("prog", "cool"));
    EXPECT_EQ(num("prog_target"), 26);
    setBool("prog_cool_default_off", true);
    EXPECT_EQ(num("prog_target"), 35);
    EXPECT_EQ(dev("prog")->get_value_string(), "cool");

    set("prog_cool_level1_setpoint", 24);
    activate("prog_cool_level1");
    EXPECT_EQ(num("prog_target"), 24) << "an active level wins over the off default";
}

TEST_F(ClimateProgramTest, AChangeOnlyAppliesWhenItChangesTheSetpoint)
{
    load();
    ASSERT_TRUE(mode("prog", "heat"));
    activate("prog_heat_level2");

    //A manual setpoint stays until the program has something new.
    set("prog_target", 20);
    set("prog_heat_default", 19);
    set("prog_heat_level1_setpoint", 22);
    EXPECT_EQ(num("prog_target"), 20);

    set("prog_heat_level2_setpoint", 21);
    EXPECT_EQ(num("prog_target"), 21);
}

TEST_F(ClimateProgramTest, AutoWritesThePairWithTheGap)
{
    load();
    set("prog_heat_default", 20);
    set("prog_cool_default", 21);
    ASSERT_TRUE(mode("prog", "auto"));
    EXPECT_EQ(num("prog_low"), 20);
    EXPECT_EQ(num("prog_high"), 22);

    //Already apart: written as asked.
    set("prog_cool_default", 25);
    EXPECT_EQ(num("prog_low"), 20);
    EXPECT_EQ(num("prog_high"), 25);

    //Off: the highest high.
    setBool("prog_cool_default_off", true);
    EXPECT_EQ(num("prog_high"), 35);

    //The high bound cannot rise: the low one goes down.
    setBool("prog_cool_default_off", false);
    set("prog_heat_default", 35);
    EXPECT_EQ(num("prog_low"), 33);
    EXPECT_EQ(num("prog_high"), 35);
}

TEST_F(ClimateProgramTest, TheGapIsRoundedUpToTheStep)
{
    load();
    set("gap_heat_default", 20);
    set("gap_cool_default", 21);
    ASSERT_TRUE(mode("gap", "auto"));
    EXPECT_EQ(num("gap_low"), 20);
    EXPECT_EQ(num("gap_high"), 22.5);
}

TEST_F(ClimateProgramTest, APairThatCannotKeepTheGapIsNotWritten)
{
    load();
    set("wide_low", 18);
    set("wide_high", 24);
    ASSERT_TRUE(mode("wide", "auto"));
    EXPECT_EQ(num("wide_low"), 18);
    EXPECT_EQ(num("wide_high"), 24);
}

TEST_F(ClimateProgramTest, AloneTheCoolSetMovesTheLowBound)
{
    load();
    set("coolset_low", 21);
    set("coolset_high", 30);
    ASSERT_TRUE(mode("coolset", "auto"));
    EXPECT_EQ(num("coolset_high"), 22);
    EXPECT_EQ(num("coolset_low"), 20);
}

TEST_F(ClimateProgramTest, ADisabledProgramWritesNothing)
{
    load();
    ASSERT_TRUE(mode("prog", "heat"));
    setBool("prog_program_enabled", false);
    set("prog_target", 20);

    set("prog_heat_default", 18);
    activate("prog_heat_level1");
    ASSERT_TRUE(mode("prog", "cool"));
    ASSERT_TRUE(mode("prog", "heat"));
    EXPECT_EQ(num("prog_target"), 20);

    set("prog_heat_level1_setpoint", 23);
    setBool("prog_program_enabled", true);
    EXPECT_EQ(num("prog_target"), 23);
}

TEST_F(ClimateProgramTest, TheProgramAppliesAtStart)
{
    load();
    EXPECT_EQ(dev("boot")->get_value_string(), "heat");
    EXPECT_EQ(num("boot_target"), 18);
}

TEST_F(ClimateProgramTest, ValuesSurviveARestart)
{
    load();
    set("prog_heat_default", 19);
    set("prog_heat_level2_setpoint", 23);
    setBool("prog_cool_default_off", true);
    setBool("prog_program_enabled", false);
    saveConfig();

    reload();
    EXPECT_EQ(num("prog_heat_default"), 19);
    EXPECT_EQ(num("prog_heat_level2_setpoint"), 23);
    EXPECT_EQ(num("prog_heat_level1_setpoint"), 17);
    EXPECT_TRUE(on("prog_cool_default_off"));
    EXPECT_FALSE(on("prog_program_enabled"));
    EXPECT_EQ(linked("prog", "heat_level1"), "prog_heat_level1");
}

TEST_F(ClimateProgramTest, ASetAtZeroRemovesItsIos)
{
    load();
    saveConfig();
    std::string xml = replaced(ioXmlOnDisk(), "name=\"heat_levels\" value=\"2\"",
                               "name=\"heat_levels\" value=\"1\"");
    xml = replaced(xml, "name=\"cool_levels\" value=\"2\"", "name=\"cool_levels\" value=\"0\"");
    writeConfig(xml, rulesXmlOnDisk());
    reload();

    EXPECT_NE(io("prog_heat_level1"), nullptr);
    EXPECT_NE(io("prog_heat_default"), nullptr);
    EXPECT_NE(io("prog_program_enabled"), nullptr);
    //A lower count is still "a program": the heat set keeps all its levels.
    EXPECT_NE(io("prog_heat_level2"), nullptr);
    EXPECT_NE(io("prog_heat_level10_setpoint"), nullptr);
    for (const char *gone: { "prog_cool_level1", "prog_cool_level1_setpoint", "prog_cool_level2",
                             "prog_cool_level10", "prog_cool_default", "prog_cool_default_off" })
        EXPECT_EQ(io(gone), nullptr) << gone;
    EXPECT_EQ(linked("prog", "cool_default"), "");

    saveConfig();
    xml = replaced(ioXmlOnDisk(), "name=\"heat_levels\" value=\"1\"",
                   "name=\"heat_levels\" value=\"0\"");
    writeConfig(xml, rulesXmlOnDisk());
    reload();
    for (const char *gone: { "prog_heat_level1", "prog_heat_default", "prog_program_enabled" })
        EXPECT_EQ(io(gone), nullptr) << gone;
    EXPECT_EQ(linked("prog", "program_enabled"), "");
    //The devices links stay: target_temp is not a program IO.
    EXPECT_NE(io("prog_target"), nullptr);

    saveConfig();
    EXPECT_EQ(ioXmlOnDisk().find("prog_heat_level"), std::string::npos);
    EXPECT_EQ(ioXmlOnDisk().find("prog_program_enabled"), std::string::npos);
}

TEST_F(ClimateProgramTest, RecreatedIosStartFromTheirInitialValue)
{
    //A value cached by a removed IO of the same id does not come back.
    Config::Instance().SaveValueIO("boot_heat_default", "30", false);
    Config::Instance().SaveValueIO("boot_program_enabled", "false", false);
    load();
    EXPECT_EQ(num("boot_heat_default"), 18);
    EXPECT_TRUE(on("boot_program_enabled"));
}

TEST_F(ClimateProgramTest, TakenIdsGetTheSmallestFreeNumber)
{
    load();
    EXPECT_EQ(linked("uniq", "heat_level1"), "uniq_heat_level1_0");
    EXPECT_EQ(linked("uniq", "heat_default"), "uniq_heat_default_1");
    EXPECT_EQ(linked("uniq", "heat_level1_setpoint"), "uniq_heat_level1_setpoint");
    EXPECT_EQ(num("uniq_heat_default_1"), 19);
    EXPECT_EQ(num("uniq_heat_level1_setpoint"), 19);

    saveConfig();
    reload();
    EXPECT_EQ(linked("uniq", "heat_level1"), "uniq_heat_level1_0");
    EXPECT_EQ(linked("uniq", "heat_default"), "uniq_heat_default_1");
    EXPECT_EQ(io("uniq_heat_level1_1"), nullptr) << "nothing created again";
}

TEST_F(ClimateProgramTest, LevelParamsAreChecked)
{
    load();
    EXPECT_EQ(linked("big", "heat_level10"), "big_heat_level10");
    EXPECT_EQ(linked("big", "heat_level11"), "");

    for (const std::string id: { "neg", "frac" })
    {
        EXPECT_EQ(linked(id, "heat_level1"), "") << id;
        EXPECT_EQ(linked(id, "heat_default"), "") << id;
        EXPECT_EQ(linked(id, "program_enabled"), "") << id;
    }

    //Cool is not offered without output_cool.
    EXPECT_EQ(linked("heatonly", "heat_level1"), "heatonly_heat_level1");
    EXPECT_EQ(linked("heatonly", "cool_level1"), "");
    EXPECT_EQ(linked("heatonly", "cool_default"), "");
}

TEST_F(ClimateProgramTest, ZeroHeatLevelsRemoveTheHeatDefaultAndKeepTheCoolSet)
{
    load(solo("zh", true, levels(2, 2)), "zh");
    ASSERT_NE(io("zh_heat_default"), nullptr);
    changeParam("zh", "heat_levels", "2", "0");

    for (const char *gone: { "zh_heat_level1", "zh_heat_level1_setpoint", "zh_heat_level10",
                             "zh_heat_level10_setpoint", "zh_heat_default" })
        EXPECT_EQ(io(gone), nullptr) << gone;
    for (const char *kept: { "zh_cool_level1", "zh_cool_level2_setpoint", "zh_cool_default",
                             "zh_cool_default_off", "zh_program_enabled" })
        EXPECT_NE(io(kept), nullptr) << kept;
    EXPECT_EQ(linked("zh", "heat_default"), "");
    EXPECT_EQ(linked("zh", "cool_default"), "zh_cool_default");

    //Heat has no program left: the cool set does not write in heat mode.
    ASSERT_TRUE(mode("zh", "heat"));
    set("zh_target", 20);
    set("zh_cool_default", 24);
    EXPECT_EQ(num("zh_target"), 20);

    saveConfig();
    EXPECT_EQ(ioXmlOnDisk().find("zh_heat_"), std::string::npos);
    EXPECT_EQ(ListeRule::Instance().size(), 0);
}

TEST_F(ClimateProgramTest, ZeroCoolLevelsRemoveCoolDefaultOffAndItsState)
{
    load(solo("zc", true, levels(1, 2) + paramXml("cool_default_off", "false")), "zc");
    setBool("zc_cool_default_off", true);
    changeParam("zc", "cool_levels", "2", "0");

    EXPECT_EQ(io("zc_cool_default_off"), nullptr);
    EXPECT_EQ(io("zc_cool_default"), nullptr);
    EXPECT_NE(io("zc_heat_default"), nullptr);
    EXPECT_NE(io("zc_program_enabled"), nullptr);

    changeParam("zc", "cool_levels", "0", "1");
    ASSERT_NE(io("zc_cool_default_off"), nullptr);
    EXPECT_FALSE(on("zc_cool_default_off")) << "the flag starts from the param";
    EXPECT_EQ(linked("zc", "cool_default_off"), "zc_cool_default_off");
}

TEST_F(ClimateProgramTest, ZeroLevelsInTotalRemoveEveryProgramIoAndBringThemBackNew)
{
    load(solo("zr", true, levels(2, 1) + paramXml("heat_default_setpoint", "19")), "zr");
    set("zr_heat_level2_setpoint", 23);
    set("zr_heat_default", 21);
    setBool("zr_program_enabled", false);

    changeParam("zr", "heat_levels", "2", "0");
    EXPECT_NE(io("zr_program_enabled"), nullptr) << "cool levels remain";
    EXPECT_FALSE(on("zr_program_enabled")) << "the flag is kept while a set remains";
    changeParam("zr", "cool_levels", "1", "0");
    for (const char *gone: { "zr_heat_level1", "zr_heat_level2", "zr_heat_level2_setpoint",
                             "zr_heat_default", "zr_cool_level1", "zr_cool_level1_setpoint",
                             "zr_cool_default", "zr_cool_default_off", "zr_program_enabled" })
        EXPECT_EQ(io(gone), nullptr) << gone;
    for (const char *role: { "program_enabled", "heat_default", "cool_default", "heat_level1",
                             "cool_level1_setpoint", "cool_default_off" })
        EXPECT_EQ(linked("zr", role), "") << role;
    EXPECT_NE(io("zr_target"), nullptr);
    EXPECT_NE(io("zr_low"), nullptr);

    ASSERT_TRUE(mode("zr", "heat"));
    set("zr_target", 22);
    EXPECT_EQ(num("zr_target"), 22) << "no program: the user's setpoint stays";

    changeParam("zr", "heat_levels", "0", "2");
    EXPECT_EQ(linked("zr", "heat_level2"), "zr_heat_level2") << "the freed id is reused";
    EXPECT_EQ(num("zr_heat_default"), 19) << "from the param, not from the removed IO";
    EXPECT_EQ(num("zr_heat_level2_setpoint"), 19);
    EXPECT_TRUE(on("zr_program_enabled")) << "created enabled";
    EXPECT_FALSE(on("zr_heat_level1"));
    EXPECT_EQ(io("zr_cool_level1"), nullptr);
    expectUniqueIds();
    EXPECT_EQ(ListeRule::Instance().size(), 0);
}

TEST_F(ClimateProgramTest, ARemovedSetNoLongerDrivesTheSetpoint)
{
    load(solo("zn", false, levels(2, 0)), "zn");
    ASSERT_TRUE(mode("zn", "heat"));
    set("zn_heat_level2_setpoint", 23);
    activate("zn_heat_level2");
    EXPECT_EQ(num("zn_target"), 23);

    //Heat off: the program of that set, and with it every level, is gone.
    changeParam("zn", "heat_levels", "2", "0");
    EXPECT_EQ(io("zn_heat_level2"), nullptr);
    EXPECT_EQ(num("zn_target"), 23) << "no program left, the last setpoint stays";
}

TEST_F(ClimateProgramTest, AThreeLevelHouseGainsEmptyLevelsAndKeepsItsSetpoints)
{
    std::string legacy = paramXml("heat_levels", "3") + paramXml("heat_default_setpoint", "19") +
                         ownedXml("InternalInt", "lg_heat_default", SAVED) +
                         ownedXml("InternalBool", "lg_program_enabled", SAVED) +
                         linkXml("heat_default", "lg_heat_default") +
                         linkXml("program_enabled", "lg_program_enabled");
    //Level n: day, start and end of its slots.
    struct Slot
    {
        int wday;
        const char *day;
        const char *sh, *sm, *eh, *em;
    };
    const std::vector<std::vector<Slot>> slots = {
        { { TimeRange::MONDAY, "lundi", "8", "0", "12", "0" },
          { TimeRange::MONDAY, "lundi", "14", "0", "18", "30" } },
        { { TimeRange::SATURDAY, "samedi", "22", "0", "23", "30" } },
        { { TimeRange::SUNDAY, "dimanche", "6", "15", "7", "45" } },
    };
    auto rangeXml = [&](const std::string &id, const std::vector<Slot> &sl)
    {
        std::string x = "      <calaos:internal type=\"InPlageHoraire\" id=\"" + id + "\" name=\"" + id +
                        "\" visible=\"false\">\n";
        for (const Slot &s: sl)
            x += std::string("        <calaos:") + s.day + "><calaos:plage start_type=\"0\" start_hour=\"" +
                 s.sh + "\" start_min=\"" + s.sm + "\" start_sec=\"0\" end_type=\"0\" end_hour=\"" + s.eh +
                 "\" end_min=\"" + s.em + "\" end_sec=\"0\" /></calaos:" + s.day + ">\n";
        return x + "      </calaos:internal>\n";
    };
    for (int n = 1; n <= 3; n++)
    {
        const std::string r = "lg_heat_level" + std::to_string(n);
        legacy += rangeXml(r, slots[n - 1]) + ownedXml("InternalInt", r + "_setpoint", SAVED) +
                  linkXml("heat_level" + std::to_string(n), r) +
                  linkXml("heat_level" + std::to_string(n) + "_setpoint", r + "_setpoint");
    }
    Config::Instance().SaveValueIO("lg_heat_default", "19", false);
    Config::Instance().SaveValueIO("lg_heat_level1_setpoint", "20", false);
    Config::Instance().SaveValueIO("lg_heat_level2_setpoint", "22", false);
    Config::Instance().SaveValueIO("lg_heat_level3_setpoint", "18", false);
    load(solo("lg", false, legacy), "lg");

    auto expectKept = [&]()
    {
        for (int n = 1; n <= 3; n++)
        {
            const std::string r = "lg_heat_level" + std::to_string(n);
            EXPECT_EQ(linked("lg", "heat_level" + std::to_string(n)), r) << n;
            InPlageHoraire *p = range(r);
            if (!p)
                continue;
            for (int d = TimeRange::SUNDAY; d <= TimeRange::SATURDAY; d++)
            {
                std::vector<TimeRange> want;
                for (const Slot &s: slots[n - 1])
                    if (s.wday == d)
                        want.push_back(span(s.sh, s.sm, "0", s.eh, s.em, "0"));
                std::vector<TimeRange> &got = d == TimeRange::SUNDAY      ? p->getSunday()
                                              : d == TimeRange::MONDAY    ? p->getMonday()
                                              : d == TimeRange::TUESDAY   ? p->getTuesday()
                                              : d == TimeRange::WEDNESDAY ? p->getWednesday()
                                              : d == TimeRange::THURSDAY  ? p->getThursday()
                                              : d == TimeRange::FRIDAY    ? p->getFriday()
                                                                          : p->getSaturday();
                EXPECT_EQ(got, want) << r << " day " << d;
            }
        }
        EXPECT_EQ(num("lg_heat_level1_setpoint"), 20);
        EXPECT_EQ(num("lg_heat_level2_setpoint"), 22);
        EXPECT_EQ(num("lg_heat_level3_setpoint"), 18);
        for (int n = 4; n <= 10; n++)
        {
            const std::string r = "lg_heat_level" + std::to_string(n);
            EXPECT_NE(io(r), nullptr) << r;
            EXPECT_EQ(num(r + "_setpoint"), 19) << "a new level starts at the default";
        }
        EXPECT_EQ(linked("lg", "heat_level11"), "");
        expectUniqueIds();
    };
    expectKept();

    //The new levels are written to io.xml, and the second load reads them
    //back next to the old ones without duplicating any id.
    saveConfig();
    reload("lg");
    expectKept();

    const Json caps = dev("lg")->deviceObject().at("capabilities");
    EXPECT_EQ(caps.at("program").at("heat_levels"), 10);
    EXPECT_EQ(caps.at("program").at("max_levels"), 10);
}

TEST_F(ClimateProgramTest, MoreLevelsStartFromTheCurrentDefaultAndKeepTheExisting)
{
    load(solo("gr", false, levels(1, 0)), "gr");
    set("gr_heat_level1_setpoint", 22);
    set("gr_heat_default", 19);
    activate("gr_heat_level1");

    changeParam("gr", "heat_levels", "1", "3");
    EXPECT_EQ(num("gr_heat_level1_setpoint"), 22);
    EXPECT_EQ(num("gr_heat_default"), 19);
    for (const char *added: { "gr_heat_level2_setpoint", "gr_heat_level3_setpoint" })
        EXPECT_EQ(linked("gr", std::string(added).substr(3)), added);
    expectUniqueIds();
}

TEST_F(ClimateProgramTest, ResolvingTheLinksAgainChangesNothing)
{
    load(solo("rs", true, levels(2, 1)), "rs");
    const size_t onRange = IOChangeHub::Instance().subscriberCount("rs_heat_level1");
    const size_t onDefault = IOChangeHub::Instance().subscriberCount("rs_heat_default");
    const std::vector<std::string> before = allIds();
    ASSERT_TRUE(mode("rs", "heat"));
    set("rs_heat_default", 18);
    ASSERT_EQ(num("rs_target"), 18);

    ListeRoom::Instance().resolveDeviceLinks();
    pumpFor(5);
    EXPECT_EQ(allIds(), before);
    EXPECT_EQ(IOChangeHub::Instance().subscriberCount("rs_heat_level1"), onRange);
    EXPECT_EQ(IOChangeHub::Instance().subscriberCount("rs_heat_default"), onDefault);
    set("rs_heat_default", 20);
    EXPECT_EQ(num("rs_target"), 20);
}

TEST_F(ClimateProgramTest, OwnedIdsAreUniqueAndTakeTheSmallestFreeNumber)
{
    //"_0" and "_2" are taken: the free hole is "_1".
    load(solo("rd", true, levels(1, 1),
              roomIo("InternalBool", "rd_heat_level1") +
                  roomIo("InternalBool", "rd_heat_level1_0") +
                  roomIo("InternalBool", "rd_heat_level1_2") +
                  roomIo("InternalBool", "rd_program_enabled") +
                  roomIo("InternalInt", "rd_cool_default")),
         "rd");
    EXPECT_EQ(linked("rd", "heat_level1"), "rd_heat_level1_1");
    EXPECT_EQ(linked("rd", "program_enabled"), "rd_program_enabled_0");
    EXPECT_EQ(linked("rd", "cool_default"), "rd_cool_default_0");
    EXPECT_EQ(linked("rd", "heat_default"), "rd_heat_default");
    expectUniqueIds();

    EXPECT_EQ(dynamic_cast<InPlageHoraire *>(io("rd_heat_level1")), nullptr);
    EXPECT_NE(dynamic_cast<InPlageHoraire *>(io("rd_heat_level1_1")), nullptr);

    const int count = ioCount();
    saveConfig();
    reload("rd");
    EXPECT_EQ(ioCount(), count) << "nothing created again";
    EXPECT_EQ(linked("rd", "heat_level1"), "rd_heat_level1_1");
    expectUniqueIds();

    //The program's IO goes, never the one that held the conventional id.
    changeParam("rd", "heat_levels", "1", "0");
    EXPECT_EQ(io("rd_heat_level1_1"), nullptr);
    EXPECT_NE(io("rd_heat_level1"), nullptr);
    EXPECT_NE(io("rd_heat_level1_0"), nullptr);
    EXPECT_NE(io("rd_heat_level1_2"), nullptr);
    EXPECT_NE(io("rd_program_enabled"), nullptr);
    changeParam("rd", "heat_levels", "0", "1");
    EXPECT_EQ(linked("rd", "heat_level1"), "rd_heat_level1_1");
    expectUniqueIds();
}

TEST_F(ClimateProgramTest, ADisabledProgramWritesNoPairInAutoUntilItIsTurnedOn)
{
    load();
    set("prog_heat_default", 20);
    set("prog_cool_default", 25);
    ASSERT_TRUE(mode("prog", "auto"));
    ASSERT_EQ(num("prog_low"), 20);
    ASSERT_EQ(num("prog_high"), 25);

    setBool("prog_program_enabled", false);
    set("prog_heat_default", 22);
    set("prog_cool_default", 28);
    EXPECT_EQ(num("prog_low"), 20);
    EXPECT_EQ(num("prog_high"), 25);

    setBool("prog_program_enabled", true);
    EXPECT_EQ(num("prog_low"), 22);
    EXPECT_EQ(num("prog_high"), 28);
}

TEST_F(ClimateProgramTest, TurningTheProgramOnWithoutProgramModeWritesNothing)
{
    load();
    ASSERT_TRUE(mode("prog", "off"));
    setBool("prog_program_enabled", false);
    set("prog_target", 20);
    setBool("prog_program_enabled", true);
    EXPECT_EQ(num("prog_target"), 20);
    EXPECT_EQ(dev("prog")->get_value_string(), "off");
}

TEST_F(ClimateProgramTest, ADisabledProgramStaysDisabledAfterARestart)
{
    load();
    ASSERT_TRUE(mode("prog", "heat"));
    setBool("prog_program_enabled", false);
    set("prog_target", 24);
    saveConfig();

    reload();
    EXPECT_FALSE(on("prog_program_enabled"));
    EXPECT_EQ(num("prog_target"), 24) << "no application at start";
}

TEST_F(ClimateProgramTest, OnlyTheSetOfTheModeWrites)
{
    load();
    set("coolset_target", 20);
    ASSERT_TRUE(mode("coolset", "heat"));
    set("coolset_cool_default", 23);
    activate("coolset_cool_level1");
    EXPECT_EQ(num("coolset_target"), 20) << "no heat set: nothing applied in heat";
    ASSERT_TRUE(mode("coolset", "cool"));
    EXPECT_EQ(num("coolset_target"), 22) << "the active level, at its initial value";
    set("coolset_cool_level1_setpoint", 24);
    EXPECT_EQ(num("coolset_target"), 24);
}

TEST_F(ClimateProgramTest, AutoUsesBothSetsAndLeavesTheTargetAlone)
{
    load();
    set("prog_heat_default", 18);
    set("prog_cool_default", 27);
    set("prog_target", 21);
    ASSERT_TRUE(mode("prog", "auto"));
    EXPECT_EQ(num("prog_low"), 18);
    EXPECT_EQ(num("prog_high"), 27);
    EXPECT_EQ(num("prog_target"), 21);

    set("prog_cool_level1_setpoint", 29);
    activate("prog_cool_level1");
    EXPECT_EQ(num("prog_high"), 29) << "the active cool level moves the high bound only";
    EXPECT_EQ(num("prog_low"), 18);
    set("prog_heat_level2_setpoint", 20);
    activate("prog_heat_level2");
    EXPECT_EQ(num("prog_low"), 20);
    EXPECT_EQ(num("prog_high"), 29);
}

TEST_F(ClimateProgramTest, TheAutoPairKeepsTheGapForEverySetpointPair)
{
    load();
    ASSERT_TRUE(mode("prog", "auto"));
    //The default bounds of the setpoints (ClimateBase::DEFAULT_TEMP_MIN/MAX).
    const double min = 7, max = 35;
    for (double heat = min; heat <= max; heat += 1)
    {
        for (double cool = min; cool <= max; cool += 3)
        {
            set("prog_heat_default", heat);
            set("prog_cool_default", cool);
            const double low = num("prog_low"), high = num("prog_high");
            ASSERT_GE(high - low, 2) << heat << "/" << cool;
            ASSERT_GE(low, min) << heat << "/" << cool;
            ASSERT_GE(high, 0) << heat << "/" << cool;
            ASSERT_LE(high, max) << heat << "/" << cool;
            if (cool - heat >= 2)
            {
                ASSERT_EQ(low, heat) << heat << "/" << cool;
                ASSERT_EQ(high, cool) << heat << "/" << cool;
            }
        }
    }
}

TEST_F(ClimateProgramTest, APairThatCannotKeepTheGapWritesNothingAtAll)
{
    load();
    set("wide_low", 18);
    set("wide_high", 24);
    ASSERT_TRUE(mode("wide", "auto"));
    int changes = 0;
    auto low = IOChangeHub::Instance().subscribe("wide_low", [&](IOBase &)
                                                 {
        changes++;
    });
    auto high = IOChangeHub::Instance().subscribe("wide_high", [&](IOBase &)
                                                  {
        changes++;
    });
    set("wide_heat_default", 20);
    set("wide_cool_default", 30);
    activate("wide_heat_level1");
    deactivate("wide_heat_level1");
    setBool("wide_program_enabled", false);
    setBool("wide_program_enabled", true);
    pumpFor(5);
    EXPECT_EQ(changes, 0);
    EXPECT_EQ(num("wide_low"), 18);
    EXPECT_EQ(num("wide_high"), 24);
}

TEST_F(ClimateProgramTest, NoRuleIsGeneratedWhateverTheProgramDoes)
{
    load();
    ASSERT_TRUE(mode("prog", "auto"));
    activate("prog_heat_level1");
    activate("prog_cool_level2");
    ASSERT_TRUE(mode("prog", "heat"));
    setBool("prog_program_enabled", false);
    setBool("prog_program_enabled", true);
    EXPECT_EQ(ListeRule::Instance().size(), 0);

    changeParam("prog", "heat_levels", "2", "0");
    changeParam("prog", "cool_levels", "2", "0");
    changeParam("prog", "heat_levels", "0", "3");
    EXPECT_EQ(ListeRule::Instance().size(), 0);
    saveConfig();
    EXPECT_EQ(rulesXmlOnDisk().find("calaos:rule "), std::string::npos) << rulesXmlOnDisk();
}

//Reports no mode, as an override whose state is unreadable would.
class TestBlindThermostat: public InternalThermostat
{
public:
    TestBlindThermostat(Params &p):
        InternalThermostat(p)
    {
    }

    bool blind = false;

protected:
    bool currentMode(ClimateMode &out) override
    {
        return !blind && InternalThermostat::currentMode(out);
    }
};

REGISTER_DEVICE(test_blind_thermostat, TestBlindThermostat)

TEST_F(ClimateProgramTest, ALevelSetpointThatVanishedLeavesTheTargetAlone)
{
    load(solo("ml", false, levels(1, 0)), "ml");
    set("ml_heat_default", 20);
    set("ml_heat_level1_setpoint", 23);
    ASSERT_TRUE(mode("ml", "heat"));
    activate("ml_heat_level1");
    ASSERT_EQ(num("ml_target"), 23);

    set("ml_target", 21);
    ASSERT_EQ(num("ml_target"), 21);
    ASSERT_TRUE(ListeRoom::Instance().deleteIO(must("ml_heat_level1_setpoint")));
    ASSERT_EQ(io("ml_heat_level1_setpoint"), nullptr);
    ASSERT_EQ(linked("ml", "heat_level1_setpoint"), "ml_heat_level1_setpoint");

    //The level is still the active one: neither its setpoint nor the default is known.
    setBool("ml_program_enabled", false);
    setBool("ml_program_enabled", true);
    pumpFor(5);
    EXPECT_EQ(num("ml_target"), 21);
    EXPECT_EQ(num("ml_heat_default"), 20);
}

TEST_F(ClimateProgramTest, AnUnreadableModeStopsAChangeFromApplyingTheProgram)
{
    load(replaced(solo("nm", false, levels(1, 0)), "impl=\"thermostat\"",
                  "impl=\"test_blind_thermostat\""),
         "nm");
    auto *blindDev = ListeRoom::Instance().findIOAs<TestBlindThermostat>("nm");
    ASSERT_NE(blindDev, nullptr);
    ASSERT_TRUE(mode("nm", "heat"));
    set("nm_heat_level1_setpoint", 22);
    activate("nm_heat_level1");
    ASSERT_EQ(num("nm_target"), 22);

    set("nm_target", 19);
    blindDev->blind = true;
    set("nm_heat_level1_setpoint", 24);
    pumpFor(5);
    EXPECT_EQ(num("nm_target"), 19) << "no mode, no write";

    blindDev->blind = false;
    set("nm_heat_level1_setpoint", 25);
    EXPECT_EQ(num("nm_target"), 25) << "readable again, the change applies";
}

TEST_F(ClimateProgramTest, AutoWritesNothingWhileABoundWasNeverReceived)
{
    //Not saved: an entry of the state cache would make the bounds known, and it cannot be erased.
    std::string xml = solo("nr", true, levels(1, 1));
    for (const char *bound: { "nr_low", "nr_high" })
        xml = replaced(xml, ownedXml("InternalInt", bound, SAVED), ownedXml("InternalInt", bound));
    load(xml, "nr");
    ASSERT_FALSE(io("nr_low")->hasValue());
    ASSERT_FALSE(io("nr_high")->hasValue());

    ASSERT_TRUE(mode("nr", "auto"));
    set("nr_heat_default", 20);
    pumpFor(5);
    EXPECT_FALSE(io("nr_low")->hasValue());
    EXPECT_FALSE(io("nr_high")->hasValue());

    //One known bound is not enough: the pair is written whole or not at all.
    set("nr_low", 18);
    ASSERT_TRUE(io("nr_low")->hasValue());
    ASSERT_FALSE(io("nr_high")->hasValue());
    set("nr_heat_default", 21);
    pumpFor(5);
    EXPECT_EQ(num("nr_low"), 18);
    EXPECT_FALSE(io("nr_high")->hasValue());

    set("nr_high", 26);
    set("nr_heat_default", 22);
    EXPECT_EQ(num("nr_low"), 22);
    EXPECT_TRUE(io("nr_high")->hasValue());
    EXPECT_EQ(num("nr_high"), 26);

    //Cooling off at level 0: the high bound goes to its maximum, not to a setpoint.
    setBool("nr_cool_default_off", true);
    EXPECT_EQ(num("nr_high"), 35);
}

TEST_F(ClimateProgramTest, AUserIoLinkedAsProgramEnabledOutlivesTheLevels)
{
    std::string xml = solo("ow", false, levels(1, 0), roomIo("InternalBool", "ow_flag"));
    xml = replaced(xml, linkXml("output_heat", "ow_heat"),
                   linkXml("output_heat", "ow_heat") + linkXml("program_enabled", "ow_flag"));
    load(xml, "ow");
    ASSERT_EQ(linked("ow", "program_enabled"), "ow_flag");
    EXPECT_EQ(io("ow_program_enabled"), nullptr) << "the user's IO is the flag, none is created";

    changeParam("ow", "heat_levels", "1", "0");
    EXPECT_EQ(linked("ow", "program_enabled"), "");
    EXPECT_NE(io("ow_flag"), nullptr) << "not owned by the device: it stays";
    EXPECT_EQ(io("ow_heat_default"), nullptr);
    expectUniqueIds();
}

TEST_F(ClimateProgramTest, LevelSetpointsCopyTheBoundsOfTheTarget)
{
    const std::string plain = ownedXml("InternalInt", "vb_target", SAVED);
    load(replaced(solo("vb", false, levels(1, 0)), plain,
                  ownedXml("InternalInt", "vb_target",
                           std::string(SAVED) + R"( value_min="10" value_max="30" step="2")")),
         "vb");
    for (const char *id: { "vb_heat_default", "vb_heat_level1_setpoint" })
    {
        ASSERT_NE(io(id), nullptr) << id;
        EXPECT_EQ(io(id)->get_param("value_min"), "10") << id;
        EXPECT_EQ(io(id)->get_param("value_max"), "30") << id;
        EXPECT_EQ(io(id)->get_param("step"), "2") << id;
    }
}

TEST_F(ClimateProgramTest, AnIdKeptByAnInertElementIsNotTakenByAProgramIo)
{
    //The element sits after the thermostat and is kept as is: its ids are
    //not in the model, only reserved.
    const std::string inert =
        "    <calaos:device id=\"future\" type=\"climate\" impl=\"future_impl\" name=\"F\">\n" +
        ownedXml("InternalBool", "res_heat_level1") + ownedXml("InternalInt", "res_heat_default") +
        "    </calaos:device>\n";
    load(solo("res", false, levels(1, 0), inert), "res");

    ASSERT_TRUE(ListeRoom::Instance().isIdReserved("res_heat_level1"));
    EXPECT_EQ(io("res_heat_level1"), nullptr);
    EXPECT_EQ(io("res_heat_default"), nullptr);
    for (const char *role: { "heat_level1", "heat_default" })
    {
        const std::string id = linked("res", role);
        EXPECT_EQ(id.rfind(std::string("res_") + role + "_", 0), 0u) << role << " -> " << id;
        EXPECT_NE(io(id), nullptr) << id;
    }
    EXPECT_EQ(linked("res", "heat_level1_setpoint"), "res_heat_level1_setpoint");
    expectUniqueIds();
}

TEST_F(ClimateProgramTest, ResolvingTheLinksAgainCreatesNothing)
{
    load();
    const int before = ioCount();
    const std::vector<std::string> ids = allIds();
    const std::string level = linked("prog", "heat_level1");

    dev("prog")->resolveLinks();
    dev("prog")->resolveLinks();

    EXPECT_EQ(ioCount(), before);
    EXPECT_EQ(allIds(), ids);
    EXPECT_EQ(linked("prog", "heat_level1"), level);
    expectUniqueIds();
}

//state.program (docs/08_http_api.md): the active level, the setpoint it
//writes and the next change over 7 days. Instants are built from the clock
//of the run, so every range is relative to the current hour.
namespace
{

//Start of the current hour, moved by `hours` and `secs`, in local time.
time_t hourAt(int hours, int secs = 0)
{
    time_t t = time(nullptr);
    struct tm tm = *localtime(&t);
    tm.tm_min = 0;
    tm.tm_sec = secs;
    tm.tm_hour += hours;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

time_t parseLocal(const std::string &iso)
{
    struct tm tm = {};
    const char *end = strptime(iso.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
    EXPECT_NE(end, nullptr) << iso;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

} //namespace

class ClimateProgramStateTest: public ClimateProgramTest
{
protected:
    //Every day of the week, from hour `sh` to `eh` (end second included).
    static void everyDay(const std::string &id, int sh, int eh, int es = 0)
    {
        InPlageHoraire *r = range(id);
        ASSERT_NE(r, nullptr);
        r->clear();
        for (int d = TimeRange::SUNDAY; d <= TimeRange::SATURDAY; d++)
            addOn(r, d, span(std::to_string(sh).c_str(), "0", "0", std::to_string(eh).c_str(), "0", std::to_string(es).c_str()));
        r->hasChanged();
    }

    int startHour = 0;

    void SetUp() override
    {
        ClimateProgramTest::SetUp();
        startHour = hourNow();
    }

    //The expected instants are built from the hour of the start; skip when the run crossed it.
    bool hourRolled() const { return hourNow() != startHour; }

    static int hourNow()
    {
        time_t t = time(nullptr);
        return localtime(&t)->tm_hour;
    }

    static Json program() { return dev("prog")->deviceObject().at("state").at("program"); }
};

TEST_F(ClimateProgramStateTest, TheStateFollowsTheContract)
{
    load();
    ASSERT_TRUE(mode("prog", "heat"));
    set("prog_heat_level2_setpoint", 19);
    activate("prog_heat_level2");

    const Json p = program();
    EXPECT_TRUE(p.at("enabled").get<bool>());
    EXPECT_EQ(p.at("mode_set"), "heat");
    const Json &heat = p.at("heat");
    EXPECT_EQ(heat.at("levels"), 10);
    //remote_ui rejects 2.0 for an integer field.
    EXPECT_TRUE(heat.at("active_level").is_number_integer()) << heat.dump();
    EXPECT_EQ(heat.at("active_level").get<int>(), 2);
    EXPECT_EQ(heat.at("setpoint").get<double>(), 19);
    EXPECT_TRUE(heat.at("default").is_number());
    EXPECT_TRUE(p.at("cool").at("active_level").is_null());
    //Active all day, every day: nothing changes.
    EXPECT_TRUE(p.at("next_change").is_null()) << p.dump();

    const Json caps = dev("prog")->deviceObject().at("capabilities");
    EXPECT_EQ(caps.at("program").at("heat_levels"), 10);
    EXPECT_EQ(caps.at("program").at("max_levels"), 10);
}

TEST_F(ClimateProgramStateTest, NoActiveLevelIsNull)
{
    load();
    ASSERT_TRUE(mode("prog", "heat"));
    EXPECT_TRUE(program().at("heat").at("active_level").is_null());
}

TEST_F(ClimateProgramStateTest, TheNextChangeIsTheStartOfALaterRange)
{
    load();
    ASSERT_TRUE(mode("prog", "heat"));
    set("prog_heat_level1_setpoint", 21);
    everyDay("prog_heat_level1", (hourNow() + 2) % 24, (hourNow() + 2) % 24, 59);

    const Json next = program().at("next_change");
    ASSERT_TRUE(next.is_object()) << program().dump();
    EXPECT_EQ(next.at("set"), "heat");
    EXPECT_EQ(next.at("level"), 1);
    EXPECT_EQ(next.at("setpoint").get<double>(), 21);
    const std::string at = next.at("at").get<std::string>();
    ASSERT_EQ(at.size(), 25u) << at;
    EXPECT_EQ(at[22], ':');
    const time_t when = parseLocal(at);
    EXPECT_EQ(when, hourAt(2));
}

TEST_F(ClimateProgramStateTest, ARangeOverMidnightReportsItsEndAsWritten)
{
    load();
    ASSERT_TRUE(mode("prog", "heat"));
    const int h = hourNow();
    //From this hour to the previous one: active now, flips an hour before
    //it started, tomorrow.
    everyDay("prog_heat_level1", h, (h + 23) % 24);
    time_t t = time(nullptr);
    if (localtime(&t)->tm_min == 0 && localtime(&t)->tm_sec == 0)
        GTEST_SKIP() << "on the start second";
    ASSERT_TRUE(on("prog_heat_level1"));

    const Json next = program().at("next_change");
    ASSERT_TRUE(next.is_object()) << program().dump();
    EXPECT_TRUE(next.at("level").is_null());
    if (hourRolled())
        GTEST_SKIP() << "the hour changed during the test";
    EXPECT_EQ(parseLocal(next.at("at").get<std::string>()), hourAt(23));
}

TEST_F(ClimateProgramStateTest, AnInvalidRangeIsSkippedWhenLookingForTheNextChange)
{
    load();
    ASSERT_TRUE(mode("prog", "heat"));
    set("prog_heat_level1_setpoint", 21);
    everyDay("prog_heat_level1", (hourNow() + 2) % 24, (hourNow() + 2) % 24, 59);
    InPlageHoraire *r = range("prog_heat_level1");
    for (int d = TimeRange::SUNDAY; d <= TimeRange::SATURDAY; d++)
        addOn(r, d, span("x", "0", "0", "y", "0", "0"));
    r->hasChanged();

    const Json next = program().at("next_change");
    ASSERT_TRUE(next.is_object()) << program().dump();
    if (hourRolled())
        GTEST_SKIP() << "the hour changed during the test";
    EXPECT_EQ(parseLocal(next.at("at").get<std::string>()), hourAt(2));
}

TEST_F(ClimateProgramStateTest, CoolingDefaultOffGivesANullSetpointToTheNextChange)
{
    load();
    ASSERT_TRUE(mode("off", "cool"));
    ASSERT_TRUE(on("off_cool_default_off"));
    const int h = hourNow();
    everyDay("off_cool_level1", h, (h + 23) % 24);
    time_t t = time(nullptr);
    if (localtime(&t)->tm_min == 0 && localtime(&t)->tm_sec == 0)
        GTEST_SKIP() << "on the start second";
    ASSERT_TRUE(on("off_cool_level1"));

    const Json p = dev("off")->deviceObject().at("state").at("program");
    const Json &next = p.at("next_change");
    ASSERT_TRUE(next.is_object()) << p.dump();
    EXPECT_EQ(next.at("set"), "cool");
    EXPECT_TRUE(next.at("level").is_null());
    EXPECT_TRUE(next.at("setpoint").is_null()) << p.dump();
}

TEST_F(ClimateProgramStateTest, ALowerLevelRangeHiddenByAHigherOneChangesNothing)
{
    load();
    ASSERT_TRUE(mode("prog", "heat"));
    activate("prog_heat_level1");
    everyDay("prog_heat_level2", hourNow(), hourNow(), 59);
    EXPECT_TRUE(program().at("next_change").is_null()) << program().dump();
}

TEST_F(ClimateProgramStateTest, ALevelPriorityChangeIsTheNextChange)
{
    load();
    ASSERT_TRUE(mode("prog", "heat"));
    set("prog_heat_level1_setpoint", 23);
    activate("prog_heat_level2");
    const int h = (hourNow() + 2) % 24;
    everyDay("prog_heat_level1", h, h, 59);

    const Json p = program();
    EXPECT_EQ(p.at("heat").at("active_level"), 2);
    const Json &next = p.at("next_change");
    ASSERT_TRUE(next.is_object()) << p.dump();
    EXPECT_EQ(next.at("level"), 1);
    EXPECT_EQ(next.at("setpoint").get<double>(), 23);
    if (hourRolled())
        GTEST_SKIP() << "the hour changed during the test";
    EXPECT_EQ(parseLocal(next.at("at").get<std::string>()), hourAt(2));
}

TEST_F(ClimateProgramStateTest, TheNextChangeIsRepublishedAfterARangeEditWithoutALevelChange)
{
    load();
    ASSERT_TRUE(mode("prog", "heat"));
    everyDay("prog_heat_level1", (hourNow() + 2) % 24, (hourNow() + 2) % 24, 59);
    pumpFor(300);

    std::vector<Json> seen;
    sigc::connection c = EventManager::Instance().newEvent.connect(
        [&seen](const CalaosEvent &ev)
        {
        const Json j = ev.toJson();
        if (ev.getType() == CalaosEvent::EventIOChanged && j.at("data").value("id", "") == "prog" &&
            j.at("data").contains("device"))
            seen.push_back(j.at("data").at("device").at("state").at("program"));
    });

    //What set_timerange does: the edit, then the owner's notification. The
    //range stays inactive, so the level itself does not change.
    everyDay("prog_heat_level1", (hourNow() + 3) % 24, (hourNow() + 3) % 24, 59);
    IOChangeHub::Instance().notify("prog_heat_level1");
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return !seen.empty();
    }));
    c.disconnect();

    const Json next = seen.back().at("next_change");
    ASSERT_TRUE(next.is_object()) << seen.back().dump();
    if (hourRolled())
        GTEST_SKIP() << "the hour changed during the test";
    EXPECT_EQ(parseLocal(next.at("at").get<std::string>()), hourAt(3));
}

TEST_F(ClimateProgramStateTest, ADisabledProgramHasNoNextChange)
{
    load();
    ASSERT_TRUE(mode("prog", "heat"));
    everyDay("prog_heat_level1", (hourNow() + 2) % 24, (hourNow() + 2) % 24, 59);
    ASSERT_TRUE(program().at("next_change").is_object());
    setBool("prog_program_enabled", false);
    const Json p = program();
    EXPECT_FALSE(p.at("enabled").get<bool>());
    EXPECT_TRUE(p.at("next_change").is_null());
}

TEST_F(ClimateProgramStateTest, ADisabledRangeNeverChanges)
{
    load();
    ASSERT_TRUE(mode("prog", "heat"));
    activate("prog_heat_level1");
    everyDay("prog_heat_level2", (hourNow() + 2) % 24, (hourNow() + 2) % 24, 59);
    range("prog_heat_level1")->set_param("enabled", "false");
    range("prog_heat_level2")->set_param("enabled", "false");

    const Json p = program();
    EXPECT_EQ(p.at("heat").at("active_level"), 1);
    EXPECT_TRUE(p.at("next_change").is_null()) << p.dump();
    //a disabled range keeps its value whatever the date
    EXPECT_TRUE(range("prog_heat_level1")->isActiveAt(time(nullptr) + 3 * 24 * 3600));
    EXPECT_FALSE(range("prog_heat_level2")->isActiveAt(time(nullptr) + 3 * 24 * 3600));
}

//Fixed dates, so the month and the sun cases do not depend on the clock of the run.
class ClimateProgramCalendarTest: public ClimateProgramStateTest
{
protected:
    static time_t local(int y, int mo, int d, int h = 0, int mi = 0, int s = 0)
    {
        struct tm tm = {};
        tm.tm_year = y - 1900;
        tm.tm_mon = mo - 1;
        tm.tm_mday = d;
        tm.tm_hour = h;
        tm.tm_min = mi;
        tm.tm_sec = s;
        tm.tm_isdst = -1;
        return mktime(&tm);
    }

    static void everyWeekday(const std::string &id, const TimeRange &t, const std::vector<int> &months)
    {
        InPlageHoraire *r = range(id);
        ASSERT_NE(r, nullptr);
        r->clear();
        if (!months.empty())
        {
            r->months.reset();
            for (int m: months)
                r->months.set(m);
        }
        for (int d = TimeRange::SUNDAY; d <= TimeRange::SATURDAY; d++)
            addOn(r, d, t);
        r->hasChanged();
    }

    //"+01:00" form of the UTC offset at `t`
    static std::string tzSuffix(time_t t)
    {
        char buf[16];
        struct tm tm = *localtime(&t);
        strftime(buf, sizeof(buf), "%z", &tm);
        std::string z = buf;
        return z.substr(0, 3) + ":" + z.substr(3);
    }

    static std::string isoLocal(time_t t)
    {
        char buf[32];
        struct tm tm = *localtime(&t);
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
        return buf + tzSuffix(t);
    }

    Json nextAt(time_t now) { return dev("prog")->programNextChange("heat", now); }

    static std::string atOf(const Json &next)
    {
        EXPECT_TRUE(next.is_object()) << next.dump();
        return next.is_object() ? next.at("at").get<std::string>() : std::string();
    }
};

TEST_F(ClimateProgramCalendarTest, AMonthChangeCutsAnOvernightRangeAtMidnight)
{
    load();
    everyWeekday("prog_heat_level1", span("22", "0", "0", "6", "0", "0"), { InPlageHoraire::JANUARY });
    EXPECT_EQ(atOf(nextAt(local(2027, 1, 31, 23))), "2027-02-01T00:00:00" + tzSuffix(local(2027, 2, 1)));
}

TEST_F(ClimateProgramCalendarTest, ARangeEndingAtTheEndOfTheDayKeepsItsWrittenEnd)
{
    load();
    everyWeekday("prog_heat_level1", span("20", "0", "0", "23", "59", "59"), { InPlageHoraire::JANUARY });
    EXPECT_EQ(atOf(nextAt(local(2027, 1, 31, 21))), "2027-01-31T23:59:59" + tzSuffix(local(2027, 1, 31)));
}

TEST_F(ClimateProgramCalendarTest, AMonthChangeCutsARangeStillActiveAtMidnight)
{
    load();
    everyWeekday("prog_heat_level1", span("0", "0", "0", "23", "59", "59"), { InPlageHoraire::JANUARY });
    const Json next = nextAt(local(2027, 1, 31, 12));
    EXPECT_EQ(atOf(next), "2027-02-01T00:00:00" + tzSuffix(local(2027, 2, 1)));
    EXPECT_TRUE(next.at("level").is_null());
}

TEST_F(ClimateProgramCalendarTest, AnIncludedMonthStartsAtMidnightOfItsFirstDay)
{
    load();
    everyWeekday("prog_heat_level1", span("22", "0", "0", "6", "0", "0"), { InPlageHoraire::FEBRUARY });
    const Json next = nextAt(local(2027, 1, 31, 12));
    EXPECT_EQ(atOf(next), "2027-02-01T00:00:00" + tzSuffix(local(2027, 2, 1)));
    EXPECT_EQ(next.at("level"), 1);
}

TEST_F(ClimateProgramCalendarTest, ARangeBeyondTheHorizonGivesNoNextChange)
{
    load();
    everyWeekday("prog_heat_level1", span("8", "0", "0", "9", "0", "0"), { InPlageHoraire::JULY });
    EXPECT_TRUE(nextAt(local(2027, 1, 10, 12)).is_null());
}

TEST_F(ClimateProgramCalendarTest, ASolarBoundIsEvaluatedAtItsOwnDate)
{
    load();
    TimeRange t = span("0", "0", "0", "23", "59", "59");
    t.start_type = TimeRange::HTYPE_SUNRISE;
    everyWeekday("prog_heat_level1", t, {});

    std::vector<time_t> starts;
    for (int mo: { 1, 7 })
    {
        const time_t midnight = local(2027, mo, 10);
        const time_t expected = midnight + t.getStartTimeSec(2027, mo, 10);
        EXPECT_EQ(atOf(nextAt(midnight)), isoLocal(expected));
        starts.push_back(expected - midnight);
    }
    EXPECT_NE(starts[0], starts[1]);
}

TEST_F(ClimateProgramCalendarTest, AnOvernightRangeEndsAtTheSolarEndOfItsStartDay)
{
    load();
    TimeRange t = span("22", "0", "0", "0", "0", "0");
    t.end_type = TimeRange::HTYPE_SUNRISE;
    InPlageHoraire *r = range("prog_heat_level1");
    r->clear();
    for (int d = TimeRange::SUNDAY; d <= TimeRange::SATURDAY; d++)
        addOn(r, d, t);

    const time_t midnight = local(2027, 1, 10);
    const long end = t.getEndTimeSec(2027, 1, 10);
    std::vector<time_t> instants;
    r->changeInstants(local(2027, 1, 11), local(2027, 1, 11, 12), instants);
    EXPECT_NE(std::find(instants.begin(), instants.end(), midnight + 86400 + end + 1), instants.end());
}

TEST_F(ClimateProgramCalendarTest, TheChangeOfTimeKeepsTheLocalHours)
{
    //POSIX rule, without tzdata: CET, CEST from the last Sunday of March to the last of October.
    const char *old = getenv("TZ");
    const std::string saved = old ? old : "";
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    load();
    everyWeekday("prog_heat_level1", span("8", "0", "0", "9", "0", "0"), {});
    //the day of 23 hours, then the one of 25
    EXPECT_EQ(atOf(nextAt(local(2027, 3, 27, 12))), "2027-03-28T08:00:00+02:00");
    EXPECT_EQ(atOf(nextAt(local(2027, 3, 28, 8, 30))), "2027-03-28T09:00:00+02:00");
    EXPECT_EQ(atOf(nextAt(local(2027, 10, 30, 12))), "2027-10-31T08:00:00+01:00");
    EXPECT_EQ(atOf(nextAt(local(2027, 10, 31, 8, 30))), "2027-10-31T09:00:00+01:00");

    if (old)
        setenv("TZ", saved.c_str(), 1);
    else
        unsetenv("TZ");
    tzset();
}

TEST_F(ClimateProgramCalendarTest, ARepeatedHourIsReportedAtEachOfItsPasses)
{
    //The hour 02:00-03:00 of 2027-10-31 happens twice; the range follows the wall clock,
    //so it starts at both 02:30.
    const char *old = getenv("TZ");
    const std::string saved = old ? old : "";
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    load();
    everyWeekday("prog_heat_level1", span("2", "30", "0", "2", "45", "0"), {});
    EXPECT_EQ(atOf(nextAt(local(2027, 10, 31, 0))), "2027-10-31T02:30:00+02:00");
    EXPECT_EQ(atOf(nextAt(local(2027, 10, 31, 0) + 2 * 3600 + 30 * 60 + 1)), "2027-10-31T02:45:00+02:00");
    EXPECT_EQ(atOf(nextAt(local(2027, 10, 31, 0) + 2 * 3600 + 45 * 60 + 1)), "2027-10-31T02:30:00+01:00");

    if (old)
        setenv("TZ", saved.c_str(), 1);
    else
        unsetenv("TZ");
    tzset();
}

TEST_F(ClimateProgramCalendarTest, AMissingHourStartsNothing)
{
    //02:00-03:00 of 2027-03-28 does not exist: a range inside it never runs that day.
    const char *old = getenv("TZ");
    const std::string saved = old ? old : "";
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    load();
    everyWeekday("prog_heat_level1", span("2", "30", "0", "2", "45", "0"), {});
    EXPECT_EQ(atOf(nextAt(local(2027, 3, 28, 0))), "2027-03-29T02:30:00+02:00");

    if (old)
        setenv("TZ", saved.c_str(), 1);
    else
        unsetenv("TZ");
    tzset();
}

TEST_F(ClimateProgramStateTest, ALevelOrEnabledChangeIsPublished)
{
    load();
    ASSERT_TRUE(mode("prog", "heat"));
    pumpFor(300);

    std::vector<Json> seen;
    sigc::connection c = EventManager::Instance().newEvent.connect(
        [&seen](const CalaosEvent &ev)
        {
        const Json j = ev.toJson();
        if (ev.getType() == CalaosEvent::EventIOChanged && j.at("data").value("id", "") == "prog" &&
            j.at("data").contains("device"))
            seen.push_back(j.at("data").at("device").at("state").at("program"));
    });

    activate("prog_heat_level1");
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return !seen.empty() && seen.back().at("heat").at("active_level") == 1;
    }));

    seen.clear();
    setBool("prog_program_enabled", false);
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return !seen.empty() && !seen.back().at("enabled").get<bool>();
    }));
    c.disconnect();
}
