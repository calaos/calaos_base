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

//Reversible floor zones (decision 22): one valve for heat and cool, the modes
//param, floor cooling with and without a humidity sensor.

#include "CalaosCoreFixture.h"
#include "IOFactory.h"
#include "InternalThermostat.h"
#include "libuvw.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

using Clock = std::chrono::steady_clock;

int msSince(Clock::time_point t0)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
}

bool pumpUntil(const std::function<bool()> &done, int ms = 5000)
{
    auto loop = uvw::Loop::getDefault();
    const auto t0 = Clock::now();
    while (!done())
    {
        if (msSince(t0) > ms)
            return false;
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

void pumpFor(int ms)
{
    auto never = []()
    {
        return false;
    };
    pumpUntil(never, ms);
}

std::string link(const std::string &name, const std::string &io)
{
    return "      <calaos:link name=\"" + name + "\" io=\"" + io + "\" />\n";
}

std::string param(const std::string &name, const std::string &value)
{
    return "      <calaos:param name=\"" + name + "\" value=\"" + value + "\" />\n";
}

std::string internal(const std::string &type, const std::string &id, const std::string &indent = "      ",
                     const std::string &extra = std::string())
{
    return indent + "<calaos:internal type=\"" + type + "\" id=\"" + id + "\" name=\"" + id +
           "\" visible=\"false\"" + extra + " />\n";
}

const char *const SAVED = R"( save="true")";

std::string device(const std::string &id, const std::string &inner)
{
    return "    <calaos:device id=\"" + id + "\" type=\"climate\" impl=\"thermostat\" name=\"" + id +
           "\">\n" + inner + "    </calaos:device>\n";
}

/* <id>: owned setpoint and action, sensor <id>_temp and valve <id>_valve in
 * the room. With `range`, owned low and high setpoints too. */
std::string zone(const std::string &id, const std::string &params, bool range = false)
{
    std::string inner = internal("InternalInt", id + "_target", "      ", SAVED) +
                        internal("InternalString", id + "_action") + link("current_temp", id + "_temp") +
                        link("target_temp", id + "_target") + link("action", id + "_action") +
                        link("output", id + "_valve");
    if (range)
        inner += internal("InternalInt", id + "_low", "      ", SAVED) +
                 internal("InternalInt", id + "_high", "      ", SAVED) + link("target_temp_low", id + "_low") +
                 link("target_temp_high", id + "_high");
    return device(id, inner + params) + internal("InternalInt", id + "_temp", "    ") +
           internal("InternalBool", id + "_valve", "    ");
}

//A pair of outputs, as before decision 22.
std::string pair(const std::string &id, const std::string &params, bool withCool)
{
    std::string inner = internal("InternalInt", id + "_target", "      ", SAVED) +
                        link("current_temp", id + "_temp") + link("target_temp", id + "_target") +
                        link("output_heat", id + "_heat");
    if (withCool)
        inner += link("output_cool", id + "_cool");
    std::string ios = device(id, inner + params) + internal("InternalInt", id + "_temp", "    ") +
                      internal("InternalBool", id + "_heat", "    ");
    if (withCool)
        ios += internal("InternalBool", id + "_cool", "    ");
    return ios;
}

const std::string FLOOR = param("emitter", "floor");
const std::string REVERSIBLE = param("modes", "off,heat,cool");

std::string house()
{
    std::string ios;
    //Every lock-out at 0, no humidity sensor.
    ios += zone("zone", FLOOR + REVERSIBLE + param("put_back_delay", "0.01") + param("put_back_delay_max", "0.1"));
    ios += zone("zauto", param("modes", "heat,cool,auto"), true);
    ios += zone("zlock", REVERSIBLE + param("min_on_time", "0.2") + param("min_off_time", "0.3"));
    //Cut at 65 %, without waiting for a min_on_time.
    ios += zone("zhum", FLOOR + REVERSIBLE + link("humidity", "zhum_hum") + param("humidity_max", "65") +
                            param("min_on_time", "5"));
    ios += internal("InternalInt", "zhum_hum", "    ");
    //A humidity link whose IO does not exist.
    ios += zone("zghost", FLOOR + REVERSIBLE + link("humidity", "nowhere"));
    ios += zone("zdef", "");
    ios += zone("zbad", param("modes", "heat, cool,dry,bogus"));
    //auto without cool: it would cool anyway.
    ios += zone("zhalf", param("modes", "heat,auto"), true);
    //Auto on a floor, its dew point guarded.
    const std::string dewGuard = link("humidity", "zauhum_hum") + param("humidity_max", "65");
    ios += zone("zauhum", FLOOR + param("modes", "heat,cool,auto") + dewGuard + param("min_on_time", "5"), true);
    ios += internal("InternalInt", "zauhum_hum", "    ");
    ios += zone("zempty", param("modes", ""));
    ios += pair("pairheat", param("modes", "off,heat"), true);
    ios += pair("pairnocool", param("modes", "heat,cool"), false);
    //A range linked, but no auto to use it.
    ios += pair("pairnoauto",
                param("modes", "heat,cool") + internal("InternalInt", "pairnoauto_low", "      ", SAVED) +
                    internal("InternalInt", "pairnoauto_high", "      ", SAVED) +
                    link("target_temp_low", "pairnoauto_low") + link("target_temp_high", "pairnoauto_high"),
                true);

    //output with output_heat: a configuration error.
    ios += device("mix", internal("InternalInt", "mix_target", "      ", SAVED) + link("current_temp", "mix_temp") +
                             link("target_temp", "mix_target") + link("output_heat", "mix_heat") +
                             link("output", "mix_valve") + REVERSIBLE);
    ios += internal("InternalInt", "mix_temp", "    ") + internal("InternalBool", "mix_heat", "    ") +
           internal("InternalBool", "mix_valve", "    ");

    //Decision 17 applies to the valve: the first one loaded keeps it.
    for (const std::string id: { "own_a", "own_b" })
        ios += device(id, internal("InternalInt", id + "_target", "      ", SAVED) +
                              link("current_temp", "own_temp") + link("target_temp", id + "_target") +
                              link("output", "own_valve") + REVERSIBLE);
    ios += device("own_c", internal("InternalInt", "own_c_target", "      ", SAVED) +
                               link("current_temp", "own_temp") + link("target_temp", "own_c_target") +
                               link("output_heat", "own_valve"));
    ios += internal("InternalInt", "own_temp", "    ") + internal("InternalBool", "own_valve", "    ");
    return ioXmlDocument(roomXml("Salon", "salon", ios));
}

Json thermostatDocumentation()
{
    Params p;
    p.Add("type", "device:thermostat");
    p.Add("id", "doc");
    IOBase::ScopedDocGen docScope;
    std::unique_ptr<IOBase> io(IOFactory::Instance().CreateIO("device:thermostat", p));
    return io->getDoc()->genDocJson();
}

std::vector<std::string> splitList(const std::string &list)
{
    std::vector<std::string> out;
    std::stringstream ss(list);
    for (std::string item; std::getline(ss, item, ',');)
    {
        item.erase(0, item.find_first_not_of(' '));
        item.erase(item.find_last_not_of(' ') + 1);
        if (!item.empty())
            out.push_back(item);
    }
    return out;
}

bool listHas(const std::vector<std::string> &l, const std::string &v)
{
    return std::find(l.begin(), l.end(), v) != l.end();
}

const std::vector<std::string> DEVICES = { "zone", "zauto", "zlock", "zhum", "zghost", "zhalf", "zauhum", "zdef", "zbad", "zempty", "pairheat", "pairnocool", "pairnoauto", "mix", "own_a", "own_b", "own_c" };

} //namespace

class InternalThermostatValveTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        //Target 21, range 19..23, mode off: nothing leaks from the previous case.
        Config &c = Config::Instance();
        for (const std::string &id: DEVICES)
        {
            forgetIOState(id);
            c.SaveValueIO(id + "_target", "21", false);
            c.SaveValueIO(id + "_low", "19", false);
            c.SaveValueIO(id + "_high", "23", false);
        }
    }

    void TearDown() override
    {
        CoreFixture::TearDown();
        pumpFor(5);
    }

    std::string load()
    {
        return journal([this]()
                       {
            loadConfig(house(), rulesXmlDocument(""));
            pumpFor(20);
        });
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

    static bool on(const std::string &id) { return must(id) && io(id)->get_value_bool(); }
    static std::string str(const std::string &id) { return must(id) ? io(id)->get_value_string() : "<none>"; }
    static bool mode(const std::string &id, const std::string &m) { return dev(id)->set_value(m); }
    static Json state(const std::string &id) { return dev(id)->deviceObject().at("state"); }
    static Json caps(const std::string &id) { return dev(id)->deviceObject().at("capabilities"); }

    static void set(const std::string &id, double v)
    {
        if (must(id))
            io(id)->set_value(v);
    }

    static std::vector<std::string> modes(const std::string &id)
    {
        std::vector<std::string> res;
        for (ClimateMode m: dev(id)->supportedModes())
            res.push_back(ClimateBase::modeName(m));
        return res;
    }

    static bool waitFor(const std::string &id, bool v)
    {
        return pumpUntil([&]()
                         {
            return on(id) == v;
        });
    }

    template <class F>
    static std::string journal(F fn)
    {
        EXPECT_TRUE(Utils::calaosLogger("device")->isLevelEnabled(Logger::LOG_LEVEL_WARNING))
            << "the device domain is muted, the journal cannot be observed";
        testing::internal::CaptureStdout();
        fn();
        return testing::internal::GetCapturedStdout();
    }

    static bool has(const std::string &log, const std::string &needle) { return log.find(needle) != std::string::npos; }
};

/* Target 21, hysteresis 0.5, range 19..23, every lock-out at 0. `start` is
 * the way the valve runs before the mode is set and the temperature moves; a
 * change of way closes then opens it again in the same pass. */
TEST_F(InternalThermostatValveTest, ValveTruthTable)
{
    load();

    struct Row
    {
        const char *id;
        const char *mode;
        const char *start;
        double temp;
        bool valve;
        const char *action;
    };
    const std::vector<Row> rows = {
        { "zone", "heat", "none", 20.0, true, "heating" },
        { "zone", "heat", "none", 20.8, false, "idle" },
        { "zone", "heat", "none", 21.5, false, "idle" },
        { "zone", "heat", "heat", 20.8, true, "heating" },
        { "zone", "heat", "heat", 21.0, true, "heating" },
        { "zone", "heat", "heat", 21.2, false, "idle" },
        { "zone", "heat", "cool", 20.0, true, "heating" },
        { "zone", "heat", "cool", 20.8, false, "idle" },
        { "zone", "heat", "cool", 25.0, false, "idle" },

        { "zone", "cool", "none", 22.0, true, "cooling" },
        { "zone", "cool", "none", 21.2, false, "idle" },
        { "zone", "cool", "cool", 21.2, true, "cooling" },
        { "zone", "cool", "cool", 21.0, true, "cooling" },
        { "zone", "cool", "cool", 20.8, false, "idle" },
        { "zone", "cool", "heat", 22.0, true, "cooling" },
        { "zone", "cool", "heat", 21.2, false, "idle" },
        { "zone", "cool", "heat", 15.0, false, "idle" },

        { "zone", "off", "heat", 10.0, false, "off" },
        { "zone", "off", "cool", 30.0, false, "off" },

        { "zauto", "auto", "none", 18.0, true, "heating" },
        { "zauto", "auto", "none", 21.0, false, "idle" },
        { "zauto", "auto", "none", 24.0, true, "cooling" },
        { "zauto", "auto", "heat", 19.0, true, "heating" },
        { "zauto", "auto", "heat", 24.0, true, "cooling" },
        { "zauto", "auto", "cool", 23.0, true, "cooling" },
        { "zauto", "auto", "cool", 18.0, true, "heating" },
    };

    for (const Row &r: rows)
    {
        const std::string id = r.id;
        const std::string label = id + " " + r.mode + " from " + r.start + " at " + Utils::to_string(r.temp);
        mode(id, "off");
        ASSERT_FALSE(on(id + "_valve")) << label;
        if (std::string(r.start) == "heat")
        {
            mode(id, "heat");
            set(id + "_temp", 10);
            ASSERT_EQ(str(id + "_action"), "heating") << label;
        }
        else if (std::string(r.start) == "cool")
        {
            mode(id, "cool");
            set(id + "_temp", 30);
            ASSERT_EQ(str(id + "_action"), "cooling") << label;
        }
        else
            set(id + "_temp", r.temp);

        mode(id, r.mode);
        set(id + "_temp", r.temp);
        EXPECT_EQ(on(id + "_valve"), r.valve) << label;
        EXPECT_EQ(str(id + "_action"), r.action) << label;
        EXPECT_EQ(state(id).value("action", ""), r.action) << label;
    }
}

/* Changing way is a changeover: the valve closes after its min_on_time and
 * opens again after its min_off_time, the action following the valve. */
TEST_F(InternalThermostatValveTest, ADirectionChangeWaitsForTheLockOuts)
{
    load();
    //The start counts as a switch OFF: past its min_off_time.
    pumpFor(320);
    mode("zlock", "heat");
    set("zlock_temp", 18);
    ASSERT_TRUE(on("zlock_valve"));
    const auto t0 = Clock::now();

    mode("zlock", "cool");
    set("zlock_temp", 25);
    EXPECT_TRUE(on("zlock_valve"));
    EXPECT_EQ(str("zlock_action"), "heating");

    ASSERT_TRUE(waitFor("zlock_valve", false));
    EXPECT_GE(msSince(t0), 198);
    const auto closed = Clock::now();
    EXPECT_EQ(str("zlock_action"), "idle");
    //Published while it waits, in the way it will run.
    EXPECT_EQ(state("zlock").at("lockout").at("output"), "cool");

    ASSERT_TRUE(waitFor("zlock_valve", true));
    EXPECT_GE(msSince(closed), 298);
    EXPECT_EQ(str("zlock_action"), "cooling");
    EXPECT_FALSE(state("zlock").contains("lockout"));
}

//Off closes the valve whatever the way, without waiting for min_on_time.
TEST_F(InternalThermostatValveTest, OffClosesTheValveAtOnce)
{
    load();
    pumpFor(320);
    mode("zlock", "cool");
    set("zlock_temp", 25);
    ASSERT_TRUE(on("zlock_valve"));

    mode("zlock", "off");
    EXPECT_FALSE(on("zlock_valve"));
    EXPECT_EQ(str("zlock_action"), "off");
}

TEST_F(InternalThermostatValveTest, TheModesParamSetsTheModesAndTheCapabilities)
{
    const std::string log = load();

    EXPECT_EQ(modes("zone"), (std::vector<std::string>{ "off", "heat", "cool" }));
    EXPECT_EQ(caps("zone").at("modes"), Json({ "off", "heat", "cool" }));
    EXPECT_FALSE(caps("zone").contains("target_temp_range"));
    EXPECT_FALSE(mode("zone", "auto"));
    EXPECT_FALSE(dev("zone")->set_value(std::string(R"({"mode":"auto"})")));
    EXPECT_EQ(dev("zone")->get_value_string(), "off");
    EXPECT_TRUE(mode("zone", "cool"));

    //off is always offered, listed or not.
    EXPECT_EQ(caps("zauto").at("modes"), Json({ "off", "heat", "cool", "auto" }));
    EXPECT_TRUE(caps("zauto").contains("target_temp_range"));

    //Nothing says which way a valve runs: heat only.
    EXPECT_EQ(modes("zdef"), (std::vector<std::string>{ "off", "heat" }));
    EXPECT_FALSE(mode("zdef", "cool"));
    EXPECT_TRUE(has(log, "Device(zdef): output without a modes param, it only heats")) << log;

    EXPECT_EQ(modes("zbad"), (std::vector<std::string>{ "off", "heat", "cool" }));
    EXPECT_TRUE(has(log, "Device(zbad): mode 'dry' in the modes param is not a thermostat mode (off, heat, cool, "
                         "auto), ignored"))
        << log;
    EXPECT_TRUE(has(log, "Device(zbad): mode 'bogus' in the modes param is not a thermostat mode")) << log;

    EXPECT_EQ(modes("zempty"), (std::vector<std::string>{ "off" }));
    EXPECT_TRUE(has(log, "Device(zempty): empty modes param, only off can be selected")) << log;

    //The param narrows a pair of outputs too, and the range goes with auto.
    EXPECT_EQ(modes("pairheat"), (std::vector<std::string>{ "off", "heat" }));
    EXPECT_FALSE(mode("pairheat", "cool"));
    EXPECT_FALSE(caps("pairheat").contains("target_temp_range"));
    EXPECT_EQ(io("pairheat_target_temp_low"), nullptr);
    EXPECT_FALSE(has(log, "Device(pairheat): mode")) << log;

    EXPECT_FALSE(caps("pairnoauto").contains("target_temp_range"));
    EXPECT_FALSE(dev("pairnoauto")->set_value(std::string(R"({"target_temp_low":18,"target_temp_high":24})")));
    EXPECT_EQ(io("pairnoauto_low")->get_value_double(), 19);

    //It never adds a mode the outputs cannot run.
    EXPECT_EQ(modes("pairnocool"), (std::vector<std::string>{ "off", "heat" }));
    EXPECT_TRUE(has(log, "Device(pairnocool): mode 'cool' in the modes param has no output to run it, ignored"))
        << log;
}

//A mode saved before the modes param narrowed it does not come back.
TEST_F(InternalThermostatValveTest, ASavedModeOutsideTheModesParamIsOff)
{
    Config::Instance().SaveValueIO("pairheat", "cool", false);
    const std::string log = load();
    EXPECT_EQ(dev("pairheat")->get_value_string(), "off");
    EXPECT_TRUE(has(log, "Device(pairheat): saved mode #")) << log;
}

//Decision 22: without a humidity sensor, a floor cools; the condensation is
//the installation's business.
TEST_F(InternalThermostatValveTest, AFloorCoolsWithoutAHumiditySensor)
{
    load();
    mode("zone", "cool");
    set("zone_temp", 25);
    EXPECT_TRUE(on("zone_valve"));
    EXPECT_EQ(str("zone_action"), "cooling");
    EXPECT_FALSE(state("zone").contains("fault")) << state("zone").dump();
}

//With one, the dew-point cut stops the cooling at once and leaves heating alone.
TEST_F(InternalThermostatValveTest, AHumiditySensorCutsTheCooling)
{
    load();
    set("zhum_hum", 50);
    mode("zhum", "cool");
    set("zhum_temp", 25);
    ASSERT_TRUE(on("zhum_valve"));

    set("zhum_hum", 70);
    EXPECT_FALSE(on("zhum_valve"));
    EXPECT_EQ(state("zhum").value("fault", ""), "humidity_high");

    set("zhum_hum", 60);
    EXPECT_TRUE(on("zhum_valve"));

    set("zhum_hum", 70);
    ASSERT_FALSE(on("zhum_valve"));
    mode("zhum", "heat");
    set("zhum_temp", 18);
    EXPECT_TRUE(on("zhum_valve"));
    EXPECT_EQ(str("zhum_action"), "heating");
}

//A mistyped humidity IO must not lift the cut.
/* Heating, then cool asked with the humidity too high: the heat pump follows
 * the house mode and sends cold water, so the valve closes at once instead of
 * waiting for its min_on_time. */
TEST_F(InternalThermostatValveTest, TheHumidityCutClosesAValveHeldForADirectionChange)
{
    load();
    set("zhum_hum", 70);
    mode("zhum", "heat");
    set("zhum_temp", 18);
    ASSERT_TRUE(on("zhum_valve"));

    mode("zhum", "cool");
    set("zhum_temp", 25);
    EXPECT_FALSE(on("zhum_valve"));
    EXPECT_EQ(state("zhum").value("fault", ""), "humidity_high");
}

//The same in auto, once auto decides to cool.
TEST_F(InternalThermostatValveTest, TheHumidityCutClosesAValveWhenAutoTurnsToCooling)
{
    load();
    set("zauhum_hum", 70);
    mode("zauhum", "auto");
    set("zauhum_temp", 18);
    ASSERT_TRUE(on("zauhum_valve"));

    set("zauhum_temp", 25);
    EXPECT_FALSE(on("zauhum_valve"));
    EXPECT_EQ(state("zauhum").value("fault", ""), "humidity_high");
}

//An unresolved humidity link still cuts: no warning that nothing does.
TEST_F(InternalThermostatValveTest, AFloorWithoutHumiditySensorIsWarnedAtLoad)
{
    const std::string log = load();
    const std::string line = "Device(zone): emitter=floor without a humidity link, cooling has no dew-point cut";
    ASSERT_TRUE(has(log, line)) << log;
    //A warning, not an info: shown on a stock install.
    const size_t start = log.rfind('\n', log.find(line));
    EXPECT_NE(log.substr(start == std::string::npos ? 0 : start, log.find(line) - start).find("[WRN]"),
              std::string::npos)
        << log;
    EXPECT_FALSE(has(log, "Device(zghost): emitter=floor without a humidity link")) << log;
    EXPECT_FALSE(has(log, "Device(zhum): emitter=floor without a humidity link")) << log;
}

//auto cools as much as it heats: never offered without cool.
TEST_F(InternalThermostatValveTest, AutoNeedsHeatAndCool)
{
    const std::string log = load();
    EXPECT_EQ(modes("zhalf"), (std::vector<std::string>{ "off", "heat" }));
    EXPECT_FALSE(mode("zhalf", "auto"));
    EXPECT_FALSE(caps("zhalf").contains("target_temp_range"));
    EXPECT_TRUE(has(log, "Device(zhalf): mode 'auto' in the modes param needs heat and cool, ignored")) << log;
}

TEST_F(InternalThermostatValveTest, AHumidityLinkThatDoesNotResolveCutsTheCooling)
{
    load();
    mode("zghost", "cool");
    set("zghost_temp", 25);
    pumpFor(20);
    EXPECT_FALSE(on("zghost_valve"));
    EXPECT_EQ(state("zghost").value("fault", ""), "humidity_stale");
}

TEST_F(InternalThermostatValveTest, AValveWithAPairOfOutputsIsIgnoredAtLoad)
{
    const std::string log = load();
    EXPECT_TRUE(has(log, "Device(mix): output 'mix_valve' cannot be linked with output_heat or output_cool, the "
                         "output link is ignored"))
        << log;
    EXPECT_FALSE(dev("mix")->deviceObject().at("ios").contains("output"));
    EXPECT_EQ(modes("mix"), (std::vector<std::string>{ "off", "heat" }));

    mode("mix", "heat");
    set("mix_temp", 18);
    EXPECT_TRUE(on("mix_heat"));
    EXPECT_FALSE(on("mix_valve"));
}

//Decision 17: a valve, like any output, belongs to one thermostat.
TEST_F(InternalThermostatValveTest, AValveBelongsToOneThermostat)
{
    const std::string log = load();
    EXPECT_TRUE(has(log, "Device(own_b): output 'own_valve' is already an output of thermostat 'own_a', this "
                         "link is ignored"))
        << log;
    EXPECT_TRUE(has(log, "Device(own_c): output_heat 'own_valve' is already an output of thermostat 'own_a'"))
        << log;
    EXPECT_FALSE(has(log, "Device(own_a): output")) << log;
    EXPECT_EQ(modes("own_b"), (std::vector<std::string>{ "off" }));

    mode("own_a", "heat");
    set("own_temp", 18);
    EXPECT_TRUE(on("own_valve"));
}

//Spec §8.3: a write from elsewhere is warned about, applied, then taken back.
TEST_F(InternalThermostatValveTest, TheValveIsAProtectedOutput)
{
    load();
    mode("zone", "heat");
    set("zone_temp", 18);
    ASSERT_TRUE(on("zone_valve"));

    const std::string log = journal([]()
                                    {
        InternalThermostat::warnIfProtected(*io("zone_valve"), "a test");
        io("zone_valve")->set_value(false);
    });
    EXPECT_TRUE(has(log, "Device(zone): output 'zone_valve' is driven by the thermostat and was written by a test;"))
        << log;
    EXPECT_FALSE(on("zone_valve"));
    EXPECT_TRUE(waitFor("zone_valve", true));
    EXPECT_EQ(str("zone_action"), "heating");
}

//Opened by something else while the room asks for it: the valve is kept, in
//the way the mode runs.
TEST_F(InternalThermostatValveTest, AValveOpenedElsewhereIsAdopted)
{
    load();
    mode("zone", "cool");
    set("zone_temp", 21.2);
    ASSERT_FALSE(on("zone_valve"));

    io("zone_valve")->set_value(true);
    set("zone_temp", 22);
    pumpFor(20);
    EXPECT_TRUE(on("zone_valve"));
    EXPECT_EQ(str("zone_action"), "cooling");
}

/* Outside a rule march (a command here), a valve write runs the rules at
 * once, and one of them may change an input in the middle of a pass: the pass
 * starts over against it. Here, when the valve opens. */
TEST_F(InternalThermostatValveTest, ARuleReactingToTheValveRestartsTheEvaluation)
{
    load();
    set("zone_temp", 18);
    ASSERT_NE(addSimpleRule("open_lowers_target", "zone_valve", "==", "true", "zone_target", "10"), nullptr);

    mode("zone", "heat");
    EXPECT_EQ(io("zone_target")->get_value_double(), 10);
    EXPECT_FALSE(on("zone_valve"));
    EXPECT_EQ(str("zone_action"), "idle");
}

//And when it closes to change way.
TEST_F(InternalThermostatValveTest, ARuleReactingToADirectionChangeRestartsTheEvaluation)
{
    load();
    mode("zauto", "cool");
    set("zauto_temp", 25);
    ASSERT_EQ(str("zauto_action"), "cooling");
    ASSERT_NE(addSimpleRule("close_lowers_target", "zauto_valve", "==", "false", "zauto_target", "10"), nullptr);

    EXPECT_TRUE(mode("zauto", R"({"mode":"heat","target_temp":30})"));
    EXPECT_EQ(io("zauto_target")->get_value_double(), 10);
    EXPECT_FALSE(on("zauto_valve"));
    EXPECT_EQ(str("zauto_action"), "idle");
}

/* A client derives the modes of an instance from the document alone: the rule
 * published on the modes param, and the links each mode needs. It has to land
 * on what the server offers, for every way of listing modes or not. */
TEST_F(InternalThermostatValveTest, TheModesTheDocumentSaysAreThoseTheServerOffers)
{
    load();
    const Json doc = thermostatDocumentation();

    Json rule;
    for (const Json &p: doc.at("parameters"))
    {
        if (p.at("name") == "modes")
            rule = p.at("list_rule");
    }
    ASSERT_FALSE(rule.is_null());
    Json field;
    for (const Json &f: doc.at("command_fields"))
    {
        if (f.at("name") == "mode")
            field = f;
    }

    //A requirement on a replaced role is met by the role that replaces it.
    auto linked = [&doc](InternalThermostat *d, const std::string &role)
    {
        if (d->linkIo(role))
            return true;
        for (const Json &r: doc.at("link_roles"))
        {
            for (const Json &replaced: r.value("replaces", Json::array()))
            {
                if (replaced == role && d->linkIo(r.at("role").get<std::string>()))
                    return true;
            }
        }
        return false;
    };

    int checked = 0;
    for (const std::string &id: { "zone", "zauto", "zlock", "zhum", "zghost", "zdef", "zbad", "zhalf", "zauhum",
                                  "zempty", "pairheat", "pairnocool", "pairnoauto" })
    {
        InternalThermostat *d = dev(id);
        ASSERT_NE(d, nullptr) << id;

        std::vector<std::string> list;
        bool narrowed = false;
        if (d->hasDeviceParam("modes"))
        {
            list = splitList(d->getDeviceParam("modes"));
            narrowed = true;
        }
        else
        {
            for (const auto &it: rule.at("default_when_linked").items())
            {
                if (d->linkIo(it.key()))
                {
                    list = it.value().get<std::vector<std::string>>();
                    narrowed = true;
                }
            }
        }
        for (const Json &always: rule.at("always_includes"))
            list.push_back(always.get<std::string>());

        std::vector<std::string> offered;
        for (const Json &v: field.at("values"))
        {
            const std::string name = v.get<std::string>();
            bool ok = !narrowed || listHas(list, name);
            for (const Json &role: field.at("value_requires_links").value(name, Json::array()))
                ok = ok && linked(d, role.get<std::string>());
            if (ok)
                offered.push_back(name);
        }
        std::vector<std::string> final;
        for (const std::string &name: offered)
        {
            bool ok = true;
            for (const Json &need: rule.at("value_requires_values").value(name, Json::array()))
                ok = ok && listHas(offered, need.get<std::string>());
            if (ok)
                final.push_back(name);
        }
        EXPECT_EQ(modes(id), final) << id;
        checked++;
    }
    EXPECT_EQ(13, checked);
}
