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
#include "ClimateBase.h"
#include "libuvw.h"
#include "InPlageHoraire.h"

#include <algorithm>
#include <cmath>
#include <sstream>

using namespace Calaos;

namespace
{

const char *const LOW = "target_temp_low";
const char *const HIGH = "target_temp_high";
const char *const REFUSED_BY_IO = "the linked IO refused the value";

ClimateBase::LoopClock injectedLoopClock = nullptr;

//A disabled or read-only IO refuses a write: the log must still say why.
bool written(bool ok, string &reason)
{
    if (!ok)
        reason = REFUSED_BY_IO;
    return ok;
}

//Indexed by the enum values, in declaration order.
const char *const MODE_NAMES[] = { "off", "heat", "cool", "auto", "dry", "fan_only" };
const char *const ACTION_NAMES[] = { "off", "idle", "heating", "cooling", "drying", "fan" };

template <class E, size_t N>
bool valueOf(const char *const (&names)[N], const string &name, E &value)
{
    for (size_t i = 0; i < N; i++)
    {
        if (name == names[i])
        {
            value = static_cast<E>(i);
            return true;
        }
    }
    return false;
}

bool numberParam(IOBase *io, const char *name, double &out)
{
    const string s = io->get_param(name);
    double v = 0;
    if (!Utils::from_string(s, v) || !std::isfinite(v))
        return false;
    out = v;
    return true;
}

bool nonNegative(const string &s, double &out)
{
    double v = 0;
    if (!Utils::from_string(s, v) || !std::isfinite(v) || v < 0)
        return false;
    out = v;
    return true;
}

const char *const PROGRAM_ENABLED = "program_enabled";
const char *const COOL_DEFAULT_OFF = "cool_default_off";

string levelRole(const string &set, int n)
{
    return set + "_level" + Utils::to_string(n);
}

string levelSetpointRole(const string &set, int n)
{
    return levelRole(set, n) + "_setpoint";
}

string defaultRole(const string &set)
{
    return set + "_default";
}

} //namespace

bool ClimateBase::hiddenFromRemoteUi(const string &role) const
{
    if (role == PROGRAM_ENABLED || role == COOL_DEFAULT_OFF)
        return true;
    for (const char *set: { "heat", "cool" })
    {
        if (role == defaultRole(set) || role.rfind(set + string("_level"), 0) == 0)
            return true;
    }
    return false;
}

void ClimateBase::trimForRemoteUi(Json &deviceObject) const
{
    for (const char *member: { "capabilities", "state" })
    {
        auto it = deviceObject.find(member);
        if (it != deviceObject.end() && it->is_object())
            it->erase("program");
    }

    auto ios = deviceObject.find("ios");
    if (ios != deviceObject.end() && ios->is_object())
    {
        for (auto it = ios->begin(); it != ios->end();)
        {
            if (hiddenFromRemoteUi(it.key()))
                it = ios->erase(it);
            else
                ++it;
        }
    }
}

ClimateBase::ClimateBase(Params &p):
    Device(p)
{
    ioDoc->deviceFamilySet("climate");
    ioDoc->paramAdd("min_range", _("Minimum gap in degrees between target_temp_low and target_temp_high. "
                                   "A range closer than that is refused."),
                    IODoc::TYPE_FLOAT, false, "0", false, _("Minimum range"));
    ioDoc->paramInChild("min_range");
    ioDoc->paramAdd("sensor_timeout", _("Seconds a linked sensor may go without a reading, a repeated value "
                                        "included, before it counts as stale. A positive number; the default "
                                        "outlasts one lost hourly Zigbee report. A sensor older than half of "
                                        "it is asked for a reading when its protocol can (KNX)."),
                    IODoc::TYPE_FLOAT, false, Utils::to_string(DEFAULT_SENSOR_TIMEOUT), false, _("Sensor timeout"));
    ioDoc->paramInChild("sensor_timeout");
}

void ClimateBase::documentModesParam()
{
    ioDoc->paramAdd("modes", _("Comma separated modes the device offers (off, heat, cool, auto, dry, fan_only). "
                               "Default to all of them."),
                    IODoc::TYPE_STRING, false, string(), false, _("Modes"));
    ioDoc->paramInChild("modes");
}

void ClimateBase::documentFanSwing()
{
    ioDoc->paramAdd("fan_modes", _("Comma separated fan modes the appliance accepts. Without it every fan_mode "
                                   "command is refused."),
                    IODoc::TYPE_STRING, false, string(), false, _("Fan modes"));
    ioDoc->paramAdd("swing_modes", _("Comma separated swing modes the appliance accepts. Without it every "
                                     "swing_mode command is refused."),
                    IODoc::TYPE_STRING, false, string(), false, _("Swing modes"));
    ioDoc->paramInChild("fan_modes");
    ioDoc->paramInChild("swing_modes");
    ioDoc->commandFieldEnum("fan_mode", _("Fan mode, one of the fan_modes param."), {}, { "fan_mode" },
                            "fan_modes");
    ioDoc->commandFieldEnum("swing_mode", _("Swing mode, one of the swing_modes param."), {},
                            { "swing_mode" }, "swing_modes");
}

void ClimateBase::documentCommand(const vector<ClimateMode> &modes, const string &modeLink,
                                  const string &modeNote, const IODoc::ValueLinks &modeRequires)
{
    vector<string> names;
    for (ClimateMode m: modes)
        names.push_back(modeName(m));

    vector<string> modeLinks;
    if (!modeLink.empty())
        modeLinks.push_back(modeLink);

    const string unit = DEFAULT_TEMP_UNIT;
    const string bounds = _(" Bounds are those of the linked IO (value_min, value_max, step, unit), "
                            "else 7 to 35 by 0.5 in °C. The effective ones are published in the "
                            "capabilities.");
    ioDoc->commandFieldEnum("mode",
                            _("Operating mode. The modes an instance accepts are published in its capabilities.") +
                                modeNote,
                            names, modeLinks, string(), modeRequires);
    ioDoc->commandFieldNumber("target_temp", _("The setpoint used in heat and cool.") + bounds,
                              DEFAULT_TEMP_MIN, DEFAULT_TEMP_MAX, DEFAULT_TEMP_STEP, unit,
                              { "target_temp" });
    //Both bounds of a range are needed: min_range is checked on the pair.
    ioDoc->commandFieldNumber("target_temp_low", _("Low bound of the range used in auto.") + bounds,
                              DEFAULT_TEMP_MIN, DEFAULT_TEMP_MAX, DEFAULT_TEMP_STEP, unit,
                              { "target_temp_low", "target_temp_high" });
    ioDoc->commandFieldNumber("target_temp_high", _("High bound of the range used in auto.") + bounds,
                              DEFAULT_TEMP_MIN, DEFAULT_TEMP_MAX, DEFAULT_TEMP_STEP, unit,
                              { "target_temp_low", "target_temp_high" });
}

const vector<ClimateMode> &ClimateBase::allModes()
{
    static const vector<ClimateMode> modes = { ClimateMode::Off, ClimateMode::Heat,
                                               ClimateMode::Cool, ClimateMode::Auto,
                                               ClimateMode::Dry, ClimateMode::FanOnly };
    return modes;
}

string ClimateBase::modeName(ClimateMode mode)
{
    return MODE_NAMES[static_cast<size_t>(mode)];
}

bool ClimateBase::parseMode(const string &name, ClimateMode &mode)
{
    return valueOf(MODE_NAMES, name, mode);
}

string ClimateBase::actionName(ClimateAction action)
{
    return ACTION_NAMES[static_cast<size_t>(action)];
}

bool ClimateBase::parseAction(const string &name, ClimateAction &action)
{
    return valueOf(ACTION_NAMES, name, action);
}

void ClimateBase::resolveLinks()
{
    Device::resolveLinks();

    //Logged once here rather than at each publication.
    const string id = get_param("id");
    checkModesParam();
    for (const char *role: { "fan_mode", "swing_mode" })
    {
        const string list = string(role) + "s";
        if (linkIo(role) && paramList(list).empty())
            cErrorDom("device") << "Device(" << id << "): link '" << role << "' without a "
                                << list << " param, every value will be refused";
    }
    double gap;
    if (hasDeviceParam("min_range") && !nonNegative(getDeviceParam("min_range"), gap))
        cErrorDom("device") << "Device(" << id << "): min_range '" << getDeviceParam("min_range")
                            << "' is not a non-negative number, 0 is used";
    if (hasDeviceParam("sensor_timeout"))
    {
        double v = 0;
        if (Utils::from_string(getDeviceParam("sensor_timeout"), v) && std::isfinite(v) && v > 0)
            sensorTimeoutS = v;
        else
            cErrorDom("device") << "Device(" << id << "): sensor_timeout '"
                                << getDeviceParam("sensor_timeout")
                                << "' is not a positive number of seconds, "
                                << DEFAULT_SENSOR_TIMEOUT << " is used";
    }
    if (linkIo(LOW) && linkIo(HIGH))
    {
        const Bounds b = rangeBounds();
        if (minRange() > b.max - b.min)
            cErrorDom("device") << "Device(" << id << "): min_range " << minRange()
                                << " is wider than the setpoint bounds " << b.min << ".." << b.max
                                << ", every range will be refused";
    }
}

void ClimateBase::warnIfChangesOnly(const string &role)
{
    IOBase *io = linkIo(role);
    if (io && !io->datesEveryReading())
        cWarningDom("device") << "Device(" << get_param("id") << "): " << role << " '"
                              << io->get_param("id") << "' (" << io->get_param("type")
                              << ") only reports changes: steady for " << sensorTimeout()
                              << " s, it counts as silent";
}

void ClimateBase::requestReadingIfOld(const string &role)
{
    IOBase *io = linkIo(role);
    const auto last = io ? io->lastReading() : std::nullopt;
    const std::chrono::duration<double> age =
        last ? std::chrono::steady_clock::now() - *last : std::chrono::duration<double>(0);
    if (io && (!last || age.count() > sensorTimeoutS / 2))
        io->requestReading();
}

ClimateBase::Freshness ClimateBase::freshness(IOBase *io) const
{
    //An ESPHome last will, a Zigbee availability topic: its last value stays.
    if (io && io->getStatusInfo()["connected"] == "false")
        return Freshness::Disconnected;
    if (!io || !io->lastReading())
        return Freshness::NotRead;
    const std::chrono::duration<double> age = std::chrono::steady_clock::now() - *io->lastReading();
    return age.count() < sensorTimeoutS ? Freshness::Fresh : Freshness::Silent;
}

string ClimateBase::staleReason(Freshness f) const
{
    if (f == Freshness::Disconnected)
        return "reports itself disconnected";
    std::ostringstream reason;
    reason << "has sent nothing for " << sensorTimeoutS << " s";
    return reason.str();
}

vector<ClimateMode> ClimateBase::supportedModes()
{
    if (!linkIo("mode") || !isWritable("mode"))
        return {};
    if (!hasDeviceParam("modes"))
        return allModes();

    const vector<string> names = paramList("modes");
    vector<ClimateMode> res;
    for (ClimateMode m: allModes())
    {
        if (std::find(names.begin(), names.end(), modeName(m)) != names.end())
            res.push_back(m);
    }
    return res;
}

ClimateBase::Bounds ClimateBase::setpointBounds(const string &role)
{
    Bounds b{ DEFAULT_TEMP_MIN, DEFAULT_TEMP_MAX, DEFAULT_TEMP_STEP, DEFAULT_TEMP_UNIT };
    IOBase *io = writeTarget(role);
    if (!io)
        return b;

    double lo = 0, hi = 0;
    const bool hasMin = numberParam(io, "value_min", lo);
    const bool hasMax = numberParam(io, "value_max", hi);
    //The IO ignores both bounds then, so the defaults stand.
    if (!(hasMin && hasMax && lo > hi))
    {
        if (hasMin)
            b.min = lo;
        if (hasMax)
            b.max = hi;
        //A single bound beyond the opposite default must not invert the range.
        if (hasMin && !hasMax)
            b.max = std::max(b.max, b.min);
        if (hasMax && !hasMin)
            b.min = std::min(b.min, b.max);
    }

    double step = 0;
    if (numberParam(io, "step", step) && step > 0)
        b.step = step;

    if (!io->get_param("unit").empty())
        b.unit = io->get_param("unit");

    //Not a named return value: gcov charges its unwinding to the closing
    //brace, which the diff-coverage gate reads as a line never run.
    return Bounds(b);
}

ClimateBase::Bounds ClimateBase::rangeBounds()
{
    const Bounds lo = setpointBounds(LOW);
    const Bounds hi = setpointBounds(HIGH);
    return Bounds{ lo.min, hi.max, lo.step, lo.unit };
}

double ClimateBase::minRange() const
{
    double v = 0;
    nonNegative(getDeviceParam("min_range"), v);
    return v;
}

bool ClimateBase::currentMode(ClimateMode &mode)
{
    IOBase *io = linkIo("mode");
    return io && parseMode(io->get_value_string(), mode);
}

bool ClimateBase::writeMode(ClimateMode mode)
{
    IOBase *io = writeTarget("mode");
    return io && io->set_value(modeName(mode));
}

vector<string> ClimateBase::paramList(const string &name) const
{
    vector<string> tokens, res;
    Utils::split(getDeviceParam(name), tokens, ",");
    for (const string &t: tokens)
    {
        const string item = Utils::trim(t);
        if (!item.empty() && std::find(res.begin(), res.end(), item) == res.end())
            res.push_back(item);
    }
    return res;
}

void ClimateBase::checkModesParam()
{
    const string id = get_param("id");
    for (const string &m: paramList("modes"))
    {
        ClimateMode mode;
        if (!parseMode(m, mode))
            cErrorDom("device") << "Device(" << id << "): unknown mode '" << m
                                << "' in the modes param, ignored";
    }
    if (hasDeviceParam("modes") && paramList("modes").empty())
        cErrorDom("device") << "Device(" << id << "): empty modes param, no mode can be selected";
}

IOBase *ClimateBase::writeTarget(const string &role)
{
    IOBase *io = linkCommandIo(role);
    return io ? io : linkIo(role);
}

bool ClimateBase::isWritable(const string &role)
{
    VAR_UNUSED(role);
    return true;
}

bool ClimateBase::rangeAvailable()
{
    return linkIo(LOW) && linkIo(HIGH) && isWritable(LOW) && isWritable(HIGH);
}

bool ClimateBase::rangeBound(const string &role, double &value)
{
    IOBase *io = linkIo(role);
    if (!io || !io->hasValue())
        return false;
    value = io->get_value_double();
    return true;
}

bool ClimateBase::writeRangeBound(const string &role, double value)
{
    IOBase *io = writeTarget(role);
    return io && io->set_value(value);
}

bool ClimateBase::applyCommand(const Json &command)
{
    bool applied = applyFields(command, { LOW, HIGH });
    if (command.contains(LOW) || command.contains(HIGH))
        applied = applyRange(command) || applied;
    return applied;
}

/* low and high are checked as a pair against min_range, the missing one
 * taken from the current state. A pair that violates it is refused whole:
 * moving the other bound instead would change a setpoint nobody asked to
 * change, and keeping one of the two could leave a range the heat pump
 * cannot honour. */
bool ClimateBase::applyRange(const Json &command)
{
    if (!rangeAvailable())
    {
        logRefusedField("target_temp_range", "unknown field for this device");
        return false;
    }

    //0 is the default of a bound never written, not a setpoint to check against.
    double low = 0;
    double high = 0;
    const bool lowKnown = rangeBound(LOW, low);
    const bool highKnown = rangeBound(HIGH, high);
    if ((!command.contains(LOW) && !lowKnown) || (!command.contains(HIGH) && !highKnown))
    {
        logRefusedField("target_temp_range", "the other bound is not known yet, send both");
        return false;
    }

    const double oldHigh = high;
    string reason;
    for (const char *role: { LOW, HIGH })
    {
        if (!command.contains(role))
            continue;
        const Bounds b = setpointBounds(role);
        double &v = string(role) == LOW ? low : high;
        if (!numberField(command.at(role), b.min, b.max, b.step, v, reason))
        {
            logRefusedField("target_temp_range", string(role) + ": " + reason);
            return false;
        }
    }

    const double minGap = minRange();
    const string pair = "low " + Utils::to_string(low);
    if (low > high)
    {
        logRefusedField("target_temp_range", pair + " is above high " + Utils::to_string(high));
        return false;
    }
    //Tolerance for the binary residue of decimal steps (16.4 - 14.4).
    if (high - low < minGap - 1e-9)
    {
        logRefusedField("target_temp_range", pair + " and high " + Utils::to_string(high) +
                                                 " are closer than min_range " +
                                                 Utils::to_string(minGap));
        return false;
    }

    /* Each write is seen at once by the impl through the change hook. Raising
     * writes high first and lowering writes low first, so the pair in between
     * keeps min_range whenever the old pair did. */
    bool ok = true;
    auto write = [&](const char *role, double v)
    {
        if (command.contains(role))
            ok = writeRangeBound(role, v) && ok;
    };
    if (high > oldHigh)
    {
        write(HIGH, high);
        write(LOW, low);
    }
    else
    {
        write(LOW, low);
        write(HIGH, high);
    }
    if (!ok)
        logRefusedField("target_temp_range", REFUSED_BY_IO);
    return ok;
}

bool ClimateBase::applyCommandField(const string &field, const Json &value, string &reason)
{
    if (field == "mode")
    {
        const vector<ClimateMode> modes = supportedModes();
        if (!modes.empty())
        {
            vector<string> names;
            for (ClimateMode m: modes)
                names.push_back(modeName(m));
            string name;
            ClimateMode mode;
            if (!enumField(value, names, name, reason) || !parseMode(name, mode))
                return false;
            ClimateMode before;
            const bool changed = !currentMode(before) || before != mode;
            if (!written(writeMode(mode), reason))
                return false;
            //Before the other fields: a target_temp of the same command wins.
            if (changed)
                applyProgram();
            return true;
        }
    }
    else if (field == "target_temp" && linkIo(field) && isWritable(field))
    {
        const Bounds b = setpointBounds(field);
        double v;
        if (!numberField(value, b.min, b.max, b.step, v, reason))
            return false;
        return written(writeTarget(field)->set_value(v), reason);
    }
    else if ((field == "fan_mode" || field == "swing_mode") && linkIo(field) && isWritable(field))
    {
        string v;
        if (!enumField(value, paramList(field + "s"), v, reason))
            return false;
        return written(writeTarget(field)->set_value(v), reason);
    }

    return Device::applyCommandField(field, value, reason);
}

Json ClimateBase::deviceCapabilities()
{
    Json caps = Json::object();

    const vector<ClimateMode> modes = supportedModes();
    if (!modes.empty())
    {
        Json names = Json::array();
        for (ClimateMode m: modes)
            names.push_back(modeName(m));
        caps["modes"] = names;
    }

    const bool range = rangeAvailable();
    const bool single = linkIo("target_temp") && isWritable("target_temp");
    if (single || range)
    {
        //Without a single setpoint, the range IOs bound the dial.
        const Bounds b = single ? setpointBounds("target_temp") : rangeBounds();
        Json &t = caps["target_temp"];
        t["min"] = b.min;
        t["max"] = b.max;
        t["step"] = b.step;
        t["unit"] = b.unit;
        //Here and not next to target_temp_range: merged clients read that one as a boolean.
        if (range)
            t["min_range"] = minRange();
    }
    if (range)
        caps["target_temp_range"] = true;
    if (linkIo("action"))
        caps["reports_action"] = true;
    if (linkIo("fan_mode") && isWritable("fan_mode"))
        caps["fan_modes"] = paramList("fan_modes");
    if (linkIo("swing_mode") && isWritable("swing_mode"))
        caps["swing_modes"] = paramList("swing_modes");
    if (linkIo("remote_temp") && linkIo("remote_temp_source") && isWritable("remote_temp"))
        caps["remote_temp"] = true;

    if (heatLevels + coolLevels > 0)
        caps["program"] = Json{ { "heat_levels", heatLevels },
                                { "cool_levels", coolLevels },
                                { "max_levels", MAX_PROGRAM_LEVELS } };

    return caps;
}

Json ClimateBase::deviceState()
{
    Json state = Json::object();

    ClimateMode mode;
    if (currentMode(mode))
        state["mode"] = modeName(mode);

    for (const char *role: { "current_temp", "target_temp", LOW, HIGH })
    {
        IOBase *io = linkIo(role);
        //A sensor not read yet would publish 0 °C.
        if (io && io->hasValue())
            state[role] = io->get_value_double();
    }

    if (IOBase *io = linkIo("action"))
    {
        ClimateAction action;
        if (parseAction(io->get_value_string(), action))
            state["action"] = actionName(action);
    }

    for (const char *role: { "fan_mode", "swing_mode" })
    {
        IOBase *io = linkIo(role);
        if (!io)
            continue;
        const vector<string> allowed = paramList(string(role) + "s");
        const string v = io->get_value_string();
        if (std::find(allowed.begin(), allowed.end(), v) != allowed.end())
            state[role] = v;
    }

    //Like current_temp: a value not read yet would publish 0 %.
    IOBase *humidity = linkIo("humidity");
    if (humidity && humidity->hasValue())
        state["humidity"] = humidity->get_value_double();

    if (heatLevels + coolLevels > 0)
        state["program"] = programState();

    return Json(std::move(state));
}

double ClimateBase::nonNegativeParam(const char *name, double def)
{
    if (!hasDeviceParam(name))
        return def;

    double v = 0;
    if (Utils::from_string(getDeviceParam(name), v) && std::isfinite(v) && v >= 0)
        return v;

    cErrorDom("device") << "Device(" << get_param("id") << "): " << name << " '"
                        << getDeviceParam(name) << "' is not a non-negative number, " << def
                        << " is used";
    return def;
}

uint64_t ClimateBase::toMs(double seconds)
{
    return static_cast<uint64_t>(std::llround(std::min(seconds, 1e9) * 1000));
}

void ClimateBase::setLoopClock(LoopClock clock)
{
    injectedLoopClock = clock;
}

uint64_t ClimateBase::loopNow()
{
    if (injectedLoopClock)
        return injectedLoopClock();

    auto loop = uvw::Loop::getDefault();
    loop->update();
    return loop->now().count();
}

uint64_t ClimateBase::lockoutWait(bool on, bool forced, const std::optional<uint64_t> &onAt,
                                  const std::optional<uint64_t> &offAt,
                                  const std::optional<uint64_t> &otherOffAt, const Lockouts &l,
                                  uint64_t now)
{
    uint64_t until = 0;
    auto after = [&until](const std::optional<uint64_t> &since, uint64_t delay)
    {
        if (since)
            until = std::max(until, *since + delay);
    };

    if (on)
    {
        after(offAt, l.minOffMs);
        after(otherOffAt, l.changeoverMs);
    }
    else if (!forced)
        after(onAt, l.minOnMs);

    return until > now ? until - now : 0;
}

void ClimateBase::documentProgram()
{
    static_assert(MAX_PROGRAM_LEVELS == 10, "the levels descriptions below say 10");
    ioDoc->paramAdd("heat_levels",
                    _("Heating program, 0 for no program. Any value of 1 or more gives the maximum of 10 schedule "
                      "levels (an invalid number means 0). A level is a time range (InPlageHoraire io) with its "
                      "own setpoint io. The program IOs (heat_default, and a range and a setpoint per level) are "
                      "created hidden at load when missing, and removed when this param no longer asks for them."),
                    IODoc::TYPE_INT, false, "0", false, _("Heating program"));
    ioDoc->paramAdd("cool_levels",
                    _("Cooling program, 0 for no program. Any value of 1 or more gives the maximum of 10 schedule "
                      "levels (an invalid number means 0). A level is a time range (InPlageHoraire io) with its "
                      "own setpoint io. The program IOs (cool_default, cool_default_off, and a range and a setpoint "
                      "per level) are created hidden at load when missing, and removed when this param no longer "
                      "asks for them."),
                    IODoc::TYPE_INT, false, "0", false, _("Cooling program"));
    ioDoc->paramAdd("heat_default_setpoint", _("Heating setpoint outside the levels of the program."),
                    IODoc::TYPE_FLOAT, false, "17", false, _("Default heating setpoint"));
    ioDoc->paramAdd("cool_default_setpoint", _("Cooling setpoint outside the levels of the program."),
                    IODoc::TYPE_FLOAT, false, "26", false, _("Default cooling setpoint"));
    ioDoc->paramAdd("cool_default_off",
                    _("Outside the levels of the program, cooling stops instead of keeping a setpoint."),
                    IODoc::TYPE_BOOL, false, "false", false, _("Cooling off by default"));
    ioDoc->paramAdd("program_min_gap",
                    _("Smallest gap between the low and the high setpoint the program writes in auto."),
                    IODoc::TYPE_FLOAT, false, "2", false, _("Auto minimum gap"));
    for (const char *name: { "heat_levels", "cool_levels", "heat_default_setpoint", "cool_default_setpoint",
                             "cool_default_off", "program_min_gap" })
        ioDoc->paramInChild(name);
}

int ClimateBase::levelsParam(const string &set, ClimateMode mode)
{
    const string name = set + "_levels";
    if (!hasDeviceParam(name))
        return 0;

    const string id = get_param("id");
    const string raw = getDeviceParam(name);
    double v = 0;
    if (!nonNegative(raw, v) || v != std::floor(v))
    {
        cWarningDom("device") << "Device(" << id << "): " << name << " '" << raw
                              << "' is not a whole number of levels, 0 is used";
        return 0;
    }
    //The level count is not a setting: any program gets the maximum.
    if (v >= 1)
        v = MAX_PROGRAM_LEVELS;
    const vector<ClimateMode> modes = supportedModes();
    if (v > 0 && std::find(modes.begin(), modes.end(), mode) == modes.end())
    {
        cWarningDom("device") << "Device(" << id << "): " << name << " ignored, the mode '"
                              << modeName(mode) << "' is not offered";
        return 0;
    }
    return static_cast<int>(v);
}

int ClimateBase::programLevels(const string &set) const
{
    if (set == "heat")
        return heatLevels;
    return set == "cool" ? coolLevels : 0;
}

void ClimateBase::setupProgram()
{
    programSubscriptions.clear();
    programStarted = false;
    heatLevels = levelsParam("heat", ClimateMode::Heat);
    coolLevels = levelsParam("cool", ClimateMode::Cool);
    programMinGap = nonNegativeParam("program_min_gap", 2);

    removeProgramIos("heat", heatLevels);
    removeProgramIos("cool", coolLevels);
    if (heatLevels + coolLevels == 0)
    {
        removeOwnedLink(PROGRAM_ENABLED);
        return;
    }

    //The bounds of the setpoint the program writes, copied once.
    const Bounds b = linkIo("target_temp") ? setpointBounds("target_temp") : rangeBounds();
    Params setpoint;
    setpoint.Add("type", "InternalInt");
    setpoint.Add("save", "true");
    setpoint.Add("value_min", Utils::to_string(b.min));
    setpoint.Add("value_max", Utils::to_string(b.max));
    setpoint.Add("step", Utils::to_string(b.step));
    setpoint.Add("unit", b.unit);
    createProgramIos("heat", heatLevels, setpoint);
    createProgramIos("cool", coolLevels, setpoint);
    if (!linkIo(PROGRAM_ENABLED))
    {
        const Params enabled{ { "type", "InternalBool" },
                              { "save", "true" },
                              { "value", "true" },
                              { "name", PROGRAM_ENABLED },
                              { "visible", "false" } };
        //A cached value left by a removed IO of the same id would win over "value".
        if (IOBase *io = createOwnedLink(PROGRAM_ENABLED, enabled))
            io->set_value(true);
    }

    //Any program IO moves the published program: active level, setpoints, next change.
    auto subscribe = [this](const string &role, std::function<void()> changed)
    {
        if (IOBase *io = linkIo(role))
            programSubscriptions.push_back(IOChangeHub::Instance().subscribe(
                io->get_param("id"), [this, changed](IOBase &)
                {
                changed();
                deviceStateChanged();
                }));
    };
    for (const string set: { "heat", "cool" })
    {
        auto changedWhenDefault = [this, set]()
        {
            if (programActiveLevel(set) == 0)
                programChanged(set);
        };
        for (int n = 1; n <= programLevels(set); n++)
        {
            subscribe(levelRole(set, n), [this, set]()
                      {
                programChanged(set);
            });
            subscribe(levelSetpointRole(set, n), [this, set, n]()
                      {
                if (programActiveLevel(set) == n)
                    programChanged(set);
            });
        }
        if (programLevels(set) > 0)
            subscribe(defaultRole(set), changedWhenDefault);
        if (set == "cool" && coolLevels > 0)
            subscribe(COOL_DEFAULT_OFF, changedWhenDefault);
    }
    //Turned off, nothing is written: applyProgram() checks it.
    subscribe(PROGRAM_ENABLED, [this]()
              {
        applyProgram();
    });
}

void ClimateBase::createProgramIos(const string &set, int levels, const Params &setpoint)
{
    if (levels == 0)
        return;

    auto create = [this](const string &role, Params params) -> IOBase *
    {
        if (linkIo(role))
            return nullptr;
        params.Add("name", role);
        params.Add("visible", "false");
        IOBase *io = createOwnedLink(role, params);
        if (io)
            cInfoDom("device") << "Device(" << get_param("id") << "): program IO '"
                               << io->get_param("id") << "' created for '" << role << "'";
        return io;
    };

    /* The initial values are forced after creation: with save=true, a cached
     * value left by a removed IO of the same id would win over "value". */
    const double initial = set == "heat" ? nonNegativeParam("heat_default_setpoint", 17)
                                         : nonNegativeParam("cool_default_setpoint", 26);
    Params def = setpoint;
    def.Add("value", Utils::to_string(initial));
    if (IOBase *io = create(defaultRole(set), def))
        io->set_value(initial);
    if (set == "cool")
    {
        const bool off = getDeviceParam(COOL_DEFAULT_OFF) == "true";
        const Params p{ { "type", "InternalBool" },
                        { "save", "true" },
                        { "value", off ? "true" : "false" } };
        if (IOBase *io = create(COOL_DEFAULT_OFF, p))
            io->set_value(off);
    }

    IOBase *defIo = linkIo(defaultRole(set));
    for (int n = 1; n <= levels; n++)
    {
        create(levelRole(set, n), Params{ { "type", "InPlageHoraire" } });
        Params p = setpoint;
        const double v = defIo ? defIo->get_value_double() : initial;
        p.Add("value", Utils::to_string(v));
        if (IOBase *io = create(levelSetpointRole(set, n), p))
            io->set_value(v);
    }
}

void ClimateBase::removeProgramIos(const string &set, int levels)
{
    vector<string> roles;
    for (int n = MAX_PROGRAM_LEVELS; n > levels; n--)
    {
        roles.push_back(levelSetpointRole(set, n));
        roles.push_back(levelRole(set, n));
    }
    if (levels == 0)
    {
        roles.push_back(defaultRole(set));
        if (set == "cool")
            roles.push_back(COOL_DEFAULT_OFF);
    }
    for (const string &role: roles)
    {
        if (removeOwnedLink(role))
            cInfoDom("device") << "Device(" << get_param("id") << "): program IO for '" << role
                               << "' removed, the levels param no longer asks for it";
    }
}

void ClimateBase::startProgram()
{
    //A range is evaluated by the rules loop only: until then it reads false.
    for (const string set: { "heat", "cool" })
    {
        for (int n = 1; n <= programLevels(set); n++)
        {
            if (IOBase *range = linkIo(levelRole(set, n)))
                range->hasChanged();
        }
    }
    programStarted = true;
    applyProgram();
}

int ClimateBase::programActiveLevel(const string &set)
{
    for (int n = 1; n <= programLevels(set); n++)
    {
        IOBase *range = linkIo(levelRole(set, n));
        if (range && range->get_value_bool())
            return n;
    }
    return 0;
}

bool ClimateBase::programSetpoint(const string &set, double &value, bool &off)
{
    if (programLevels(set) == 0)
        return false;

    const int level = programActiveLevel(set);
    off = false;
    if (level == 0 && set == "cool")
    {
        IOBase *io = linkIo(COOL_DEFAULT_OFF);
        if (io && io->get_value_bool())
        {
            off = true;
            return true;
        }
    }
    IOBase *io = linkIo(level > 0 ? levelSetpointRole(set, level) : defaultRole(set));
    if (!io)
        return false;
    value = io->get_value_double();
    return true;
}

bool ClimateBase::programRunning()
{
    if (!programStarted || heatLevels + coolLevels == 0)
        return false;
    IOBase *enabled = linkIo(PROGRAM_ENABLED);
    return enabled && enabled->get_value_bool();
}

void ClimateBase::programChanged(const string &set)
{
    ClimateMode mode;
    if (!currentMode(mode))
        return;
    //Another set rewriting the setpoint would undo a manual one for nothing.
    if (mode == ClimateMode::Auto || (mode == ClimateMode::Heat && set == "heat") ||
        (mode == ClimateMode::Cool && set == "cool"))
        applyProgram();
}

void ClimateBase::applyProgram()
{
    ClimateMode mode;
    if (!programRunning() || !currentMode(mode))
        return;
    if (mode == ClimateMode::Auto)
    {
        applyProgramRange();
        return;
    }
    if (mode != ClimateMode::Heat && mode != ClimateMode::Cool)
        return;

    double v = 0;
    bool off = false;
    if (!programSetpoint(mode == ClimateMode::Heat ? "heat" : "cool", v, off) || !linkIo("target_temp"))
        return;
    //Cooling then waits for a room above the highest setpoint, the mode stays cool.
    if (off)
        v = setpointBounds("target_temp").max;
    applyCommand(Json{ { "target_temp", v } });
}

/* The pair the program writes in auto. The set that has levels commands its
 * bound; with both, heat does. When the gap is not kept, the other bound
 * moves, and only if it cannot, the commanding one. One pair command, so that
 * applyRange() sees both. */
bool ClimateBase::programRangePair(double &low, double &high, bool quiet)
{
    double heatSp = 0, coolSp = 0;
    bool heatOff = false, coolOff = false;
    const bool heat = programSetpoint("heat", heatSp, heatOff);
    const bool cool = programSetpoint("cool", coolSp, coolOff);
    if ((!heat && !cool) || !rangeAvailable())
        return false;

    const string id = get_param("id");
    if (!rangeBound(LOW, low) || !rangeBound(HIGH, high))
    {
        if (!quiet)
            cWarningDom("device") << "Device(" << id << "): program range not written, a bound is "
                                  << "not known yet";
        return false;
    }

    const Bounds lb = setpointBounds(LOW);
    const Bounds hb = setpointBounds(HIGH);
    string reason;
    if (heat)
        numberField(Json(heatSp), lb.min, lb.max, lb.step, low, reason);
    if (cool && coolOff)
        high = hb.max;
    else if (cool)
        numberField(Json(coolSp), hb.min, hb.max, hb.step, high, reason);

    double gap = std::max(minRange(), programMinGap);
    if (lb.step > 0)
        gap = std::ceil(gap / lb.step - 1e-9) * lb.step;
    //Tolerance for the binary residue of decimal steps, as applyRange().
    if (high - low < gap - 1e-9)
    {
        if (heat)
        {
            high = std::min(low + gap, hb.max);
            low = high - gap;
        }
        else
        {
            low = std::max(high - gap, lb.min);
            high = low + gap;
        }
    }
    if (low < lb.min - 1e-9 || high > hb.max + 1e-9)
    {
        if (!quiet)
            cWarningDom("device") << "Device(" << id << "): program range not written, a gap of "
                                  << gap << " does not fit in " << lb.min << ".." << hb.max;
        return false;
    }
    return true;
}

void ClimateBase::applyProgramRange()
{
    double low = 0, high = 0;
    if (!programRangePair(low, high, false))
        return;
    applyCommand(Json{ { LOW, low }, { HIGH, high } });
}

Json ClimateBase::programState()
{
    ClimateMode mode;
    const bool known = currentMode(mode);
    string modeSet;
    if (known && mode == ClimateMode::Auto)
        modeSet = "auto";
    else if (known && mode == ClimateMode::Heat && heatLevels > 0)
        modeSet = "heat";
    else if (known && mode == ClimateMode::Cool && coolLevels > 0)
        modeSet = "cool";

    IOBase *enabledIo = linkIo(PROGRAM_ENABLED);
    const bool enabled = enabledIo && enabledIo->get_value_bool();

    //In auto the written pair, after the gap; when it is not written, the level's own setpoint.
    double low = 0, high = 0;
    const bool pair = modeSet == "auto" && programRangePair(low, high, true);

    Json program = Json::object();
    program["enabled"] = enabled;
    program["mode_set"] = modeSet.empty() ? Json(nullptr) : Json(modeSet);

    for (const string set: { "heat", "cool" })
    {
        if (programLevels(set) == 0)
            continue;

        double v = 0;
        bool off = false;
        const bool hasSetpoint = programSetpoint(set, v, off);
        if (pair && !off)
        {
            v = set == "heat" ? low : high;
        }
        const int level = programActiveLevel(set);

        double def = 0;
        bool defKnown = false;
        if (IOBase *io = linkIo(defaultRole(set)))
        {
            def = io->get_value_double();
            defKnown = true;
        }
        IOBase *defOff = set == "cool" ? linkIo(COOL_DEFAULT_OFF) : nullptr;
        if (defOff && defOff->get_value_bool())
            defKnown = false;

        Json j = Json::object();
        j["levels"] = programLevels(set);
        j["active_level"] = level > 0 ? Json(level) : Json(nullptr);
        j["setpoint"] = hasSetpoint && !off ? Json(v) : Json(nullptr);
        j["default"] = defKnown ? Json(def) : Json(nullptr);
        program[set] = j;
    }

    program["next_change"] = enabled ? programNextChange(modeSet, time(nullptr)) : Json(nullptr);
    return program;
}

int ClimateBase::programLevelAt(const string &set, time_t t)
{
    for (int n = 1; n <= programLevels(set); n++)
    {
        auto *range = dynamic_cast<InPlageHoraire *>(linkIo(levelRole(set, n)));
        if (range && range->isActiveAt(t))
            return n;
    }
    return 0;
}

Json ClimateBase::programNextChange(const string &modeSet, time_t now)
{
    vector<string> sets;
    if (modeSet == "auto")
        sets = { "heat", "cool" };
    else if (!modeSet.empty())
        sets = { modeSet };

    const time_t horizon = now + 7 * 24 * 3600;
    time_t bestAt = 0;
    string bestSet;
    int bestLevel = 0;

    for (const string &set: sets)
    {
        if (programLevels(set) == 0)
            continue;

        vector<time_t> instants;
        for (int n = 1; n <= programLevels(set); n++)
        {
            if (auto *range = dynamic_cast<InPlageHoraire *>(linkIo(levelRole(set, n))))
                range->changeInstants(now, horizon, instants);
        }
        std::sort(instants.begin(), instants.end());
        instants.erase(std::unique(instants.begin(), instants.end()), instants.end());

        const int before = programLevelAt(set, now);
        for (time_t t: instants)
        {
            const int level = programLevelAt(set, t);
            if (level == before)
                continue;
            //The range ends one second before the instant it flips (end bound included);
            //the contract reports the end as written. A start is reported as is, and so
            //is a cut by a month change: that one happens at 00:00:00, not the second before.
            bool atInstant = false;
            if (level > 0)
            {
                auto *r = dynamic_cast<InPlageHoraire *>(linkIo(levelRole(set, level)));
                atInstant = r && !r->isActiveAt(t - 1);
            }
            const int prev = programLevelAt(set, t - 1);
            if (!atInstant && prev > 0)
            {
                auto *r = dynamic_cast<InPlageHoraire *>(linkIo(levelRole(set, prev)));
                atInstant = r && r->isActiveAt(t, true) && !r->isActiveAt(t);
            }
            const time_t reported = atInstant ? t : t - 1;
            //Heat wins a tie: it is first in `sets`.
            if (bestAt == 0 || reported < bestAt)
            {
                bestAt = reported;
                bestSet = set;
                bestLevel = level;
            }
            break;
        }
    }
    if (bestAt == 0)
        return Json(nullptr);

    Json setpoint = nullptr;
    IOBase *io = nullptr;
    bool off = false;
    if (bestLevel == 0 && bestSet == "cool")
    {
        IOBase *offIo = linkIo(COOL_DEFAULT_OFF);
        off = offIo && offIo->get_value_bool();
    }
    if (!off)
        io = linkIo(bestLevel > 0 ? levelSetpointRole(bestSet, bestLevel) : defaultRole(bestSet));
    if (io)
        setpoint = io->get_value_double();

    struct tm tm = *localtime(&bestAt);
    char buf[40];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tm);
    //%z gives +0100, ISO 8601 wants +01:00.
    string at = buf;
    at.insert(at.size() - 2, ":");

    return Json{ { "at", at },
                 { "set", bestSet },
                 { "level", bestLevel > 0 ? Json(bestLevel) : Json(nullptr) },
                 { "setpoint", setpoint } };
}
