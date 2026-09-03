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

/* AutoScenario is a GENERATOR: it destroys the rules it wrote and rebuilds all
 * of them from AutoScenarioDef, which is the source of truth. Rules are a
 * projection - never read one to learn what the scenario is.
 *
 * Generated rules carry two markers: `auto_scenario` (the scenario id, which
 * older readers still expect) and `autoscenario_uid` (the definition uid).
 * Only the uid decides what may be destroyed; a rule without it was written by
 * somebody else and is never touched.
 *
 * PITFALL - the stand-down. A configuration written before the definition
 * existed carries rules with no uid and an io.xml with no definition
 * (configs/raoulh: 18 rules). While one such rule exists the generator does
 * nothing at all, so those rules survive and no duplicate is built beside them.
 * The test is on the RULES, not on the definition: a save mints a uid into
 * io.xml on its own, and that must not be enough to arm the generator over
 * rules it did not write. Only an explicit authoring call (addStep(),
 * addStepAction(), deleteRules(), addSchedule()...) takes ownership and
 * replaces them, which is what `autoscenario modify` has always done.
 */

class AutoScenario
{
private:
    //`auto_scenario` param of the Scenario IO. Also the prefix of the derived
    //ids of the machinery IOs, so it cannot be renamed on its own.
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

    /* Until the first successful build a scenario legitimately owns no rule at
     * all, and "fewer rules than declared" must not read as a breakage then.
     */
    bool rulesGenerated = false;

    //Owned by the Scenario IO, never null while it lives.
    AutoScenarioDef *definition() const;

    //Rules are looked up from their owner, never memorized: that is what makes
    //a dangling back-pointer impossible rather than merely unlikely.
    list<Rule *> scenarioRules() const;
    list<Rule *> generatedRules() const;
    //At least one rule of this scenario carries no uid - see the stand-down.
    bool hasLegacyRules() const;
    size_t expectedRuleCount() const;
    Rule *ruleOfType(const string &type) const;

    Rule *stepRule(int s) const;

    IOBase *createInput(string type, string id);
    /* False on a factory miss, or when an IO of the wrong type squats one of
     * the derived ids. The caller must abort BEFORE destroying anything, so a
     * refused build leaves the configuration as it found it.
     */
    bool prepareInternalIos();

    void addRuleCondition(Rule *rule, IOBase *input, string oper, string value);
    void addRuleAction(Rule *rule, IOBase *output, string value);
    /* An unresolved id is KEPT in the rule and the rule is marked as
     * referencing a missing IO, so the engine skips it instead of running an
     * amputated action list. Dropping it here is what used to lose it for good.
     */
    void addRuleActionById(Rule *rule, const string &ioId, const string &value);
    static string actionValueOn(Rule *rule, const string &ioId);

    //Ownership passes to ListeRule.
    Rule *newGeneratedRule(const string &name, const string &autoScenarioType);

    //`all` also takes the rules carrying no uid - only an authoring call may.
    void destroyRules(bool all);

    void declareDefinition();

    //`takeOwnership` is what an authoring call passes to defeat the stand-down.
    bool rebuildRules(bool takeOwnership);

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

    /* Destroys the rules of this scenario and regenerates all of them from the
     * definition. Idempotent: two consecutive calls produce the same rules.
     * False when the machinery IOs could not be built, and then nothing was
     * destroyed either.
     */
    bool rebuildRules() { return rebuildRules(false); }

    //Same thing under its historical name; there is nothing left to check.
    bool checkScenarioRules() { return rebuildRules(false); }
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

    /* Gate 1, a pure read. Three independent reasons, all three needed:
     *  - an action of the definition names an IO that does not resolve. Being
     *    definition-derived, no rules.xml round trip can whitewash it;
     *  - a live rule of the scenario is disabled (an IO deleted at runtime);
     *  - fewer rules carry our uid than the definition calls for, i.e. one was
     *    destroyed under us. This is the only reason that names no id.
     */
    bool isBroken() const;

    //The unresolved ids, de-duplicated, "id_a, id_b". Definition first (steps
    //in order, then the final step), then the live rules.
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

    //Empty while the scenario has declared nothing.
    string getScenarioUid() const;

    Scenario *getIOScenario() { return ioScenario; }
    Internal *getIOIsActive() { return ioIsActive; }
    Internal *getIOScheduleEnabled() { return ioScheduleEnabled; }
    Internal *getIOStep() { return ioStep; }
    InputTimer *getIOTimer() { return ioTimer; }
    InPlageHoraire *getIOTimeRange() { return ioTimeRange; }

    Room *getRoomContainer() { return roomContainer; }

    /* Lookups into ListeRule, not memorized pointers: a rule destroyed by its
     * owner simply stops being found. None of these mutates anything.
     */
    Rule *getRuleStart() const { return ruleOfType("button_start"); }
    Rule *getRuleStop() const { return ruleOfType("button_stop"); }
    Rule *getRuleStepEnd() const { return ruleOfType("step_end"); }
    Rule *getRulePlageStart() const { return ruleOfType("time_start"); }
    Rule *getRulePlageStop() const { return ruleOfType("time_stop"); }
    vector<Rule *> getRuleSteps() const;

    void addStep(double pause);
    void setStepPause(int step, double pause);
    void addStepAction(int step, IOBase *out, string action);
    //The final step is a step of its own, not an out of range index: an
    //integer sentinel drifting against a container index is how an action
    //placed after the final step used to disappear without a word.
    void addFinalStepAction(IOBase *out, string action);
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
