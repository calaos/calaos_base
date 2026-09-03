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
#include <memory>

#include <ListeRule.h>
#include "AutoScenarioDef.h"

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
    //The non-owning index goes first, so that nothing can observe it pointing
    //at a rule that is being destroyed.
    rules_scenarios.clear();

    //Front to back, as before. The rule is moved out of the vector *first* so
    //that its destructor (which cascades into its conditions and actions)
    //never runs on an element that is still half-present in `rules`.
    while (!rules.empty())
    {
        std::unique_ptr<Rule> rule = std::move(rules.front());
        rules.erase(rules.begin());
    }
}

void ListeRule::Add(Rule *r)
{
    //Ownership transfer in. A null rule used to be pushed into the list and
    //then dereferenced right below.
    if (!r)
    {
        cErrorDom("rule") << "Add(): ignoring a null rule";
        return;
    }

    rules.emplace_back(r);

    /* Non-owning index, same pointer, kept in sync by Remove(). Two keys: a
     * generated rule carries both, a rule from an older server only the first,
     * and all of them have to be findable.
     */
    if (r->param_exists("auto_scenario") ||
        r->param_exists(AutoScenarioDef::KEY_UID))
        rules_scenarios.push_back(r);

    cDebugDom("rule") << r->get_name() << "," << r->get_type() << ": Ok";
}

void ListeRule::Remove(int pos)
{
    if (pos < 0 || (size_t)pos >= rules.size())
    {
        cErrorDom("rule") << "Remove(): no rule at index " << pos
                          << " (" << rules.size() << " rules), ignoring";
        return;
    }

    Rule *rule = rules[pos].get();

    /* Drop the non-owning index entry before the rule can be destroyed.
     * Unconditionally, like the Rule* overload: an erase-remove is a no-op for
     * a rule that is not indexed, so gating it on param_exists("auto_scenario")
     * bought nothing and made the index depend on that predicate answering the
     * same thing at Remove() as it did at Add(). Nothing enforces that today
     * (Rule exposes no param removal, so it happens to hold), and the day it
     * stops holding the entry is orphaned and points at a destroyed rule -
     * exactly the dangling pointer this series exists to make impossible.
     */
    rules_scenarios.erase(std::remove(rules_scenarios.begin(), rules_scenarios.end(), rule),
                          rules_scenarios.end());

    /* Take the owner OUT of the vector before anything can be destroyed
     * (E4.2b lesson, same as Room::RemoveIO()): vector::erase move-assigns
     * the tail down and unique_ptr::operator= is reset(u.release()), so
     * ~Rule() running inside erase() would see a container of unchanged size
     * whose slot `pos` already holds the NEXT rule and whose last slot is
     * null. `owned` destroys the rule at the end of this scope, with `rules`
     * already fully consistent. Removals keep the order of the rest.
     */
    std::unique_ptr<Rule> owned = std::move(rules[pos]);
    rules.erase(rules.begin() + pos);

    cDebugDom("rule");
}

void ListeRule::Remove(Rule *obj)
{
    if (!obj) return;

    //Non-owning index first, unconditionally (as before: an erase-remove is a
    //no-op for a rule that is not in it)
    rules_scenarios.erase(std::remove(rules_scenarios.begin(), rules_scenarios.end(), obj),
                          rules_scenarios.end());

    //Two owners of the same Rule cannot exist, so there is at most one match
    auto it = std::find_if(rules.begin(), rules.end(),
                           [obj](const std::unique_ptr<Rule> &p) { return p.get() == obj; });
    if (it == rules.end())
    {
        /* Not ours. The raw pointer version did `delete obj` here whatever
         * happened; under explicit ownership only the owner may destroy, and
         * we are not it. No caller reaches this: every `new Rule` of the tree
         * (AutoScenario x6, Config::LoadConfigRule, the test fixtures) is
         * handed to Add() before any Remove(), and AutoScenario nulls its
         * back-pointers right after removing.
         */
        cWarningDom("rule") << "Remove(): rule not owned by this list, ignoring";
        return;
    }

    //Out of the vector before it is destroyed, see Remove(int)
    std::unique_ptr<Rule> owned = std::move(*it);
    rules.erase(it);
}

Rule *ListeRule::operator[] (int i) const
{
    return rules[i].get();
}

Rule *ListeRule::get_rule(int i)
{
    return rules[i].get();
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

    //Front to back, insertion order: this is the rule evaluation order
    for (const std::unique_ptr<Rule> &owned: rules)
    {
        Rule *rule = owned.get();

        /* E4.2e: a rule referencing an IO that did not exist at load is never
         * triggered. Rule::CheckConditions()/ExecuteActions() refuse too, this
         * is the "no trigger" half of the contract: the rule is not even looked
         * at, so it cannot be collected and cannot spawn a script condition. */
        if (rule->isDisabled())
            continue;

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

void ListeRule::RemoveRule(IOBase *obj, RuleDetachPolicy policy)
{
    if (!obj) return;

    //Read the id once, here: this is the only dereference of `obj` in the whole
    //removal path now that conditions and actions compare ids.
    const std::string id = obj->get_param("id");

    //Deal with every rule referencing this id, whatever the condition/action
    //type it is referenced from.
    for (uint i = 0;i < rules.size();)
    {
        Rule *rule = rules[i].get();
        bool used = false;

        for (int j = 0;!used && j < rule->get_size_conds();j++)
            used = conditionUsesIO(rule->get_condition(j), id);

        for (int j = 0;!used && j < rule->get_size_actions();j++)
            used = actionUsesIO(rule->get_action(j), id);

        if (!used)
        {
            i++;
            continue;
        }

        if (policy == RuleDetachPolicy::Destroy)
        {
            //Remove() erases the entry, the next rule now sits at index i
            cDebugDom("rule") << "Removing rule " << rule->get_name()
                              << ", it uses deleted IO " << id;
            Remove(rule);
            continue;
        }

        /* T3.18: KEEP the rule and disable it. No erase, no Remove(): `rules`
         * and the non-owning `rules_scenarios` index keep both their content
         * and their order, so the rule evaluation order is strictly untouched
         * and the rule is still serialized with all its conditions and actions
         * - including the dead reference, which ActionStd::SaveToXml() writes
         * back verbatim (E4.2e). That is what lets a save/reload cycle
         * reproduce the very same disabled state through the load path, with
         * nothing new stored on the rule side.
         */
        rule->markIoMissing(id);
        cWarningDom("rule") << "Rule '" << rule->get_name() << "' is DISABLED: it "
                            << "references the deleted IO " << id << ". The rule is "
                            << "kept in the configuration and saved untouched, but it "
                            << "will never be triggered, evaluated nor executed.";
        i++;
    }
}

void ListeRule::ExecuteStartRules()
{
    for (uint i = 0;i < rules.size();i++)
    {
        Rule *rule = get_rule(i);

        //E4.2e: a disabled rule does not run at startup either (Execute()
        //refuses as well, this skips the walk and the log)
        if (rule->isDisabled())
            continue;

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

vector<Rule *> ListeRule::getDisabledRules() const
{
    vector<Rule *> disabled;

    for (const std::unique_ptr<Rule> &owned: rules)
    {
        if (owned && owned->isDisabled())
            disabled.push_back(owned.get());
    }

    return disabled;
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

list<Rule *> ListeRule::getRulesOfScenarioUid(const string &uid)
{
    list<Rule *> l;

    //An empty uid is not an identity: a scenario that has not declared one
    //owns no generated rule, and matching "" would hand it every rule whose
    //param happens to be absent.
    if (uid.empty()) return l;

    for (Rule *r: rules_scenarios)
        if (r && r->get_param(AutoScenarioDef::KEY_UID) == uid)
            l.push_back(r);

    return l;
}
