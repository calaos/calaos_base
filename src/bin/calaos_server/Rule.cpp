/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include "Rule.h"
#include "Rules/RulesFactory.h"

using namespace Calaos;

Rule::Rule(string type, string name):
    auto_sc_mark(false)
{
    params.Add("type", type);
    params.Add("name", name);

    cDebugDom("rule") << "Rule::Rule("
                      << type << "," << name << "): Ok";
}

Rule::~Rule()
{
    for (uint i = 0;i < conds.size();i++)
        delete conds[i];

    for (uint i = 0;i < actions.size();i++)
        delete actions[i];
}

void Rule::AddCondition(Condition *cond)
{
    conds.push_back(cond);

    cDebugDom("rule");
}

void Rule::AddAction(Action *act)
{
    actions.push_back(act);

    cDebugDom("rule");
}

bool Rule::Execute()
{
    cDebugDom("rule") << "Rule(" << get_param("type") << "," << get_param("name") << "): Trying execution...";

    if (CheckConditions())
        return ExecuteActions();

    return false;
}

bool Rule::CheckConditions()
{
    bool ret = true;

    for (Condition *condition: conds)
    {
        if (!condition->Evaluate())
            ret = false;
    }

    cDebugDom("rule") << "Rule(" << get_param("type") << "," << get_param("name") << "): checking conditions: " << (ret?"true":"false");

    return ret;
}

void Rule::CheckConditionsAsync(std::function<void (bool check)> cb)
{
    //this works only for scripts because they need
    //to be executed in separate process

    //first step is to get all non-async conditions and evaluate them
    //if one is failing, stops immediatly.
    //then we can start all scripts in parallel

    list<ConditionScript *> cond_scripts;

    for (Condition *condition: conds)
    {
        ConditionScript *script_cond = dynamic_cast<ConditionScript *>(condition);

        if (script_cond)
            cond_scripts.push_back(script_cond);
        else
        {
            if (!condition->Evaluate())
            {
                cb(false);
                return; //short path, return immediatly because on of the standard condition fails
            }
        }
    }

    //At this point all normal condition are evaluated and valid,
    //start all condition script
    typedef struct //_EvalResult
    {
        bool result = true;
        int remaining = 0;
    } EvalResult;

    EvalResult *res = new EvalResult;
    res->remaining = cond_scripts.size();

    for (ConditionScript *cond: cond_scripts)
    {
        cond->EvaluateAsync([=](bool eval)
        {
            if (!eval)
                res->result = false;
            res->remaining--;
            if (res->remaining <= 0)
            {
                bool r = res->result;
                delete res;
                cb(r);
            }
        });
    }
}

bool Rule::ExecuteActions()
{
    bool ret = true;

    cInfoDom("rule") << "Rule(" << get_param("type") << "," << get_param("name")
                     << "): Starting execution (" << actions.size() << " actions)";

    for (Action *action: actions)
    {
        if (!action->Execute())
            ret = false;
    }

    cInfoDom("rule") << "Rule(" << get_param("type") << "," << get_param("name")
                     << "): Execution done.";

    if (!ret)
        cWarningDom("rule") << "One or more Actions execution Failed !";

    return ret;
}

void Rule::RemoveCondition(int pos)
{
    vector<Condition *>::iterator iter = conds.begin();
    for (int i = 0;i < pos;iter++, i++) ;
    conds.erase(iter);

    cDebugDom("rule");
}

void Rule::RemoveAction(int pos)
{
    vector<Action *>::iterator iter = actions.begin();
    for (int i = 0;i < pos;iter++, i++) ;
    actions.erase(iter);

    cDebugDom("rule");
}

bool Rule::LoadFromXml(TiXmlElement *node)
{
    TiXmlAttribute *attr = node->ToElement()->FirstAttribute();

    for (; attr; attr = attr->Next())
    {
        if (string(attr->Name()) != "name" && string(attr->Name()) != "type")
            params.Add(attr->Name(), attr->ValueStr());
    }

    TiXmlElement *cnode = node->FirstChildElement();

    for (; cnode; cnode = cnode->NextSiblingElement())
    {
        if (cnode->ValueStr() == "calaos:condition")
        {
            Condition *cond = RulesFactory::CreateCondition(cnode);
            if (cond)
                AddCondition(cond);
        }
        else if (cnode->ValueStr() == "calaos:action")
        {
            Action *action = RulesFactory::CreateAction(cnode);
            if (action)
                AddAction(action);
        }
    }

    return true;
}

bool Rule::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *rule_node = new TiXmlElement("calaos:rule");

    for (int i = 0;i < params.size();i++)
    {
        string key, value;
        params.get_item(i, key, value);
        rule_node->SetAttribute(key, value);
    }

    node->LinkEndChild(rule_node);

    for (uint i = 0;i < conds.size();i++)
    {
        Condition *cond = conds[i];
        cond->SaveToXml(rule_node);
    }

    for (uint i = 0;i < actions.size();i++)
    {
        Action *action = actions[i];
        action->SaveToXml(rule_node);
    }

    return true;
}
