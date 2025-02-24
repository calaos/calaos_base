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
#include <Params.h>
#include <sstream>

using namespace std;

void Params::Add(string key, string value)
{
    params[key] = value;
}

bool Params::Exists(string key) const
{
    map<string, string>::const_iterator fter = params.find(key);
    if (fter != params.cend())
        return true;
    return false;
}

string Params::get_param(string key)
{
    if (Exists(key))
        return params[key];
    return "";
}

string Params::get_param_const(const string key) const
{
    map<string, string>::const_iterator fter = params.find(key);
    if (fter != params.cend())
        return fter->second;

    return "";
}

string Params::operator[] (string key) const
{
    return get_param_const(key);
}

void Params::get_item(int i, string &key, string &value) const
{
    map<string, string>::const_iterator iter;
    for (iter = params.cbegin();iter != params.cend();iter++,i--)
    {
        if (i == 0)
        {
            key = (*iter).first;
            value = (*iter).second;
            break;
        }
    }
}

void Params::Parse(string str)
{
    int count = 0;
    unsigned int i = 0;

    while (str.length() > 0)
    {
        if (isspace(str[i]))
        {
            string sid, val = str.substr(0, i);
            stringstream id;
            id << count;
            sid = id.str();
            Add(sid, val);

            while (isspace(str[i]))
            {
                i++;
                if (i >= str.length())
                    break;
            }

            str.erase(0, i);

            i = 0;
            count++;
        }
        else
        {
            i++;
            if (i >= str.length())
            {
                stringstream id;
                string sid;
                id << count;
                sid = id.str();
                Add(sid, str);
                break;
            }
        }
    }
}

string Params::toString() const
{
    string ret, key, value;

    map<string, string>::const_iterator iter;
    ret = "Params::toString():\n";
    for (iter = params.cbegin();iter != params.cend();iter++)
    {
        key = (*iter).first;
        value = (*iter).second;

        ret += "\t" + key + ":" + value + "\n";
    }

    return ret;
}

json_t *Params::toJson() const
{
    json_t *ret = json_object();

    auto it = params.begin();
    for (;it != params.end();it++)
    {
        json_object_set_new(ret,
                            (*it).first.c_str(),
                            json_string((*it).second.c_str()));
    }

    return ret;
}

Json Params::toNJson() const
{
    return Json(params);
}

Params Params::fromNJson(const Json &j)
{
    Params p;
    for (Json::const_iterator it = j.begin(); it != j.end(); ++it)
    {
        p.params[it.key()] = it.value();
    }
    return p;
}
