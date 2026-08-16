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

    virtual bool LoadFromXml(pugi::xml_node node);
    virtual bool SaveToXml(pugi::xml_node node);
};

}
#endif
