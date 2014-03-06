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
#include <ConditionOutput.h>
#include <ListeRoom.h>

using namespace Calaos;

ConditionOutput::ConditionOutput():
        Condition(COND_OUTPUT)
{
        Utils::logger("rule.condition.output") << Priority::DEBUG << "ConditionOutput::ConditionOutput(): New output condition" << log4cpp::eol;
}

ConditionOutput::~ConditionOutput()
{
        Utils::logger("rule.condition.output") << Priority::DEBUG << "ConditionOutput::~ConditionOutput(): Ok" << log4cpp::eol;
}

bool ConditionOutput::Evaluate()
{
        string sval, oper;
        bool bval;
        double dval;
        bool ret = false;

        bool ovar = false;
        bool changed = false;
        switch (output->get_type())
        {
        case TBOOL:
                if (params_var != "")
                {
                        Output *out = ListeRoom::Instance().get_output(params_var);
                        if (out && out->get_type() == TBOOL)
                        {
                                bval = out->get_value_bool();
                                ovar = true;
                        }
                }

                if (!ovar)
                {
                        if (params == "true")
                                bval = true;
                        else if (params == "false")
                                bval = false;
                        else if (params == "changed")
                                changed = true;
                        else
                        {
                                Utils::logger("rule.condition.output") << Priority::WARN << "ConditionOutput::Evaluate(): get_value(bool) not bool !" << log4cpp::eol;
                                ret = false;
                                break;
                        }
                }

                if (!changed)
                {
                        ret = eval(output->get_value_bool(), ops, bval);
                }
                else
                {
                        ret = true;
                }
                break;
        case TINT:
                if (params_var != "")
                {
                        Output *out = ListeRoom::Instance().get_output(params_var);
                        if (out && out->get_type() == TINT)
                        {
                                dval = out->get_value_double();
                                sval = "ovar";
                                ovar = true;
                        }
                }

                if (!ovar)
                        sval = params;

                if (sval != "")
                {
                        if (!ovar)
                        {
                                if (sval == "changed")
                                        changed = true;
                                else
                                        from_string(sval, dval);
                        }

                        if (!changed)
                        {
                                ret = eval(output->get_value_double(), ops, dval);
                        }
                        else
                        {
                                ret = true;
                        }
                }
                else
                        Utils::logger("rule.condition.output") << Priority::WARN << "ConditionOutput::Evaluate(): get_value(int) not int !" << log4cpp::eol;
                break;
        case TSTRING:
                if (params_var != "")
                {
                        Output *out = ListeRoom::Instance().get_output(params_var);
                        if (out && out->get_type() == TSTRING)
                        {
                                sval = out->get_value_string();
                                ovar = true;
                        }
                }
                if (!ovar)
                {
                        sval = url_decode2(params);
                        if (sval == "changed")
                                changed = true;
                }

                if (!changed)
                {
                        ret = eval(output, ops, sval);
                }
                else
                {
                        ret = true;
                }
                break;
        default: break;
        }

        if (ret)
                Utils::logger("rule.condition.output") << Priority::DEBUG << "ConditionOutput::Evaluate(): Ok" << log4cpp::eol;
        else
                Utils::logger("rule.condition.output") << Priority::DEBUG << "ConditionOutput::Evaluate(): Failed !" << log4cpp::eol;

        return ret;
}

bool ConditionOutput::eval(bool val1, std::string oper, bool val2)
{
        if (oper != "!=" && oper != "==")
        {
                Utils::logger("rule.condition.output") << Priority::ERROR << "ConditionOutput::eval(bool): Invalid operator (" << oper << ")" << log4cpp::eol;
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

bool ConditionOutput::eval(double val1, std::string oper, double val2)
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

bool ConditionOutput::eval(std::string val1, std::string oper, std::string val2)
{
        if (oper != "!=" && oper != "==")
        {
                Utils::logger("rule.condition.output") << Priority::ERROR << "ConditionOutput::eval(string): Invalid operator (" << oper << ")" << log4cpp::eol;
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

bool ConditionOutput::eval(Output *out, string oper, string val)
{
        if (oper != "!=" && oper != "==")
        {
                Utils::logger("rule.condition.output") << Priority::ERROR << "ConditionOutput::eval(string): Invalid operator (" << oper << ")" << log4cpp::eol;
                return false;
        }

        return out->check_condition_value(val, oper == "==");

        return false;
}

bool ConditionOutput::LoadFromXml(TiXmlElement *node)
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
                if (node->ValueStr() == "calaos:output")
                {
                        string id = "", oper = "", val = "", val_var = "";

                        if (node->Attribute("id")) id = node->Attribute("id");
                        if (node->Attribute("oper")) oper = node->Attribute("oper");
                        if (node->Attribute("val")) val = node->Attribute("val");
                        if (node->Attribute("val_var")) val_var = node->Attribute("val_var");

                        Output *out = ListeRoom::Instance().get_output(id);
                        if (out)
                        {
                                setOutput(out);
                                params = val;
                                ops = oper;
                                if (val_var != "")
                                        params_var = val_var;
                        }
                        else
                        {
                                return false;
                        }
                }
        }

        return true;
}

bool ConditionOutput::SaveToXml(TiXmlElement *node)
{
        TiXmlElement *cond_node = new TiXmlElement("calaos:condition");
        cond_node->SetAttribute("type", "output");
        cond_node->SetAttribute("trigger", trigger?"true":"false");
        node->LinkEndChild(cond_node);

        TiXmlElement *cnode = new TiXmlElement("calaos:output");
        cnode->SetAttribute("id", output->get_param("id"));
        cnode->SetAttribute("oper", ops);
        cnode->SetAttribute("val", params);
        if (params_var != "")
                cnode->SetAttribute("val_var", params_var);

        cond_node->LinkEndChild(cnode);

        return true;
}
