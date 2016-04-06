/******************************************************************************
 **  Copyright (c) 2007-2015, Calaos. All Rights Reserved.
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

namespace Calaos {

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

    void friendlyNameSet(const std::string &friendlyName);
    void descriptionSet(const std::string &description);
    void descriptionBaseSet(const std::string &description);
    void linkAdd(const std::string &description, const std::string &link);
    void paramAdd(const std::string &name, const std::string &description, ParamType type, bool mandatory, const std::string defaultval = std::string(), bool readonly = false);
    void paramAddInt(const std::string &name, const std::string &description, int min, int max, bool mandatory, int defval = 0, bool readonly = false);
    void paramAddFloat(const std::string &name, const std::string &description, bool mandatory, double min, double max, double defval = 0, bool readonly = false);
    void paramAddList(const std::string &name, const std::string &description, bool mandatory, const Params &keyvalues, const std::string &defkey = std::string(), bool readonly = false);
    void conditionAdd(const std::string &name, const std::string &description);
    void actionAdd(const std::string &name, const std::string &description);
    void aliasAdd(std::string alias);

    bool isAlias(std::string alias);

    json_t *genDocJson();
    std::string genDocMd(const std::string iotype);

private:
    std::string m_name;
    std::string m_description_base;
    std::string m_description;

    std::vector<std::string> m_aliases;
    std::vector<Params> m_links;

   typedef std::unordered_map<std::string, Params> ParamMap;

    ParamMap m_parameters;
    ParamMap m_conditions;
    ParamMap m_actions;

    //for parameter of type list. This contains a map of all key/values for the list of possible parameter values
    ParamMap param_list_value;

    std::string typeToString(ParamType t);
    ParamType typeFromString(const std::string &t);
};

}

#endif /* S_IODOC_H */
