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
#include "Scenario.h"
#include "AutoScenario.h"
#include "AutoScenarioDef.h"
#include "ActionStd.h"
#include "Rule.h"
#include "Timer.h"
#include "IOFactory.h"

#include <set>

using namespace Calaos;

REGISTER_IO(Scenario)

Scenario::Scenario(Params &p):
    IOBase(p, IOBase::IO_INOUT),
    value(false),
    auto_scenario(NULL),
    auto_scenario_def(new AutoScenarioDef())
{
    ioDoc->friendlyNameSet("Scenario");
    ioDoc->descriptionSet(_("A scenario variable. Use this like a virtual button to start a scenario (list of actions)"));
    ioDoc->actionAdd("true", _("Start the scenario"));
    ioDoc->actionAdd("false", _("Stop the scenario (only for special looping scenarios)"));
    ioDoc->conditionAdd("true", _("Event triggered when scenario is started"));
    ioDoc->actionAdd("changed", _("Event triggered on any change"));

    ioDoc->paramAdd("auto_scenario", _("Internal use only for Auto Scenario. read only."), IODoc::TYPE_STRING, false, string(), true);

    cInfoDom("output") << "Scenario::Scenario(" << get_param("id") << "): Ok";

    set_param("gui_type", "scenario");

    /* THE MARKER, AND IT IS STILL `auto_scenario`. Re-key it and
     * no AutoScenario is built for an existing scenario: its rules stop being
     * claimed, its definition is never bootstrapped from them, and the whole
     * scenario silently stops existing - 18 rules on configs/raoulh.
     * `autoscenario_uid` lives NEXT TO it, never in its place.
     */
    if (get_param("auto_scenario") != "")
        auto_scenario = new AutoScenario(this);

    //The definition, when this IO carries one. Answers false and changes
    //nothing when it does not.
    auto_scenario_def->loadFromParams(get_params());

    if (!get_params().Exists("visible")) set_param("visible", "true");
    if (!get_params().Exists("log_history")) set_param("log_history", "true");
}

Scenario::~Scenario()
{
    DELETE_NULL(auto_scenario);
    DELETE_NULL(auto_scenario_def);
}

namespace
{

/* The ids of the IOs that drive the scenario itself. Same skip list as
 * AutoScenario::isScenarioInternalIO() (AutoScenario.cpp:919-928), by ID
 * rather than by pointer so that a machinery IO which failed to build - the
 * factory miss T2.18 guards - cannot be mistaken for a user action.
 * The derived ids are added too: they are what AutoScenario::createInput()
 * builds them from (`<sid>_step` & co., AutoScenario.cpp:574-599).
 */
std::set<std::string> scenarioMachineryIds(AutoScenario *as, IOBase *scenarioIo)
{
    std::set<std::string> ids;

    if (scenarioIo) ids.insert(scenarioIo->get_param("id"));
    if (!as) return ids;

    IOBase *machinery[] = { as->getIOIsActive(), as->getIOStep(), as->getIOTimer(),
                            as->getIOScheduleEnabled(), as->getIOScenario() };
    for (IOBase *io: machinery)
        if (io) ids.insert(io->get_param("id"));

    const std::string sid = as->getScenarioId();
    if (!sid.empty())
        for (const char *suffix: { "_is_active", "_step", "_timer", "_schedule",
                                   "_is_schedule_enabled" })
            ids.insert(sid + suffix);

    return ids;
}

/* The user actions of one generated rule, BY ID.
 *
 * get_output_id() and not get_output(): the id of an action whose IO has
 * disappeared is still there (E4.6.md §2.5) and it is exactly what D4 says to
 * keep. Reading it through get_output() would resolve to nullptr, which
 * isScenarioInternalIO() reports as "machinery", which is how RC3 escamotes
 * the action today.
 */
void collectRuleActions(Rule *rule, const std::set<std::string> &machinery,
                        vector<AutoScenarioDefAction> &out)
{
    if (!rule) return;

    for (int i = 0;i < rule->get_size_actions();i++)
    {
        ActionStd *act = dynamic_cast<ActionStd *>(rule->get_action(i));
        if (!act) continue;
        if (act->get_size() != 1) continue;

        const std::string id = act->get_output_id(0);
        if (id.empty()) continue;
        if (machinery.find(id) != machinery.end()) continue;

        AutoScenarioDefAction a;
        a.ioId = id;
        a.value = act->get_params().get_param(id);
        out.push_back(a);
    }
}

}

void Scenario::captureDefinitionFromRules()
{
    if (!auto_scenario) return;

    /* BOOTSTRAP ONLY. Once the definition holds a uid it IS the source of
     * truth and the rules are its projection; re-deriving it from them at
     * every save would put a lossy round trip between the client and the
     * datum - it is how an action ends up amputated without anybody asking.
     */
    if (auto_scenario_def->isDefined()) return;

    AutoScenarioDef &def = *auto_scenario_def;

    //Allocated once and then reused for ever: a uid is never recycled.
    def.uid = AutoScenarioDef::newUid();

    def.cycle = auto_scenario->isCycling();
    //`enabled` is the old `disabled`, inverted ONCE, here (D6).
    def.enabled = !auto_scenario->isDisabled();
    def.scheduleIoId = (auto_scenario->isScheduled() && auto_scenario->getIOTimeRange())?
                           auto_scenario->getIOTimeRange()->get_param("id"):
                           std::string();

    const std::set<std::string> machinery = scenarioMachineryIds(auto_scenario, this);

    const vector<Rule *> stepRules = auto_scenario->getRuleSteps();
    vector<AutoScenarioDefStep> captured;
    captured.reserve(stepRules.size());

    for (size_t i = 0;i < stepRules.size();i++)
    {
        AutoScenarioDefStep step;
        step.stepId = AutoScenarioDef::newStepId();
        step.pause = auto_scenario->getStepPause((int)i);
        collectRuleActions(stepRules[i], machinery, step.actions);

        captured.push_back(step);
    }

    def.steps.swap(captured);

    def.finalStep = AutoScenarioDefStep();
    collectRuleActions(auto_scenario->getRuleStepEnd(), machinery, def.finalStep.actions);
}

bool Scenario::SaveToXml(pugi::xml_node node)
{
    captureDefinitionFromRules();
    auto_scenario_def->saveToParams(get_params());

    return IOBase::SaveToXml(node);
}

bool Scenario::set_value(bool val)
{
    if (!isEnabled()) return true;

    /* T3.18 - THE cut, and it closes both ways in: the button/set_state, and
     * the schedule (rulePlageStart has a single action, ioScenario = "true",
     * so it comes through here too).
     *
     * ONLY on val == true: set_value(false) is the STOP path (ruleStop), and
     * cutting it would make an already running scenario impossible to stop.
     *
     * The two gates. isBroken() is live and derived from Rule::isDisabled(),
     * so no client can forge it; disabledMissingIo is persisted and sticky, so
     * it survives the reboot and the IO coming back. Either one refuses.
     * `return true` is the convention of the isEnabled() guard just above: the
     * command was accepted and deliberately did nothing.
     */
    if (val && auto_scenario &&
        (auto_scenario->isBroken() || auto_scenario->isDisabledMissingIo()))
    {
        cWarningDom("scenario") << "Scenario '" << get_param("name") << "' ("
                                << get_param("id") << ") did NOT start: "
                                << (auto_scenario->isBroken()?
                                        "it references missing IO(s)":
                                        "it is disabled until it is re-enabled "
                                        "explicitly (disabled_missing_io)")
                                << ". Missing IO(s): "
                                << auto_scenario->getMissingIoDescription();
        return true;
    }

    value = val;
    EmitSignalIO();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", val?"true":"false" } },
                         value); //Only log scenario activation

    //reset input value to 0 after 250ms (simulate button press/release)
    //T3.40: armed through the IO's lifetime tag - a deleteIO() in those
    //250 ms used to leave this write pointing at a destroyed Scenario.
    ioAlive.singleShot(0.250, [this]() { value = false; });

    return true;
}

namespace
{

/* One action of the payload. `resolved` is the whole point of naming the IO by
 * id: an action whose target is gone is still emitted, in its place, so a
 * client that reads a scenario back and sends it again cannot lose it.
 */
Json actionToJson(const AutoScenarioDefAction &a)
{
    Json jact = Json::object();

    jact["io"] = a.ioId;
    jact["value"] = a.value;
    jact["resolved"] = ListeRoom::Instance().findIO(a.ioId)? "true": "false";

    return jact;
}

Json actionsToJson(const vector<AutoScenarioDefAction> &actions)
{
    Json jacts = Json::array();

    for (const AutoScenarioDefAction &a: actions)
        jacts.push_back(actionToJson(a));

    return jacts;
}

}

Json Scenario::toJson()
{
    Json jret = Json::object();

    if (!auto_scenario)
        return jret;

    const AutoScenarioDef &def = *auto_scenario_def;
    Room *room = ListeRoom::Instance().getRoomByIO(this);

    /* THE ONE SCHEMA: every key below is read back by `autoscenario create`
     * and `autoscenario modify`, so what a client reads is what it may send.
     * The derived keys (category and the three below) are accepted and ignored
     * on the way in, which is what keeps the round trip an identity.
     */
    jret["id"] = get_param("id");
    jret["name"] = get_param("name");
    jret["room_name"] = room? room->get_name(): string();
    jret["room_type"] = room? room->get_type(): string();
    jret["visible"] = get_param("visible") == "true"? "true": "false";
    jret["cycle"] = auto_scenario->isCycling()? "true": "false";
    jret["enabled"] = auto_scenario->isDisabled()? "false": "true";
    jret["schedule"] = auto_scenario->isScheduled()?
                           auto_scenario->getIOTimeRange()->get_param("id"):
                           string("false");
    jret["category"] = auto_scenario->getCategory();

    /* The three keys of the missing-IO arbitration, and all three are needed.
     * "broken" and "disabled_missing_io" DIVERGE ON PURPOSE: broken=false with
     * the flag still true is "repaired, waiting for a manual re-enable", the
     * state a UI has to turn into a button. Emit only one of the two and that
     * state becomes indistinguishable from a healthy scenario.
     * "missing_ios" is what to repair, empty when there is nothing to.
     */
    jret["broken"] = auto_scenario->isBroken()? "true": "false";
    jret["disabled_missing_io"] = auto_scenario->isDisabledMissingIo()? "true": "false";
    jret["missing_ios"] = auto_scenario->getMissingIoDescription();

    Json jsteps = Json::array();
    for (const AutoScenarioDefStep &step: def.steps)
    {
        Json jstep = Json::object();

        jstep["step_id"] = step.stepId;
        jstep["pause"] = Utils::to_string(step.pause);
        jstep["actions"] = actionsToJson(step.actions);

        jsteps.push_back(jstep);
    }
    jret["steps"] = jsteps;

    //A FIELD OF ITS OWN, not a last entry of `steps`: len(steps) is the number
    //of steps, and the +1 invariant the synthetic end step imposed is gone.
    jret["final_step"] = Json{{ "actions", actionsToJson(def.finalStep.actions) }};

    return jret;
}
