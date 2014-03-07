/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
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

using namespace std;

class Params
{
protected:
    std::map<string, string> params;

public:
    Params()
    { }
    ~Params()
    { }

    void Add(string key, string value);
    int size() { return params.size(); }
    bool Exists(string key);
    string get_param(string key);
    string get_param_const(const string key) const;
    void get_item(int i, string &key, string &value);
    void Delete(std::string key) { params.erase(key); }

    string operator[] (string key);

    void Parse(string str);

    string toString();

    void clear() { params.clear(); }
};

#endif
