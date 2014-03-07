/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
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
#include <ActionScript.h>

using namespace Calaos;

ActionScript::ActionScript(): Action(ACTION_SCRIPT)
{
    cDebugDom("rule.action.script") <<  "ActionScript::ActionScript(): New Script action";
}

ActionScript::~ActionScript()
{
    cDebugDom("rule.action.script") <<  "ActionScript::~ActionScript(): Ok";
}

bool ActionScript::Execute()
{
    return ScriptManager::Instance().ExecuteScript(script);
}

bool ActionScript::LoadFromXml(TiXmlElement *pnode)
{
    TiXmlElement *sc_node = pnode->FirstChildElement("calaos:script");
    if (!sc_node) return false;

    string type = "";
    if (sc_node->Attribute("type"))
        type = sc_node->Attribute("type");
    if (type == "lua")
    {
        TiXmlText *tnode = dynamic_cast<TiXmlText *>(sc_node->FirstChild());

        if (tnode)
            script = tnode->ValueStr();
    }

    return true;
}

bool ActionScript::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *action_node = new TiXmlElement("calaos:action");
    action_node->SetAttribute("type", "script");
    node->LinkEndChild(action_node);

    TiXmlElement *sc_node = new TiXmlElement("calaos:script");
    sc_node->SetAttribute("type", "lua");
    action_node->LinkEndChild(sc_node);

    TiXmlText *txt_node = new TiXmlText(script);
    txt_node->SetCDATA(true);
    sc_node->LinkEndChild(txt_node);

    return true;
}
