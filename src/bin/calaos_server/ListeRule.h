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
#ifndef S_LISTERULE_H
#define S_LISTERULE_H

#include <deque>
#include <memory>

#include "Calaos.h"
#include "Rule.h"
#include "ConditionStd.h"
#include "ConditionOutput.h"
#include "ConditionStart.h"
#include "ConditionScript.h"
#include "ActionStd.h"
#include "ListeRoom.h"
#include "Room.h"

using namespace std;

namespace Calaos
{

class ListeRule: public sigc::trackable
{
protected:
    /* -------------------------------------------------------------------
     * Ownership (E4.2d)
     *
     * ListeRule owns the Rules; a Rule owns its Conditions and Actions
     * (see Rule.h). Nothing else in the tree may delete a Rule, and there is
     * no `delete` left here.
     *
     * `rules` is and stays a plain vector appended to by Add(): its ORDER is
     * the rule evaluation order (collectTriggeredRules(), ExecuteStartRules()
     * and RemoveRule() all march it front to back), and changing it changes
     * which rule fires first. Removals keep the relative order of the rest.
     *
     * `rules_scenarios` is a NON-OWNING index over the very same Rules, kept
     * in sync by Add()/Remove(): a Rule appears in both containers, so only
     * one of them can own it. getRuleAutoScenario() hands out those same
     * non-owning pointers.
     *
     * `in_event` is a non-owning list of IOs owned by their Room (E4.2b),
     * registered by the IO itself (see Add(IOBase*)/Remove(IOBase*)).
     * ---------------------------------------------------------------- */
    std::vector<std::unique_ptr<Rule>> rules;

    //these input's events are detected in the RunEventLoop() function
    std::vector<IOBase *> in_event;

    //Rules for autoscenario. NON-OWNING view over `rules`.
    list<Rule *> rules_scenarios;

    bool loop = false;

    /* True while a march over the rule list is running on the stack.
     * It only covers the synchronous part of an execution: the actions of a
     * rule set IOs, which signal back into ExecuteRuleSignal(), and such a
     * nested march would iterate the rules while the first one is still using
     * them. Asynchronous script conditions are deliberately *not* covered:
     * holding the lock for the whole lifetime of a detached process wedged
     * every other rule until the script came back, forever when it never did.
     * What the async side needs is per rule protection, which is the lifetime
     * token of Rule (see Rule::aliveToken()), not a global lock.
     */
    bool execInProgress = false;

    //Trigger ids signalled while a march was running. They are executed by the
    //outermost march when it releases the lock: no idler, so no busy wait, and
    //nothing is executed on top of a running march.
    std::deque<std::string> pendingTriggers;

    //One march over the rule list for a single trigger id
    void executeTrigger(const std::string &id);

    //Execute the triggers deferred by a nested ExecuteRuleSignal(), including
    //those deferred while draining
    void drainPendingTriggers();

    //Run the actions of a rule while holding the execution lock
    void executeActionsLocked(Rule *rule);

    /* Start the asynchronous evaluation of the script conditions of `rule` for
     * the trigger `id`, and run its actions when they all pass.
     * Virtual so that a test can observe the dispatch without spawning a lua
     * process for real (no libuv loop runs there).
     */
    virtual void dispatchAsyncRule(Rule *rule, const std::string &id);

    /* Completion of the evaluation started by dispatchAsyncRule(). `rule` is
     * only dereferenced when `token` is still valid: the rule can have been
     * deleted while its scripts were running, and nothing cancels a running
     * ScriptExec callback.
     * A function of its own so that a test can replay a callback landing after
     * the deletion, which is the case that used to be a use after free.
     */
    void asyncConditionsChecked(Rule *rule, const std::weak_ptr<bool> &token, bool check);

    ListeRule()
    { }

public:
    //singleton
    static ListeRule &Instance();

    ~ListeRule();

    /* TAKES OWNERSHIP of p (a null p is ignored). Appended at the end: the
       rule evaluation order is the insertion order. */
    void Add(Rule *p);
    /* Destroy the rule at index i, and with it its conditions and actions.
       Out of range is a logged no-op. */
    void Remove(int i);
    /* Same, by pointer. `obj` must be a rule this list owns - the only thing
       ListeRule may destroy is what it owns, so a rule it does not hold is a
       logged no-op instead of the unconditional `delete obj` of the raw
       pointer era (no caller ever relied on it, every Rule of the tree is
       Add()ed to this list right after being built). */
    void Remove(Rule *obj);
    void RemoveRule(IOBase *obj); //remove all rules containing obj

    //NON-OWNING. Valid while this list holds the rule.
    Rule *get_rule(int i);
    Rule *operator[] (int i) const;

    /* Event polling list. Registration is done by the IO itself (see
     * InputTime/InputAnalog/InPlageHoraire), so unregistration must be driven
     * by membership in THIS list and by nothing else - it used to be gated on
     * a gui_type whitelist in ListeRoom::detachIOFromRules(), which is not the
     * same set (E4.2c). Remove() is an erase-remove: a no-op for an IO that
     * never registered, so it is always safe to call. */
    void Add(IOBase *io) { in_event.push_back(io); }
    void Remove(IOBase *io)
    { in_event.erase(std::remove(in_event.begin(), in_event.end(), io), in_event.end()); }

    /* Is this IO currently in the polling list? RunEventLoop() dereferences
     * every entry, so "registered" must mean "still alive": this is what a test
     * asserts on after an IO deletion. */
    bool isEventRegistered(IOBase *io) const
    { return std::find(in_event.begin(), in_event.end(), io) != in_event.end(); }

    size_t eventCount() const { return in_event.size(); }
    //Run a loop to detect event from inputs when time or temperature changes
    void RunEventLoop();
    void StopLoop();

    int size() { return rules.size(); }

    //Execute all rules where the input 'input_id' is used
    //The function is called only when a signal is emited from inputs
    virtual void ExecuteRuleSignal(std::string id);

    /* Rules triggered by the IO `id`, split by the way their conditions have to
     * be evaluated: `syncRules` are the ones whose conditions are already known
     * to pass, `asyncRules` the ones holding at least one script condition
     * triggered by `id`, which needs a detached process to be evaluated.
     * A rule appears at most once in each list, whatever the number of its
     * conditions matching `id`.
     * Evaluates conditions but changes nothing, which is what makes the
     * dispatch observable from a test.
     */
    void collectTriggeredRules(const std::string &id,
                               std::vector<Rule *> &syncRules,
                               std::vector<Rule *> &asyncRules);

    //True while a march is running. A pending script condition does not hold it
    bool isExecutionLocked() const { return execInProgress; }

    //Triggers signalled during the current march, waiting for it to end
    size_t pendingTriggerCount() const { return pendingTriggers.size(); }

    /* This executes all rules at program startup. All rules with ConditionStart
                 * will be evaluated and executed (only once)
                 */
    void ExecuteStartRules();

    //NON-OWNING pointers into `rules`, in rules_scenarios order.
    list<Rule *> getRuleAutoScenario(string auto_scenario);
};

}
#endif
