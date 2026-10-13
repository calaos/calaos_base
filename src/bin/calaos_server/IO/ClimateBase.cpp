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

#include <algorithm>
#include <cmath>

using namespace Calaos;

namespace
{

const char *const LOW = "target_temp_low";
const char *const HIGH = "target_temp_high";
const char *const REFUSED_BY_IO = "the linked IO refused the value";

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

} //namespace

ClimateBase::ClimateBase(Params &p):
    Device(p)
{
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
    for (const string &m: paramList("modes"))
    {
        ClimateMode mode;
        if (!parseMode(m, mode))
            cErrorDom("device") << "Device(" << id << "): unknown mode '" << m
                                << "' in the modes param, ignored";
    }
    for (const char *role: { "fan_mode", "swing_mode" })
    {
        const string list = string(role) + "s";
        if (linkIo(role) && paramList(list).empty())
            cErrorDom("device") << "Device(" << id << "): link '" << role << "' without a "
                                << list << " param, every value will be refused";
    }
    if (hasDeviceParam("modes") && paramList("modes").empty())
        cErrorDom("device") << "Device(" << id << "): empty modes param, no mode can be selected";
    double gap;
    if (hasDeviceParam("min_range") && !nonNegative(getDeviceParam("min_range"), gap))
        cErrorDom("device") << "Device(" << id << "): min_range '" << getDeviceParam("min_range")
                            << "' is not a non-negative number, 0 is used";
    if (linkIo(LOW) && linkIo(HIGH))
    {
        const Bounds b = rangeBounds();
        if (minRange() > b.max - b.min)
            cErrorDom("device") << "Device(" << id << "): min_range " << minRange()
                                << " is wider than the setpoint bounds " << b.min << ".." << b.max
                                << ", every range will be refused";
    }
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
    return writeTarget(role)->set_value(value);
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
            return written(writeMode(mode), reason);
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

    return Json(std::move(state));
}
