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
#include "AutoScenario.h"
using namespace Calaos;

static bool _sortCompStepRule(const RuleRef &s1, const RuleRef &s2)
{
    Rule *r1 = s1.get();
    Rule *r2 = s2.get();

    //Dead entries sort last; purgeDeadSteps() runs before every sort, so this
    //only exists to keep the ordering strict and weak in every case
    if (!r1) return false;
    if (!r2) return true;

    int t1, t2;
    from_string(r1->get_param("auto_scenario_step"), t1);
    from_string(r2->get_param("auto_scenario_step"), t2);
    return (t1 < t2);
}

/* Mutates ruleSteps from inside the read accessors (stepRule() ->
 * getStepActionCount()/getStepAction()), and those are called by getCategory()
 * and Scenario::toJson() WHILE they iterate over the indexes of that very
 * vector. It is safe only because nothing in those loops can destroy a Rule:
 * they read, they never touch ListeRule nor delete an IO, so after the first
 * purge every later one is a no-op and no index shifts under the loop. A
 * future caller that can destroy a rule mid-iteration breaks that, and must
 * snapshot getRuleSteps() once instead of re-indexing.
 */
void AutoScenario::purgeDeadSteps()
{
    ruleSteps.erase(std::remove_if(ruleSteps.begin(), ruleSteps.end(),
                                   [](const RuleRef &s) { return s.get() == nullptr; }),
                    ruleSteps.end());
}

Rule *AutoScenario::stepRule(int s)
{
    purgeDeadSteps();

    if (s < 0 || s >= (int)ruleSteps.size()) return nullptr;

    return ruleSteps[s].get();
}

vector<Rule *> AutoScenario::getRuleSteps()
{
    purgeDeadSteps();

    vector<Rule *> ret;
    ret.reserve(ruleSteps.size());
    for (uint i = 0;i < ruleSteps.size();i++)
        ret.push_back(ruleSteps[i].get());

    return ret;
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

    ListeRoom::Instance().addScenarioCache(ioScenario);
}

AutoScenario::~AutoScenario()
{
    /* IOBase::ascenario is set on the _schedule IO only (checkScenarioRules())
     * and used to be left pointing here for ever: the schedule IO is a plain
     * IO of the room, it outlives the scenario whose IO was deleted, and
     * JsonApi reads getAutoScenarioPtr() on every IO it serializes.
     *
     * The IO is re-resolved through io_table instead of through ioTimeRange:
     * ~Room destroys its IOs in list order and nothing guarantees the schedule
     * IO is not already gone when the Scenario (hence this) is destroyed.
     * IOBase::~IOBase() keeps io_table up to date, so a null answer here means
     * "already destroyed, nothing to clean".
     */
    IOBase *schedule = ListeRoom::Instance().get_io(scenario_id + "_schedule");
    if (schedule && schedule->getAutoScenarioPtr() == this)
        schedule->setAutoScenarioPtr(nullptr);

    ListeRoom::Instance().delScenarioCache(ioScenario);
}

void AutoScenario::setCycling(bool c)
{
    if (c == cycle) return;
    cycle = c;

    if (cycle)
        ioScenario->set_param("cycle", "true");
    else
        ioScenario->set_param("cycle", "false");
}

void AutoScenario::setDisabled(bool d)
{
    if (d == disabled) return;
    disabled = d;

    if (disabled)
        ioScenario->set_param("disabled", "true");
    else
        ioScenario->set_param("disabled", "false");
}

void AutoScenario::deleteAll()
{
    cInfoDom("scenario") << "AutoScenario::delete(" << ioScenario->get_param("id") << ")";

    //delete rules. Remove(nullptr) is a no-op, and a rule ListeRule already
    //destroyed under us resolves to null: same order, same removals as before.
    ListeRule::Instance().Remove(ruleStart.get());
    ruleStart.reset();
    ListeRule::Instance().Remove(ruleStop.get());
    ruleStop.reset();
    ListeRule::Instance().Remove(ruleStepEnd.get());
    ruleStepEnd.reset();
    ListeRule::Instance().Remove(rulePlageStart.get());
    rulePlageStart.reset();
    ListeRule::Instance().Remove(rulePlageStop.get());
    rulePlageStop.reset();

    for (uint i = 0;i < ruleSteps.size();i++)
        ListeRule::Instance().Remove(ruleSteps[i].get());
    ruleSteps.clear();

    //delete IOs
    if (ioIsActive)
        ListeRoom::Instance().deleteIO(ioIsActive);
    ioIsActive = NULL;
    if (ioScheduleEnabled)
        ListeRoom::Instance().deleteIO(ioScheduleEnabled);
    ioScheduleEnabled = NULL;
    if (ioStep)
        ListeRoom::Instance().deleteIO(ioStep);
    ioStep = NULL;
    if (ioTimer)
        ListeRoom::Instance().deleteIO(ioTimer);
    ioTimer = NULL;
    if (ioTimeRange)
    {
        //Symmetric with checkScenarioRules(): drop the back-pointer before the
        //IO it lives on is destroyed
        if (ioTimeRange->getAutoScenarioPtr() == this)
            ioTimeRange->setAutoScenarioPtr(nullptr);
        ListeRoom::Instance().deleteIO(ioTimeRange);
    }
    ioTimeRange = NULL;
}

void AutoScenario::deleteRules()
{
    cInfoDom("scenario") << "AutoScenario::deleteRules(" << ioScenario->get_param("id") << ")";

    //delete rules
    ListeRule::Instance().Remove(ruleStepEnd.get());
    ruleStepEnd.reset();

    //recreate empty step rule
    createRuleStepEnd();

    for (uint i = 0;i < ruleSteps.size();i++)
        ListeRule::Instance().Remove(ruleSteps[i].get());
    ruleSteps.clear();
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
    if (in)
        in->setAutoScenario(true);
    else
        cErrorDom("scenario") << "createInput(" << type << ", " << id
                              << "): creation failed";

    return in;
}

bool AutoScenario::checkCondition(Rule *rule, IOBase *input, string oper, string value)
{
    bool ret = false;
    for (int i = 0;i < rule->get_size_conds() && !ret;i++)
    {
        ConditionStd *cond = dynamic_cast<ConditionStd *>(rule->get_condition(i));
        if (!cond) continue;
        if (cond->get_size() != 1) continue;
        if (cond->get_input(0) != input) continue;
        if (cond->get_operator().get_param(input->get_param("id")) != oper) continue;
        if (cond->get_params().get_param(input->get_param("id")) != value) continue;

        ret = true;
    }

    return ret;
}

bool AutoScenario::checkAction(Rule *rule, IOBase *output, string value)
{
    bool ret = false;
    for (int i = 0;i < rule->get_size_actions() && !ret;i++)
    {
        ActionStd *act = dynamic_cast<ActionStd *>(rule->get_action(i));
        if (!act) continue;
        if (act->get_size() != 1) continue;
        if (act->get_output(0) != output) continue;
        if (act->get_params().get_param(output->get_param("id")) != value) continue;

        ret = true;
    }

    return ret;
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

void AutoScenario::setRuleCondition(Rule *rule, IOBase *input, string oper, string value)
{
    for (int i = 0;i < rule->get_size_conds();i++)
    {
        ConditionStd *cond = dynamic_cast<ConditionStd *>(rule->get_condition(i));
        if (!cond) continue;
        if (cond->get_size() != 1) continue;
        if (cond->get_input(0) != input) continue;
        cond->get_operator().Add(input->get_param("id"), oper);
        cond->get_params().Add(input->get_param("id"), value);

        break;
    }
}

void AutoScenario::setRuleAction(Rule *rule, IOBase *output, string value)
{
    for (int i = 0;i < rule->get_size_actions();i++)
    {
        ActionStd *act = dynamic_cast<ActionStd *>(rule->get_action(i));
        if (!act) continue;
        if (act->get_size() != 1) continue;
        if (act->get_output(0) != output) continue;
        act->get_params().Add(output->get_param("id"), value);

        break;
    }
}

string AutoScenario::getRuleConditionValue(Rule *rule, IOBase *input, string oper)
{
    string ret;

    for (int i = 0;i < rule->get_size_conds();i++)
    {
        ConditionStd *cond = dynamic_cast<ConditionStd *>(rule->get_condition(i));
        if (!cond) continue;
        if (cond->get_size() != 1) continue;
        if (cond->get_input(0) != input) continue;
        cond->get_operator().Add(input->get_param("id"), oper);

        ret = cond->get_params().get_param(input->get_param("id"));

        break;
    }

    return ret;
}

string AutoScenario::getRuleActionValue(Rule *rule, IOBase *output)
{
    string ret;

    for (int i = 0;i < rule->get_size_actions();i++)
    {
        ActionStd *act = dynamic_cast<ActionStd *>(rule->get_action(i));
        if (!act) continue;
        if (act->get_size() != 1) continue;
        if (act->get_output(0) != output) continue;
        ret = act->get_params().get_param(output->get_param("id"));

        break;
    }

    return ret;
}

bool AutoScenario::checkScenarioRules()
{
    /* get/create needed IOs for rules */

    //clear everything
    ruleStart.reset();
    ruleStop.reset();
    ruleStepEnd.reset();
    rulePlageStart.reset();
    rulePlageStop.reset();
    ruleSteps.clear();
    ioIsActive = NULL;
    ioScheduleEnabled = NULL;
    ioStep = NULL;
    ioTimer = NULL;
    ioTimeRange = NULL;

    roomContainer = ListeRoom::Instance().getRoomByIO(ioScenario);

    if (!ioIsActive)
        ioIsActive = dynamic_cast<Internal *>(createInput("InternalBool", scenario_id + "_is_active"));
    if (!ioStep)
        ioStep = dynamic_cast<Internal *>(createInput("InternalInt", scenario_id + "_step"));
    if (!ioTimer)
        ioTimer = dynamic_cast<InputTimer *>(createInput("InputTimer", scenario_id + "_timer"));

    //Every rule below dereferences these three IOs: without them the rules
    //cannot be built, abort instead of crashing. createInput() returns null
    //on a factory/room miss, and the dynamic_cast rejects an existing IO of
    //the wrong type using one of the internal ids.
    if (!ioIsActive || !ioStep || !ioTimer)
    {
        cErrorDom("scenario") << "AutoScenario (" << scenario_id
                              << "): unable to create the internal scenario IOs, "
                              << "aborting rules creation";
        return false;
    }

    //Get the PlageHoraire input if the scenario is scheduled
    ioTimeRange = dynamic_cast<InPlageHoraire *>(ListeRoom::Instance().get_io(scenario_id + "_schedule"));
    if (ioTimeRange)
    {
        ioTimeRange->setAutoScenarioPtr(this);

        if (!ioScheduleEnabled)
            ioScheduleEnabled = dynamic_cast<Internal *>(createInput("InternalBool", scenario_id + "_is_schedule_enabled"));

        if (!ioScheduleEnabled)
        {
            cErrorDom("scenario") << "AutoScenario (" << scenario_id
                                  << "): unable to create the schedule enable IO, "
                                  << "aborting rules creation";
            return false;
        }

        ioScheduleEnabled->set_value(!disabled); // scenario scheduling is enabled by default
        ioScheduleEnabled->set_param("save", "true"); //Save the value on disk
    }
    else
    {
        //ioScheduleEnabled is not needed if the scenario has no schedule, so
        //delete it if it exists
        //It will automatically delete all rules using this input
        ioScheduleEnabled = dynamic_cast<Internal *>(ListeRoom::Instance().get_io(scenario_id + "_is_schedule_enabled"));
        if (ioScheduleEnabled)
        {
            ListeRoom::Instance().deleteIO(ioScheduleEnabled);
            ioScheduleEnabled = nullptr;
        }
    }

    /* search needed rules for scenario */

    list<Rule *> srules = ListeRule::Instance().getRuleAutoScenario(scenario_id);
    list<Rule *>::iterator it = srules.begin();

    for (;it != srules.end();it++)
    {
        Rule *rule = *it;

        if (rule->get_param("auto_scenario_type") == "button_start")
        {
            if (!checkCondition(rule, ioScenario, "==", "true")) continue;
            if (!checkCondition(rule, ioIsActive, "==", "false")) continue;

            if (!checkAction(rule, ioScenario, "false")) continue;
            if (!checkAction(rule, ioIsActive, "true")) continue;
            if (!checkAction(rule, ioStep, "0")) continue;
            if (!checkAction(rule, ioTimer, "0")) continue;
            if (!checkAction(rule, ioTimer, "start")) continue;

            ruleStart = rule;
            rule->setAutoScenario(true);
        }
        else if (rule->get_param("auto_scenario_type") == "button_stop")
        {
            if (!checkCondition(rule, ioScenario, "==", "true")) continue;
            if (!checkCondition(rule, ioIsActive, "==", "true")) continue;

            if (!checkAction(rule, ioScenario, "false")) continue;
            if (!checkAction(rule, ioStep, "-1")) continue;
            if (!checkAction(rule, ioTimer, "0")) continue;
            if (!checkAction(rule, ioTimer, "start")) continue;

            ruleStop = rule;
            rule->setAutoScenario(true);
        }
        else if (rule->get_param("auto_scenario_type") == "step_end")
        {
            if (!checkCondition(rule, ioIsActive, "==", "true")) continue;
            if (!checkCondition(rule, ioStep, "==", "-1")) continue;
            if (!checkCondition(rule, ioTimer, "==", "true")) continue;

            if (!checkAction(rule, ioIsActive, "false")) continue;

            ruleStepEnd = rule;
            rule->setAutoScenario(true);
        }
        else if (rule->get_param("auto_scenario_type") == "step" &&
                 is_of_type<int>(rule->get_param("auto_scenario_step")))
        {
            if (!checkCondition(rule, ioIsActive, "==", "true")) continue;
            if (!checkCondition(rule, ioTimer, "==", "true")) continue;

            if (!checkAction(rule, ioTimer, "start")) continue;

            ruleSteps.push_back(rule);
            rule->setAutoScenario(true);
        }
        else if (ioTimeRange && rule->get_param("auto_scenario_type") == "time_start")
        {
            if (!checkCondition(rule, ioIsActive, "==", "false")) continue;
            if (!checkCondition(rule, ioScheduleEnabled, "==", "true")) continue;
            if (!checkCondition(rule, ioTimeRange, "==", "true")) continue;

            if (!checkAction(rule, ioScenario, "true")) continue;

            rulePlageStart = rule;
            rule->setAutoScenario(true);
        }
        else if (ioTimeRange && cycle && rule->get_param("auto_scenario_type") == "time_stop")
        {
            if (!checkCondition(rule, ioIsActive, "==", "true")) continue;
            if (!checkCondition(rule, ioScheduleEnabled, "==", "true")) continue;
            if (!checkCondition(rule, ioTimeRange, "==", "false")) continue;

            if (!checkAction(rule, ioTimer, "0")) continue;
            if (!checkAction(rule, ioTimer, "start")) continue;
            if (!checkAction(rule, ioStep, "-1")) continue;

            rulePlageStop = rule;
            rule->setAutoScenario(true);
        }
    }

    cDebugDom("scenario") << "AutoScenario Check: " << ioScenario->get_param("id") <<
                             ", scenario_id: " << scenario_id <<
                             ", " << srules.size() << " rules checked";
    cDebugDom("scenario") << "Found " << ruleSteps.size() << " steps, " <<
                             ((ioTimeRange)?"has schedule, ":"has no schedule, ") <<
                             ((disabled)?"schedule is disabled ":"schedule is enabled ") <<
                             ((cycle)?"cycling":"");
    cDebugDom("scenario") << "Found rules (" <<
                             "ruleStart:" << ((ruleStart)?"yes":"no") << " - " <<
                             "ruleStop:" << ((ruleStop)?"yes":"no") << " - " <<
                             "rulePlageStart:" << ((rulePlageStart)?"yes":"no") << " - " <<
                             "rulePlageStop:" << ((rulePlageStop)?"yes":"no") << " - " <<
                             "ruleStepEnd:" << ((ruleStepEnd)?"yes":"no") <<
                             ")";

    //Create missing rules

    //_button_start and _button_stop rules are only needed if the scenario is visible

    if (!ruleStart)
    {
        Rule *rule = new Rule("AutoScenario", scenario_id + "_button_start");
        rule->set_param("auto_scenario", scenario_id);
        rule->set_param("auto_scenario_type", "button_start");
        rule->setAutoScenario(true);
        ListeRule::Instance().Add(rule);

        addRuleCondition(rule, ioScenario, "==", "true");
        addRuleCondition(rule, ioIsActive, "==", "false");
        addRuleAction(rule, ioScenario, "false");
        addRuleAction(rule, ioIsActive, "true");
        addRuleAction(rule, ioStep, "0");
        addRuleAction(rule, ioTimer, "0");
        addRuleAction(rule, ioTimer, "start");

        ruleStart = rule;
    }

    if (!ruleStop)
    {
        Rule *rule = new Rule("AutoScenario", scenario_id + "_button_stop");
        rule->set_param("auto_scenario", scenario_id);
        rule->set_param("auto_scenario_type", "button_stop");
        rule->setAutoScenario(true);
        ListeRule::Instance().Add(rule);

        addRuleCondition(rule, ioScenario, "==", "true");
        addRuleCondition(rule, ioIsActive, "==", "true");
        addRuleAction(rule, ioScenario, "false");
        addRuleAction(rule, ioStep, "-1");
        addRuleAction(rule, ioTimer, "0");
        addRuleAction(rule, ioTimer, "start");

        ruleStop = rule;
    }

    createRuleStepEnd();

    if (ioTimeRange && !rulePlageStart)
    {
        Rule *rule = new Rule("AutoScenario", scenario_id + "_time_start");
        rule->set_param("auto_scenario", scenario_id);
        rule->set_param("auto_scenario_type", "time_start");
        rule->setAutoScenario(true);
        ListeRule::Instance().Add(rule);

        addRuleCondition(rule, ioIsActive, "==", "false");
        addRuleCondition(rule, ioScheduleEnabled, "==", "true");
        addRuleCondition(rule, ioTimeRange, "==", "true");
        addRuleAction(rule, ioScenario, "true");

        rulePlageStart = rule;
    }

    if (ioTimeRange && cycle && !rulePlageStop)
    {
        Rule *rule = new Rule("AutoScenario", scenario_id + "_time_stop");
        rule->set_param("auto_scenario", scenario_id);
        rule->set_param("auto_scenario_type", "time_stop");
        rule->setAutoScenario(true);
        ListeRule::Instance().Add(rule);

        addRuleCondition(rule, ioIsActive, "==", "true");
        addRuleCondition(rule, ioScheduleEnabled, "==", "true");
        addRuleCondition(rule, ioTimeRange, "==", "false");
        addRuleAction(rule, ioStep, "-1");
        addRuleAction(rule, ioTimer, "0");
        addRuleAction(rule, ioTimer, "start");

        rulePlageStop = rule;
    }

    //Check steps rules, if they are correctly chained and if the last one is calling the final endStep
    std::sort(ruleSteps.begin(), ruleSteps.end(), _sortCompStepRule);

    for (uint i = 0;i < ruleSteps.size();i++)
    {
        Rule *rule = ruleSteps[i].get();
        if (!rule) continue;
        setRuleCondition(rule, ioStep, "==", Utils::to_string(i));
        if (i + 1 >= ruleSteps.size())
        {
            if (cycle)
                setRuleAction(rule, ioStep, "0");
            else
                setRuleAction(rule, ioStep, "-1");
        }
        else
        {
            setRuleAction(rule, ioStep, Utils::to_string(i + 1));
        }
    }

    return true;
}

void AutoScenario::addStep(double pause)
{
    /* Defence in depth, and NOT the equivalent of checkScenarioRules():
     * that one RENUMBERS every surviving step (see the chaining loop at the
     * end of it), addStep() only numbers the new one. On a holed list - a step
     * whose rule died with no checkScenarioRules() run behind it - the new
     * rule would take auto_scenario_step/ioStep == ruleSteps.size() while a
     * survivor already carries that number, and the setRuleAction(last,
     * ioStep, size()) below would then make the previous step re-trigger
     * itself. Purging the hole does NOT fix that: only a renumbering does.
     *
     * It cannot happen today: the two production callers (JsonApi.cpp:1680
     * and :1773) call addStep() right after deleteRules() or on a brand new
     * scenario, so the list is never holed here and this purge never has
     * anything to remove. It is kept so that a future caller breaking that
     * sequence does not silently build the collision - and such a caller
     * would still have to run checkScenarioRules() to renumber.
     */
    purgeDeadSteps();

    int step = ruleSteps.size();

    Rule *rule = new Rule("AutoScenario", scenario_id + "_step");
    rule->set_param("auto_scenario", scenario_id);
    rule->set_param("auto_scenario_type", "step");
    rule->set_param("auto_scenario_step", Utils::to_string(step));
    rule->setAutoScenario(true);

    addRuleCondition(rule, ioIsActive, "==", "true");
    addRuleCondition(rule, ioStep, "==", Utils::to_string(step));
    addRuleCondition(rule, ioTimer, "==", "true");
    addRuleAction(rule, ioStep, "-1");
    addRuleAction(rule, ioTimer, Utils::to_string(pause));
    addRuleAction(rule, ioTimer, "start");

    //Correctly chain the last rule
    if (ruleSteps.size() > 0)
    {
        Rule *last = (ruleSteps.end() - 1)->get();

        if (last)
            setRuleAction(last, ioStep, Utils::to_string(ruleSteps.size()));
    }

    ListeRule::Instance().Add(rule);

    ruleSteps.push_back(rule);
}

void AutoScenario::setStepPause(int s, double pause)
{
    Rule *step = stepRule(s);
    if (!step) return;

    setRuleAction(step, ioTimer, Utils::to_string(pause));
}

void AutoScenario::addStepAction(int s, IOBase *out, string action)
{
    purgeDeadSteps();
    cDebugDom("scenario") << "s == " << s << " ruleSteps.size() == " << ruleSteps.size();

    Rule *step;
    if (s == END_STEP)
        step = ruleStepEnd.get();
    else
        step = stepRule(s);

    //Out of range, or the rule was destroyed by ListeRule under us
    if (!step) return;

    addRuleAction(step, out, action);
}

double AutoScenario::getStepPause(int s)
{
    Rule *step = stepRule(s);
    if (!step) return 0.0;

    double pause;
    from_string(getRuleActionValue(step, ioTimer), pause);

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
    //rule, which is what an out of range or destroyed step is now
    return countRealActions(stepRule(s));
}

ScenarioAction AutoScenario::getStepAction(int s, int action)
{
    return getRealAction(stepRule(s), action);
}

int AutoScenario::getEndStepActionCount()
{
    return countRealActions(ruleStepEnd.get());
}

ScenarioAction AutoScenario::getEndStepAction(int action)
{
    return getRealAction(ruleStepEnd.get(), action);
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

    purgeDeadSteps();

    for (uint i = 0;i < ruleSteps.size();i++)
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

    checkScenarioRules();
}

void AutoScenario::deleteSchedule()
{
    if (ioTimeRange)
    {
        //Symmetric with the setAutoScenarioPtr(this) of checkScenarioRules()
        if (ioTimeRange->getAutoScenarioPtr() == this)
            ioTimeRange->setAutoScenarioPtr(nullptr);
        ListeRoom::Instance().deleteIO(ioTimeRange);
    }
    ioTimeRange = nullptr;

    checkScenarioRules();
}

void AutoScenario::createRuleStepEnd()
{
    if (!ruleStepEnd)
    {
        //The internal IOs are null until checkScenarioRules() succeeded once:
        //a rule cannot be built over them yet
        if (!ioIsActive || !ioStep || !ioTimer)
        {
            cErrorDom("scenario") << "AutoScenario (" << scenario_id
                                  << "): missing internal IOs, cannot create the step_end rule";
            return;
        }

        Rule *rule = new Rule("AutoScenario", scenario_id + "_step_end");
        rule->set_param("auto_scenario", scenario_id);
        rule->set_param("auto_scenario_type", "step_end");
        rule->setAutoScenario(true);
        ListeRule::Instance().Add(rule);

        addRuleCondition(rule, ioIsActive, "==", "true");
        addRuleCondition(rule, ioStep, "==", "-1");
        addRuleCondition(rule, ioTimer, "==", "true");
        addRuleAction(rule, ioIsActive, "false");

        ruleStepEnd = rule;
    }
}
