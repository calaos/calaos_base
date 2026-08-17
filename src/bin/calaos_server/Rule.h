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
#ifndef S_RULE_H
#define S_RULE_H

#include <memory>

#include "Calaos.h"
#include "Condition.h"
#include "Action.h"
#include "Timer.h"

using namespace std;

namespace Calaos
{

/* What to do with the rules referencing an IO that is being taken away
 * (ListeRule::RemoveRule(), ListeRoom::detachIOFromRules()/deleteIO()).
 *
 * T3.18, user decision. `Disable` is the DEFAULT because it is the semantics of
 * E4.2e: the rule stays in the list, keeps its conditions and its actions, and
 * is only marked as referencing a missing IO - which is what Rule::isDisabled()
 * answers, and what ActionStd::SaveToXml() then writes back verbatim. Nothing
 * of the user's configuration is lost, and reloading the very same rules.xml
 * reproduces the same state through the load path.
 *
 * `Destroy` is the historical behaviour and must stay EXPLICIT at the teardown
 * sites (AutoScenario::deleteAll()/deleteSchedule(), the schedule-enable IO of
 * checkScenarioRules(), Room::~Room). Those sites rebuild the very same rules
 * right after, and keeping a disabled copy around would produce duplicates that
 * nothing collects (Rule::setAutoScenario(false) does not exist).
 *
 * It lives in Rule.h and not in ListeRule.h because ListeRoom.h and ListeRule.h
 * include each other: Rule.h is the one header both of them see complete,
 * whichever of the two is included first.
 */
enum class RuleDetachPolicy { Disable, Destroy };

class Rule
{
protected:
    /* -------------------------------------------------------------------
     * Ownership (E4.2d)
     *
     * A Rule is the ONE owner of its Conditions and its Actions. The
     * vectors express it: destroying the Rule destroys them, and nothing
     * else in the tree may delete a Condition or an Action. Both base
     * classes have a virtual destructor, so the derived object (ConditionStd,
     * ActionMail...) is the one that runs.
     *
     * Every hand-out below (get_condition(), get_action()) is a NON-OWNING
     * observation, valid as long as this Rule holds the object. There is no
     * hand-back path here: unlike Room::RemoveIO(pos, del=false) in E4.2b,
     * nothing in the tree ever takes a Condition or an Action back out of a
     * Rule, so no release() is needed - see RemoveCondition()/RemoveAction().
     *
     * Iteration order is the insertion order and is load bearing (it decides
     * the order conditions are evaluated and actions executed): both
     * containers stay plain vectors appended to by AddCondition()/AddAction().
     * ---------------------------------------------------------------- */
    vector<std::unique_ptr<Condition>> conds;
    vector<std::unique_ptr<Action>> actions;

    Params params;

    /* -------------------------------------------------------------------
     * Disabled rule (E4.2e) - user decision
     *
     * Ids that one of the conditions/actions of this rule references and that
     * did NOT resolve when the config was read (collected from
     * Condition::getMissingIoIds()/Action::getMissingIoIds() by AddCondition()
     * and AddAction()). A non-empty list means the rule is DISABLED.
     *
     * WHY the whole rule and not just the term: rejecting the term amputates a
     * conjunction, and an amputated conjunction is WEAKER. `absent == true AND
     * hour > 22h -> switch everything off` with the presence IO gone becomes
     * `hour > 22h -> switch everything off`, i.e. it fires every single night.
     * Better an inert rule than one acting on incomplete criteria.
     *
     * A disabled rule stays LOADED AND VISIBLE - it is still in ListeRule, it
     * is still serialized by SaveToXml() with all its conditions and actions -
     * it is only excluded from execution: no trigger (ListeRule skips it), no
     * evaluation (CheckConditions() answers false), no action (ExecuteActions()
     * does nothing). Deleting it instead would destroy the user's config as
     * soon as a driver is unplugged.
     *
     * NOT persisted: `disabled` is derived from the config at every load, so
     * writing it back into rules.xml would turn a diagnosis into a stored
     * state that survives the IO coming back. Restore the IO, reload, and the
     * rule runs again.
     * ---------------------------------------------------------------- */
    vector<string> missingIoIds;

    bool auto_sc_mark; //true if rule is used by an auto_scenario

    /* Lifetime token for the asynchronous evaluation of the script conditions.
     * Those callbacks are plain std::function held by a detached ScriptExec
     * process: there is no connection to disconnect and no way to cancel them,
     * while the rule is deleted from under them as soon as one of the IOs it
     * uses goes away (ListeRule::RemoveRule()). Whoever starts an asynchronous
     * evaluation captures a weak_ptr on this token next to the Rule*, and drops
     * the callback when it has expired.
     */
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    //Merge the unresolved ids of a condition/action into missingIoIds
    void collectMissingIo(const vector<string> &ids);

public:
    Rule(string _type, string _name);
    virtual ~Rule();

    //See `alive`. Expires when the rule is destroyed.
    std::weak_ptr<bool> aliveToken() const { return alive; }

    /* TAKE OWNERSHIP of p, appended at the end (the evaluation/execution
       order is the insertion order). */
    void AddCondition(Condition *p);
    void AddAction(Action *p);
    bool Execute();
    bool CheckConditions();
    void CheckConditionsAsync(std::function<void (bool check)> cb, string triggerId);
    bool ExecuteActions();
    /* Destroy the condition/action at index i. Out of range is a logged
       no-op (it used to walk the iterator past end()). This is NOT a
       hand-back: the object is destroyed, not returned. */
    void RemoveCondition(int i);
    void RemoveAction(int i);

    //NON-OWNING. Valid while this Rule holds the object.
    Condition *get_condition(int i) { return conds[i].get(); }
    Action *get_action(int i) { return actions[i].get(); }

    int get_size_conds() { return conds.size(); }
    int get_size_actions() { return actions.size(); }

    string get_type() { return params["type"]; }
    string get_name() { return params["name"]; }
    string get_param(string p) { return params[p]; }
    void set_param(string p, string v) { params.Add(p, v); }
    bool param_exists(string p) { return params.Exists(p); }

    bool isAutoScenario() { return auto_sc_mark; }
    void setAutoScenario(bool m) { auto_sc_mark = m; }

    /* E4.2e. True when at least one condition or action of this rule
     * references an IO that did not exist when the config was read. Such a
     * rule is never triggered, never evaluated and never executed - see the
     * comment on missingIoIds above. */
    bool isDisabled() const { return !missingIoIds.empty(); }

    //The unresolved ids, in the order they were met. Empty for a healthy rule.
    const vector<string> &getMissingIoIds() const { return missingIoIds; }

    /* T3.18. Record `id` as a dependency of this rule that no longer resolves,
     * from the HOT path this time: the load path fills missingIoIds through
     * AddCondition()/AddAction(), but an IO deleted while the server runs never
     * goes through them. Same storage, same de-duplication, same empty-id
     * sentinel as Condition::addMissingIo(), so a rule disabled at runtime is
     * indistinguishable from one disabled at load - which is exactly what makes
     * the state survive a save/reload cycle without a single new stored field on
     * the rule side.
     */
    void markIoMissing(const string &id);

    //"io_a, io_b" - for the logs and the config alert
    string getMissingIoDescription() const;

    virtual bool LoadFromXml(pugi::xml_node node);
    virtual bool SaveToXml(pugi::xml_node node);
};

}
#endif
