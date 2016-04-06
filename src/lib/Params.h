/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef S_PARAMS_H
#define S_PARAMS_H

#include <iostream>
#include <map>
#include <jansson.h>

class Params
{
protected:
    std::map<std::string, std::string> params;

public:
    Params()
    { }

    /* C++11 initializer_list constructor. This allows to do:
     * Params p = { {"key", "val"}, {"key2", "val2"} };
     */
    typedef std::pair<const std::string, std::string> param_value_type;
    Params(std::initializer_list<param_value_type> il)
    {
        clear();
        params.insert(il.begin(), il.end());
    }

    ~Params()
    { }

    void Add(std::string key, std::string value);
    int size() const { return params.size(); }
    bool Exists(std::string key) const;
    std::string get_param(std::string key);
    std::string get_param_const(const std::string key) const;
    void get_item(int i, std::string &key, std::string &value) const;
    void Delete(std::string key) { params.erase(key); }

    std::string operator[] (std::string key) const;

    void Parse(std::string str);

    std::string toString() const;
    json_t *toJson() const;

    void clear() { params.clear(); }
};

#endif
