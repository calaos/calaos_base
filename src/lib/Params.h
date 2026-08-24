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
#ifndef S_PARAMS_H
#define S_PARAMS_H

#include <iostream>
#include <map>

#include "json.hpp"
using Json = nlohmann::json;

using namespace std;

class Params
{
protected:
    std::map<string, string> params;

public:
    Params()
    { }

    /* C++11 initializer_list constructor. This allows to do:
     * Params p = { {"key", "val"}, {"key2", "val2"} };
     */
    typedef std::pair<const string, string> param_value_type;
    Params(std::initializer_list<param_value_type> il)
    {
        clear();
        params.insert(il.begin(), il.end());
    }

    ~Params()
    { }

    void Add(string key, string value);
    int size() const { return params.size(); }
    bool Exists(string key) const;
    string get_param(string key);
    string get_param_const(const string key) const;
    void get_item(int i, string &key, string &value) const;
    void Delete(std::string key) { params.erase(key); }

    /* E4.1a: read-only iteration over the pairs, so that the transitional
     * jansson adapter can serialize a Params from the outside. Params is a
     * std::map, so this walk is alphabetical and NOT insertion ordered;
     * tests/ParamsJson_test.cpp pins it.
     *
     * cbegin/cend only, deliberately: adding a begin()/end() pair would
     * make Params look like a container to nlohmann's compatible-type
     * detection and let it convert implicitly to a JSON array somewhere,
     * which is exactly the kind of silent change this ticket must not make.
     */
    typedef std::map<string, string>::const_iterator const_iterator;
    const_iterator cbegin() const { return params.cbegin(); }
    const_iterator cend() const { return params.cend(); }

    string operator[] (string key) const;

    void Parse(string str);

    string toString() const;
    Json toNJson() const;

    void clear() { params.clear(); }

    static Params fromNJson(const Json &j);
};

#endif
