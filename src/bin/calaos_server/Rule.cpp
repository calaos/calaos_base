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
#include <algorithm>

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
    /* The conditions and the actions are owned by the vectors now, so this
     * body only exists to pin the ORDER the old one had: conditions first,
     * front to back, then actions. Left to the implicit destruction of the
     * members, actions would go first (members are destroyed in reverse
     * declaration order). Nothing observable depends on it today, but this
     * is the destruction order the tree has always had.
     */
    conds.clear();
    actions.clear();
}

void Rule::AddCondition(Condition *cond)
{
    //Ownership transfer in
    conds.emplace_back(cond);

    /* E4.2e: an unresolvable reference disables the whole rule. Done here and
     * not only in LoadFromXml() so that every construction path (the XML
     * factory, the JSON API, AutoScenario) goes through the same gate.
     *
     * This is a SNAPSHOT taken at add time, not a live view: a condition
     * populated AFTER being added to the rule is not re-read. Harmless for the
     * two call sites of the tree - RulesFactory hands over a fully loaded
     * condition, and AutoScenario does add-then-populate but only ever with
     * IOs it has just created, so it can never carry a missing one - but a
     * future add-then-fill-from-config call site would have to re-run this.
     */
    if (cond)
        collectMissingIo(cond->getMissingIoIds());

    cDebugDom("rule");
}

void Rule::AddAction(Action *act)
{
    //Ownership transfer in
    actions.emplace_back(act);

    //Same as AddCondition(): an action pointing at an IO that is not there
    //disables the rule
    if (act)
        collectMissingIo(act->getMissingIoIds());

    cDebugDom("rule");
}

void Rule::collectMissingIo(const vector<string> &ids)
{
    for (const string &id: ids)
    {
        if (id.empty()) continue;
        if (std::find(missingIoIds.begin(), missingIoIds.end(), id) != missingIoIds.end())
            continue;
        missingIoIds.push_back(id);
    }
}

string Rule::getMissingIoDescription() const
{
    string desc;

    for (const string &id: missingIoIds)
    {
        if (!desc.empty()) desc += ", ";
        desc += id;
    }

    return desc;
}

bool Rule::Execute()
{
    //E4.2e: a rule missing one of its IOs does nothing at all. CheckConditions()
    //already refuses, this is only here to say why in the log.
    if (isDisabled())
    {
        cWarningDom("rule") << "Rule(" << get_param("type") << "," << get_param("name")
                            << "): DISABLED (missing IO: " << getMissingIoDescription()
                            << "), not executed";
        return false;
    }

    cDebugDom("rule") << "Rule(" << get_param("type") << "," << get_param("name") << "): Trying execution...";

    if (CheckConditions())
        return ExecuteActions();

    return false;
}

bool Rule::CheckConditions()
{
    /* E4.2e: fail closed. This is the entry point every dispatch goes through
     * (ListeRule::collectTriggeredRules() calls it directly), and answering
     * "true" for a rule whose criteria are incomplete is precisely the danger
     * this ticket exists for. Note that a rule whose conditions were ALL
     * rejected used to answer true here, for zero condition. */
    if (isDisabled())
    {
        cWarningDom("rule") << "Rule(" << get_param("type") << "," << get_param("name")
                            << "): DISABLED (missing IO: " << getMissingIoDescription()
                            << "), conditions are not evaluated";
        return false;
    }

    bool ret = true;

    for (const std::unique_ptr<Condition> &condition: conds)
    {
        if (!condition->Evaluate())
            ret = false;
    }

    cDebugDom("rule") << "Rule(" << get_param("type") << "," << get_param("name") << "): checking conditions: " << (ret?"true":"false");

    return ret;
}

void Rule::CheckConditionsAsync(std::function<void (bool check)> cb, string triggerId)
{
    //E4.2e: same gate as CheckConditions(), before any script is spawned
    if (isDisabled())
    {
        cWarningDom("rule") << "Rule(" << get_param("type") << "," << get_param("name")
                            << "): DISABLED (missing IO: " << getMissingIoDescription()
                            << "), script conditions are not evaluated";
        cb(false);
        return;
    }

    //this works only for scripts because they need
    //to be executed in separate process

    //first step is to get all non-async conditions and evaluate them
    //if one is failing, stops immediatly.
    //then we can start all scripts in parallel

    list<ConditionScript *> cond_scripts;

    for (const std::unique_ptr<Condition> &condition: conds)
    {
        ConditionScript *script_cond = dynamic_cast<ConditionScript *>(condition.get());

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
        }, triggerId);
    }
}

bool Rule::ExecuteActions()
{
    /* E4.2e: the last gate, and the one that really matters. ListeRule runs the
     * actions of an already-collected rule through here without re-checking
     * anything (executeTrigger(), executeActionsLocked()), so this is what
     * guarantees a disabled rule cannot act. */
    if (isDisabled())
    {
        cWarningDom("rule") << "Rule(" << get_param("type") << "," << get_param("name")
                            << "): DISABLED (missing IO: " << getMissingIoDescription()
                            << "), actions are not executed";
        return false;
    }

    bool ret = true;

    cInfoDom("rule") << "Rule(" << get_param("type") << "," << get_param("name")
                     << "): Starting execution (" << actions.size() << " actions)";

    for (const std::unique_ptr<Action> &action: actions)
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
    if (pos < 0 || (size_t)pos >= conds.size())
    {
        cErrorDom("rule") << "RemoveCondition(): no condition at index " << pos
                          << " (" << conds.size() << " conditions), ignoring";
        return;
    }

    /* Take the owner OUT of the vector before anything can be destroyed
     * (E4.2b lesson, same as Room::RemoveIO()): vector::erase move-assigns
     * the tail down and unique_ptr::operator= is reset(u.release()), so a
     * destructor running inside erase() would see a container of unchanged
     * size whose slot `pos` already holds the NEXT condition and whose last
     * slot is null.
     * The condition is destroyed here, it is never handed back: nothing in
     * the tree takes a Condition out of a Rule (this used to erase the raw
     * pointer without deleting it, i.e. leak it - there is no caller, so the
     * fix is unobservable).
     */
    std::unique_ptr<Condition> owned = std::move(conds[pos]);
    conds.erase(conds.begin() + pos);

    cDebugDom("rule");
}

void Rule::RemoveAction(int pos)
{
    if (pos < 0 || (size_t)pos >= actions.size())
    {
        cErrorDom("rule") << "RemoveAction(): no action at index " << pos
                          << " (" << actions.size() << " actions), ignoring";
        return;
    }

    //Same as RemoveCondition(): out of the vector first, then destroyed
    std::unique_ptr<Action> owned = std::move(actions[pos]);
    actions.erase(actions.begin() + pos);

    cDebugDom("rule");
}

bool Rule::LoadFromXml(pugi::xml_node node)
{
    for (pugi::xml_attribute attr: node.attributes())
    {
        if (string(attr.name()) != "name" && string(attr.name()) != "type")
            params.Add(attr.name(), attr.value());
    }

    pugi::xml_node cnode = XmlUtils::firstChildElement(node);

    for (; cnode; cnode = XmlUtils::nextSiblingElement(cnode))
    {
        if (string(cnode.name()) == "calaos:condition")
        {
            Condition *cond = RulesFactory::CreateCondition(cnode);
            if (cond)
                AddCondition(cond);
        }
        else if (string(cnode.name()) == "calaos:action")
        {
            Action *action = RulesFactory::CreateAction(cnode);
            if (action)
                AddAction(action);
        }
    }

    /* E4.2e. The two rejection causes are distinguished here, by construction:
     *   - RulesFactory returned NULL          -> malformed node or unknown
     *                                            type, dropped as it always
     *                                            was, the rule stays enabled,
     *   - it returned an object flagged with
     *     hasMissingIo()                      -> the object is kept (so the
     *                                            save loses nothing) and the
     *                                            rule is disabled here.
     */
    if (isDisabled())
    {
        cErrorDom("rule") << "Rule '" << get_name() << "' is DISABLED: it references "
                          << missingIoIds.size() << " IO(s) that do not exist ("
                          << getMissingIoDescription() << "). The rule is kept in the "
                          << "configuration and saved untouched, but it will never be "
                          << "triggered, evaluated nor executed: running it would act "
                          << "on incomplete criteria.";
    }

    return true;
}

bool Rule::SaveToXml(pugi::xml_node node)
{
    pugi::xml_node rule_node = node.append_child("calaos:rule");

    for (int i = 0;i < params.size();i++)
    {
        string key, value;
        params.get_item(i, key, value);
        XmlUtils::setAttribute(rule_node, key, value);
    }

    for (uint i = 0;i < conds.size();i++)
        conds[i]->SaveToXml(rule_node);

    for (uint i = 0;i < actions.size();i++)
        actions[i]->SaveToXml(rule_node);

    return true;
}
