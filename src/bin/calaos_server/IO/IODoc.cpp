/******************************************************************************
 **  Copyright (c) 2006-2015, Calaos. All Rights Reserved.
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
#include "IODoc.h"

using namespace Calaos;

IODoc::IODoc()
{
}

void IODoc::friendlyNameSet(const string &friendlyName)
{
    m_name = friendlyName;
}

void IODoc::descriptionSet(const string &description)
{
    m_description = description;
}

void IODoc::linkAdd(const string &description, const string &link)
{
    Params p;
    p.Add("description", description);
    p.Add("link", link);
    m_links.push_back(p);
}

void IODoc::paramAdd(const string &name, const string &description, ParamType type, bool mandatory)
{
    Params param;
    param.Add("name", name);
    param.Add("description", description);
    param.Add("type", typeToString(type));
    param.Add("mandatory", mandatory ? "true" : "false");
    m_parameters.push_back(param);
}

void IODoc::conditionAdd(const string &name, const string &description)
{
    Params p;
    p.Add("name", name);
    p.Add("description", description);
    m_conditions.push_back(p);
}

void IODoc::actionAdd(const string &name, const string &description)
{
    Params p;
    p.Add("name", name);
    p.Add("description", description);
    m_actions.push_back(p);
}

void IODoc::aliasAdd(string alias)
{
    m_aliases.push_back(alias);
}

bool IODoc::isAlias(string alias)
{
    for (auto s: m_aliases)
    {
        if (Utils::str_to_lower(alias) == Utils::str_to_lower(s))
            return true;
    }

    return false;
}

json_t *IODoc::genDocJson()
{
    json_t *ret = json_object();

    json_object_set_new(ret, "description", json_string(m_description.c_str()));

    json_t *aliases = json_array();
    for (const auto &alias : m_aliases)
    {
        json_array_append_new(aliases, json_string(alias.c_str()));
    }
    json_object_set_new(ret, "alias", aliases);

    json_t *array = json_array();
    for (const auto &param : m_parameters)
    {
        json_array_append_new(array, param.toJson());
    }
    json_object_set_new(ret, "parameters", array);

    array = json_array();
    for (const auto &param : m_conditions)
    {
        json_array_append_new(array, param.toJson());
    }
    json_object_set_new(ret, "conditions", array);

    array = json_array();
    for (const auto &param : m_actions)
    {
        json_array_append_new(array, param.toJson());
    }
    json_object_set_new(ret, "actions", array);

    return ret;
}

string IODoc::genDocMd(const string iotype)
{
    string doc ="";
    string name = m_name;

    if (m_name.empty())
    {
        name = iotype;
        doc += "#" + name + " - UNDOCUMENTED IO\n";
        doc += "SPANK SPANK SPANK : naughty programmer ! You did not add documentation for this IO, that's BAD :'(\n";
        doc += "Go document it in your code or you will burn in hell!\n\n";
    }
    else
    {
        doc += "\n#" + name + "\n";

        for (uint i = 0;i < m_aliases.size();i++)
        {
            if (i == 0)
                doc += "#### Alias: " + m_aliases[i];
            else
                doc += ", " + m_aliases[i];
        }
        if (!m_aliases.empty())
            doc += "\n";
    }

    doc += m_description + "\n";

    if (m_parameters.size())
    {

        doc += "\n##Parameters of " + name + "\n";

        doc += "Name | Type | Mandatory | Description\n";
        doc += "---- | ---- | --------- | -----------\n";

        for (const auto &param : m_parameters)
        {
            doc += param["name"] + " | ";
            doc += param["type"] + " | ";
            if (param["mandatory"] == "true")
                doc += "yes | ";
            else
                doc += "no | ";
            doc += param["description"] + "\n";
        }
        doc += "\n";
    }

    if (m_conditions.size())
    {
        doc += "##Conditions of " + name + "\n";

        doc += "Name | Description\n";
        doc += "---- | -----------\n";

        for (const auto &param : m_conditions)
        {
            doc += param["name"] + " | ";
            doc += param["description"] + " \n ";
        }
        doc += "\n";
    }

    if (m_actions.size())
    {
        doc += "##Actions of " + name + "\n";
        doc += "Name | Description\n";
        doc += "---- | -----------\n";

        for (const auto &param : m_actions)
        {
            doc += param["name"] + " | ";
            doc += param["description"] + " \n ";
        }
        doc += "\n";
    }

    if (m_links.size())
    {
        doc += "##More Infos\n";


        for (const auto &param : m_links)
        {
            doc += param["description"] + " : ";
            doc += param["link"] + " \n ";
        }
        doc += "\n";
    }

    return doc;
}

string IODoc::typeToString(ParamType t)
{
    switch (t)
    {
    case TYPE_STRING: return "string";
    case TYPE_BOOL: return "bool";
    case TYPE_FLOAT: return "float";
    case TYPE_INT: return "int";
    case TYPE_LIST: return "list";
    default: break;
    }

    return "unknown";
}

IODoc::ParamType IODoc::typeFromString(const string &t)
{
    if (t == "string") return TYPE_STRING;
    else if (t == "bool") return TYPE_BOOL;
    else if (t == "float") return TYPE_FLOAT;
    else if (t == "int") return TYPE_INT;
    else if (t == "list") return TYPE_LIST;

    return TYPE_UNKOWN;
}
