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
#include <ActionStd.h>
#include <ListeRoom.h>
#include <ListeRule.h>
#include <WODigital.h>
#include <ActionTouchscreen.h>

using namespace Calaos;

ActionStd::~ActionStd()
{
    cDebugDom("rule.action.standard") <<  "ActionStd::~ActionStd(): Ok";
}

void ActionStd::Add(Output *out)
{
    outputs.push_back(out);

    cDebugDom("rule.action.standard") <<  "ActionStd::Add(): Output(" << out->get_param("id") << ") added";
}

bool ActionStd::Execute()
{
    std::string tmp;
    bool ret = true;
    std::string sval;
    bool bval;
    double dval;

    for (uint i = 0;i < outputs.size();i++)
    {
        bool ovar = false;
        switch (outputs[i]->get_type())
        {
        case TBOOL:
        {
            if (params_var[outputs[i]->get_param("id")] != "")
            {
                std::string var_id = params_var[outputs[i]->get_param("id")];
                Output *out = ListeRoom::Instance().get_output(var_id);
                if (out && out->get_type() == TBOOL)
                {
                    bval = out->get_value_bool();
                    ovar = true;
                }
            }

            if (ovar)
            {
                if (!outputs[i]->set_value(bval)) ret = false;
            }
            else if (params[outputs[i]->get_param("id")] == "true")
            {
                if (!outputs[i]->set_value(true)) ret = false;
            }
            else if (params[outputs[i]->get_param("id")] == "false")
            {
                if (!outputs[i]->set_value(false)) ret = false;
            }
            else
            {
                if (!outputs[i]->set_value(params[outputs[i]->get_param("id")])) ret = false;
            }
            break;
        }
        case TINT:
        {
            if (params_var[outputs[i]->get_param("id")] != "")
            {
                std::string var_id = params_var[outputs[i]->get_param("id")];
                Output *out = ListeRoom::Instance().get_output(var_id);
                if (out && out->get_type() == TINT)
                {
                    dval = out->get_value_double();
                    ovar = true;
                }
            }
            tmp = params[outputs[i]->get_param("id")];

            if (ovar)
            {
                if (!outputs[i]->set_value(dval)) ret = false;
            }
            else if (is_of_type<double>(tmp))
            {
                if (!outputs[i]->set_value(atof(tmp.c_str()))) ret = false;
            }
            else
            {
                if (!outputs[i]->set_value(tmp)) ret = false;
            }
            break;
        }
        case TSTRING:
        {
            if (params_var[outputs[i]->get_param("id")] != "")
            {
                std::string var_id = params_var[outputs[i]->get_param("id")];
                Output *out = ListeRoom::Instance().get_output(var_id);
                if (out && out->get_type() == TSTRING)
                {
                    sval = out->get_command_string();
                    ovar = true;
                }
            }
            tmp = params[outputs[i]->get_param("id")];

            if (ovar)
            {
                if (!outputs[i]->set_value(sval)) ret = false;
            }
            else if (tmp != "")
            {
                if (!outputs[i]->set_value(tmp)) ret = false;
            }

            break;
        }
        default: break;
        }
    }

    if (ret)
        cDebugDom("rule.action.standard") <<  "ActionStd::Execute(): Ok";
    else
        cErrorDom("rule.action.standard") <<  "ActionStd::Execute(): Failed !";

    return ret;
}

void ActionStd::Remove(int pos)
{
    vector<Output *>::iterator iter = outputs.begin();
    for (int i = 0;i < pos;iter++, i++) ;
    outputs.erase(iter);

    cDebugDom("rule.action.standard") <<  "ActionStd::Remove(): Ok";
}

void ActionStd::Assign(int i, Output *obj)
{
    outputs[i] = obj;
}

bool ActionStd::LoadFromXml(TiXmlElement *node)
{
    node = node->FirstChildElement();

    for (; node; node = node->NextSiblingElement())
    {
        if (node->ValueStr() == "calaos:output")
        {
            string id = "", val = "", val_var = "";

            if (node->Attribute("id")) id = node->Attribute("id");
            if (node->Attribute("val")) val = node->Attribute("val");
            if (node->Attribute("val_var")) val_var = node->Attribute("val_var");

            if (id == "OutTouchscreen" && ListeRule::Instance().size() > 0)
            {
                cInfoDom("rule.action.standard") <<  "ActionStd::LoadFromXml(): Converting old OutTouchscreen to new ActionTouchscreen";
                Rule *rule = ListeRule::Instance().get_rule(ListeRule::Instance().size() - 1);
                ActionTouchscreen *action = new ActionTouchscreen(val);
                rule->AddAction(dynamic_cast<Action *>(action));
            }

            Output *out = ListeRoom::Instance().get_output(id);
            if (out)
            {
                Add(out);
                params.Add(id, val);
                if (val_var != "")
                    params_var.Add(id, val_var);
            }
            else
            {
                return false;
            }
        }
    }

    return true;
}

bool ActionStd::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *action_node = new TiXmlElement("calaos:action");
    action_node->SetAttribute("type", "standard");
    node->LinkEndChild(action_node);

    for (uint i = 0;i < outputs.size();i++)
    {
        Output *out = outputs[i];

        TiXmlElement *cnode = new TiXmlElement("calaos:output");

        cnode->SetAttribute("id", out->get_param("id"));
        cnode->SetAttribute("val", params[out->get_param("id")]);
        if (params_var[out->get_param("id")] != "")
            cnode->SetAttribute("val_var", params_var[out->get_param("id")]);

        action_node->LinkEndChild(cnode);
    }

    return true;
}
