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
#include "ConditionScript.h"
#include "ListeRoom.h"

namespace Calaos {

ConditionScript::ConditionScript():
    Condition(COND_SCRIPT)
{
    cDebugDom("rule.condition.script") <<  "New Script condition";
}

ConditionScript::~ConditionScript()
{
    cDebugDom("rule.condition.script");
}

bool ConditionScript::Evaluate()
{
    cError() << "Scripts needs to be evaluated using EvaluateAsync() !";
    return false;
}

void ConditionScript::EvaluateAsync(std::function<void(bool eval)> cb)
{
    ScriptExec::ExecuteScriptDetached(script, [=](bool ret)
    {
        cInfoDom("rule.condition.script") << "Script finished with " << (ret?"true":"false");
        cb(ret);
    });
}

bool ConditionScript::containsTriggerIO(IOBase *io)
{
    auto it = in_event.find(io);
    return it != in_event.end();
}

bool ConditionScript::LoadFromXml(TiXmlElement *node)
{
    TiXmlElement *sc_node = node->FirstChildElement();
    if (!sc_node) return false;

    for (;sc_node;sc_node = sc_node->NextSiblingElement())
    {
        if (sc_node->ValueStr() == "calaos:script")
        {
            std::string type = "";
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
            std::string id = sc_node->Attribute("id");
            IOBase *in = ListeRoom::Instance().get_io(id);
            if (in)
                in_event[in] = in;
        }
    }

    return true;
}

bool ConditionScript::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *cond_node = new TiXmlElement("calaos:condition");
    cond_node->SetAttribute("type", "script");
    node->LinkEndChild(cond_node);

    for (auto it: in_event)
    {
        IOBase *io = it.first;
        TiXmlElement *in_node = new TiXmlElement("calaos:input");
        in_node->SetAttribute("id", io->get_param("id"));
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

}
