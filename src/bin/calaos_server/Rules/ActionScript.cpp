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
#include "ActionScript.h"

using namespace Calaos;

ActionScript::ActionScript(): Action(ACTION_SCRIPT)
{
    cDebugDom("rule.action.script") <<  "New Script action";
}

ActionScript::~ActionScript()
{
}

bool ActionScript::Execute()
{
    ScriptExec::ExecuteScriptDetached(script, [=](bool ret)
    {
        cInfoDom("rule.action.script") << "Script finished with " << (ret?"true":"false");
    });

    return true;
}

bool ActionScript::LoadFromXml(pugi::xml_node pnode)
{
    pugi::xml_node sc_node = pnode.child("calaos:script");
    if (!sc_node) return false;

    string type = "";
    if (sc_node.attribute("type"))
        type = sc_node.attribute("type").as_string();
    if (type == "lua")
    {
        if (XmlUtils::hasText(sc_node))
            script = XmlUtils::text(sc_node);
    }

    return true;
}

bool ActionScript::SaveToXml(pugi::xml_node node)
{
    pugi::xml_node action_node = node.append_child("calaos:action");
    XmlUtils::setAttribute(action_node, "type", "script");

    pugi::xml_node sc_node = action_node.append_child("calaos:script");
    XmlUtils::setAttribute(sc_node, "type", "lua");

    XmlUtils::appendCData(sc_node, script);

    return true;
}
