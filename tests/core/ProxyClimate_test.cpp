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

//ProxyClimate: reads from each link's io, writes to its command_io, the mode
//mirrors the link (heat_cool is auto), and the remote temperature is relayed
//on every change and by a keep-alive. The loop is the real one, with periods
//of tens of milliseconds.

#include "CalaosCoreFixture.h"
#include "IOChangeHub.h"
#include "ProxyClimate.h"
#include "libuvw.h"

#include <chrono>
#include <functional>
#include <string>
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
    Clock::time_point at;
};

std::vector<Write> &writes()
{
    static std::vector<Write> w;
    return w;
}

} //namespace

//A broker output: remembers and echoes what it is given, like OutputString.
class RecordingOutput: public IOBase
{
public:
    RecordingOutput(Params &p):
        IOBase(p, IOBase::IO_OUTPUT)
    {
    }

    DATA_TYPE get_type() override { return TSTRING; }
    std::string get_value_string() override { return text; }
    double get_value_double() override { return number; }

    bool set_value(std::string val) override
    {
        if (get_param("refuse") == "true")
            return false;
        text = val;
        record();
        return true;
    }

    bool set_value(double val) override
    {
        if (get_param("refuse") == "true")
            return false;
        number = val;
        record();
        return true;
    }

private:
    void record()
    {
        writes().push_back({ get_param("id"), text, number, Clock::now() });
        EmitSignalIO();
    }

    std::string text;
    double number = 0;
};

//A broker input: the test delivers what the appliance reported.
class FeedInput: public IOBase
{
public:
    FeedInput(Params &p):
        IOBase(p, IOBase::IO_INPUT)
    {
    }

    DATA_TYPE get_type() override { return TSTRING; }
    std::string get_value_string() override { return text; }
    double get_value_double() override { return number; }

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

REGISTER_IO(RecordingOutput)
REGISTER_IO(FeedInput)

namespace
{

const char *const EXT_TEMP = "ext_temp";

std::string element(const std::string &kind, const std::string &type, const std::string &id,
                    const std::string &extra = std::string())
{
    return "      <calaos:" + kind + " type=\"" + type + "\" id=\"" + id + "\" name=\"" + id +
           "\" visible=\"false\"" + extra + " />\n";
}

std::string in(const std::string &id) { return element("input", "FeedInput", id); }

std::string out(const std::string &id, const std::string &extra = std::string())
{
    return element("output", "RecordingOutput", id, extra);
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

std::string device(const std::string &id, const std::string &inner)
{
    return "    <calaos:device id=\"" + id + "\" type=\"climate\" impl=\"proxy\" name=\"" + id +
           "\">\n" + inner + "    </calaos:device>\n";
}

const std::string BOUNDS_10_30 = R"( value_min="10" value_max="30" step="0.5")";

//A range read back from inputs, written to separate outputs.
std::string rangeDevice(const std::string &id, const std::string &params,
                        const std::string &highAttrs = std::string())
{
    return device(id, in(id + "_low") + out(id + "_low_cmd", BOUNDS_10_30) + in(id + "_high") +
                          out(id + "_high_cmd", BOUNDS_10_30 + highAttrs) +
                          link("target_temp_low", id + "_low", id + "_low_cmd") +
                          link("target_temp_high", id + "_high", id + "_high_cmd") +
                          param("min_range", "2") + params);
}

//The remote temperature relay alone. `owned` false leaves its IOs to the
//caller, which keeps them when the device is deleted.
std::string remoteDevice(const std::string &id, const std::string &params, bool owned = true)
{
    return device(id, (owned ? out(id + "_remote") + in(id + "_src") : std::string()) +
                          link("remote_temp", id + "_remote") +
                          link("remote_temp_source", id + "_src") + params);
}

std::string modeDevice(const std::string &id, const std::string &params = std::string())
{
    return device(id, in(id + "_mode") + out(id + "_cmd") +
                          link("mode", id + "_mode", id + "_cmd") + params);
}

std::string house()
{
    std::string ac;
    ac += in("ac_mode") + out("ac_mode_cmd") + in("ac_current");
    ac += out("ac_target", R"( value_min="16" value_max="30" step="0.5" unit="°C")");
    ac += in("ac_low") + out("ac_low_cmd", BOUNDS_10_30) + in("ac_high") +
          out("ac_high_cmd", BOUNDS_10_30);
    ac += in("ac_action") + in("ac_fan") + out("ac_fan_cmd") + out("ac_swing") + out("ac_remote");
    ac += link("mode", "ac_mode", "ac_mode_cmd") + link("current_temp", "ac_current");
    ac += link("target_temp", "ac_target");
    ac += link("target_temp_low", "ac_low", "ac_low_cmd");
    ac += link("target_temp_high", "ac_high", "ac_high_cmd");
    ac += link("action", "ac_action") + link("fan_mode", "ac_fan", "ac_fan_cmd");
    ac += link("swing_mode", "ac_swing") + link("remote_temp", "ac_remote");
    ac += link("remote_temp_source", EXT_TEMP);
    ac += param("fan_modes", "auto,low,high") + param("swing_modes", "off,vertical");
    ac += param("min_range", "2") + param("remote_temp_keepalive", "30");

    std::string salon = device("ac", ac);
    salon += modeDevice("mm_a");
    salon += modeDevice("mm_b");
    salon += modeDevice("mm_c", param("auto_payload", "auto"));
    salon += modeDevice("mm_d", param("auto_payload", "bogus"));
    salon += modeDevice("mm_modes", param("modes", "off,auto"));
    salon += device("mm_ro", in("mm_ro_mode") + link("mode", "mm_ro_mode"));
    salon += device("mm_rw", out("mm_rw_mode") + link("mode", "mm_rw_mode"));

    salon += rangeDevice("rg", "");
    salon += rangeDevice("hd", param("readback_hold", "0.1"));
    salon += rangeDevice("rf", "", R"( refuse="true")");
    salon += rangeDevice("bh", param("readback_hold", "-3"));

    salon += remoteDevice("ka", param("remote_temp_keepalive", "0.05"));
    salon += remoteDevice("gone", param("remote_temp_keepalive", "0.05"), false);
    salon += remoteDevice("ns", param("remote_temp_keepalive", "0.05"));
    salon += remoteDevice("ds", param("remote_temp_keepalive", "0.05"));
    salon += remoteDevice("bk", param("remote_temp_keepalive", "abc"));
    salon += device("rc", in("rc_read") + out("rc_cmd") + in("rc_src") +
                              link("remote_temp", "rc_read", "rc_cmd") +
                              link("remote_temp_source", "rc_src"));
    salon += device("r_only", out("r_only_remote") + link("remote_temp", "r_only_remote"));
    salon += device("s_only", in("s_only_src") + link("remote_temp_source", "s_only_src"));
    salon += device("r_ro", in("r_ro_read") + in("r_ro_src") + link("remote_temp", "r_ro_read") +
                                link("remote_temp_source", "r_ro_src"));

    return ioXmlDocument(roomXml("Salon", "salon", salon) +
                         roomXml("Kitchen", "kitchen",
                                 in(EXT_TEMP) + in("gone_src") + out("gone_remote")));
}

bool runLoopUntil(const std::function<bool()> &pred, int timeoutMs = 3000)
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
    runLoopUntil([&end]() { return Clock::now() >= end; }, ms + 1000);
}

Json parse(const char *s)
{
    return Json::parse(s);
}

} //namespace

class ProxyClimateTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        writes().clear();
        loadConfig(house(), rulesXmlDocument(""));
    }

    static ProxyClimate *dev(const std::string &id)
    {
        return ListeRoom::Instance().findIOAs<ProxyClimate>(id);
    }

    static FeedInput *feed(const std::string &id)
    {
        return ListeRoom::Instance().findIOAs<FeedInput>(id);
    }

    static Json caps(const std::string &id) { return dev(id)->deviceObject().at("capabilities"); }
    static Json state(const std::string &id) { return dev(id)->deviceObject().at("state"); }
    static bool cmd(const std::string &id, const std::string &command)
    {
        return dev(id)->set_value(command);
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

    static std::string lastText(const std::string &id)
    {
        const auto w = writesTo(id);
        return w.empty() ? std::string("<none>") : w.back().text;
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

    static size_t count(const std::string &haystack, const std::string &needle)
    {
        size_t n = 0;
        for (size_t at = haystack.find(needle); at != std::string::npos;
             at = haystack.find(needle, at + needle.size()))
            n++;
        return n;
    }
};

TEST_F(ProxyClimateTest, EveryCommandedFieldIsWrittenToItsCommandIo)
{
    EXPECT_TRUE(cmd("ac", R"({"mode":"cool","target_temp":21.26,"fan_mode":"low","swing_mode":"vertical"})"));

    EXPECT_EQ(lastText("ac_mode_cmd"), "cool");
    ASSERT_EQ(writesTo("ac_target").size(), 1u);
    EXPECT_DOUBLE_EQ(writesTo("ac_target")[0].number, 21.5);
    EXPECT_EQ(lastText("ac_fan_cmd"), "low");
    EXPECT_EQ(lastText("ac_swing"), "vertical");

    //The read side is the appliance's word, not ours.
    EXPECT_EQ(feed("ac_mode")->get_value_string(), "");
    EXPECT_EQ(feed("ac_fan")->get_value_string(), "");
    EXPECT_FALSE(state("ac").contains("mode"));

    EXPECT_FALSE(cmd("ac", R"({"fan_mode":"turbo"})"));
    EXPECT_EQ(writesTo("ac_fan_cmd").size(), 1u);
}

TEST_F(ProxyClimateTest, ARangeIsWrittenToTheCommandIosOfBothBounds)
{
    feed("ac_low")->deliver(18.0);
    feed("ac_high")->deliver(22.0);

    EXPECT_TRUE(cmd("ac", R"({"target_temp_low":20,"target_temp_high":26})"));
    ASSERT_EQ(writesTo("ac_low_cmd").size(), 1u);
    ASSERT_EQ(writesTo("ac_high_cmd").size(), 1u);
    EXPECT_DOUBLE_EQ(writesTo("ac_low_cmd")[0].number, 20);
    EXPECT_DOUBLE_EQ(writesTo("ac_high_cmd")[0].number, 26);
}

TEST_F(ProxyClimateTest, StateIsReadFromTheIoOfEachLink)
{
    feed("ac_mode")->deliver("heat");
    feed("ac_current")->deliver(23.4);
    feed("ac_low")->deliver(18.0);
    feed("ac_high")->deliver(22.0);
    feed("ac_action")->deliver("heating");
    feed("ac_fan")->deliver("high");

    EXPECT_EQ(state("ac"), parse(R"({"mode": "heat", "current_temp": 23.4, "target_temp_low": 18,
        "target_temp_high": 22, "action": "heating", "fan_mode": "high"})"));
    EXPECT_EQ(dev("ac")->get_value_string(), "heat");
}

TEST_F(ProxyClimateTest, CapabilitiesFollowTheLinks)
{
    EXPECT_EQ(caps("ac"), parse(R"({
        "modes": ["off", "heat", "cool", "auto", "dry", "fan_only"],
        "target_temp": {"min": 16, "max": 30, "step": 0.5, "unit": "°C", "min_range": 2},
        "target_temp_range": true, "reports_action": true,
        "fan_modes": ["auto", "low", "high"], "swing_modes": ["off", "vertical"],
        "remote_temp": true})"));
    EXPECT_EQ(dev("ac")->deviceObject().at("impl"), "proxy");
}

TEST_F(ProxyClimateTest, HeatCoolIsReadAsAuto)
{
    feed("mm_a_mode")->deliver("heat_cool");
    EXPECT_EQ(state("mm_a").at("mode"), "auto");
    EXPECT_EQ(dev("mm_a")->get_value_string(), "auto");

    feed("mm_a_mode")->deliver("auto");
    EXPECT_EQ(state("mm_a").at("mode"), "auto");

    feed("mm_a_mode")->deliver("fan_only");
    EXPECT_EQ(state("mm_a").at("mode"), "fan_only");
}

TEST_F(ProxyClimateTest, AutoIsWrittenAsHeatCoolUntilTheApplianceSaysOtherwise)
{
    //Nothing reported yet: the ESPHome spelling the spec maps to auto.
    EXPECT_TRUE(cmd("mm_a", "auto"));
    EXPECT_EQ(lastText("mm_a_cmd"), "heat_cool");

    //An appliance that reported "auto" then another mode still speaks "auto".
    feed("mm_b_mode")->deliver("auto");
    feed("mm_b_mode")->deliver("heat");
    EXPECT_TRUE(cmd("mm_b", "auto"));
    EXPECT_EQ(lastText("mm_b_cmd"), "auto");

    //... and one that reported heat_cool switches back to heat_cool.
    feed("mm_b_mode")->deliver("heat_cool");
    feed("mm_b_mode")->deliver("cool");
    EXPECT_TRUE(cmd("mm_b", R"({"mode":"auto"})"));
    EXPECT_EQ(lastText("mm_b_cmd"), "heat_cool");
}

TEST_F(ProxyClimateTest, TheAutoPayloadParamWinsOverWhatWasReported)
{
    feed("mm_c_mode")->deliver("heat_cool");
    EXPECT_TRUE(cmd("mm_c", "auto"));
    EXPECT_EQ(lastText("mm_c_cmd"), "auto");
    EXPECT_EQ(state("mm_c").at("mode"), "auto");

    //An invalid value is ignored, not obeyed.
    feed("mm_d_mode")->deliver("auto");
    EXPECT_TRUE(cmd("mm_d", "auto"));
    EXPECT_EQ(lastText("mm_d_cmd"), "auto");
    feed("mm_d_mode")->deliver("heat_cool");
    EXPECT_TRUE(cmd("mm_d", "auto"));
    EXPECT_EQ(lastText("mm_d_cmd"), "heat_cool");
}

TEST_F(ProxyClimateTest, OtherModesAreWrittenByName)
{
    for (const char *mode: { "off", "heat", "cool", "dry", "fan_only" })
    {
        EXPECT_TRUE(cmd("mm_a", mode)) << mode;
        EXPECT_EQ(lastText("mm_a_cmd"), mode);
    }
    EXPECT_FALSE(cmd("mm_a", "heat_cool"));
    EXPECT_EQ(writesTo("mm_a_cmd").size(), 5u);
}

TEST_F(ProxyClimateTest, TheModesParamRestrictsWhatIsWritten)
{
    EXPECT_EQ(caps("mm_modes").at("modes"), parse(R"(["off", "auto"])"));
    EXPECT_FALSE(cmd("mm_modes", "heat"));
    EXPECT_TRUE(cmd("mm_modes", "auto"));
    EXPECT_EQ(lastText("mm_modes_cmd"), "heat_cool");
}

TEST_F(ProxyClimateTest, AnUnknownModeIsLoggedOncePerValueAndNotPublished)
{
    const std::string log = journal(
        [this]()
        {
        //Nothing reported yet is not an unknown mode.
        EXPECT_FALSE(state("mm_a").contains("mode"));
        for (int i = 0; i < 3; i++)
        {
            feed("mm_a_mode")->deliver("weird");
            EXPECT_FALSE(state("mm_a").contains("mode"));
            EXPECT_EQ(dev("mm_a")->get_value_string(), "");
        }
        feed("mm_a_mode")->deliver("other");
        feed("mm_a_mode")->deliver("heat");
        EXPECT_EQ(state("mm_a").at("mode"), "heat");
        feed("mm_a_mode")->deliver("weird");
    });
    EXPECT_EQ(count(log, "reports the mode 'weird'"), 1u) << log;
    EXPECT_EQ(count(log, "reports the mode 'other'"), 1u) << log;
    EXPECT_EQ(count(log, "reports the mode"), 2u) << log;
}

TEST_F(ProxyClimateTest, UnknownModesNeverPrintRawBytesAndAreBounded)
{
    const std::string log = journal(
        [this]()
        {
        feed("mm_a_mode")->deliver("bad\nDevice(forged): owned");
        for (int i = 0; i < 40; i++)
            feed("mm_b_mode")->deliver("v" + std::to_string(i));
    });
    EXPECT_EQ(log.find("forged"), std::string::npos) << log;
    EXPECT_EQ(count(log, "Device(mm_a): the appliance reports the mode #"), 1u) << log;
    EXPECT_EQ(count(log, "Device(mm_b): the appliance reports the mode"), 16u) << log;
}

TEST_F(ProxyClimateTest, AModeLinkThatCannotBeWrittenOffersNoMode)
{
    EXPECT_FALSE(caps("mm_ro").contains("modes"));
    EXPECT_FALSE(cmd("mm_ro", "heat"));
    EXPECT_FALSE(cmd("mm_ro", R"({"mode":"off"})"));
    EXPECT_TRUE(writes().empty());

    //It is still read.
    feed("mm_ro_mode")->deliver("heat");
    EXPECT_EQ(state("mm_ro").at("mode"), "heat");
    EXPECT_EQ(dev("mm_ro")->get_value_string(), "heat");

    //An output without a command_io is both read and written.
    EXPECT_EQ(caps("mm_rw").at("modes").size(), 6u);
    EXPECT_TRUE(cmd("mm_rw", "dry"));
    EXPECT_EQ(lastText("mm_rw_mode"), "dry");
    EXPECT_EQ(state("mm_rw").at("mode"), "dry");
    EXPECT_TRUE(cmd("mm_rw", "auto"));
    EXPECT_EQ(lastText("mm_rw_mode"), "heat_cool");
    EXPECT_EQ(state("mm_rw").at("mode"), "auto");
}

TEST_F(ProxyClimateTest, ConfigurationMistakesAreLoggedAtLoad)
{
    const std::string log = journal(
        [this]()
        {
        clearCoreState();
        loadConfig(house(), rulesXmlDocument(""));
    });
    for (const char *expected: {
             "Device(mm_ro): link 'mode' has no command_io and its io is not an output",
             "Device(r_ro): link 'remote_temp' has no command_io and its io is not an output",
             "Device(r_only): link 'remote_temp' without a 'remote_temp_source'",
             "Device(s_only): link 'remote_temp_source' without a 'remote_temp'",
             "Device(mm_d): auto_payload must be 'auto' or 'heat_cool'",
             "Device(bk): remote_temp_keepalive is not a positive number of seconds",
             "Device(bh): readback_hold is not a positive number of seconds",
         })
        EXPECT_NE(log.find(expected), std::string::npos) << expected << "\n" << log;

    for (const char *quiet: { "ac", "mm_a", "mm_b", "mm_c", "mm_rw", "rc", "ka", "rg", "hd" })
        EXPECT_EQ(log.find(std::string("Device(") + quiet + "): "), std::string::npos)
            << quiet << "\n" << log;
}

TEST_F(ProxyClimateTest, TheSourceIsRelayedOnEveryChange)
{
    feed(EXT_TEMP)->deliver(20.5);
    ASSERT_EQ(writesTo("ac_remote").size(), 1u);
    EXPECT_DOUBLE_EQ(writesTo("ac_remote")[0].number, 20.5);

    feed(EXT_TEMP)->deliver(21.0);
    ASSERT_EQ(writesTo("ac_remote").size(), 2u);
    EXPECT_DOUBLE_EQ(writesTo("ac_remote")[1].number, 21.0);
}

TEST_F(ProxyClimateTest, TheRemoteTemperatureGoesToTheCommandIo)
{
    feed("rc_src")->deliver(19.0);
    EXPECT_TRUE(writesTo("rc_read").empty());
    ASSERT_EQ(writesTo("rc_cmd").size(), 1u);
    EXPECT_DOUBLE_EQ(writesTo("rc_cmd")[0].number, 19.0);
}

TEST_F(ProxyClimateTest, NothingIsRelayedBeforeTheSourceHasAValue)
{
    //Six keep-alive periods with a source that never reported.
    pumpLoopFor(300);
    EXPECT_TRUE(writesTo("ns_remote").empty());

    //0 is a reading like another.
    feed("ns_src")->deliver(0.0);
    ASSERT_EQ(writesTo("ns_remote").size(), 1u);
    EXPECT_DOUBLE_EQ(writesTo("ns_remote")[0].number, 0.0);
}

TEST_F(ProxyClimateTest, TheKeepAliveRepeatsTheLastValueWhileTheSourceIsSilent)
{
    feed("ka_src")->deliver(20.5);
    ASSERT_TRUE(runLoopUntil([]() { return writesTo("ka_remote").size() >= 5; }));

    std::vector<Write> w = writesTo("ka_remote");
    for (const Write &x: w)
        EXPECT_DOUBLE_EQ(x.number, 20.5);
    //Index 0 answers the change, the others the timer. A busy host can only
    //stretch the gaps.
    for (size_t i = 2; i < w.size(); i++)
    {
        const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(w[i].at - w[i - 1].at);
        EXPECT_GE(gap.count(), 40) << "keep-alive " << i;
    }

    feed("ka_src")->deliver(22.0);
    const size_t before = writesTo("ka_remote").size();
    ASSERT_TRUE(runLoopUntil([before]() { return writesTo("ka_remote").size() >= before + 3; }));
    w = writesTo("ka_remote");
    for (size_t i = before; i < w.size(); i++)
        EXPECT_DOUBLE_EQ(w[i].number, 22.0);
}

TEST_F(ProxyClimateTest, WithoutAUsablePeriodTheKeepAliveIsTwoMinutes)
{
    feed("bk_src")->deliver(20.0);
    feed("rc_src")->deliver(20.0);
    pumpLoopFor(300);
    EXPECT_EQ(writesTo("bk_remote").size(), 1u);
    EXPECT_EQ(writesTo("rc_cmd").size(), 1u);
}

TEST_F(ProxyClimateTest, ADisconnectedSourceIsNotRelayed)
{
    FeedInput *src = feed("ds_src");
    src->deliver(20.0);
    ASSERT_EQ(writesTo("ds_remote").size(), 1u);

    src->setStatusInfo(IOBase::StatusType::Connected, IOBase::StatusConnected::STATUS_DISCONNECTED);
    src->deliver(25.0);
    pumpLoopFor(200);
    EXPECT_EQ(writesTo("ds_remote").size(), 1u);

    src->setStatusInfo(IOBase::StatusType::Connected, IOBase::StatusConnected::STATUS_CONNECTED);
    ASSERT_TRUE(runLoopUntil([]() { return writesTo("ds_remote").size() >= 2; }));
    EXPECT_DOUBLE_EQ(writesTo("ds_remote").back().number, 25.0);
}

TEST_F(ProxyClimateTest, TheKeepAliveStopsWhenTheDeviceIsDestroyed)
{
    feed("gone_src")->deliver(20.0);
    ASSERT_TRUE(runLoopUntil([]() { return writesTo("gone_remote").size() >= 3; }));
    EXPECT_EQ(IOChangeHub::Instance().subscriberCount("gone_src"), 2u);

    ASSERT_TRUE(ListeRoom::Instance().deleteIO(dev("gone")));
    EXPECT_EQ(IOChangeHub::Instance().subscriberCount("gone_src"), 0u);

    const size_t before = writesTo("gone_remote").size();
    feed("gone_src")->deliver(30.0);
    pumpLoopFor(300);
    EXPECT_EQ(writesTo("gone_remote").size(), before);
}

TEST_F(ProxyClimateTest, ASecondPartialRangeCommandIsCheckedAgainstTheFirstNotTheStaleReadback)
{
    feed("rg_low")->deliver(18.0);
    feed("rg_high")->deliver(22.0);

    ASSERT_TRUE(cmd("rg", R"({"target_temp_high":28})"));
    //The appliance has not reported the new high yet: 25 is above its 22.
    ASSERT_TRUE(cmd("rg", R"({"target_temp_low":25})"));
    ASSERT_FALSE(writesTo("rg_high_cmd").empty());
    ASSERT_FALSE(writesTo("rg_low_cmd").empty());
    EXPECT_DOUBLE_EQ(writesTo("rg_high_cmd").back().number, 28);
    EXPECT_DOUBLE_EQ(writesTo("rg_low_cmd").back().number, 25);

    //... and the commanded 28 still guards min_range.
    EXPECT_FALSE(cmd("rg", R"({"target_temp_low":27.5})"));
    EXPECT_EQ(writesTo("rg_low_cmd").size(), 1u);
}

TEST_F(ProxyClimateTest, TheReadbackReplacesTheCommandedValueWhenItArrives)
{
    feed("rg_low")->deliver(18.0);
    feed("rg_high")->deliver(22.0);
    ASSERT_TRUE(cmd("rg", R"({"target_temp_high":28})"));

    //The appliance clamped it to 24.
    feed("rg_high")->deliver(24.0);
    EXPECT_FALSE(cmd("rg", R"({"target_temp_low":23})"));
    EXPECT_TRUE(cmd("rg", R"({"target_temp_low":21})"));
}

TEST_F(ProxyClimateTest, ACommandedPairMakesBothBoundsKnown)
{
    //No readback at all: the other bound is unknown.
    EXPECT_FALSE(cmd("rg", R"({"target_temp_low":20})"));
    EXPECT_TRUE(writes().empty());

    ASSERT_TRUE(cmd("rg", R"({"target_temp_low":20,"target_temp_high":26})"));
    EXPECT_TRUE(cmd("rg", R"({"target_temp_high":28})"));
    EXPECT_FALSE(cmd("rg", R"({"target_temp_high":21})"));
}

TEST_F(ProxyClimateTest, ACommandedValueIsForgottenAfterTheHold)
{
    feed("hd_low")->deliver(18.0);
    feed("hd_high")->deliver(22.0);
    ASSERT_TRUE(cmd("hd", R"({"target_temp_high":28})"));

    //The appliance never answered: the last thing it reported is the reference.
    pumpLoopFor(150);
    EXPECT_FALSE(cmd("hd", R"({"target_temp_low":25})"));
}

TEST_F(ProxyClimateTest, AnInvalidHoldFallsBackToTheDefault)
{
    feed("bh_low")->deliver(18.0);
    feed("bh_high")->deliver(22.0);
    ASSERT_TRUE(cmd("bh", R"({"target_temp_high":28})"));
    EXPECT_TRUE(cmd("bh", R"({"target_temp_low":25})"));
}

TEST_F(ProxyClimateTest, ARefusedWriteIsNotRemembered)
{
    feed("rf_low")->deliver(18.0);
    feed("rf_high")->deliver(22.0);

    EXPECT_FALSE(cmd("rf", R"({"target_temp_high":28})"));
    EXPECT_FALSE(cmd("rf", R"({"target_temp_low":25})"));
    EXPECT_TRUE(writes().empty());
}
