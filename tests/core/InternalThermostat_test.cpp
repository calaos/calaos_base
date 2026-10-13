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

//InternalThermostat (spec §8.1): evaluate() for every mode, temperature region
//and hysteresis edge, the lock-outs and the changeover on the real libuv loop,
//the owned IOs recreated at load and the mode kept in the state cache.

#include "CalaosCoreFixture.h"
#include "IOChangeHub.h"
#include "InternalThermostat.h"
#include "JsonApi.h"
#include "libuvw.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

//An input: every write is refused.
class TestReadOnly: public IOBase
{
public:
    TestReadOnly(Params &p):
        IOBase(p, IOBase::IO_INPUT)
    {
    }

    DATA_TYPE get_type() override { return TBOOL; }
};

//An output whose read-back follows its commands only when told.
class TestLaggingOutput: public IOBase
{
public:
    TestLaggingOutput(Params &p):
        IOBase(p, IOBase::IO_OUTPUT)
    {
    }

    DATA_TYPE get_type() override { return TBOOL; }
    bool get_value_bool() override { return state; }

    bool set_value(bool v) override
    {
        commands.push_back(v);
        return true;
    }

    void confirm()
    {
        state = commands.back();
        EmitSignalIO();
    }

    std::vector<bool> commands;
    bool state = false;
};

//A safety override that can refuse heat. It does not call the base, as an
//override may forget to.
class TestSafeThermostat: public InternalThermostat
{
public:
    TestSafeThermostat(Params &p):
        InternalThermostat(p)
    {
    }

    bool heatAllowed = true;

protected:
    OutputPair outputsAllowed() override { return OutputPair{ heatAllowed, true }; }
};

REGISTER_IO(TestReadOnly)
REGISTER_IO(TestLaggingOutput)
REGISTER_DEVICE(test_safe_thermostat, TestSafeThermostat)

namespace
{

using Clock = std::chrono::steady_clock;

int msSince(Clock::time_point t0)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
}

//Runs the real loop until `done` holds; false after `ms` without it.
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

std::string thermostatXml(const std::string &id, const std::string &inner)
{
    return "    <calaos:device id=\"" + id + "\" type=\"climate\" impl=\"thermostat\" name=\"" +
           id + "\">\n" + inner + "    </calaos:device>\n";
}

std::string roomIo(const std::string &type, const std::string &id,
                   const std::string &extra = std::string())
{
    return "    <calaos:internal type=\"" + type + "\" id=\"" + id + "\" name=\"" + id + "\"" +
           extra + " />\n";
}

const char *const SAVED = R"( save="true")";

/* <id>: setpoints, action and outputs owned, current temp <id>_temp in the
 * room. With `cool`, a range and an output_cool. */
std::string fullThermostat(const std::string &id, bool cool, const std::string &params,
                           bool ownOutputs = true)
{
    std::string inner = ownedXml("InternalInt", id + "_target", SAVED) +
                        ownedXml("InternalString", id + "_action") +
                        linkXml("current_temp", id + "_temp") +
                        linkXml("target_temp", id + "_target") +
                        linkXml("action", id + "_action") + linkXml("output_heat", id + "_heat");
    if (ownOutputs)
        inner += ownedXml("InternalBool", id + "_heat");
    if (cool)
    {
        inner += ownedXml("InternalInt", id + "_low", SAVED) +
                 ownedXml("InternalInt", id + "_high", SAVED) +
                 linkXml("target_temp_low", id + "_low") +
                 linkXml("target_temp_high", id + "_high") + linkXml("output_cool", id + "_cool");
        if (ownOutputs)
            inner += ownedXml("InternalBool", id + "_cool");
    }
    return thermostatXml(id, inner + params) + roomIo("InternalInt", id + "_temp");
}

const std::vector<std::string> THERMOSTATS = { "th", "rev", "lock", "lag", "bare", "dangling", "taken", "novalue", "nosp", "nosensor", "coolonly", "refuse", "badparams", "safe", "slow" };

std::string house()
{
    std::string ios;
    ios += fullThermostat("th", false, paramXml("hysteresis", "0.5"));
    ios += fullThermostat("rev", true, paramXml("hysteresis", "0.5") + paramXml("min_range", "2"));
    //Durations at ms granularity, on outputs the device does not own.
    ios += fullThermostat("lock", true,
                          paramXml("min_on_time", "0.2") + paramXml("min_off_time", "0.3"), false);
    ios += roomIo("InternalBool", "lock_heat") + roomIo("InternalBool", "lock_cool");
    std::string safe = fullThermostat("safe", true, paramXml("min_on_time", "0.2") + paramXml("min_off_time", "0.3"));
    safe.replace(safe.find("impl=\"thermostat\""), 17, "impl=\"test_safe_thermostat\"");
    ios += safe;
    ios += fullThermostat("slow", false, paramXml("min_off_time", "1"));

    ios += thermostatXml("lag", linkXml("current_temp", "lag_temp") +
                                    ownedXml("InternalInt", "lag_target", SAVED) +
                                    linkXml("target_temp", "lag_target") +
                                    linkXml("output_heat", "lag_heat") +
                                    linkXml("output_cool", "lag_cool") +
                                    paramXml("min_on_time", "0.2"));
    ios += roomIo("InternalInt", "lag_temp");
    ios += "    <calaos:output type=\"TestLaggingOutput\" id=\"lag_heat\" name=\"lag_heat\" />\n";
    ios += "    <calaos:output type=\"TestLaggingOutput\" id=\"lag_cool\" name=\"lag_cool\" />\n";

    //Only the outputs and the sensor: everything else is recreated.
    ios += thermostatXml("bare", ownedXml("InternalBool", "bare_heat") +
                                     ownedXml("InternalBool", "bare_cool") +
                                     linkXml("current_temp", "bare_temp") +
                                     linkXml("output_heat", "bare_heat") +
                                     linkXml("output_cool", "bare_cool") +
                                     paramXml("min_range", "7"));
    ios += roomIo("InternalInt", "bare_temp");

    //A link naming a missing IO, and a link naming none.
    ios += thermostatXml("dangling", ownedXml("InternalBool", "dangling_heat") +
                                         linkXml("output_heat", "dangling_heat") +
                                         linkXml("current_temp", "dangling_temp") +
                                         linkXml("target_temp", "dangling_sp") +
                                         linkXml("action", ""));
    ios += roomIo("InternalInt", "dangling_temp");

    //The conventional id is held by an unrelated IO.
    ios += thermostatXml("taken", ownedXml("InternalBool", "taken_heat") +
                                      ownedXml("InternalString", "taken_action") +
                                      linkXml("output_heat", "taken_heat") +
                                      linkXml("action", "taken_action") +
                                      linkXml("current_temp", "taken_temp"));
    ios += roomIo("InternalInt", "taken_temp") + roomIo("InternalInt", "taken_target_temp");

    //A sensor never read, and an output left ON by the previous run.
    ios += fullThermostat("novalue", false, "", false);
    ios += roomIo("InternalBool", "novalue_heat", SAVED);

    //A setpoint not saved: unknown until written.
    ios += thermostatXml("nosp", ownedXml("InternalInt", "nosp_target") +
                                     ownedXml("InternalBool", "nosp_heat") +
                                     ownedXml("InternalBool", "nosp_cool") +
                                     linkXml("current_temp", "nosp_temp") +
                                     linkXml("target_temp", "nosp_target") +
                                     linkXml("output_heat", "nosp_heat") +
                                     linkXml("output_cool", "nosp_cool"));
    ios += roomIo("InternalInt", "nosp_temp");

    ios += thermostatXml("nosensor", ownedXml("InternalInt", "nosensor_target", SAVED) +
                                         ownedXml("InternalBool", "nosensor_heat") +
                                         linkXml("target_temp", "nosensor_target") +
                                         linkXml("output_heat", "nosensor_heat"));

    ios += thermostatXml("coolonly", ownedXml("InternalBool", "coolonly_cool") +
                                         linkXml("current_temp", "coolonly_temp") +
                                         linkXml("output_cool", "coolonly_cool"));
    ios += roomIo("InternalInt", "coolonly_temp");

    ios += thermostatXml("refuse", "      <calaos:input type=\"TestReadOnly\" id=\"refuse_heat\" "
                                   "name=\"refuse_heat\" />\n" +
                                       linkXml("output_heat", "refuse_heat") +
                                       ownedXml("InternalBool", "refuse_cool") +
                                       linkXml("output_cool", "refuse_cool") +
                                       linkXml("current_temp", "refuse_temp"));
    ios += roomIo("InternalInt", "refuse_temp");

    ios += thermostatXml("badparams", ownedXml("InternalBool", "badparams_heat") +
                                          linkXml("output_heat", "badparams_heat") +
                                          linkXml("current_temp", "badparams_temp") +
                                          paramXml("hysteresis", "abc") +
                                          paramXml("min_on_time", "-1") +
                                          paramXml("min_off_time", "1e300"));
    ios += roomIo("InternalInt", "badparams_temp");

    return ioXmlDocument(roomXml("Salon", "salon", ios));
}

std::string readFile(const std::string &path)
{
    std::ifstream f(path.c_str());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} //namespace

class InternalThermostatTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        //The state cache outlives a case: no mode may leak from the previous one.
        for (const std::string &id: THERMOSTATS)
            forgetIOState(id);
        Config::Instance().SaveValueIO("novalue_heat", "true", false);
    }

    void TearDown() override
    {
        CoreFixture::TearDown();
        //Lets the closed timer handles go before the next case.
        pumpFor(5);
    }

    //Loads `xml` and runs the first evaluation; the journal of both.
    std::string load(const std::string &xml = house())
    {
        auto evaluated = []()
        {
            return !io("th") || io("th_action")->hasValue();
        };
        bool done = false;
        auto run = [&]()
        {
            loadConfig(xml, rulesXmlDocument(""));
            done = pumpUntil(evaluated);
            pumpFor(5);
        };
        const std::string log = journal(run);
        EXPECT_TRUE(done);
        return log;
    }

    //Pumps until the IO `id` reads `state`.
    static bool waitFor(const std::string &id, bool state)
    {
        auto reached = [&]()
        {
            return on(id) == state;
        };
        return pumpUntil(reached);
    }

    static InternalThermostat *dev(const std::string &id)
    {
        return ListeRoom::Instance().findIOAs<InternalThermostat>(id);
    }

    static Json state(const std::string &id) { return dev(id)->deviceObject().at("state"); }

    //Missing IOs fail the case instead of crashing the binary.
    static IOBase *must(const std::string &id)
    {
        IOBase *i = io(id);
        EXPECT_NE(i, nullptr) << "no IO " << id;
        return i;
    }

    static bool on(const std::string &id) { return must(id) && io(id)->get_value_bool(); }
    static std::string str(const std::string &id) { return must(id) ? io(id)->get_value_string() : "<none>"; }
    static double num(const std::string &id) { return must(id) ? io(id)->get_value_double() : -1; }

    static bool mode(const std::string &id, const std::string &m)
    {
        return dev(id)->set_value(m);
    }

    static void temp(const std::string &id, double t)
    {
        if (must(id + "_temp"))
            io(id + "_temp")->set_value(t);
    }

    static void setpoints(const std::string &id, double target, double low, double high)
    {
        io(id + "_target")->set_value(target);
        if (io(id + "_low"))
        {
            io(id + "_low")->set_value(low);
            io(id + "_high")->set_value(high);
        }
    }

    //The device journal of `fn`: warnings and errors must reach it.
    template <class F>
    static std::string journal(F fn)
    {
        EXPECT_TRUE(Utils::calaosLogger("device")->isLevelEnabled(Logger::LOG_LEVEL_WARNING))
            << "the device domain is muted, the journal cannot be observed";
        testing::internal::CaptureStdout();
        fn();
        return testing::internal::GetCapturedStdout();
    }

    //Counts the notifications of an IO: one per write.
    struct Counter
    {
        int count = 0;
        IOChangeHub::Subscription sub;
    };

    static std::unique_ptr<Counter> count(const std::string &id)
    {
        auto c = std::make_unique<Counter>();
        Counter *p = c.get();
        auto counted = [p](IOBase &)
        {
            p->count++;
        };
        c->sub = IOChangeHub::Instance().subscribe(id, counted);
        return c;
    }

    static std::vector<std::string> modes(const std::string &id)
    {
        std::vector<std::string> res;
        for (ClimateMode m: dev(id)->supportedModes())
            res.push_back(ClimateBase::modeName(m));
        return res;
    }

    std::string cache() const { return ::readFile(cacheDir() + "/iostates.cache"); }
};

TEST_F(InternalThermostatTest, ModesFollowTheOutputs)
{
    const std::string log = load();

    EXPECT_EQ(modes("th"), (std::vector<std::string>{ "off", "heat" }));
    EXPECT_EQ(modes("rev"), (std::vector<std::string>{ "off", "heat", "cool", "auto" }));
    EXPECT_EQ(modes("coolonly"), (std::vector<std::string>{ "off", "cool" }));
    EXPECT_NE(log.find("Device(coolonly): no output_heat, heat is unavailable"), std::string::npos)
        << log;
    EXPECT_EQ(log.find("Device(rev): no output_heat"), std::string::npos) << log;

    EXPECT_EQ(dev("rev")->deviceObject().at("capabilities").at("modes"),
              Json({ "off", "heat", "cool", "auto" }));

    EXPECT_FALSE(mode("th", "cool"));
    EXPECT_EQ(dev("th")->get_value_string(), "off");
    EXPECT_TRUE(mode("rev", "auto"));
    EXPECT_EQ(dev("rev")->get_value_string(), "auto");
}

//Spec §8.1 with target 21, range 19..23 and a 0.5 hysteresis, every lock-out
//at 0: `start` is the output ON before the mode is set and the temperature
//moves. The edges (target - h, target, target + h) keep the current state.
TEST_F(InternalThermostatTest, EvaluateTruthTable)
{
    load();

    struct Row
    {
        const char *mode;
        const char *start;
        double temp;
        bool heat;
        bool cool;
        const char *action;
    };
    const std::vector<Row> rows = {
        { "heat", "none", 20.0, true, false, "heating" },
        { "heat", "none", 20.5, false, false, "idle" },
        { "heat", "none", 20.8, false, false, "idle" },
        { "heat", "none", 21.0, false, false, "idle" },
        { "heat", "none", 21.5, false, false, "idle" },
        { "heat", "heat", 20.0, true, false, "heating" },
        { "heat", "heat", 20.5, true, false, "heating" },
        { "heat", "heat", 20.8, true, false, "heating" },
        { "heat", "heat", 21.0, true, false, "heating" },
        { "heat", "heat", 21.2, false, false, "idle" },
        { "heat", "cool", 25.0, false, false, "idle" },
        { "heat", "cool", 20.0, true, false, "heating" },

        { "cool", "none", 22.0, false, true, "cooling" },
        { "cool", "none", 21.5, false, false, "idle" },
        { "cool", "none", 21.2, false, false, "idle" },
        { "cool", "none", 21.0, false, false, "idle" },
        { "cool", "none", 20.0, false, false, "idle" },
        { "cool", "cool", 22.0, false, true, "cooling" },
        { "cool", "cool", 21.5, false, true, "cooling" },
        { "cool", "cool", 21.2, false, true, "cooling" },
        { "cool", "cool", 21.0, false, true, "cooling" },
        { "cool", "cool", 20.8, false, false, "idle" },
        { "cool", "heat", 15.0, false, false, "idle" },
        { "cool", "heat", 22.0, false, true, "cooling" },

        { "auto", "none", 18.0, true, false, "heating" },
        { "auto", "none", 18.5, false, false, "idle" },
        { "auto", "none", 21.0, false, false, "idle" },
        { "auto", "none", 23.5, false, false, "idle" },
        { "auto", "none", 24.0, false, true, "cooling" },
        { "auto", "heat", 18.8, true, false, "heating" },
        { "auto", "heat", 19.0, true, false, "heating" },
        { "auto", "heat", 19.2, false, false, "idle" },
        { "auto", "heat", 24.0, false, true, "cooling" },
        { "auto", "cool", 23.2, false, true, "cooling" },
        { "auto", "cool", 23.0, false, true, "cooling" },
        { "auto", "cool", 22.8, false, false, "idle" },
        { "auto", "cool", 18.0, true, false, "heating" },

        { "off", "none", 10.0, false, false, "off" },
        { "off", "heat", 10.0, false, false, "off" },
        { "off", "cool", 30.0, false, false, "off" },
    };

    setpoints("rev", 21, 19, 23);
    for (const Row &r: rows)
    {
        const std::string label = std::string(r.mode) + " from " + r.start + " at " +
                                  Utils::to_string(r.temp);
        mode("rev", "off");
        ASSERT_FALSE(on("rev_heat")) << label;
        ASSERT_FALSE(on("rev_cool")) << label;
        if (std::string(r.start) == "heat")
        {
            mode("rev", "heat");
            temp("rev", 10);
            ASSERT_TRUE(on("rev_heat")) << label;
        }
        else if (std::string(r.start) == "cool")
        {
            mode("rev", "cool");
            temp("rev", 30);
            ASSERT_TRUE(on("rev_cool")) << label;
        }
        else
            temp("rev", r.temp);

        mode("rev", r.mode);
        temp("rev", r.temp);
        EXPECT_EQ(on("rev_heat"), r.heat) << label;
        EXPECT_EQ(on("rev_cool"), r.cool) << label;
        EXPECT_EQ(str("rev_action"), r.action) << label;
        EXPECT_EQ(state("rev").value("action", ""), r.action) << label;
    }
}

TEST_F(InternalThermostatTest, HeatOnlyThermostat)
{
    load();
    EXPECT_EQ(str("th_action"), "off");

    setpoints("th", 21, 0, 0);
    mode("th", "heat");
    EXPECT_EQ(str("th_action"), "idle");
    temp("th", 20);
    EXPECT_TRUE(on("th_heat"));
    EXPECT_EQ(str("th_action"), "heating");
    temp("th", 21.2);
    EXPECT_FALSE(on("th_heat"));
    EXPECT_EQ(str("th_action"), "idle");
}

TEST_F(InternalThermostatTest, OutputsAndActionAreOnlyWrittenWhenTheyChange)
{
    load();
    setpoints("rev", 21, 19, 23);
    temp("rev", 22);
    mode("rev", "heat");

    auto heat = count("rev_heat");
    auto cool = count("rev_cool");
    auto action = count("rev_action");

    temp("rev", 19);
    EXPECT_EQ(heat->count, 1);
    EXPECT_EQ(action->count, 1);

    //Inside the band, the same setpoint, the same mode: nothing to write.
    temp("rev", 19.5);
    temp("rev", 20.6);
    io("rev_target")->set_value(21.0);
    mode("rev", "heat");
    EXPECT_EQ(heat->count, 1);
    EXPECT_EQ(action->count, 1);

    temp("rev", 21.2);
    EXPECT_EQ(heat->count, 2);
    EXPECT_EQ(action->count, 2);
    EXPECT_EQ(cool->count, 0);
}

TEST_F(InternalThermostatTest, AnOutputChangedBehindTheThermostatIsPutBack)
{
    load();
    setpoints("rev", 21, 19, 23);
    mode("rev", "heat");
    temp("rev", 19);
    ASSERT_TRUE(on("rev_heat"));

    io("rev_heat")->set_value(false);
    temp("rev", 19.2);
    EXPECT_TRUE(on("rev_heat"));

    temp("rev", 22);
    ASSERT_FALSE(on("rev_heat"));
    io("rev_heat")->set_value(true);
    temp("rev", 22.1);
    EXPECT_FALSE(on("rev_heat"));
    EXPECT_EQ(str("rev_action"), "idle");
}

//Until CLIM-4 adds the startup and stale sensor rules, a sensor that has never
//given a value keeps both outputs OFF, even one left ON by the previous run.
TEST_F(InternalThermostatTest, NoCurrentTempValueKeepsTheOutputsOff)
{
    load();
    EXPECT_FALSE(io("novalue_temp")->hasValue());
    EXPECT_FALSE(on("novalue_heat"));

    setpoints("novalue", 21, 0, 0);
    mode("novalue", "heat");
    EXPECT_FALSE(on("novalue_heat"));
    EXPECT_EQ(str("novalue_action"), "idle");

    temp("novalue", 18);
    EXPECT_TRUE(on("novalue_heat"));
}

TEST_F(InternalThermostatTest, NoCurrentTempLinkKeepsTheOutputsOff)
{
    const std::string log = load();
    EXPECT_NE(log.find("Device(nosensor): no current_temp, the outputs stay OFF"),
              std::string::npos)
        << log;

    io("nosensor_target")->set_value(30.0);
    mode("nosensor", "heat");
    EXPECT_FALSE(on("nosensor_heat"));
    EXPECT_EQ(str("nosensor_action"), "idle");
}

TEST_F(InternalThermostatTest, SetpointWithoutValueKeepsItsOutputOff)
{
    load();
    ASSERT_FALSE(io("nosp_target")->hasValue());

    //Read as 0, an unknown target would start cooling at 5 °C.
    temp("nosp", 5);
    mode("nosp", "cool");
    EXPECT_FALSE(on("nosp_cool"));
    mode("nosp", "heat");
    EXPECT_FALSE(on("nosp_heat"));

    io("nosp_target")->set_value(20.0);
    EXPECT_TRUE(on("nosp_heat"));
}

TEST_F(InternalThermostatTest, RefusedOutputWriteIsLogged)
{
    std::string log = load();
    EXPECT_NE(log.find("Device(refuse): output_heat 'refuse_heat' refused to switch OFF"),
              std::string::npos)
        << log;
}

//An output that refused to switch ON is still OFF: it must not show as
//heating, nor hold the other output back.
TEST_F(InternalThermostatTest, ARefusedSwitchOnIsNotTakenAsDone)
{
    load();
    ASSERT_NE(io("refuse_target_temp_low"), nullptr);
    io("refuse_target_temp_low")->set_value(25.0);
    io("refuse_target_temp_high")->set_value(18.0);

    auto run = []()
    {
        mode("refuse", "auto");
        temp("refuse", 20);
    };
    const std::string log = journal(run);
    EXPECT_NE(log.find("Device(refuse): output_heat 'refuse_heat' refused to switch ON"),
              std::string::npos)
        << log;
    EXPECT_TRUE(on("refuse_cool"));
    EXPECT_EQ(str("refuse_action"), "cooling");
}

TEST_F(InternalThermostatTest, InvalidParamsAreLoggedAndReplaced)
{
    const std::string log = load();
    EXPECT_NE(log.find("Device(badparams): hysteresis 'abc' is not a non-negative number, 0.5 is used"),
              std::string::npos)
        << log;
    EXPECT_NE(log.find("Device(badparams): min_on_time '-1' is not a non-negative number, 0 is used"),
              std::string::npos)
        << log;
    EXPECT_EQ(log.find("min_off_time"), std::string::npos) << log;

    ASSERT_NE(io("badparams_target_temp"), nullptr);
    io("badparams_target_temp")->set_value(21.0);
    mode("badparams", "heat");
    temp("badparams", 18);
    ASSERT_TRUE(on("badparams_heat"));
    temp("badparams", 22);
    ASSERT_FALSE(on("badparams_heat"));
    //1e300 s holds the output OFF: no overflow into "no lock-out".
    temp("badparams", 18);
    EXPECT_FALSE(on("badparams_heat"));
    EXPECT_EQ(str("badparams_action"), "idle");
}

TEST_F(InternalThermostatTest, MinOnTimeDefersTheSwitchOff)
{
    load();
    setpoints("lock", 21, 19, 23);
    mode("lock", "heat");

    const auto t0 = Clock::now();
    temp("lock", 19);
    ASSERT_TRUE(on("lock_heat"));

    temp("lock", 23);
    EXPECT_TRUE(on("lock_heat"));
    EXPECT_EQ(str("lock_action"), "heating");

    ASSERT_TRUE(waitFor("lock_heat", false));
    EXPECT_GE(msSince(t0), 198);
    EXPECT_EQ(str("lock_action"), "idle");
}

TEST_F(InternalThermostatTest, MinOffTimeDefersTheSwitchOn)
{
    load();
    setpoints("lock", 21, 19, 23);
    mode("lock", "heat");
    temp("lock", 19);
    ASSERT_TRUE(on("lock_heat"));
    pumpFor(220);

    const auto t0 = Clock::now();
    temp("lock", 23);
    ASSERT_FALSE(on("lock_heat"));

    temp("lock", 19);
    EXPECT_FALSE(on("lock_heat"));
    EXPECT_EQ(str("lock_action"), "idle");

    ASSERT_TRUE(waitFor("lock_heat", true));
    EXPECT_GE(msSince(t0), 298);
}

//A lock-out timer that becomes useless is dropped, and the state it would have
//changed is the one evaluate() keeps.
//min_on_time spares the equipment short cycles; it must not delay a stop
//that was asked for (mode off) or that safety requires. min_off_time still
//guards the restart.
TEST_F(InternalThermostatTest, OffAndARefusedOutputStopAtOnce)
{
    load();
    setpoints("lock", 21, 19, 23);
    mode("lock", "heat");
    temp("lock", 19);
    ASSERT_TRUE(on("lock_heat"));

    const auto t0 = Clock::now();
    mode("lock", "off");
    EXPECT_FALSE(on("lock_heat"));
    EXPECT_EQ(str("lock_action"), "off");
    mode("lock", "heat");
    EXPECT_FALSE(on("lock_heat"));
    ASSERT_TRUE(waitFor("lock_heat", true));
    EXPECT_GE(msSince(t0), 298);

    auto *safe = ListeRoom::Instance().findIOAs<TestSafeThermostat>("safe");
    ASSERT_NE(safe, nullptr);
    setpoints("safe", 21, 19, 23);
    mode("safe", "heat");
    temp("safe", 19);
    ASSERT_TRUE(on("safe_heat"));

    const auto t1 = Clock::now();
    safe->heatAllowed = false;
    temp("safe", 19);
    EXPECT_FALSE(on("safe_heat"));
    EXPECT_EQ(str("safe_action"), "idle");

    safe->heatAllowed = true;
    temp("safe", 19);
    EXPECT_FALSE(on("safe_heat"));
    ASSERT_TRUE(waitFor("safe_heat", true));
    EXPECT_GE(msSince(t1), 298);
}

TEST_F(InternalThermostatTest, ADeferredSwitchFollowsTheLatestDemand)
{
    load();
    setpoints("lock", 21, 19, 23);
    mode("lock", "heat");
    temp("lock", 19);
    temp("lock", 23);
    temp("lock", 20.8);
    pumpFor(300);
    EXPECT_TRUE(on("lock_heat"));
}

TEST_F(InternalThermostatTest, AutoChangeoverWaitsForTheOtherOutput)
{
    load();
    setpoints("lock", 21, 19, 23);
    mode("lock", "auto");

    const auto t0 = Clock::now();
    temp("lock", 18);
    ASSERT_TRUE(on("lock_heat"));

    temp("lock", 25);
    EXPECT_TRUE(on("lock_heat"));
    EXPECT_FALSE(on("lock_cool"));

    bool both = false;
    int heatOff = -1;
    auto coolOn = [&]()
    {
        both = both || (on("lock_heat") && on("lock_cool"));
        if (heatOff < 0 && !on("lock_heat"))
            heatOff = msSince(t0);
        return on("lock_cool");
    };
    ASSERT_TRUE(pumpUntil(coolOn));
    EXPECT_FALSE(both);
    EXPECT_GE(heatOff, 198);
    //min_on_time of heat, then min_off_time of heat before cool.
    EXPECT_GE(msSince(t0), 498);
    EXPECT_EQ(str("lock_action"), "cooling");
}

TEST_F(InternalThermostatTest, ModeChangeoverWaitsForTheOtherOutput)
{
    load();
    setpoints("lock", 21, 19, 23);
    mode("lock", "cool");

    const auto t0 = Clock::now();
    temp("lock", 25);
    ASSERT_TRUE(on("lock_cool"));

    mode("lock", "heat");
    temp("lock", 18);
    EXPECT_TRUE(on("lock_cool"));
    EXPECT_FALSE(on("lock_heat"));
    EXPECT_EQ(str("lock_action"), "cooling");

    bool both = false;
    auto heatOn = [&]()
    {
        both = both || (on("lock_heat") && on("lock_cool"));
        return on("lock_heat");
    };
    ASSERT_TRUE(pumpUntil(heatOn));
    EXPECT_FALSE(both);
    EXPECT_FALSE(on("lock_cool"));
    EXPECT_GE(msSince(t0), 498);
}

//Written by a rule straight into the IOs, a range can be inverted: both
//outputs then ask to run, and only one may.
TEST_F(InternalThermostatTest, NeverBothOnWithAnInvertedRange)
{
    load();
    setpoints("rev", 21, 25, 18);
    mode("rev", "auto");
    temp("rev", 20);
    EXPECT_TRUE(on("rev_heat"));
    EXPECT_FALSE(on("rev_cool"));
    EXPECT_EQ(str("rev_action"), "heating");
}

/* Until both the last write and the read-back say OFF, an output counts as
 * ON for the other one. The read-back catching up is enough to go on, without
 * waiting for the next temperature. */
TEST_F(InternalThermostatTest, ALaggingReadBackStillCountsAsOn)
{
    load();
    auto *heat = dynamic_cast<TestLaggingOutput *>(io("lag_heat"));
    auto *cool = dynamic_cast<TestLaggingOutput *>(io("lag_cool"));
    ASSERT_TRUE(heat && cool);
    io("lag_target")->set_value(21.0);

    mode("lag", "heat");
    temp("lag", 18);
    ASSERT_EQ(heat->commands, (std::vector<bool>{ false, true }));

    //Written ON, not read back yet.
    mode("lag", "cool");
    temp("lag", 25);
    EXPECT_EQ(heat->commands, (std::vector<bool>{ false, true }));
    EXPECT_EQ(cool->commands, (std::vector<bool>{ false }));

    //Read back ON, written OFF once min_on_time is over, not read back yet.
    heat->confirm();
    auto heatOffSent = [&]()
    {
        return heat->commands.back() == false;
    };
    ASSERT_TRUE(pumpUntil(heatOffSent));
    EXPECT_EQ(cool->commands, (std::vector<bool>{ false }));

    heat->confirm();
    auto coolOnSent = [&]()
    {
        return cool->commands.back() == true;
    };
    EXPECT_TRUE(pumpUntil(coolOnSent, 1000));
}

//Inside the band, an output keeps the state last written: an OFF not read
//back yet does not turn into ON again.
TEST_F(InternalThermostatTest, AnOffNotReadBackYetStaysOffInTheBand)
{
    load();
    auto *heat = dynamic_cast<TestLaggingOutput *>(io("lag_heat"));
    ASSERT_NE(heat, nullptr);
    io("lag_target")->set_value(21.0);
    mode("lag", "heat");
    temp("lag", 18);
    heat->confirm();
    pumpFor(220);

    temp("lag", 22);
    ASSERT_EQ(heat->commands.back(), false);
    const size_t ons = std::count(heat->commands.begin(), heat->commands.end(), true);

    temp("lag", 20.8);
    pumpFor(20);
    EXPECT_EQ(std::count(heat->commands.begin(), heat->commands.end(), true), ons);
    EXPECT_EQ(heat->commands.back(), false);
    EXPECT_EQ(str("lag_action"), "idle");
}

//Written behind the thermostat's back, an output is put back without waiting
//for the next temperature: heat and cool are never left ON together.
TEST_F(InternalThermostatTest, AnOutputSwitchedBehindTheThermostatIsPutBackAtOnce)
{
    load();
    setpoints("rev", 21, 19, 23);
    mode("rev", "cool");
    temp("rev", 25);
    ASSERT_TRUE(on("rev_cool"));

    io("rev_heat")->set_value(true);
    EXPECT_TRUE(waitFor("rev_heat", false));
    EXPECT_TRUE(on("rev_cool"));
    EXPECT_EQ(str("rev_action"), "cooling");
}

//Putting an output back is not a switch of the thermostat: min_off_time still
//counts from its own OFF.
TEST_F(InternalThermostatTest, PuttingAnOutputBackDoesNotRestartItsLockOut)
{
    load();
    setpoints("slow", 21, 0, 0);
    mode("slow", "heat");
    temp("slow", 19);
    ASSERT_TRUE(on("slow_heat"));
    const auto tOff = Clock::now();
    temp("slow", 22);
    ASSERT_FALSE(on("slow_heat"));

    pumpFor(700);
    io("slow_heat")->set_value(true);
    ASSERT_TRUE(waitFor("slow_heat", false));

    temp("slow", 19);
    ASSERT_TRUE(waitFor("slow_heat", true));
    EXPECT_GE(msSince(tOff), 998);
    EXPECT_LT(msSince(tOff), 1600);
}

/* Outside a rule march (a command, a timer), an output write runs the rules
 * at once, and one of them may change an input in the middle of a pass: the
 * rest of that pass is dropped and it starts over, never nested. */
TEST_F(InternalThermostatTest, ARuleReactingToAnOutputRestartsTheEvaluation)
{
    load();
    setpoints("rev", 21, 19, 23);
    temp("rev", 25);
    io("rev_heat")->set_value(true);
    ASSERT_NE(addSimpleRule("heat_off_raises_target", "rev_heat", "==", "false", "rev_target", "30"),
              nullptr);
    auto cool = count("rev_cool");

    //heat goes OFF first, its rule moves the target above 25: cool must stay OFF.
    mode("rev", "cool");
    EXPECT_EQ(num("rev_target"), 30);
    EXPECT_FALSE(on("rev_heat"));
    EXPECT_FALSE(on("rev_cool"));
    EXPECT_EQ(cool->count, 0);
    EXPECT_EQ(str("rev_action"), "idle");
}

TEST_F(InternalThermostatTest, CommandsThroughSetState)
{
    load();
    setpoints("rev", 21, 19, 23);
    temp("rev", 21.4);
    JsonApi api;

    Params p;
    p.Add("id", "rev");
    p.Add("value", R"({"mode":"heat","target_temp":22})");
    EXPECT_TRUE(api.decodeSetState(p));
    EXPECT_EQ(num("rev_target"), 22);
    EXPECT_TRUE(on("rev_heat"));

    p.Add("value", R"({"mode":"auto","target_temp_low":20,"target_temp_high":25})");
    EXPECT_TRUE(api.decodeSetState(p));
    EXPECT_EQ(num("rev_low"), 20);
    EXPECT_EQ(num("rev_high"), 25);
    EXPECT_FALSE(on("rev_heat"));
    EXPECT_FALSE(on("rev_cool"));

    p.Add("value", R"({"target_temp_high":20.5})");
    EXPECT_FALSE(api.decodeSetState(p));
    EXPECT_EQ(num("rev_high"), 25);

    p.Add("value", "off");
    EXPECT_TRUE(api.decodeSetState(p));
    EXPECT_EQ(state("rev").value("mode", ""), "off");
    EXPECT_EQ(str("rev_action"), "off");
}

//A command is evaluated once all its fields are applied: the mode must not
//switch ON against the previous target, then hold for min_on_time.
TEST_F(InternalThermostatTest, ACommandIsEvaluatedOnceWhole)
{
    load();
    setpoints("lock", 25, 19, 23);
    temp("lock", 20);
    auto heat = count("lock_heat");

    EXPECT_TRUE(mode("lock", R"({"mode":"heat","target_temp":18})"));
    EXPECT_EQ(num("lock_target"), 18);
    EXPECT_FALSE(on("lock_heat"));
    EXPECT_EQ(heat->count, 0);
    EXPECT_EQ(str("lock_action"), "idle");

    EXPECT_TRUE(mode("lock", R"({"target_temp":22})"));
    EXPECT_TRUE(on("lock_heat"));
}

TEST_F(InternalThermostatTest, ModeIsKeptInTheStateCache)
{
    load();
    EXPECT_EQ(dev("rev")->get_value_string(), "off");
    mode("rev", "cool");
    EXPECT_NE(cache().find(R"("rev": "cool")"), std::string::npos) << cache();

    //From the file, not from what the process still holds.
    Config::Instance().loadStateCache();
    reloadFromDisk();
    pumpFor(5);
    EXPECT_EQ(dev("rev")->get_value_string(), "cool");
    EXPECT_EQ(state("rev").value("mode", ""), "cool");
}

TEST_F(InternalThermostatTest, ASavedModeTheOutputsCannotRunIsOff)
{
    Config::Instance().SaveValueIO("th", "cool", false);
    Config::Instance().SaveValueIO("coolonly", "dry", false);
    Config::Instance().SaveValueIO("rev", "auto", false);
    const std::string log = load();

    EXPECT_EQ(dev("th")->get_value_string(), "off");
    EXPECT_EQ(dev("coolonly")->get_value_string(), "off");
    EXPECT_EQ(dev("rev")->get_value_string(), "auto");
    EXPECT_NE(log.find("Device(th): saved mode #"), std::string::npos) << log;
    EXPECT_NE(log.find("Device(coolonly): saved mode #"), std::string::npos) << log;
    EXPECT_EQ(log.find("Device(rev): saved mode"), std::string::npos) << log;
}

TEST_F(InternalThermostatTest, MissingOwnedIosAreRecreated)
{
    const std::string log = load();

    const std::vector<std::pair<std::string, std::string>> created = {
        { "target_temp", "bare_target_temp" },
        { "target_temp_low", "bare_target_temp_low" },
        { "target_temp_high", "bare_target_temp_high" },
        { "action", "bare_action" },
    };
    for (const auto &c: created)
    {
        EXPECT_NE(log.find("Device(bare): no IO for '" + c.first + "', created '" + c.second + "'"),
                  std::string::npos)
            << log;
        ASSERT_NE(io(c.second), nullptr) << c.second;
        EXPECT_EQ(io(c.second)->getOwnerDevice(), "bare") << c.second;
        EXPECT_EQ(dev("bare")->deviceObject().at("ios").value(c.first, ""), c.second);
    }
    EXPECT_EQ(num("bare_target_temp"), 20);
    EXPECT_EQ(num("bare_target_temp_low"), 19);
    //Wide enough for min_range.
    EXPECT_EQ(num("bare_target_temp_high"), 26);
    EXPECT_EQ(io("bare_target_temp")->get_param("save"), "true");
    EXPECT_EQ(str("bare_action"), "off");
    EXPECT_TRUE(dev("bare")->deviceObject().at("capabilities").value("target_temp_range", false));

    //Regulates on them, and publishes their changes.
    mode("bare", "heat");
    temp("bare", 18);
    EXPECT_TRUE(on("bare_heat"));
    EXPECT_EQ(state("bare").value("action", ""), "heating");

    //Saved inside the device, so the next load finds them.
    EXPECT_EQ(IOChangeHub::Instance().subscriberCount("bare_action"), 1u);
    EXPECT_EQ(IOChangeHub::Instance().subscriberCount("bare_target_temp"), 2u);

    saveConfig();
    EXPECT_NE(ioXmlOnDisk().find(R"(id="bare_target_temp_high")"), std::string::npos);
    auto reload = [this]()
    {
        reloadFromDisk();
    };
    const std::string again = journal(reload);
    EXPECT_EQ(again.find("', created '"), std::string::npos) << again;
    EXPECT_EQ(io("bare_target_temp")->getOwnerDevice(), "bare");
    EXPECT_TRUE(roundTripIo());
}

TEST_F(InternalThermostatTest, DanglingLinksGetTheirIoBack)
{
    const std::string log = load();
    EXPECT_NE(log.find("Device(dangling): no IO for 'target_temp', created 'dangling_sp'"),
              std::string::npos)
        << log;
    EXPECT_NE(log.find("Device(dangling): no IO for 'action', created 'dangling_action'"),
              std::string::npos)
        << log;
    ASSERT_NE(io("dangling_sp"), nullptr);
    EXPECT_EQ(io("dangling_sp")->getOwnerDevice(), "dangling");
    EXPECT_EQ(dev("dangling")->deviceObject().at("ios").value("action", ""), "dangling_action");

    //Each link followed once, for the publication (and the regulation).
    EXPECT_EQ(IOChangeHub::Instance().subscriberCount("dangling_action"), 1u);
    EXPECT_EQ(IOChangeHub::Instance().subscriberCount("dangling_sp"), 2u);
    mode("dangling", "heat");
    EXPECT_EQ(state("dangling").value("action", ""), "idle");
}

TEST_F(InternalThermostatTest, RecreatedIoAvoidsATakenId)
{
    const std::string log = load();
    EXPECT_NE(log.find("Device(taken): no IO for 'target_temp', created 'taken_target_temp_0'"),
              std::string::npos)
        << log;
    EXPECT_EQ(io("taken_target_temp")->getOwnerDevice(), "");
    ASSERT_NE(io("taken_target_temp_0"), nullptr);
    EXPECT_EQ(io("taken_target_temp_0")->getOwnerDevice(), "taken");
}

//The first device holding an id owns the IOs (Device::ownedIos()): its
//shadow must not create any under that id.
TEST_F(InternalThermostatTest, AShadowedThermostatCreatesNothing)
{
    const std::string xml = ioXmlDocument(roomXml(
        "Salon", "salon",
        thermostatXml("dup", ownedXml("InternalInt", "dup_target", SAVED) +
                                 ownedXml("InternalString", "dup_action") +
                                 linkXml("target_temp", "dup_target") +
                                 linkXml("action", "dup_action")) +
            thermostatXml("dup", "")));
    const std::string log = load(xml);

    EXPECT_EQ(log.find("', created '"), std::string::npos) << log;
    EXPECT_EQ(io("dup_target_temp"), nullptr);
    EXPECT_EQ(io("dup_action_0"), nullptr);
    EXPECT_EQ(firstRoom()->get_size(), 4);
}

//An id kept by a device this build cannot load is not given to a new IO.
TEST_F(InternalThermostatTest, AReservedIdIsNotRecreated)
{
    const std::string xml = ioXmlDocument(roomXml(
        "Salon", "salon",
        "    <calaos:device id=\"opaque\" type=\"climate\" impl=\"unknown_impl\" name=\"o\" />\n" +
            thermostatXml("res", linkXml("target_temp", "opaque"))));
    const std::string log = load(xml);

    EXPECT_EQ(log.find("created 'opaque'"), std::string::npos) << log;
    EXPECT_EQ(io("opaque"), nullptr);
    EXPECT_EQ(dev("res")->deviceObject().at("ios").value("target_temp", ""), "");
    EXPECT_EQ(dev("res")->deviceObject().at("ios").value("action", ""), "res_action");
}

//Deleting the device leaves its outputs as they are (documented in the
//header) and nothing of it may run afterwards: run under ASan.
TEST_F(InternalThermostatTest, DeletedWhileALockOutIsArmed)
{
    load();
    setpoints("lock", 21, 19, 23);
    mode("lock", "heat");
    temp("lock", 19);
    temp("lock", 23);
    ASSERT_TRUE(on("lock_heat"));
    const size_t before = IOChangeHub::Instance().subscriberCount("lock_temp");
    ASSERT_GE(before, 2u);

    ASSERT_TRUE(deleteIO(io("lock")));
    EXPECT_EQ(io("lock"), nullptr);
    EXPECT_EQ(io("lock_target"), nullptr);
    EXPECT_EQ(IOChangeHub::Instance().subscriberCount("lock_temp"), 0u);

    pumpFor(400);
    temp("lock", 30);
    EXPECT_TRUE(on("lock_heat"));
}

TEST_F(InternalThermostatTest, DeletedBeforeItsFirstEvaluation)
{
    loadConfig(house(), rulesXmlDocument(""));
    ASSERT_FALSE(io("th_action")->hasValue());
    ASSERT_TRUE(deleteIO(io("th")));
    pumpFor(20);
    EXPECT_EQ(io("th"), nullptr);
}
