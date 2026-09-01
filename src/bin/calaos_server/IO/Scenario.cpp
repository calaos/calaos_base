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

    /* THE MARKER, AND IT IS STILL `auto_scenario` - E4.6b DOES NOT RE-KEY IT.
     *
     * E4.6.md §5.2, the one thing in the whole epic that can destroy user
     * data: ListeRoom::checkAutoScenario() (ListeRoom.cpp:320-330) destroys
     * every rule that carries the `auto_scenario` param and that no
     * AutoScenario has adopted, then SaveConfigRule() persists it. Re-key this
     * test and no AutoScenario is built for an existing scenario, nothing
     * adopts its rules, and the sweep destroys them all - 18 of them on
     * configs/raoulh - at the first startup, in silence.
     * The sweep is E4.6c's to remove; until then the marker stays where it is,
     * and `autoscenario_uid` lives NEXT TO it.
     */
    if (get_param("auto_scenario") != "")
    {
        auto_scenario = new AutoScenario(this);
        setAutoScenario(true);
    }

    //E4.6b. The definition, when this IO carries one. Answers false and
    //changes nothing when it does not.
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

    AutoScenarioDef &def = *auto_scenario_def;

    //Allocated once and then reused for ever: a uid is never recycled (D3).
    if (def.uid.empty()) def.uid = AutoScenarioDef::newUid();

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
        //REUSE the id already held at this position, so two consecutive saves
        //of an unchanged scenario write the same bytes. E4.6c makes the
        //identity real (the definition becomes the source); here it is as
        //stable as a positional model can be.
        step.stepId = (i < def.steps.size() && !def.steps[i].stepId.empty())?
                          def.steps[i].stepId: AutoScenarioDef::newStepId();
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

json_t *Scenario::toJson()
{
    json_t *jret = json_object();

    if (!auto_scenario)
        return jret;

    json_object_set_new(jret, "id", json_string(get_param("id").c_str()));
    json_object_set_new(jret, "cycle", json_string(auto_scenario->isCycling()?"true":"false"));
    json_object_set_new(jret, "enabled", json_string(auto_scenario->isDisabled()?"false":"true"));
    json_object_set_new(jret, "schedule", json_string(auto_scenario->isScheduled()?
                                                          auto_scenario->getIOTimeRange()->get_param("id").c_str():
                                                          "false"));
    json_object_set_new(jret, "category", json_string(auto_scenario->getCategory().c_str()));

    /* T3.18 - three keys, and all three are needed. They are READ ONLY:
     * buildAutoscenarioModify() does not consume any of them.
     *
     * "broken" and "disabled_missing_io" DIVERGE ON PURPOSE. broken=false with
     * the flag still true is "repaired, waiting for a manual re-enable" - the
     * state the whole ticket exists for, and the one an UI has to turn into a
     * "re-enable" button. Emit only one of the two and that state becomes
     * indistinguishable from a healthy scenario, which is the silence T3.18
     * removes.
     * "missing_ios" is what to repair, empty when there is nothing to.
     */
    json_object_set_new(jret, "broken",
                        json_string(auto_scenario->isBroken()?"true":"false"));
    json_object_set_new(jret, "disabled_missing_io",
                        json_string(auto_scenario->isDisabledMissingIo()?"true":"false"));
    json_object_set_new(jret, "missing_ios",
                        json_string(auto_scenario->getMissingIoDescription().c_str()));

    json_object_set_new(jret, "steps_count", json_string(Utils::to_string(auto_scenario->getRuleSteps().size()).c_str()));

    json_t *jsteps = json_array();

    for (uint i = 0;i < auto_scenario->getRuleSteps().size();i++)
    {
        json_t *jstep = json_object();
        json_object_set_new(jstep, "step_pause", json_string(Utils::to_string(auto_scenario->getStepPause(i)).c_str()));
        json_object_set_new(jstep, "step_type", json_string("standard"));

        json_t *jacts = json_array();
        for (int j = 0;j < auto_scenario->getStepActionCount(i);j++)
        {
            ScenarioAction sa = auto_scenario->getStepAction(i, j);

            //Defensive, same guard as AutoScenario::getCategory(): an index
            //that does not resolve gives back an empty action
            if (!sa.io) continue;

            json_t *jact = json_object();
            json_object_set_new(jact, "id", json_string(sa.io->get_param("id").c_str()));
            json_object_set_new(jact, "action", json_string(sa.action.c_str()));
            json_array_append_new(jacts, jact);

        }
        json_object_set_new(jstep, "actions", jacts);

        json_array_append_new(jsteps, jstep);
    }

    //add end step
    {
        json_t *jstep = json_object();
        json_object_set_new(jstep, "step_type", json_string("end"));

        json_t *jacts = json_array();
        for (int j = 0;j < auto_scenario->getEndStepActionCount();j++)
        {
            ScenarioAction sa = auto_scenario->getEndStepAction(j);

            if (!sa.io) continue;

            json_t *jact = json_object();
            json_object_set_new(jact, "id", json_string(sa.io->get_param("id").c_str()));
            json_object_set_new(jact, "action", json_string(sa.action.c_str()));
            json_array_append_new(jacts, jact);
        }
        json_object_set_new(jstep, "actions", jacts);
        json_array_append_new(jsteps, jstep);
    }

    json_object_set_new(jret, "steps", jsteps);

    return jret;
}
