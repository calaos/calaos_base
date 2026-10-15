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

//The loop clock the timers use, refreshed: outside a loop turn it lags.
uint64_t loopNow()
{
    auto loop = uvw::Loop::getDefault();
    loop->update();
    return loop->now().count();
}

/* Never freed: the first thermostat is built after ListeRoom, so a function
 * static would die before it, and ~ListeRoom() destroys the thermostats left
 * in their rooms at exit. */
vector<InternalThermostat *> &liveThermostats()
{
    static auto *all = new vector<InternalThermostat *>();
    return *all;
}

//Not through REGISTER_DEVICE: it registers a factory, not an observer.
[[maybe_unused]] const bool observing = (Device::observeOutsideWrites(&InternalThermostat::warnIfProtected), true);

} //namespace

InternalThermostat::InternalThermostat(Params &p):
    ClimateBase(p)
{
    liveThermostats().push_back(this);

    ioDoc->friendlyNameSet(_("Climate thermostat"));
    ioDoc->descriptionSet(_("A thermostat that regulates in Calaos: it switches output_heat and output_cool "
                            "from current_temp and its setpoints. Its mode is stored by the device."));
    ioDoc->descriptionBaseSet(_("Night setback, away mode and the like are rules writing the setpoints or the "
                                "mode. Missing setpoint and action IOs are created at load."));

    ioDoc->paramAdd("hysteresis", _("Degrees past the setpoint before an output starts switching on: heat below "
                                    "the setpoint minus hysteresis, cool above the setpoint plus hysteresis. "
                                    "Switching off comes as soon as the setpoint is crossed."),
                    IODoc::TYPE_FLOAT, false, Utils::to_string(DEFAULT_HYSTERESIS), false, _("Hysteresis"));
    ioDoc->paramAdd("min_on_time", _("Seconds an output stays on at least, except when the mode is off or a "
                                     "safety cut stops it."),
                    IODoc::TYPE_FLOAT, false, "0", false, _("Minimum on time"));
    ioDoc->paramAdd("min_off_time", _("Seconds an output stays off at least before it can start again."),
                    IODoc::TYPE_FLOAT, false, "0", false, _("Minimum off time"));
    ioDoc->paramAdd("put_back_delay", _("Seconds before an output that something else keeps switching is put "
                                        "back; the wait doubles while the other writer keeps undoing it, up to "
                                        "put_back_delay_max. At least 0.001, else the default is used."),
                    IODoc::TYPE_FLOAT, false, Utils::to_string(DEFAULT_PUT_BACK_DELAY), false, _("Put-back delay"));
    ioDoc->paramAdd("put_back_delay_max", _("Longest wait between two put-backs, in seconds. Not below "
                                            "put_back_delay. It also sets the window of the log of writes "
                                            "from outside to an output: one line per source and output "
                                            "during that time."),
                    IODoc::TYPE_FLOAT, false, Utils::to_string(DEFAULT_PUT_BACK_DELAY_MAX), false,
                    _("Maximum put-back delay"));
    //One call per line: gcov charges the elements of an initializer list to no line.
    Params emitters;
    emitters.Add("electric", _("Electric heaters"));
    emitters.Add("water_radiator", _("Water radiators (boiler, zone valve)"));
    emitters.Add("floor", _("Underfloor heating"));
    emitters.Add("heat_pump", _("Reversible heat pump on contacts"));
    emitters.Add("fan_coil", _("Fan coils (valves on a water loop)"));
    ioDoc->paramAddList("emitter", _("What the outputs drive. Only floor has an effect: floor cooling then needs a "
                                     "humidity link and stops at humidity_max (dew point)."),
                        false, emitters, string(), false, _("Emitter"));
    ioDoc->paramAdd("humidity_max", _("Relative humidity (%) from which floor cooling stops, with "
                                      "emitter=floor. Not negative."),
                    IODoc::TYPE_FLOAT, false, Utils::to_string(DEFAULT_HUMIDITY_MAX), false,
                    _("Maximum humidity"));
    for (const char *name: { "hysteresis", "min_on_time", "min_off_time", "put_back_delay",
                             "put_back_delay_max", "emitter", "humidity_max" })
        ioDoc->paramInChild(name);

    const string owner = _(" An output belongs to a single thermostat: a second one linking it ignores the link.");
    ioDoc->linkRoleAdd("current_temp", _("Current temperature"),
                       _("Temperature the regulation reads. The outputs stay off until it has "
                         "delivered a reading since the start (a cached or retained value does "
                         "not count), and while it has sent nothing for sensor_timeout or "
                         "reports itself disconnected."),
                       IODoc::LINK_NUMBER, true, false);
    ioDoc->linkRoleAdd("target_temp", _("Setpoint"), _("Setpoint used in heat and cool. Created when missing."),
                       IODoc::LINK_NUMBER, true, true);
    ioDoc->linkRoleAdd("target_temp_low", _("Low setpoint"),
                       _("Low bound of the auto range. Created when missing, with output_cool."),
                       IODoc::LINK_NUMBER, false, true);
    ioDoc->linkRoleAdd("target_temp_high", _("High setpoint"),
                       _("High bound of the auto range. Created when missing, with output_cool."),
                       IODoc::LINK_NUMBER, false, true);
    ioDoc->linkRoleAdd("action", _("Action"),
                       _("Written by the thermostat: off, idle, heating or cooling. Created when missing."),
                       IODoc::LINK_STRING, false, true);
    ioDoc->linkRoleAdd("output_heat", _("Heating output"), _("Output driven for heating. Enables heat.") + owner,
                       IODoc::LINK_BOOL, true, true);
    ioDoc->linkRoleAdd("output_cool", _("Cooling output"),
                       _("Output driven for cooling. Enables cool and auto.") + owner, IODoc::LINK_BOOL, false,
                       true);
    ioDoc->linkRoleAdd("humidity", _("Humidity sensor"),
                       _("Relative humidity of the room. With emitter=floor, cooling stops while it "
                         "reads humidity_max or more, has not read, is stale or disconnected; "
                         "without it, a floor offers neither cool nor auto."),
                       IODoc::LINK_NUMBER, false, false, "emitter", "floor", "output_cool");

    documentCommand({ ClimateMode::Off, ClimateMode::Heat, ClimateMode::Cool, ClimateMode::Auto }, "",
                    _(" Heat needs the output_heat link, cool needs output_cool and auto needs both."),
                    { { "heat", { "output_heat" } },
                      { "cool", { "output_cool" } },
                      { "auto", { "output_heat", "output_cool" } } });
}

InternalThermostat::~InternalThermostat()
{
    auto &all = liveThermostats();
    all.erase(std::find(all.begin(), all.end(), this));
}

void InternalThermostat::resolveLinks()
{
    hysteresis = nonNegativeParam("hysteresis", DEFAULT_HYSTERESIS);
    floorCooling = getDeviceParam("emitter") == "floor";
    humidityMax = nonNegativeParam("humidity_max", DEFAULT_HUMIDITY_MAX);
    minOnMs = toMs(nonNegativeParam("min_on_time", 0));
    minOffMs = toMs(nonNegativeParam("min_off_time", 0));
    readPutBackDelays();

    claimOutputs();
    recreateOwnedIos();
    //Before the base records the scalar: a restored mode is not a change.
    restoreMode();
    ClimateBase::resolveLinks();

    const string id = get_param("id");
    if (!linkIo("current_temp"))
        cErrorDom("device") << "Device(" << id << "): no current_temp, the outputs stay OFF";
    if (!outputIo(heat))
        cErrorDom("device") << "Device(" << id << "): no output_heat, heat is unavailable";
    if (outputIo(cool) && !canCool())
        cErrorDom("device") << "Device(" << id << "): emitter=floor without a humidity link, "
                            << "cool and auto are refused";
    warnIfChangesOnly("current_temp");
    if (floorCooling)
        warnIfChangesOnly("humidity");

    //A duplicate id: the device holding it regulates, this one never does.
    if (ListeRoom::Instance().get_io(id) != this)
        return;

    auto inputChanged = [this](IOBase &)
    {
        evaluate();
    };
    //By id, as the links themselves.
    for (const Link &l: getLinks())
    {
        Output *out = nullptr;
        if (l.name == heat.role)
            out = &heat;
        else if (l.name == cool.role)
            out = &cool;
        if (out && !out->refused)
        {
            auto outputChanged = [this, out](IOBase &io)
            {
                noticeOutput(*out, io);
                evaluateSoon();
            };
            triggers.push_back(IOChangeHub::Instance().subscribe(l.io, outputChanged));
        }
        else if (l.name == "current_temp" || l.name == "target_temp" || l.name == LOW ||
                 l.name == HIGH || l.name == "humidity")
            triggers.push_back(IOChangeHub::Instance().subscribe(l.io, inputChanged));
    }

    //Once the loop runs: drivers are not ready during the load, and nothing
    //else re-evaluates a thermostat whose inputs were all restored.
    auto first = [this]()
    {
        //A relay may have gone OFF just before a restart: the start counts
        //as a switch OFF, min_off_time passes before the first ON.
        heat.st.offAt = cool.st.offAt = loopNow();
        evaluate();
    };
    ioAlive.singleShot(0, first);

    auto check = [this]()
    {
        requestReadingIfOld("current_temp");
        if (floorCooling)
            requestReadingIfOld("humidity");
        evaluate();
    };
    //Several times per sensor_timeout: an old sensor is asked before it is stale.
    freshnessCheck = std::make_unique<Timer>(std::min(sensorTimeout() / 4, FRESHNESS_POLL), check);
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

/* An output belongs to one thermostat: the first one loaded keeps it, a later
 * one logs it and leaves that link aside as if it did not resolve. One IO as
 * both outputs keeps the heat one. Called in load order, once every room is
 * loaded. */
void InternalThermostat::claimOutputs()
{
    ListeRoom &lr = ListeRoom::Instance();
    const string id = get_param("id");
    //A shadowed duplicate drives nothing, so it claims nothing either.
    if (lr.get_io(id) != this)
        return;

    IOBase *heatIo = linkIo(heat.role);
    if (heatIo && linkIo(cool.role) == heatIo)
    {
        cErrorDom("device") << "Device(" << id << "): output_cool '" << heatIo->get_param("id")
                            << "' is also its output_heat, the output_cool link is ignored";
        cool.refused = true;
    }

    for (Output *out: { &heat, &cool })
    {
        IOBase *io = outputIo(*out);
        for (int i = 0; io && i < lr.size(); i++)
        {
            Room *room = lr.get_room(i);
            for (int j = 0; io && j < room->get_size(); j++)
            {
                auto *other = dynamic_cast<InternalThermostat *>(room->get_io(j));
                if (!other || other == this || !other->claimed ||
                    (other->outputIo(other->heat) != io && other->outputIo(other->cool) != io))
                    continue;
                cErrorDom("device") << "Device(" << id << "): " << out->role << " '"
                                    << io->get_param("id") << "' is already an output of thermostat '"
                                    << other->get_param("id") << "', this link is ignored";
                out->refused = true;
                io = nullptr;
            }
        }
    }
    claimed = true;
}

IOBase *InternalThermostat::outputIo(Output &out)
{
    return out.refused ? nullptr : linkIo(out.role);
}

Json InternalThermostat::deviceIos()
{
    Json ios = ClimateBase::deviceIos();
    for (Output *out: { &heat, &cool })
    {
        if (out->refused)
            ios.erase(out->role);
    }
    return Json(std::move(ios));
}

//A spacing of 0 would never grow, and a maximum below it would cut it.
void InternalThermostat::readPutBackDelays()
{
    const string id = get_param("id");
    double shortest = nonNegativeParam("put_back_delay", DEFAULT_PUT_BACK_DELAY);
    if (shortest < MIN_PUT_BACK_DELAY)
    {
        cErrorDom("device") << "Device(" << id << "): put_back_delay '"
                            << getDeviceParam("put_back_delay") << "' is shorter than "
                            << MIN_PUT_BACK_DELAY << " s, " << DEFAULT_PUT_BACK_DELAY << " is used";
        shortest = DEFAULT_PUT_BACK_DELAY;
    }

    double longest = nonNegativeParam("put_back_delay_max", DEFAULT_PUT_BACK_DELAY_MAX);
    if (longest < shortest)
    {
        longest = std::max(DEFAULT_PUT_BACK_DELAY_MAX, shortest);
        if (hasDeviceParam("put_back_delay_max"))
            cErrorDom("device") << "Device(" << id << "): put_back_delay_max '"
                                << getDeviceParam("put_back_delay_max")
                                << "' is shorter than put_back_delay, " << longest << " is used";
    }

    putBackMs = toMs(shortest);
    putBackMaxMs = toMs(longest);
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
    if (outputIo(cool))
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
    {
        mode = m;
        return;
    }

    cWarningDom("device") << "Device(" << get_param("id") << "): saved mode #"
                          << Utils::logTag(saved) << " is not available, the mode is off";
    //Or it would come back by itself with the output it needs.
    Config::Instance().SaveValueIO(get_param("id"), modeName(ClimateMode::Off));
}

bool InternalThermostat::canCool()
{
    //Nothing would guard a cooling floor against condensation.
    return outputIo(cool) && (!floorCooling || linkIo("humidity"));
}

bool InternalThermostat::isWritable(const string &role)
{
    if ((role == LOW || role == HIGH) && outputIo(cool) && !canCool())
        return false;
    return ClimateBase::isWritable(role);
}

vector<ClimateMode> InternalThermostat::supportedModes()
{
    const bool canHeat = outputIo(heat);
    const bool canCool = this->canCool();

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
    const bool cooling = mode == ClimateMode::Cool || mode == ClimateMode::Auto;
    return OutputPair{ true, !floorCooling || !cooling || humidityAllowsCooling() };
}

bool InternalThermostat::humidityAllowsCooling()
{
    IOBase *h = linkIo("humidity");
    const Freshness f = freshness(h);
    HumidityCut cut = HumidityCut::None;
    if (f == Freshness::NotRead)
        cut = HumidityCut::Unknown;
    else if (f != Freshness::Fresh)
        cut = HumidityCut::Stale;
    else if (h->get_value_double() >= humidityMax)
        cut = HumidityCut::High;

    const string id = get_param("id");
    if (cut == humidityCut)
        return cut == HumidityCut::None;
    //Not read yet since the start is waited for silently, like current_temp.
    if (cut == HumidityCut::Stale)
        cErrorDom("device") << "Device(" << id << "): humidity '" << h->get_param("id") << "' "
                            << staleReason(f) << ", output_cool is OFF";
    else if (cut == HumidityCut::High)
        cWarningDom("device") << "Device(" << id << "): humidity '" << h->get_param("id")
                              << "' reads " << h->get_value_double()
                              << ", at or above humidity_max " << humidityMax
                              << ", output_cool is OFF";
    else if (cut == HumidityCut::None && humidityCut != HumidityCut::Unknown)
        cInfoDom("device") << "Device(" << id << "): humidity '" << h->get_param("id")
                           << "' allows cooling again";
    humidityCut = cut;
    return cut == HumidityCut::None;
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

/* Once the rule march that moved an output is over: put the output back, or
 * go on with a changeover once its read-back caught up. Not at 0: libuv runs
 * a timer re-armed at 0 from a timer callback again in the same pass. */
void InternalThermostat::evaluateSoon()
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
    outputsMoved = std::make_unique<Timer>(0.001, run);
}

/* The output now reads something else than what the thermostat holds, and
 * its own writes and read-backs never do: something else switched it. That
 * counts as a switch, so putting it back waits for the lock-out it started;
 * a put-back undone within its spacing doubles the spacing of the next one. */
void InternalThermostat::noticeOutput(Output &out, IOBase &io)
{
    Output::State &st = out.st;
    if (!st.commanded || !io.hasValue() || io.get_value_bool() == *st.commanded)
        return;

    const bool v = io.get_value_bool();
    const uint64_t now = loopNow();
    st.fighting = st.putBackAt && now - *st.putBackAt <= st.spacingMs;
    if (!st.fighting)
        st.spacingMs = putBackMs;
    (v ? st.onAt : st.offAt) = now;
    st.commanded = v;
    st.foreign = true;
    const bool undone = st.putBackWritten;
    st.putBackWritten = false;

    if (!out.warned)
    {
        out.warned = true;
        cWarningDom("device") << "Device(" << get_param("id") << "): " << out.role << " '"
                              << io.get_param("id") << "' was switched by something else; it is "
                              << "put back after its lock-outs, less and less often while "
                              << "that goes on";
    }

    /* The only defence against a rule that lights a stopped output again.
     * No limiter of its own: a put-back comes at least put_back_delay after
     * the previous one, and only a put-back can be undone. */
    if (undone && v && out.stopForced)
        cErrorDom("device") << "Device(" << get_param("id") << "): " << out.role << " '"
                            << io.get_param("id") << "' was switched ON again after the thermostat "
                            << "stopped it (" << (mode == ClimateMode::Off ? "mode off" : "safety")
                            << "); something keeps undoing the stop";
}

bool InternalThermostat::sensorFresh(IOBase *sensor)
{
    const Freshness f = freshness(sensor);
    const bool stale = f == Freshness::Silent || f == Freshness::Disconnected;
    if (stale && f != sensorState)
        cErrorDom("device") << "Device(" << get_param("id") << "): current_temp '"
                            << sensor->get_param("id") << "' " << staleReason(f)
                            << ", the outputs are OFF";
    if (f == Freshness::Fresh && sensorState != Freshness::Fresh &&
        sensorState != Freshness::NotRead)
        cInfoDom("device") << "Device(" << get_param("id") << "): current_temp '"
                           << sensor->get_param("id") << "' reads again, regulation resumes";
    sensorState = f;
    return f == Freshness::Fresh;
}

void InternalThermostat::regulate()
{
    OutputPair allowed = outputsAllowed();
    /* Not left to the hook, which an override replaces. A value restored
     * from the state cache is the one from before the restart. */
    IOBase *sensor = linkIo("current_temp");
    const bool known = sensorFresh(sensor);
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
    heat.stopForced = forced.heat;
    cool.stopForced = forced.cool;

    const uint64_t now = loopNow();

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
    fault = safetyFault(mode == ClimateMode::Cool || mode == ClimateMode::Auto);
    publishStatusChange();
}

/* Decision 20: the thermostat stays reachable whatever its sensor does, so a
 * disconnected sensor is a fault and not connected:false. Mode off holds the
 * outputs by itself, no rule does. */
string InternalThermostat::safetyFault(bool cooling)
{
    if (mode == ClimateMode::Off)
        return string();
    if (!linkIo("current_temp"))
        return "sensor_missing";
    if (sensorState == Freshness::NotRead)
        return "sensor_waiting";
    if (sensorState == Freshness::Silent)
        return "sensor_stale";
    if (sensorState == Freshness::Disconnected)
        return "sensor_disconnected";
    if (!floorCooling || !cooling || humidityCut == HumidityCut::None)
        return string();
    return humidityCut == HumidityCut::High ? "humidity_high" : "humidity_stale";
}

void InternalThermostat::publishStatusChange()
{
    const Output *waiting = waitingOutput();
    const string status = fault + "|" + (waiting ? waiting->role : "");
    if (status == publishedStatus)
        return;
    publishedStatus = status;
    deviceStateChanged();
}

Json InternalThermostat::deviceState()
{
    Json state = ClimateBase::deviceState();
    if (!fault.empty())
        state["fault"] = fault;

    //Seconds left, rounded up: clients count down from it. A deadline already
    //passed waits for its timer to fire, on this loop turn.
    const Output *waiting = waitingOutput();
    const uint64_t now = loopNow();
    if (waiting && *waiting->startsAt > now)
    {
        const auto remaining = static_cast<int64_t>(std::ceil((*waiting->startsAt - now) / 1000.0));
        state["lockout"] = Json{ { "output", waiting == &heat ? "heat" : "cool" }, { "remaining", remaining } };
    }
    return Json(std::move(state));
}

const InternalThermostat::Output *InternalThermostat::waitingOutput() const
{
    if (heat.startsAt)
        return &heat;
    return cool.startsAt ? &cool : nullptr;
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
    if (out.st.commanded)
        return *out.st.commanded;
    IOBase *io = outputIo(out);
    return io && io->get_value_bool();
}

bool InternalThermostat::isOn(Output &out)
{
    IOBase *io = outputIo(out);
    return (io && io->get_value_bool()) || out.st.commanded.value_or(false);
}

/* Written unless the last write and the IO's read-back, when it has one,
 * both say `on` already: an output whose read-back lags its write still
 * counts as ON. */
void InternalThermostat::switchOutput(Output &out, Output &other, bool on, uint64_t now,
                                      bool forced)
{
    out.startsAt.reset();
    IOBase *io = outputIo(out);
    if (!io)
        return;

    Output::State &st = out.st;
    if (!st.commanded && io->hasValue())
        st.commanded = io->get_value_bool();

    //Never both: the other one switching OFF, or its read-back, evaluates again.
    if (on && isOn(other))
    {
        out.lockout.reset();
        //It is ON behind the thermostat's back and cannot be put back yet:
        //this one yields, at once.
        if (held(out))
            switchOutput(out, other, false, now, true);
        return;
    }

    const bool ioAgrees = !io->hasValue() || io->get_value_bool() == on;
    if (st.commanded == on && ioAgrees)
    {
        out.lockout.reset();
        return;
    }

    const bool transition = st.commanded.value_or(false) != on;
    //Switching back what something else switched.
    const bool putBack = transition && st.foreign;
    if (transition)
    {
        //A stop asked for, needed for safety, or with both ON waits for no lock-out.
        uint64_t wait = lockedFor(out, other, on, now, forced || (!on && isOn(other)));
        if (putBack && st.putBackAt && *st.putBackAt + st.spacingMs > now)
            wait = std::max(wait, *st.putBackAt + st.spacingMs - now);
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
            if (on)
                out.startsAt = now + wait;
            return;
        }
    }

    out.lockout.reset();
    //Recorded before the write, so that what its rules do to the output is
    //noticed against it; undone if the IO refuses the write.
    const Output::State before = st;
    if (transition)
        (on ? st.onAt : st.offAt) = now;
    if (putBack)
    {
        if (st.fighting)
            st.spacingMs = std::min(st.spacingMs * 2, putBackMaxMs);
        st.putBackAt = now;
    }
    st.putBackWritten = putBack;
    st.foreign = false;
    st.commanded = on;
    if (!io->set_value(on))
    {
        st = before;
        cErrorDom("device") << "Device(" << get_param("id") << "): " << out.role << " '"
                            << io->get_param("id") << "' refused to switch "
                            << (on ? "ON" : "OFF");
    }
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
        after(out.st.offAt, minOffMs);
        after(other.st.offAt, minOffMs);
    }
    else if (!forced)
        after(out.st.onAt, minOnMs);

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

/* Rate limit: one line per source and output within put_back_delay_max, the
 * longest a fight is spaced. A rule firing every second so costs a line a
 * minute, a rare writer is logged every time, and a changed source shows at
 * once. */
void InternalThermostat::warnWrite(Output &out, IOBase &io, const char *source)
{
    const uint64_t now = loopNow();
    auto last = out.writeWarnedAt.find(source);
    if (last != out.writeWarnedAt.end() && now - last->second < putBackMaxMs)
        return;
    out.writeWarnedAt[source] = now;

    cWarningDom("device") << "Device(" << get_param("id") << "): " << out.role << " '"
                          << io.get_param("id") << "' is driven by the thermostat and was written by "
                          << source << "; the write is applied, the thermostat takes it back "
                          << "after its lock-outs";
}

void InternalThermostat::warnIfProtected(IOBase &io, const char *source)
{
    for (InternalThermostat *t: liveThermostats())
    {
        if (!t->claimed)
            continue;
        for (Output *out: { &t->heat, &t->cool })
        {
            if (t->outputIo(*out) == &io)
                t->warnWrite(*out, io, source);
        }
    }
}
