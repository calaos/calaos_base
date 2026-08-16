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
#include "ConditionStd.h"
#include "ListeRoom.h"

using namespace Calaos;

ConditionStd::ConditionStd():
    Condition(COND_STD)
{
    cDebugDom("rule.condition.standard") <<  "New standard condition";
}

ConditionStd::~ConditionStd()
{
}

void ConditionStd::Add(IOBase *in)
{
    if (!in)
    {
        cErrorDom("rule.condition.standard") << "Add(): ignoring a null IO";
        return;
    }

    Add(in->get_param("id"));
}

void ConditionStd::Add(const std::string &id)
{
    if (id.empty())
    {
        //An empty id can never be resolved back (findIO() refuses it), so it
        //would be a permanently false term nobody can diagnose.
        cErrorDom("rule.condition.standard") << "Add(): ignoring an IO with no id";
        return;
    }

    inputIds.push_back(id);

    cDebugDom("rule.condition.standard") <<  "Input(" << id << ") added";
}

IOBase *ConditionStd::get_input(int i)
{
    if (i < 0 || i >= (int)inputIds.size()) return nullptr;

    return ListeRoom::Instance().findIO(inputIds[i]);
}

const std::string &ConditionStd::get_input_id(int i) const
{
    static const std::string empty;

    if (i < 0 || i >= (int)inputIds.size()) return empty;

    return inputIds[i];
}

void ConditionStd::getVarIds(vector<IOBase *> &list)
{
    for (uint i = 0;i < inputIds.size();i++)
    {
        std::string var_id = params_var[inputIds[i]];
        if (var_id.empty()) continue;

        IOBase *in = ListeRoom::Instance().findIO(var_id);

        if (in)
        {
            list.push_back(in);
        }
    }
}

void ConditionStd::getVarIds(vector<std::string> &list)
{
    for (uint i = 0;i < inputIds.size();i++)
    {
        std::string var_id = params_var[inputIds[i]];
        if (var_id.empty()) continue;

        list.push_back(var_id);
    }
}

bool ConditionStd::Evaluate()
{
    std::string sval, oper;
    bool bval = false;
    double dval = 0.0;
    bool ret = true;

    for (uint i = 0;i < inputIds.size();i++)
    {
        const std::string &input_id = inputIds[i];

        //E4.2c: resolve once per input per evaluation. This is THE missing-IO
        //check: everything below dereferences `input`, and nothing else in this
        //function may.
        IOBase *input = ListeRoom::Instance().findIO(input_id);

        if (!input)
        {
            //Fail closed and say so. Returning "true" for a term whose IO is
            //gone would let the rule fire on a condition nobody can read any
            //more, and skipping the term would do the same silently.
            cErrorDom("rule.condition.standard")
                    << "Input '" << input_id << "' does not exist (any more): "
                    << "condition evaluates to false";
            ret = false;
            continue;
        }

        bool ovar = false;
        bool changed = false;
        switch (input->get_type())
        {
        case TBOOL:
            if (params_var[input_id] != "")
            {
                std::string var_id = params_var[input_id];
                IOBase *in = ListeRoom::Instance().findIO(var_id);
                if (in && in->get_type() == TBOOL)
                {
                    bval = in->get_value_bool();
                    ovar = true;
                }
            }

            if (!ovar)
            {
                if (params[input_id] == "true")
                    bval = true;
                else if (params[input_id] == "false")
                    bval = false;
                else if (params[input_id] == "changed")
                    changed = true;
                else
                {
                    cWarningDom("rule.condition.standard") <<  "get_value(bool) not bool !";
                    ret = false;
                    break;
                }
            }

            if (!changed)
            {
                oper = ops[input_id];
                ret = ret && eval(input->get_value_bool(), oper, bval);
            }
            break;
        case TINT:
            if (params_var[input_id] != "")
            {
                std::string var_id = params_var[input_id];
                IOBase *in = ListeRoom::Instance().findIO(var_id);
                if (in && in->get_type() == TINT)
                {
                    dval = in->get_value_double();
                    sval = "ovar";
                    ovar = true;
                }
            }

            if (!ovar)
                sval = params[input_id];

            if (sval != "")
            {
                if (!ovar)
                {
                    if (sval == "changed")
                        changed = true;
                    else
                        Utils::from_string(sval, dval);
                }

                if (!changed)
                {
                    oper = ops[input_id];
                    ret = ret && eval(input->get_value_double(), oper, dval);
                }
            }
            else
                cWarningDom("rule.condition.standard") <<  "get_value(int) not int !";
            break;
        case TSTRING:
            if (params_var[input_id] != "")
            {
                std::string var_id = params_var[input_id];
                IOBase *in = ListeRoom::Instance().findIO(var_id);
                if (in && in->get_type() == TSTRING)
                {
                    sval = in->get_value_string();
                    ovar = true;
                }
            }
            if (!ovar)
            {
                sval = Utils::url_decode2(params[input_id]);
                if (sval == "changed") changed = true;
            }

            if (!changed)
            {
                oper = ops[input_id];
                ret = ret && eval(input->get_value_string(), oper, sval);
            }
            break;
        default: break;
        }
    }

    if (ret)
        cDebugDom("rule.condition.standard") << "Ok";
    else
        cDebugDom("rule.condition.standard") << "Failed !";

    return ret;
}

void ConditionStd::Remove(int pos)
{
    if (pos < 0 || pos >= (int)inputIds.size())
    {
        cErrorDom("rule.condition.standard") << "Remove(): index " << pos
                                             << " out of range";
        return;
    }

    inputIds.erase(inputIds.begin() + pos);

    cDebugDom("rule.condition.standard");
}

void ConditionStd::Assign(int i, IOBase *obj)
{
    if (!obj)
    {
        cErrorDom("rule.condition.standard") << "Assign(): ignoring a null IO";
        return;
    }

    Assign(i, obj->get_param("id"));
}

void ConditionStd::Assign(int i, const std::string &id)
{
    if (i < 0 || i >= (int)inputIds.size() || id.empty())
    {
        cErrorDom("rule.condition.standard") << "Assign(): refusing index " << i
                                             << " / id '" << id << "'";
        return;
    }

    inputIds[i] = id;
}

namespace Calaos
{
namespace ConditionEval
{

bool evalOperator(bool val1, std::string oper, bool val2, const char *logDomain)
{
    if (oper != "!=" && oper != "==")
    {
        cErrorDom(logDomain) <<  "Invalid operator (" << oper << ")";
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

bool evalOperator(double val1, std::string oper, double val2)
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

bool evalOperator(std::string val1, std::string oper, std::string val2, const char *logDomain)
{
    if (oper != "!=" && oper != "==")
    {
        cErrorDom(logDomain) <<  "Invalid operator (" << oper << ")";
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

}
}

bool ConditionStd::eval(bool val1, std::string oper, bool val2)
{
    return ConditionEval::evalOperator(val1, oper, val2, "rule.condition.standard");
}

bool ConditionStd::eval(double val1, std::string oper, double val2)
{
    return ConditionEval::evalOperator(val1, oper, val2);
}

bool ConditionStd::eval(std::string val1, std::string oper, std::string val2)
{
    return ConditionEval::evalOperator(val1, oper, val2, "rule.condition.standard");
}

bool ConditionStd::LoadFromXml(pugi::xml_node node)
{
    if (node.attribute("trigger"))
    {
        if (string(node.attribute("trigger").as_string()) == "true")
            trigger = true;
        else if (string(node.attribute("trigger").as_string()) == "false")
            trigger = false;
    }

    node = XmlUtils::firstChildElement(node);

    for (; node; node = XmlUtils::nextSiblingElement(node))
    {
        if (string(node.name()) == "calaos:input")
        {
            string id = "", oper = "", val = "", val_var = "";

            if (node.attribute("id")) id = node.attribute("id").as_string();
            if (node.attribute("oper")) oper = node.attribute("oper").as_string();
            if (node.attribute("val")) val = node.attribute("val").as_string();
            if (node.attribute("val_var")) val_var = node.attribute("val_var").as_string();

            IOBase *in = ListeRoom::Instance().findIO(id);

            if (!in)
            {
                //for compatibility with old AudioPlayer and Camera, update ids if needed
                list<IOBase *> l = ListeRoom::Instance().getAudioList();
                for (IOBase *io: l)
                {
                    if (io->get_param("iid") == id ||
                        io->get_param("oid") == id)
                        in = io;
                }

                l = ListeRoom::Instance().getCameraList();
                for (IOBase *io: l)
                {
                    if (io->get_param("iid") == id ||
                        io->get_param("oid") == id)
                        in = io;
                }
            }

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
                /* LOAD-TIME contract, deliberately unchanged by E4.2c: an id
                 * that is unknown *at load* is a config that never described
                 * anything resolvable, and the condition is rejected here (the
                 * rule survives without it - see CoreSmoke_test). The by-id
                 * model addresses the other case: an id that resolved at load
                 * and stopped resolving later, which used to leave a dangling
                 * IOBase* and now evaluates to false, loudly. */
                cErrorDom("rule.condition.standard")
                        << "Input '" << id << "' is unknown, condition rejected";
                return false;
            }
        }
    }

    return true;
}

bool ConditionStd::SaveToXml(pugi::xml_node node)
{
    pugi::xml_node cond_node = node.append_child("calaos:condition");
    XmlUtils::setAttribute(cond_node, "type", "standard");
    XmlUtils::setAttribute(cond_node, "trigger", trigger?"true":"false");

    //Saving no longer resolves anything: the id IS what gets written, so a
    //rule whose IO disappeared is serialized unchanged instead of
    //dereferencing a dead pointer (or silently losing the reference).
    for (uint i = 0;i < inputIds.size();i++)
    {
        const std::string &in_id = inputIds[i];

        pugi::xml_node cnode = cond_node.append_child("calaos:input");

        XmlUtils::setAttribute(cnode, "id", in_id);
        XmlUtils::setAttribute(cnode, "oper", ops[in_id]);
        XmlUtils::setAttribute(cnode, "val", params[in_id]);
        if (params_var[in_id] != "")
            XmlUtils::setAttribute(cnode, "val_var", params_var[in_id]);
    }

    return true;
}
