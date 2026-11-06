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
#include "ProxyClimate.h"
#include "CalaosConfig.h"

#include <algorithm>
#include <cmath>

using namespace Calaos;

REGISTER_DEVICE(proxy, ProxyClimate)

namespace
{

const char *const HEAT_COOL = "heat_cool";
const char *const AUTO = "auto";
const char *const MODE = "mode";
const char *const REMOTE = "remote_temp";
const char *const SOURCE = "remote_temp_source";
const char *const CURRENT = "current_temp";
const char *const TARGET = "target_temp";
const char *const LOW = "target_temp_low";
const char *const HIGH = "target_temp_high";
//Values of the range IOs created when emulate_range finds none.
const double DEFAULT_LOW = 19;
const double DEFAULT_HIGH = 25;
//Distinct unknown values logged: a broker can send an unbounded number.
const size_t MAX_LOGGED_UNKNOWN = 16;

} //namespace

ProxyClimate::ProxyClimate(Params &p):
    ClimateBase(p)
{
    ioDoc->friendlyNameSet(_("Climate proxy"));
    ioDoc->descriptionSet(_("A climate appliance that regulates by itself (heat pump, ESPHome climate): Calaos "
                            "reads its state and relays commands, it does not regulate."));
    ioDoc->descriptionBaseSet(_("Each link role reads the io of its link and writes its command_io, or the io "
                                "itself when that is an output. A role that cannot be written is not offered."));

    ioDoc->paramAddList("auto_payload", _("What is written to the mode link for auto. Without it, the spelling "
                                          "the appliance last reported, else heat_cool."),
                        false, Params({ { "auto", "auto" }, { "heat_cool", "heat_cool" } }), string(), false,
                        _("Auto payload"));
    ioDoc->paramAdd("remote_temp_keepalive", _("Seconds between two relays of the remote_temp_source value to "
                                               "remote_temp, on top of each change. The appliance falls back to "
                                               "its own sensor when updates stop. A number of seconds above 0, "
                                               "else the default is used."),
                    IODoc::TYPE_FLOAT, false, Utils::to_string(DEFAULT_KEEPALIVE), false,
                    _("Remote temperature refresh"));
    ioDoc->paramAdd("readback_hold", _("Seconds a range bound just commanded is trusted before its readback "
                                       "confirms it, so two quick partial commands are validated together. "
                                       "A number of seconds above 0, else the default is used."),
                    IODoc::TYPE_FLOAT, false, Utils::to_string(DEFAULT_READBACK_HOLD), false,
                    _("Readback hold"));
    ioDoc->paramAdd("emulate_range", _("For a unit with a single setpoint: the device keeps target_temp_low and "
                                       "target_temp_high itself (internal IOs, created when missing) and offers "
                                       "auto. In auto the unit runs in heat on the low setpoint below the range, "
                                       "in cool on the high setpoint above it, and is off in between. Needs a "
                                       "target_temp link and heat and cool among the modes. The room temperature "
                                       "is remote_temp_source, else current_temp; while it is stale the unit is "
                                       "left as it is. A mode changed on the unit itself suspends auto until it "
                                       "is selected again. auto is never sent to the unit."),
                    IODoc::TYPE_BOOL, false, "false", false, _("Emulated dual setpoint"));
    ioDoc->paramAdd("hysteresis", _("With emulate_range: degrees past the range before the unit starts, in heat "
                                    "below the low setpoint minus hysteresis, in cool above the high setpoint "
                                    "plus hysteresis. It stops as soon as the room is back in the range."),
                    IODoc::TYPE_FLOAT, false, Utils::to_string(DEFAULT_HYSTERESIS), false, _("Hysteresis"));
    ioDoc->paramAdd("min_on_time", _("With emulate_range: seconds the unit runs at least in heat or cool before "
                                     "auto stops it. A mode selected by the user is applied at once."),
                    IODoc::TYPE_FLOAT, false, Utils::to_string(DEFAULT_MIN_ON), false, _("Minimum on time"));
    ioDoc->paramAdd("min_off_time", _("With emulate_range: seconds the unit stays off at least before auto "
                                      "starts it again."),
                    IODoc::TYPE_FLOAT, false, Utils::to_string(DEFAULT_MIN_OFF), false, _("Minimum off time"));
    ioDoc->paramAdd("changeover_delay", _("With emulate_range: seconds between the end of heat and the start of "
                                          "cool, or the reverse, at least min_off_time."),
                    IODoc::TYPE_FLOAT, false, Utils::to_string(DEFAULT_CHANGEOVER), false, _("Changeover delay"));
    for (const char *name: { "auto_payload", "remote_temp_keepalive", "readback_hold", "emulate_range",
                             "hysteresis", "min_on_time", "min_off_time", "changeover_delay" })
        ioDoc->paramInChild(name);

    ioDoc->linkRoleAdd("mode", _("Mode"),
                       _("Mode of the appliance. Needs a command_io unless its io is an output. Its "
                         "availability is the appliance's: connected false in the state."),
                       IODoc::LINK_STRING, true, true);
    ioDoc->linkRoleAdd("current_temp", _("Current temperature"), _("Temperature the appliance measures."),
                       IODoc::LINK_NUMBER, false, false);
    ioDoc->linkRoleAdd("target_temp", _("Setpoint"), _("Setpoint used in heat and cool."), IODoc::LINK_NUMBER, false,
                       true);
    ioDoc->linkRoleAdd("target_temp_low", _("Low setpoint"),
                       _("Low bound of the auto range. Both bounds are needed for a range."), IODoc::LINK_NUMBER,
                       false, true);
    ioDoc->linkRoleAdd("target_temp_high", _("High setpoint"),
                       _("High bound of the auto range. Both bounds are needed for a range."), IODoc::LINK_NUMBER,
                       false, true);
    ioDoc->linkRoleAdd("action", _("Action"),
                       _("What the appliance is doing: off, idle, heating, cooling, drying, fan."),
                       IODoc::LINK_STRING, false, false);
    ioDoc->linkRoleAdd("fan_mode", _("Fan mode"), _("Fan mode. Needs the fan_modes param."), IODoc::LINK_STRING,
                       false, true);
    ioDoc->linkRoleAdd("swing_mode", _("Swing mode"), _("Swing mode. Needs the swing_modes param."),
                       IODoc::LINK_STRING, false, true);
    ioDoc->linkRoleAdd("remote_temp", _("Remote temperature"),
                       _("Where the temperature of another sensor is written, for appliances that "
                         "take one. Needs remote_temp_source."),
                       IODoc::LINK_NUMBER, false, true);
    ioDoc->linkRoleAdd("humidity", _("Humidity sensor"),
                       _("Relative humidity the appliance, or a room sensor, reports: shown in the state."),
                       IODoc::LINK_NUMBER, false, false);
    ioDoc->linkRoleAdd("remote_temp_source", _("Room sensor"),
                       _("The sensor relayed to remote_temp. Needs remote_temp. Not relayed "
                         "before it has read since the start, nor while it has sent "
                         "nothing for sensor_timeout or reports itself disconnected: the "
                         "appliance then falls back to its own sensor."),
                       IODoc::LINK_NUMBER, false, false);

    documentModesParam();
    documentCommand(allModes(), "mode");
    documentFanSwing();
}

ProxyClimate::~ProxyClimate()
{
    stopListening();
}

double ProxyClimate::secondsParam(const string &name, double fallback) const
{
    double v = 0;
    if (Utils::from_string(getDeviceParam(name), v) && std::isfinite(v) && v > 0)
        return v;
    return fallback;
}

string ProxyClimate::linkIoId(const string &role) const
{
    string id;
    for (const Link &l: getLinks())
    {
        if (l.name == role)
        {
            id = l.io;
            break;
        }
    }
    //Not a named return value: gcov would charge its unwinding to the brace.
    return string(id);
}

//An input takes a write and sends it nowhere (InputAnalog just stores it).
bool ProxyClimate::isWritable(const string &role)
{
    IOBase *io = writeTarget(role);
    return io && io->isOutput();
}

void ProxyClimate::resolveLinks()
{
    //Before the base records the scalar: a restored auto is not a change.
    startEmulation();
    ClimateBase::resolveLinks();

    const string id = get_param("id");
    //Loads all the same (spec §3.2): required is a hint for the installer.
    if (linkIoId(MODE).empty())
        cWarningDom("device") << "Device(" << id << "): no 'mode' link, no mode is offered";
    for (const char *role: { "mode", "target_temp", "target_temp_low", "target_temp_high",
                             "fan_mode", "swing_mode", "remote_temp" })
    {
        if (linkIo(role) && !isWritable(role))
            cErrorDom("device") << "Device(" << id << "): link '" << role << "' "
                                << (linkCommandIo(role) ? "has a command_io that is not an output"
                                                        : "has no command_io and its io is not an output")
                                << ", it cannot be written";
    }
    if (linkIo(REMOTE) && !linkIo(SOURCE))
        cErrorDom("device") << "Device(" << id << "): link 'remote_temp' without a "
                            << "'remote_temp_source', nothing is relayed";
    if (linkIo(SOURCE) && !linkIo(REMOTE))
        cErrorDom("device") << "Device(" << id << "): link 'remote_temp_source' without a "
                            << "'remote_temp', nothing is relayed";
    if (linkIo(REMOTE))
        warnIfChangesOnly(SOURCE);

    if (hasDeviceParam("auto_payload") && getDeviceParam("auto_payload") != AUTO &&
        getDeviceParam("auto_payload") != HEAT_COOL)
        cErrorDom("device") << "Device(" << id << "): auto_payload must be 'auto' or 'heat_cool', "
                            << "ignored";
    for (const char *name: { "remote_temp_keepalive", "readback_hold" })
    {
        if (hasDeviceParam(name) && secondsParam(name, 0) == 0)
            cErrorDom("device") << "Device(" << id << "): " << name
                                << " is not a positive number of seconds, the default is used";
    }

    readbackHold = secondsParam("readback_hold", DEFAULT_READBACK_HOLD);
    readbackSubscriptions.clear();
    for (const char *name: { "target_temp_low", "target_temp_high" })
    {
        const string role = name;
        if (!linkIo(role))
            continue;
        readbackSubscriptions.push_back(IOChangeHub::Instance().subscribe(
            linkIoId(role), [this, role](IOBase &)
            {
            commanded.erase(role);
            }));
    }

    startRemoteTemp();

    if (!emu.enabled)
        return;
    if (!linkIo(referenceRole()))
        cErrorDom("device") << "Device(" << id << "): emulate_range without remote_temp_source or "
                            << "current_temp, auto never switches the unit";
    else if (referenceRole() == string(CURRENT))
        warnIfChangesOnly(CURRENT);

    auto changed = [this](IOBase &)
    {
        emulate();
    };
    for (const char *role: { MODE, TARGET, LOW, HIGH, referenceRole() })
    {
        if (!linkIoId(role).empty())
            emu.triggers.push_back(IOChangeHub::Instance().subscribe(linkIoId(role), changed));
    }
    auto check = [this]()
    {
        requestReadingIfOld(referenceRole());
        emulate();
    };
    emu.freshnessCheck = std::make_unique<Timer>(std::min(sensorTimeout() / 4, FRESHNESS_POLL), check);
    //Once the loop runs: a restored auto has nothing else to wake it.
    ioAlive.singleShot(0, [this]()
                       {
        emulate();
    });
}

/* Reads the params even when the device cannot emulate, so that every
 * mistake is logged at load. */
void ProxyClimate::startEmulation()
{
    if (!hasDeviceParam("emulate_range"))
        return;
    const string id = get_param("id");
    const string flag = getDeviceParam("emulate_range");
    if (flag != "true")
    {
        if (flag != "false")
            cErrorDom("device") << "Device(" << id << "): emulate_range must be 'true' or 'false', "
                                << "no emulated auto";
        return;
    }

    emu.hysteresis = nonNegativeParam("hysteresis", DEFAULT_HYSTERESIS);
    emu.lockouts.minOnMs = toMs(nonNegativeParam("min_on_time", DEFAULT_MIN_ON));
    emu.lockouts.minOffMs = toMs(nonNegativeParam("min_off_time", DEFAULT_MIN_OFF));
    //The unit is off since the other mode stopped: min_off_time holds too.
    emu.lockouts.changeoverMs =
        std::max(toMs(nonNegativeParam("changeover_delay", DEFAULT_CHANGEOVER)), emu.lockouts.minOffMs);

    const vector<ClimateMode> modes = ClimateBase::supportedModes();
    auto offers = [&modes](ClimateMode m)
    {
        return std::find(modes.begin(), modes.end(), m) != modes.end();
    };
    if (!linkIo(TARGET) || !isWritable(TARGET))
    {
        cErrorDom("device") << "Device(" << id << "): emulate_range needs a target_temp link that can "
                            << "be written, no emulated auto";
        return;
    }
    if (!offers(ClimateMode::Heat) || !offers(ClimateMode::Cool))
    {
        cErrorDom("device") << "Device(" << id << "): emulate_range needs heat and cool among the "
                            << "modes, no emulated auto";
        return;
    }

    recreateRange();
    if (!linkIo(LOW) || !linkIo(HIGH) || !isWritable(LOW) || !isWritable(HIGH))
    {
        cErrorDom("device") << "Device(" << id << "): emulate_range needs target_temp_low and "
                            << "target_temp_high that can be written, no emulated auto";
        return;
    }

    emu.enabled = true;
    restoreEmulation();
    /* The unit may have run just before a restart: a stop waits for
     * min_on_time, a start for min_off_time, and the changeover delay only
     * applies towards the opposite of the last mode it ran in. */
    const uint64_t now = loopNow();
    emu.heatOnAt = emu.coolOnAt = emu.startedAt = now;
    if (emu.lastActive == ClimateMode::Heat)
        emu.heatOffAt = now;
    if (emu.lastActive == ClimateMode::Cool)
        emu.coolOffAt = now;
}

void ProxyClimate::recreateRange()
{
    const Bounds b = setpointBounds(TARGET);
    auto recreate = [this, &b](const char *role, double value)
    {
        if (linkIo(role))
            return;
        Params p{ { "type", "InternalInt" },
                  { "name", role },
                  { "visible", "false" },
                  { "save", "true" },
                  { "value", Utils::to_string(std::clamp(value, b.min, b.max)) },
                  { "value_min", Utils::to_string(b.min) },
                  { "value_max", Utils::to_string(b.max) },
                  { "step", Utils::to_string(b.step) },
                  { "unit", b.unit } };
        //The nominal way to get a range: the unit has none, the template creates none.
        if (IOBase *io = createOwnedLink(role, p))
            cInfoDom("device") << "Device(" << get_param("id") << "): " << role << " kept by Calaos in '"
                               << io->get_param("id") << "'";
    };
    recreate(LOW, DEFAULT_LOW);
    recreate(HIGH, std::max(DEFAULT_HIGH, DEFAULT_LOW + minRange()));
}

/* Saved as "<auto|manual> <mode last sent> [<last of heat and cool sent>]":
 * a unit that reports another mode was changed meanwhile. */
void ProxyClimate::restoreEmulation()
{
    string saved;
    if (!Config::Instance().ReadValueIO(get_param("id"), saved))
        return;
    vector<string> words;
    Utils::split(saved, words, " ");
    auto runMode = [](const string &w, ClimateMode &m)
    {
        return parseMode(w, m) && (m == ClimateMode::Off || m == ClimateMode::Heat || m == ClimateMode::Cool);
    };
    ClimateMode m;
    if (words.size() >= 2 && words[0] == AUTO && runMode(words[1], m))
    {
        emu.active = true;
        emu.commanded = m;
    }
    if (words.size() == 3 && runMode(words[2], m))
        emu.lastActive = m;
}

void ProxyClimate::saveEmulation()
{
    string saved = string(emu.active ? AUTO : "manual") + " " + modeName(emu.commanded);
    if (emu.lastActive != ClimateMode::Off)
        saved += " " + modeName(emu.lastActive);
    Config::Instance().SaveValueIO(get_param("id"), saved);
}

const char *ProxyClimate::referenceRole()
{
    return linkIo(SOURCE) ? SOURCE : CURRENT;
}

vector<ClimateMode> ProxyClimate::supportedModes()
{
    vector<ClimateMode> modes = ClimateBase::supportedModes();
    if (emu.enabled && std::find(modes.begin(), modes.end(), ClimateMode::Auto) == modes.end())
        modes.insert(std::upper_bound(modes.begin(), modes.end(), ClimateMode::Auto), ClimateMode::Auto);
    //Not a named return value: gcov charges its unwinding to the closing brace.
    return vector<ClimateMode>(std::move(modes));
}

bool ProxyClimate::currentMode(ClimateMode &mode)
{
    if (emu.active)
    {
        mode = ClimateMode::Auto;
        return true;
    }
    return unitMode(mode);
}

bool ProxyClimate::unitMode(ClimateMode &mode)
{
    IOBase *io = linkIo(MODE);
    if (!io)
        return false;

    const string raw = io->get_value_string();
    if (raw == HEAT_COOL || raw == AUTO)
    {
        autoReported = raw;
        mode = ClimateMode::Auto;
        return true;
    }
    if (parseMode(raw, mode))
        return true;

    //Nothing reported yet is not an unknown mode.
    if (!raw.empty())
        logUnknownMode(raw);
    return false;
}

void ProxyClimate::logUnknownMode(const string &raw)
{
    if (unknownModes.size() >= MAX_LOGGED_UNKNOWN || !unknownModes.insert(raw).second)
        return;
    cWarningDom("device") << "Device(" << get_param("id") << "): the appliance reports the mode "
                          << fieldForLog(raw) << ", which is not a climate mode";
}

bool ProxyClimate::writeMode(ClimateMode mode)
{
    if (mode == ClimateMode::Auto && emu.enabled)
    {
        return emu.active || startAuto();
    }
    if (emu.active)
    {
        emu.active = false;
        emu.lockout.reset();
        emu.startsAt.reset();
        saveEmulation();
    }
    if (mode != ClimateMode::Auto)
        return writeUnitMode(mode);

    //Only reached through supportedModes(), which needs a writable target.
    IOBase *io = writeTarget(MODE);

    string payload = getDeviceParam("auto_payload");
    if (payload != AUTO && payload != HEAT_COOL)
        payload = autoReported.empty() ? HEAT_COOL : autoReported;
    return io->set_value(payload);
}

/* Unknown, dry, fan_only or the unit's own auto: none is a state of the
 * emulation, the unit is stopped and auto starts from off. */
bool ProxyClimate::startAuto()
{
    emu.active = true;
    emu.setpoint.reset();
    ClimateMode unit;
    if (unitMode(unit) && (unit == ClimateMode::Off || unit == ClimateMode::Heat || unit == ClimateMode::Cool))
    {
        emu.commanded = unit;
        emu.commandedAt.reset();
        saveEmulation();
        return true;
    }
    if (writeUnitMode(ClimateMode::Off))
        return true;
    emu.active = false;
    return false;
}

bool ProxyClimate::writeUnitMode(ClimateMode mode)
{
    const uint64_t now = loopNow();
    ClimateMode before;
    if (!unitMode(before))
        before = emu.commanded;

    const ClimateMode oldCommanded = emu.commanded;
    const std::optional<uint64_t> oldAt = emu.commandedAt;
    const ClimateMode oldPrevious = emu.previous;
    //Set before the write: the readback it causes is checked against it.
    emu.previous = before;
    emu.commanded = mode;
    emu.commandedAt = now;
    if (!writeTarget(MODE)->set_value(modeName(mode)))
    {
        emu.commanded = oldCommanded;
        emu.commandedAt = oldAt;
        emu.previous = oldPrevious;
        cErrorDom("device") << "Device(" << get_param("id") << "): the mode link refused "
                            << modeName(mode);
        return false;
    }

    if (before == ClimateMode::Heat && mode != ClimateMode::Heat)
        emu.heatOffAt = now;
    if (before != ClimateMode::Heat && mode == ClimateMode::Heat)
        emu.heatOnAt = now;
    if (before == ClimateMode::Cool && mode != ClimateMode::Cool)
        emu.coolOffAt = now;
    if (before != ClimateMode::Cool && mode == ClimateMode::Cool)
        emu.coolOnAt = now;
    if (mode == ClimateMode::Heat || mode == ClimateMode::Cool)
        emu.lastActive = mode;
    if (emu.enabled)
        saveEmulation();
    return true;
}

bool ProxyClimate::applyCommandField(const string &field, const Json &value, string &reason)
{
    if (field == TARGET && emu.active)
    {
        reason = "auto drives target_temp from target_temp_low and target_temp_high";
        return false;
    }
    return ClimateBase::applyCommandField(field, value, reason);
}

/* {"mode":"auto","target_temp_low":18} must not start the unit against the
 * previous range and then hold it for min_on_time. */
bool ProxyClimate::applyCommand(const Json &command)
{
    const bool nested = emu.evaluating;
    emu.evaluating = true;
    const bool applied = ClimateBase::applyCommand(command);
    emu.evaluating = nested;
    if (emu.enabled)
        emulate();
    return applied;
}

//Never nested: a write whose rules call back starts the pass over once it returns.
void ProxyClimate::emulate()
{
    if (emu.evaluating)
    {
        emu.again = true;
        return;
    }
    emu.evaluating = true;
    do
    {
        emu.again = false;
        regulate();
    } while (emu.again);
    emu.evaluating = false;
    publishStatusChange();
}

void ProxyClimate::regulate()
{
    emu.fault.clear();
    if (!emu.active)
        return;

    /* A readback other than what was sent is a change made on the unit,
     * unless it is the mode before the command, which the unit may still
     * republish for readback_hold (ESPHome does after the setpoint write).
     * A unit that is not reachable says nothing of its mode. */
    const uint64_t now = loopNow();
    const bool reachable = freshness(linkIo(MODE)) != Freshness::Disconnected;
    ClimateMode unit;
    if (reachable && unitMode(unit) && unit != emu.commanded)
    {
        const bool late = emu.commandedAt && now - *emu.commandedAt < toMs(readbackHold) && unit == emu.previous;
        if (!late)
        {
            suspend(unit);
            return;
        }
    }

    double low = 0, high = 0;
    const bool ready = referenceFresh() && reachable && rangeBound(LOW, low) && rangeBound(HIGH, high) &&
                       std::isfinite(linkIo(referenceRole())->get_value_double());
    const ClimateMode cur = emu.commanded;
    if (!ready)
    {
        emu.lockout.reset();
        emu.startsAt.reset();
        return;
    }

    const double t = linkIo(referenceRole())->get_value_double();
    ClimateMode want = cur;
    if (t < low - emu.hysteresis)
        want = ClimateMode::Heat;
    else if (t > high + emu.hysteresis)
        want = ClimateMode::Cool;
    else if ((cur == ClimateMode::Heat && t >= low) || (cur == ClimateMode::Cool && t <= high))
        want = ClimateMode::Off;

    //A changeover goes through off: one lock-out at a time.
    const ClimateMode next = cur != ClimateMode::Off && want != cur ? ClimateMode::Off : want;
    const bool heating = (next == ClimateMode::Off ? cur : next) == ClimateMode::Heat;
    const bool on = next != ClimateMode::Off;
    const uint64_t wait =
        next == cur ? 0
                    : lockoutWait(on, false, heating ? emu.heatOnAt : emu.coolOnAt,
                                  (heating ? emu.heatOffAt : emu.coolOffAt).value_or(*emu.startedAt),
                                  heating ? emu.coolOffAt : emu.heatOffAt, emu.lockouts, now);
    if (next == cur || wait > 0)
    {
        if (cur != ClimateMode::Off)
            followSetpoint(cur == ClimateMode::Heat ? low : high);
        emu.startsAt.reset();
        if (wait == 0)
        {
            emu.lockout.reset();
            return;
        }
        auto expired = [this]()
        {
            ProxyClimate *self = this;
            //A Timer repeats: drop it, which its own slot may do.
            emu.lockout.reset();
            self->emulate();
        };
        if (!emu.lockout || emu.lockoutUntil != now + wait)
            emu.lockout = std::make_unique<Timer>(wait / 1000.0, expired);
        emu.lockoutUntil = now + wait;
        if (on)
        {
            emu.waiting = next;
            emu.startsAt = now + wait;
        }
        return;
    }

    emu.lockout.reset();
    emu.startsAt.reset();
    //The setpoint first: the unit must not start against the previous one.
    if (on)
        followSetpoint(next == ClimateMode::Heat ? low : high);
    writeUnitMode(next);
}

bool ProxyClimate::referenceFresh()
{
    const char *role = referenceRole();
    IOBase *ref = linkIo(role);
    const Freshness f = freshness(ref);
    const bool stale = f == Freshness::Silent || f == Freshness::Disconnected;
    if (stale && f != emu.reference)
        cErrorDom("device") << "Device(" << get_param("id") << "): " << role << " '" << ref->get_param("id")
                            << "' " << staleReason(f) << ", auto leaves the unit as it is";
    if (f == Freshness::Fresh && emu.reference != Freshness::Fresh && emu.reference != Freshness::NotRead)
        cInfoDom("device") << "Device(" << get_param("id") << "): " << role << " '" << ref->get_param("id")
                           << "' reads again, auto drives the unit again";
    emu.reference = f;

    if (!ref)
        emu.fault = "sensor_missing";
    else if (f == Freshness::NotRead)
        emu.fault = "sensor_waiting";
    else if (f == Freshness::Silent)
        emu.fault = "sensor_stale";
    else if (f == Freshness::Disconnected)
        emu.fault = "sensor_disconnected";
    return f == Freshness::Fresh;
}

/* A unit that reads another setpoint (changed at the remote) is written
 * again, once per readback_hold at most. One that still differs after that
 * cannot hold it (rounding, a minimum of its mode): it is left as it is
 * until Calaos decides another setpoint, or each readback would write it. */
void ProxyClimate::followSetpoint(double value)
{
    const Bounds b = setpointBounds(TARGET);
    const double v = std::clamp(value, b.min, b.max);
    IOBase *readback = linkIo(TARGET);
    const bool held = readback->hasValue() && std::abs(readback->get_value_double() - v) <= b.step / 2 + 1e-9;
    const bool decided = emu.setpoint == v;
    if (held)
    {
        emu.setpointRetry.reset();
        if (decided)
            emu.setpointRewritten = emu.setpointGivenUp = false;
        return;
    }

    const uint64_t now = loopNow();
    if (decided)
    {
        if (emu.setpointGivenUp)
            return;
        const uint64_t holdMs = toMs(readbackHold);
        if (emu.setpointAt && now - *emu.setpointAt < holdMs)
        {
            auto retry = [this]()
            {
                ProxyClimate *self = this;
                //A Timer repeats: drop it, which its own slot may do.
                emu.setpointRetry.reset();
                self->emulate();
            };
            if (!emu.setpointRetry)
                emu.setpointRetry = std::make_unique<Timer>((*emu.setpointAt + holdMs - now) / 1000.0, retry);
            return;
        }
        if (emu.setpointRewritten)
        {
            emu.setpointGivenUp = true;
            cWarningDom("device") << "Device(" << get_param("id") << "): target_temp reads "
                                  << readback->get_value_double() << ", not the " << v
                                  << " Calaos sent; left as it is until Calaos sets another one";
            return;
        }
        emu.setpointRewritten = true;
    }
    else
        emu.setpointRewritten = emu.setpointGivenUp = false;

    emu.setpointAt = now;
    emu.setpointRetry.reset();
    if (writeTarget(TARGET)->set_value(v))
        emu.setpoint = v;
    else
        cErrorDom("device") << "Device(" << get_param("id") << "): target_temp refused " << v;
}

void ProxyClimate::suspend(ClimateMode reported)
{
    cWarningDom("device") << "Device(" << get_param("id") << "): the unit reports " << modeName(reported)
                          << " where Calaos sent " << modeName(emu.commanded)
                          << ": its mode was changed outside Calaos, auto is suspended until it "
                          << "is selected again";
    emu.active = false;
    emu.lockout.reset();
    emu.startsAt.reset();
    saveEmulation();
}

//Publishes the device when auto, its fault or its lock-out start or end.
void ProxyClimate::publishStatusChange()
{
    const string status = emu.active ? string(AUTO) + "|" + emu.fault + "|" +
                                           (emu.startsAt ? modeName(emu.waiting) : string())
                                     : string();
    if (status == emu.publishedStatus)
        return;
    emu.publishedStatus = status;
    deviceStateChanged();
}

/* The mode IO's availability is the appliance's (an ESPHome last will covers
 * all its entities). Only false is said: clients take an absent key as
 * connected. */
Json ProxyClimate::deviceState()
{
    Json state = ClimateBase::deviceState();
    if (freshness(linkIo(MODE)) == Freshness::Disconnected)
        state["connected"] = false;
    if (!emu.active)
        return Json(std::move(state));

    //The unit is off inside the range: the device in auto is idle.
    if (state.contains("action") && state["action"] == "off")
        state["action"] = actionName(ClimateAction::Idle);
    if (!emu.fault.empty())
        state["fault"] = emu.fault;
    //As the thermostat's: seconds left, rounded up, clients count down.
    const uint64_t now = loopNow();
    if (emu.startsAt && *emu.startsAt > now)
    {
        const auto remaining = static_cast<int64_t>(std::ceil((*emu.startsAt - now) / 1000.0));
        state["lockout"] = Json{ { "output", modeName(emu.waiting) }, { "remaining", remaining } };
    }
    return Json(std::move(state));
}

bool ProxyClimate::rangeBound(const string &role, double &value)
{
    const auto it = commanded.find(role);
    if (it != commanded.end())
    {
        const std::chrono::duration<double> age = Clock::now() - it->second.at;
        if (age.count() < readbackHold)
        {
            value = it->second.value;
            return true;
        }
        commanded.erase(it);
    }
    return ClimateBase::rangeBound(role, value);
}

bool ProxyClimate::writeRangeBound(const string &role, double value)
{
    if (!ClimateBase::writeRangeBound(role, value))
        return false;
    commanded[role] = { value, Clock::now() };
    return true;
}

void ProxyClimate::startRemoteTemp()
{
    keepalive.reset();
    sourceSubscription.reset();
    if (!linkIo(REMOTE) || !linkIo(SOURCE))
        return;

    sourceSubscription = IOChangeHub::Instance().subscribe(linkIoId(SOURCE),
                                                           [this](IOBase &)
                                                           {
        relayRemoteTemp();
    });
    keepalive = std::make_unique<Timer>(secondsParam("remote_temp_keepalive", DEFAULT_KEEPALIVE),
                                        [this]()
                                        {
        requestReadingIfOld(SOURCE);
        relayRemoteTemp();
    });
}

/* A source that has not read since the start would send 0 °C, or what it
 * said before the restart, and the unit would regulate on it. One that
 * reports itself disconnected, or has sent nothing for sensor_timeout, is
 * stale: the unit is better off falling back to its own sensor once the
 * updates stop. */
void ProxyClimate::relayRemoteTemp()
{
    IOBase *source = linkIo(SOURCE);
    if (!source || !isWritable(REMOTE))
        return;

    const Freshness f = freshness(source);
    const bool stale = f == Freshness::Silent || f == Freshness::Disconnected;
    if (stale && f != sourceState)
        cErrorDom("device") << "Device(" << get_param("id") << "): remote_temp_source '"
                            << source->get_param("id") << "' " << staleReason(f)
                            << ", remote_temp is no longer relayed";
    if (f == Freshness::Fresh && sourceState != Freshness::Fresh &&
        sourceState != Freshness::NotRead)
        cInfoDom("device") << "Device(" << get_param("id") << "): remote_temp_source '"
                           << source->get_param("id") << "' reads again, remote_temp is relayed";
    sourceState = f;
    if (f != Freshness::Fresh)
        return;

    writeTarget(REMOTE)->set_value(source->get_value_double());
}
