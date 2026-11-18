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

//The schedule program on a ProxyClimate (docs/17_device_templates.md,
//"Programme horaire des climates"): the effective setpoint reaches the
//appliance through its command_io, which a fake transport records.

#include "CalaosCoreFixture.h"
#include "IO/InPlageHoraire.h"
#include "IOChangeHub.h"
#include "ProxyClimate.h"
#include "libuvw.h"

#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

using Clock = std::chrono::steady_clock;

struct Write
{
    std::string id;
    std::string text;
    double number = 0;
};

std::vector<Write> &writes()
{
    static std::vector<Write> w;
    return w;
}

} //namespace

//A broker output: remembers and echoes what it is given.
class ProgRecordingOutput: public IOBase
{
public:
    ProgRecordingOutput(Params &p):
        IOBase(p, IOBase::IO_OUTPUT)
    {
    }

    DATA_TYPE get_type() override { return TSTRING; }
    std::string get_value_string() override { return text; }
    double get_value_double() override { return number; }

    bool set_value(std::string val) override
    {
        text = val;
        record();
        return true;
    }

    bool set_value(double val) override
    {
        number = val;
        record();
        return true;
    }

private:
    void record()
    {
        writes().push_back({ get_param("id"), text, number });
        EmitSignalIO();
    }

    std::string text;
    double number = 0;
};

//A broker input: the test delivers what the appliance reported.
class ProgFeedInput: public IOBase
{
public:
    ProgFeedInput(Params &p):
        IOBase(p, IOBase::IO_INPUT)
    {
    }

    DATA_TYPE get_type() override { return TSTRING; }
    std::string get_value_string() override { return text; }
    double get_value_double() override { return number; }
    bool datesEveryReading() override { return true; }
    bool set_value(std::string) override { return false; }
    bool set_value(double) override { return false; }

    void deliver(const std::string &v)
    {
        text = v;
        EmitSignalIO();
    }

    void deliver(double v)
    {
        number = v;
        EmitSignalIO();
    }

private:
    std::string text;
    double number = 0;
};

REGISTER_IO(ProgRecordingOutput)
REGISTER_IO(ProgFeedInput)

namespace
{

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

std::string element(const std::string &kind, const std::string &type, const std::string &id,
                    const std::string &extra = std::string())
{
    return "      <calaos:" + kind + " type=\"" + type + "\" id=\"" + id + "\" name=\"" + id +
           "\" visible=\"false\"" + extra + " />\n";
}

std::string in(const std::string &id) { return element("input", "ProgFeedInput", id); }

std::string out(const std::string &id, const std::string &extra = std::string())
{
    return element("output", "ProgRecordingOutput", id, extra);
}

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

const std::string BOUNDS_16_30 = R"( value_min="16" value_max="30" step="0.5")";

std::string device(const std::string &id, const std::string &inner)
{
    return "    <calaos:device id=\"" + id + "\" type=\"climate\" impl=\"proxy\" name=\"" + id +
           "\">\n" + inner + "    </calaos:device>\n";
}

//Mode, a single setpoint, and a low/high pair read back from inputs.
std::string pairDevice(const std::string &id, const std::string &params)
{
    return device(id, in(id + "_mode") + out(id + "_mode_cmd") + out(id + "_target", BOUNDS_16_30) +
                          in(id + "_low") + out(id + "_low_cmd", BOUNDS_16_30) + in(id + "_high") +
                          out(id + "_high_cmd", BOUNDS_16_30) +
                          link("mode", id + "_mode", id + "_mode_cmd") +
                          link("target_temp", id + "_target") +
                          link("target_temp_low", id + "_low", id + "_low_cmd") +
                          link("target_temp_high", id + "_high", id + "_high_cmd") +
                          param("min_range", "2") + params);
}

//A single setpoint and a room sensor: auto only through emulate_range.
std::string singleDevice(const std::string &id, const std::string &params)
{
    return device(id, in(id + "_mode") + out(id + "_mode_cmd") + out(id + "_target", BOUNDS_16_30) +
                          in(id + "_current") + link("mode", id + "_mode", id + "_mode_cmd") +
                          link("target_temp", id + "_target") + link("current_temp", id + "_current") +
                          params);
}

const std::string LEVELS = param("heat_levels", "2") + param("cool_levels", "1");

std::string house()
{
    std::string ios;
    ios += pairDevice("pg", LEVELS);
    ios += pairDevice("po", LEVELS + param("cool_default_off", "true"));
    ios += pairDevice("pn", std::string());
    ios += singleDevice("ps", param("heat_levels", "1") + param("cool_levels", "1"));
    ios += singleDevice("pe", param("heat_levels", "1") + param("cool_levels", "1") +
                                  param("emulate_range", "true") + param("min_range", "2"));
    return ioXmlDocument(roomXml("Salon", "salon", ios));
}

} //namespace

class ProxyClimateProgramTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        writes().clear();
        loadConfig(house(), rulesXmlDocument(""));
        //The start waits for the loop.
        pumpFor(20);
        writes().clear();
    }

    void TearDown() override
    {
        CoreFixture::TearDown();
        pumpFor(5);
    }

    static ProgFeedInput *feed(const std::string &id)
    {
        return ListeRoom::Instance().findIOAs<ProgFeedInput>(id);
    }

    static void mode(const std::string &id, const std::string &m)
    {
        feed(id + "_mode")->deliver(m);
        pumpFor(5);
    }

    //The appliance's own mode change is no trigger: the program applies at the next one.
    static void trigger(const std::string &id)
    {
        io(id + "_program_enabled")->set_value(false);
        pumpFor(5);
        io(id + "_program_enabled")->set_value(true);
        pumpFor(5);
    }

    //The appliance published its bounds.
    static void publishPair(const std::string &id, double low, double high)
    {
        feed(id + "_low")->deliver(low);
        feed(id + "_high")->deliver(high);
        pumpFor(5);
    }

    static std::vector<Write> writesTo(const std::string &id)
    {
        std::vector<Write> res;
        for (const Write &w: writes())
        {
            if (w.id == id)
                res.push_back(w);
        }
        return res;
    }

    static double lastNumber(const std::string &id)
    {
        const auto w = writesTo(id);
        return w.empty() ? -1000 : w.back().number;
    }

    static void setLevel(const std::string &id, double v)
    {
        io(id)->set_value(v);
        pumpFor(5);
    }

    static void activate(const std::string &id, bool on = true)
    {
        auto *r = dynamic_cast<InPlageHoraire *>(io(id));
        ASSERT_NE(r, nullptr) << id;
        r->clear();
        if (on)
        {
            TimeRange t;
            t.shour = "0";
            t.smin = "0";
            t.ssec = "0";
            t.ehour = "23";
            t.emin = "59";
            t.esec = "59";
            r->AddMonday(t);
            r->AddTuesday(t);
            r->AddWednesday(t);
            r->AddThursday(t);
            r->AddFriday(t);
            r->AddSaturday(t);
            r->AddSunday(t);
        }
        r->hasChanged();
        pumpFor(5);
    }
};

TEST_F(ProxyClimateProgramTest, ProgramIosAreCreatedFromTheParams)
{
    for (const char *role: { "heat_level1", "heat_level1_setpoint", "heat_level2", "heat_level2_setpoint",
                             "cool_level1", "cool_level1_setpoint", "heat_default", "cool_default",
                             "cool_default_off", "program_enabled" })
        EXPECT_NE(io(std::string("pg_") + role), nullptr) << role;
    EXPECT_EQ(io("pg_cool_level2"), nullptr);
    //The setpoint bounds are those of the appliance's setpoint.
    EXPECT_EQ(io("pg_heat_default")->get_param("value_min"), "16");
    EXPECT_EQ(io("pg_heat_default")->get_param("value_max"), "30");
}

TEST_F(ProxyClimateProgramTest, NoLevelsNoProgram)
{
    EXPECT_EQ(io("pn_program_enabled"), nullptr);
    EXPECT_EQ(io("pn_heat_default"), nullptr);
    mode("pn", "heat");
    EXPECT_TRUE(writes().empty());
}

TEST_F(ProxyClimateProgramTest, HeatWritesTheDefaultThenTheLevel)
{
    mode("pg", "heat");
    trigger("pg");
    ASSERT_EQ(writesTo("pg_target").size(), 1u);
    EXPECT_EQ(lastNumber("pg_target"), 17);

    setLevel("pg_heat_level2_setpoint", 21);
    activate("pg_heat_level2");
    EXPECT_EQ(lastNumber("pg_target"), 21);

    activate("pg_heat_level2", false);
    EXPECT_EQ(lastNumber("pg_target"), 17);
    EXPECT_TRUE(writesTo("pg_low_cmd").empty());
    EXPECT_TRUE(writesTo("pg_high_cmd").empty());
}

TEST_F(ProxyClimateProgramTest, CoolWritesTheCoolSet)
{
    mode("pg", "cool");
    trigger("pg");
    EXPECT_EQ(lastNumber("pg_target"), 26);

    setLevel("pg_cool_level1_setpoint", 24);
    activate("pg_cool_level1");
    EXPECT_EQ(lastNumber("pg_target"), 24);
}

TEST_F(ProxyClimateProgramTest, CoolOffWritesTheHighestSetpoint)
{
    mode("po", "cool");
    trigger("po");
    EXPECT_EQ(lastNumber("po_target"), 30);

    publishPair("po", 18, 24);
    mode("po", "heat_cool");
    trigger("po");
    EXPECT_EQ(lastNumber("po_high_cmd"), 30);
}

TEST_F(ProxyClimateProgramTest, AutoWritesTheLowHighPair)
{
    publishPair("pg", 18, 24);
    mode("pg", "heat_cool");
    trigger("pg");
    ASSERT_EQ(writesTo("pg_low_cmd").size(), 1u);
    ASSERT_EQ(writesTo("pg_high_cmd").size(), 1u);
    EXPECT_EQ(lastNumber("pg_low_cmd"), 17 < 16 ? 16 : 17);
    EXPECT_EQ(lastNumber("pg_high_cmd"), 26);
    EXPECT_TRUE(writesTo("pg_target").empty());
}

TEST_F(ProxyClimateProgramTest, AutoKeepsTheGap)
{
    publishPair("pg", 18, 24);
    mode("pg", "heat_cool");
    setLevel("pg_heat_level1_setpoint", 25);
    activate("pg_heat_level1");
    EXPECT_EQ(lastNumber("pg_low_cmd"), 25);
    EXPECT_GE(lastNumber("pg_high_cmd") - lastNumber("pg_low_cmd"), 2);
}

TEST_F(ProxyClimateProgramTest, AutoWaitsForTheBoundsOfTheAppliance)
{
    mode("pg", "heat_cool");
    EXPECT_TRUE(writesTo("pg_low_cmd").empty());
    EXPECT_TRUE(writesTo("pg_high_cmd").empty());

    publishPair("pg", 18, 24);
    activate("pg_heat_level1");
    EXPECT_EQ(writesTo("pg_low_cmd").size(), 1u);
    EXPECT_EQ(writesTo("pg_high_cmd").size(), 1u);
}

TEST_F(ProxyClimateProgramTest, AutoWithoutAPairWritesNothing)
{
    mode("ps", "heat_cool");
    activate("ps_heat_level1");
    EXPECT_TRUE(writesTo("ps_target").empty());
}

TEST_F(ProxyClimateProgramTest, DisabledProgramWritesNothing)
{
    io("pg_program_enabled")->set_value(false);
    pumpFor(5);
    writes().clear();

    mode("pg", "heat");
    setLevel("pg_heat_default", 20);
    activate("pg_heat_level1");
    EXPECT_TRUE(writes().empty());

    io("pg_program_enabled")->set_value(true);
    pumpFor(5);
    EXPECT_EQ(writesTo("pg_target").size(), 1u);
}

TEST_F(ProxyClimateProgramTest, AWriteFromTheApplianceDoesNotDisableTheProgram)
{
    mode("pg", "heat");
    setLevel("pg_heat_level1_setpoint", 21);
    activate("pg_heat_level1");
    EXPECT_EQ(lastNumber("pg_target"), 21);
    const size_t before = writes().size();

    //The remote control moves the setpoint of the unit.
    io("pg_target")->set_value(23.5);
    pumpFor(20);
    EXPECT_TRUE(io("pg_program_enabled")->get_value_bool());
    EXPECT_EQ(writes().size(), before + 1);

    //The next level change applies the program again.
    activate("pg_heat_level2");
    activate("pg_heat_level1", false);
    EXPECT_NE(lastNumber("pg_target"), 23.5);
}

TEST_F(ProxyClimateProgramTest, EmulatedAutoWritesTheEmulatedPair)
{
    mode("pe", "heat_cool");
    trigger("pe");
    pumpFor(20);
    IOBase *low = nullptr;
    IOBase *high = nullptr;
    for (const Device::Link &l: ListeRoom::Instance().findIOAs<ProxyClimate>("pe")->getLinks())
    {
        if (l.name == "target_temp_low")
            low = io(l.io);
        if (l.name == "target_temp_high")
            high = io(l.io);
    }
    ASSERT_NE(low, nullptr);
    ASSERT_NE(high, nullptr);
    EXPECT_EQ(low->get_value_double(), 17);
    EXPECT_EQ(high->get_value_double(), 26);
    EXPECT_TRUE(writesTo("pe_target").empty());
}
