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

bool ConditionOutput::LoadFromXml(pugi::xml_node node)
{
    if (node.attribute("trigger"))
    {
        if (string(node.attribute("trigger").as_string()) == "true")
            trigger = true;
        else if (string(node.attribute("trigger").as_string()) == "false")
            trigger = false;
    }

    cDebugDom("rule.condition.output") << "trigger: " << (trigger?"true":"false");

    node = XmlUtils::firstChildElement(node);

    for (; node; node = XmlUtils::nextSiblingElement(node))
    {
        if (string(node.name()) == "calaos:output")
        {
            string id = "", oper = "", val = "", val_var = "";

            if (node.attribute("id")) id = node.attribute("id").as_string();
            if (node.attribute("oper")) oper = node.attribute("oper").as_string();
            if (node.attribute("val")) val = node.attribute("val").as_string();
            if (node.attribute("val_var")) val_var = node.attribute("val_var").as_string();

            IOBase *out = ListeRoom::Instance().findIO(id);
            if (out)
                setOutput(out);
            else
            {
                //Load-time contract, E4.2e: the reference is kept as it stands
                //(so the save cannot lose it) and recorded as missing, which
                //disables the rule using this condition. See the long comment
                //in ConditionStd::LoadFromXml().
                cErrorDom("rule.condition.output")
                        << "Output '" << id << "' does not exist: the reference is "
                        << "kept as it is, and the rule using this condition will "
                        << "be disabled";
                addMissingIo(id);
                setOutputId(id);
            }

            //Unconditional: the parameters belong to the reference, resolvable
            //or not, and losing them would lose the user's condition.
            params = val;
            ops = oper;
            if (val_var != "")
                params_var = val_var;
        }
    }

    return true;
}

bool ConditionOutput::SaveToXml(pugi::xml_node node)
{
    pugi::xml_node cond_node = node.append_child("calaos:condition");
    XmlUtils::setAttribute(cond_node, "type", "output");
    XmlUtils::setAttribute(cond_node, "trigger", trigger?"true":"false");

    pugi::xml_node cnode = cond_node.append_child("calaos:output");
    //The id IS the reference: saving resolves nothing, so a condition whose IO
    //disappeared is written back unchanged instead of dereferencing it.
    XmlUtils::setAttribute(cnode, "id", outputId);
    XmlUtils::setAttribute(cnode, "oper", ops);
    XmlUtils::setAttribute(cnode, "val", params);
    if (params_var != "")
        XmlUtils::setAttribute(cnode, "val_var", params_var);

    return true;
}
