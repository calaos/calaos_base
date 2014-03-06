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
#include <ConditionStd.h>
#include <ListeRoom.h>

using namespace Calaos;

ConditionStd::ConditionStd():
        Condition(COND_STD)
{
        Utils::logger("rule.condition.standard") << Priority::DEBUG << "ConditionStd::ConditionStd(): New standard condition" << log4cpp::eol;
}

ConditionStd::~ConditionStd()
{
        Utils::logger("rule.condition.standard") << Priority::DEBUG << "ConditionStd::~ConditionStd(): Ok" << log4cpp::eol;
}

void ConditionStd::Add(Input *in)
{
        inputs.push_back(in);

        Utils::logger("rule.condition.standard") << Priority::DEBUG << "ConditionStd::Add(): Input(" << in->get_param("id") << ") added" << log4cpp::eol;
}

void ConditionStd::getVarIds(vector<Input *> &list)
{
        for (uint i = 0;i < inputs.size();i++)
        {
                std::string var_id = params_var[inputs[i]->get_param("id")];
                if (var_id.empty()) continue;

                Input *in = ListeRoom::Instance().get_input(var_id);

                if (in)
                {
                        list.push_back(in);
                }
        }
}

bool ConditionStd::Evaluate()
{
        std::string sval, oper;
        bool bval = false;
        double dval = 0.0;
        bool ret = false;

        for (uint i = 0;i < inputs.size();i++)
        {
                bool ovar = false;
                bool changed = false;
                switch (inputs[i]->get_type())
                {
                  case TBOOL:
                        if (params_var[inputs[i]->get_param("id")] != "")
                        {
                                std::string var_id = params_var[inputs[i]->get_param("id")];
                                Input *in = ListeRoom::Instance().get_input(var_id);
                                if (in && in->get_type() == TBOOL)
                                {
                                        bval = in->get_value_bool();
                                        ovar = true;
                                }
                        }

                        if (!ovar)
                        {
                                if (params[inputs[i]->get_param("id")] == "true")
                                        bval = true;
                                else if (params[inputs[i]->get_param("id")] == "false")
                                        bval = false;
                                else if (params[inputs[i]->get_param("id")] == "changed")
                                        changed = true;
                                else
                                {
                                        Utils::logger("rule.condition.standard") << Priority::WARN << "ConditionStd::Evaluate(): get_value(bool) not bool !" << log4cpp::eol;
                                        ret = false;
                                        break;
                                }
                        }

                        if (!changed)
                        {
                                oper = ops[inputs[i]->get_param("id")];
                                ret = eval(inputs[i]->get_value_bool(), oper, bval);
                        }
                        else
                        {
                                ret = true;
                        }
                        break;
                  case TINT:
                        if (params_var[inputs[i]->get_param("id")] != "")
                        {
                                std::string var_id = params_var[inputs[i]->get_param("id")];
                                Input *in = ListeRoom::Instance().get_input(var_id);
                                if (in && in->get_type() == TINT)
                                {
                                        dval = in->get_value_double();
                                        sval = "ovar";
                                        ovar = true;
                                }
                        }

                        if (!ovar)
                                sval = params[inputs[i]->get_param("id")];

                        if (sval != "")
                        {
                                if (!ovar)
                                {
                                        if (sval == "changed")
                                                changed = true;
                                        else
                                                dval = atof(sval.c_str());
                                }

                                if (!changed)
                                {
                                        oper = ops[inputs[i]->get_param("id")];
                                        ret = eval(inputs[i]->get_value_double(), oper, dval);
                                }
                                else
                                {
                                        ret = true;
                                }
                        }
                        else
                                Utils::logger("rule.condition.standard") << Priority::WARN << "ConditionStd::Evaluate(): get_value(int) not int !" << log4cpp::eol;
                        break;
                  case TSTRING:
                        if (params_var[inputs[i]->get_param("id")] != "")
                        {
                                std::string var_id = params_var[inputs[i]->get_param("id")];
                                Input *in = ListeRoom::Instance().get_input(var_id);
                                if (in && in->get_type() == TSTRING)
                                {
                                        sval = in->get_value_string();
                                        ovar = true;
                                }
                        }
                        if (!ovar)
                        {
                                sval = Utils::url_decode2(params[inputs[i]->get_param("id")]);
                                if (sval == "changed") changed = true;
                        }

                        if (!changed)
                        {
                                oper = ops[inputs[i]->get_param("id")];
                                ret = eval(inputs[i]->get_value_string(), oper, sval);
                        }
                        else
                        {
                                ret = true;
                        }
                        break;
                  default: break;
                }
        }

        if (ret)
                Utils::logger("rule.condition.standard") << Priority::DEBUG << "ConditionStd::Evaluate(): Ok" << log4cpp::eol;
        else
                Utils::logger("rule.condition.standard") << Priority::DEBUG << "ConditionStd::Evaluate(): Failed !" << log4cpp::eol;

        return ret;
}

void ConditionStd::Remove(int pos)
{
        vector<Input *>::iterator iter = inputs.begin();
        for (int i = 0;i < pos;iter++, i++) ;
        inputs.erase(iter);

        Utils::logger("rule.condition.standard") << Priority::DEBUG << "ConditionStd::Remove(): Ok" << log4cpp::eol;
}

void ConditionStd::Assign(int i, Input *obj)
{
        inputs[i] = obj;
}

bool ConditionStd::eval(bool val1, std::string oper, bool val2)
{
        if (oper != "!=" && oper != "==")
        {
                Utils::logger("rule.condition.standard") << Priority::ERROR << "ConditionStd::eval(bool): Invalid operator (" << oper << ")" << log4cpp::eol;
                return false;
        }

        if (oper == "==")
        {
                if (val1 == val2)
                        return true;
                else
                        return false;
        }

        if (oper == "!=")
        {
                if (val1 != val2)
                        return true;
                else
                        return false;
        }

        return false;
}

bool ConditionStd::eval(double val1, std::string oper, double val2)
{
        if (oper == "==")
        {
                if (val1 == val2)
                        return true;
                else
                        return false;
        }

        if (oper == "!=")
        {
                if (val1 != val2)
                        return true;
                else
                        return false;
        }

        if (oper == "SUP")
        {
                if (val1 > val2)
                        return true;
                else
                        return false;
        }

        if (oper == "SUP=")
        {
                if (val1 >= val2)
                        return true;
                else
                        return false;
        }

        if (oper == "INF")
        {
                if (val1 < val2)
                        return true;
                else
                        return false;
        }

        if (oper == "INF=")
        {
                if (val1 <= val2)
                        return true;
                else
                        return false;
        }

        return false;
}

bool ConditionStd::eval(std::string val1, std::string oper, std::string val2)
{
        if (oper != "!=" && oper != "==")
        {
                Utils::logger("rule.condition.standard") << Priority::ERROR << "ConditionStd::eval(string): Invalid operator (" << oper << ")" << log4cpp::eol;
                return false;
        }

        if (oper == "==")
        {
                if (val1 == val2)
                        return true;
                else
                        return false;
        }

        if (oper == "!=")
        {
                if (val1 != val2)
                        return true;
                else
                        return false;
        }

        return false;
}

bool ConditionStd::LoadFromXml(TiXmlElement *node)
{
        if (node->Attribute("trigger"))
        {
                if (node->Attribute("trigger") == string("true"))
                        trigger = true;
                else if (node->Attribute("trigger") == string("false"))
                        trigger = false;
        }

        node = node->FirstChildElement();

        for (; node; node = node->NextSiblingElement())
        {
                if (node->ValueStr() == "calaos:input")
                {
                        string id = "", oper = "", val = "", val_var = "";

                        if (node->Attribute("id")) id = node->Attribute("id");
                        if (node->Attribute("oper")) oper = node->Attribute("oper");
                        if (node->Attribute("val")) val = node->Attribute("val");
                        if (node->Attribute("val_var")) val_var = node->Attribute("val_var");

                        Input *in = ListeRoom::Instance().get_input(id);
                        if (in)
                        {
                                Add(in);
                                params.Add(id, val);
                                ops.Add(id, oper);
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

bool ConditionStd::SaveToXml(TiXmlElement *node)
{
        TiXmlElement *cond_node = new TiXmlElement("calaos:condition");
        cond_node->SetAttribute("type", "standard");
        cond_node->SetAttribute("trigger", trigger?"true":"false");
        node->LinkEndChild(cond_node);

        for (uint i = 0;i < inputs.size();i++)
        {
                Input *in = inputs[i];

                TiXmlElement *cnode = new TiXmlElement("calaos:input");

                cnode->SetAttribute("id", in->get_param("id"));
                cnode->SetAttribute("oper", ops[in->get_param("id")]);
                cnode->SetAttribute("val", params[in->get_param("id")]);
                if (params_var[in->get_param("id")] != "")
                        cnode->SetAttribute("val_var", params_var[in->get_param("id")]);

                cond_node->LinkEndChild(cnode);
        }

        return true;
}
