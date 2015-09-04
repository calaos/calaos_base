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
#ifndef S_RULE_H
#define S_RULE_H

#include "Calaos.h"
#include "Condition.h"
#include "Action.h"
#include "EcoreTimer.h"

using namespace std;

namespace Calaos
{

class Rule
{
protected:
    vector<Condition *> conds;
    vector<Action *> actions;

    Params params;

    bool auto_sc_mark; //true if rule is used by an auto_scenario

public:
    Rule(string _type, string _name);
    virtual ~Rule();

    void AddCondition(Condition *p);
    void AddAction(Action *p);
    bool Execute();
    bool CheckConditions();
    void CheckConditionsAsync(std::function<void (bool check)> cb);
    bool ExecuteActions();
    void RemoveCondition(int i);
    void RemoveAction(int i);

    Condition *get_condition(int i) { return conds[i]; }
    Action *get_action(int i) { return actions[i]; }

    int get_size_conds() { return conds.size(); }
    int get_size_actions() { return actions.size(); }

    string get_type() { return params["type"]; }
    string get_name() { return params["name"]; }
    string get_param(string p) { return params[p]; }
    void set_param(string p, string v) { params.Add(p, v); }
    bool param_exists(string p) { return params.Exists(p); }

    bool isAutoScenario() { return auto_sc_mark; }
    void setAutoScenario(bool m) { auto_sc_mark = m; }

    virtual bool LoadFromXml(TiXmlElement *node);
    virtual bool SaveToXml(TiXmlElement *node);
};

}
#endif
