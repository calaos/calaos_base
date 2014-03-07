#include "AutoScenario.h"
using namespace Calaos;

static bool _sortCompStepRule(Rule *r1, Rule* r2)
{
    int t1, t2;
    from_string(r1->get_param("auto_scenario_step"), t1);
    from_string(r2->get_param("auto_scenario_step"), t2);
    return (t1 < t2);
}

AutoScenario::AutoScenario(Input *input):
    ioScenario(dynamic_cast<Scenario *>(input)),
    ioIsActive(NULL),
    ioScheduleEnabled(NULL),
    ioStep(NULL),
    ioTimer(NULL),
    ioPlage(NULL),
    roomContainer(NULL),
    ruleStart(NULL),
    ruleStop(NULL),
    ruleStepEnd(NULL),
    rulePlageStart(NULL),
    rulePlageStop(NULL)

{
    cInfoDom("scenario") << "AutoScenario::AutoScenario(" << input->get_param("id") << "): Ok";

    scenario_id = input->get_param("auto_scenario");
    cycle = (input->get_param("cycle") == "true")?true:false;
    disabled = (input->get_param("disabled") == "true")?true:false;

    ListeRoom::Instance().addScenarioCache(ioScenario);
}

AutoScenario::~AutoScenario()
{
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

    //delete rules
    if (ruleStart)
        ListeRule::Instance().Remove(ruleStart);
    ruleStart = NULL;
    if (ruleStop)
        ListeRule::Instance().Remove(ruleStop);
    ruleStop = NULL;
    if (ruleStepEnd)
        ListeRule::Instance().Remove(ruleStepEnd);
    ruleStepEnd = NULL;
    if (rulePlageStart)
        ListeRule::Instance().Remove(rulePlageStart);
    rulePlageStart = NULL;
    if (rulePlageStop)
        ListeRule::Instance().Remove(rulePlageStop);
    rulePlageStop = NULL;

    for (uint i = 0;i < ruleSteps.size();i++)
        ListeRule::Instance().Remove(ruleSteps[i]);
    ruleSteps.clear();

    //delete IOs
    if (ioIsActive)
        ListeRoom::Instance().deleteIO(dynamic_cast<Input *>(ioIsActive));
    ioIsActive = NULL;
    if (ioScheduleEnabled)
        ListeRoom::Instance().deleteIO(dynamic_cast<Input *>(ioScheduleEnabled));
    ioScheduleEnabled = NULL;
    if (ioStep)
        ListeRoom::Instance().deleteIO(dynamic_cast<Input *>(ioStep));
    ioStep = NULL;
    if (ioTimer)
        ListeRoom::Instance().deleteIO(dynamic_cast<Input *>(ioTimer));
    ioTimer = NULL;
    if (ioPlage)
        ListeRoom::Instance().deleteIO(dynamic_cast<Input *>(ioPlage));
    ioPlage = NULL;
}

void AutoScenario::deleteRules()
{
    cInfoDom("scenario") << "AutoScenario::deleteRules(" << ioScenario->get_param("id") << ")";

    //delete rules
    if (ruleStepEnd)
        ListeRule::Instance().Remove(ruleStepEnd);
    ruleStepEnd = NULL;

    for (uint i = 0;i < ruleSteps.size();i++)
        ListeRule::Instance().Remove(ruleSteps[i]);
    ruleSteps.clear();
}

Input *AutoScenario::createInput(string type, string id)
{
    Input *in = ListeRoom::Instance().get_input(id);
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

        in = ListeRoom::Instance().createInput(params, roomContainer);
    }

    in->setAutoScenario(true);

    return in;
}

bool AutoScenario::checkCondition(Rule *rule, Input *input, string oper, string value)
{
    bool ret = false;
    for (int i = 0;i < rule->get_size_conds() && !ret;i++)
    {
        ConditionStd *cond = reinterpret_cast<ConditionStd *>(rule->get_condition(i));
        if (!cond) continue;
        if (cond->get_size() != 1) continue;
        if (cond->get_input(0) != input) continue;
        if (cond->get_operator().get_param(input->get_param("id")) != oper) continue;
        if (cond->get_params().get_param(input->get_param("id")) != value) continue;

        ret = true;
    }

    return ret;
}

bool AutoScenario::checkAction(Rule *rule, Output *output, string value)
{
    bool ret = false;
    for (int i = 0;i < rule->get_size_actions() && !ret;i++)
    {
        ActionStd *act = reinterpret_cast<ActionStd *>(rule->get_action(i));
        if (!act) continue;
        if (act->get_size() != 1) continue;
        if (act->get_output(0) != output) continue;
        if (act->get_params().get_param(output->get_param("id")) != value) continue;

        ret = true;
    }

    return ret;
}

void AutoScenario::addRuleCondition(Rule *rule, Input *input, string oper, string value)
{
    ConditionStd *cond = new ConditionStd();
    rule->AddCondition(cond);

    cond->Add(input);
    cond->get_operator().Add(input->get_param("id"), oper);
    cond->get_params().Add(input->get_param("id"), value);
}

void AutoScenario::addRuleAction(Rule *rule, Output *output, string value)
{
    ActionStd *act = new ActionStd();
    rule->AddAction(act);

    act->Add(output);
    act->get_params().Add(output->get_param("id"), value);
}

void AutoScenario::setRuleCondition(Rule *rule, Input *input, string oper, string value)
{
    for (int i = 0;i < rule->get_size_conds();i++)
    {
        ConditionStd *cond = reinterpret_cast<ConditionStd *>(rule->get_condition(i));
        if (!cond) continue;
        if (cond->get_size() != 1) continue;
        if (cond->get_input(0) != input) continue;
        cond->get_operator().Add(input->get_param("id"), oper);
        cond->get_params().Add(input->get_param("id"), value);

        break;
    }
}

void AutoScenario::setRuleAction(Rule *rule, Output *output, string value)
{
    for (int i = 0;i < rule->get_size_actions();i++)
    {
        ActionStd *act = reinterpret_cast<ActionStd *>(rule->get_action(i));
        if (!act) continue;
        if (act->get_size() != 1) continue;
        if (act->get_output(0) != output) continue;
        act->get_params().Add(output->get_param("id"), value);

        break;
    }
}

string AutoScenario::getRuleConditionValue(Rule *rule, Input *input, string oper)
{
    string ret;

    for (int i = 0;i < rule->get_size_conds();i++)
    {
        ConditionStd *cond = reinterpret_cast<ConditionStd *>(rule->get_condition(i));
        if (!cond) continue;
        if (cond->get_size() != 1) continue;
        if (cond->get_input(0) != input) continue;
        cond->get_operator().Add(input->get_param("id"), oper);

        ret = cond->get_params().get_param(input->get_param("id"));

        break;
    }

    return ret;
}

string AutoScenario::getRuleActionValue(Rule *rule, Output *output)
{
    string ret;

    for (int i = 0;i < rule->get_size_actions();i++)
    {
        ActionStd *act = reinterpret_cast<ActionStd *>(rule->get_action(i));
        if (!act) continue;
        if (act->get_size() != 1) continue;
        if (act->get_output(0) != output) continue;
        ret = act->get_params().get_param(output->get_param("id"));

        break;
    }

    return ret;
}

void AutoScenario::checkScenarioRules()
{
    /* get/create needed IOs for rules */

    //clear everything
    ruleStart = NULL;
    ruleStop = NULL;
    ruleStepEnd = NULL;
    rulePlageStart = NULL;
    rulePlageStop = NULL;
    ruleSteps.clear();
    ioIsActive = NULL;
    ioScheduleEnabled = NULL;
    ioStep = NULL;
    ioTimer = NULL;
    ioPlage = NULL;

    roomContainer = ListeRoom::Instance().getRoomByInput(ioScenario);

    if (!ioIsActive)
        ioIsActive = dynamic_cast<Internal *>(createInput("InternalBool", scenario_id + "_is_active"));
    if (!ioStep)
        ioStep = dynamic_cast<Internal *>(createInput("InternalInt", scenario_id + "_step"));
    if (!ioTimer)
        ioTimer = dynamic_cast<InputTimer *>(createInput("InputTimer", scenario_id + "_timer"));

    //Get the PlageHoraire input if the scenario is scheduled
    ioPlage = dynamic_cast<InPlageHoraire *>(ListeRoom::Instance().get_input(scenario_id + "_schedule"));
    if (ioPlage)
    {
        if (!ioScheduleEnabled)
            ioScheduleEnabled = dynamic_cast<Internal *>(createInput("InternalBool", scenario_id + "_is_schedule_enabled"));

        ioScheduleEnabled->force_input_bool(!disabled); // scenario scheduling is enabled by default
        ioScheduleEnabled->set_param("save", "true"); //Save the value on disk
    }
    else
    {
        //ioScheduleEnabled is not needed if the scenario has no schedule, so
        //delete it if it exists
        //It will automatically delete all rules using this input
        ioScheduleEnabled = dynamic_cast<Internal *>(ListeRoom::Instance().get_input(scenario_id + "_is_schedule_enabled"));
        if (ioScheduleEnabled)
            ListeRoom::Instance().deleteIO(dynamic_cast<Input *>(ioScheduleEnabled));
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
        else if (ioPlage && rule->get_param("auto_scenario_type") == "time_start")
        {
            if (!checkCondition(rule, ioIsActive, "==", "false")) continue;
            if (!checkCondition(rule, ioScheduleEnabled, "==", "true")) continue;
            if (!checkCondition(rule, ioPlage, "==", "true")) continue;

            if (!checkAction(rule, ioScenario, "true")) continue;

            rulePlageStart = rule;
            rule->setAutoScenario(true);
        }
        else if (ioPlage && cycle && rule->get_param("auto_scenario_type") == "time_stop")
        {
            if (!checkCondition(rule, ioIsActive, "==", "true")) continue;
            if (!checkCondition(rule, ioScheduleEnabled, "==", "true")) continue;
            if (!checkCondition(rule, ioPlage, "==", "false")) continue;

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
                             ((ioPlage)?"has schedule, ":"has no schedule, ") <<
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
        ruleStart = new Rule("AutoScenario", scenario_id + "_button_start");
        ruleStart->set_param("auto_scenario", scenario_id);
        ruleStart->set_param("auto_scenario_type", "button_start");
        ruleStart->setAutoScenario(true);
        ListeRule::Instance().Add(ruleStart);

        addRuleCondition(ruleStart, ioScenario, "==", "true");
        addRuleCondition(ruleStart, ioIsActive, "==", "false");
        addRuleAction(ruleStart, ioScenario, "false");
        addRuleAction(ruleStart, ioIsActive, "true");
        addRuleAction(ruleStart, ioStep, "0");
        addRuleAction(ruleStart, ioTimer, "0");
        addRuleAction(ruleStart, ioTimer, "start");
    }

    if (!ruleStop)
    {
        ruleStop = new Rule("AutoScenario", scenario_id + "_button_stop");
        ruleStop->set_param("auto_scenario", scenario_id);
        ruleStop->set_param("auto_scenario_type", "button_stop");
        ruleStop->setAutoScenario(true);
        ListeRule::Instance().Add(ruleStop);

        addRuleCondition(ruleStop, ioScenario, "==", "true");
        addRuleCondition(ruleStop, ioIsActive, "==", "true");
        addRuleAction(ruleStop, ioScenario, "false");
        addRuleAction(ruleStop, ioStep, "-1");
        addRuleAction(ruleStop, ioTimer, "0");
        addRuleAction(ruleStop, ioTimer, "start");
    }

    if (!ruleStepEnd)
    {
        ruleStepEnd = new Rule("AutoScenario", scenario_id + "_step_end");
        ruleStepEnd->set_param("auto_scenario", scenario_id);
        ruleStepEnd->set_param("auto_scenario_type", "step_end");
        ruleStepEnd->setAutoScenario(true);
        ListeRule::Instance().Add(ruleStepEnd);

        addRuleCondition(ruleStepEnd, ioIsActive, "==", "true");
        addRuleCondition(ruleStepEnd, ioStep, "==", "-1");
        addRuleCondition(ruleStepEnd, ioTimer, "==", "true");
        addRuleAction(ruleStepEnd, ioIsActive, "false");
    }

    if (ioPlage && !rulePlageStart)
    {
        rulePlageStart = new Rule("AutoScenario", scenario_id + "_time_start");
        rulePlageStart->set_param("auto_scenario", scenario_id);
        rulePlageStart->set_param("auto_scenario_type", "time_start");
        rulePlageStart->setAutoScenario(true);
        ListeRule::Instance().Add(rulePlageStart);

        addRuleCondition(rulePlageStart, ioIsActive, "==", "false");
        addRuleCondition(rulePlageStart, ioScheduleEnabled, "==", "true");
        addRuleCondition(rulePlageStart, ioPlage, "==", "true");
        addRuleAction(rulePlageStart, ioScenario, "true");
    }

    if (ioPlage && cycle && !rulePlageStop)
    {
        rulePlageStop = new Rule("AutoScenario", scenario_id + "_time_stop");
        rulePlageStop->set_param("auto_scenario", scenario_id);
        rulePlageStop->set_param("auto_scenario_type", "time_stop");
        rulePlageStop->setAutoScenario(true);
        ListeRule::Instance().Add(rulePlageStop);

        addRuleCondition(rulePlageStop, ioIsActive, "==", "true");
        addRuleCondition(rulePlageStop, ioScheduleEnabled, "==", "true");
        addRuleCondition(rulePlageStop, ioPlage, "==", "false");
        addRuleAction(rulePlageStop, ioStep, "-1");
        addRuleAction(rulePlageStop, ioTimer, "0");
        addRuleAction(rulePlageStop, ioTimer, "start");
    }

    //Check steps rules, if they are correctly chained and if the last one is calling the final endStep
    ::sort(ruleSteps.begin(), ruleSteps.end(), _sortCompStepRule);

    for (uint i = 0;i < ruleSteps.size();i++)
    {
        Rule *rule = ruleSteps[i];
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
}

void AutoScenario::addStep(double pause)
{
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
        Rule *last = *(ruleSteps.end() - 1);

        setRuleAction(last, ioStep, Utils::to_string(ruleSteps.size()));
    }

    ListeRule::Instance().Add(rule);

    ruleSteps.push_back(rule);
}

void AutoScenario::setStepPause(int s, double pause)
{
    if (s >= (int)ruleSteps.size() || s < 0) return;

    Rule *step = ruleSteps[s];
    setRuleAction(step, ioTimer, Utils::to_string(pause));
}

void AutoScenario::addStepAction(int s, Output *out, string action)
{
    if ((s >= (int)ruleSteps.size() || s < 0) && s != END_STEP) return;

    Rule *step;
    if (s == END_STEP)
        step = ruleStepEnd;
    else
        step = ruleSteps[s];
    addRuleAction(step, out, action);
}

double AutoScenario::getStepPause(int s)
{
    if (s >= (int)ruleSteps.size() || s < 0) return 0.0;

    double pause;
    Rule *step = ruleSteps[s];
    from_string(getRuleActionValue(step, ioTimer), pause);

    return pause;
}

int AutoScenario::getStepActionCount(int s)
{
    if (s >= (int)ruleSteps.size() || s < 0) return 0;

    Rule *step = ruleSteps[s];
    return step->get_size_actions() - 3;
}

ScenarioAction AutoScenario::getStepAction(int s, int action)
{
    if (s >= (int)ruleSteps.size() || s < 0) return ScenarioAction();

    Rule *step = ruleSteps[s];
    ScenarioAction sa;

    int cpt = 0;
    for (int i = 0;i < step->get_size_actions();i++)
    {
        ActionStd *act = reinterpret_cast<ActionStd *>(step->get_action(i));
        if (!act) continue;
        if (act->get_size() != 1) continue;
        if (act->get_output(0) == ioStep) continue;
        if (act->get_output(0) == ioTimer) continue;

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

int AutoScenario::getEndStepActionCount()
{
    return ruleStepEnd->get_size_actions() - 1;
}

ScenarioAction AutoScenario::getEndStepAction(int action)
{
    ScenarioAction sa;

    int cpt = 0;
    for (int i = 0;i < ruleStepEnd->get_size_actions();i++)
    {
        ActionStd *act = reinterpret_cast<ActionStd *>(ruleStepEnd->get_action(i));
        if (!act) continue;
        if (act->get_size() != 1) continue;
        if (act->get_output(0) == ioStep) continue;
        if (act->get_output(0) == ioTimer) continue;

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

    for (uint i = 0;i < ruleSteps.size();i++)
    {
        for (int j = 0;j < getStepActionCount(i);j++)
        {
            ScenarioAction sa = getStepAction(i, j);

            if ((sa.io->get_param("type") == "WODigital" && sa.io->get_param("gtype") == "light") ||
                sa.io->get_param("type") == "WODali" ||
                sa.io->get_param("type") == "WODaliRVB" ||
                sa.io->get_param("type") == "WONeon" ||
                sa.io->get_param("type") == "X10Output")
            {
                catLight.count++;
            }
            else if (sa.io->get_param("type") == "WOVolet" ||
                     sa.io->get_param("type") == "WOVoletSmart")
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
