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
#ifndef  AUTOSCENARIO_H
#define  AUTOSCENARIO_H

#include "Calaos.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "IOFactory.h"
#include "ConditionStd.h"
#include "ActionStd.h"
#include "Room.h"
#include "Scenario.h"
#include "IntValue.h"
#include "InputTimer.h"
#include "InPlageHoraire.h"

namespace Calaos
{

class ScenarioAction
{
public:
    IOBase *io = nullptr;
    string action;
};

/* NON-OWNING reference to a Rule owned by ListeRule.
 *
 * AutoScenario does not own a single one of the rules it points at: they all
 * belong to ListeRule (E4.2d), which destroys them on its own initiative. The
 * path that matters is not even reachable from here:
 * ListeRoom::deleteIO() -> detachIOFromRules() -> ListeRule::RemoveRule(io)
 * destroys *every* rule citing that IO id, and a step rule cites the IO the
 * user picked with addStepAction(). Nothing nulls the back-pointer and nothing
 * re-runs checkScenarioRules() (ListeRoom::checkAutoScenario() only runs once,
 * at startup), so the next get_scenario of the UI read freed memory.
 *
 * The token is Rule::aliveToken(), the very same one the asynchronous script
 * conditions use: a weak_ptr held by value, expiring when the Rule is
 * destroyed. get() answers null from that moment on, which is exactly what the
 * next checkScenarioRules() would produce for a rule that no longer exists.
 */
class RuleRef
{
public:
    RuleRef() = default;
    RuleRef(Rule *r) { *this = r; }

    RuleRef &operator=(Rule *r)
    {
        rule = r;
        if (r)
            token = r->aliveToken();
        else
            token.reset();
        return *this;
    }

    //Null as soon as the rule has been destroyed (a default-constructed
    //weak_ptr is expired, so a null rule resolves to null too)
    Rule *get() const { return token.expired()?nullptr:rule; }
    explicit operator bool() const { return get() != nullptr; }

    /* T3.18: "a rule WAS referenced here and has since been destroyed", told
     * apart from "nothing was ever referenced here". get() answers null for
     * both, which is enough for the readers but not for AutoScenario::isBroken()
     * - ruleStart & co. are legitimately null before the first
     * checkScenarioRules() and that is not a breakage.
     */
    bool isDangling() const { return rule != nullptr && token.expired(); }

    void reset() { rule = nullptr; token.reset(); }

private:
    Rule *rule = nullptr;
    std::weak_ptr<bool> token;
};

class AutoScenario
{
private:
    string scenario_id;
    bool cycle;
    bool disabled;

    /* -------------------------------------------------------------------
     * T3.18 - gate 2: "this scenario was disabled because an IO it uses
     * disappeared", persisted in the `disabled_missing_io` param of the
     * Scenario IO. USER DECISION, and it is deliberately STICKY:
     *
     *   "On desactive le scenario, on le flag avec un nouveau parametre dans
     *    la config pour que ca survive a un reboot, et un user doit corriger
     *    le scenario manuellement en le reactivant. Si un IO disparait c'est
     *    un probleme, on ne peut pas le resoudre sans intervention manuelle."
     *
     * So: it is only ever SET by the detection pass
     * (ListeRoom::refreshBrokenScenarios()) and only ever CLEARED by
     * tryReenable(), i.e. by an explicit user action. No refresh pass may
     * clear it - putting the IO back is not enough, and that is the point:
     * a missing IO is an incident, not a transient state to catch up with on
     * its own.
     *
     * It is NOT `disabled` (see setDisabled()): that one is a user choice
     * meaning "do not run this scenario on its schedule", it is exposed as
     * "enabled" in the payload and REWRITTEN by every `autoscenario modify`.
     * Reusing it would let the first modify of any client restart a broken
     * scenario. The long name is on purpose, it shares a Params with
     * `disabled` and `auto_scenario`.
     *
     * READ IN THE CONSTRUCTOR, next to cycle/disabled, i.e. BEFORE anything
     * can save: ListeRoom::checkAutoScenario() ends with SaveConfigIO(), so a
     * flag that was not read back at load would be wiped from disk on the very
     * first startup, silently and without any user action.
     * ---------------------------------------------------------------- */
    bool disabledMissingIo;

    //All IO used by this scenario's rules
    Scenario *ioScenario;
    Internal *ioIsActive;
    Internal *ioScheduleEnabled;
    Internal *ioStep;
    InputTimer *ioTimer;
    InPlageHoraire *ioTimeRange;

    Room *roomContainer;

    //NON-OWNING, see RuleRef: ListeRule owns and may destroy them at any time
    RuleRef ruleStart, ruleStop, ruleStepEnd;
    RuleRef rulePlageStart, rulePlageStop;
    vector<RuleRef> ruleSteps;

    /* Drop the steps whose rule has been destroyed, keeping the order of the
     * survivors. Called by everything that indexes ruleSteps, because that
     * index IS the step number of the API: a stale size is half the bug (the
     * UI asks for step N, gets the actions of another one, or of nothing).
     */
    void purgeDeadSteps();
    //The step rule at the (compacted) index s, null when s is out of range
    Rule *stepRule(int s);

    IOBase *createInput(string type, string id);
    bool checkCondition(Rule *rule, IOBase *input, string oper, string value);
    bool checkAction(Rule *rule, IOBase *output, string value);
    void addRuleCondition(Rule *rule, IOBase *input, string oper, string value);
    void addRuleAction(Rule *rule, IOBase *output, string value);
    void setRuleCondition(Rule *rule, IOBase *input, string oper, string value);
    void setRuleAction(Rule *rule, IOBase *output, string value);
    string getRuleConditionValue(Rule *rule, IOBase *input, string oper);
    string getRuleActionValue(Rule *rule, IOBase *output);
    list<IOBase *> getRuleRealActions(Rule *rule);
    void createRuleStepEnd();

    //True for the IOs driving the scenario itself (step, timer, is_active,
    //the scenario IO and the schedule flag). Those are never reported as user
    //actions of a step. A null IO is reported as internal too.
    bool isScenarioInternalIO(IOBase *io);
    //Count/get the user actions of a rule, both using the same skip list so
    //that an index returned by the first is always resolvable by the second
    int countRealActions(Rule *rule);
    ScenarioAction getRealAction(Rule *rule, int action);

public:
    AutoScenario(IOBase *input);
    ~AutoScenario();

    static const int END_STEP = 0xFEDC1234;

    //False when one of the internal scenario IOs could not be created (IO
    //factory miss, or an existing IO of the wrong type using one of the
    //internal ids): the rules build is aborted and nothing was created.
    bool checkScenarioRules();
    void deleteAll();
    void deleteRules();

    string getScenarioId() { return scenario_id; }
    bool isCycling() { return cycle; }
    bool isDisabled() { return disabled; }
    bool isScheduled() { return ioTimeRange?true:false; }
    void setCycling(bool c); //should call checkScenarioRules() to commit changes
    void setDisabled(bool d); //should call checkScenarioRules() to commit changes

    /* -------------------------------------------------------------------
     * T3.18 - the two gates. A scenario starts if and only if
     *
     *      !isBroken()   AND   !isDisabledMissingIo()
     *
     * and both are needed. Gate 1 alone would clear itself as soon as the IO
     * came back (refused by the user's arbitration); gate 2 alone would be
     * forgeable, since set_param/del_param accept any (io, param) pair without
     * a whitelist. Gate 1 is LIVE, derived and not stored anywhere, so no
     * client can write it.
     * ---------------------------------------------------------------- */

    /* Gate 1. True when a step rule that was registered here has been
     * destroyed under us, or when any rule of this scenario (steps, start,
     * stop, step_end, schedule start/stop) references an IO that does not
     * resolve - Rule::isDisabled(), the very mechanism of E4.2e.
     */
    bool isBroken() const;

    //The unresolved ids of every rule of this scenario, de-duplicated, in the
    //E4.2e format ("id_a, id_b"). Empty for a healthy scenario.
    string getMissingIoDescription() const;

    //Gate 2. Persisted in the `disabled_missing_io` param of the Scenario IO.
    bool isDisabledMissingIo() const { return disabledMissingIo; }

    /* Set or clear gate 2. Setting writes disabled_missing_io="true" on the
     * Scenario IO; clearing REMOVES the param instead of writing "false", so a
     * healthy scenario's io.xml is byte for byte the one it always was.
     * Only two callers are allowed: ListeRoom::refreshBrokenScenarios() (which
     * only ever SETS) and tryReenable() (which only ever CLEARS).
     */
    void setDisabledMissingIo(bool d);

    /* The manual re-enable the user asked for. Refuses - and says why, naming
     * the ids - while the scenario is still broken: a re-enable answering
     * "success" and then disabling itself again would be exactly the silent
     * no-op this ticket exists to remove, moved one level up.
     * On success the flag is cleared, an EventScenarioChanged is raised and the
     * caller is expected to persist io.xml.
     */
    bool tryReenable(string &errorOut);

    /* Bring a scenario that is running to a clean stop. Without it, a scenario
     * broken WHILE it runs stays "in progress" for ever: its step rules never
     * fire again, so ioStep is stuck, ruleStepEnd (which needs ioStep == -1)
     * never comes and _button_start (which needs ioIsActive == false) can never
     * restart it. A no-op when the scenario is not running.
     */
    void stopBrokenRun();

    //Try to categorize the scenario, returns either "light", "shutter", "other"
    //it can be mutliple category, like "light-shutter"
    string getCategory();

    Scenario *getIOScenario() { return ioScenario; }
    Internal *getIOIsActive() { return ioIsActive; }
    Internal *getIOScheduleEnabled() { return ioScheduleEnabled; }
    Internal *getIOStep() { return ioStep; }
    InputTimer *getIOTimer() { return ioTimer; }
    InPlageHoraire *getIOTimeRange() { return ioTimeRange; }

    Room *getRoomContainer() { return roomContainer; }

    /* Same signatures as ever: no caller has to change. They now answer the
     * *resolution* of the back-pointer, so null (or a compacted list) once
     * ListeRule has destroyed the rule under us.
     */
    Rule *getRuleStart() { return ruleStart.get(); }
    Rule *getRuleStop() { return ruleStop.get(); }
    Rule *getRuleStepEnd() { return ruleStepEnd.get(); }
    Rule *getRulePlageStart() { return rulePlageStart.get(); }
    Rule *getRulePlageStop() { return rulePlageStop.get(); }
    vector<Rule *> getRuleSteps();

    void addStep(double pause);
    void setStepPause(int step, double pause);
    void addStepAction(int step, IOBase *out, string action);
    double getStepPause(int step);
    int getStepActionCount(int step);
    ScenarioAction getStepAction(int step, int action);
    int getEndStepActionCount();
    ScenarioAction getEndStepAction(int action);

    void addSchedule();
    void deleteSchedule();
};

}

#endif // AUTOSCENARIO_H
