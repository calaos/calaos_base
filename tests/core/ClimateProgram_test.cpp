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
#include "IO/InPlageHoraire.h"
#include "InternalThermostat.h"
#include "ListeRule.h"
#include "libuvw.h"

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
    pumpUntil([]() { return false; }, ms);
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
                                           "big", "neg", "frac", "uniq", "off", "boot" };

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
    void load(const std::string &xml = house())
    {
        loadConfig(xml, rulesXmlDocument(""));
        waitStarted();
    }

    void reload()
    {
        Config::Instance().loadStateCache();
        reloadFromDisk();
        waitStarted();
    }

    static void waitStarted()
    {
        EXPECT_TRUE(pumpUntil([]() { return io("prog_action")->hasValue(); }));
        pumpFor(5);
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
        "heat_level1",  "heat_level1_setpoint", "heat_level2",          "heat_level2_setpoint",
        "cool_level1",  "cool_level1_setpoint", "cool_level2",          "cool_level2_setpoint",
        "heat_default", "cool_default",         "cool_default_off",     "program_enabled"
    };
    for (const std::string &role: roles)
    {
        EXPECT_EQ(linked("prog", role), "prog_" + role) << role;
        IOBase *owned = io("prog_" + role);
        ASSERT_NE(owned, nullptr) << role;
        EXPECT_EQ(owned->get_param("visible"), "false") << role;
        EXPECT_NE(owned->get_param("log_history"), "true") << role;
    }
    EXPECT_EQ(linked("prog", "heat_level3"), "");
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

TEST_F(ClimateProgramTest, FewerLevelsRemoveTheirIos)
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
    for (const char *gone: { "prog_heat_level2", "prog_heat_level2_setpoint", "prog_cool_level1",
                             "prog_cool_level1_setpoint", "prog_cool_level2", "prog_cool_default",
                             "prog_cool_default_off" })
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
    EXPECT_EQ(linked("big", "heat_level6"), "big_heat_level6");
    EXPECT_EQ(linked("big", "heat_level7"), "");

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
