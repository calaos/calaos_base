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

/* Does `condition` reference the IO `id` ?
 *
 * E4.2c: this is now a pure id comparison. Conditions store ids, so answering
 * this question resolves nothing and dereferences nothing - which matters
 * precisely here, since the caller is about to destroy that IO (and, in the
 * ~Room path, may be calling us while the IO is already half gone).
 * ConditionStart holds no IO, and ConditionStd::params_var only holds ids that
 * are resolved at each evaluation, so neither can dangle.
 */
bool conditionUsesIO(Calaos::Condition *condition, const std::string &id)
{
    if (!condition || id.empty()) return false;

    if (ConditionStd *cond = dynamic_cast<ConditionStd *>(condition))
    {
        for (int i = 0;i < cond->get_size();i++)
        {
            if (cond->get_input_id(i) == id)
                return true;
        }
        return false;
    }

    if (ConditionOutput *cond = dynamic_cast<ConditionOutput *>(condition))
        return cond->getOutputId() == id;

    if (ConditionScript *cond = dynamic_cast<ConditionScript *>(condition))
        return cond->containsTriggerId(id);

    return false;
}

/* Same for actions. Only ActionStd references IOs, ActionMail/ActionPush/
 * ActionScript/ActionTouchscreen keep plain strings.
 */
bool actionUsesIO(Calaos::Action *action, const std::string &id)
{
    if (!action || id.empty()) return false;

    if (ActionStd *act = dynamic_cast<ActionStd *>(action))
    {
        for (int i = 0;i < act->get_size();i++)
        {
            if (act->get_output_id(i) == id)
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

void ListeRule::collectTriggeredRules(const string &id, vector<Rule *> &syncRules, vector<Rule *> &asyncRules)
{
    IOBase *triggerIO = ListeRoom::Instance().get_io(id);

    for (Rule *rule: rules)
    {
        bool syncTriggered = false;
        bool asyncTriggered = false;

        for (int j = 0;j < rule->get_size_conds();j++)
        {
            Condition *condition = rule->get_condition(j);

            ConditionStd *cond = dynamic_cast<ConditionStd *>(condition);
            if (cond)
            {
                bool matched = false;

                /* E4.2c: pure id comparison. This is the hot path - it runs for
                 * every condition of every rule at every IO change - and it no
                 * longer resolves anything: before, each input cost a
                 * get_param("id") (a std::string built from the literal, a map
                 * lookup and a string copy), now it is a compare against the
                 * stored id. */
                for (int k = 0;k < cond->get_size();k++)
                {
                    if (cond->get_input_id(k) == id)
                    {
                        if (!syncTriggered && cond->useForTrigger() && rule->CheckConditions())
                            syncTriggered = true;
                        matched = true;
                    }
                }

                if (!matched)
                {
                    vector<std::string> list;
                    cond->getVarIds(list);

                    for (uint k = 0;k < list.size();k++)
                    {
                        if (list[k] == id &&
                            !syncTriggered && cond->useForTrigger() && rule->CheckConditions())
                            syncTriggered = true;
                    }
                }
            }

            ConditionScript *script_cond = dynamic_cast<ConditionScript *>(condition);
            //`triggerIO` is still required, deliberately: a signal for an id
            //that resolves to nothing never dispatched a script rule, and that
            //stays true (the id lookup below would otherwise start matching).
            if (!asyncTriggered && script_cond && triggerIO &&
                script_cond->containsTriggerId(id))
            {
                //Once per rule, not once per matching condition: the
                //asynchronous evaluation runs *every* script condition of the
                //rule anyway, so dispatching it again would spawn the same
                //scripts a second time and execute the actions twice.
                asyncTriggered = true;
            }

            ConditionOutput *ocond = dynamic_cast<ConditionOutput *>(condition);
            //An empty output id is "no output at all" (it never resolves), so
            //it must not match an empty trigger id either.
            if (!syncTriggered && ocond && !ocond->getOutputId().empty() &&
                ocond->getOutputId() == id &&
                ocond->useForTrigger() && rule->CheckConditions())
                syncTriggered = true;
        }

        if (syncTriggered)
            syncRules.push_back(rule);
        if (asyncTriggered)
            asyncRules.push_back(rule);
    }
}

void ListeRule::executeTrigger(const string &id)
{
    cDebugDom("rule") << "Received signal for id " << id;

    vector<Rule *> syncRules;
    vector<Rule *> asyncRules;

    collectTriggeredRules(id, syncRules, asyncRules);

    for (Rule *rule: syncRules)
        rule->ExecuteActions();

    for (Rule *rule: asyncRules)
        dispatchAsyncRule(rule, id);
}

void ListeRule::dispatchAsyncRule(Rule *rule, const string &id)
{
    /* The script conditions are evaluated by detached processes: the callback
     * lands long after this returns, and the rule can be deleted in between
     * (an IO it uses is removed, config reload). Nothing cancels a running
     * ScriptExec callback, so it is the callback that has to check whether its
     * rule is still there, through the token that dies with it.
     */
    rule->CheckConditionsAsync([this, rule, token = rule->aliveToken()](bool check)
    {
        asyncConditionsChecked(rule, token, check);
    }, id);
}

void ListeRule::asyncConditionsChecked(Rule *rule, const std::weak_ptr<bool> &token, bool check)
{
    if (token.expired())
    {
        //The rule was deleted while its scripts were running: `rule` points to
        //freed memory, nothing here may touch it
        cDebugDom("rule") << "Script conditions completed for a deleted rule, skipping";
        return;
    }

    if (check)
        executeActionsLocked(rule);
}

void ListeRule::executeActionsLocked(Rule *rule)
{
    if (execInProgress)
    {
        //A march is already on the stack (a script condition that completed
        //synchronously): run the actions inline, that march drains whatever
        //they signal back.
        rule->ExecuteActions();
        return;
    }

    execInProgress = true;
    rule->ExecuteActions();
    drainPendingTriggers();
    execInProgress = false;
}

void ListeRule::drainPendingTriggers()
{
    while (!pendingTriggers.empty())
    {
        string id = pendingTriggers.front();
        pendingTriggers.pop_front();

        executeTrigger(id);
    }
}

void ListeRule::ExecuteRuleSignal(std::string id)
{
    if (execInProgress)
    {
        /* A march is running on the stack: an action changed an IO, which
         * signalled back into us. Queue the trigger instead of executing it
         * here, the march below runs it as soon as it is done.
         * The old code re-armed an Idler for every deferred signal, which
         * spun the event loop at 100% cpu for as long as the lock was held.
         */
        pendingTriggers.push_back(id);

        cDebugDom("rule") << "Execution in progress, deferring input " << id;
        return;
    }

    execInProgress = true;
    executeTrigger(id);
    drainPendingTriggers();
    execInProgress = false;
}

void ListeRule::RemoveRule(IOBase *obj)
{
    if (!obj) return;

    //Read the id once, here: this is the only dereference of `obj` in the whole
    //removal path now that conditions and actions compare ids.
    const std::string id = obj->get_param("id");

    //Delete every rule referencing this id, whatever the condition/action type
    //it is referenced from.
    for (uint i = 0;i < rules.size();)
    {
        Rule *rule = rules[i];
        bool used = false;

        for (int j = 0;!used && j < rule->get_size_conds();j++)
            used = conditionUsesIO(rule->get_condition(j), id);

        for (int j = 0;!used && j < rule->get_size_actions();j++)
            used = actionUsesIO(rule->get_action(j), id);

        if (used)
        {
            //Remove() erases the entry, the next rule now sits at index i
            cDebugDom("rule") << "Removing rule " << rule->get_name()
                              << ", it uses deleted IO " << id;
            Remove(rule);
        }
        else
            i++;
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
