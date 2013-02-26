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
#include <ConditionScript.h>
#include <ListeRoom.h>

using namespace Calaos;

ConditionScript::ConditionScript():
        Condition(COND_SCRIPT)
{
        Utils::logger("rule.condition.script") << Priority::DEBUG << "ConditionScript::ConditionScript(): New Script condition" << log4cpp::eol;
}

ConditionScript::~ConditionScript()
{
        Utils::logger("rule.condition.script") << Priority::DEBUG << "ConditionScript::~ConditionScript(): Ok" << log4cpp::eol;
}

bool ConditionScript::Evaluate()
{
        return ScriptManager::Instance().ExecuteScript(script);
}

bool ConditionScript::LoadFromXml(TiXmlElement *node)
{
        TiXmlElement *sc_node = node->FirstChildElement();
        if (!sc_node) return false;

        for (;sc_node;sc_node = sc_node->NextSiblingElement())
        {
                if (sc_node->ValueStr() == "calaos:script")
                {
                        string type = "";
                        if (sc_node->Attribute("type"))
                                type = sc_node->Attribute("type");
                        if (type == "lua")
                        {
                                TiXmlText *tnode = dynamic_cast<TiXmlText *>(sc_node->FirstChild());

                                if (tnode)
                                        script = tnode->ValueStr();
                        }
                }
                else if (sc_node->ValueStr() == "calaos:input" &&
                         sc_node->Attribute("id"))
                {
                        string id = sc_node->Attribute("id");
                        Input *in = ListeRoom::Instance().get_input(id);
                        if (in) in_event.push_back(in);
                }
        }

        return true;
}

bool ConditionScript::SaveToXml(TiXmlElement *node)
{
        TiXmlElement *cond_node = new TiXmlElement("calaos:condition");
        cond_node->SetAttribute("type", "script");
        node->LinkEndChild(cond_node);

        for (uint i = 0;i < in_event.size();i++)
        {
                TiXmlElement *in_node = new TiXmlElement("calaos:input");
                in_node->SetAttribute("id", in_event[i]->get_param("id"));
                cond_node->LinkEndChild(in_node);
        }

        TiXmlElement *sc_node = new TiXmlElement("calaos:script");
        sc_node->SetAttribute("type", "lua");
        cond_node->LinkEndChild(sc_node);

        TiXmlText *txt_node = new TiXmlText(script);
        txt_node->SetCDATA(true);
        sc_node->LinkEndChild(txt_node);

        return true;
}
