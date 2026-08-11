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
#include <ListeRule.h>

using namespace Calaos;

namespace
{

/* Does `condition` keep a pointer to `obj` ?
 *
 * Every Condition subclass that stores an IOBase* must be handled here: a
 * missing one means RemoveRule() keeps a rule alive with a dangling IOBase*
 * after the IO has been deleted (use after free at the next evaluation or at
 * the next SaveConfigRule()).
 * ConditionStart holds no IO, and ConditionStd::params_var only holds ids that
 * are resolved against ListeRoom at each evaluation (getVarIds()), so neither
 * can dangle.
 */
bool conditionUsesIO(Calaos::Condition *condition, Calaos::IOBase *obj)
{
    if (!condition || !obj) return false;

    if (ConditionStd *cond = dynamic_cast<ConditionStd *>(condition))
    {
        for (int i = 0;i < cond->get_size();i++)
        {
            IOBase *in = cond->get_input(i);
            if (in && (in == obj || in->get_param("id") == obj->get_param("id")))
                return true;
        }
        return false;
    }

    if (ConditionOutput *cond = dynamic_cast<ConditionOutput *>(condition))
    {
        IOBase *out = cond->getOutput();
        return out && (out == obj || out->get_param("id") == obj->get_param("id"));
    }

    if (ConditionScript *cond = dynamic_cast<ConditionScript *>(condition))
    {
        //in_event is keyed by pointer and private, containsTriggerIO() is the
        //only way to look into it
        return cond->containsTriggerIO(obj);
    }

    return false;
}

/* Same for actions. Only ActionStd stores IOBase*, ActionMail/ActionPush/
 * ActionScript/ActionTouchscreen keep plain strings.
 */
bool actionUsesIO(Calaos::Action *action, Calaos::IOBase *obj)
{
    if (!action || !obj) return false;

    if (ActionStd *act = dynamic_cast<ActionStd *>(action))
    {
        for (int i = 0;i < act->get_size();i++)
        {
            IOBase *out = act->get_output(i);
            if (out && (out == obj || out->get_param("id") == obj->get_param("id")))
                return true;
        }
    }

    return false;
}

}

ListeRule &ListeRule::Instance()
{
    static ListeRule inst;

    return inst;
}

ListeRule::~ListeRule()
{
    for (uint i = 0;i < rules.size();i++)
        delete rules[i];

    rules.clear();
}

void ListeRule::Add(Rule *r)
{
    rules.push_back(r);

    if (r->param_exists("auto_scenario"))
        rules_scenarios.push_back(r);

    cDebugDom("rule") << r->get_name() << "," << r->get_type() << ": Ok";
}

void ListeRule::Remove(int pos)
{
    vector<Rule *>::iterator iter = rules.begin();
    for (int i = 0;i < pos;iter++, i++) ;

    if (rules[pos]->param_exists("auto_scenario"))
        rules_scenarios.erase(std::remove(rules_scenarios.begin(), rules_scenarios.end(), rules[pos]), rules_scenarios.end());

    delete rules[pos];
    rules.erase(iter);

    cDebugDom("rule");
}

Rule *ListeRule::operator[] (int i) const
{
    return rules[i];
}

Rule *ListeRule::get_rule(int i)
{
    return rules[i];
}

void ListeRule::RunEventLoop()
{
    if (loop) return; //only one loop at once!

    loop = true;

    //detect events
    for (uint i = 0;i < in_event.size();i++)
        in_event[i]->hasChanged();

    loop = false;

    //        cDebugDom("rule") << "ListeRule::RunEventLoop(): Loop exited";
}

void ListeRule::StopLoop()
{
    loop = false;
}

void ListeRule::ExecuteRuleSignal(std::string id)
{
    if (execInProgress)
    {
        //We can't execute rules for now. Do it later.
        Idler::singleIdler([=]()
        {
            ListeRule::Instance().ExecuteRuleSignal(id);
        });

        cDebugDom("rule") << "Mutex locked, execute rule later for input " << id;
        return;
    }

    execInProgress = true;

    //The synchronous walk below counts as one outstanding execution. Every
    //async script execution started on the way takes its own reference, so
    //execInProgress is only released once the last one has completed and the
    //deferral guard above really serializes the executions.
    //Taking a reference for the synchronous part is what makes a callback
    //fired synchronously (Rule::CheckConditionsAsync() short path) harmless.
    execRefCount = 1;

    cDebugDom("rule") << "Received signal for id " << id;

    unordered_map<Rule *, bool> execRules;

    for (Rule *rule: rules)
    {
        for (int j = 0;j < rule->get_size_conds();j++)
        {
            ConditionStd *cond = dynamic_cast<ConditionStd *>(rule->get_condition(j));
            bool exec = false;
            for (int k = 0;cond && k < cond->get_size();k++)
            {
                if (cond->get_input(k)->get_param("id") == id)
                {
                    if (cond->useForTrigger() &&
                        rule->CheckConditions())
                    {
                        //Add only rules once to the exec list
                        if (execRules.find(rule) == execRules.end())
                            execRules[rule] = true;
                    }
                    exec = true;
                }
            }
            if (!exec && cond)
            {
                vector<IOBase *> list;
                cond->getVarIds(list);

                for (uint k = 0;k < list.size();k++)
                {
                    if (list[k]->get_param("id") == id)
                    {
                        if (cond->useForTrigger() &&
                            rule->CheckConditions())
                        {
                            if (execRules.find(rule) == execRules.end())
                                execRules[rule] = true;
                        }
                        exec = true;
                    }
                }
            }

            ConditionScript *script_cond = dynamic_cast<ConditionScript *>(rule->get_condition(j));
            if (script_cond &&
                script_cond->containsTriggerIO(ListeRoom::Instance().get_io(id)))
            {
                //Keep the execution locked until this script has completed
                execRefCount++;

                rule->CheckConditionsAsync([=](bool check)
                {
                    if (check)
                        rule->ExecuteActions();
                    releaseExecution();
                }, id);
            }

            ConditionOutput *ocond = dynamic_cast<ConditionOutput *>(rule->get_condition(j));
            if (ocond && ocond->getOutput()->get_param("id") == id &&
                ocond->useForTrigger() &&
                rule->CheckConditions())
            {
                if (execRules.find(rule) == execRules.end())
                    execRules[rule] = true;
            }
        }
    }

    //Execute all rules actions now
    for (auto it: execRules)
    {
        it.first->ExecuteActions();
    }

    //Drop the reference taken for the synchronous part. If script conditions
    //are still running, execInProgress stays true until their last callback.
    releaseExecution();
}

void ListeRule::releaseExecution()
{
    if (execRefCount > 0)
        execRefCount--;

    if (execRefCount == 0)
        execInProgress = false;
}

void ListeRule::RemoveRule(IOBase *obj)
{
    if (!obj) return;

    //Delete every rule referencing obj, whatever the condition/action type it
    //is referenced from. Anything left behind would keep a dangling IOBase*.
    for (uint i = 0;i < rules.size();)
    {
        Rule *rule = rules[i];
        bool used = false;

        for (int j = 0;!used && j < rule->get_size_conds();j++)
            used = conditionUsesIO(rule->get_condition(j), obj);

        for (int j = 0;!used && j < rule->get_size_actions();j++)
            used = actionUsesIO(rule->get_action(j), obj);

        if (used)
        {
            //Remove() erases the entry, the next rule now sits at index i
            cDebugDom("rule") << "Removing rule " << rule->get_name()
                              << ", it uses deleted IO " << obj->get_param("id");
            Remove(rule);
        }
        else
            i++;
    }
}

void ListeRule::updateAllRulesToInput(IOBase *oldio, IOBase *newio)
{
    for (uint i = 0;i < rules.size();i++)
    {
        Rule *rule = get_rule(i);
        for (int j = 0;j < rule->get_size_conds();j++)
        {
            ConditionStd *cond = dynamic_cast<ConditionStd *>(rule->get_condition(j));
            if (!cond) continue;
            for (int k = 0;k < cond->get_size();k++)
            {
                if (cond->get_input(k) == oldio)
                    cond->Assign(k, newio);
            }
        }
    }
}

void ListeRule::updateAllRulesToOutput(IOBase *oldio, IOBase *newio)
{
    for (uint i = 0;i < rules.size();i++)
    {
        Rule *rule = get_rule(i);
        for (int j = 0;j < rule->get_size_actions();j++)
        {
            ActionStd *action = dynamic_cast<ActionStd *>(rule->get_action(j));
            if (!action) continue;
            for (int k = 0;k < action->get_size();k++)
            {
                if (action->get_output(k) == oldio)
                    action->Assign(k, newio);
            }
        }
    }
}

void ListeRule::ExecuteStartRules()
{
    for (uint i = 0;i < rules.size();i++)
    {
        Rule *rule = get_rule(i);
        bool found = false;

        for (int j = 0;j < rule->get_size_conds();j++)
        {
            ConditionStart *condition = dynamic_cast<ConditionStart *>(rule->get_condition(j));
            if (condition)
                found = true;
        }

        if (found)
            rule->Execute();
    }
}

list<Rule *> ListeRule::getRuleAutoScenario(string auto_scenario)
{
    list<Rule *> l;
    list<Rule *>::iterator it = rules_scenarios.begin();

    for (;it != rules_scenarios.end();it++)
    {
        Rule *r = *it;
        if (r->get_param("auto_scenario") == auto_scenario)
            l.push_back(r);
    }

    return l;
}
