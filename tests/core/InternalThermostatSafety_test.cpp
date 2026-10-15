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

//InternalThermostat safety (spec §8.2) on the real libuv loop: each rule cuts
//the outputs, and regulation comes back once the cause is gone.

#include "CalaosCoreFixture.h"
#include "InternalThermostat.h"
#include "libuvw.h"

#include <chrono>
#include <functional>
#include <limits>
#include <string>
#include <thread>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

//A sensor driver: a new value is signalled, a repeated one only recorded.
class TestSensor: public IOBase
{
public:
    TestSensor(Params &p):
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

    void repeat() { markReading(); }

private:
    double value = 0;
};

REGISTER_IO(TestSensor)

//A driver that signals its changes and nothing else.
class TestChangeSensor: public IOBase
{
public:
    TestChangeSensor(Params &p):
        IOBase(p, IOBase::IO_INPUT)
    {
    }

    DATA_TYPE get_type() override { return TINT; }
};

REGISTER_IO(TestChangeSensor)

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
    pumpUntil([]()
              {
        return false;
    },
              ms);
}

std::string link(const std::string &name, const std::string &io)
{
    return "      <calaos:link name=\"" + name + "\" io=\"" + io + "\" />\n";
}

std::string param(const std::string &name, const std::string &value)
{
    return "      <calaos:param name=\"" + name + "\" value=\"" + value + "\" />\n";
}

std::string internal(const std::string &type, const std::string &id,
                     const std::string &extra = std::string())
{
    return "      <calaos:internal type=\"" + type + "\" id=\"" + id + "\" name=\"" + id +
           "\" visible=\"false\"" + extra + " />\n";
}

const char *const SAVED = R"( save="true")";

/* <id>: owned setpoints, outputs and action, all saved; the sensor <id>_temp
 * in the room unless `sensor` holds its own element. */
std::string thermostat(const std::string &id, bool cool, const std::string &params,
                       const std::string &sensor = std::string())
{
    std::string inner = internal("InternalInt", id + "_target", SAVED) +
                        internal("InternalString", id + "_action") +
                        internal("InternalBool", id + "_heat", SAVED) +
                        link("current_temp", id + "_temp") + link("target_temp", id + "_target") +
                        link("action", id + "_action") + link("output_heat", id + "_heat");
    if (cool)
        inner += internal("InternalInt", id + "_low", SAVED) +
                 internal("InternalInt", id + "_high", SAVED) +
                 internal("InternalBool", id + "_cool", SAVED) +
                 link("target_temp_low", id + "_low") + link("target_temp_high", id + "_high") +
                 link("output_cool", id + "_cool");
    std::string room = "    <calaos:device id=\"" + id +
                       "\" type=\"climate\" impl=\"thermostat\" name=\"" + id + "\">\n" + inner +
                       params + "    </calaos:device>\n";
    if (sensor.empty())
        room += internal("InternalInt", id + "_temp");
    else
        room += sensor;
    return room;
}

std::string house()
{
    std::string ios;
    //A sensor whose value comes from the state cache, not from a reading.
    ios += thermostat("boot", false, "", internal("InternalInt", "boot_temp", SAVED));
    //Stale after 0.2 s, and a min_on_time a stale stop must not wait for.
    ios += thermostat("stale", false, param("sensor_timeout", "0.2") + param("min_on_time", "5"));
    //Timeouts of the tests that keep a sensor fresh are at least 10 times their cadence: a
    //runner that stalls the test for a few hundred ms must not make the sensor silent.
    ios += thermostat("steady", false, param("sensor_timeout", "1"));
    ios += thermostat("quiet", false, param("sensor_timeout", "1"),
                      "    <calaos:input type=\"TestSensor\" id=\"quiet_temp\" name=\"quiet_temp\" />\n");
    //Floor cooling: cut at 65 % humidity, without waiting for min_on_time.
    ios += thermostat("dew", true,
                      param("emitter", "floor") + link("humidity", "dew_hum") +
                          param("humidity_max", "65") + param("min_on_time", "5"));
    ios += internal("InternalInt", "dew_hum");
    ios += thermostat("dewst", true,
                      param("emitter", "floor") + link("humidity", "dewst_hum") +
                          param("sensor_timeout", "0.6"));
    ios += internal("InternalInt", "dewst_hum");
    //Floor cooling without a humidity link, its output_cool left ON.
    ios += thermostat("nohum", true, param("emitter", "floor"));
    ios += thermostat("fancoil", true, param("emitter", "fan_coil"));
    ios += thermostat("start", false, param("min_off_time", "0.4"));
    //Plausible readings 5..40; a stop by implausible reading ignores min_on_time, a restart not min_off_time.
    ios += thermostat("rng", false,
                      param("valid_min", "5") + param("valid_max", "40") + param("min_on_time", "5") +
                          param("min_off_time", "0.6"));
    ios += thermostat("rngst", false,
                      param("valid_min", "5") + param("valid_max", "40") + param("sensor_timeout", "1"));
    ios += thermostat("lo", false, param("valid_min", "10"));
    ios += thermostat("hi", false, param("valid_max", "30"));
    //Unusable bounds: a word, and an interval nothing fits in.
    ios += thermostat("rngbad", false, param("valid_min", "warm") + param("valid_max", "30"));
    ios += thermostat("rngswap", false, param("valid_min", "30") + param("valid_max", "10"));
    //Sensors that only report their changes.
    ios += thermostat("chg", true, param("emitter", "floor") + link("humidity", "chg_hum"),
                      "    <calaos:input type=\"TestChangeSensor\" id=\"chg_temp\" name=\"chg_temp\" />\n");
    ios += "    <calaos:input type=\"TestChangeSensor\" id=\"chg_hum\" name=\"chg_hum\" />\n";
    return ioXmlDocument(roomXml("Salon", "salon", ios));
}

struct Thermostat
{
    std::string id;
    bool cool;
};

//Every thermostat of the house: their saved IOs are reset before each case.
const std::vector<Thermostat> THERMOSTATS = { { "boot", false }, { "stale", false }, { "steady", false }, { "quiet", false }, { "dew", true }, { "dewst", true }, { "nohum", true }, { "fancoil", true }, { "start", false }, { "chg", true }, { "rng", false }, { "rngst", false }, { "lo", false }, { "hi", false }, { "rngbad", false }, { "rngswap", false } };

} //namespace

class InternalThermostatSafetyTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        //The state cache outlives a case: nothing may leak from the previous
        //one. Target 21, range 19..23, outputs OFF, mode off.
        Config &c = Config::Instance();
        for (const Thermostat &t: THERMOSTATS)
        {
            forgetIOState(t.id);
            c.SaveValueIO(t.id + "_target", "21", false);
            c.SaveValueIO(t.id + "_heat", "false", false);
            if (t.cool)
            {
                c.SaveValueIO(t.id + "_low", "19", false);
                c.SaveValueIO(t.id + "_high", "23", false);
                c.SaveValueIO(t.id + "_cool", "false", false);
            }
        }
        c.SaveValueIO("boot_temp", "18", false);
        c.SaveValueIO("nohum_cool", "true", false);
    }

    void TearDown() override
    {
        CoreFixture::TearDown();
        pumpFor(5);
    }

    //Loads the house and runs the first evaluation; the device journal of both.
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

    static std::string fault(const std::string &id)
    {
        return dev(id)->deviceObject().at("state").value("fault", "");
    }

    static bool mode(const std::string &id, const std::string &m) { return dev(id)->set_value(m); }

    static std::vector<std::string> modes(const std::string &id)
    {
        std::vector<std::string> res;
        for (ClimateMode m: dev(id)->supportedModes())
            res.push_back(ClimateBase::modeName(m));
        return res;
    }

    static void set(const std::string &id, double v)
    {
        if (must(id))
            io(id)->set_value(v);
    }

    static bool waitFor(const std::string &id, bool state, int ms = 5000)
    {
        return pumpUntil([&]()
                         {
            return on(id) == state;
        },
                         ms);
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

    static bool has(const std::string &log, const std::string &needle)
    {
        return log.find(needle) != std::string::npos;
    }

    static size_t count(const std::string &log, const std::string &needle)
    {
        size_t n = 0;
        for (size_t at = log.find(needle); at != std::string::npos; at = log.find(needle, at + 1))
            n++;
        return n;
    }
};

//A value restored from the state cache is what the sensor said before the
//restart, maybe hours ago: regulation waits for a reading.
TEST_F(InternalThermostatSafetyTest, AValueFromTheStateCacheIsNotAReading)
{
    load();
    ASSERT_TRUE(io("boot_temp")->hasValue());
    mode("boot", "heat");
    pumpFor(20);
    EXPECT_FALSE(on("boot_heat"));
    EXPECT_EQ(str("boot_action"), "idle");

    //The same value, read this time.
    set("boot_temp", 18);
    EXPECT_TRUE(on("boot_heat"));
    EXPECT_EQ(str("boot_action"), "heating");
}

const char *const STALE_LOG = "Device(stale): current_temp 'stale_temp' has sent nothing for 0.2 s, "
                              "the outputs are OFF";

//Spec §8.2: no reading for sensor_timeout, outputs OFF at once and an error,
//once.
TEST_F(InternalThermostatSafetyTest, AStaleSensorStopsTheOutputsAtOnce)
{
    load();
    mode("stale", "heat");
    //Before the reading: the timeout runs from it, not from this test's next line.
    const auto t0 = Clock::now();
    set("stale_temp", 18);
    ASSERT_TRUE(on("stale_heat"));

    bool stopped = false;
    const std::string log = journal([&]()
                                    {
        stopped = waitFor("stale_heat", false, 2000);
        pumpFor(500);
    });
    ASSERT_TRUE(stopped);
    EXPECT_GE(msSince(t0), 190 + 500);
    //Long before min_on_time.
    EXPECT_LT(msSince(t0), 2000);
    EXPECT_EQ(count(log, STALE_LOG), 1u) << log;
    EXPECT_EQ(str("stale_action"), "idle");

    //Nothing but a reading brings it back.
    set("stale_target", 25);
    pumpFor(50);
    EXPECT_FALSE(on("stale_heat"));
}

TEST_F(InternalThermostatSafetyTest, ReadingsWithinTheTimeoutKeepTheOutputsOn)
{
    load();
    mode("steady", "heat");
    for (int i = 0; i < 8; i++)
    {
        set("steady_temp", 18 + (i % 2) * 0.1);
        pumpFor(100);
        ASSERT_TRUE(on("steady_heat")) << "after " << i + 1 << " readings";
    }
}

TEST_F(InternalThermostatSafetyTest, AStaleSensorRegulatesAgainOnItsNextReading)
{
    load();
    mode("stale", "heat");
    set("stale_temp", 18);
    ASSERT_TRUE(waitFor("stale_heat", false, 2000));

    //The value it had: still a reading.
    set("stale_temp", 18);
    EXPECT_TRUE(on("stale_heat"));
    EXPECT_EQ(str("stale_action"), "heating");

    //And a second episode is logged again.
    const std::string log = journal([]()
                                    {
        EXPECT_TRUE(waitFor("stale_heat", false, 2000));
    });
    EXPECT_EQ(count(log, STALE_LOG), 1u) << log;
}

//What the drivers do with a reading equal to the last one: nothing is
//signalled, the thermostat finds it on its own.
TEST_F(InternalThermostatSafetyTest, ARepeatedReadingKeepsTheSensorFresh)
{
    load();
    auto *sensor = ListeRoom::Instance().findIOAs<TestSensor>("quiet_temp");
    ASSERT_NE(sensor, nullptr);
    mode("quiet", "heat");
    sensor->deliver(18);
    ASSERT_TRUE(on("quiet_heat"));

    for (int i = 0; i < 12; i++)
    {
        pumpFor(50);
        sensor->repeat();
        ASSERT_TRUE(on("quiet_heat")) << "after " << i + 1 << " repeats";
    }

    ASSERT_TRUE(waitFor("quiet_heat", false, 10000));
    const auto t0 = Clock::now();
    sensor->repeat();
    EXPECT_TRUE(waitFor("quiet_heat", true, 5000));
    //Found by the periodic check, within a sensor_timeout or two: the bound only
    //tells it is not left to the next reading.
    EXPECT_LE(msSince(t0), 3000);
}

const char *const DEW_LOG = "Device(dew): humidity 'dew_hum' reads 70, at or above humidity_max 65, "
                            "output_cool is OFF";

//Spec §8.2, floor cooling below the dew point wets the floor.
TEST_F(InternalThermostatSafetyTest, HumidityAtTheMaximumCutsTheCoolingAtOnce)
{
    load();
    set("dew_hum", 50);
    mode("dew", "cool");
    set("dew_temp", 25);
    ASSERT_TRUE(on("dew_cool"));

    const std::string log = journal([]()
                                    {
        set("dew_hum", 70);
    });
    EXPECT_FALSE(on("dew_cool"));
    EXPECT_EQ(str("dew_action"), "idle");
    EXPECT_EQ(count(log, DEW_LOG), 1u) << log;

    set("dew_hum", 65);
    pumpFor(20);
    EXPECT_FALSE(on("dew_cool"));

    //Heating does not care.
    mode("dew", "heat");
    set("dew_temp", 18);
    EXPECT_TRUE(on("dew_heat"));
}

TEST_F(InternalThermostatSafetyTest, CoolingResumesBelowTheHumidityMaximum)
{
    load();
    set("dew_hum", 70);
    mode("dew", "cool");
    set("dew_temp", 25);
    ASSERT_FALSE(on("dew_cool"));

    set("dew_hum", 64.5);
    EXPECT_TRUE(on("dew_cool"));
    EXPECT_EQ(str("dew_action"), "cooling");
}

TEST_F(InternalThermostatSafetyTest, CoolingWaitsForAHumidityReading)
{
    load();
    ASSERT_FALSE(io("dew_hum")->hasValue());
    mode("dew", "cool");
    set("dew_temp", 25);
    pumpFor(20);
    EXPECT_FALSE(on("dew_cool"));

    set("dew_hum", 50);
    EXPECT_TRUE(on("dew_cool"));
}

TEST_F(InternalThermostatSafetyTest, ASilentHumiditySensorCutsTheCooling)
{
    load();
    //Before the humidity reading: the timeout runs from it.
    const auto t0 = Clock::now();
    set("dewst_hum", 50);
    mode("dewst", "cool");
    set("dewst_temp", 25);
    ASSERT_TRUE(on("dewst_cool"));

    //current_temp keeps reading, humidity does not.
    const std::string log = journal([&]()
                                    {
        while (on("dewst_cool") && msSince(t0) < 10000)
        {
            set("dewst_temp", 25);
            pumpFor(50);
        }
    });
    EXPECT_FALSE(on("dewst_cool"));
    EXPECT_GE(msSince(t0), 590);
    EXPECT_EQ(count(log, "Device(dewst): humidity 'dewst_hum' has sent nothing for 0.6 s, "
                         "output_cool is OFF"),
              1u)
        << log;

    set("dewst_hum", 50);
    EXPECT_TRUE(on("dewst_cool"));
}

//Decision 22: without a humidity link a floor cools, the condensation being
//the installation's business.
TEST_F(InternalThermostatSafetyTest, FloorCoolingWithoutHumidityRuns)
{
    const std::string log = load();
    EXPECT_FALSE(has(log, "are refused")) << log;
    EXPECT_EQ(modes("nohum"), (std::vector<std::string>{ "off", "heat", "cool", "auto" }));
    EXPECT_EQ(dev("nohum")->deviceObject().at("capabilities").at("modes"), Json({ "off", "heat", "cool", "auto" }));
    //Left ON before the restart, it waits for a reading like any output.
    EXPECT_FALSE(on("nohum_cool"));

    EXPECT_TRUE(mode("nohum", "cool"));
    set("nohum_temp", 25);
    EXPECT_TRUE(on("nohum_cool"));
    EXPECT_EQ(str("nohum_action"), "cooling");
    EXPECT_FALSE(dev("nohum")->deviceObject().at("state").contains("fault"));
}

/* The server may restart while a relay is ON, or a few seconds after it went
 * OFF: the start counts as a switch OFF, so min_off_time passes before the
 * first ON. */
TEST_F(InternalThermostatSafetyTest, TheFirstSwitchOnWaitsForMinOffTimeFromTheStart)
{
    const auto t0 = Clock::now();
    load();
    mode("start", "heat");
    set("start_temp", 18);
    EXPECT_FALSE(on("start_heat"));

    ASSERT_TRUE(waitFor("start_heat", true, 2000));
    EXPECT_GE(msSince(t0), 390);
    EXPECT_EQ(str("start_action"), "heating");
}

/* A sensor whose driver drops a repeated value without dating it looks
 * silent once steady for sensor_timeout: said at load, so that the timeout
 * can be raised. */
TEST_F(InternalThermostatSafetyTest, ASensorThatOnlyReportsChangesIsLoggedAtLoad)
{
    const std::string log = load();
    EXPECT_TRUE(has(log, "Device(chg): current_temp 'chg_temp' (TestChangeSensor) only reports "
                         "changes: steady for 9000 s, it counts as silent"))
        << log;
    EXPECT_TRUE(has(log, "Device(chg): humidity 'chg_hum' (TestChangeSensor) only reports "
                         "changes: steady for 9000 s, it counts as silent"))
        << log;
    EXPECT_EQ(count(log, "only reports changes"), 2u) << log;
}

//It runs auto, so it offers the range.
TEST_F(InternalThermostatSafetyTest, FloorCoolingWithoutHumidityOffersTheRange)
{
    load();
    const Json caps = dev("nohum")->deviceObject().at("capabilities");
    EXPECT_TRUE(caps.contains("target_temp_range")) << caps.dump();
    EXPECT_TRUE(dev("nohum")->set_value(std::string(R"({"target_temp_low":18,"target_temp_high":24})")));
    EXPECT_EQ(io("nohum_low")->get_value_double(), 18);
}

/* A device can say it is gone (ESPHome's last will, a Zigbee availability
 * topic) while its last value stays: the sensor is not fresh then, whatever
 * it delivers, until it says it is back. */
TEST_F(InternalThermostatSafetyTest, ASensorThatSaysItIsDisconnectedIsNotFresh)
{
    load();
    mode("boot", "heat");
    set("boot_temp", 18);
    ASSERT_TRUE(on("boot_heat"));

    const std::string log = journal([]()
                                    {
        io("boot_temp")->setStatusInfo(IOBase::StatusType::Connected, IOBase::StatusConnected::STATUS_DISCONNECTED);
        set("boot_target", 22);
    });
    EXPECT_FALSE(on("boot_heat"));
    EXPECT_EQ(count(log, "Device(boot): current_temp 'boot_temp' reports itself disconnected, "
                         "the outputs are OFF"),
              1u)
        << log;

    set("boot_temp", 18);
    EXPECT_FALSE(on("boot_heat"));

    io("boot_temp")->setStatusInfo(IOBase::StatusType::Connected, IOBase::StatusConnected::STATUS_CONNECTED);
    set("boot_temp", 18);
    EXPECT_TRUE(on("boot_heat"));
}

/* A reading outside valid_min..valid_max is no valid reading. Both
 * bounds belong to the valid range. */
TEST_F(InternalThermostatSafetyTest, TheBoundsAreInclusive)
{
    load();
    mode("lo", "heat");
    mode("hi", "heat");
    set("lo_target", 40);

    //Heating wanted well below 40: ON at the bound, cut just under it.
    set("lo_temp", 10);
    EXPECT_TRUE(on("lo_heat"));
    EXPECT_EQ(fault("lo"), "");
    set("lo_temp", 9);
    EXPECT_FALSE(on("lo_heat"));
    EXPECT_EQ(fault("lo"), "sensor_out_of_range");

    //A single bound is a check by itself, and the other side is open.
    set("lo_temp", 200);
    EXPECT_EQ(fault("lo"), "");
    EXPECT_FALSE(on("lo_heat"));

    set("hi_temp", 30);
    EXPECT_EQ(fault("hi"), "");
    set("hi_temp", 31);
    EXPECT_EQ(fault("hi"), "sensor_out_of_range");
    set("hi_temp", -50);
    EXPECT_EQ(fault("hi"), "");
}

//The safety stop does not wait for min_on_time, the restart waits for min_off_time.
TEST_F(InternalThermostatSafetyTest, AnImplausibleReadingStopsTheOutputsAtOnce)
{
    load();
    mode("rng", "heat");
    set("rng_temp", 10);
    ASSERT_TRUE(waitFor("rng_heat", true, 5000)) << "min_off_time passes first";
    EXPECT_EQ(str("rng_action"), "heating");

    const std::string log = journal([]()
                                    {
        set("rng_temp", 41);
        //Repeated and changing excesses are one episode.
        set("rng_temp", 90);
    });
    EXPECT_FALSE(on("rng_heat")) << "min_on_time is 5 s";
    EXPECT_EQ(str("rng_action"), "idle");
    EXPECT_EQ(fault("rng"), "sensor_out_of_range");
    EXPECT_EQ(count(log, "Device(rng): current_temp 'rng_temp' reads 41, outside the valid range, "
                         "the outputs are OFF"),
              1u)
        << log;
    EXPECT_EQ(count(log, "outside the valid range"), 1u) << log;

    //A setpoint and a mode are still accepted, and change nothing now.
    EXPECT_TRUE(dev("rng")->set_value(std::string(R"({"mode":"heat","target_temp":25})")));
    EXPECT_EQ(io("rng_target")->get_value_double(), 25);
    EXPECT_FALSE(on("rng_heat"));

    //The first valid reading ends it; the lock-out of the stop still applies.
    const std::string back = journal([]()
                                     {
        set("rng_temp", 12);
    });
    EXPECT_EQ(fault("rng"), "");
    EXPECT_TRUE(has(back, "Device(rng): current_temp 'rng_temp' reads 12, within the valid range again, "
                          "regulation resumes"))
        << back;
    EXPECT_FALSE(on("rng_heat"));
    EXPECT_TRUE(waitFor("rng_heat", true, 5000));
}

//A silent or disconnected sensor says so, whatever its last value was.
TEST_F(InternalThermostatSafetyTest, AnImplausibleReadingYieldsToStalenessAndDisconnection)
{
    load();
    mode("rngst", "heat");
    EXPECT_EQ(fault("rngst"), "sensor_waiting");
    set("rngst_temp", 60);
    EXPECT_EQ(fault("rngst"), "sensor_out_of_range");

    //Silent: the fault changes by itself, then an implausible reading is back.
    ASSERT_TRUE(pumpUntil([]()
                          {
        return fault("rngst") == "sensor_stale";
    }));
    //Back out of bounds: it does not say that regulation resumes.
    const std::string again = journal([]()
                                      {
        set("rngst_temp", 60);
    });
    EXPECT_EQ(fault("rngst"), "sensor_out_of_range");
    EXPECT_FALSE(has(again, "regulation resumes")) << again;
    EXPECT_TRUE(has(again, "outside the valid range")) << again;
    set("rngst_temp", 15);
    EXPECT_EQ(fault("rngst"), "");

    io("rngst_temp")->setStatusInfo(IOBase::StatusType::Connected, IOBase::StatusConnected::STATUS_DISCONNECTED);
    set("rngst_temp", 60);
    EXPECT_EQ(fault("rngst"), "sensor_disconnected");
    io("rngst_temp")->setStatusInfo(IOBase::StatusType::Connected, IOBase::StatusConnected::STATUS_CONNECTED);
    set("rngst_temp", 60);
    EXPECT_EQ(fault("rngst"), "sensor_out_of_range");
    EXPECT_FALSE(on("rngst_heat"));
}

//Mode off holds the outputs by itself: no fault to report.
TEST_F(InternalThermostatSafetyTest, ModeOffHidesTheImplausibleReading)
{
    load();
    set("rng_temp", 90);
    EXPECT_EQ(fault("rng"), "");
    mode("rng", "heat");
    EXPECT_EQ(fault("rng"), "sensor_out_of_range");
}

TEST_F(InternalThermostatSafetyTest, UnusableBoundsAreLoggedAndIgnored)
{
    const std::string log = load();
    EXPECT_TRUE(has(log, "Device(rngbad): valid_min 'warm' is not a number, it is ignored")) << log;
    EXPECT_TRUE(has(log, "Device(rngswap): valid_min 30 is above valid_max 10, both are ignored")) << log;

    mode("rngbad", "heat");
    set("rngbad_temp", -50);
    EXPECT_EQ(fault("rngbad"), "");
    set("rngbad_temp", 31);
    EXPECT_EQ(fault("rngbad"), "sensor_out_of_range");

    mode("rngswap", "heat");
    set("rngswap_temp", 20);
    EXPECT_EQ(fault("rngswap"), "");
    set("rngswap_temp", 500);
    EXPECT_EQ(fault("rngswap"), "");
}

//Mode off holds the outputs by itself: the excess is not an event, until the mode asks for the sensor.
TEST_F(InternalThermostatSafetyTest, AnImplausibleReadingIsOnlyLoggedOnceTheModeNeedsTheSensor)
{
    load();
    const std::string off = journal([]()
                                    {
        set("rng_temp", 90);
    });
    EXPECT_FALSE(has(off, "outside the valid range")) << off;

    const std::string heat = journal([]()
                                     {
        mode("rng", "heat");
    });
    EXPECT_EQ(count(heat, "outside the valid range"), 1u) << heat;
}

//A reading that is not a number is no reading, bounds or not: it would compare false to
//every threshold and leave an ON output held.
TEST_F(InternalThermostatSafetyTest, ANonFiniteReadingIsNoReadingWithoutBounds)
{
    load();
    auto *sensor = ListeRoom::Instance().findIOAs<TestSensor>("quiet_temp");
    ASSERT_NE(sensor, nullptr);
    mode("quiet", "heat");
    sensor->deliver(18);
    ASSERT_TRUE(on("quiet_heat"));

    sensor->deliver(std::numeric_limits<double>::quiet_NaN());
    EXPECT_FALSE(on("quiet_heat"));
    EXPECT_EQ(fault("quiet"), "sensor_out_of_range");

    sensor->deliver(std::numeric_limits<double>::infinity());
    EXPECT_EQ(fault("quiet"), "sensor_out_of_range");
    sensor->deliver(18);
    EXPECT_EQ(fault("quiet"), "");
    EXPECT_TRUE(on("quiet_heat"));
}
