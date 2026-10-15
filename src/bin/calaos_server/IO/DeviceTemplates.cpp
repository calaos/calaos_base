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
#include "DeviceTemplates.h"
#include "FileUtils.h"

#include <fstream>

/* The builders end with `return Json(std::move(x))` and not `return x`: with
 * the copy elided, the closing brace only runs when unwinding, and the
 * coverage gate reads it as an untested line. */

namespace
{

/* ------------------------------------------------------------------------
 * Fields: what the wizard asks.
 * --------------------------------------------------------------------- */

Json field(const string &name, const string &type, const string &label, const string &description,
           bool required)
{
    return { { "name", name },
             { "type", type },
             { "label", label },
             { "description", description },
             { "required", required } };
}

Json text(const string &name, const string &label, const string &description, bool required,
          const string &def = string())
{
    Json f = field(name, "string", label, description, required);
    if (!def.empty())
        f["default"] = def;
    return Json(std::move(f));
}

//Shown masked by the wizard.
Json secretText(const string &name, const string &label, const string &description)
{
    Json f = text(name, label, description, false);
    f["secret"] = true;
    return Json(std::move(f));
}

Json number(const string &name, const string &label, const string &description, double min, double max,
            double step, const string &unit, double def)
{
    Json f = field(name, "number", label, description, true);
    f["min"] = min;
    f["max"] = max;
    f["step"] = step;
    f["unit"] = unit;
    f["default"] = def;
    return Json(std::move(f));
}

Json flag(const string &name, const string &label, const string &description, bool def)
{
    Json f = field(name, "bool", label, description, false);
    f["default"] = def;
    return Json(std::move(f));
}

//A choice of an enum or multi field: the wizard shows the label.
Json item(const string &value, const string &label)
{
    return { { "value", value }, { "label", label } };
}

//Required: one value at least.
Json multi(const string &name, const string &label, const string &description, bool required,
           const Json &values, const vector<string> &def)
{
    Json f = field(name, "multi", label, description, required);
    f["values"] = values;
    f["default"] = def;
    return Json(std::move(f));
}

//An option of an enum field; its params replace the template's ones of the same name.
Json option(const string &value, const string &label, const Json &params)
{
    return { { "value", value }, { "label", label }, { "params", params } };
}

Json choice(const string &name, const string &label, const string &description, const Json &options,
            const string &def)
{
    Json f = field(name, "enum", label, description, true);
    f["values"] = options;
    f["default"] = def;
    return Json(std::move(f));
}

//An IO of the house, picked by the user, that a link targets.
Json pick(const string &name, const string &label, const string &description, bool required,
          const string &valueType, bool writable)
{
    Json f = field(name, "io", label, description, required);
    f["io_filter"] = { { "value", valueType }, { "writable", writable } };
    return Json(std::move(f));
}

Json nameField()
{
    return text("name", _("Name"), _("Name of the device, shown in the interfaces."), true);
}

Json roomField()
{
    return field("room", "room", _("Room"), _("Room the device is created in."), true);
}

//Every template asks for these two first.
Json baseFields()
{
    Json fields = Json::array();
    fields.push_back(nameField());
    fields.push_back(roomField());
    return Json(std::move(fields));
}

/* ------------------------------------------------------------------------
 * Owned IOs, links and params.
 * --------------------------------------------------------------------- */

Json withWhen(Json j, const string &when)
{
    if (!when.empty())
        j["when"] = when;
    return j;
}

Json owned(const string &key, const string &element, const string &type, const Json &params,
           const string &when = string())
{
    return withWhen({ { "key", key }, { "element", element }, { "type", type }, { "params", params } }, when);
}

Json linkOwned(const string &role, const string &key)
{
    return { { "role", role }, { "io", { { "owned", key } } } };
}

Json linkField(const string &role, const string &fieldName)
{
    return { { "role", role }, { "io", { { "field", fieldName } } } };
}

Json param(const string &name, const string &value, const string &when = string())
{
    return withWhen({ { "name", name }, { "value", value } }, when);
}

Json makeTemplate(const string &id, const string &impl, const string &name, const string &description,
                  const Json &fields, const Json &ios, const Json &links, const Json &params)
{
    return { { "id", id },
             { "family", "climate" },
             { "impl", impl },
             { "name", name },
             { "description", description },
             { "attributes", { { "name", "{name}" }, { "visible", "true" } } },
             { "fields", fields },
             { "ios", ios },
             { "links", links },
             { "params", params } };
}

/* ------------------------------------------------------------------------
 * ESPHome climate over MQTT (proxy).
 * --------------------------------------------------------------------- */

const char *const REMOTE_TOPIC = "{topic_prefix}/calaos/remote_temp";

vector<Json> mqttFields()
{
    return { text("topic_prefix", _("ESPHome topic prefix"),
                  _("topic_prefix of the mqtt: block of the ESPHome node; its node name by default."), true),
             text("object_id", _("Climate object id"),
                  _("Object id of the climate entity: its name in lower case, spaces and punctuation "
                    "turned into underscores (Clim salon gives clim_salon)."),
                  true),
             text("mqtt_host", _("MQTT broker"), _("Address of the MQTT broker the node publishes to."), true,
                  "127.0.0.1"),
             number("mqtt_port", _("MQTT port"), _("TCP port of the MQTT broker."), 1, 65535, 1, "", 1883),
             text("mqtt_user", _("MQTT user"), _("User of the MQTT broker, if it asks for one."), false),
             secretText("mqtt_password", _("MQTT password"), _("Password of that user.")) };
}

//An ESPHome MQTT climate publishes <prefix>/climate/<object_id>/<what>/state
//and listens on .../<what>/command.
Json mqttIo(const string &name, const string &stateTopic, const Json &extra = Json::object())
{
    Json p = { { "name", name },
               { "visible", "false" },
               { "host", "{mqtt_host}" },
               { "port", "{mqtt_port}" },
               { "user", "{mqtt_user}" },
               { "password", "{mqtt_password}" },
               { "topic_sub", stateTopic },
               //The node says online there, and its last will says offline.
               { "connected_status_topic", "{topic_prefix}/status" },
               { "connected_status_expr", "value == 'online'" } };
    p.update(extra);
    return Json(std::move(p));
}

string esphomeTopic(const string &what, const string &way)
{
    return "{topic_prefix}/climate/{object_id}/" + what + "/" + way;
}

Json esphomeOutput(const string &name, const string &what, const Json &extra = Json::object())
{
    Json p = { { "topic_pub", esphomeTopic(what, "command") }, { "data", "__##VALUE##__" } };
    p.update(extra);
    return mqttIo(name, esphomeTopic(what, "state"), p);
}

Json setpointBounds()
{
    return { { "value_min", "{temp_min}" }, { "value_max", "{temp_max}" }, { "step", "{temp_step}" }, { "unit", "°C" } };
}

Json fanModesField()
{
    const Json values = { item("auto", _("Auto")), item("on", _("On")), item("off", _("Off")),
                          item("low", _("Low")), item("medium", _("Medium")), item("high", _("High")),
                          item("middle", _("Middle")), item("focus", _("Focus")), item("diffuse", _("Diffuse")),
                          item("quiet", _("Quiet")) };
    return multi("fan_modes", _("Fan modes"), _("Fan modes the climate accepts. None: no fan control."), false,
                 values, {});
}

Json swingModesField()
{
    return multi("swing_modes", _("Swing modes"), _("Swing modes the climate accepts. None: no swing control."),
                 false,
                 { item("off", _("Off")), item("both", _("Both")), item("vertical", _("Vertical")),
                   item("horizontal", _("Horizontal")) },
                 {});
}

Json modesField(const vector<string> &def)
{
    const Json values = { item("off", _("Off")), item("heat", _("Heat")), item("cool", _("Cool")),
                          item("auto", _("Auto")), item("dry", _("Dry")), item("fan_only", _("Fan only")) };
    return multi("modes", _("Modes"), _("Modes the climate offers. One at least."), true, values, def);
}

Json esphomeClimate()
{
    Json fields = baseFields();
    for (const Json &f: mqttFields())
        fields.push_back(f);
    fields.push_back(modesField({ "off", "heat", "cool", "auto", "dry", "fan_only" }));
    fields.push_back(flag("two_point", _("Two setpoints"),
                          _("The climate takes a low and a high setpoint (target_temperature_low and _high) "
                            "instead of a single one, as a dual setpoint ESPHome thermostat does."),
                          false));
    fields.push_back(number("temp_min", _("Lowest setpoint"), _("Lowest setpoint the climate accepts."), 0, 50, 0.5,
                            "°C", 16));
    fields.push_back(number("temp_max", _("Highest setpoint"), _("Highest setpoint the climate accepts."), 0, 50, 0.5,
                            "°C", 30));
    fields.push_back(number("temp_step", _("Setpoint step"), _("Step of the setpoint."), 0.1, 5, 0.1, "°C", 0.5));
    fields.push_back(flag("with_action", _("Reports its action"),
                          _("The climate publishes what it is doing (heating, cooling, idle). Not every "
                            "platform does."),
                          true));
    fields.push_back(fanModesField());
    fields.push_back(swingModesField());
    fields.push_back(pick("remote_temp_source", _("Room sensor"),
                          _("A sensor whose temperature Calaos relays to the climate, which then regulates on it. "
                            "ESPHome needs a few lines of YAML to take it (see the notes): Calaos publishes it on "
                            "<topic_prefix>/calaos/remote_temp."),
                          false, "number", false));

    const Json ios = {
        owned("mode", "output", "MqttOutputString", esphomeOutput(_("{name} mode"), "mode")),
        owned("current_temp", "input", "MqttInputTemp",
              mqttIo(_("{name} temperature"), esphomeTopic("current_temperature", "state"))),
        owned("target_temp", "output", "MqttOutputAnalog",
              esphomeOutput(_("{name} setpoint"), "target_temperature", setpointBounds()), "!two_point"),
        owned("target_temp_low", "output", "MqttOutputAnalog",
              esphomeOutput(_("{name} low setpoint"), "target_temperature_low", setpointBounds()), "two_point"),
        owned("target_temp_high", "output", "MqttOutputAnalog",
              esphomeOutput(_("{name} high setpoint"), "target_temperature_high", setpointBounds()), "two_point"),
        owned("action", "input", "MqttInputString", mqttIo(_("{name} action"), esphomeTopic("action", "state")),
              "with_action"),
        owned("fan_mode", "output", "MqttOutputString", esphomeOutput(_("{name} fan"), "fan_mode"), "fan_modes"),
        owned("swing_mode", "output", "MqttOutputString", esphomeOutput(_("{name} swing"), "swing_mode"),
              "swing_modes"),
        //Read back from its own topic: MqttCtrl refuses an IO without topic_sub.
        owned("remote_temp", "output", "MqttOutputAnalog",
              mqttIo(_("{name} remote temperature"), REMOTE_TOPIC,
                     { { "topic_pub", REMOTE_TOPIC }, { "data", "__##VALUE##__" } }),
              "remote_temp_source"),
    };

    const Json links = { linkOwned("mode", "mode"),
                         linkOwned("current_temp", "current_temp"),
                         linkOwned("target_temp", "target_temp"),
                         linkOwned("target_temp_low", "target_temp_low"),
                         linkOwned("target_temp_high", "target_temp_high"),
                         linkOwned("action", "action"),
                         linkOwned("fan_mode", "fan_mode"),
                         linkOwned("swing_mode", "swing_mode"),
                         linkOwned("remote_temp", "remote_temp"),
                         linkField("remote_temp_source", "remote_temp_source") };

    //ESPHome reads "auto" as a schedule of the appliance; the range is heat_cool.
    const Json params = { param("modes", "{modes}"),
                          param("auto_payload", "heat_cool"),
                          param("fan_modes", "{fan_modes}"),
                          param("swing_modes", "{swing_modes}"),
                          param("remote_temp_keepalive", "120", "remote_temp_source"),
                          param("sensor_timeout", "9000", "remote_temp_source") };

    const string yaml =
        _("ESPHome takes no remote temperature over MQTT out of the box. For a Mitsubishi CN105 climate (its id "
          "being hp), add to the YAML of the node:") +
        string("\n\n```yaml\nmqtt:\n  on_message:\n    - topic: ") + REMOTE_TOPIC +
        "\n      then:\n        - lambda: |-\n            auto t = parse_number<float>(x);\n"
        "            if (t.has_value()) id(hp).set_remote_temperature(*t);\n```\n\n" +
        _("For a climate that regulates on a sensor of its own (thermostat platform), make the relayed "
          "temperature that sensor; the timeout turns it into NaN, and the thermostat off, when Calaos stops "
          "relaying:") +
        "\n\n```yaml\nsensor:\n  - platform: mqtt_subscribe\n    id: calaos_temp\n    topic: " + REMOTE_TOPIC +
        "\n    filters:\n      - timeout: 5min\nclimate:\n  - platform: thermostat\n    sensor: calaos_temp\n```\n";

    Json t = makeTemplate("esphome_climate", "proxy", _("ESPHome climate (MQTT)"),
                          _("A heat pump or an air conditioner run by ESPHome, which regulates by itself: Calaos "
                            "reads its state over MQTT and sends the commands. Optionally, Calaos relays the "
                            "temperature of a room sensor to it."),
                          fields, ios, links, params);
    t["notes"] = Json::array({ withWhen({ { "text", yaml } }, "remote_temp_source") });
    return Json(std::move(t));
}

Json esphomeClimateIr()
{
    Json fields = baseFields();
    for (const Json &f: mqttFields())
        fields.push_back(f);
    fields.push_back(modesField({ "off", "heat", "cool", "auto" }));
    fields.push_back(flag("with_current_temp", _("Room temperature"),
                          _("The IR climate has a sensor: option and publishes current_temperature."), false));
    fields.push_back(number("temp_min", _("Lowest setpoint"), _("Lowest setpoint the climate accepts."), 0, 50, 0.5,
                            "°C", 16));
    fields.push_back(number("temp_max", _("Highest setpoint"), _("Highest setpoint the climate accepts."), 0, 50, 0.5,
                            "°C", 30));
    fields.push_back(number("temp_step", _("Setpoint step"), _("Step of the setpoint."), 0.1, 5, 0.1, "°C", 1));
    fields.push_back(fanModesField());
    fields.push_back(swingModesField());

    const Json ios = {
        owned("mode", "output", "MqttOutputString", esphomeOutput(_("{name} mode"), "mode")),
        owned("current_temp", "input", "MqttInputTemp",
              mqttIo(_("{name} temperature"), esphomeTopic("current_temperature", "state")), "with_current_temp"),
        owned("target_temp", "output", "MqttOutputAnalog",
              esphomeOutput(_("{name} setpoint"), "target_temperature", setpointBounds())),
        owned("fan_mode", "output", "MqttOutputString", esphomeOutput(_("{name} fan"), "fan_mode"), "fan_modes"),
        owned("swing_mode", "output", "MqttOutputString", esphomeOutput(_("{name} swing"), "swing_mode"),
              "swing_modes"),
    };

    const Json links = { linkOwned("mode", "mode"), linkOwned("current_temp", "current_temp"),
                         linkOwned("target_temp", "target_temp"), linkOwned("fan_mode", "fan_mode"),
                         linkOwned("swing_mode", "swing_mode") };

    //climate_ir offers heat_cool, never auto.
    const Json params = { param("modes", "{modes}"), param("auto_payload", "heat_cool"),
                          param("fan_modes", "{fan_modes}"), param("swing_modes", "{swing_modes}") };

    return makeTemplate("esphome_climate_ir", "proxy", _("ESPHome IR climate (MQTT)"),
                        _("An air conditioner driven by infrared from ESPHome (climate_ir platforms). It reports "
                          "no action and cannot take a remote temperature."),
                        fields, ios, links, params);
}

/* ------------------------------------------------------------------------
 * Thermostats: Calaos regulates.
 * --------------------------------------------------------------------- */

Json setpoint(const string &key, const string &name, const string &value)
{
    return owned(key, "internal", "InternalInt",
                 { { "name", name },
                   { "visible", "false" },
                   { "save", "true" },
                   { "value", value },
                   { "value_min", "7" },
                   { "value_max", "30" },
                   { "step", "0.5" },
                   { "unit", "°C" } });
}

Json actionIo()
{
    return owned("action", "internal", "InternalString", { { "name", _("{name} action") }, { "visible", "false" } });
}

Json sensorField()
{
    return pick("current_temp", _("Temperature sensor"),
                _("The room temperature the thermostat regulates on. A sensor that only reports its changes "
                  "may be taken for a silent one."),
                true, "number", false);
}

Json outputField(const string &name, const string &label)
{
    return pick(name, label,
                _("Relay or contact the thermostat switches. It belongs to this thermostat alone: rules must not "
                  "write it."),
                true, "bool", true);
}

/* Lock-out and hysteresis depend on what the outputs drive; sensor_timeout
 * depends on the sensor (see docs/17_device_templates.md for the reasons). */
Json lockouts(const string &hysteresis, const string &minOn, const string &minOff)
{
    return { { "hysteresis", hysteresis }, { "min_on_time", minOn }, { "min_off_time", minOff } };
}

Json withRange(Json params, const string &minRange)
{
    params["min_range"] = minRange;
    return params;
}

//The template's own values are those of the default option, so a wizard that
//ignores options still writes a working device.
Json thermostatParams(const string &emitter, const Json &preset)
{
    Json params = Json::array({ param("emitter", emitter) });
    for (const char *name: { "hysteresis", "min_on_time", "min_off_time", "min_range" })
    {
        if (preset.contains(name))
            params.push_back(param(name, preset[name].get<string>()));
    }
    params.push_back(param("sensor_timeout", "9000"));
    params.push_back(param("valid_min", "-30"));
    params.push_back(param("valid_max", "60"));
    params.push_back(param("put_back_delay", "1"));
    params.push_back(param("put_back_delay_max", "60"));
    return Json(std::move(params));
}

Json thermostatHeat()
{
    const Json electric = lockouts("0.3", "120", "120");
    const Json options = { option("electric", _("Electric heaters"), electric),
                           option("water_radiator", _("Water radiators (boiler, zone valve)"),
                                  lockouts("0.5", "300", "300")),
                           option("floor", _("Underfloor heating"), lockouts("0.5", "900", "900")) };
    Json fields = baseFields();
    fields.push_back(sensorField());
    fields.push_back(outputField("output_heat", _("Heating output")));
    fields.push_back(choice("emitter", _("Emitter"),
                            _("What the output drives: it sets the hysteresis and the minimum on and off times."),
                            options, "electric"));
    const Json ios = { setpoint("target_temp", _("{name} setpoint"), "20"), actionIo() };
    const Json links = { linkField("current_temp", "current_temp"), linkOwned("target_temp", "target_temp"),
                         linkOwned("action", "action"), linkField("output_heat", "output_heat") };

    return makeTemplate("thermostat_heat", "thermostat", _("Heating thermostat"),
                        _("Calaos switches a heating output from a room sensor and a setpoint."), fields, ios, links,
                        thermostatParams("{emitter}", electric));
}

Json rangeIos()
{
    return { setpoint("target_temp", _("{name} setpoint"), "20"),
             setpoint("target_temp_low", _("{name} low setpoint"), "19"),
             setpoint("target_temp_high", _("{name} high setpoint"), "25"), actionIo() };
}

Json rangeLinks()
{
    return { linkField("current_temp", "current_temp"), linkOwned("target_temp", "target_temp"),
             linkOwned("target_temp_low", "target_temp_low"), linkOwned("target_temp_high", "target_temp_high"),
             linkOwned("action", "action"), linkField("output_heat", "output_heat"),
             linkField("output_cool", "output_cool") };
}

Json thermostatHeatCool()
{
    const Json heatPump = withRange(lockouts("0.5", "600", "300"), "2");
    const Json options = { option("heat_pump", _("Reversible heat pump on contacts"), heatPump),
                           option("fan_coil", _("Fan coils (valves on a water loop)"),
                                  withRange(lockouts("0.5", "120", "120"), "1")) };
    Json fields = baseFields();
    fields.push_back(sensorField());
    fields.push_back(outputField("output_heat", _("Heating output")));
    fields.push_back(outputField("output_cool", _("Cooling output")));
    fields.push_back(choice("emitter", _("Emitter"),
                            _("What the outputs drive: it sets the hysteresis, the minimum on and off times and "
                              "the minimum range."),
                            options, "heat_pump"));

    return makeTemplate("thermostat_heat_cool", "thermostat", _("Heating and cooling thermostat"),
                        _("Calaos switches a heating and a cooling output from a room sensor: one setpoint in heat "
                          "and cool, a range in auto."),
                        fields, rangeIos(), rangeLinks(), thermostatParams("{emitter}", heatPump));
}

Json thermostatFloorHeatCool()
{
    Json fields = baseFields();
    fields.push_back(sensorField());
    fields.push_back(pick("humidity", _("Humidity sensor"),
                          _("Relative humidity of the room. Cooling stops while it reads humidity_max or more, "
                            "has not read, or is silent: the floor would sweat."),
                          true, "number", false));
    fields.push_back(number("humidity_max", _("Maximum humidity"),
                            _("Relative humidity from which cooling stops. At 60 % and 25 °C the dew point is about "
                              "17 °C, under the surface of a floor fed with water at 16 °C."),
                            40, 90, 1, "%", 60));
    fields.push_back(outputField("output_heat", _("Heating output")));
    fields.push_back(outputField("output_cool", _("Cooling output")));

    Json links = rangeLinks();
    links.push_back(linkField("humidity", "humidity"));

    Json params = thermostatParams("floor", withRange(lockouts("0.5", "900", "900"), "2"));
    params.push_back(param("humidity_max", "{humidity_max}"));

    return makeTemplate("thermostat_floor_heat_cool", "thermostat", _("Reversible floor thermostat"),
                        _("Calaos heats and cools through the floor, and stops cooling before the dew point."), fields,
                        rangeIos(), links, params);
}

} //namespace

Json DeviceTemplates::all()
{
    return { { "format", FORMAT },
             { "templates",
               { esphomeClimate(), esphomeClimateIr(), thermostatHeat(), thermostatHeatCool(),
                 thermostatFloorHeatCool() } } };
}

bool DeviceTemplates::generate(const string &path)
{
    if (!FileUtils::exists(path))
        FileUtils::mkpath(path);

    //Same writer settings as io_doc.json: pure ASCII, no throw on bad UTF-8.
    std::ofstream file(path + "/" + FILE_NAME);
    file << all().dump(4, ' ', true, Json::error_handler_t::replace);
    file.close();
    if (file.fail())
    {
        cError() << "Unable to write " << path << "/" << FILE_NAME;
        return false;
    }
    return true;
}
