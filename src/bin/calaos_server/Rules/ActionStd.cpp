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
#include "ActionStd.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "WODigital.h"
#include "ActionTouchscreen.h"

using namespace Calaos;

ActionStd::~ActionStd()
{
}

void ActionStd::Add(IOBase *out)
{
    if (!out)
    {
        cErrorDom("rule.action.standard") << "Add(): ignoring a null IO";
        return;
    }

    if (!out->isOutput())
    {
        cWarningDom("rule.action.standard") << "Unable to add IO "
                                            << out->get_param("id")
                                            << " to action list. IO is not an output";
        return;
    }

    Add(out->get_param("id"));
}

void ActionStd::Add(const std::string &id)
{
    if (id.empty())
    {
        //An empty id never resolves back, it would be a permanently broken
        //output nobody can diagnose.
        cErrorDom("rule.action.standard") << "Add(): ignoring an IO with no id";
        return;
    }

    outputIds.push_back(id);

    cDebugDom("rule.action.standard") <<  "Output(" << id << ") added";
}

IOBase *ActionStd::get_output(int i)
{
    if (i < 0 || i >= (int)outputIds.size()) return nullptr;

    return ListeRoom::Instance().findIO(outputIds[i]);
}

const std::string &ActionStd::get_output_id(int i) const
{
    static const std::string empty;

    if (i < 0 || i >= (int)outputIds.size()) return empty;

    return outputIds[i];
}

bool ActionStd::Execute()
{
    std::string tmp;
    bool ret = true;
    std::string sval;
    bool bval = false;
    double dval = 0;

    for (uint i = 0;i < outputIds.size();i++)
    {
        const std::string &out_id = outputIds[i];

        //E4.2c: resolve once per output per execution. Everything below
        //dereferences `output`, and nothing else in this function may.
        IOBase *output = ListeRoom::Instance().findIO(out_id);

        if (!output)
        {
            cErrorDom("rule.action.standard")
                    << "Output '" << out_id << "' does not exist (any more): "
                    << "action skipped";
            ret = false;
            continue;
        }

        if (!output->isOutput())
        {
            //Add() refuses non-outputs, so this only happens when the id was
            //taken over by another IO after the rule was built. Setting a value
            //on an input is not something an action may do silently.
            cErrorDom("rule.action.standard")
                    << "'" << out_id << "' is not an output (any more): "
                    << "action skipped";
            ret = false;
            continue;
        }

        bool ovar = false;
        switch (output->get_type())
        {
        case TBOOL:
        {
            if (params_var[out_id] != "")
            {
                std::string var_id = params_var[out_id];
                IOBase *out = ListeRoom::Instance().findIO(var_id);
                if (out &&
                    (out->get_type() == TBOOL ||
                     out->get_type() == TSTRING))
                {
                    bval = out->get_command_bool();
                    ovar = true;
                }
            }

            if (ovar)
            {
                if (!output->set_value(bval)) ret = false;
            }
            else if (params[out_id] == "true")
            {
                if (!output->set_value(true)) ret = false;
            }
            else if (params[out_id] == "false")
            {
                if (!output->set_value(false)) ret = false;
            }
            else
            {
                if (!output->set_value(params[out_id])) ret = false;
            }
            break;
        }
        case TINT:
        {
            if (params_var[out_id] != "")
            {
                std::string var_id = params_var[out_id];
                IOBase *out = ListeRoom::Instance().findIO(var_id);
                if (out && out->get_type() == TINT)
                {
                    dval = out->get_command_double();
                    ovar = true;
                }
            }
            tmp = params[out_id];

            if (ovar)
            {
                if (!output->set_value(dval)) ret = false;
            }
            else if (is_of_type<double>(tmp))
            {
                double v;
                Utils::from_string(tmp, v);
                if (!output->set_value(v)) ret = false;
            }
            else
            {
                if (!output->set_value(tmp)) ret = false;
            }
            break;
        }
        case TSTRING:
        {
            if (params_var[out_id] != "")
            {
                std::string var_id = params_var[out_id];
                IOBase *out = ListeRoom::Instance().findIO(var_id);
                if (out && out->get_type() == TSTRING)
                {
                    sval = out->get_command_string();
                    ovar = true;
                }
                else if (out && out->get_type() == TBOOL)
                {
                    sval = out->get_command_bool() ? "true" : "false";
                    ovar = true;
                }
            }
            tmp = params[out_id];

            if (ovar)
            {
                if (!output->set_value(sval)) ret = false;
            }
            else if (tmp != "")
            {
                if (!output->set_value(tmp)) ret = false;
            }

            break;
        }
        default: break;
        }
    }

    if (ret)
        cDebugDom("rule.action.standard") <<  "Ok";
    else
        cErrorDom("rule.action.standard") <<  "Failed !";

    return ret;
}

void ActionStd::Remove(int pos)
{
    if (pos < 0 || pos >= (int)outputIds.size())
    {
        cErrorDom("rule.action.standard") << "Remove(): index " << pos
                                          << " out of range";
        return;
    }

    outputIds.erase(outputIds.begin() + pos);

    cDebugDom("rule.action.standard") <<  "Ok";
}

void ActionStd::Assign(int i, IOBase *obj)
{
    if (!obj)
    {
        cErrorDom("rule.action.standard") << "Assign(): ignoring a null IO";
        return;
    }

    Assign(i, obj->get_param("id"));
}

void ActionStd::Assign(int i, const std::string &id)
{
    if (i < 0 || i >= (int)outputIds.size() || id.empty())
    {
        cErrorDom("rule.action.standard") << "Assign(): refusing index " << i
                                          << " / id '" << id << "'";
        return;
    }

    outputIds[i] = id;
}

bool ActionStd::LoadFromXml(pugi::xml_node node)
{
    node = XmlUtils::firstChildElement(node);

    for (; node; node = XmlUtils::nextSiblingElement(node))
    {
        if (string(node.name()) == "calaos:output")
        {
            string id = "", val = "", val_var = "";

            if (node.attribute("id")) id = node.attribute("id").as_string();
            if (node.attribute("val")) val = node.attribute("val").as_string();
            if (node.attribute("val_var")) val_var = node.attribute("val_var").as_string();

            IOBase *out = ListeRoom::Instance().findIO(id);

            if (!out)
            {
                //for compatibility with old AudioPlayer and Camera, update ids if needed
                list<IOBase *> l = ListeRoom::Instance().getAudioList();
                for (IOBase *io: l)
                {
                    if (io->get_param("iid") == id ||
                        io->get_param("oid") == id)
                    {
                        out = io;
                        id = out->get_param("id"); //Use new ID
                    }
                }

                l = ListeRoom::Instance().getCameraList();
                for (IOBase *io: l)
                {
                    if (io->get_param("iid") == id ||
                        io->get_param("oid") == id)
                    {
                        out = io;
                        id = out->get_param("id"); //Use new ID
                    }
                }
            }

            if (out && out->isOutput())
                Add(out);
            else
            {
                /* Load-time contract, E4.2e: an id that does not resolve (or
                 * that resolves to something which is not an output, i.e. an
                 * unusable reference just the same) is KEPT as the file
                 * describes it and recorded as missing, which disables the rule
                 * owning this action. See the long comment in
                 * ConditionStd::LoadFromXml(): dropping the action here is what
                 * made the user lose it at the next save.
                 * Note Add(id) is used on purpose, not Add(IOBase*): the latter
                 * refuses a non-output, and the reference must survive.
                 */
                cErrorDom("rule.action.standard")
                        << "Output '" << id << "' does not exist or is not an "
                        << "output: the reference is kept as it is, and the rule "
                        << "using this action will be disabled";
                addMissingIo(id);
                Add(id);
            }

            //Unconditional: the parameters belong to the reference, resolvable
            //or not, and losing them would lose the user's action.
            params.Add(id, val);
            if (val_var != "")
                params_var.Add(id, val_var);
        }
    }

    return true;
}

bool ActionStd::SaveToXml(pugi::xml_node node)
{
    pugi::xml_node action_node = node.append_child("calaos:action");
    XmlUtils::setAttribute(action_node, "type", "standard");

    //The id IS the reference: saving resolves nothing, so an action whose IO
    //disappeared is written back unchanged instead of dereferencing it.
    for (uint i = 0;i < outputIds.size();i++)
    {
        const std::string &out_id = outputIds[i];

        pugi::xml_node cnode = action_node.append_child("calaos:output");

        XmlUtils::setAttribute(cnode, "id", out_id);
        XmlUtils::setAttribute(cnode, "val", params[out_id]);
        if (params_var[out_id] != "")
            XmlUtils::setAttribute(cnode, "val_var", params_var[out_id]);
    }

    return true;
}
