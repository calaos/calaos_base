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

/*******************************************************************************
 * TRANSITIONAL ADAPTER - E4.1a. TO BE REMOVED BY THE LAST E4.1 SUB-TICKET.
 *
 * This is Params::toJson() moved out of the class, unchanged. src/lib/Params.h
 * used to include BOTH jansson.h and json.hpp - it was the one real bridge
 * between the two libraries and the crossing point of nearly every API
 * payload. Params now exposes a single JSON API (nlohmann, Params::toNJson);
 * the jansson serialization lives here, in the jansson-only header, until the
 * call sites themselves are migrated.
 *
 * `grep -rn jansson_from_params src tests` is therefore the exact, current
 * list of what the rest of E4.1 still has to convert. When that list is empty,
 * delete this function and this header.
 *
 * Its behaviour must stay bit for bit that of the old member, and that
 * includes the silent drop of invalid UTF-8: json_string() answers NULL,
 * json_object_set_new() answers -1, and neither return code is tested here
 * any more than it was tested before. Turning that drop into U+FFFD is a
 * change of observable behaviour and belongs to the sub-ticket that migrates
 * the call site, not to the move. tests/ParamsJson_test.cpp pins both the drop
 * and the string-only typing (an int that became a JSON number would break the
 * type-strict oracle of the golden suite).
 ******************************************************************************/
inline json_t *jansson_from_params(const Params &params)
{
    json_t *ret = json_object();

    for (Params::const_iterator it = params.cbegin(); it != params.cend(); it++)
    {
        json_object_set_new(ret,
                            (*it).first.c_str(),
                            json_string((*it).second.c_str()));
    }

    return ret;
}

/*******************************************************************************
 * TRANSITIONAL ADAPTER - E4.1l. REMOVED BY E4.1m.
 *
 * `grep -rn jansson_from_json src tests` is the exact list of what still needs
 * it, and that list has exactly ONE entry: LuaScript/ScriptExec.cpp, where the
 * {msg:"event", data:<event>} message sent to calaos_script is still assembled
 * with jansson because the SIBLING message of the same lambda's enclosing
 * scope carries JsonApi::buildFlatIOList(), which is still a json_t *. If you
 * find yourself calling this from a second site, you have overrun a perimeter.
 *
 * Protocol copied from jansson_from_params() above (E4.1a), because it worked:
 * a greppable name, and a comment naming the ticket that deletes it.
 *
 * It is deliberately the SAME dump() as every other emitter of the epic -
 * compact, ensure_ascii = true, error_handler_t::replace - so that a value the
 * client influenced cannot throw type_error.316 from inside an ExternProc read
 * callback, where nothing catches it (the KNX precedent, and E4.1j measured
 * that a Lua script can put a raw 0xFF into a JSON string in one line). The
 * round trip through json_loads() is what keeps the ScriptExec wire byte
 * stable this ticket: jansson re-escapes in its own UPPERCASE form on the way
 * out, so the only thing that changes on that wire is the key order of the
 * event object.
 ******************************************************************************/
inline json_t *jansson_from_json(const Json &j)
{
    json_error_t e;
    return json_loads(j.dump(-1, ' ', true,
                             Json::error_handler_t::replace).c_str(), 0, &e);
}

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
