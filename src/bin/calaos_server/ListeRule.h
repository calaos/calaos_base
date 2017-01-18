/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
    std::vector<Rule *> rules;

    //these input's events are detected in the RunEventLoop() function
    std::vector<IOBase *> in_event;

    //Rules for autoscenario
    list<Rule *> rules_scenarios;

    bool loop = false;

    bool execInProgress = false;

    ListeRule()
    { }

public:
    //singleton
    static ListeRule &Instance();

    ~ListeRule();

    void Add(Rule *p);
    void Remove(int i);
    void Remove(Rule *obj)
    { rules.erase(std::remove(rules.begin(), rules.end(), obj), rules.end());
        rules_scenarios.erase(std::remove(rules_scenarios.begin(), rules_scenarios.end(), obj), rules_scenarios.end()); delete obj; }
    void RemoveRule(IOBase *obj); //remove all rules containing obj

    void updateAllRulesToInput(IOBase *oldio, IOBase *newio);
    void updateAllRulesToOutput(IOBase *oldio, IOBase *newio);

    Rule *get_rule(int i);
    Rule *operator[] (int i) const;

    void Add(IOBase *io) { in_event.push_back(io); }
    void Remove(IOBase *io)
    { in_event.erase(std::remove(in_event.begin(), in_event.end(), io), in_event.end()); }
    //Run a loop to detect event from inputs when time or temperature changes
    void RunEventLoop();
    void StopLoop();

    int size() { return rules.size(); }

    //Execute all rules where the input 'input_id' is used
    //The function is called only when a signal is emited from inputs
    virtual void ExecuteRuleSignal(std::string id);

    /* This executes all rules at program startup. All rules with ConditionStart
                 * will be evaluated and executed (only once)
                 */
    void ExecuteStartRules();

    list<Rule *> getRuleAutoScenario(string auto_scenario);
};

}
#endif
