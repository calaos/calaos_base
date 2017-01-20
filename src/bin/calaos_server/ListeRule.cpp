/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
                rule->CheckConditionsAsync([=](bool check)
                {
                    if (!check) return;
                    //lock execution and execute rule
                    execInProgress = true;
                    rule->ExecuteActions();
                    execInProgress = false;
                });
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

    execInProgress = false;
}

void ListeRule::RemoveRule(IOBase *obj)
{
    //delete all rules using "output"
    for (uint i = 0;i < rules.size();i++)
    {
        Rule *rule = get_rule(i);
        Rule *rule_to_del = nullptr;
        for (int j = 0;j < rule->get_size_conds();j++)
        {
            ConditionStd *cond = dynamic_cast<ConditionStd *>(rule->get_condition(j));
            if (!cond) continue;
            for (int k = 0;k < cond->get_size();k++)
            {
                if (obj->get_param("id") ==
                    cond->get_input(k)->get_param("id"))
                    rule_to_del = rule;
            }
        }

        for (int j = 0;j < rule->get_size_actions();j++)
        {
            ActionStd *action = dynamic_cast<ActionStd *>(rule->get_action(j));
            if (!action) continue;
            for (int k = 0;k < action->get_size();k++)
            {
                if (obj->get_param("id") == action->get_output(k)->get_param("id"))
                    rule_to_del = rule;
            }
        }

        if (rule_to_del)
        {
            Remove(rule_to_del);
            i--;
        }
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
