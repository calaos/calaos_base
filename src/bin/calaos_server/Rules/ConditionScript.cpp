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
#include "ConditionScript.h"
#include "ListeRoom.h"

using namespace Calaos;

ConditionScript::ConditionScript():
    Condition(COND_SCRIPT)
{
    cDebugDom("rule.condition.script") <<  "New Script condition";
}

ConditionScript::~ConditionScript()
{
}

bool ConditionScript::Evaluate()
{
    cError() << "Scripts needs to be evaluated using EvaluateAsync() !";
    return false;
}

void ConditionScript::EvaluateAsync(std::function<void(bool eval)> cb, string triggerId)
{
    ScriptExec::ExecuteScriptDetached(script, [=](bool ret)
    {
        cInfoDom("rule.condition.script") << "Script finished with " << (ret?"true":"false");
        cb(ret);
    },
    {{ "trigger_id", triggerId }});
}

void ConditionScript::addTriggerIO(IOBase *io)
{
    if (!io)
    {
        cErrorDom("rule.condition.script") << "addTriggerIO(): ignoring a null IO";
        return;
    }

    addTriggerId(io->get_param("id"));
}

void ConditionScript::addTriggerId(const std::string &id)
{
    if (id.empty())
    {
        cErrorDom("rule.condition.script") << "addTriggerId(): ignoring an empty id";
        return;
    }

    //The map this replaces deduplicated by construction, keep that
    if (containsTriggerId(id)) return;

    inEventIds.push_back(id);
}

bool ConditionScript::containsTriggerIO(IOBase *io)
{
    if (!io) return false;

    return containsTriggerId(io->get_param("id"));
}

bool ConditionScript::containsTriggerId(const std::string &id) const
{
    if (id.empty()) return false;

    return std::find(inEventIds.begin(), inEventIds.end(), id) != inEventIds.end();
}

const std::string &ConditionScript::getTriggerId(int i) const
{
    static const std::string empty;

    if (i < 0 || i >= (int)inEventIds.size()) return empty;

    return inEventIds[i];
}

bool ConditionScript::LoadFromXml(pugi::xml_node node)
{
    pugi::xml_node sc_node = XmlUtils::firstChildElement(node);
    if (!sc_node) return false;

    for (;sc_node;sc_node = XmlUtils::nextSiblingElement(sc_node))
    {
        if (string(sc_node.name()) == "calaos:script")
        {
            string type = "";
            if (sc_node.attribute("type"))
                type = sc_node.attribute("type").as_string();
            if (type == "lua")
            {
                if (XmlUtils::hasText(sc_node))
                    script = XmlUtils::text(sc_node);
            }
        }
        else if (string(sc_node.name()) == "calaos:input" &&
                 sc_node.attribute("id"))
        {
            string id = sc_node.attribute("id").as_string();
            //Load-time contract, unchanged: an id that resolves is kept, one
            //that does not is dropped here (this condition type never made the
            //whole load fail). What changes is that the kept reference is the
            //id, so it cannot dangle if the IO disappears later.
            IOBase *in = ListeRoom::Instance().findIO(id);
            if (in)
                addTriggerIO(in);
            else
                cErrorDom("rule.condition.script")
                        << "Trigger input '" << id << "' is unknown, ignored";
        }
    }

    return true;
}

bool ConditionScript::SaveToXml(pugi::xml_node node)
{
    pugi::xml_node cond_node = node.append_child("calaos:condition");
    XmlUtils::setAttribute(cond_node, "type", "script");

    for (const std::string &id: inEventIds)
    {
        pugi::xml_node in_node = cond_node.append_child("calaos:input");
        XmlUtils::setAttribute(in_node, "id", id);
    }

    pugi::xml_node sc_node = cond_node.append_child("calaos:script");
    XmlUtils::setAttribute(sc_node, "type", "lua");

    XmlUtils::appendCData(sc_node, script);

    return true;
}
