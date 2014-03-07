/******************************************************************************
**  Copyright (c) 2006-2012, Calaos. All Rights Reserved.
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
#ifndef  AUTOSCENARIO_H
#define  AUTOSCENARIO_H

#include <Calaos.h>
#include <ListeRoom.h>
#include <ListeRule.h>
#include <IOFactory.h>
#include <ConditionStd.h>
#include <ActionStd.h>
#include <Room.h>

namespace Calaos
{

class ScenarioAction
{
public:
    Output *io;
    string action;
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
    InPlageHoraire *ioPlage;

    Room *roomContainer;

    Rule *ruleStart, *ruleStop, *ruleStepEnd;
    Rule *rulePlageStart, *rulePlageStop;
    vector<Rule *> ruleSteps;

    Input *createInput(string type, string id);
    bool checkCondition(Rule *rule, Input *input, string oper, string value);
    bool checkAction(Rule *rule, Output *output, string value);
    void addRuleCondition(Rule *rule, Input *input, string oper, string value);
    void addRuleAction(Rule *rule, Output *output, string value);
    void setRuleCondition(Rule *rule, Input *input, string oper, string value);
    void setRuleAction(Rule *rule, Output *output, string value);
    string getRuleConditionValue(Rule *rule, Input *input, string oper);
    string getRuleActionValue(Rule *rule, Output *output);
    list<Output*> getRuleRealActions(Rule *rule);

public:
    AutoScenario(Input *input);
    ~AutoScenario();

    static const int END_STEP = 0xFEDC1234;

    void checkScenarioRules();
    void deleteAll();
    void deleteRules();

    string getScenarioId() { return scenario_id; }
    bool isCycling() { return cycle; }
    bool isDisabled() { return disabled; }
    bool isScheduled() { return ioPlage?true:false; }
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
    InPlageHoraire *getIOPlage() { return ioPlage; }

    Room *getRoomContainer() { return roomContainer; }

    Rule *getRuleStart() { return ruleStart; }
    Rule *getRuleStop() { return ruleStop; }
    Rule *getRuleStepEnd() { return ruleStepEnd; }
    Rule *getRulePlageStart() { return rulePlageStart; }
    Rule *getRulePlageStop() { return rulePlageStop; }
    vector<Rule *> getRuleSteps() { return ruleSteps; }

    void addStep(double pause);
    void setStepPause(int step, double pause);
    void addStepAction(int step, Output *out, string action);
    double getStepPause(int step);
    int getStepActionCount(int step);
    ScenarioAction getStepAction(int step, int action);
    int getEndStepActionCount();
    ScenarioAction getEndStepAction(int action);
};

}

#endif // AUTOSCENARIO_H
