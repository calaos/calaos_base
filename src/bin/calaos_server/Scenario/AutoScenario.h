/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
    IOBase *io;
    std::string action;
};

class AutoScenario
{
private:
    std::string scenario_id;
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

    Rule *ruleStart, *ruleStop, *ruleStepEnd;
    Rule *rulePlageStart, *rulePlageStop;
    std::vector<Rule *> ruleSteps;

    IOBase *createInput(std::string type, std::string id);
    bool checkCondition(Rule *rule, IOBase *input, std::string oper, std::string value);
    bool checkAction(Rule *rule, IOBase *output, std::string value);
    void addRuleCondition(Rule *rule, IOBase *input, std::string oper, std::string value);
    void addRuleAction(Rule *rule, IOBase *output, std::string value);
    void setRuleCondition(Rule *rule, IOBase *input, std::string oper, std::string value);
    void setRuleAction(Rule *rule, IOBase *output, std::string value);
    std::string getRuleConditionValue(Rule *rule, IOBase *input, std::string oper);
    std::string getRuleActionValue(Rule *rule, IOBase *output);
    std::list<IOBase *> getRuleRealActions(Rule *rule);
    void createRuleStepEnd();

public:
    AutoScenario(IOBase *input);
    ~AutoScenario();

    static const int END_STEP = 0xFEDC1234;

    void checkScenarioRules();
    void deleteAll();
    void deleteRules();

    std::string getScenarioId() { return scenario_id; }
    bool isCycling() { return cycle; }
    bool isDisabled() { return disabled; }
    bool isScheduled() { return ioTimeRange?true:false; }
    void setCycling(bool c); //should call checkScenarioRules() to commit changes
    void setDisabled(bool d); //should call checkScenarioRules() to commit changes

    //Try to categorize the scenario, returns either "light", "shutter", "other"
    //it can be mutliple category, like "light-shutter"
    std::string getCategory();

    Scenario *getIOScenario() { return ioScenario; }
    Internal *getIOIsActive() { return ioIsActive; }
    Internal *getIOScheduleEnabled() { return ioScheduleEnabled; }
    Internal *getIOStep() { return ioStep; }
    InputTimer *getIOTimer() { return ioTimer; }
    InPlageHoraire *getIOTimeRange() { return ioTimeRange; }

    Room *getRoomContainer() { return roomContainer; }

    Rule *getRuleStart() { return ruleStart; }
    Rule *getRuleStop() { return ruleStop; }
    Rule *getRuleStepEnd() { return ruleStepEnd; }
    Rule *getRulePlageStart() { return rulePlageStart; }
    Rule *getRulePlageStop() { return rulePlageStop; }
    std::vector<Rule *> getRuleSteps() { return ruleSteps; }

    void addStep(double pause);
    void setStepPause(int step, double pause);
    void addStepAction(int step, IOBase *out, std::string action);
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
