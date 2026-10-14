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
#ifndef S_IODOC_H
#define S_IODOC_H

#include "Calaos.h"
#include <Params.h>
#include <map>
#include <set>

class IODoc
{
public:
    IODoc();

    typedef enum
    {
        TYPE_UNKOWN,
        TYPE_STRING,
        TYPE_BOOL,
        TYPE_INT,
        TYPE_FLOAT,
        TYPE_LIST,
    } ParamType;

    void friendlyNameSet(const string &friendlyName);
    void descriptionSet(const string &description);
    void descriptionBaseSet(const string &description);
    void linkAdd(const string &description, const string &link);
    void paramAdd(const string &name, const string &description, ParamType type, bool mandatory, const string defaultval = string(), bool readonly = false);
    void paramAddInt(const string &name, const string &description, int min, int max, bool mandatory, int defval = 0, bool readonly = false);
    void paramAddFloat(const string &name, const string &description, bool mandatory, double min, double max, double defval = 0, bool readonly = false);
    void paramAddList(const string &name, const string &description, bool mandatory, const Params &keyvalues, const string &defkey = string(), bool readonly = false);
    void conditionAdd(const string &name, const string &description);
    void actionAdd(const string &name, const string &description);
    void aliasAdd(string alias);

    /* A device takes a JSON object as command (spec §5.2, §11): adding a field
     * switches command_format to "json". Fields keep their declaration order.
     * requiresLinks are the link roles that must exist, and be writable, on an
     * instance for the field to be offered (§6.3). valueRequires adds roles
     * that one value of an enum needs on top (cool needs output_cool). */
    typedef std::map<string, vector<string>> ValueLinks;
    void commandFieldEnum(const string &name, const string &description, const vector<string> &values,
                          const vector<string> &requiresLinks, const string &valuesParam = string(),
                          const ValueLinks &valueRequires = ValueLinks());
    void commandFieldNumber(const string &name, const string &description, double min, double max,
                            double step, const string &unit, const vector<string> &requiresLinks);

    //Only devices have a family and link roles; they are what tells them from a plain IO.
    void deviceFamilySet(const string &family);
    /* writable: the device writes this link (to the appliance, to an output,
     * or to an IO it owns); a role that is not is only read.
     * requiredWhenParam/Value make an optional role required once a param has
     * that value (humidity when emitter is floor). */
    void linkRoleAdd(const string &role, const string &description, bool required, bool writable,
                     const string &requiredWhenParam = string(), const string &requiredWhenValue = string());
    /* The parameter is a <calaos:param> child of the device, not an XML
     * attribute. Whatever the order of the calls: a name never added is ignored. */
    void paramInChild(const string &name);

    bool isAlias(string alias);

    Json genDocJson();
    string genDocMd(const string iotype);

private:
    string m_name;
    string m_description_base;
    string m_description;

    vector<string> m_aliases;
    vector<Params> m_links;

    typedef unordered_map<string, Params> ParamMap;

    ParamMap m_parameters;
    ParamMap m_conditions;
    ParamMap m_actions;

    //for parameter of type list. This contains a map of all key/values for the list of possible parameter values
    ParamMap param_list_value;

    std::set<string> m_child_params;

    string m_family;
    Json m_command_fields = Json::array();
    Json m_link_roles = Json::array();

    static string commandFieldRange(const Json &field);
    string typeToString(ParamType t);
    ParamType typeFromString(const string &t);
};


#endif /* S_IODOC_H */
