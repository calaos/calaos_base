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
