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

//ClimateBase: mode/action names, capabilities derived from the links (spec
//§6.3), the state object, and command validation including the low/high pair
//checked together against min_range.

#include "CalaosCoreFixture.h"
#include "ClimateBase.h"
#include "IOChangeHub.h"
#include "InputTemp.h"

#include <string>
#include <utility>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

//The base as is: what a proxy-like impl inherits.
class TestClimate: public ClimateBase
{
public:
    TestClimate(Params &p):
        ClimateBase(p)
    {
    }

    using ClimateBase::writeRangeBound;
};

//An impl that stores its mode and lets its outputs decide the modes, as the
//thermostat will.
class TestThermostat: public ClimateBase
{
public:
    TestThermostat(Params &p):
        ClimateBase(p)
    {
        documentCommand({ ClimateMode::Off, ClimateMode::Heat }, "", " note_x");
    }

    vector<ClimateMode> supportedModes() override
    {
        if (linkIo("output_cool"))
            return { ClimateMode::Off, ClimateMode::Heat, ClimateMode::Cool, ClimateMode::Auto };
        return { ClimateMode::Off, ClimateMode::Heat };
    }

protected:
    bool currentMode(ClimateMode &m) override
    {
        m = mode;
        return true;
    }

    bool writeMode(ClimateMode m) override
    {
        mode = m;
        return true;
    }

private:
    ClimateMode mode = ClimateMode::Off;
};

} //namespace

//An input: every write is refused, as with a link misconfigured on a sensor.
class TestReadOnly: public IOBase
{
public:
    TestReadOnly(Params &p):
        IOBase(p, IOBase::IO_INPUT)
    {
    }

    DATA_TYPE get_type() override { return TSTRING; }
};

//Reads like the temperature drivers: a reading equal to the current value is
//not signalled.
class TestTemp: public InputTemp
{
public:
    TestTemp(Params &p):
        InputTemp(p)
    {
    }

    void deliver(double v)
    {
        reading = v;
        readValue();
    }

protected:
    void readValue() override
    {
        if (reading != value)
        {
            value = reading;
            emitChange();
        }
    }

private:
    double reading = 0;
};

REGISTER_IO(TestTemp)
REGISTER_DEVICE(test_climate, TestClimate)
REGISTER_DEVICE(test_thermostat, TestThermostat)
REGISTER_IO(TestReadOnly)

namespace
{

const char *const EXT_TEMP = "ext_temp";

std::string linkXml(const std::string &name, const std::string &io,
                    const std::string &commandIo = std::string())
{
    std::string x = "      <calaos:link name=\"" + name + "\" io=\"" + io + "\"";
    if (!commandIo.empty())
        x += " command_io=\"" + commandIo + "\"";
    return x + " />\n";
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

std::string deviceXml(const std::string &id, const std::string &inner,
                      const std::string &impl = "test_climate")
{
    return "    <calaos:device id=\"" + id + "\" type=\"climate\" impl=\"" + impl +
           "\" name=\"" + id + "\">\n" + inner + "    </calaos:device>\n";
}

//A device with one owned IO per link role, each named <id>_<role>.
std::string singleLink(const std::string &id, const std::string &role, const std::string &type,
                       const std::string &extra = std::string())
{
    return deviceXml(id, ownedXml(type, id + "_" + role) + linkXml(role, id + "_" + role) + extra);
}

const std::string SETPOINT_16_30 = R"( value_min="16" value_max="30" step="0.5" unit="°C")";
const std::string SETPOINT_10_30 = R"( value_min="10" value_max="30" step="0.5")";

std::string house()
{
    std::string full;
    full += ownedXml("InternalString", "full_mode");
    full += ownedXml("InternalInt", "full_target", SETPOINT_16_30);
    full += ownedXml("InternalInt", "full_low", SETPOINT_10_30);
    full += ownedXml("InternalInt", "full_high", SETPOINT_10_30);
    full += ownedXml("InternalString", "full_action");
    full += ownedXml("InternalString", "full_fan");
    full += ownedXml("InternalString", "full_swing");
    full += ownedXml("InternalInt", "full_remote");
    full += linkXml("mode", "full_mode", "full_mode");
    full += linkXml("current_temp", EXT_TEMP);
    full += linkXml("target_temp", "full_target");
    full += linkXml("target_temp_low", "full_low");
    full += linkXml("target_temp_high", "full_high");
    full += linkXml("action", "full_action");
    full += linkXml("fan_mode", "full_fan", "full_fan");
    full += linkXml("swing_mode", "full_swing");
    full += linkXml("remote_temp", "full_remote");
    full += linkXml("remote_temp_source", EXT_TEMP);
    full += paramXml("modes", "auto, heat,off,cool");
    full += paramXml("fan_modes", "auto, low ,high,low");
    full += paramXml("swing_modes", "off,vertical");
    full += paramXml("min_range", "2");

    //Reads from one IO, writes to another with its own bounds.
    std::string split;
    split += ownedXml("InternalString", "split_mode");
    split += ownedXml("InternalString", "split_mode_cmd");
    split += ownedXml("InternalInt", "split_target");
    split += ownedXml("InternalInt", "split_target_cmd",
                      R"( value_min="18" value_max="24" step="1" unit="°F")");
    split += linkXml("mode", "split_mode", "split_mode_cmd");
    split += linkXml("target_temp", "split_target", "split_target_cmd");

    //A 0.1 step: low/high differences carry a binary residue.
    const std::string fineStep = R"( value_min="10" value_max="30" step="0.1")";
    std::string fine = ownedXml("InternalInt", "fine_low", fineStep) +
                       ownedXml("InternalInt", "fine_high", fineStep) +
                       linkXml("target_temp_low", "fine_low") +
                       linkXml("target_temp_high", "fine_high") + paramXml("min_range", "2");

    //Configuration mistakes, logged at load.
    std::string bad = ownedXml("InternalInt", "bad_low") + ownedXml("InternalInt", "bad_high") +
                      ownedXml("InternalString", "bad_fan") + ownedXml("InternalString", "bad_mode") +
                      linkXml("mode", "bad_mode") + linkXml("target_temp_low", "bad_low") +
                      linkXml("target_temp_high", "bad_high") + linkXml("fan_mode", "bad_fan") +
                      paramXml("modes", "heat,heat_cool") + paramXml("min_range", "-1");

    //Empty modes list, and a min_range the bounds cannot hold.
    const std::string narrow = R"( value_min="20" value_max="21")";
    std::string bad2 = ownedXml("InternalString", "bad2_mode") + ownedXml("InternalInt", "bad2_low", narrow) +
                       ownedXml("InternalInt", "bad2_high", narrow) + linkXml("mode", "bad2_mode") +
                       linkXml("target_temp_low", "bad2_low") + linkXml("target_temp_high", "bad2_high") +
                       paramXml("modes", " , ") + paramXml("min_range", "5");

    //Every link on an IO that refuses writes.
    std::string ro;
    for (const char *role: { "mode", "target_temp", "target_temp_low", "target_temp_high", "fan_mode" })
    {
        ro += ownedXml("TestReadOnly", std::string("ro_") + role);
        ro += linkXml(role, std::string("ro_") + role);
    }
    ro += paramXml("fan_modes", "auto");

    //Nothing written yet, except a setpoint restored from its saved value.
    std::string fresh = ownedXml("InternalInt", "fresh_current") +
                        ownedXml("InternalInt", "fresh_target", R"( save="true" value="21")") +
                        ownedXml("InternalInt", "fresh_low") + linkXml("current_temp", "fresh_current") +
                        linkXml("target_temp", "fresh_target") + linkXml("target_temp_low", "fresh_low");

    //Range without a single setpoint: the dial goes from low's min to high's max.
    std::string skew = ownedXml("InternalInt", "skew_low", R"( value_min="10" value_max="25" step="0.5" unit="°F")") +
                       ownedXml("InternalInt", "skew_high", R"( value_min="15" value_max="30" step="1")") +
                       linkXml("target_temp_low", "skew_low") + linkXml("target_temp_high", "skew_high") +
                       paramXml("min_range", "3");

    std::string thermoHeat = ownedXml("InternalBool", "th_heat") + linkXml("output_heat", "th_heat");
    std::string thermoRev = ownedXml("InternalBool", "tr_heat") + ownedXml("InternalBool", "tr_cool") +
                            linkXml("output_heat", "tr_heat") + linkXml("output_cool", "tr_cool");

    std::string salon;
    salon += deviceXml("full", full);
    salon += deviceXml("split", split);
    salon += deviceXml("fine", fine);
    salon += deviceXml("bad", bad);
    salon += deviceXml("bad2", bad2);
    salon += deviceXml("ro", ro);
    salon += deviceXml("fresh", fresh);
    salon += deviceXml("skew", skew);
    salon += deviceXml("cached", linkXml("current_temp", "cached_temp"));
    salon += deviceXml("uncached", linkXml("current_temp", "uncached_temp"));
    salon += deviceXml("none", "");
    salon += singleLink("only_mode", "mode", "InternalString");
    salon += singleLink("only_current", "current_temp", "InternalInt");
    salon += singleLink("only_target", "target_temp", "InternalInt");
    salon += singleLink("only_low", "target_temp_low", "InternalInt");
    salon += singleLink("only_high", "target_temp_high", "InternalInt");
    salon += singleLink("only_action", "action", "InternalString");
    salon += singleLink("only_fan", "fan_mode", "InternalString", paramXml("fan_modes", "auto,quiet"));
    salon += singleLink("only_swing", "swing_mode", "InternalString", paramXml("swing_modes", "off,both"));
    salon += singleLink("only_remote", "remote_temp", "InternalInt");
    salon += singleLink("only_remote_source", "remote_temp_source", "InternalInt");
    salon += singleLink("only_humidity", "humidity", "InternalInt");
    salon += deviceXml("pair_range", ownedXml("InternalInt", "pr_low") + ownedXml("InternalInt", "pr_high") +
                                         linkXml("target_temp_low", "pr_low") +
                                         linkXml("target_temp_high", "pr_high"));
    salon += deviceXml("target_range", ownedXml("InternalInt", "tg_target") + ownedXml("InternalInt", "tg_low") +
                                           ownedXml("InternalInt", "tg_high") + linkXml("target_temp", "tg_target") +
                                           linkXml("target_temp_low", "tg_low") +
                                           linkXml("target_temp_high", "tg_high"));
    salon += deviceXml("pair_remote", ownedXml("InternalInt", "pm_remote") +
                                          linkXml("remote_temp", "pm_remote") +
                                          linkXml("remote_temp_source", EXT_TEMP));
    salon += deviceXml("dangling", linkXml("target_temp", "nowhere") + linkXml("mode", "nowhere_either"));
    salon += deviceXml("thermo_heat", thermoHeat, "test_thermostat");
    salon += deviceXml("thermo_rev", thermoRev, "test_thermostat");

    //Setpoint IOs whose bounds are partial or wrong.
    salon += singleLink("b_min_only", "target_temp", "InternalInt", "");
    salon += singleLink("b_max_only", "target_temp", "InternalInt", "");
    salon += singleLink("b_inverted", "target_temp", "InternalInt", "");
    salon += singleLink("b_bad_step", "target_temp", "InternalInt", "");

    std::string ios = salon;
    auto withAttrs = [&ios](const std::string &id, const std::string &attrs)
    {
        const std::string needle = "id=\"" + id + "\" name=\"" + id + "\" visible=\"false\"";
        ios.replace(ios.find(needle), needle.size(), needle + attrs);
    };
    withAttrs("b_min_only_target_temp", R"( value_min="40")");
    withAttrs("b_max_only_target_temp", R"( value_max="5")");
    withAttrs("b_inverted_target_temp", R"( value_min="30" value_max="10")");
    withAttrs("b_bad_step_target_temp", R"( step="0" value_min="abc")");

    return ioXmlDocument(roomXml("Salon", "salon", ios) +
                         roomXml("Kitchen", "kitchen",
                                 internalIoXml("InternalInt", EXT_TEMP, "Temp") +
                                     R"(    <calaos:input type="TestTemp" id="cached_temp" name="cached_temp" />
    <calaos:input type="TestTemp" id="uncached_temp" name="uncached_temp" />
)"));
}

Json parse(const char *s)
{
    return Json::parse(s);
}

} //namespace

class ClimateBaseTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig(house(), rulesXmlDocument(""));
    }

    static ClimateBase *dev(const std::string &id)
    {
        return ListeRoom::Instance().findIOAs<ClimateBase>(id);
    }

    static Json caps(const std::string &id) { return dev(id)->deviceObject().at("capabilities"); }
    static Json state(const std::string &id) { return dev(id)->deviceObject().at("state"); }

    static bool cmd(const std::string &id, const std::string &command)
    {
        return dev(id)->set_value(command);
    }

    static double num(const std::string &id) { return io(id)->get_value_double(); }
    static std::string str(const std::string &id) { return io(id)->get_value_string(); }

    static void setRange(double low, double high)
    {
        io("full_low")->set_value(low);
        io("full_high")->set_value(high);
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
};

TEST_F(ClimateBaseTest, ModeAndActionNamesAreTheEspHomeStrings)
{
    const std::vector<std::pair<ClimateMode, std::string>> modes = {
        { ClimateMode::Off, "off" },
        { ClimateMode::Heat, "heat" },
        { ClimateMode::Cool, "cool" },
        { ClimateMode::Auto, "auto" },
        { ClimateMode::Dry, "dry" },
        { ClimateMode::FanOnly, "fan_only" },
    };
    ASSERT_EQ(ClimateBase::allModes().size(), modes.size());
    for (size_t i = 0; i < modes.size(); i++)
    {
        EXPECT_EQ(ClimateBase::allModes()[i], modes[i].first);
        EXPECT_EQ(ClimateBase::modeName(modes[i].first), modes[i].second);
        ClimateMode m = ClimateMode::Off;
        EXPECT_TRUE(ClimateBase::parseMode(modes[i].second, m)) << modes[i].second;
        EXPECT_EQ(m, modes[i].first);
    }

    const std::vector<std::pair<ClimateAction, std::string>> actions = {
        { ClimateAction::Off, "off" },
        { ClimateAction::Idle, "idle" },
        { ClimateAction::Heating, "heating" },
        { ClimateAction::Cooling, "cooling" },
        { ClimateAction::Drying, "drying" },
        { ClimateAction::Fan, "fan" },
    };
    for (const auto &a: actions)
    {
        EXPECT_EQ(ClimateBase::actionName(a.first), a.second);
        ClimateAction parsed = ClimateAction::Off;
        EXPECT_TRUE(ClimateBase::parseAction(a.second, parsed)) << a.second;
        EXPECT_EQ(parsed, a.first);
    }

    ClimateMode m;
    for (const char *bad: { "Heat", "fan-only", "fanonly", "heat_cool", "", " off" })
        EXPECT_FALSE(ClimateBase::parseMode(bad, m)) << bad;
    ClimateAction a;
    for (const char *bad: { "Heating", "fan_only", "" })
        EXPECT_FALSE(ClimateBase::parseAction(bad, a)) << bad;
}

//Spec §6.3: a capability exists if and only if its link does.
TEST_F(ClimateBaseTest, EachLinkGivesExactlyItsCapability)
{
    const Json defaultTarget = { { "min", 7 }, { "max", 35 }, { "step", 0.5 }, { "unit", "°C" } };
    const std::vector<std::pair<std::string, Json>> rows = {
        { "none", Json::object() },
        { "only_mode", { { "modes", { "off", "heat", "cool", "auto", "dry", "fan_only" } } } },
        { "only_current", Json::object() },
        { "only_target", { { "target_temp", defaultTarget } } },
        { "only_low", Json::object() },
        { "only_high", Json::object() },
        { "pair_range",
          { { "target_temp", { { "min", 7 }, { "max", 35 }, { "step", 0.5 }, { "unit", "°C" }, { "min_range", 0 } } },
            { "target_temp_range", true } } },
        //min_range rides on target_temp, which this one has, 0 without the param.
        { "target_range",
          { { "target_temp", { { "min", 7 }, { "max", 35 }, { "step", 0.5 }, { "unit", "°C" }, { "min_range", 0 } } },
            { "target_temp_range", true } } },
        { "only_action", { { "reports_action", true } } },
        { "only_fan", { { "fan_modes", { "auto", "quiet" } } } },
        { "only_swing", { { "swing_modes", { "off", "both" } } } },
        { "only_remote", Json::object() },
        { "only_remote_source", Json::object() },
        { "pair_remote", { { "remote_temp", true } } },
        { "only_humidity", Json::object() },
        { "dangling", Json::object() },
    };
    for (const auto &row: rows)
    {
        ASSERT_NE(dev(row.first), nullptr) << row.first;
        EXPECT_EQ(caps(row.first), row.second) << row.first << ": " << caps(row.first).dump();
    }
}

TEST_F(ClimateBaseTest, FullDeviceCapabilitiesAreRealJsonTypes)
{
    const Json c = caps("full");
    EXPECT_EQ(c, parse(R"({
        "modes": ["off", "heat", "cool", "auto"],
        "target_temp": {"min": 16, "max": 30, "step": 0.5, "unit": "°C", "min_range": 2},
        "target_temp_range": true,
        "reports_action": true,
        "fan_modes": ["auto", "low", "high"],
        "swing_modes": ["off", "vertical"],
        "remote_temp": true
    })"))
        << c.dump();
    EXPECT_TRUE(c.at("target_temp").at("min").is_number());
    EXPECT_TRUE(c.at("target_temp_range").is_boolean());

    const Json o = dev("full")->deviceObject();
    EXPECT_EQ(o.at("family"), "climate");
    EXPECT_EQ(o.at("ios").at("target_temp"), "full_target");
    EXPECT_EQ(o.at("ios").at("current_temp"), EXT_TEMP);
}

TEST_F(ClimateBaseTest, ThermostatHookRestrictsTheModes)
{
    EXPECT_EQ(caps("thermo_heat"), parse(R"({"modes": ["off", "heat"]})"));
    EXPECT_EQ(caps("thermo_rev"), parse(R"({"modes": ["off", "heat", "cool", "auto"]})"));

    EXPECT_FALSE(cmd("thermo_heat", "cool"));
    EXPECT_FALSE(cmd("thermo_heat", "auto"));
    EXPECT_EQ(dev("thermo_heat")->get_value_string(), "off");
    EXPECT_TRUE(cmd("thermo_heat", "heat"));
    EXPECT_EQ(dev("thermo_heat")->get_value_string(), "heat");
    EXPECT_EQ(state("thermo_heat"), parse(R"({"mode": "heat"})"));

    EXPECT_TRUE(cmd("thermo_rev", R"({"mode":"auto"})"));
    EXPECT_EQ(dev("thermo_rev")->get_value_string(), "auto");
    EXPECT_FALSE(cmd("thermo_rev", "dry"));
    EXPECT_EQ(dev("thermo_rev")->get_value_string(), "auto");
}

TEST_F(ClimateBaseTest, SetpointBoundsComeFromTheLinkedIo)
{
    auto bounds = [](const std::string &id)
    {
        const ClimateBase::Bounds b = dev(id)->setpointBounds("target_temp");
        return Json{ b.min, b.max, b.step, b.unit };
    };

    EXPECT_EQ(bounds("full"), Json({ 16, 30, 0.5, "°C" }));
    EXPECT_EQ(bounds("only_target"), Json({ 7, 35, 0.5, "°C" }));
    //A single bound past the other default does not invert the range.
    EXPECT_EQ(bounds("b_min_only"), Json({ 40, 40, 0.5, "°C" }));
    EXPECT_EQ(bounds("b_max_only"), Json({ 5, 5, 0.5, "°C" }));
    //The IO ignores inverted bounds, and so does the device.
    EXPECT_EQ(bounds("b_inverted"), Json({ 7, 35, 0.5, "°C" }));
    EXPECT_EQ(bounds("b_bad_step"), Json({ 7, 35, 0.5, "°C" }));
    //No link at all.
    EXPECT_EQ(bounds("none"), Json({ 7, 35, 0.5, "°C" }));

    EXPECT_EQ(caps("b_min_only").at("target_temp"),
              parse(R"({"min": 40, "max": 40, "step": 0.5, "unit": "°C"})"));
}

TEST_F(ClimateBaseTest, CommandsGoToTheCommandIoWithItsBounds)
{
    EXPECT_EQ(caps("split").at("target_temp"),
              parse(R"({"min": 18, "max": 24, "step": 1, "unit": "°F"})"));

    EXPECT_TRUE(cmd("split", R"({"mode":"cool","target_temp":30})"));
    EXPECT_EQ(str("split_mode_cmd"), "cool");
    EXPECT_DOUBLE_EQ(num("split_target_cmd"), 24);
    EXPECT_TRUE(cmd("split", R"({"target_temp":20.4})"));
    EXPECT_DOUBLE_EQ(num("split_target_cmd"), 20);

    //The state is what the read IOs report, not what was sent.
    EXPECT_EQ(str("split_mode"), "");
    EXPECT_DOUBLE_EQ(num("split_target"), 0);
    io("split_mode")->set_value(std::string("cool"));
    EXPECT_EQ(dev("split")->get_value_string(), "cool");
}

TEST_F(ClimateBaseTest, StateCarriesNumbersAndOnlyValidEnums)
{
    io("full_mode")->set_value(std::string("heat"));
    io(EXT_TEMP)->set_value(21.5);
    io("full_target")->set_value(20.0);
    setRange(18, 24);
    io("full_action")->set_value(std::string("heating"));
    io("full_fan")->set_value(std::string("low"));
    io("full_swing")->set_value(std::string("off"));

    const Json s = state("full");
    EXPECT_EQ(s, parse(R"({"mode": "heat", "current_temp": 21.5, "target_temp": 20,
                           "target_temp_low": 18, "target_temp_high": 24, "action": "heating",
                           "fan_mode": "low", "swing_mode": "off"})"))
        << s.dump();
    EXPECT_TRUE(s.at("current_temp").is_number());
    EXPECT_EQ(dev("full")->get_value_string(), "heat");

    //An unknown value on the wire is left out rather than passed on.
    io("full_mode")->set_value(std::string("heat_cool"));
    io("full_action")->set_value(std::string("defrosting"));
    const Json s2 = state("full");
    EXPECT_FALSE(s2.contains("mode")) << s2.dump();
    EXPECT_FALSE(s2.contains("action")) << s2.dump();
    EXPECT_EQ(dev("full")->get_value_string(), "");

    EXPECT_EQ(state("only_current"), Json::object());
    EXPECT_EQ(state("none"), Json::object());
    EXPECT_EQ(state("dangling"), Json::object());
}

TEST_F(ClimateBaseTest, ModeOutsideTheCapabilitiesIsRefused)
{
    EXPECT_TRUE(cmd("full", "auto"));
    EXPECT_EQ(str("full_mode"), "auto");

    for (const char *m: { "dry", "fan_only", "turbo", "Heat", "" })
        EXPECT_FALSE(cmd("full", m)) << m;
    EXPECT_FALSE(cmd("full", R"({"mode":1})"));
    EXPECT_EQ(str("full_mode"), "auto");

    //Without the mode link there is no mode field.
    EXPECT_FALSE(cmd("only_target", "off"));
    EXPECT_FALSE(cmd("dangling", "off"));
    EXPECT_TRUE(cmd("only_mode", "fan_only"));
    EXPECT_EQ(str("only_mode_mode"), "fan_only");
}

TEST_F(ClimateBaseTest, TargetTempIsRoundedToTheStepAndClamped)
{
    auto target = [this](const char *c)
    {
        EXPECT_TRUE(cmd("full", c)) << c;
        return num("full_target");
    };
    EXPECT_DOUBLE_EQ(target(R"({"target_temp":21.3})"), 21.5);
    EXPECT_DOUBLE_EQ(target(R"({"target_temp":21.2})"), 21);
    EXPECT_DOUBLE_EQ(target(R"({"target_temp":50})"), 30);
    EXPECT_DOUBLE_EQ(target(R"({"target_temp":3})"), 16);

    EXPECT_FALSE(cmd("full", R"({"target_temp":"22"})"));
    EXPECT_FALSE(cmd("only_mode", R"({"target_temp":22})"));
    EXPECT_DOUBLE_EQ(num("full_target"), 16);
}

TEST_F(ClimateBaseTest, FanAndSwingModesAreEnumsFromTheirParams)
{
    EXPECT_TRUE(cmd("full", R"({"fan_mode":"high","swing_mode":"vertical"})"));
    EXPECT_EQ(str("full_fan"), "high");
    EXPECT_EQ(str("full_swing"), "vertical");

    EXPECT_FALSE(cmd("full", R"({"fan_mode":"quiet"})"));
    EXPECT_FALSE(cmd("full", R"({"swing_mode":"both"})"));
    EXPECT_EQ(str("full_fan"), "high");
    EXPECT_EQ(str("full_swing"), "vertical");

    //The state only shows values of the lists, never "" or a stray one.
    io("full_fan")->set_value(std::string("turbo"));
    io("full_swing")->set_value(std::string(""));
    EXPECT_FALSE(state("full").contains("fan_mode")) << state("full").dump();
    EXPECT_FALSE(state("full").contains("swing_mode")) << state("full").dump();
    io("full_fan")->set_value(std::string("high"));
    EXPECT_EQ(state("full").at("fan_mode"), "high");

    EXPECT_TRUE(cmd("only_fan", R"({"fan_mode":"quiet"})"));
    EXPECT_FALSE(cmd("only_fan", R"({"swing_mode":"off"})"));
    EXPECT_FALSE(cmd("bad", R"({"fan_mode":"auto"})"));
}

TEST_F(ClimateBaseTest, ValidRangeAppliesBothBounds)
{
    setRange(24, 28);

    //Checked one by one, high (20) would fall below the old low (24).
    EXPECT_TRUE(cmd("full", R"({"target_temp_low":18,"target_temp_high":20})"));
    EXPECT_DOUBLE_EQ(num("full_low"), 18);
    EXPECT_DOUBLE_EQ(num("full_high"), 20);
}

TEST_F(ClimateBaseTest, RangeNarrowerThanMinRangeIsRefusedWholeAndLogged)
{
    setRange(18, 20);

    std::string log = journal(
        [this]()
        {
        EXPECT_FALSE(cmd("full", R"({"target_temp_low":22,"target_temp_high":23})"));
    });
    EXPECT_DOUBLE_EQ(num("full_low"), 18);
    EXPECT_DOUBLE_EQ(num("full_high"), 20);
    EXPECT_NE(log.find("Device(full)"), std::string::npos) << log;
    EXPECT_NE(log.find("closer than min_range 2"), std::string::npos) << log;

    log = journal(
        [this]()
        {
        EXPECT_FALSE(cmd("full", R"({"target_temp_low":25,"target_temp_high":21})"));
    });
    EXPECT_NE(log.find("low 25 is above high 21"), std::string::npos) << log;
    EXPECT_DOUBLE_EQ(num("full_low"), 18);
    EXPECT_DOUBLE_EQ(num("full_high"), 20);
}

TEST_F(ClimateBaseTest, PartialRangeIsCheckedAgainstTheCurrentOtherBound)
{
    setRange(18, 20);

    EXPECT_TRUE(cmd("full", R"({"target_temp_high":25})"));
    EXPECT_DOUBLE_EQ(num("full_low"), 18);
    EXPECT_DOUBLE_EQ(num("full_high"), 25);

    EXPECT_FALSE(cmd("full", R"({"target_temp_low":24})"));
    EXPECT_DOUBLE_EQ(num("full_low"), 18);

    EXPECT_TRUE(cmd("full", R"({"target_temp_low":23})"));
    EXPECT_DOUBLE_EQ(num("full_low"), 23);
    EXPECT_DOUBLE_EQ(num("full_high"), 25);

    EXPECT_FALSE(cmd("full", R"({"target_temp_high":24.5})"));
    EXPECT_DOUBLE_EQ(num("full_high"), 25);
}

TEST_F(ClimateBaseTest, RangeBoundsAndStepApplyBeforeTheCheck)
{
    setRange(18, 20);

    EXPECT_TRUE(cmd("full", R"({"target_temp_low":5.2,"target_temp_high":99})"));
    EXPECT_DOUBLE_EQ(num("full_low"), 10);
    EXPECT_DOUBLE_EQ(num("full_high"), 30);

    EXPECT_TRUE(cmd("full", R"({"target_temp_low":20.3,"target_temp_high":22.7})"));
    EXPECT_DOUBLE_EQ(num("full_low"), 20.5);
    EXPECT_DOUBLE_EQ(num("full_high"), 22.5);

    //The gap is checked on the rounded values: 20.5 and 22 are too close.
    EXPECT_FALSE(cmd("full", R"({"target_temp_high":22.2})"));
    EXPECT_DOUBLE_EQ(num("full_high"), 22.5);

    //Clamped to 30 each: a zero gap.
    EXPECT_FALSE(cmd("full", R"({"target_temp_low":40,"target_temp_high":50})"));
    EXPECT_DOUBLE_EQ(num("full_low"), 20.5);
}

TEST_F(ClimateBaseTest, DecimalStepsDoNotTripMinRange)
{
    //16.4 - 14.4 is 1.9999999999999982 in binary.
    EXPECT_TRUE(cmd("fine", R"({"target_temp_low":14.4,"target_temp_high":16.4})"));
    EXPECT_DOUBLE_EQ(num("fine_low"), 14.4);
    EXPECT_DOUBLE_EQ(num("fine_high"), 16.4);
    EXPECT_FALSE(cmd("fine", R"({"target_temp_low":14.5})"));
}

TEST_F(ClimateBaseTest, AnInvalidRangeValueRefusesThePairButNotTheOtherFields)
{
    setRange(18, 20);
    io("full_mode")->set_value(std::string("off"));

    EXPECT_TRUE(cmd("full", R"({"mode":"heat","target_temp_low":15,"target_temp_high":"x"})"));
    EXPECT_EQ(str("full_mode"), "heat");
    EXPECT_DOUBLE_EQ(num("full_low"), 18);
    EXPECT_DOUBLE_EQ(num("full_high"), 20);

    EXPECT_FALSE(cmd("full", R"({"target_temp_low":true,"target_temp_high":25})"));
    EXPECT_DOUBLE_EQ(num("full_high"), 20);

    //A device without both range links has no range field.
    EXPECT_FALSE(cmd("only_low", R"({"target_temp_low":18})"));
    EXPECT_FALSE(cmd("only_target", R"({"target_temp_high":25})"));
    EXPECT_DOUBLE_EQ(num("only_low_target_temp_low"), 0);
}

//The impl sees each write through the change hook, so the pair in between
//must keep min_range as well.
TEST_F(ClimateBaseTest, NoIntermediatePairBreaksMinRange)
{
    setRange(18, 20);

    std::vector<std::pair<double, double>> seen;
    auto record = [&seen](IOBase &)
    {
        seen.emplace_back(io("full_low")->get_value_double(), io("full_high")->get_value_double());
    };
    auto subLow = IOChangeHub::Instance().subscribe("full_low", record);
    auto subHigh = IOChangeHub::Instance().subscribe("full_high", record);

    ASSERT_TRUE(cmd("full", R"({"target_temp_low":24,"target_temp_high":28})"));
    ASSERT_TRUE(cmd("full", R"({"target_temp_low":18,"target_temp_high":20})"));
    EXPECT_DOUBLE_EQ(num("full_low"), 18);
    EXPECT_DOUBLE_EQ(num("full_high"), 20);

    ASSERT_EQ(seen.size(), 4u);
    for (const auto &p: seen)
        EXPECT_GE(p.second - p.first, 2) << p.first << " / " << p.second;
}

TEST_F(ClimateBaseTest, PartialCommandsKeepTheOtherFields)
{
    ASSERT_TRUE(cmd("full", R"({"mode":"heat","target_temp":21,"fan_mode":"auto"})"));
    setRange(18, 22);
    io("full_swing")->set_value(std::string("off"));
    const Json before = state("full");

    EXPECT_TRUE(cmd("full", R"({"fan_mode":"low"})"));
    Json expected = before;
    expected["fan_mode"] = "low";
    EXPECT_EQ(state("full"), expected);

    EXPECT_TRUE(cmd("full", "cool"));
    expected["mode"] = "cool";
    EXPECT_EQ(state("full"), expected);

    EXPECT_TRUE(cmd("full", R"({"target_temp_high":23})"));
    expected["target_temp_high"] = 23;
    EXPECT_EQ(state("full"), expected);
}

TEST_F(ClimateBaseTest, ConfigurationMistakesAreLoggedAtLoad)
{
    std::string log = journal(
        [this]()
        {
        clearCoreState();
        loadConfig(house(), rulesXmlDocument(""));
    });
    EXPECT_NE(log.find("Device(bad): unknown mode 'heat_cool' in the modes param"), std::string::npos)
        << log;
    EXPECT_NE(log.find("Device(bad): link 'fan_mode' without a fan_modes param"), std::string::npos)
        << log;
    EXPECT_NE(log.find("Device(bad): min_range '-1' is not a non-negative number"), std::string::npos)
        << log;
    EXPECT_EQ(log.find("Device(full): "), std::string::npos) << log;

    EXPECT_NE(log.find("Device(bad2): empty modes param"), std::string::npos) << log;
    EXPECT_NE(log.find("Device(bad2): min_range 5 is wider than the setpoint bounds 20..21"),
              std::string::npos)
        << log;
    EXPECT_EQ(log.find("Device(skew): "), std::string::npos) << log;
    EXPECT_FALSE(caps("bad2").contains("modes"));

    EXPECT_EQ(caps("bad"), parse(R"({"modes": ["heat"], "target_temp_range": true, "fan_modes": [],
        "target_temp": {"min": 7, "max": 35, "step": 0.5, "unit": "°C", "min_range": 0}})"));
    EXPECT_DOUBLE_EQ(dev("bad")->minRange(), 0);
    EXPECT_DOUBLE_EQ(dev("full")->minRange(), 2);
    EXPECT_DOUBLE_EQ(dev("none")->minRange(), 0);

    //min_range 0 still keeps low <= high.
    EXPECT_TRUE(cmd("bad", R"({"target_temp_low":20,"target_temp_high":20})"));
    EXPECT_FALSE(cmd("bad", R"({"target_temp_low":21})"));
}

TEST_F(ClimateBaseTest, WritesRefusedByTheLinkedIoAreLoggedWithTheReason)
{
    const std::string log = journal(
        [this]()
        {
        EXPECT_FALSE(cmd("ro", "heat"));
        EXPECT_FALSE(cmd("ro", R"({"target_temp":21})"));
        EXPECT_FALSE(cmd("ro", R"({"fan_mode":"auto"})"));
        EXPECT_FALSE(cmd("ro", R"({"target_temp_low":18,"target_temp_high":22})"));
    });
    for (const char *field: { "'mode'", "'target_temp'", "'fan_mode'", "'target_temp_range'" })
    {
        const std::string line = std::string("Device(ro): command field ") + field +
                                 " refused: the linked IO refused the value";
        EXPECT_NE(log.find(line), std::string::npos) << line << "\n"
                                                     << log;
    }
}

TEST_F(ClimateBaseTest, ARangeBoundWithoutAnIoIsNotWritten)
{
    TestClimate *d = ListeRoom::Instance().findIOAs<TestClimate>("only_low");
    ASSERT_NE(nullptr, d);
    EXPECT_FALSE(d->writeRangeBound("target_temp_high", 21));
    EXPECT_TRUE(d->writeRangeBound("target_temp_low", 21));
    EXPECT_DOUBLE_EQ(num("only_low_target_temp_low"), 21);
}

TEST_F(ClimateBaseTest, ValuesNeverReceivedAreLeftOutOfTheState)
{
    //Restored from its saved value: known. The others still hold a default 0.
    EXPECT_EQ(state("fresh"), parse(R"({"target_temp": 21})"));
    EXPECT_TRUE(io("fresh_target")->hasValue());
    EXPECT_FALSE(io("fresh_current")->hasValue());

    io("fresh_current")->set_value(0.0);
    io("fresh_low")->set_value(17.0);
    EXPECT_EQ(state("fresh"), parse(R"({"current_temp": 0, "target_temp": 21, "target_temp_low": 17})"));
}

TEST_F(ClimateBaseTest, PartialRangeNeedsTheOtherBoundKnown)
{
    EXPECT_FALSE(cmd("pair_range", R"({"target_temp_high":25})"));
    EXPECT_FALSE(io("pr_high")->hasValue());

    EXPECT_TRUE(cmd("pair_range", R"({"target_temp_low":18,"target_temp_high":25})"));
    EXPECT_TRUE(cmd("pair_range", R"({"target_temp_high":26})"));
    EXPECT_DOUBLE_EQ(num("pr_low"), 18);
    EXPECT_DOUBLE_EQ(num("pr_high"), 26);
}

TEST_F(ClimateBaseTest, ARangeAloneBoundsTheTargetCapability)
{
    EXPECT_EQ(caps("skew"), parse(R"({"target_temp_range": true,
        "target_temp": {"min": 10, "max": 30, "step": 0.5, "unit": "°F", "min_range": 3}})"));
    EXPECT_FALSE(state("skew").contains("target_temp"));
}

//After a restart in a stable room, the first reading equals the cached one and
//is not signalled: the cached value must still count.
TEST_F(ClimateBaseTest, ASensorRestoredFromTheCacheIsKnown)
{
    Config::Instance().SaveValueIO("cached_temp", "19.5", false);
    clearCoreState();
    loadConfig(house(), rulesXmlDocument(""));
    auto *cached = dynamic_cast<TestTemp *>(io("cached_temp"));
    auto *uncached = dynamic_cast<TestTemp *>(io("uncached_temp"));
    ASSERT_NE(cached, nullptr);
    ASSERT_NE(uncached, nullptr);

    cached->deliver(19.5);
    EXPECT_EQ(state("cached"), parse(R"({"current_temp": 19.5})"));

    //No cache and a first reading equal to the default: unknown until it moves.
    uncached->deliver(0);
    EXPECT_EQ(state("uncached"), Json::object());
    uncached->deliver(20);
    EXPECT_EQ(state("uncached"), parse(R"({"current_temp": 20})"));

    forgetIOState("cached_temp");
    forgetIOState("uncached_temp");
}

namespace
{

Json documentOf(const std::string &impl)
{
    Params p;
    p.Add("type", "device:" + impl);
    p.Add("id", "doc");
    IOBase::ScopedDocGen docScope;
    std::unique_ptr<IOBase> io(IOFactory::Instance().CreateIO("device:" + impl, p));
    return io->getDoc()->genDocJson();
}

Json fieldOf(const Json &doc, const std::string &name)
{
    for (const Json &f: doc.at("command_fields"))
    {
        if (f.at("name") == name)
            return f;
    }
    return Json();
}

} //namespace

//The base documents what every climate impl shares, and nothing about the
//command until the impl says which modes it offers.
TEST_F(ClimateBaseTest, TheFamilyAndItsParamsAreDocumentedByTheBase)
{
    const Json doc = documentOf("test_climate");

    EXPECT_EQ("climate", doc.at("family").get<std::string>());
    EXPECT_FALSE(doc.contains("command_format"));
    EXPECT_FALSE(doc.contains("command_fields"));
    for (const Json &p: doc.at("parameters"))
    {
        const std::string name = p.at("name").get<std::string>();
        const bool shared = name == "modes" || name == "fan_modes" || name == "swing_modes" ||
                            name == "min_range";
        EXPECT_EQ(shared, p.contains("storage")) << name;
    }
}

//The thermostat will store its mode: no link to require, and the modes its
//outputs allow. The other fields follow the links like a proxy's.
TEST_F(ClimateBaseTest, AnImplThatStoresItsModeDocumentsItWithoutALink)
{
    const Json doc = documentOf("test_thermostat");

    EXPECT_EQ("json", doc.at("command_format").get<std::string>());
    EXPECT_EQ(Json::array({ "off", "heat" }), fieldOf(doc, "mode").at("values"));
    EXPECT_EQ(Json::array(), fieldOf(doc, "mode").at("requires_links"));
    const std::string desc = fieldOf(doc, "mode").at("description").get<std::string>();
    EXPECT_EQ(" note_x", desc.substr(desc.size() - 7));
    EXPECT_EQ(std::string::npos, fieldOf(doc, "target_temp").at("description").get<std::string>().find("note_x"));
    EXPECT_EQ(Json::array({ "target_temp" }), fieldOf(doc, "target_temp").at("requires_links"));

    //Documented without a link, and accepted without one.
    ClimateBase *th = ListeRoom::Instance().findIOAs<ClimateBase>("thermo_heat");
    ASSERT_NE(nullptr, th);
    EXPECT_FALSE(th->linkIo("mode"));
    EXPECT_TRUE(th->set_value(std::string(R"({"mode":"heat"})")));
}
