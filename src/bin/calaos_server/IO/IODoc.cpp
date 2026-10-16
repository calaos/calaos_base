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
#include "IODoc.h"

#include <algorithm>

using namespace Calaos;

namespace
{

//The maps are unordered: their iteration order depends on the standard library,
//and the generated files are diffed (and compared by the installer's CI).
template <class Map>
std::map<string, Params> byName(const Map &m)
{
    return std::map<string, Params>(m.begin(), m.end());
}

string joinValues(const Json &values)
{
    string out;
    for (const Json &v: values)
        out += (out.empty() ? "" : ", ") + v.get<string>();
    //Not a named return value: gcov charges its unwinding to the closing brace.
    return string(std::move(out));
}

} //namespace

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

void IODoc::descriptionBaseSet(const string &description)
{
    m_description_base = description;
}

void IODoc::linkAdd(const string &description, const string &link)
{
    Params p;
    p.Add("description", description);
    p.Add("link", link);
    m_links.push_back(p);
}

void IODoc::paramAdd(const string &name, const string &description, ParamType type, bool mandatory, const string defaultval, bool readonly, const string &label)
{
    Params param;
    param.Add("name", name);
    param.Add("description", description);
    if (!label.empty())
        param.Add("label", label);
    param.Add("type", typeToString(type));
    param.Add("mandatory", mandatory ? "true" : "false");
    if (!defaultval.empty())
        param.Add("default", defaultval);
    param.Add("readonly", readonly ? "true" : "false");
    m_parameters[name] = param;
}

void IODoc::paramAddInt(const string &name, const string &description, int min, int max, bool mandatory, int defval, bool readonly, const string &label)
{
    Params param;
    param.Add("name", name);
    param.Add("description", description);
    if (!label.empty())
        param.Add("label", label);
    param.Add("type", typeToString(TYPE_INT));
    param.Add("mandatory", mandatory ? "true" : "false");
    param.Add("default", Utils::to_string(defval));
    param.Add("min", Utils::to_string(min));
    param.Add("max", Utils::to_string(max));
    param.Add("readonly", readonly ? "true" : "false");
    m_parameters[name] = param;
}

void IODoc::paramAddFloat(const string &name, const string &description, bool mandatory, double min, double max, double defval, bool readonly, const string &label)
{
    Params param;
    param.Add("name", name);
    param.Add("description", description);
    if (!label.empty())
        param.Add("label", label);
    param.Add("type", typeToString(TYPE_FLOAT));
    param.Add("mandatory", mandatory ? "true" : "false");
    param.Add("default", Utils::to_string(defval));
    param.Add("min", Utils::to_string(min));
    param.Add("max", Utils::to_string(max));
    param.Add("readonly", readonly ? "true" : "false");
    m_parameters[name] = param;
}

void IODoc::paramAddList(const string &name, const string &description, bool mandatory, const Params &keyvalues, const string &defkey, bool readonly, const string &label)
{
    Params param;
    param.Add("name", name);
    param.Add("description", description);
    if (!label.empty())
        param.Add("label", label);
    param.Add("type", typeToString(TYPE_LIST));
    param.Add("mandatory", mandatory ? "true" : "false");
    param.Add("default", defkey);
    param.Add("readonly", readonly ? "true" : "false");
    m_parameters[name] = param;
    param_list_value[name] = keyvalues;
}

void IODoc::conditionAdd(const string &name, const string &description)
{
    Params p;
    p.Add("name", name);
    p.Add("description", description);
    m_conditions[name] = p;
}

void IODoc::actionAdd(const string &name, const string &description)
{
    Params p;
    p.Add("name", name);
    p.Add("description", description);
    m_actions[name] = p;
}

void IODoc::aliasAdd(string alias)
{
    m_aliases.push_back(alias);
}

void IODoc::commandFieldEnum(const string &name, const string &description, const vector<string> &values,
                             const vector<string> &requiresLinks, const string &valuesParam,
                             const ValueLinks &valueRequires)
{
    Json field = { { "name", name },
                   { "type", "enum" },
                   { "description", description },
                   { "requires_links", requiresLinks } };
    //Instance dependent lists (fan_modes) name the param that holds them.
    if (!valuesParam.empty())
        field["values_param"] = valuesParam;
    else
        field["values"] = values;
    if (!valueRequires.empty())
        field["value_requires_links"] = valueRequires;
    m_command_fields.push_back(field);
}

void IODoc::commandFieldNumber(const string &name, const string &description, double min, double max,
                               double step, const string &unit, const vector<string> &requiresLinks)
{
    m_command_fields.push_back({ { "name", name },
                                 { "type", "number" },
                                 { "description", description },
                                 { "min", min },
                                 { "max", max },
                                 { "step", step },
                                 { "unit", unit },
                                 { "requires_links", requiresLinks } });
}

void IODoc::deviceFamilySet(const string &family)
{
    m_family = family;
}

string IODoc::linkValueTypeToString(LinkValueType t)
{
    switch (t)
    {
    case LINK_BOOL: return "bool";
    case LINK_STRING: return "string";
    case LINK_NUMBER: break;
    }
    return "number";
}

void IODoc::linkRoleAdd(const string &role, const string &label, const string &description,
                        LinkValueType valueType, bool required, bool writable,
                        const string &requiredWhenParam, const string &requiredWhenValue,
                        const string &requiredWhenLink)
{
    Json r = { { "role", role },
               { "label", label },
               { "description", description },
               { "value_type", linkValueTypeToString(valueType) },
               { "required", required },
               { "writable", writable } };
    if (!requiredWhenParam.empty())
    {
        r["required_when"] = { { "param", requiredWhenParam }, { "value", requiredWhenValue } };
        if (!requiredWhenLink.empty())
            r["required_when"]["link"] = requiredWhenLink;
    }
    m_link_roles.push_back(r);
}

void IODoc::linkRoleReplaces(const string &role, const vector<string> &replaced)
{
    for (Json &r: m_link_roles)
    {
        if (r["role"] == role)
            r["replaces"] = replaced;
    }
}

void IODoc::paramListRule(const string &name, const vector<string> &alwaysIncludes,
                          const ValueLinks &defaultWhenLinked, const ValueLinks &valueRequiresValues)
{
    Json rule = Json::object();
    if (!alwaysIncludes.empty())
        rule["always_includes"] = alwaysIncludes;
    if (!defaultWhenLinked.empty())
        rule["default_when_linked"] = defaultWhenLinked;
    if (!valueRequiresValues.empty())
        rule["value_requires_values"] = valueRequiresValues;
    m_list_rules[name] = rule;
}

void IODoc::paramInChild(const string &name)
{
    m_child_params.insert(name);
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

Json IODoc::genDocJson()
{
    Json ret = Json::object();

    string desc;
    if (!m_description.empty())
        desc += m_description;
    if (!m_description_base.empty())
        desc += " " + m_description_base;

    ret["description"] = desc;

    Json aliases = Json::array();
    for (const auto &alias : m_aliases)
    {
        aliases.push_back(alias);
    }
    ret["alias"] = aliases;

    Json array = Json::array();
    for (const auto &it: byName(m_parameters))
    {
        //Params is a map<string,string>: every value published here is a JSON
        //STRING, min/max/default of an int or a float included. Typing them
        //would break the type-strict oracle of the test suite (3 != "3").
        Json jparam = it.second.toJson();
        if (m_child_params.count(it.first))
            jparam["storage"] = "calaos:param";
        const auto rule = m_list_rules.find(it.first);
        if (rule != m_list_rules.end() && !rule->second.empty())
            jparam["list_rule"] = rule->second;
        if (typeFromString(it.second["type"]) == TYPE_LIST)
            jparam["list_value"] = param_list_value[it.second["name"]].toJson();
        array.push_back(jparam);
    }
    ret["parameters"] = array;

    array = Json::array();
    for (const auto &it: byName(m_conditions))
    {
        array.push_back(it.second.toJson());
    }
    ret["conditions"] = array;

    array = Json::array();
    for (const auto &it: byName(m_actions))
    {
        array.push_back(it.second.toJson());
    }
    ret["actions"] = array;

    if (!m_family.empty())
        ret["family"] = m_family;
    if (!m_link_roles.empty())
        ret["link_roles"] = m_link_roles;
    if (!m_command_fields.empty())
    {
        ret["command_format"] = "json";
        ret["command_fields"] = m_command_fields;
    }

    return ret;
}

string IODoc::genDocMd(const string iotype)
{
    string doc ="";
    string name = m_name;

    if (m_name.empty())
    {
        name = iotype;
        doc += "# " + name + " - UNDOCUMENTED IO\n";
        doc += "SPANK SPANK SPANK : naughty programmer ! You did not add documentation for this IO, that's BAD :'(\n";
        doc += "Go document it in your code or you will burn in hell!\n\n";
    }
    else
    {
        doc += "\n# " + name + "\n";

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

    if (!m_description.empty())
        doc += m_description + "\n";
    if (!m_description_base.empty())
        doc += "\n\n" + m_description_base + "\n";

    if (!m_family.empty())
        doc += "\nDevice of family `" + m_family + "`, impl `" + iotype + "`.\n";

    if (m_parameters.size())
    {

        doc += "\n## Parameters of " + name + "\n";

        bool labelled = false;
        for (const auto &it: m_parameters)
            labelled = labelled || it.second.Exists("label");

        doc += labelled ? "Name | Label | Type | Mandatory | Description\n"
                        : "Name | Type | Mandatory | Description\n";
        doc += labelled ? "---- | ----- | ---- | --------- | -----------\n"
                        : "---- | ---- | --------- | -----------\n";

        for (const auto &it: byName(m_parameters))
        {
            doc += it.second["name"] + (m_child_params.count(it.first) ? " (calaos:param)" : "") + " | ";
            if (labelled)
                doc += it.second["label"] + " | ";
            doc += it.second["type"] + " | ";
            if (it.second["mandatory"] == "true")
                doc += "yes | ";
            else
                doc += "no | ";
            doc += it.second["description"] + "\n";
        }
        doc += "\n";

        for (const auto &it: m_list_rules)
        {
            if (!m_parameters.count(it.first) || it.second.empty())
                continue;
            const Json &rule = it.second;
            doc += "List `" + it.first + "`:";
            if (rule.contains("always_includes"))
                doc += " always offers " + joinValues(rule["always_includes"]) + ".";
            //items() of a temporary would iterate a destroyed document.
            const Json defaults = rule.value("default_when_linked", Json::object());
            const Json needs = rule.value("value_requires_values", Json::object());
            for (const auto &d: defaults.items())
                doc += " Without it and with " + d.key() + " linked: " + joinValues(d.value()) + ".";
            for (const auto &d: needs.items())
                doc += " " + d.key() + " needs " + joinValues(d.value()) + ".";
            doc += "\n\n";
        }
    }

    if (!m_link_roles.empty())
    {
        doc += "## Link roles of " + name + "\n";
        doc += "Writable: the device writes this link.\n\n";
        doc += "Role | Label | Value | Required | Writable | Description\n";
        doc += "---- | ----- | ----- | -------- | -------- | -----------\n";

        for (const Json &r: m_link_roles)
        {
            doc += r["role"].get<string>() + " | ";
            doc += r["label"].get<string>() + " | ";
            doc += r["value_type"].get<string>() + " | ";
            string required = r["required"].get<bool>() ? "yes" : "no";
            for (const Json &other: m_link_roles)
            {
                const Json replaced = other.value("replaces", Json::array());
                if (r["required"].get<bool>() &&
                    std::find(replaced.begin(), replaced.end(), r["role"]) != replaced.end())
                    required += ", or " + other["role"].get<string>();
            }
            if (r.contains("required_when"))
            {
                const Json &when = r["required_when"];
                required = "when " + when["param"].get<string>() + "=" + when["value"].get<string>();
                if (when.contains("link"))
                    required += " and " + when["link"].get<string>();
            }
            doc += required + " | ";
            doc += string(r["writable"].get<bool>() ? "yes" : "no") + " | ";
            doc += r["description"].get<string>();
            if (r.contains("replaces"))
            {
                string roles;
                for (const Json &x: r["replaces"])
                    roles += (roles.empty() ? "" : ", ") + x.get<string>();
                doc += " Replaces " + roles + ".";
            }
            doc += "\n";
        }
        doc += "\n";
    }

    if (!m_command_fields.empty())
    {
        doc += "## Command of " + name + "\n";
        doc += "The value is a mode (`off`) or a JSON object holding any of these fields, e.g. "
               "`{\"mode\":\"heat\",\"target_temp\":21.5}`. A field is only applied when all its "
               "links exist and are writable on the instance.\n\n";
        doc += "Name | Type | Values or bounds | Requires links | Description\n";
        doc += "---- | ---- | ---------------- | -------------- | -----------\n";

        for (const Json &f: m_command_fields)
        {
            doc += f["name"].get<string>() + " | ";
            doc += f["type"].get<string>() + " | ";
            doc += commandFieldRange(f) + " | ";
            string links;
            for (const Json &l: f["requires_links"])
                links += (links.empty() ? "" : ", ") + l.get<string>();
            doc += links + " | ";
            doc += f["description"].get<string>() + "\n";
        }
        doc += "\n";
    }

    if (m_conditions.size())
    {
        doc += "## Conditions of " + name + "\n";

        doc += "Name | Description\n";
        doc += "---- | -----------\n";

        for (const auto &it: byName(m_conditions))
        {
            doc += it.second["name"] + " | ";
            doc += it.second["description"] + " \n ";
        }
        doc += "\n";
    }

    if (m_actions.size())
    {
        doc += "## Actions of " + name + "\n";
        doc += "Name | Description\n";
        doc += "---- | -----------\n";

        for (const auto &it: byName(m_actions))
        {
            doc += it.second["name"] + " | ";
            doc += it.second["description"] + " \n ";
        }
        doc += "\n";
    }

    if (m_links.size())
    {
        doc += "## More Infos\n";


        for (const auto &param : m_links)
        {
            doc += "* " + param["description"] + ": ";
            doc += param["link"] + "\n";
        }
        doc += "\n";
    }

    return doc;
}

string IODoc::commandFieldRange(const Json &field)
{
    if (field["type"] == "number")
        return Utils::to_string(field["min"].get<double>()) + " to " +
               Utils::to_string(field["max"].get<double>()) + ", step " +
               Utils::to_string(field["step"].get<double>()) + " " + field["unit"].get<string>();

    if (field.contains("values_param"))
        return "values of the " + field["values_param"].get<string>() + " param";

    string values;
    for (const Json &v: field["values"])
    {
        const string value = v.get<string>();
        values += (values.empty() ? "" : ", ") + value;
        if (!field.contains("value_requires_links") || !field["value_requires_links"].contains(value))
            continue;
        string needs;
        for (const Json &l: field["value_requires_links"][value])
            needs += (needs.empty() ? "" : " + ") + l.get<string>();
        values += " (needs " + needs + ")";
    }
    return values;
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
