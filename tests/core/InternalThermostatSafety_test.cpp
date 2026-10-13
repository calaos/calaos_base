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
    ios += thermostat("quiet", false, param("sensor_timeout", "0.2"),
                      "    <calaos:input type=\"TestSensor\" id=\"quiet_temp\" name=\"quiet_temp\" />\n");
    //Floor cooling: cut at 65 % humidity, without waiting for min_on_time.
    ios += thermostat("dew", true,
                      param("emitter", "floor") + link("humidity", "dew_hum") +
                          param("humidity_max", "65") + param("min_on_time", "5"));
    ios += internal("InternalInt", "dew_hum");
    ios += thermostat("dewst", true,
                      param("emitter", "floor") + link("humidity", "dewst_hum") +
                          param("sensor_timeout", "0.3"));
    ios += internal("InternalInt", "dewst_hum");
    //Floor cooling without a humidity link, its output_cool left ON.
    ios += thermostat("nohum", true, param("emitter", "floor"));
    ios += thermostat("fancoil", true, param("emitter", "fan_coil"));
    ios += thermostat("start", false, param("min_off_time", "0.4"));
    return ioXmlDocument(roomXml("Salon", "salon", ios));
}

struct Thermostat
{
    std::string id;
    bool cool;
};

//Every thermostat of the house: their saved IOs are reset before each case.
const std::vector<Thermostat> THERMOSTATS = { { "boot", false }, { "stale", false }, { "quiet", false }, { "dew", true }, { "dewst", true }, { "nohum", true }, { "fancoil", true }, { "start", false } };

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
    set("stale_temp", 18);
    ASSERT_TRUE(on("stale_heat"));

    const auto t0 = Clock::now();
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
    mode("stale", "heat");
    for (int i = 0; i < 8; i++)
    {
        set("stale_temp", 18 + (i % 2) * 0.1);
        pumpFor(100);
        ASSERT_TRUE(on("stale_heat")) << "after " << i + 1 << " readings";
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

    ASSERT_TRUE(waitFor("quiet_heat", false, 2000));
    const auto t0 = Clock::now();
    sensor->repeat();
    EXPECT_TRUE(waitFor("quiet_heat", true, 1000));
    //Found by the periodic check, at most one sensor_timeout later.
    EXPECT_LE(msSince(t0), 400);
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
    set("dewst_hum", 50);
    mode("dewst", "cool");
    set("dewst_temp", 25);
    ASSERT_TRUE(on("dewst_cool"));

    //current_temp keeps reading, humidity does not.
    const auto t0 = Clock::now();
    const std::string log = journal([&]()
                                    {
        while (on("dewst_cool") && msSince(t0) < 2000)
        {
            set("dewst_temp", 25);
            pumpFor(50);
        }
    });
    EXPECT_FALSE(on("dewst_cool"));
    EXPECT_GE(msSince(t0), 290);
    EXPECT_EQ(count(log, "Device(dewst): humidity 'dewst_hum' has sent nothing for 0.3 s, "
                         "output_cool is OFF"),
              1u)
        << log;

    set("dewst_hum", 50);
    EXPECT_TRUE(on("dewst_cool"));
}

//Spec §8.2: without a humidity link nothing guards the floor against
//condensation, so it cannot cool at all.
TEST_F(InternalThermostatSafetyTest, FloorCoolingWithoutHumidityIsRefusedAtLoad)
{
    const std::string log = load();
    EXPECT_TRUE(has(log, "Device(nohum): emitter=floor without a humidity link, cool and auto "
                         "are refused"))
        << log;
    EXPECT_EQ(modes("nohum"), (std::vector<std::string>{ "off", "heat" }));
    EXPECT_EQ(dev("nohum")->deviceObject().at("capabilities").at("modes"), Json({ "off", "heat" }));
    EXPECT_FALSE(mode("nohum", "cool"));
    EXPECT_FALSE(mode("nohum", "auto"));
    //Still its output: left ON before the restart, it is switched OFF.
    EXPECT_FALSE(on("nohum_cool"));

    //Only a floor needs it.
    EXPECT_FALSE(has(log, "Device(fancoil)")) << log;
    EXPECT_EQ(modes("fancoil"), (std::vector<std::string>{ "off", "heat", "cool", "auto" }));
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
