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
#include <algorithm>

#include "AutoScenario.h"
#include "AutoScenarioDef.h"
#include "EventManager.h"
using namespace Calaos;

namespace
{

//The generated step number, or -1 when the rule does not carry one.
int stepNumberOf(Rule *r)
{
    if (!r || !r->param_exists("auto_scenario_step")) return -1;

    int n = -1;
    from_string(r->get_param("auto_scenario_step"), n);
    return n;
}

}

AutoScenario::AutoScenario(IOBase *input):
    ioScenario(dynamic_cast<Scenario *>(input)),
    ioIsActive(NULL),
    ioScheduleEnabled(NULL),
    ioStep(NULL),
    ioTimer(NULL),
    ioTimeRange(NULL),
    roomContainer(NULL)
{
    cInfoDom("scenario") << "AutoScenario::AutoScenario(" << input->get_param("id") << "): Ok";

    scenario_id = input->get_param("auto_scenario");
    cycle = (input->get_param("cycle") == "true")?true:false;
    disabled = (input->get_param("disabled") == "true")?true:false;

    /* T3.18. READ HERE, with cycle and disabled, and nowhere else: this runs
     * from IOFactory::CreateIO() while the config is being read, so it is the
     * only point that is guaranteed to be BEFORE any SaveConfigIO(). The very
     * first thing the server does after loading is
     * ListeRoom::checkAutoScenario(), which ends with SaveConfigIO(): a flag
     * read too late - or not read at all - would be erased from io.xml at the
     * first startup, silently, with no user action involved. That is the only
     * erasure vector of this ticket that nobody could notice.
     */
    disabledMissingIo = (input->get_param("disabled_missing_io") == "true")?true:false;

    if (disabledMissingIo)
    {
        cWarningDom("scenario") << "AutoScenario (" << scenario_id << "): loaded "
                                << "DISABLED, it referenced a missing IO. It will not "
                                << "start until it is re-enabled explicitly.";
    }

    ListeRoom::Instance().addScenarioCache(ioScenario);
}

AutoScenario::~AutoScenario()
{
    ListeRoom::Instance().delScenarioCache(ioScenario);
}

AutoScenarioDef *AutoScenario::definition() const
{
    return ioScenario? ioScenario->getDefinition(): nullptr;
}

string AutoScenario::getScenarioUid() const
{
    AutoScenarioDef *def = definition();
    return def? def->uid: string();
}

list<Rule *> AutoScenario::scenarioRules() const
{
    list<Rule *> rules = ListeRule::Instance().getRuleAutoScenario(scenario_id);

    //A generated rule carries both markers, so the legacy lookup above already
    //returns it. Only a rule stamped with the uid alone has to be added.
    const string uid = getScenarioUid();
    if (uid.empty()) return rules;

    for (Rule *r: ListeRule::Instance().getRulesOfScenarioUid(uid))
        if (std::find(rules.begin(), rules.end(), r) == rules.end())
            rules.push_back(r);

    return rules;
}

list<Rule *> AutoScenario::generatedRules() const
{
    return ListeRule::Instance().getRulesOfScenarioUid(getScenarioUid());
}

bool AutoScenario::hasLegacyRules() const
{
    /* On the very first startup of a configuration written before the
     * definition existed, OUR uid is empty too - so the test has to be
     * "carries no uid at all", never "carries a uid other than ours".
     */
    for (Rule *r: scenarioRules())
        if (r && (!r->param_exists(AutoScenarioDef::KEY_UID) ||
                  r->get_param(AutoScenarioDef::KEY_UID).empty()))
            return true;

    return false;
}

size_t AutoScenario::expectedRuleCount() const
{
    AutoScenarioDef *def = definition();
    if (!def) return 0;

    //button_start, button_stop, step_end, one per step, and the schedule pair
    size_t n = 3 + def->steps.size();
    if (ioTimeRange) n += cycle? 2: 1;

    return n;
}

Rule *AutoScenario::ruleOfType(const string &type) const
{
    for (Rule *r: scenarioRules())
        if (r && r->get_param("auto_scenario_type") == type)
            return r;

    return nullptr;
}

vector<Rule *> AutoScenario::getRuleSteps() const
{
    vector<Rule *> steps;

    for (Rule *r: scenarioRules())
        if (r && r->get_param("auto_scenario_type") == "step" &&
            is_of_type<int>(r->get_param("auto_scenario_step")))
            steps.push_back(r);

    std::stable_sort(steps.begin(), steps.end(),
                     [](Rule *a, Rule *b) { return stepNumberOf(a) < stepNumberOf(b); });

    return steps;
}

Rule *AutoScenario::stepRule(int s) const
{
    const vector<Rule *> steps = getRuleSteps();

    if (s < 0 || s >= (int)steps.size()) return nullptr;

    return steps[s];
}

void AutoScenario::setCycling(bool c)
{
    if (c == cycle) return;
    cycle = c;

    ioScenario->set_param("cycle", cycle? "true": "false");

    AutoScenarioDef *def = definition();
    if (def) def->cycle = cycle;
}

void AutoScenario::setDisabled(bool d)
{
    if (d == disabled) return;
    disabled = d;

    ioScenario->set_param("disabled", disabled? "true": "false");

    AutoScenarioDef *def = definition();
    if (def) def->enabled = !disabled;
}

/* -----------------------------------------------------------------------
 * T3.18 - the two gates, the manual re-enable, the clean stop
 * -------------------------------------------------------------------- */

bool AutoScenario::isBroken() const
{
    AutoScenarioDef *def = definition();

    if (def)
    {
        for (const AutoScenarioDefStep &step: def->steps)
            for (const AutoScenarioDefAction &a: step.actions)
                if (!a.ioId.empty() && !ListeRoom::Instance().findIO(a.ioId))
                    return true;

        for (const AutoScenarioDefAction &a: def->finalStep.actions)
            if (!a.ioId.empty() && !ListeRoom::Instance().findIO(a.ioId))
                return true;
    }

    for (Rule *rule: scenarioRules())
        if (rule && rule->isDisabled())
            return true;

    //One of our rules was destroyed by a third party. Meaningless before the
    //first build, where owning no rule is the normal state.
    if (rulesGenerated && generatedRules().size() < expectedRuleCount())
        return true;

    return false;
}

string AutoScenario::getMissingIoDescription() const
{
    vector<string> ids;

    auto add = [&ids](const string &id)
    {
        if (id.empty()) return;
        if (std::find(ids.begin(), ids.end(), id) != ids.end()) return;
        ids.push_back(id);
    };

    AutoScenarioDef *def = definition();
    if (def)
    {
        for (const AutoScenarioDefStep &step: def->steps)
            for (const AutoScenarioDefAction &a: step.actions)
                if (!ListeRoom::Instance().findIO(a.ioId)) add(a.ioId);

        for (const AutoScenarioDefAction &a: def->finalStep.actions)
            if (!ListeRoom::Instance().findIO(a.ioId)) add(a.ioId);
    }

    //Rules come second so that a configuration whose rules predate the
    //definition still names what it is missing.
    for (Rule *rule: scenarioRules())
    {
        if (!rule) continue;
        for (const string &id: rule->getMissingIoIds())
            add(id);
    }

    string desc;
    for (const string &id: ids)
    {
        if (!desc.empty()) desc += ", ";
        desc += id;
    }

    return desc;
}

void AutoScenario::setDisabledMissingIo(bool d)
{
    if (d == disabledMissingIo) return;
    disabledMissingIo = d;

    if (!ioScenario) return;

    if (disabledMissingIo)
    {
        ioScenario->set_param("disabled_missing_io", "true");
    }
    else
    {
        /* REMOVED, not set to "false". A healthy scenario must produce exactly
         * the io.xml it always produced - "the healthy case is strictly
         * unchanged" is an invariant of this ticket, and a leftover
         * disabled_missing_io="false" would break it for every scenario that
         * was repaired once.
         */
        ioScenario->del_param("disabled_missing_io");
    }
}

bool AutoScenario::tryReenable(string &errorOut)
{
    if (isBroken())
    {
        /* REFUSED, and the refusal carries the diagnosis. Answering "success"
         * and letting the next detection pass disable the scenario again would
         * reproduce, one level up, the very defect this ticket removes: an
         * action that looks like it worked and did nothing.
         */
        errorOut = "scenario still references missing IOs: " + getMissingIoDescription();

        cWarningDom("scenario") << "AutoScenario (" << scenario_id
                                << "): re-enable REFUSED, " << errorOut;
        return false;
    }

    if (!disabledMissingIo)
    {
        //Nothing to do, and saying so is not an error: re-enabling a scenario
        //that is not disabled is idempotent.
        cInfoDom("scenario") << "AutoScenario (" << scenario_id
                             << "): re-enable is a no-op, it is not disabled";
        return true;
    }

    setDisabledMissingIo(false);

    cInfoDom("scenario") << "AutoScenario (" << scenario_id
                         << "): re-enabled by the user, it can run again";

    EventManager::create(CalaosEvent::EventScenarioChanged,
                         { { "id", ioScenario? ioScenario->get_param("id"): scenario_id } });

    return true;
}

void AutoScenario::stopBrokenRun()
{
    if (!ioIsActive || !ioIsActive->get_value_bool()) return;

    cWarningDom("scenario") << "AutoScenario (" << scenario_id << "): broken while "
                            << "running, forcing a clean stop";

    /* ioIsActive FIRST: with it back to false the step rules and the step_end
     * rule can no longer pass their conditions, so setting ioStep below cannot
     * restart anything. Real set_value() calls and not a silent write, so the
     * UI sees the scenario stop.
     */
    ioIsActive->set_value(false);

    if (ioStep) ioStep->set_value(-1.0);
    if (ioTimer) ioTimer->set_value(string("stop"));
}

void AutoScenario::deleteAll()
{
    cInfoDom("scenario") << "AutoScenario::delete(" << ioScenario->get_param("id") << ")";

    //Every rule of the scenario, legacy ones included: the scenario is being
    //torn down and nothing else will ever collect them.
    destroyRules(true);
    rulesGenerated = false;

    AutoScenarioDef *def = definition();
    if (def) def->clear();

    /* Destroy, not Disable: these ids are deterministic and would be recreated
     * identically, so disabled copies of their rules would be duplicated by the
     * next build with nothing to collect the originals.
     */
    if (ioIsActive)
        ListeRoom::Instance().deleteIO(ioIsActive, false, RuleDetachPolicy::Destroy);
    ioIsActive = NULL;
    if (ioScheduleEnabled)
        ListeRoom::Instance().deleteIO(ioScheduleEnabled, false, RuleDetachPolicy::Destroy);
    ioScheduleEnabled = NULL;
    if (ioStep)
        ListeRoom::Instance().deleteIO(ioStep, false, RuleDetachPolicy::Destroy);
    ioStep = NULL;
    if (ioTimer)
        ListeRoom::Instance().deleteIO(ioTimer, false, RuleDetachPolicy::Destroy);
    ioTimer = NULL;
    if (ioTimeRange)
        ListeRoom::Instance().deleteIO(ioTimeRange, false, RuleDetachPolicy::Destroy);
    ioTimeRange = NULL;
}

void AutoScenario::deleteRules()
{
    cInfoDom("scenario") << "AutoScenario::deleteRules(" << ioScenario->get_param("id") << ")";

    AutoScenarioDef *def = definition();
    if (def)
    {
        def->steps.clear();
        def->finalStep = AutoScenarioDefStep();
    }

    rebuildRules(true);
}

IOBase *AutoScenario::createInput(string type, string id)
{
    IOBase *in = ListeRoom::Instance().get_io(id);
    if (!in)
    {
        Params params;
        params.Add("type", type);
        params.Add("name", id);
        params.Add("rw", "true");
        params.Add("visible", "false");
        params.Add("save", "false");
        params.Add("id", id);
        params.Add("auto_scenario", scenario_id);

        in = ListeRoom::Instance().createIO(params, roomContainer);
    }

    //createIO() returns null on an IO factory miss or when no room can hold
    //the IO: propagate the null, the caller has to abort cleanly
    if (!in)
        cErrorDom("scenario") << "createInput(" << type << ", " << id
                              << "): creation failed";

    return in;
}

void AutoScenario::addRuleCondition(Rule *rule, IOBase *input, string oper, string value)
{
    ConditionStd *cond = new ConditionStd();
    rule->AddCondition(cond);

    cond->Add(input);
    cond->get_operator().Add(input->get_param("id"), oper);
    cond->get_params().Add(input->get_param("id"), value);
}

void AutoScenario::addRuleAction(Rule *rule, IOBase *output, string value)
{
    ActionStd *act = new ActionStd();
    rule->AddAction(act);

    act->Add(output);
    act->get_params().Add(output->get_param("id"), value);
}

void AutoScenario::addRuleActionById(Rule *rule, const string &ioId, const string &value)
{
    if (!rule || ioId.empty()) return;

    ActionStd *act = new ActionStd();
    rule->AddAction(act);

    act->Add(ioId);
    act->get_params().Add(ioId, value);

    /* AddAction() only collects the unresolved ids the XML loader recorded, so
     * a rule built in memory has to say it itself, or the engine would run an
     * action list that is silently one action short.
     */
    if (!ListeRoom::Instance().findIO(ioId))
        rule->markIoMissing(ioId);
}

string AutoScenario::actionValueOn(Rule *rule, const string &ioId)
{
    if (!rule) return string();

    for (int i = 0;i < rule->get_size_actions();i++)
    {
        ActionStd *act = dynamic_cast<ActionStd *>(rule->get_action(i));
        if (!act) continue;
        if (act->get_size() != 1) continue;
        if (act->get_output_id(0) != ioId) continue;

        return act->get_params().get_param(ioId);
    }

    return string();
}

Rule *AutoScenario::newGeneratedRule(const string &name, const string &autoScenarioType)
{
    Rule *rule = new Rule("AutoScenario", name);
    rule->set_param("auto_scenario", scenario_id);
    rule->set_param(AutoScenarioDef::KEY_UID, getScenarioUid());
    rule->set_param("auto_scenario_type", autoScenarioType);
    ListeRule::Instance().Add(rule);

    return rule;
}

void AutoScenario::destroyRules(bool all)
{
    const string uid = getScenarioUid();

    //Snapshot first: Remove() mutates the very index the lookup walks.
    vector<Rule *> doomed;
    for (Rule *r: all? scenarioRules(): generatedRules())
        if (r && (all || r->get_param(AutoScenarioDef::KEY_UID) == uid))
            doomed.push_back(r);

    for (Rule *r: doomed)
        ListeRule::Instance().Remove(r);
}

void AutoScenario::declareDefinition()
{
    AutoScenarioDef *def = definition();
    if (!def) return;

    if (def->uid.empty()) def->uid = AutoScenarioDef::newUid();

    def->cycle = cycle;
    def->enabled = !disabled;
    def->scheduleIoId = ioTimeRange? ioTimeRange->get_param("id"): string();
}

bool AutoScenario::prepareInternalIos()
{
    roomContainer = ListeRoom::Instance().getRoomByIO(ioScenario);

    ioIsActive = dynamic_cast<Internal *>(createInput("InternalBool", scenario_id + "_is_active"));
    ioStep = dynamic_cast<Internal *>(createInput("InternalInt", scenario_id + "_step"));
    ioTimer = dynamic_cast<InputTimer *>(createInput("InputTimer", scenario_id + "_timer"));

    if (!ioIsActive || !ioStep || !ioTimer)
    {
        cErrorDom("scenario") << "AutoScenario (" << scenario_id
                              << "): unable to create the internal scenario IOs, "
                              << "aborting rules creation";
        return false;
    }

    ioTimeRange = dynamic_cast<InPlageHoraire *>(
                ListeRoom::Instance().get_io(scenario_id + "_schedule"));
    ioScheduleEnabled = nullptr;

    if (!ioTimeRange) return true;

    ioScheduleEnabled = dynamic_cast<Internal *>(
                createInput("InternalBool", scenario_id + "_is_schedule_enabled"));
    if (!ioScheduleEnabled)
    {
        cErrorDom("scenario") << "AutoScenario (" << scenario_id
                              << "): unable to create the schedule enable IO, "
                              << "aborting rules creation";
        return false;
    }

    ioScheduleEnabled->set_value(!disabled); // scenario scheduling is enabled by default
    ioScheduleEnabled->set_param("save", "true"); //Save the value on disk

    return true;
}

bool AutoScenario::rebuildRules(bool takeOwnership)
{
    //Before any destruction: a refused build must leave everything as it was.
    if (!prepareInternalIos()) return false;

    if (!takeOwnership && hasLegacyRules())
    {
        cInfoDom("scenario") << "AutoScenario (" << scenario_id << "): rules predate "
                             << "the definition, leaving them untouched";
        return true;
    }

    if (!ioTimeRange)
    {
        /* Destroy, not Disable: addSchedule() rebuilds the very same id, so a
         * disabled copy of the schedule rules would be duplicated by the next
         * build.
         */
        Internal *stale = dynamic_cast<Internal *>(
                    ListeRoom::Instance().get_io(scenario_id + "_is_schedule_enabled"));
        if (stale)
            ListeRoom::Instance().deleteIO(stale, false, RuleDetachPolicy::Destroy);
    }

    declareDefinition();
    destroyRules(takeOwnership);

    AutoScenarioDef *def = definition();
    if (!def) return false;

    Rule *rule = newGeneratedRule(scenario_id + "_button_start", "button_start");
    addRuleCondition(rule, ioScenario, "==", "true");
    addRuleCondition(rule, ioIsActive, "==", "false");
    addRuleAction(rule, ioScenario, "false");
    addRuleAction(rule, ioIsActive, "true");
    addRuleAction(rule, ioStep, "0");
    addRuleAction(rule, ioTimer, "0");
    addRuleAction(rule, ioTimer, "start");

    rule = newGeneratedRule(scenario_id + "_button_stop", "button_stop");
    addRuleCondition(rule, ioScenario, "==", "true");
    addRuleCondition(rule, ioIsActive, "==", "true");
    addRuleAction(rule, ioScenario, "false");
    addRuleAction(rule, ioStep, "-1");
    addRuleAction(rule, ioTimer, "0");
    addRuleAction(rule, ioTimer, "start");

    rule = newGeneratedRule(scenario_id + "_step_end", "step_end");
    addRuleCondition(rule, ioIsActive, "==", "true");
    addRuleCondition(rule, ioStep, "==", "-1");
    addRuleCondition(rule, ioTimer, "==", "true");
    addRuleAction(rule, ioIsActive, "false");
    for (const AutoScenarioDefAction &a: def->finalStep.actions)
        addRuleActionById(rule, a.ioId, a.value);

    for (size_t i = 0;i < def->steps.size();i++)
    {
        const AutoScenarioDefStep &step = def->steps[i];

        rule = newGeneratedRule(scenario_id + "_step", "step");
        rule->set_param("auto_scenario_step", Utils::to_string(i));

        addRuleCondition(rule, ioIsActive, "==", "true");
        addRuleCondition(rule, ioStep, "==", Utils::to_string(i));
        addRuleCondition(rule, ioTimer, "==", "true");

        //The chaining. -1 hands over to the step_end rule; a cycling scenario
        //loops back to 0 instead.
        string next;
        if (i + 1 < def->steps.size()) next = Utils::to_string(i + 1);
        else next = cycle? "0": "-1";

        addRuleAction(rule, ioStep, next);
        addRuleAction(rule, ioTimer, Utils::to_string(step.pause));
        addRuleAction(rule, ioTimer, "start");

        for (const AutoScenarioDefAction &a: step.actions)
            addRuleActionById(rule, a.ioId, a.value);
    }

    if (ioTimeRange)
    {
        rule = newGeneratedRule(scenario_id + "_time_start", "time_start");
        addRuleCondition(rule, ioIsActive, "==", "false");
        addRuleCondition(rule, ioScheduleEnabled, "==", "true");
        addRuleCondition(rule, ioTimeRange, "==", "true");
        addRuleAction(rule, ioScenario, "true");

        if (cycle)
        {
            rule = newGeneratedRule(scenario_id + "_time_stop", "time_stop");
            addRuleCondition(rule, ioIsActive, "==", "true");
            addRuleCondition(rule, ioScheduleEnabled, "==", "true");
            addRuleCondition(rule, ioTimeRange, "==", "false");
            addRuleAction(rule, ioStep, "-1");
            addRuleAction(rule, ioTimer, "0");
            addRuleAction(rule, ioTimer, "start");
        }
    }

    rulesGenerated = true;

    return true;
}

void AutoScenario::addStep(double pause)
{
    AutoScenarioDef *def = definition();
    if (!def) return;

    AutoScenarioDefStep step;
    step.stepId = AutoScenarioDef::newStepId();
    step.pause = pause;
    def->steps.push_back(step);

    rebuildRules(true);
}

void AutoScenario::setStepPause(int s, double pause)
{
    AutoScenarioDef *def = definition();
    if (!def || s < 0 || s >= (int)def->steps.size()) return;

    def->steps[s].pause = pause;

    rebuildRules(true);
}

void AutoScenario::addStepAction(int s, IOBase *out, string action)
{
    AutoScenarioDef *def = definition();
    if (!def || !out) return;
    if (s < 0 || s >= (int)def->steps.size()) return;

    AutoScenarioDefAction a;
    a.ioId = out->get_param("id");
    a.value = action;
    def->steps[s].actions.push_back(a);

    rebuildRules(true);
}

double AutoScenario::getStepPause(int s)
{
    Rule *step = stepRule(s);
    if (!step || !ioTimer) return 0.0;

    double pause;
    from_string(actionValueOn(step, ioTimer->get_param("id")), pause);

    return pause;
}

bool AutoScenario::isScenarioInternalIO(IOBase *io)
{
    //A null IO is skipped like an internal one, it can never be a user action
    if (!io) return true;

    return io == ioStep ||
           io == ioTimer ||
           io == ioIsActive ||
           io == ioScenario ||
           io == ioScheduleEnabled;
}

/* Count the "real" (user visible) actions of a rule: the ones that are not
 * part of the scenario machinery. Counting instead of subtracting a fixed
 * number is what keeps countRealActions() and getRealAction() in sync: a rule
 * whose user action happens to target one of the scenario IOs used to be
 * counted here but skipped there, and getRealAction() then returned an empty
 * ScenarioAction whose null `io` was dereferenced by the callers.
 */
int AutoScenario::countRealActions(Rule *rule)
{
    if (!rule) return 0;

    int cpt = 0;
    for (int i = 0;i < rule->get_size_actions();i++)
    {
        ActionStd *act = dynamic_cast<ActionStd *>(rule->get_action(i));
        if (!act) continue;
        if (act->get_size() != 1) continue;
        if (isScenarioInternalIO(act->get_output(0))) continue;

        cpt++;
    }

    return cpt;
}

ScenarioAction AutoScenario::getRealAction(Rule *rule, int action)
{
    ScenarioAction sa;

    if (!rule) return sa;

    int cpt = 0;
    for (int i = 0;i < rule->get_size_actions();i++)
    {
        ActionStd *act = dynamic_cast<ActionStd *>(rule->get_action(i));
        if (!act) continue;
        if (act->get_size() != 1) continue;
        if (isScenarioInternalIO(act->get_output(0))) continue;

        if (cpt == action)
        {
            sa.io = act->get_output(0);
            sa.action = act->get_params().get_param(sa.io->get_param("id"));

            return sa;
        }
        cpt++;
    }

    return sa;
}

int AutoScenario::getStepActionCount(int s)
{
    //countRealActions()/getRealAction() answer 0/an empty action for a null
    //rule, which is what an out of range or destroyed step is
    return countRealActions(stepRule(s));
}

ScenarioAction AutoScenario::getStepAction(int s, int action)
{
    return getRealAction(stepRule(s), action);
}

int AutoScenario::getEndStepActionCount()
{
    return countRealActions(getRuleStepEnd());
}

struct SCCategory
{
    int count;
    int type;
};

static bool _sortDesc (SCCategory i, SCCategory j)
{
    return (i.count > j.count);
}

string AutoScenario::getCategory()
{
    struct SCCategory catLight = {0, 0};
    struct SCCategory catShutter = {0, 1};
    struct SCCategory catOther = {0, 2};

    const size_t stepCount = getRuleSteps().size();

    for (uint i = 0;i < stepCount;i++)
    {
        for (int j = 0;j < getStepActionCount(i);j++)
        {
            ScenarioAction sa = getStepAction(i, j);

            //Defensive: getStepAction() returns an empty action when the index
            //does not resolve, there is nothing to categorize then
            if (!sa.io) continue;

            if (sa.io->get_param("gui_type") == "light" ||
                sa.io->get_param("gui_type") == "light_dimmer" ||
                sa.io->get_param("gui_type") == "light_rgb")
            {
                catLight.count++;
            }
            else if (sa.io->get_param("type") == "shutter" ||
                     sa.io->get_param("type") == "shutter_smart")
            {
                catShutter.count++;
            }
            else
            {
                catOther.count++;
            }
        }
    }

    vector<struct SCCategory> v;
    if (catLight.count) v.push_back(catLight);
    if (catShutter.count) v.push_back(catShutter);
    if (catOther.count) v.push_back(catOther);

    sort(v.begin(), v.end(), _sortDesc);

    string cat;
    for (uint i = 0;i < v.size();i++)
    {
        if (v[i].type == 0) cat += "light";
        else if (v[i].type == 1) cat += "shutter";
        else if (v[i].type == 2) cat += "other";
        if (i < v.size() - 1) cat += '-';
    }

    return cat;
}

void AutoScenario::addSchedule()
{
    if (ioTimeRange) return;

    ioTimeRange = dynamic_cast<InPlageHoraire *>(createInput("InPlageHoraire", scenario_id + "_schedule"));

    rebuildRules(true);
}

void AutoScenario::deleteSchedule()
{
    if (ioTimeRange)
    {
        /* Destroy, not Disable: addSchedule() rebuilds the very same id, so
         * disabled copies of the schedule rules would be duplicated by it.
         */
        ListeRoom::Instance().deleteIO(ioTimeRange, false, RuleDetachPolicy::Destroy);
    }
    ioTimeRange = nullptr;

    rebuildRules(true);
}
