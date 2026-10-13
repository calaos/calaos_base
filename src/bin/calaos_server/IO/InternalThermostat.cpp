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
#include "InternalThermostat.h"
#include "CalaosConfig.h"
#include "ListeRoom.h"
#include "libuvw.h"

#include <algorithm>
#include <cmath>

using namespace Calaos;

REGISTER_DEVICE(thermostat, InternalThermostat)

namespace
{

const char *const LOW = "target_temp_low";
const char *const HIGH = "target_temp_high";

//Capped at about 30 years: beyond, llround() and the deadlines would overflow.
uint64_t toMs(double seconds)
{
    return static_cast<uint64_t>(std::llround(std::min(seconds, 1e9) * 1000));
}

} //namespace

InternalThermostat::InternalThermostat(Params &p):
    ClimateBase(p)
{
}

void InternalThermostat::resolveLinks()
{
    hysteresis = nonNegativeParam("hysteresis", DEFAULT_HYSTERESIS);
    minOnMs = toMs(nonNegativeParam("min_on_time", 0));
    minOffMs = toMs(nonNegativeParam("min_off_time", 0));

    recreateOwnedIos();
    //Before the base records the scalar: a restored mode is not a change.
    restoreMode();
    ClimateBase::resolveLinks();

    const string id = get_param("id");
    if (!linkIo("current_temp"))
        cErrorDom("device") << "Device(" << id << "): no current_temp, the outputs stay OFF";
    if (!linkIo("output_heat"))
        cErrorDom("device") << "Device(" << id << "): no output_heat, heat is unavailable";

    //A duplicate id: the device holding it regulates, this one never does.
    if (ListeRoom::Instance().get_io(id) != this)
        return;

    auto inputChanged = [this](IOBase &)
    {
        evaluate();
    };
    /* An output moved: put it back, or go on with a changeover once its
     * read-back caught up. On the next loop turn and not in the callback: a
     * rule rewriting the output on its change would otherwise spin with the
     * thermostat inside one rule march. */
    auto outputChanged = [this](IOBase &)
    {
        if (outputsMoved)
            return;
        auto run = [this]()
        {
            InternalThermostat *self = this;
            //A Timer repeats: drop it, which its own slot may do.
            outputsMoved.reset();
            self->evaluate();
        };
        //Not 0: libuv runs a timer re-armed at 0 from a timer callback again
        //in the same pass, where that fight would spin just the same.
        outputsMoved = std::make_unique<Timer>(0.001, run);
    };
    //By id, as the links themselves.
    for (const Link &l: getLinks())
    {
        if (l.name == heat.role || l.name == cool.role)
            triggers.push_back(IOChangeHub::Instance().subscribe(l.io, outputChanged));
        else if (l.name == "current_temp" || l.name == "target_temp" || l.name == LOW ||
                 l.name == HIGH)
            triggers.push_back(IOChangeHub::Instance().subscribe(l.io, inputChanged));
    }

    //Once the loop runs: drivers are not ready during the load, and nothing
    //else re-evaluates a thermostat whose inputs were all restored.
    auto first = [this]()
    {
        evaluate();
    };
    ioAlive.singleShot(0, first);
}

double InternalThermostat::nonNegativeParam(const char *name, double def)
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

void InternalThermostat::recreateOwnedIos()
{
    auto recreate = [this](const char *role, Params params)
    {
        if (linkIo(role))
            return;
        params.Add("name", role);
        params.Add("visible", "false");
        if (IOBase *io = createOwnedLink(role, params))
            cWarningDom("device") << "Device(" << get_param("id") << "): no IO for '" << role
                                  << "', created '" << io->get_param("id") << "'";
    };
    auto setpoint = [](double value)
    {
        return Params{ { "type", "InternalInt" },
                       { "save", "true" },
                       { "value", Utils::to_string(value) } };
    };

    recreate("target_temp", setpoint(DEFAULT_TARGET));
    if (linkIo("output_cool"))
    {
        recreate(LOW, setpoint(DEFAULT_LOW));
        recreate(HIGH, setpoint(std::max(DEFAULT_HIGH, DEFAULT_LOW + minRange())));
    }
    recreate("action", Params{ { "type", "InternalString" } });
}

void InternalThermostat::restoreMode()
{
    string saved;
    if (!Config::Instance().ReadValueIO(get_param("id"), saved) || saved.empty())
        return;

    ClimateMode m;
    const vector<ClimateMode> modes = supportedModes();
    if (parseMode(saved, m) && std::find(modes.begin(), modes.end(), m) != modes.end())
        mode = m;
    else
        cWarningDom("device") << "Device(" << get_param("id") << "): saved mode #"
                              << Utils::logTag(saved) << " is not available, the mode is off";
}

vector<ClimateMode> InternalThermostat::supportedModes()
{
    const bool canHeat = linkIo("output_heat");
    const bool canCool = linkIo("output_cool");

    vector<ClimateMode> modes = { ClimateMode::Off };
    if (canHeat)
        modes.push_back(ClimateMode::Heat);
    if (canCool)
        modes.push_back(ClimateMode::Cool);
    if (canHeat && canCool)
        modes.push_back(ClimateMode::Auto);
    //Not a named return value: gcov charges its unwinding to the closing
    //brace, which the diff-coverage gate reads as a line never run.
    return vector<ClimateMode>(std::move(modes));
}

bool InternalThermostat::currentMode(ClimateMode &m)
{
    m = mode;
    return true;
}

//Evaluated by applyCommand(), once the whole command is applied.
bool InternalThermostat::writeMode(ClimateMode m)
{
    //Each save rewrites the whole cache file.
    if (m == mode)
        return true;
    mode = m;
    Config::Instance().SaveValueIO(get_param("id"), modeName(m));
    return true;
}

/* {"mode":"heat","target_temp":18} must not switch on against the previous
 * target and then hold for min_on_time: the fields are all applied first,
 * their own change callbacks deferred, then one evaluation runs. */
bool InternalThermostat::applyCommand(const Json &command)
{
    const bool nested = evaluating;
    evaluating = true;
    const bool applied = ClimateBase::applyCommand(command);
    evaluating = nested;
    evaluate();
    return applied;
}

InternalThermostat::OutputPair InternalThermostat::outputsAllowed()
{
    return OutputPair{ true, true };
}

void InternalThermostat::evaluate()
{
    if (evaluating)
    {
        evaluateAgain = true;
        return;
    }

    evaluating = true;
    do
    {
        evaluateAgain = false;
        regulate();
    } while (evaluateAgain);
    evaluating = false;
}

void InternalThermostat::regulate()
{
    OutputPair allowed = outputsAllowed();
    //Not left to the hook, which an override replaces.
    IOBase *sensor = linkIo("current_temp");
    const bool known = sensor && sensor->hasValue();
    allowed.heat = allowed.heat && known;
    allowed.cool = allowed.cool && known;
    const double temp = known ? sensor->get_value_double() : 0;

    OutputPair want;
    if (mode == ClimateMode::Heat || mode == ClimateMode::Auto)
        want.heat = demand(heat, temp, mode == ClimateMode::Auto ? LOW : "target_temp", false);
    if (mode == ClimateMode::Cool || mode == ClimateMode::Auto)
        want.cool = demand(cool, temp, mode == ClimateMode::Auto ? HIGH : "target_temp", true);
    want.heat = want.heat && allowed.heat;
    want.cool = want.cool && allowed.cool;
    //A stop that was asked for or that safety needs does not wait for
    //min_on_time; min_off_time still guards the restart.
    const OutputPair forced{ mode == ClimateMode::Off || !allowed.heat,
                             mode == ClimateMode::Off || !allowed.cool };

    auto loop = uvw::Loop::getDefault();
    loop->update();
    const uint64_t now = loop->now().count();

    //Every OFF before any ON, so that a changeover sees the other one off.
    for (bool on: { false, true })
    {
        for (Output *out: { &heat, &cool })
        {
            if ((out == &heat ? want.heat : want.cool) != on)
                continue;
            const bool isHeat = out == &heat;
            switchOutput(*out, isHeat ? cool : heat, on, now, isHeat ? forced.heat : forced.cool);
            //A write ran rules that changed an input: `want` is stale.
            if (evaluateAgain)
                return;
        }
    }

    writeAction();
}

/* Heat: ON below setpoint - hysteresis, OFF above setpoint, unchanged in
 * between. Cool mirrors it. Nothing to regulate against without a setpoint. */
bool InternalThermostat::demand(Output &out, double temp, const char *setpoint, bool cooling)
{
    IOBase *io = linkIo(setpoint);
    if (!io || !io->hasValue())
        return false;

    const double gap = cooling ? temp - io->get_value_double() : io->get_value_double() - temp;
    if (gap > hysteresis)
        return true;
    if (gap < 0)
        return false;
    return held(out);
}

bool InternalThermostat::held(Output &out)
{
    if (out.commanded)
        return *out.commanded;
    IOBase *io = linkIo(out.role);
    return io && io->get_value_bool();
}

bool InternalThermostat::isOn(Output &out)
{
    IOBase *io = linkIo(out.role);
    return (io && io->get_value_bool()) || out.commanded.value_or(false);
}

/* Written unless the last write and the IO's read-back, when it has one,
 * both say `on` already: a value changed behind the thermostat's back is put
 * back, and an output whose read-back lags its write still counts as ON. */
void InternalThermostat::switchOutput(Output &out, Output &other, bool on, uint64_t now,
                                      bool forced)
{
    IOBase *io = linkIo(out.role);
    if (!io)
        return;

    if (!out.commanded && io->hasValue())
        out.commanded = io->get_value_bool();
    const bool ioAgrees = !io->hasValue() || io->get_value_bool() == on;
    if (out.commanded == on && ioAgrees)
    {
        out.lockout.reset();
        return;
    }

    //Never both: the other one switching OFF, or its read-back, evaluates again.
    if (on && isOn(other))
    {
        out.lockout.reset();
        return;
    }

    //Putting back a value changed behind the thermostat is not a switch: no
    //lock-out, and its clock keeps the thermostat's own last switch.
    const bool transition = out.commanded.value_or(false) != on;
    if (transition)
    {
        const uint64_t wait = lockedFor(out, other, on, now, forced);
        if (wait > 0)
        {
            Output *o = &out;
            auto expired = [this, o]()
            {
                InternalThermostat *self = this;
                //A Timer repeats: drop it, which its own slot may do.
                o->lockout.reset();
                self->evaluate();
            };
            o->lockout = std::make_unique<Timer>(wait / 1000.0, expired);
            return;
        }
    }

    out.lockout.reset();
    //A refused write changed nothing: neither the state held nor its clock.
    if (!io->set_value(on))
    {
        cErrorDom("device") << "Device(" << get_param("id") << "): " << out.role << " '"
                            << io->get_param("id") << "' refused to switch "
                            << (on ? "ON" : "OFF");
        return;
    }
    if (transition)
        (on ? out.onAt : out.offAt) = now;
    out.commanded = on;
}

//ms before `out` may switch to `on`; a changeover also waits for the other
//output's min_off_time. A forced stop waits for nothing.
uint64_t InternalThermostat::lockedFor(const Output &out, const Output &other, bool on,
                                       uint64_t now, bool forced) const
{
    uint64_t until = 0;
    auto after = [&until](const std::optional<uint64_t> &since, uint64_t delay)
    {
        if (since)
            until = std::max(until, *since + delay);
    };

    if (on)
    {
        after(out.offAt, minOffMs);
        after(other.offAt, minOffMs);
    }
    else if (!forced)
        after(out.onAt, minOnMs);

    return until > now ? until - now : 0;
}

void InternalThermostat::writeAction()
{
    IOBase *io = linkIo("action");
    if (!io)
        return;

    ClimateAction action = ClimateAction::Idle;
    if (held(heat))
        action = ClimateAction::Heating;
    else if (held(cool))
        action = ClimateAction::Cooling;
    else if (mode == ClimateMode::Off)
        action = ClimateAction::Off;

    const string name = actionName(action);
    if (!io->hasValue() || io->get_value_string() != name)
        io->set_value(name);
}
