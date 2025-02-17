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
#ifndef JANSSON_ADDITION_H
#define JANSSON_ADDITION_H

#include <jansson.h>
#include "Params.h"

using namespace Utils;

inline bool jansson_bool_get(const json_t *json, const std::string &str, const bool default_value = false)
{
    bool ret;
    json_t *jdata;

    jdata = json_object_get(json, str.c_str());
    if (!jdata) return default_value;

    if (!json_is_boolean(jdata))
        return default_value;

    ret = json_is_true(jdata)?true:false;

    return ret;
}

inline std::string jansson_string_get(const json_t *json, const std::string &str, const std::string default_value = "")
{
    std::string ret;
    json_t *jdata;

    jdata = json_object_get(json, str.c_str());
    if (!jdata) return default_value;

    if (!json_is_string(jdata))
        return default_value;

    ret = json_string_value(jdata);

    return ret;
}

inline int jansson_int_get(const json_t *json, const std::string &str, const int default_value = 0)
{
    int ret;
    json_t *jdata;

    jdata = json_object_get(json, str.c_str());
    if (!jdata) return default_value;

    if (!json_is_integer(jdata))
        return default_value;

    ret = json_integer_value(jdata);

    return ret;
}

inline double jansson_double_get(const json_t *json, const std::string &str, const double default_value = 0.0)
{
    double ret;
    json_t *jdata;

    jdata = json_object_get(json, str.c_str());
    if (!jdata) return default_value;

    if (!json_is_real(jdata))
        return default_value;

    ret = json_real_value(jdata);

    return ret;
}

inline void jansson_decode_object(json_t *jroot, Params &params)
{
    const char *key;
    json_t *value;

    json_object_foreach(jroot, key, value)
    {
        string svalue;

        if (json_is_string(value))
            svalue = json_string_value(value);
        else if (json_is_boolean(value))
            svalue = json_is_true(value)?"true":"false";
        else if (json_is_number(value))
            svalue = Utils::to_string(json_number_value(value));

        params.Add(key, svalue);
    }
}

inline string jansson_to_string(json_t *jroot)
{
    char *d = json_dumps(jroot, JSON_COMPACT | JSON_ENSURE_ASCII /*| JSON_ESCAPE_SLASH*/);
    if (!d)
    {
        cError() << "json_dumps failed!";
        json_decref(jroot);
        return string();
    }

    json_decref(jroot);
    string res(d);
    free(d);

    return res;
}

#endif // JANSSON_ADDITION_H
