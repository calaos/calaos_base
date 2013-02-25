/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
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
        for (int i = 0;i < rules.size();i++)
                delete rules[i];

        rules.clear();

        Utils::logger("rule") << Priority::DEBUG << "ListeRule::~ListeRule(): Ok" << log4cpp::eol;
}

void ListeRule::Add(Rule *r)
{
        rules.push_back(r);

        if (r->param_exists("auto_scenario"))
                rules_scenarios.push_back(r);

        Utils::logger("rule") << Priority::DEBUG << "ListeRule::Add(" << r->get_name() << "," << r->get_type() << "): Ok" << log4cpp::eol;
}

void ListeRule::Remove(int pos)
{
        vector<Rule *>::iterator iter = rules.begin();
        for (int i = 0;i < pos;iter++, i++) ;

        if (rules[pos]->param_exists("auto_scenario"))
                rules_scenarios.erase(std::remove(rules_scenarios.begin(), rules_scenarios.end(), rules[pos]), rules_scenarios.end());

        delete rules[pos];
        rules.erase(iter);

        Utils::logger("rule") << Priority::DEBUG << "ListeRule::Remove(): Ok" << log4cpp::eol;
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
        for (int i = 0;i < in_event.size();i++)
                in_event[i]->hasChanged();

        loop = false;

//        Utils::logger("rule") << Priority::DEBUG << "ListeRule::RunEventLoop(): Loop exited" << log4cpp::eol;
}

void ListeRule::StopLoop()
{
        loop = false;
}

Eina_Bool _execute_rule_signal_idler_cb(void *data)
{
        Rule_idler_cb *cb = reinterpret_cast<Rule_idler_cb *>(data);
        if (!cb) return ECORE_CALLBACK_CANCEL;

        ListeRule::Instance().ExecuteRuleSignal(cb->input);

        delete cb;

        //delete the ecore_idler
        return ECORE_CALLBACK_CANCEL;
}

void ListeRule::ExecuteRuleSignal(std::string io_id)
{
        if (!mutex.try_lock())
        {
                //We can't execute rules for now. Do it later.
                Rule_idler_cb *cb = new Rule_idler_cb;

                cb->input = io_id;
                cb->idler = ecore_idler_add(_execute_rule_signal_idler_cb, cb);

                Utils::logger("rule") << Priority::DEBUG << "ListeRule::ExecuteRuleSignal(): Mutex locked, execute rule later for input " << io_id << log4cpp::eol;

                return;
        }

        Utils::logger("rule") << Priority::DEBUG << "ListeRule::ExecuteRuleSignal(): received signal for id " << io_id << log4cpp::eol;

        for (int i = 0;i < rules.size();i++)
        {
                Rule *rule = rules[i];
                for (int j = 0;j < rule->get_size_conds();j++)
                {
                        ConditionStd *cond = dynamic_cast<ConditionStd *>(rule->get_condition(j));
                        bool exec = false;
                        for (int k = 0;cond && k < cond->get_size();k++)
                        {
                                if (cond->get_input(k)->get_param("id") == io_id)
                                {
                                        rule->Execute();
                                        exec = true;
                                }
                        }
                        if (!exec && cond)
                        {
                                vector<Input *> list;
                                cond->getVarIds(list);

                                for (int k = 0;k < list.size();k++)
                                {
                                        if (list[k]->get_param("id") == io_id)
                                        {
                                                rule->Execute();
                                                exec = true;
                                        }
                                }
                        }

                        ConditionScript *scond = dynamic_cast<ConditionScript *>(rule->get_condition(j));
                        for (int k = 0;scond && k < scond->get_size();k++)
                        {
                                if (scond->get_input(k)->get_param("id") == io_id)
                                        rule->Execute();
                        }

                        ConditionOutput *ocond = dynamic_cast<ConditionOutput *>(rule->get_condition(j));
                        if (ocond && ocond->getOutput()->get_param("id") == io_id)
                                rule->Execute();
                }
        }

        mutex.unlock();
}

void ListeRule::RemoveRule(Input *obj)
{
        //delete all rules using "output"
        for (int i = 0;i < rules.size();i++)
        {
                Rule *rule = get_rule(i);
                Rule *rule_to_del = NULL;
                for (int j = 0;j < rule->get_size_conds();j++)
                {
                        ConditionStd *cond = dynamic_cast<ConditionStd *>(rule->get_condition(j));
                        if (!cond) continue;
                        for (int k = 0;k < cond->get_size();k++)
                        {
                                        if (obj->get_param("id")
                                                == cond->get_input(k)->get_param("id"))
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

void ListeRule::RemoveRule(Output *obj)
{
        //delete all rules using "output"
        for (int i = 0;i < rules.size();i++)
        {
                Rule *rule = get_rule(i);
                Rule *rule_to_del = NULL;
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

void ListeRule::updateAllRulesToInput(Input *oldio, Input *newio)
{
        for (int i = 0;i < rules.size();i++)
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

void ListeRule::updateAllRulesToOutput(Output *oldio, Output *newio)
{
        for (int i = 0;i < rules.size();i++)
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
        for (int i = 0;i < rules.size();i++)
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
