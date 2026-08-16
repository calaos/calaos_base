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
#include "ConditionOutput.h"
#include "ConditionStd.h"
#include "ListeRoom.h"

using namespace Calaos;

ConditionOutput::ConditionOutput():
    Condition(COND_OUTPUT)
{
    cDebugDom("rule.condition.output") <<  "New output condition";
}

ConditionOutput::~ConditionOutput()
{
}

void ConditionOutput::setOutput(IOBase *p)
{
    if (!p)
    {
        cErrorDom("rule.condition.output") << "setOutput(): clearing, null IO";
        outputId.clear();
        return;
    }

    outputId = p->get_param("id");
}

IOBase *ConditionOutput::getOutput()
{
    return ListeRoom::Instance().findIO(outputId);
}

bool ConditionOutput::Evaluate()
{
    string sval, oper;
    bool bval = false;
    double dval = 0.0;
    bool ret = false;

    //E4.2c: single resolution point. Everything below dereferences `output`.
    IOBase *output = ListeRoom::Instance().findIO(outputId);

    if (!output)
    {
        cErrorDom("rule.condition.output")
                << "Output '" << outputId << "' does not exist (any more): "
                << "condition evaluates to false";
        return false;
    }

    bool ovar = false;
    bool changed = false;
    switch (output->get_type())
    {
    case TBOOL:
        if (params_var != "")
        {
            IOBase *out = ListeRoom::Instance().findIO(params_var);
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
                cWarningDom("rule.condition.output") <<  "get_value(bool) not bool !";
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
            IOBase *out = ListeRoom::Instance().findIO(params_var);
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
            cWarningDom("rule.condition.output") <<  "get_value(int) not int !";
        break;
    case TSTRING:
        if (params_var != "")
        {
            IOBase *out = ListeRoom::Instance().findIO(params_var);
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
        cDebugDom("rule.condition.output") <<  "Ok";
    else
        cDebugDom("rule.condition.output") <<  "Failed !";

    return ret;
}

bool ConditionOutput::eval(bool val1, std::string oper, bool val2)
{
    return ConditionEval::evalOperator(val1, oper, val2, "rule.condition.output");
}

bool ConditionOutput::eval(double val1, std::string oper, double val2)
{
    return ConditionEval::evalOperator(val1, oper, val2);
}

bool ConditionOutput::eval(std::string val1, std::string oper, std::string val2)
{
    return ConditionEval::evalOperator(val1, oper, val2, "rule.condition.output");
}

bool ConditionOutput::eval(IOBase *out, string oper, string val)
{
    if (oper != "!=" && oper != "==")
    {
        cErrorDom("rule.condition.output") <<  "Invalid operator (" << oper << ")";
        return false;
    }

    return out->check_condition_value(val, oper == "==");
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

    cDebugDom("rule.condition.output") << "trigger: " << (trigger?"true":"false");

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

            IOBase *out = ListeRoom::Instance().findIO(id);
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
                //Load-time contract, unchanged: an id unknown at load rejects
                //the condition. See the comment in ConditionStd::LoadFromXml().
                cErrorDom("rule.condition.output")
                        << "Output '" << id << "' is unknown, condition rejected";
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
    //The id IS the reference: saving resolves nothing, so a condition whose IO
    //disappeared is written back unchanged instead of dereferencing it.
    cnode->SetAttribute("id", outputId);
    cnode->SetAttribute("oper", ops);
    cnode->SetAttribute("val", params);
    if (params_var != "")
        cnode->SetAttribute("val_var", params_var);

    cond_node->LinkEndChild(cnode);

    return true;
}
