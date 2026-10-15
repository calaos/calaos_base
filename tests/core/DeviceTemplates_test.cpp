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

/*
 * The device templates (spec §10) against what the server accepts: types and
 * link roles it registers, params its IODoc documents, and a device built from
 * each template, as the installer wizard builds it, that loads without a
 * warning and survives a save. instantiate() below is the reference of the
 * wizard's algorithm (docs/17_device_templates.md).
 */

#include "CalaosCoreFixture.h"
#include "Device.h"
#include "DeviceTemplates.h"
#include "ExternProc.h"
#include "ExternProcSpawnHarness.h"
#include "MqttBrokersList.h"
#include "MqttWire.h"

#include <pugixml.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#if HAVE_GETTEXT
#include <libintl.h>
#endif

#include <algorithm>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

using Values = std::map<std::string, Json>;

const char *const kHost = "192.0.2.71";
const char *const kPort = "1891";
const char *const ROOM = "Salon";

std::string fileText(const std::string &path)
{
    std::ifstream f(path, std::ifstream::in | std::ifstream::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/* ------------------------------------------------------------------------
 * The wizard: template + values -> <calaos:device>.
 * --------------------------------------------------------------------- */

std::string render(const Json &v)
{
    if (v.is_string())
        return v.get<std::string>();
    if (v.is_boolean())
        return v.get<bool>() ? "true" : "false";
    if (v.is_number())
        return Utils::to_string(v.get<double>());
    std::string joined;
    for (const Json &e: v)
        joined += (joined.empty() ? "" : ",") + e.get<std::string>();
    return joined;
}

bool truthy(const Json &v)
{
    if (v.is_boolean())
        return v.get<bool>();
    //Json::empty() is false for any string.
    if (v.is_string())
        return !v.get<std::string>().empty();
    if (v.is_array())
        return !v.empty();
    return !v.is_null();
}

bool holds(const Json &entry, const Values &values)
{
    if (!entry.contains("when"))
        return true;
    std::string name = entry.at("when");
    const bool negated = name[0] == '!';
    if (negated)
        name = name.substr(1);
    auto it = values.find(name);
    const bool t = it != values.end() && truthy(it->second);
    return negated ? !t : t;
}

const std::regex PLACEHOLDER("\\{([a-z_]+)\\}");

std::string substitute(const std::string &s, const Values &values, const std::string &deviceId)
{
    std::string out;
    auto last = s.cbegin();
    for (std::sregex_iterator it(s.begin(), s.end(), PLACEHOLDER), end; it != end; ++it)
    {
        out.append(last, s.cbegin() + it->position());
        const std::string name = (*it)[1];
        if (name == "id")
            out += deviceId;
        else if (values.count(name))
            out += render(values.at(name));
        else
            out += it->str();
        last = s.cbegin() + it->position() + it->length();
    }
    out.append(last, s.cend());
    return out;
}

struct Generated
{
    std::string xml;
    //Owned key -> IO id, for the owned IOs that were created.
    std::map<std::string, std::string> owned;
};

//An unset field, or an owned IO left out, leaves the link out.
std::string resolveRef(const Json &ref, const Values &values, const Generated &g)
{
    if (ref.contains("owned"))
    {
        auto it = g.owned.find(ref.at("owned").get<std::string>());
        return it == g.owned.end() ? std::string() : it->second;
    }
    auto it = values.find(ref.at("field").get<std::string>());
    return it == values.end() ? std::string() : render(it->second);
}

Generated instantiate(const Json &t, const Values &values, const std::string &deviceId)
{
    std::map<std::string, std::string> preset;
    for (const Json &f: t.at("fields"))
    {
        if (f.at("type") != "enum")
            continue;
        for (const Json &o: f.at("values"))
        {
            if (o.at("value") == values.at(f.at("name").get<std::string>()) && o.contains("params"))
                for (auto p = o.at("params").begin(); p != o.at("params").end(); ++p)
                    preset[p.key()] = p.value().get<std::string>();
        }
    }

    Generated g;
    pugi::xml_document doc;
    pugi::xml_node dev = doc.append_child("calaos:device");
    dev.append_attribute("id") = deviceId.c_str();
    dev.append_attribute("type") = t.at("family").get<std::string>().c_str();
    dev.append_attribute("impl") = t.at("impl").get<std::string>().c_str();
    for (auto a = t.at("attributes").begin(); a != t.at("attributes").end(); ++a)
        dev.append_attribute(a.key().c_str()) = substitute(a.value().get<std::string>(), values, deviceId).c_str();

    for (const Json &io: t.at("ios"))
    {
        if (!holds(io, values))
            continue;
        const std::string id = deviceId + "_" + io.at("key").get<std::string>();
        g.owned[io.at("key").get<std::string>()] = id;
        pugi::xml_node n = dev.append_child(("calaos:" + io.at("element").get<std::string>()).c_str());
        n.append_attribute("id") = id.c_str();
        n.append_attribute("type") = io.at("type").get<std::string>().c_str();
        for (auto p = io.at("params").begin(); p != io.at("params").end(); ++p)
        {
            const std::string v = substitute(p.value().get<std::string>(), values, deviceId);
            if (!v.empty())
                n.append_attribute(p.key().c_str()) = v.c_str();
        }
    }

    for (const Json &l: t.at("links"))
    {
        const std::string io = resolveRef(l.at("io"), values, g);
        const std::string cmd = l.contains("command_io") ? resolveRef(l.at("command_io"), values, g) : "";
        if (!holds(l, values) || io.empty() || (l.contains("command_io") && cmd.empty()))
            continue;
        pugi::xml_node n = dev.append_child("calaos:link");
        n.append_attribute("name") = l.at("role").get<std::string>().c_str();
        n.append_attribute("io") = io.c_str();
        if (!cmd.empty())
            n.append_attribute("command_io") = cmd.c_str();
    }

    for (const Json &p: t.at("params"))
    {
        if (!holds(p, values))
            continue;
        const std::string name = p.at("name");
        const std::string v = substitute(preset.count(name) ? preset[name] : p.at("value").get<std::string>(),
                                         values, deviceId);
        if (v.empty())
            continue;
        pugi::xml_node n = dev.append_child("calaos:param");
        n.append_attribute("name") = name.c_str();
        n.append_attribute("value") = v.c_str();
    }

    std::ostringstream ss;
    dev.print(ss, "  ");
    g.xml = ss.str();
    return g;
}

/* ------------------------------------------------------------------------
 * Test values: the IOs a user would pick, and one or more variants per
 * template so that every `when` is taken both ways.
 * --------------------------------------------------------------------- */

std::string mqttInput(const std::string &type, const std::string &id)
{
    return "    <calaos:input type=\"" + type + "\" id=\"" + id + "\" name=\"" + id + "\" host=\"" + kHost +
           "\" port=\"" + kPort + "\" topic_sub=\"tpl/" + id + "\" />\n";
}

//In another room: a link may target any room.
std::string externalRoom()
{
    return roomXml("Capteurs", "misc",
                   mqttInput("MqttInputTemp", "ext_temp") + mqttInput("MqttInputAnalog", "ext_hum") +
                       internalIoXml("InternalBool", "ext_heat", "ext_heat") +
                       internalIoXml("InternalBool", "ext_cool", "ext_cool") +
                       internalIoXml("InternalBool", "ext_flag_in", "ext_flag_in") +
                       internalIoXml("InternalBool", "ext_flag_out", "ext_flag_out"));
}

//An empty rules.xml is logged as an error: one rule that touches no device.
std::string rules()
{
    return rulesXmlDocument(simpleRuleXml("r", "ext_flag_in", "==", "true", "ext_flag_out", "true"));
}

struct Variant
{
    std::string name;
    std::string templateId;
    Values values;
};

Values esphomeCommon()
{
    Values v;
    v["name"] = "Clim salon";
    v["room"] = ROOM;
    v["topic_prefix"] = "salon-clim";
    v["object_id"] = "clim_salon";
    v["mqtt_host"] = kHost;
    v["mqtt_port"] = 1891;
    v["mqtt_user"] = "";
    v["mqtt_password"] = "";
    v["temp_min"] = 16;
    v["temp_max"] = 30;
    v["temp_step"] = 0.5;
    v["fan_modes"] = Json::array();
    v["swing_modes"] = Json::array();
    return v;
}

Values merged(Values base, const Values &extra)
{
    for (const auto &kv: extra)
        base[kv.first] = kv.second;
    return base;
}

Values thermostatCommon(const std::string &emitter)
{
    Values v;
    v["name"] = "Chauffage";
    v["room"] = ROOM;
    v["current_temp"] = "ext_temp";
    v["output_heat"] = "ext_heat";
    v["output_cool"] = "ext_cool";
    v["emitter"] = emitter;
    return v;
}

//What the wizard pre-fills.
Json fieldDefault(const std::string &templateId, const std::string &field)
{
    const Json all = DeviceTemplates::all();
    for (const Json &t: all.at("templates"))
        for (const Json &f: t.at("fields"))
            if (t.at("id") == templateId && f.at("name") == field)
                return f.at("default");
    return Json();
}

std::vector<Variant> variants()
{
    const Json allModes = { "off", "heat", "cool", "auto", "dry", "fan_only" };
    return {
        { "esphome_full", "esphome_climate",
          merged(esphomeCommon(), { { "mqtt_user", "calaos" },
                                    { "mqtt_password", "s3cret" },
                                    { "modes", allModes },
                                    { "two_point", true },
                                    { "with_action", true },
                                    { "fan_modes", { "auto", "low", "high" } },
                                    { "swing_modes", { "off", "vertical" } },
                                    { "remote_temp_source", "ext_temp" } }) },
        { "esphome_minimal", "esphome_climate",
          merged(esphomeCommon(), { { "modes", { "off", "heat", "cool" } },
                                    { "two_point", false },
                                    { "with_action", false },
                                    { "remote_temp_source", "" } }) },
        { "esphome_ir_full", "esphome_climate_ir",
          merged(esphomeCommon(), { { "modes", allModes },
                                    { "with_current_temp", true },
                                    { "temp_step", 1 },
                                    { "fan_modes", { "auto", "high" } },
                                    { "swing_modes", { "off", "both" } } }) },
        { "esphome_ir_minimal", "esphome_climate_ir",
          merged(esphomeCommon(), { { "modes", { "off", "cool" } }, { "with_current_temp", false } }) },
        { "heat_electric", "thermostat_heat", thermostatCommon("electric") },
        { "heat_water_radiator", "thermostat_heat", thermostatCommon("water_radiator") },
        { "heat_floor", "thermostat_heat", thermostatCommon("floor") },
        { "heat_cool_heat_pump", "thermostat_heat_cool", thermostatCommon("heat_pump") },
        { "heat_cool_fan_coil", "thermostat_heat_cool", thermostatCommon("fan_coil") },
        { "floor_heat_cool", "thermostat_floor_heat_cool",
          merged(thermostatCommon("floor"),
                 { { "humidity", "ext_hum" }, { "humidity_max", fieldDefault("thermostat_floor_heat_cool", "humidity_max") } }) },
    };
}

using Strings = std::map<std::string, std::string>;

//What each variant must give: the linked roles, and the device params, which
//are the defaults of docs/17_device_templates.md.
struct Expected
{
    std::set<std::string> roles;
    Strings params;
};

Strings thermostatParams(const std::string &emitter, const std::string &hysteresis, const std::string &minOn,
                         const std::string &minOff)
{
    Strings p;
    p["emitter"] = emitter;
    p["hysteresis"] = hysteresis;
    p["min_on_time"] = minOn;
    p["min_off_time"] = minOff;
    p["sensor_timeout"] = "9000";
    p["put_back_delay"] = "1";
    p["put_back_delay_max"] = "60";
    return p;
}

Strings withEntries(Strings base, const Strings &extra)
{
    for (const auto &kv: extra)
        base[kv.first] = kv.second;
    return base;
}

std::map<std::string, Expected> expectations()
{
    const std::string allModes = "off,heat,cool,auto,dry,fan_only";
    const std::set<std::string> heat = { "current_temp", "target_temp", "action", "output_heat" };
    const std::set<std::string> range = { "current_temp", "target_temp", "target_temp_low", "target_temp_high",
                                          "action", "output_heat", "output_cool" };
    std::set<std::string> floorRange = range;
    floorRange.insert("humidity");

    std::map<std::string, Expected> e;
    e["esphome_full"] = { { "mode", "current_temp", "target_temp_low", "target_temp_high", "action", "fan_mode",
                            "swing_mode", "remote_temp", "remote_temp_source" },
                          { { "modes", allModes },
                            { "auto_payload", "heat_cool" },
                            { "fan_modes", "auto,low,high" },
                            { "swing_modes", "off,vertical" },
                            { "remote_temp_keepalive", "120" },
                            { "sensor_timeout", "9000" } } };
    e["esphome_minimal"] = { { "mode", "current_temp", "target_temp" },
                             { { "modes", "off,heat,cool" }, { "auto_payload", "heat_cool" } } };
    e["esphome_ir_full"] = { { "mode", "current_temp", "target_temp", "fan_mode", "swing_mode" },
                             { { "modes", allModes },
                               { "auto_payload", "heat_cool" },
                               { "fan_modes", "auto,high" },
                               { "swing_modes", "off,both" } } };
    e["esphome_ir_minimal"] = { { "mode", "target_temp" }, { { "modes", "off,cool" }, { "auto_payload", "heat_cool" } } };
    e["heat_electric"] = { heat, thermostatParams("electric", "0.3", "120", "120") };
    e["heat_water_radiator"] = { heat, thermostatParams("water_radiator", "0.5", "300", "300") };
    e["heat_floor"] = { heat, thermostatParams("floor", "0.5", "900", "900") };
    e["heat_cool_heat_pump"] = { range,
                                 withEntries(thermostatParams("heat_pump", "0.5", "600", "300"), { { "min_range", "2" } }) };
    e["heat_cool_fan_coil"] = { range,
                                withEntries(thermostatParams("fan_coil", "0.5", "120", "120"), { { "min_range", "1" } }) };
    e["floor_heat_cool"] = { floorRange, withEntries(thermostatParams("floor", "0.5", "900", "900"),
                                                     { { "min_range", "2" }, { "humidity_max", "60" } }) };
    return e;
}

//By value: a range-for over a member of the temporary all() would dangle.
Json templates()
{
    return DeviceTemplates::all().at("templates");
}

Json templateNamed(const std::string &id)
{
    for (const Json &t: templates())
    {
        if (t.at("id") == id)
            return t;
    }
    return Json();
}

Json fieldNamed(const Json &t, const std::string &name)
{
    for (const Json &f: t.at("fields"))
    {
        if (f.at("name") == name)
            return f;
    }
    return Json();
}

std::set<std::string> paramNames(const Json &doc)
{
    std::set<std::string> names;
    for (const Json &p: doc.at("parameters"))
        names.insert(p.at("name").get<std::string>());
    return names;
}

//Every string a template carries, for the placeholder check.
void collectStrings(const Json &j, std::vector<std::string> &out)
{
    if (j.is_string())
        out.push_back(j.get<std::string>());
    for (const Json &e: j)
        if (j.is_structured())
            collectStrings(e, out);
}

/* The stand-in sidecar of the templates' broker, connected as the real one
 * would be, so that a test can say what the broker publishes. Opened before
 * any IO of that broker exists: its controller is the launch that follows. */
struct Session
{
    bool ok = false;
    int fd = -1;
};

Session &session()
{
    static Session *s = nullptr;
    if (s)
        return *s;
    s = new Session();

    const size_t before = ExternProcSpawn::launches("calaos_mqtt").size();
    Params bp;
    bp.Add("host", kHost);
    bp.Add("port", kPort);
    MqttBrokersList::Instance().get_ctrl(bp);
    auto launched = [before]()
    {
        return ExternProcSpawn::launches("calaos_mqtt").size() > before;
    };
    if (!ExternProcSpawn::runLoopUntil(launched, 8000))
        return *s;
    const auto argv = ExternProcSpawn::launches("calaos_mqtt")[before];
    if (argv.size() < 3)
        return *s;

    s->fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un remote;
    ::memset(&remote, 0, sizeof(remote));
    remote.sun_family = AF_UNIX;
    ::strncpy(remote.sun_path, argv[2].c_str(), sizeof(remote.sun_path) - 1);
    if (s->fd < 0 || ::connect(s->fd, (struct sockaddr *)&remote, sizeof(remote)) != 0)
        return *s;

    //The server drops what it is sent before it accepted us: its config frame says it did.
    std::string rx;
    ExternProcMessage frame;
    auto accepted = [&]()
    {
        char buf[4096];
        const ssize_t n = ::recv(s->fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0)
            rx.append(buf, buf + n);
        return frame.processFrameData(rx) && frame.isValid();
    };
    s->ok = ExternProcSpawn::runLoopUntil(accepted, 8000);
    return *s;
}

bool brokerSends(const std::string &topic, const std::string &payload)
{
    const std::string raw = ExternProcMessage(MqttWire::encodeMessage(topic, payload)).getRawData();
    return ::send(session().fd, raw.data(), raw.size(), 0) == (ssize_t)raw.size();
}

class DeviceTemplatesTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        //Mqtt IOs launch their sidecar: a stand-in that stays up.
        ASSERT_TRUE(ExternProcSpawn::install("calaos_mqtt", 0, 600));
        ASSERT_TRUE(session().ok) << "stand-in sidecar not connected";
    }

    std::string outDir() const { return cacheDir() + "/templates/out"; }

    //The published IODoc, as the installer reads it.
    Json ioDoc()
    {
        loadConfig();
        IOFactory::Instance().genDoc(cacheDir() + "/iodoc");
        return Json::parse(fileText(cacheDir() + "/iodoc/io_doc.json"));
    }

    //Loads the variant beside the external sensors and outputs; the log of the load.
    std::string loadVariant(const Variant &v, Generated &g)
    {
        clearCoreState();
        g = instantiate(templateNamed(v.templateId), v.values, v.name);
        const std::string house = ioXmlDocument(roomXml(ROOM, "salon", g.xml) + externalRoom());
        EXPECT_TRUE(Utils::calaosLogger("device")->isLevelEnabled(Logger::LOG_LEVEL_WARNING))
            << "the device domain is muted, its warnings cannot be observed";
        testing::internal::CaptureStdout();
        loadConfig(house, rules());
        return testing::internal::GetCapturedStdout();
    }
};

} //namespace

TEST_F(DeviceTemplatesTest, TheFileHoldsTheFiveTemplatesOfTheSpec)
{
    ASSERT_TRUE(DeviceTemplates::generate(outDir()));
    const std::string txt = fileText(outDir() + "/" + DeviceTemplates::FILE_NAME);
    const Json j = Json::parse(txt);

    EXPECT_EQ(DeviceTemplates::all(), j);
    EXPECT_EQ(1, j.at("format"));
    std::vector<std::string> ids;
    for (const Json &t: j.at("templates"))
        ids.push_back(t.at("id").get<std::string>());
    EXPECT_EQ((std::vector<std::string>{ "esphome_climate", "esphome_climate_ir", "thermostat_heat",
                                         "thermostat_heat_cool", "thermostat_floor_heat_cool" }),
              ids);

    //Pretty printed and pure ASCII, as io_doc.json.
    EXPECT_EQ(0u, txt.find("{\n    \""));
    for (unsigned char c: txt)
        ASSERT_LT(c, 0x80u);
}

TEST_F(DeviceTemplatesTest, AnUnwritablePathIsReported)
{
    const std::string file = cacheDir() + "/a_file";
    std::ofstream(file) << "x";
    EXPECT_FALSE(DeviceTemplates::generate(file + "/sub"));
}

//Unchecked, the param is not written and the device offers every mode.
TEST_F(DeviceTemplatesTest, TheModesOfAClimateAreRequired)
{
    for (const Json &t: templates())
    {
        const Json f = fieldNamed(t, "modes");
        if (f.is_null())
            continue;
        EXPECT_TRUE(f.at("required").get<bool>()) << t.at("id");
    }
}

//ESPHome publishes online on <prefix>/status, and offline as its last will.
TEST_F(DeviceTemplatesTest, AnEsphomeDeviceFollowsTheStatusOfItsNode)
{
    for (const Variant &v: variants())
    {
        if (v.templateId.rfind("esphome", 0) != 0)
            continue;
        SCOPED_TRACE(v.name);
        Generated g;
        loadVariant(v, g);
        ASSERT_FALSE(g.owned.empty());

        for (const char *status: { "online", "offline", "online" })
        {
            const std::string connected = std::string(status) == "online" ? "true" : "false";
            ASSERT_TRUE(brokerSends("salon-clim/status", status));
            auto followed = [&]()
            {
                for (const auto &kv: g.owned)
                    if (io(kv.second)->getStatusInfo()["connected"] != connected)
                        return false;
                return true;
            };
            EXPECT_TRUE(ExternProcSpawn::runLoopUntil(followed, 3000)) << status;
            for (const auto &kv: g.owned)
                EXPECT_EQ(connected, io(kv.second)->getStatusInfo()["connected"]) << kv.first << " " << status;
            //The proxy's availability is its mode link's (CLIM-6): only false is said.
            const Json state = ListeRoom::Instance().findIOAs<Device>(v.name)->deviceObject().at("state");
            EXPECT_EQ(connected == "false", state.contains("connected")) << status;
            if (state.contains("connected"))
                EXPECT_FALSE(state.at("connected").get<bool>());
        }
    }
}

//The structure the wizard relies on, checked once for all templates.
TEST_F(DeviceTemplatesTest, EveryTemplateIsSelfConsistent)
{
    for (const Json &t: templates())
    {
        SCOPED_TRACE(t.at("id").get<std::string>());

        std::map<std::string, Json> fields;
        int rooms = 0;
        for (const Json &f: t.at("fields"))
        {
            ASSERT_TRUE(fields.emplace(f.at("name").get<std::string>(), f).second) << "field twice: " << f.at("name");
            const std::string type = f.at("type");
            rooms += type == "room";
            EXPECT_TRUE(f.at("label").is_string() && !f.at("label").empty());
            EXPECT_TRUE(f.at("description").is_string());
            EXPECT_TRUE(f.at("required").is_boolean());
            if (type == "number")
            {
                EXPECT_LE(f.at("min").get<double>(), f.at("default").get<double>()) << f.at("name");
                EXPECT_GE(f.at("max").get<double>(), f.at("default").get<double>()) << f.at("name");
            }
            if (type == "enum" || type == "multi")
            {
                //The wizard shows labels, never raw values.
                std::set<std::string> values;
                for (const Json &o: f.at("values"))
                {
                    EXPECT_TRUE(o.is_object() && o.contains("label") && !o.at("label").empty()) << f.at("name");
                    if (o.is_object())
                        values.insert(o.at("value").get<std::string>());
                }
                if (type == "multi")
                    for (const Json &d: f.at("default"))
                        EXPECT_TRUE(values.count(d.get<std::string>())) << f.at("name") << " " << d;
            }
            //A required multi asks for one choice at least.
            if (type == "multi" && f.at("required"))
                EXPECT_FALSE(f.at("default").empty()) << f.at("name");
            if (type == "io")
            {
                const std::set<std::string> kinds = { "number", "bool", "string" };
                EXPECT_TRUE(kinds.count(f.at("io_filter").at("value").get<std::string>())) << f.at("name");
                EXPECT_TRUE(f.at("io_filter").at("writable").is_boolean());
            }
            EXPECT_TRUE((std::set<std::string>{ "string", "number", "bool", "enum", "multi", "room", "io" }).count(type))
                << type;
        }
        EXPECT_EQ(1, rooms);
        ASSERT_TRUE(fields.count("name"));

        std::set<std::string> keys;
        for (const Json &io: t.at("ios"))
        {
            EXPECT_TRUE(keys.insert(io.at("key").get<std::string>()).second) << "owned IO twice: " << io.at("key");
            EXPECT_TRUE((std::set<std::string>{ "input", "output", "internal" }).count(io.at("element").get<std::string>()));
        }

        std::set<std::string> params;
        for (const Json &p: t.at("params"))
            EXPECT_TRUE(params.insert(p.at("name").get<std::string>()).second) << "param twice: " << p.at("name");

        auto whenNamesAField = [&](const Json &entry)
        {
            if (!entry.contains("when"))
                return;
            std::string name = entry.at("when");
            if (name[0] == '!')
                name = name.substr(1);
            EXPECT_TRUE(fields.count(name)) << "when names no field: " << name;
        };
        for (const char *section: { "ios", "links", "params" })
            for (const Json &e: t.at(section))
                whenNamesAField(e);
        if (t.contains("notes"))
            for (const Json &n: t.at("notes"))
                whenNamesAField(n);

        for (const Json &l: t.at("links"))
            for (const char *ref: { "io", "command_io" })
            {
                if (!l.contains(ref))
                    continue;
                const Json &r = l.at(ref);
                if (r.contains("owned"))
                    EXPECT_TRUE(keys.count(r.at("owned").get<std::string>())) << l.at("role");
                else
                    EXPECT_EQ("io", fieldNamed(t, r.at("field").get<std::string>()).at("type")) << l.at("role");
            }

        std::vector<std::string> strings;
        collectStrings(t, strings);
        for (const std::string &s: strings)
            for (std::sregex_iterator it(s.begin(), s.end(), PLACEHOLDER), end; it != end; ++it)
                EXPECT_TRUE((*it)[1] == "id" || fields.count((*it)[1])) << "unknown placeholder " << it->str();

        //An option only replaces a param the template has, and the template's
        //own values are those of the default option.
        for (const auto &kv: fields)
        {
            const Json &f = kv.second;
            if (f.at("type") != "enum")
                continue;
            bool defaultFound = false;
            //Every option sets the same params, so that switching back to one
            //restores all it chose.
            std::set<std::string> firstNames;
            for (auto p = f.at("values").at(0).at("params").begin(); p != f.at("values").at(0).at("params").end(); ++p)
                firstNames.insert(p.key());
            for (const Json &o: f.at("values"))
            {
                std::set<std::string> names;
                for (auto p = o.at("params").begin(); p != o.at("params").end(); ++p)
                    names.insert(p.key());
                EXPECT_EQ(firstNames, names) << o.at("value");
                defaultFound |= o.at("value") == f.at("default");
                for (auto p = o.at("params").begin(); p != o.at("params").end(); ++p)
                {
                    EXPECT_TRUE(params.count(p.key())) << o.at("value") << " sets " << p.key();
                    if (o.at("value") != f.at("default"))
                        continue;
                    for (const Json &tp: t.at("params"))
                        if (tp.at("name") == p.key())
                            EXPECT_EQ(p.value(), tp.at("value")) << p.key();
                }
            }
            EXPECT_TRUE(defaultFound) << kv.first;
        }
    }
}

TEST_F(DeviceTemplatesTest, EveryTemplateUsesRegisteredTypesAndKnownRoles)
{
    const Json doc = ioDoc();
    for (const Json &t: templates())
    {
        SCOPED_TRACE(t.at("id").get<std::string>());
        const std::string impl = t.at("impl");
        ASSERT_TRUE(doc.at("devices").contains(impl));
        const Json &dev = doc.at("devices").at(impl);
        EXPECT_EQ(t.at("family"), dev.at("family"));

        std::map<std::string, std::string> elements;
        for (const Json &io: t.at("ios"))
        {
            EXPECT_TRUE(doc.contains(io.at("type").get<std::string>())) << "unregistered type " << io.at("type");
            elements[io.at("key").get<std::string>()] = io.at("element").get<std::string>();
        }

        std::map<std::string, Json> roles;
        for (const Json &r: dev.at("link_roles"))
            roles[r.at("role").get<std::string>()] = r;
        std::set<std::string> linked;
        for (const Json &l: t.at("links"))
        {
            const std::string role = l.at("role");
            ASSERT_TRUE(roles.count(role)) << "unknown role " << role;
            EXPECT_TRUE(linked.insert(role).second) << "role twice: " << role;
            if (!roles[role].at("writable") || l.contains("command_io"))
                continue;
            //What the device writes must be writable.
            const Json &r = l.at("io");
            if (r.contains("owned"))
                EXPECT_NE("input", elements[r.at("owned").get<std::string>()]) << role;
            else
                EXPECT_TRUE(fieldNamed(t, r.at("field").get<std::string>()).at("io_filter").at("writable").get<bool>()) << role;
        }
        for (const auto &kv: roles)
            if (kv.second.at("required"))
                EXPECT_TRUE(linked.count(kv.first)) << "required role missing: " << kv.first;
    }
}

TEST_F(DeviceTemplatesTest, EveryParamIsDocumented)
{
    const Json doc = ioDoc();
    for (const Json &t: templates())
    {
        SCOPED_TRACE(t.at("id").get<std::string>());
        const Json &dev = doc.at("devices").at(t.at("impl").get<std::string>());

        std::set<std::string> childParams, attributes;
        for (const Json &p: dev.at("parameters"))
            (p.value("storage", "") == "calaos:param" ? childParams : attributes).insert(p.at("name").get<std::string>());
        for (const Json &p: t.at("params"))
            EXPECT_TRUE(childParams.count(p.at("name").get<std::string>())) << "undocumented device param " << p.at("name");
        for (auto a = t.at("attributes").begin(); a != t.at("attributes").end(); ++a)
            EXPECT_TRUE(attributes.count(a.key())) << "undocumented device attribute " << a.key();

        for (const Json &io: t.at("ios"))
        {
            const std::set<std::string> documented = paramNames(doc.at(io.at("type").get<std::string>()));
            for (auto p = io.at("params").begin(); p != io.at("params").end(); ++p)
                EXPECT_TRUE(documented.count(p.key())) << io.at("type") << " does not document " << p.key();
        }
    }
}

class DeviceTemplateVariantTest: public DeviceTemplatesTest, public ::testing::WithParamInterface<Variant>
{
};

//The values are of the kind each field asks for, so the load below proves
//what a wizard following the fields would produce.
TEST_P(DeviceTemplateVariantTest, TheTestValuesFitTheFields)
{
    const Variant &v = GetParam();
    loadConfig(ioXmlDocument(externalRoom()), rules());
    const Json t = templateNamed(v.templateId);
    for (const Json &f: t.at("fields"))
    {
        const std::string name = f.at("name");
        ASSERT_TRUE(v.values.count(name)) << name;
        const Json &value = v.values.at(name);
        const std::string type = f.at("type");
        if (f.at("required"))
            EXPECT_TRUE(truthy(value)) << name;
        if (type == "number")
            EXPECT_TRUE(value.is_number()) << name;
        else if (type == "bool")
            EXPECT_TRUE(value.is_boolean()) << name;
        else if (type == "multi")
            EXPECT_TRUE(value.is_array()) << name;
        else
            EXPECT_TRUE(value.is_string()) << name;
        if (type == "enum")
        {
            bool known = false;
            for (const Json &o: f.at("values"))
                known |= o.at("value") == value;
            EXPECT_TRUE(known) << name;
        }
        if (type == "io" && truthy(value))
        {
            IOBase *picked = io(value.get<std::string>());
            ASSERT_NE(nullptr, picked) << name;
            const Json &filter = f.at("io_filter");
            const std::map<std::string, DATA_TYPE> kinds = { { "number", TINT }, { "bool", TBOOL }, { "string", TSTRING } };
            EXPECT_EQ(kinds.at(filter.at("value").get<std::string>()), picked->get_type()) << name;
            if (filter.at("writable"))
                EXPECT_TRUE(picked->isOutput()) << name;
        }
    }
}

TEST_P(DeviceTemplateVariantTest, ADeviceBuiltFromTheTemplateLoadsWithoutAWarning)
{
    const Variant &v = GetParam();
    Generated g;
    const std::string log = loadVariant(v, g);

    for (const char *level: { "[WRN]", "[ERR]", "[CRI]" })
        EXPECT_EQ(std::string::npos, log.find(level)) << g.xml << log;

    auto *dev = ListeRoom::Instance().findIOAs<Device>(v.name);
    ASSERT_NE(nullptr, dev) << g.xml;
    EXPECT_EQ(templateNamed(v.templateId).at("impl"), dev->get_param("impl"));
    EXPECT_FALSE(dev->getLinks().empty());
    for (const Device::Link &l: dev->getLinks())
    {
        EXPECT_NE(nullptr, dev->linkIo(l.name)) << l.name;
        if (!l.command_io.empty())
            EXPECT_NE(nullptr, dev->linkCommandIo(l.name)) << l.name;
    }
    for (const auto &kv: g.owned)
        EXPECT_EQ(v.name, io(kv.second)->getOwnerDevice()) << kv.second;
    EXPECT_FALSE(dev->deviceObject().at("capabilities").at("modes").empty());
}

TEST_P(DeviceTemplateVariantTest, TheDeviceHasTheLinksAndDefaultsOfItsVariant)
{
    const Variant &v = GetParam();
    const Expected want = expectations().at(v.name);
    Generated g;
    loadVariant(v, g);
    auto *dev = ListeRoom::Instance().findIOAs<Device>(v.name);
    ASSERT_NE(nullptr, dev);

    std::set<std::string> roles;
    for (const Device::Link &l: dev->getLinks())
        roles.insert(l.name);
    EXPECT_EQ(want.roles, roles);

    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_string(g.xml.c_str()));
    Strings params;
    for (pugi::xml_node p: doc.first_child().children("calaos:param"))
        params[p.attribute("name").value()] = p.attribute("value").value();
    EXPECT_EQ(want.params, params);
    for (const auto &kv: want.params)
        EXPECT_EQ(kv.second, dev->getDeviceParam(kv.first)) << kv.first;

    //Setpoints start inside their bounds, which the capabilities publish.
    const Json caps = dev->deviceObject().at("capabilities");
    if (v.templateId.rfind("thermostat", 0) == 0)
    {
        const std::map<std::string, double> start = { { "target_temp", 20 },
                                                      { "target_temp_low", 19 },
                                                      { "target_temp_high", 25 } };
        for (const auto &kv: start)
            if (dev->linkIo(kv.first))
                EXPECT_EQ(kv.second, dev->linkIo(kv.first)->get_value_double()) << kv.first;
        EXPECT_EQ(7, caps.at("target_temp").at("min"));
        EXPECT_EQ(30, caps.at("target_temp").at("max"));
        EXPECT_EQ(0.5, caps.at("target_temp").at("step"));
    }
    else if (caps.contains("target_temp"))
    {
        EXPECT_EQ(16, caps.at("target_temp").at("min"));
        EXPECT_EQ(30, caps.at("target_temp").at("max"));
        EXPECT_EQ(v.values.at("temp_step"), caps.at("target_temp").at("step"));
    }
}

class EsphomeVariantTest: public DeviceTemplateVariantTest
{
};

//The topics an ESPHome climate publishes and listens on, on the broker asked.
TEST_P(EsphomeVariantTest, TheIosUseTheTopicsOfEsphome)
{
    const Variant &v = GetParam();
    Generated g;
    loadVariant(v, g);
    auto *dev = ListeRoom::Instance().findIOAs<Device>(v.name);
    ASSERT_NE(nullptr, dev);

    const Strings esphomeName = { { "mode", "mode" },
                                  { "current_temp", "current_temperature" },
                                  { "target_temp", "target_temperature" },
                                  { "target_temp_low", "target_temperature_low" },
                                  { "target_temp_high", "target_temperature_high" },
                                  { "action", "action" },
                                  { "fan_mode", "fan_mode" },
                                  { "swing_mode", "swing_mode" } };
    for (const Device::Link &l: dev->getLinks())
    {
        if (l.name == "remote_temp_source")
            continue;
        IOBase *mqtt = dev->linkIo(l.name);
        ASSERT_NE(nullptr, mqtt) << l.name;
        std::string sub = "salon-clim/calaos/remote_temp", pub = sub;
        if (l.name != "remote_temp")
        {
            sub = "salon-clim/climate/clim_salon/" + esphomeName.at(l.name) + "/state";
            pub = "salon-clim/climate/clim_salon/" + esphomeName.at(l.name) + "/command";
        }
        EXPECT_EQ(sub, mqtt->get_param("topic_sub")) << l.name;
        if (mqtt->isOutput())
        {
            EXPECT_EQ(pub, mqtt->get_param("topic_pub")) << l.name;
            EXPECT_EQ("__##VALUE##__", mqtt->get_param("data")) << l.name;
        }
        EXPECT_EQ(kHost, mqtt->get_param("host")) << l.name;
        EXPECT_EQ(kPort, mqtt->get_param("port")) << l.name;
        for (const char *credential: { "mqtt_user", "mqtt_password" })
        {
            const std::string value = v.values.at(credential);
            const std::string param = std::string(credential).substr(5);
            EXPECT_EQ(!value.empty(), mqtt->get_params().Exists(param)) << l.name << " " << param;
            EXPECT_EQ(value, mqtt->get_param(param)) << l.name << " " << param;
        }
    }
}

//Nothing the template writes is dropped or rewritten by the server.
TEST_P(DeviceTemplateVariantTest, ADeviceBuiltFromTheTemplateRoundTrips)
{
    const Variant &v = GetParam();
    Generated g;
    loadVariant(v, g);
    ASSERT_TRUE(roundTripIo());

    pugi::xml_document generated, saved;
    ASSERT_TRUE(generated.load_string(g.xml.c_str()));
    ASSERT_TRUE(saved.load_string(ioXmlOnDisk().c_str()));
    const pugi::xml_node want = generated.first_child();
    const pugi::xml_node got =
        saved.select_node(("//calaos:device[@id='" + v.name + "']").c_str()).node();
    ASSERT_TRUE(got) << ioXmlOnDisk();

    for (pugi::xml_attribute a: want.attributes())
        EXPECT_STREQ(a.value(), got.attribute(a.name()).value()) << a.name();

    auto children = [](pugi::xml_node n)
    {
        std::vector<pugi::xml_node> all;
        for (pugi::xml_node c: n.children())
            if (c.type() == pugi::node_element)
                all.push_back(c);
        return all;
    };
    const auto wantChildren = children(want);
    const auto gotChildren = children(got);
    ASSERT_EQ(wantChildren.size(), gotChildren.size()) << ioXmlOnDisk();
    for (size_t i = 0; i < wantChildren.size(); i++)
    {
        EXPECT_STREQ(wantChildren[i].name(), gotChildren[i].name()) << i;
        for (pugi::xml_attribute a: wantChildren[i].attributes())
            EXPECT_STREQ(a.value(), gotChildren[i].attribute(a.name()).value())
                << wantChildren[i].attribute("id").value() << " " << a.name();
    }
}

std::string variantName(const ::testing::TestParamInfo<Variant> &info)
{
    return info.param.name;
}

std::vector<Variant> esphomeVariants()
{
    std::vector<Variant> some;
    for (const Variant &v: variants())
        if (v.templateId.rfind("esphome", 0) == 0)
            some.push_back(v);
    return some;
}

INSTANTIATE_TEST_SUITE_P(Templates, DeviceTemplateVariantTest, ::testing::ValuesIn(variants()), variantName);
INSTANTIATE_TEST_SUITE_P(Templates, EsphomeVariantTest, ::testing::ValuesIn(esphomeVariants()), variantName);

namespace
{

//Texts the French keeps as English: a word that is the same in both languages,
//or a label that is only a placeholder around one. Every entry must be hit,
//so that the list cannot outlive the texts.
const std::set<std::string> SAME_IN_FRENCH = { "Auto", "Horizontal", "Modes", "Vertical", "{name} action",
                                               "{name} mode" };

bool isText(const std::vector<std::string> &path, const std::string &key)
{
    if (key == "label" || key == "description" || key == "text")
        return true;
    //The name of a template, and of the IOs it creates; elsewhere "name" is an identifier.
    return key == "name" && (path.size() == 2 || std::find(path.begin(), path.end(), "ios") != path.end());
}

std::set<std::string> placeholders(const std::string &s)
{
    std::set<std::string> out;
    const std::regex re("\\{[a-z_]+\\}");
    for (std::sregex_iterator it(s.begin(), s.end(), re), end; it != end; ++it)
        out.insert(it->str());
    return out;
}

//The YAML of the notes is code: only the prose around it is translated.
std::vector<std::string> codeBlocks(const std::string &s)
{
    std::vector<std::string> out;
    const std::regex re("```[^`]*```");
    for (std::sregex_iterator it(s.begin(), s.end(), re), end; it != end; ++it)
        out.push_back(it->str());
    return out;
}

struct FrenchWalk
{
    std::set<std::string> sameSeen;
    int texts = 0;

    void walk(const Json &en, const Json &fr, std::vector<std::string> &path, const std::string &key)
    {
        std::string where;
        for (const std::string &p: path)
            where += "/" + p;
        where += "/" + key;

        if (en.is_object() || en.is_array())
        {
            ASSERT_EQ(en.type(), fr.type()) << where;
            ASSERT_EQ(en.size(), fr.size()) << where;
            if (!key.empty())
                path.push_back(key);
            size_t i = 0;
            for (auto it = en.begin(); it != en.end(); ++it, ++i)
            {
                if (en.is_object())
                    walk(*it, fr.at(it.key()), path, it.key());
                else
                    walk(*it, fr.at(i), path, std::to_string(i));
            }
            if (!key.empty())
                path.pop_back();
            return;
        }
        if (!en.is_string() || !isText(path, key))
        {
            EXPECT_EQ(en, fr) << where << " is not text and must not change";
            return;
        }

        texts++;
        const std::string e = en.get<std::string>(), f = fr.get<std::string>();
        EXPECT_FALSE(f.empty()) << where;
        EXPECT_EQ(placeholders(e), placeholders(f)) << where << ": " << f;
        EXPECT_EQ(codeBlocks(e), codeBlocks(f)) << where;
        if (e != f)
            return;
        if (SAME_IN_FRENCH.count(e))
            sameSeen.insert(e);
        else
            ADD_FAILURE() << where << " is not translated: " << e;
    }
};

} //namespace

//po/fr.po is compiled and read the way the server reads it, but with LANGUAGE
//under C.UTF-8, which needs no fr_FR locale in the image.
TEST(DeviceTemplatesFrench, EveryTextOfEveryTemplateIsTranslated)
{
#if HAVE_GETTEXT
    const char *srcdir = getenv("abs_top_srcdir");
    ASSERT_NE(nullptr, srcdir) << "abs_top_srcdir is exported by tests/Makefile.am";

    char tmpl[] = "/tmp/templates-fr-XXXXXX";
    ASSERT_NE(nullptr, mkdtemp(tmpl));
    const std::string localedir = tmpl;
    std::filesystem::create_directories(localedir + "/fr/LC_MESSAGES");
    const std::string cmd = "msgfmt -o '" + localedir + "/fr/LC_MESSAGES/" PACKAGE_NAME ".mo' '" + srcdir +
                            "/po/fr.po'";
    const int rc = std::system(cmd.c_str());
    if (rc != 0)
    {
        std::filesystem::remove_all(localedir);
        FAIL() << "msgfmt (gettext tools) is needed to read po/fr.po";
    }

    const Json en = DeviceTemplates::all();

    const std::string oldLocale = setlocale(LC_ALL, nullptr);
    const char *oldLanguage = getenv("LANGUAGE");
    const std::string oldLanguageValue = oldLanguage ? oldLanguage : "";
    const bool localeSet = setlocale(LC_ALL, "C.UTF-8") != nullptr;
    setenv("LANGUAGE", "fr", 1);
    bindtextdomain(PACKAGE_NAME, localedir.c_str());
    textdomain(PACKAGE_NAME);
    const Json fr = DeviceTemplates::all();
    textdomain("messages");
    setlocale(LC_ALL, oldLocale.c_str());
    if (oldLanguage)
        setenv("LANGUAGE", oldLanguageValue.c_str(), 1);
    else
        unsetenv("LANGUAGE");
    std::filesystem::remove_all(localedir);
    ASSERT_TRUE(localeSet) << "C.UTF-8 locale is not available";

    FrenchWalk w;
    std::vector<std::string> path;
    w.walk(en, fr, path, "");
    //Guards against a walk that finds no text at all.
    EXPECT_GT(w.texts, 150);
    EXPECT_EQ(SAME_IN_FRENCH, w.sameSeen) << "an exception no longer applies: remove it";
#else
    GTEST_SKIP() << "built without gettext";
#endif
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    CalaosTest::ExternProcSpawn::teardown();

    fflush(nullptr);
    _exit(ret);
}
