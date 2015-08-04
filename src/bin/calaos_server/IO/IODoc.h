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
    void linkAdd(const string &description, const string &link);
    void paramAdd(const string &name, const string &description, ParamType type, bool mandatory);
    void conditionAdd(const string &name, const string &description);
    void actionAdd(const string &name, const string &description);
    void aliasAdd(string alias);

    bool isAlias(string alias);

    json_t *genDocJson();
    string genDocMd(const string iotype);

private:
    string m_name;
    string m_description;

    vector<string> m_aliases;

    vector<Params> m_links;
    vector<Params> m_parameters;
    vector<Params> m_conditions;
    vector<Params> m_actions;

    string typeToString(ParamType t);
    ParamType typeFromString(const string &t);
};


#endif /* S_IODOC_H */
