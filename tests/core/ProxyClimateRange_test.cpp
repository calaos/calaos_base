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

/* ProxyClimate emulate_range (decision 21): the device keeps the range and
 * its auto drives a single setpoint unit in heat, cool or off. The unit is an
 * InternalString mode and an InternalInt setpoint, the room a sensor driver.
 * Waits that must not happen use lock-outs of tens of seconds; waits that
 * must happen are polled for seconds. */

#include "CalaosConfig.h"
#include "CalaosCoreFixture.h"
#include "IOChangeHub.h"
#include "ProxyClimate.h"
#include "libuvw.h"

#include <chrono>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

//A sensor driver that dates every reading.
class RangeSensor: public IOBase
{
public:
    RangeSensor(Params &p):
        IOBase(p, IOBase::IO_INPUT)
    {
    }

    DATA_TYPE get_type() override { return TINT; }
    double get_value_double() override { return value; }
    bool datesEveryReading() override { return true; }

    void deliver(double v)
    {
        value = v;
        EmitSignalIO();
    }

private:
    double value = 0;
};

//An output the unit refuses to take.
class RangeRefusing: public IOBase
{
public:
    RangeRefusing(Params &p):
        IOBase(p, IOBase::IO_OUTPUT)
    {
    }

    DATA_TYPE get_type() override { return TSTRING; }
    bool set_value(std::string) override { return false; }
    bool set_value(double) override { return false; }
};

REGISTER_IO(RangeSensor)
REGISTER_IO(RangeRefusing)

namespace
{

using Clock = std::chrono::steady_clock;

struct Write
{
    std::string id;
    std::string value;
    Clock::time_point at;
};

std::vector<Write> &writes()
{
    static std::vector<Write> w;
    return w;
}

std::string element(const std::string &kind, const std::string &type, const std::string &id,
                    const std::string &extra = std::string())
{
    return "      <calaos:" + kind + " type=\"" + type + "\" id=\"" + id + "\" name=\"" + id +
           "\" visible=\"false\"" + extra + " />\n";
}

std::string internal(const std::string &type, const std::string &id, const std::string &extra = std::string())
{
    return element("internal", type, id, extra);
}

std::string sensor(const std::string &id) { return element("input", "RangeSensor", id); }

std::string link(const std::string &name, const std::string &io,
                 const std::string &commandIo = std::string())
{
    std::string x = "      <calaos:link name=\"" + name + "\" io=\"" + io + "\"";
    if (!commandIo.empty())
        x += " command_io=\"" + commandIo + "\"";
    return x + " />\n";
}

std::string param(const std::string &name, const std::string &value)
{
    return "      <calaos:param name=\"" + name + "\" value=\"" + value + "\" />\n";
}

std::string device(const std::string &id, const std::string &inner)
{
    return "    <calaos:device id=\"" + id + "\" type=\"climate\" impl=\"proxy\" name=\"" + id +
           "\">\n" + inner + "    </calaos:device>\n";
}

const std::string BOUNDS = R"( value_min="16" value_max="30" step="0.5")";

std::string lockouts(const std::string &on, const std::string &off, const std::string &changeover)
{
    return param("min_on_time", on) + param("min_off_time", off) + param("changeover_delay", changeover);
}

/* A unit with a single setpoint and its owned range (20..24). The mode io is
 * the unit's word: writing it is the unit taking the command at once. */
std::string unit(const std::string &id, const std::string &params, const std::string &extra = std::string())
{
    return device(id, internal("InternalString", id + "_mode") + internal("InternalInt", id + "_target", BOUNDS) +
                          sensor(id + "_cur") +
                          internal("InternalInt", id + "_low", BOUNDS + R"( save="true" value="20")") +
                          internal("InternalInt", id + "_high", BOUNDS + R"( save="true" value="24")") +
                          link("mode", id + "_mode") + link("target_temp", id + "_target") +
                          link("current_temp", id + "_cur") + link("target_temp_low", id + "_low") +
                          link("target_temp_high", id + "_high") + param("modes", "off,heat,cool,dry,fan_only") +
                          param("min_range", "2") + param("emulate_range", "true") +
                          param("hysteresis", "0.5") + params + extra);
}

const std::vector<std::string> DEVICES = { "em", "el", "elc", "es", "er", "ed", "eh", "et", "erst", "erst2", "erec",
                                           "erf", "erm", "enoref", "bad_target", "bad_modes", "bad_flag",
                                           "bad_range", "off_flag", "plain" };

std::string house()
{
    std::string s;
    s += unit("em", lockouts("0", "0", "0"), internal("InternalString", "em_action") + link("action", "em_action"));
    s += unit("el", lockouts("30", "30", "60"));
    s += unit("elc", lockouts("30", "30", "60"));
    s += unit("es", lockouts("0.2", "0.2", "1"));
    //The room sensor relayed to the unit is the reference, not the unit's own.
    s += unit("er", lockouts("0", "0", "0"),
              sensor("er_src") + internal("InternalInt", "er_remote") + link("remote_temp", "er_remote") +
                  link("remote_temp_source", "er_src"));
    s += unit("et", lockouts("0", "0", "0") + param("sensor_timeout", "0.3"));
    s += unit("erst", lockouts("0", "0", "0"));
    s += unit("erst2", lockouts("0", "0", "0"));
    s += device("erf", internal("InternalString", "erf_mode") + element("output", "RangeRefusing", "erf_target") +
                           internal("InternalInt", "erf_low", R"( value="20")") +
                           internal("InternalInt", "erf_high", R"( value="24")") + sensor("erf_cur") +
                           link("mode", "erf_mode") + link("target_temp", "erf_target") +
                           link("current_temp", "erf_cur") + link("target_temp_low", "erf_low") +
                           link("target_temp_high", "erf_high") + param("emulate_range", "true") +
                           lockouts("0", "0", "0"));

    //The unit reports its mode on one io and takes commands on another.
    auto lagging = [](const std::string &id, const std::string &hold)
    {
        return device(id, internal("InternalString", id + "_mode") + internal("InternalString", id + "_cmd") +
                              internal("InternalInt", id + "_target", BOUNDS) +
                              internal("InternalInt", id + "_target_cmd", BOUNDS) + sensor(id + "_cur") +
                              internal("InternalInt", id + "_low", BOUNDS + R"( save="true" value="20")") +
                              internal("InternalInt", id + "_high", BOUNDS + R"( save="true" value="24")") +
                              link("mode", id + "_mode", id + "_cmd") +
                              link("target_temp", id + "_target", id + "_target_cmd") +
                              link("current_temp", id + "_cur") + link("target_temp_low", id + "_low") +
                              link("target_temp_high", id + "_high") + param("emulate_range", "true") +
                              lockouts("0", "0", "0") + param("readback_hold", hold));
    };
    s += lagging("ed", "30");
    s += lagging("eh", "0.2");

    //No range: the device creates it, bounded as the unit's setpoint.
    s += device("erec", internal("InternalString", "erec_mode") +
                            internal("InternalInt", "erec_target", R"( value_min="17" value_max="28" step="1")") +
                            sensor("erec_cur") + link("mode", "erec_mode") + link("target_temp", "erec_target") +
                            link("current_temp", "erec_cur") + param("emulate_range", "true") +
                            param("min_range", "8"));
    s += device("erm", element("output", "RangeRefusing", "erm_mode") + internal("InternalInt", "erm_target") +
                           internal("InternalInt", "erm_low") + internal("InternalInt", "erm_high") +
                           sensor("erm_cur") + link("mode", "erm_mode") + link("target_temp", "erm_target") +
                           link("current_temp", "erm_cur") + link("target_temp_low", "erm_low") +
                           link("target_temp_high", "erm_high") + param("emulate_range", "true"));
    s += device("enoref", internal("InternalString", "enoref_mode") + internal("InternalInt", "enoref_target") +
                              internal("InternalInt", "enoref_low") + internal("InternalInt", "enoref_high") +
                              link("mode", "enoref_mode") + link("target_temp", "enoref_target") +
                              link("target_temp_low", "enoref_low") + link("target_temp_high", "enoref_high") +
                              param("emulate_range", "true"));

    s += device("bad_target", internal("InternalString", "bad_target_mode") + link("mode", "bad_target_mode") +
                                  param("emulate_range", "true"));
    s += device("bad_modes", internal("InternalString", "bad_modes_mode") +
                                 internal("InternalInt", "bad_modes_target") + link("mode", "bad_modes_mode") +
                                 link("target_temp", "bad_modes_target") + param("modes", "off,heat") +
                                 param("emulate_range", "true"));
    s += device("bad_flag", internal("InternalString", "bad_flag_mode") + link("mode", "bad_flag_mode") +
                                param("emulate_range", "yes"));
    s += device("off_flag", internal("InternalString", "off_flag_mode") + link("mode", "off_flag_mode") +
                                param("emulate_range", "false"));
    s += device("bad_range", internal("InternalString", "bad_range_mode") +
                                 internal("InternalInt", "bad_range_target") + sensor("bad_range_low") +
                                 internal("InternalInt", "bad_range_high") + link("mode", "bad_range_mode") +
                                 link("target_temp", "bad_range_target") + link("target_temp_low", "bad_range_low") +
                                 link("target_temp_high", "bad_range_high") + param("emulate_range", "true"));
    s += device("plain", internal("InternalString", "plain_mode") + link("mode", "plain_mode") +
                             param("modes", "off,heat,cool"));
    return ioXmlDocument(roomXml("Salon", "salon", s));
}

bool runLoopUntil(const std::function<bool()> &pred, int timeoutMs = 5000)
{
    auto loop = uvw::Loop::getDefault();
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!pred())
    {
        if (Clock::now() > deadline)
            return false;
        loop->run<uvw::Loop::Mode::NOWAIT>();
    }
    return true;
}

void pumpLoopFor(int ms)
{
    const auto end = Clock::now() + std::chrono::milliseconds(ms);
    runLoopUntil([&end]()
                 {
        return Clock::now() >= end;
    },
                 ms + 1000);
}

} //namespace

class ProxyClimateRangeTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        writes().clear();
        for (const std::string &id: DEVICES)
            forgetIOState(id);
        Config::Instance().SaveValueIO("erst", "auto heat", false);
        Config::Instance().SaveValueIO("erst2", "auto heat", false);
        //Cooled before the restart, then stopped by the user.
        Config::Instance().SaveValueIO("elc", "manual off cool", false);
        log = journal([this]()
                      {
            loadConfig(house(), rulesXmlDocument(""));
        });

        //The saved range of the previous case is still in the state cache.
        for (const std::string &id: DEVICES)
        {
            if (IOBase *low = ListeRoom::Instance().get_io(id + "_low"))
                low->set_value(20.0);
            if (IOBase *high = ListeRoom::Instance().get_io(id + "_high"))
                high->set_value(24.0);
        }

        for (const std::string &id: DEVICES)
            for (const char *suffix: { "_mode", "_cmd", "_target", "_target_cmd" })
                subscriptions.push_back(IOChangeHub::Instance().subscribe(id + suffix, [](IOBase &io)
                                                                          {
                    writes().push_back({ io.get_param("id"), io.get_value_string(), Clock::now() });
                }));
    }

    void TearDown() override
    {
        subscriptions.clear();
        CoreFixture::TearDown();
    }

    static ProxyClimate *dev(const std::string &id)
    {
        return ListeRoom::Instance().findIOAs<ProxyClimate>(id);
    }

    static RangeSensor *room(const std::string &id) { return ListeRoom::Instance().findIOAs<RangeSensor>(id); }

    static Json caps(const std::string &id) { return dev(id)->deviceObject().at("capabilities"); }
    static Json state(const std::string &id) { return dev(id)->deviceObject().at("state"); }
    static std::string mode(const std::string &id) { return state(id).value("mode", std::string("<none>")); }
    static bool cmd(const std::string &id, const std::string &command) { return dev(id)->set_value(command); }

    //What the unit was sent on `io`, in order.
    static std::vector<std::string> sent(const std::string &io)
    {
        std::vector<std::string> res;
        for (const Write &w: writes())
        {
            if (w.id == io)
                res.push_back(w.value);
        }
        return res;
    }

    static std::string unitMode(const std::string &id) { return ListeRoom::Instance().get_io(id + "_mode")->get_value_string(); }
    static double unitTarget(const std::string &id)
    {
        return ListeRoom::Instance().get_io(id + "_target")->get_value_double();
    }

    template <class F>
    static std::string journal(F fn)
    {
        testing::internal::CaptureStdout();
        fn();
        return testing::internal::GetCapturedStdout();
    }

    std::string log;
    std::vector<IOChangeHub::Subscription> subscriptions;
};

TEST_F(ProxyClimateRangeTest, AutoAndTheRangeAreOfferedForASingleSetpointUnit)
{
    const Json c = caps("em");
    EXPECT_EQ(c.at("modes"), Json({ "off", "heat", "cool", "auto", "dry", "fan_only" }));
    EXPECT_TRUE(c.at("target_temp_range").get<bool>());
    EXPECT_DOUBLE_EQ(c.at("target_temp").at("min_range").get<double>(), 2);

    //Not asked for, or not possible: the unit's modes as they are.
    EXPECT_EQ(caps("plain").at("modes"), Json({ "off", "heat", "cool" }));
    EXPECT_EQ(caps("off_flag").at("modes").size(), 6u);
    EXPECT_EQ(caps("bad_modes").at("modes"), Json({ "off", "heat" }));
    EXPECT_FALSE(caps("bad_modes").contains("target_temp_range"));
}

TEST_F(ProxyClimateRangeTest, BelowTheRangeTheUnitHeatsOnTheLowSetpoint)
{
    ASSERT_TRUE(cmd("em", "auto"));
    EXPECT_EQ(mode("em"), "auto");
    EXPECT_EQ(dev("em")->get_value_string(), "auto");
    //The unit had reported nothing: auto starts from off.
    EXPECT_EQ(sent("em_mode"), std::vector<std::string>({ "off" }));

    room("em_cur")->deliver(19.6);
    EXPECT_EQ(unitMode("em"), "off") << "inside the hysteresis";

    writes().clear();
    room("em_cur")->deliver(19.4);
    EXPECT_EQ(unitMode("em"), "heat");
    EXPECT_DOUBLE_EQ(unitTarget("em"), 20);
    //The setpoint first: the unit must not start against the previous one.
    ASSERT_EQ(writes().size(), 2u);
    EXPECT_EQ(writes()[0].id, "em_target");
    EXPECT_EQ(writes()[1].id, "em_mode");
    EXPECT_EQ(mode("em"), "auto");
}

TEST_F(ProxyClimateRangeTest, AboveTheRangeTheUnitCoolsOnTheHighSetpoint)
{
    ASSERT_TRUE(cmd("em", "auto"));
    room("em_cur")->deliver(24.4);
    EXPECT_EQ(unitMode("em"), "off");
    room("em_cur")->deliver(24.6);
    EXPECT_EQ(unitMode("em"), "cool");
    EXPECT_DOUBLE_EQ(unitTarget("em"), 24);
}

//The user's choice: nothing runs inside the range, the unit is off and not idle.
TEST_F(ProxyClimateRangeTest, BackInTheRangeTheUnitIsSwitchedOff)
{
    ASSERT_TRUE(cmd("em", "auto"));
    room("em_cur")->deliver(18.0);
    ASSERT_EQ(unitMode("em"), "heat");
    room("em_cur")->deliver(19.9);
    EXPECT_EQ(unitMode("em"), "heat");
    room("em_cur")->deliver(20.0);
    EXPECT_EQ(unitMode("em"), "off");

    room("em_cur")->deliver(26.0);
    ASSERT_EQ(unitMode("em"), "cool");
    room("em_cur")->deliver(24.1);
    EXPECT_EQ(unitMode("em"), "cool");
    room("em_cur")->deliver(24.0);
    EXPECT_EQ(unitMode("em"), "off");
}

TEST_F(ProxyClimateRangeTest, AutoIsNeverSentToTheUnit)
{
    ASSERT_TRUE(cmd("em", "auto"));
    const std::vector<std::pair<double, std::string>> steps = {
        { 18.0, "heat" }, { 22.0, "off" }, { 26.0, "cool" }, { 22.0, "off" }, { 17.0, "heat" }, { 30.0, "cool" }
    };
    for (const auto &step: steps)
    {
        room("em_cur")->deliver(step.first);
        EXPECT_EQ(unitMode("em"), step.second) << step.first;
    }
    ASSERT_TRUE(cmd("em", R"({"mode":"auto","target_temp_low":21,"target_temp_high":23})"));

    for (const std::string &m: sent("em_mode"))
    {
        EXPECT_NE(m, "auto");
        EXPECT_NE(m, "heat_cool");
    }
    EXPECT_GE(sent("em_mode").size(), 7u);
}

//In auto, the unit off inside the range is the device being idle.
TEST_F(ProxyClimateRangeTest, TheActionOfAnIdleUnitIsIdleInAuto)
{
    ListeRoom::Instance().get_io("em_action")->set_value(std::string("off"));
    EXPECT_EQ(state("em").at("action"), "off");
    ASSERT_TRUE(cmd("em", "auto"));
    EXPECT_EQ(state("em").at("action"), "idle");
    ListeRoom::Instance().get_io("em_action")->set_value(std::string("heating"));
    EXPECT_EQ(state("em").at("action"), "heating");
}

TEST_F(ProxyClimateRangeTest, TheUnitSetpointFollowsTheBoundItRunsOn)
{
    ASSERT_TRUE(cmd("em", "auto"));
    room("em_cur")->deliver(17.0);
    ASSERT_EQ(unitMode("em"), "heat");
    ASSERT_TRUE(cmd("em", R"({"target_temp_low":19,"target_temp_high":24})"));
    EXPECT_DOUBLE_EQ(unitTarget("em"), 19);
    EXPECT_EQ(unitMode("em"), "heat");

    //Moving the other bound does not touch it.
    writes().clear();
    ASSERT_TRUE(cmd("em", R"({"target_temp_high":26})"));
    EXPECT_TRUE(sent("em_target").empty());
}

/* {"mode":"auto", range} is applied whole before the unit is driven: against
 * the previous range (20) the room at 19 would have started heat. */
TEST_F(ProxyClimateRangeTest, ACommandIsAppliedWholeBeforeTheUnitIsDriven)
{
    room("em_cur")->deliver(19.0);
    ASSERT_TRUE(cmd("em", R"({"mode":"auto","target_temp_low":18,"target_temp_high":24})"));
    EXPECT_EQ(sent("em_mode"), std::vector<std::string>({ "off" }));
}

/* A restart counts as a stop: the first start waits for min_off_time, not
 * for the changeover delay (a server restart is every config upload). */
TEST_F(ProxyClimateRangeTest, AStartAfterARestartWaitsForItsLockoutAndSaysSo)
{
    ASSERT_TRUE(cmd("el", "auto"));
    room("el_cur")->deliver(15.0);
    EXPECT_EQ(unitMode("el"), "off");
    const Json lockout = state("el").at("lockout");
    EXPECT_EQ(lockout.at("output"), "heat");
    EXPECT_GT(lockout.at("remaining").get<int>(), 20);
    EXPECT_LE(lockout.at("remaining").get<int>(), 30);

    //Back inside the range: nothing waits any more.
    room("el_cur")->deliver(21.0);
    EXPECT_FALSE(state("el").contains("lockout"));
}

//The last mode the unit ran in is kept: only the opposite one waits for the changeover.
TEST_F(ProxyClimateRangeTest, AfterARestartOnlyTheOppositeOfTheLastModeWaitsForTheChangeover)
{
    ASSERT_TRUE(cmd("elc", "auto"));
    room("elc_cur")->deliver(15.0);
    EXPECT_GT(state("elc").at("lockout").at("remaining").get<int>(), 30);
    room("elc_cur")->deliver(27.0);
    EXPECT_EQ(state("elc").at("lockout").at("output"), "cool");
    EXPECT_LE(state("elc").at("lockout").at("remaining").get<int>(), 30);
}

TEST_F(ProxyClimateRangeTest, AStopWaitsForMinOnTime)
{
    //Heat selected by the user starts at once, and starts the lock-out.
    ASSERT_TRUE(cmd("el", "heat"));
    ASSERT_EQ(unitMode("el"), "heat");
    ASSERT_TRUE(cmd("el", "auto"));
    room("el_cur")->deliver(21.0);
    EXPECT_EQ(unitMode("el"), "heat");
    EXPECT_FALSE(state("el").contains("lockout")) << "only a start that waits is shown";
}

//A mode the user selects is applied at once, auto or not.
TEST_F(ProxyClimateRangeTest, AModeSelectedByTheUserIgnoresTheLockouts)
{
    ASSERT_TRUE(cmd("el", "auto"));
    room("el_cur")->deliver(15.0);
    ASSERT_TRUE(state("el").contains("lockout"));
    ASSERT_TRUE(cmd("el", "cool"));
    EXPECT_EQ(unitMode("el"), "cool");
    EXPECT_EQ(mode("el"), "cool");
    EXPECT_FALSE(state("el").contains("lockout"));

    //Auto left: the room no longer drives the unit.
    room("el_cur")->deliver(21.0);
    EXPECT_EQ(unitMode("el"), "cool");
}

/* Heat to cool: heat stops after min_on_time, cool starts changeover_delay
 * after that. Lower bounds only: a loaded runner can only make them longer. */
TEST_F(ProxyClimateRangeTest, AChangeoverStopsThenWaitsForTheDelay)
{
    ASSERT_TRUE(cmd("es", "auto"));
    room("es_cur")->deliver(15.0);
    ASSERT_TRUE(runLoopUntil([]()
                             {
        return unitMode("es") == "heat";
    }));

    room("es_cur")->deliver(27.0);
    ASSERT_TRUE(runLoopUntil([]()
                             {
        return unitMode("es") == "off";
    }));
    const Json lockout = state("es").at("lockout");
    EXPECT_EQ(lockout.at("output"), "cool");
    EXPECT_EQ(lockout.at("remaining"), 1);

    ASSERT_TRUE(runLoopUntil([]()
                             {
        return unitMode("es") == "cool";
    }));
    const std::vector<Write> &w = writes();
    Clock::time_point heatAt, offAt, coolAt;
    for (const Write &x: w)
    {
        if (x.id != "es_mode")
            continue;
        if (x.value == "heat")
            heatAt = x.at;
        else if (x.value == "off")
            offAt = x.at;
        else if (x.value == "cool")
            coolAt = x.at;
    }
    EXPECT_GE(offAt - heatAt, std::chrono::milliseconds(190));
    EXPECT_GE(coolAt - offAt, std::chrono::milliseconds(990));
    EXPECT_EQ(sent("es_mode"), std::vector<std::string>({ "off", "heat", "off", "cool" }));
}

TEST_F(ProxyClimateRangeTest, TheRelayedRoomSensorIsTheReference)
{
    ASSERT_TRUE(cmd("er", "auto"));
    room("er_cur")->deliver(15.0);
    EXPECT_EQ(unitMode("er"), "off") << "the unit's own sensor is not the room";
    room("er_src")->deliver(15.0);
    EXPECT_EQ(unitMode("er"), "heat");
}

TEST_F(ProxyClimateRangeTest, AModeChangedOnTheUnitSuspendsAuto)
{
    ASSERT_TRUE(cmd("em", "auto"));
    room("em_cur")->deliver(18.0);
    ASSERT_EQ(unitMode("em"), "heat");

    const std::string changed = journal([]()
                                        {
        ListeRoom::Instance().get_io("em_mode")->set_value(std::string("dry"));
    });
    EXPECT_NE(changed.find("Device(em): the unit reports dry where Calaos sent heat: its mode was changed "
                           "outside Calaos, auto is suspended until it is selected again"),
              std::string::npos)
        << changed;
    EXPECT_EQ(mode("em"), "dry");
    std::string saved;
    EXPECT_TRUE(Config::Instance().ReadValueIO("em", saved));
    EXPECT_EQ(saved, "manual heat heat");

    writes().clear();
    room("em_cur")->deliver(26.0);
    EXPECT_TRUE(writes().empty());

    //Selected again: the unit is taken back from where it is.
    ASSERT_TRUE(cmd("em", "auto"));
    EXPECT_EQ(sent("em_mode"), std::vector<std::string>({ "off", "cool" }));
}

//"<auto|manual> <last mode sent> <last of heat and cool sent>".
TEST_F(ProxyClimateRangeTest, AutoAndTheLastCommandAreSaved)
{
    std::string saved;
    ASSERT_TRUE(cmd("em", "auto"));
    EXPECT_TRUE(Config::Instance().ReadValueIO("em", saved));
    EXPECT_EQ(saved, "auto off");
    room("em_cur")->deliver(18.0);
    EXPECT_TRUE(Config::Instance().ReadValueIO("em", saved));
    EXPECT_EQ(saved, "auto heat heat");
    ASSERT_TRUE(cmd("em", "off"));
    EXPECT_TRUE(Config::Instance().ReadValueIO("em", saved));
    EXPECT_EQ(saved, "manual off heat");
}

/* The unit's setpoint is the device's while auto runs: one changed at the
 * remote is put back, and a target_temp command is refused. */
TEST_F(ProxyClimateRangeTest, InAutoTheUnitSetpointIsTheRangeOnly)
{
    ASSERT_TRUE(cmd("em", "auto"));
    room("em_cur")->deliver(18.0);
    ASSERT_DOUBLE_EQ(unitTarget("em"), 20);
    ListeRoom::Instance().get_io("em_target")->set_value(25.0);
    EXPECT_DOUBLE_EQ(unitTarget("em"), 20);

    EXPECT_FALSE(cmd("em", R"({"target_temp":22})"));
    EXPECT_DOUBLE_EQ(unitTarget("em"), 20);
    EXPECT_TRUE(cmd("em", R"({"mode":"heat","target_temp":22})"));
    EXPECT_DOUBLE_EQ(unitTarget("em"), 22);
}

//A node coming back replays its state: that is no change made at the remote.
TEST_F(ProxyClimateRangeTest, AModeReadWhileTheUnitIsDisconnectedDoesNotSuspend)
{
    ASSERT_TRUE(cmd("eh", "auto"));
    ListeRoom::Instance().get_io("eh_mode")->set_value(std::string("off"));
    pumpLoopFor(500);
    IOBase *m = ListeRoom::Instance().get_io("eh_mode");
    m->setStatusInfo(IOBase::StatusType::Connected, IOBase::StatusConnected::STATUS_DISCONNECTED);
    m->set_value(std::string("cool"));
    EXPECT_EQ(mode("eh"), "auto");
}

//Not a number is no temperature: nothing switches on it.
TEST_F(ProxyClimateRangeTest, ANonNumberReadingChangesNothing)
{
    ASSERT_TRUE(cmd("em", "auto"));
    room("em_cur")->deliver(18.0);
    ASSERT_EQ(unitMode("em"), "heat");
    room("em_cur")->deliver(std::nan(""));
    ASSERT_TRUE(cmd("em", R"({"target_temp_low":16,"target_temp_high":22})"));
    EXPECT_EQ(unitMode("em"), "heat");
}

TEST_F(ProxyClimateRangeTest, AutoIsRestoredAndGoesOnWhileTheUnitAgrees)
{
    EXPECT_EQ(mode("erst"), "auto");
    ListeRoom::Instance().get_io("erst_mode")->set_value(std::string("heat"));
    EXPECT_EQ(mode("erst"), "auto");
    room("erst_cur")->deliver(22.0);
    EXPECT_EQ(unitMode("erst"), "off");
}

//Changed at the remote while Calaos was not running.
TEST_F(ProxyClimateRangeTest, ARestoredAutoIsSuspendedWhenTheUnitSaysOtherwise)
{
    EXPECT_EQ(mode("erst2"), "auto");
    ListeRoom::Instance().get_io("erst2_mode")->set_value(std::string("cool"));
    EXPECT_EQ(mode("erst2"), "cool");
}

TEST_F(ProxyClimateRangeTest, AReadbackNotCaughtUpYetIsNoChange)
{
    ASSERT_TRUE(cmd("ed", "auto"));
    room("ed_cur")->deliver(18.0);
    EXPECT_EQ(sent("ed_cmd"), std::vector<std::string>({ "off", "heat" }));
    EXPECT_EQ(unitMode("ed"), "");

    //The unit has not caught up: still auto.
    room("ed_cur")->deliver(18.1);
    EXPECT_EQ(mode("ed"), "auto");
    ListeRoom::Instance().get_io("ed_mode")->set_value(std::string("off"));
    EXPECT_EQ(mode("ed"), "auto");
    ListeRoom::Instance().get_io("ed_mode")->set_value(std::string("heat"));
    EXPECT_EQ(mode("ed"), "auto");

    //Within readback_hold the unit may still republish what it was in.
    ListeRoom::Instance().get_io("ed_mode")->set_value(std::string("off"));
    EXPECT_EQ(mode("ed"), "auto");
}

//A setpoint read back late is not one changed at the remote: written once.
TEST_F(ProxyClimateRangeTest, ALateSetpointReadbackIsNotWrittenAgain)
{
    ASSERT_TRUE(cmd("ed", "auto"));
    room("ed_cur")->deliver(18.0);
    ASSERT_EQ(sent("ed_target_cmd").size(), 1u);
    room("ed_cur")->deliver(18.1);
    EXPECT_EQ(sent("ed_target_cmd").size(), 1u);
    //Caught up, then moved at the remote.
    ListeRoom::Instance().get_io("ed_target")->set_value(20.0);
    ListeRoom::Instance().get_io("ed_target")->set_value(23.0);
    EXPECT_EQ(sent("ed_target_cmd").size(), 1u) << "within readback_hold";
}

TEST_F(ProxyClimateRangeTest, AReadbackThatNeverComesSuspendsAfterTheHold)
{
    ASSERT_TRUE(cmd("eh", "auto"));
    ListeRoom::Instance().get_io("eh_mode")->set_value(std::string("off"));
    room("eh_cur")->deliver(18.0);
    ASSERT_EQ(sent("eh_cmd").back(), "heat");
    pumpLoopFor(1000);
    room("eh_cur")->deliver(18.1);
    EXPECT_EQ(mode("eh"), "off");
}

//No reading means no switch: the unit is left as it is, and the state says why.
TEST_F(ProxyClimateRangeTest, AStaleRoomSensorChangesNothing)
{
    ASSERT_TRUE(cmd("et", "auto"));
    EXPECT_EQ(state("et").at("fault"), "sensor_waiting");
    room("et_cur")->deliver(18.0);
    ASSERT_EQ(unitMode("et"), "heat");
    EXPECT_FALSE(state("et").contains("fault"));

    std::string silent = journal([]()
                                 {
        EXPECT_TRUE(runLoopUntil([]()
                                 {
            return state("et").value("fault", "") == "sensor_stale";
        }));
    });
    EXPECT_NE(silent.find("Device(et): current_temp 'et_cur' has sent nothing for 0.3 s, auto leaves the "
                          "unit as it is"),
              std::string::npos)
        << silent;
    ASSERT_TRUE(cmd("et", R"({"target_temp_low":16,"target_temp_high":22})"));
    EXPECT_EQ(unitMode("et"), "heat");
    EXPECT_EQ(mode("et"), "auto");

    const std::string back = journal([]()
                                     {
        room("et_cur")->deliver(18.0);
    });
    EXPECT_EQ(unitMode("et"), "off");
    EXPECT_NE(back.find("Device(et): current_temp 'et_cur' reads again, auto drives the unit again"),
              std::string::npos)
        << back;

    room("et_cur")->setStatusInfo(IOBase::StatusType::Connected, IOBase::StatusConnected::STATUS_DISCONNECTED);
    EXPECT_EQ(state("et").at("fault"), "sensor_disconnected");
    ASSERT_TRUE(cmd("et", R"({"target_temp_low":20,"target_temp_high":24})"));
    EXPECT_EQ(unitMode("et"), "off");
}

TEST_F(ProxyClimateRangeTest, AnUnreachableUnitIsNotDriven)
{
    ASSERT_TRUE(cmd("em", "auto"));
    ListeRoom::Instance().get_io("em_mode")->setStatusInfo(IOBase::StatusType::Connected,
                                                           IOBase::StatusConnected::STATUS_DISCONNECTED);
    room("em_cur")->deliver(15.0);
    EXPECT_EQ(unitMode("em"), "off");
    EXPECT_FALSE(state("em").at("connected").get<bool>());
}

TEST_F(ProxyClimateRangeTest, AUnitInAnotherModeIsStoppedWhenAutoIsSelected)
{
    ASSERT_TRUE(cmd("em", "dry"));
    writes().clear();
    room("em_cur")->deliver(22.0);
    ASSERT_TRUE(cmd("em", "auto"));
    EXPECT_EQ(sent("em_mode"), std::vector<std::string>({ "off" }));
}

TEST_F(ProxyClimateRangeTest, AMissingRangeIsCreatedWithTheBoundsOfTheUnitSetpoint)
{
    EXPECT_NE(log.find("Device(erec): target_temp_low kept by Calaos in 'erec_target_temp_low'"),
              std::string::npos)
        << log;
    IOBase *low = dev("erec")->linkIo("target_temp_low");
    IOBase *high = dev("erec")->linkIo("target_temp_high");
    ASSERT_NE(low, nullptr);
    ASSERT_NE(high, nullptr);
    EXPECT_EQ(low->get_param("type"), "InternalInt");
    EXPECT_EQ(low->get_param("save"), "true");
    EXPECT_DOUBLE_EQ(low->get_value_double(), 19);
    EXPECT_DOUBLE_EQ(high->get_value_double(), 27);
    EXPECT_EQ(high->get_param("value_min"), "17");
    EXPECT_EQ(high->get_param("value_max"), "28");
    EXPECT_EQ(high->get_param("step"), "1");
    EXPECT_TRUE(caps("erec").at("target_temp_range").get<bool>());
}

TEST_F(ProxyClimateRangeTest, MistakesAreLoggedAtLoad)
{
    for (const char *expected: {
             "Device(bad_target): emulate_range needs a target_temp link that can be written, no emulated auto",
             "Device(bad_modes): emulate_range needs heat and cool among the modes, no emulated auto",
             "Device(bad_flag): emulate_range must be 'true' or 'false', no emulated auto",
             "Device(bad_range): emulate_range needs target_temp_low and target_temp_high that can be written",
             "Device(enoref): emulate_range without remote_temp_source or current_temp, auto never switches "
             "the unit",
         })
        EXPECT_NE(log.find(expected), std::string::npos) << expected << "\n"
                                                         << log;
    for (const char *quiet: { "em", "el", "er", "off_flag", "plain" })
        EXPECT_EQ(log.find(std::string("Device(") + quiet + "): "), std::string::npos) << quiet << "\n"
                                                                                       << log;
}

TEST_F(ProxyClimateRangeTest, WithoutARoomSensorAutoWaitsForOne)
{
    ASSERT_TRUE(cmd("enoref", "auto"));
    EXPECT_EQ(state("enoref").at("fault"), "sensor_missing");
}

TEST_F(ProxyClimateRangeTest, ARefusedCommandIsLogged)
{
    std::string refused = journal([]()
                                  {
        EXPECT_FALSE(cmd("erm", "auto"));
    });
    EXPECT_NE(refused.find("Device(erm): the mode link refused off"), std::string::npos) << refused;
    EXPECT_NE(mode("erm"), "auto");

    //A setpoint refused does not keep the unit from running.
    ASSERT_TRUE(cmd("erf", "auto"));
    refused = journal([]()
                      {
        room("erf_cur")->deliver(15.0);
    });
    EXPECT_NE(refused.find("Device(erf): target_temp refused 20"), std::string::npos) << refused;
    EXPECT_EQ(unitMode("erf"), "heat");
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
