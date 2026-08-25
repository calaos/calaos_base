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
#ifndef S_SCRIPT_WIRE_H
#define S_SCRIPT_WIRE_H

#include <string>
#include <vector>

#include "Params.h"
#include "Utils.h"

/*
 * E4.1j - the DOWNSTREAM half of the Lua wire, in one place.
 *
 * Both ends of this wire are ours and ship together: LuaScript/ScriptExec.cpp
 * inside calaos_server and LuaScript/ScriptExtern_main.cpp +
 * LuaScript/ScriptBindings.cpp inside calaos_script, built by the same
 * Makefile.am, installed by the same package and updated together. There is no
 * supported configuration where one talks to a binary of another version, and
 * both ends decode with a REAL JSON PARSER (json_loads on the server side,
 * Json::parse here), never by substring search. So the SHAPE of the bytes has
 * no third party to protect, and the mixed state - a nlohmann emitter here, a
 * jansson receiver in ScriptExec.cpp until E4.1m - is exactly as readable as
 * the previous one. What matters is the STRUCTURE, the VALUES and their TYPES.
 *
 * The assembly used to live inline in two programs, one of which defines
 * main() through EXTERN_PROC_CLIENT_MAIN while the other only reaches the wire
 * through an object holding a connected ExternProcClient socket - which is why
 * nothing could ever test it. It lives here so tests/ScriptWire_test.cpp can.
 *
 * EVERY VALUE ON THIS WIRE IS A JSON STRING, booleans and numbers included.
 * That is load bearing, not sloppiness: ScriptExec.cpp reads the values back
 * with jansson_string_get() / jansson_decode_object(), and a value that became
 * a real JSON number or a real JSON boolean would read back as the DEFAULT,
 * i.e. as nothing at all. The oracle of the test suite is type-strict: 3 is
 * not "3".
 *
 * ****************************************************************************
 * THE THREE EMISSION INVARIANTS of E4.1 are applied in dumpJson() and NOWHERE
 * ELSE, so there is exactly one place to check: sorted keys (plain
 * nlohmann::json, never ordered_json), ensure_ascii = true, and
 * error_handler_t::replace.
 *
 * AND ON THIS WIRE THE LAST TWO ARE LOAD BEARING, NOT DEFENSIVE - unlike
 * Wago, OLA and Hue, where it was measured that no uncontrolled byte could
 * reach a dump(). Here the injection channel is ONE LINE OF USER LUA:
 *
 *     calaos.sendPushNotif(string.char(0xFF))
 *     calaos.setIOValue("io_x", string.char(0xFF))
 *     calaos.setIOParam("io_x", "key", string.char(0xFF))
 *
 * A Lua string is a byte string; LuaJIT is Lua 5.1, so string.char() and the
 * "\255" decimal escape both exist. lua_tostring() hands the bytes to
 * ScriptBindings.cpp, which puts them straight into a JSON string value. The
 * script is written by the user and arrives through rules.xml or the JSON API.
 *
 * ⚠️ MEASURED, and worth being precise about: the two spellings above WORK
 * TODAY because the script itself stays pure ASCII and the byte is born inside
 * the Lua VM. A RAW invalid byte written in the SCRIPT TEXT does NOT survive
 * the trip today - ScriptExec.cpp:154-157 sends the script through
 * jansson_from_params(), whose json_string() answers NULL on it and DROPS the
 * whole pair, so calaos_script never receives that script at all. That third
 * channel OPENS with E4.1m, which migrates ScriptExec.cpp: from then on the
 * raw bytes of the script text reach this side too. The handler below already
 * covers it.
 *
 * A NAKED dump() HERE IS std::terminate ON calaos_script: type_error.316 is
 * thrown from inside the ExternProc read callback, and there is no try/catch
 * anywhere on that path. E4.1e watched exactly that kill a KNX driver on a
 * dimmer at 78%. replace turns the bad byte into U+FFFD instead.
 * ****************************************************************************
 *
 * WHAT CHANGES ON THE BYTES, exhaustively: the top level {msg, data} envelope
 * of ScriptBindings.cpp was built key by key with jansson, which walked it in
 * INSERTION order; nlohmann sorts, so "data" now comes before "msg". The
 * "data" object itself does NOT move - it is built from a Params, which is a
 * std::map, so jansson_from_params() already walked it alphabetically - and
 * neither does the flat "finished" message, for the same reason. On a
 * non-ASCII value the hexadecimal of the escape goes from uppercase (jansson
 * JSON_ENSURE_ASCII) to lowercase. No JSON parser can see either.
 */
namespace ScriptWire
{

/*
 * NAMED TYPES for the values a call site could otherwise exchange.
 *
 * Three positional std::string arguments in a row is the defect this series
 * has measured four times: permuting two of them compiles without a warning
 * and leaves every suite green, because a free function cannot cover its own
 * call site. With these, ScriptWire::buildSetParamMessage(key, id, value) is
 * NOT COMPILABLE. Same reasoning as LmsHost{}, LightState and RedChannel
 * elsewhere in the series.
 */
struct IoId          { std::string value; };
struct ParamKey      { std::string value; };
struct ParamValue    { std::string value; };
struct PushAttachment{ std::string value; };

//THE ONLY dump() OF THIS WIRE. See the three invariants above.
inline std::string dumpJson(const Json &j)
{
    return j.dump(-1, ' ', true, Json::error_handler_t::replace);
}

/*******************************************************************************
 * EMISSION - calaos_script to calaos_server
 ******************************************************************************/

/*
 * The envelope both sendJson() bodies of ScriptBindings.cpp used to build,
 * duplicated verbatim in Lua_Calaos and in LuaIOBase. ScriptExec.cpp reads it
 * back with jansson_string_get(jroot, "msg") plus json_object_get(jroot,
 * "data"), so the two keys and the fact that "data" is an OBJECT are the
 * contract.
 *
 * msg_type is a std::string and param is a Params: exchanging the two
 * arguments does not compile.
 */
inline std::string buildDataMessage(const std::string &msg_type, const Params &param)
{
    Json jroot;
    jroot["msg"] = msg_type;
    jroot["data"] = param.toNJson();

    return dumpJson(jroot);
}

//ScriptBindings.cpp, sendPushNotif() - one argument form. The message text
//comes straight from the user's Lua script: this is the reachable channel the
//error handler of dumpJson() exists for.
inline std::string buildPushNotifMessage(const std::string &message)
{
    Params p = {{ "message", message }};

    return buildDataMessage("send_push_notif", p);
}

//ScriptBindings.cpp, sendPushNotif() - two arguments form.
inline std::string buildPushNotifMessage(const std::string &message,
                                         const PushAttachment &attachment)
{
    Params p = {{ "message", message },
                { "attachment", attachment.value }};

    return buildDataMessage("send_push_notif", p);
}

//ScriptBindings.cpp, the three LuaIOBase::set_value() overloads. They differ
//only in how the Lua value became a string before this point: bool -> the word
//"true"/"false", double -> Utils::to_string(double), string -> as is.
inline std::string buildSetStateMessage(const IoId &id, const std::string &value)
{
    Params p = {{ "id", id.value },
                { "value", value }};

    return buildDataMessage("set_state", p);
}

//ScriptBindings.cpp, LuaIOBase::set_param().
inline std::string buildSetParamMessage(const IoId &id, const ParamKey &key,
                                        const ParamValue &value)
{
    Params p = {{ "id", id.value },
                { "param", key.value },
                { "value", value.value }};

    return buildDataMessage("set_param", p);
}

/*
 * ScriptExtern_main.cpp - the only message calaos_script sends on its own
 * initiative, and the only one that is FLAT: no "data" envelope.
 *
 * The return value is a WORD, not a JSON boolean: ScriptExec.cpp:81 reads it
 * with jansson_string_get(jroot, "return_val", "false") and compares it to the
 * string "true". A real boolean here would always read back as "false".
 */
inline std::string buildFinishedMessage(bool return_val)
{
    Params pret = {{ "msg", "finished" },
                   { "return_val", return_val?"true":"false" }};

    return dumpJson(pret.toNJson());
}

/*******************************************************************************
 * DECODING - calaos_server to calaos_script
 ******************************************************************************/

/*
 * json_loads() plus json_is_object(), verbatim. jansson had no JSON_DECODE_ANY
 * here, so a top level scalar did not even parse; a top level array parsed but
 * failed json_is_object(). Both ended in the same "Error parsing json from sub
 * process" branch, and both still answer false.
 *
 * NOTE, deliberate and worth knowing: jansson filled a json_error_t whose
 * .text the warning used to print. nlohmann's non throwing parse has no
 * equivalent, so that detail is gone from the log; the raw message is still
 * printed next to it, which is what the reader actually needs. Same choice as
 * WagoWire and ReolinkWire next door.
 */
inline bool parseMessage(const std::string &msg, Json &jroot)
{
    Json parsed = Json::parse(msg, nullptr, false);

    if (parsed.is_discarded() || !parsed.is_object())
        return false;

    jroot = std::move(parsed);
    return true;
}

/*
 * jansson_string_get()'s CONTRACT, kept BY HAND and on purpose: the default
 * comes back when the key is ABSENT and when the value is NOT A STRING, and it
 * never throws.
 *
 * Neither j["k"].get<std::string>() nor j.value("k", def) is a translation of
 * that: the first throws on both, the second still throws on a type mismatch.
 * tests/ScriptWire_test.cpp pins the two forms separately.
 */
inline std::string stringGet(const Json &j, const std::string &key,
                             const std::string &default_value = std::string())
{
    const Json::const_iterator it = j.find(key);

    if (it == j.cend() || !it->is_string())
        return default_value;

    return it->get<std::string>();
}

/*
 * jansson_decode_object()'s FLATTENING CONTRACT, kept BY HAND: a string as is,
 * a boolean as the WORD "true"/"false", any number through
 * Utils::to_string(double) - a bare ostringstream, six significant digits then
 * scientific notation, frozen on purpose and not "fixed" here - and ANY OTHER
 * TYPE (object, array, null) the EMPTY STRING, WITH THE KEY STILL ADDED. That
 * last clause matters downstream: an absent key and a key at "" are not the
 * same thing.
 *
 * Params::fromNJson() is NOT a substitute, measured: it assigns the json value
 * straight into a std::string, which throws type_error.302 on anything that is
 * not a JSON string - and every IO of a real "execute" context carries a
 * boolean and a number. A tripwire in the test pins that.
 *
 * A non object (including a null, which is what an absent key parses to)
 * iterates zero times, exactly as json_object_foreach() did on a NULL or on a
 * non object.
 */
inline void decodeObject(const Json &j, Params &params)
{
    if (!j.is_object())
        return;

    for (Json::const_iterator it = j.cbegin(); it != j.cend(); ++it)
    {
        std::string svalue;

        if (it.value().is_string())
            svalue = it.value().get<std::string>();
        else if (it.value().is_boolean())
            svalue = it.value().get<bool>()?"true":"false";
        else if (it.value().is_number())
            svalue = Utils::to_string(it.value().get<double>());

        params.Add(it.key(), svalue);
    }
}

/*
 * The "context" of an "execute" message: an ARRAY of IO objects, not an
 * object. Absent, or present but not an array, yields nothing - json_array_
 * foreach() ran json_array_size(NULL) == 0 and never entered the loop.
 *
 * The order of the array is preserved, which is what lets the test detect an
 * exchange of two entries.
 */
inline std::vector<Params> decodeContext(const Json &jroot)
{
    std::vector<Params> ios;

    const Json::const_iterator it = jroot.find("context");
    if (it == jroot.cend() || !it->is_array())
        return ios;

    for (Json::const_iterator e = it->cbegin(); e != it->cend(); ++e)
    {
        Params p;
        decodeObject(*e, p);
        ios.push_back(p);
    }

    return ios;
}

//The "env" of an "execute" message: a flat object of strings. Absent, or
//present at another type, leaves env untouched.
inline void decodeEnv(const Json &jroot, Params &env)
{
    const Json::const_iterator it = jroot.find("env");
    if (it == jroot.cend())
        return;

    decodeObject(*it, env);
}

/*
 * ⭐ THE PITFALL OF THIS TICKET, refused by construction.
 *
 * The jansson original walked TWO levels down a document it did not own:
 *
 *     json_t *jev = json_object_get(jroot, "data");   // may be NULL
 *     json_t *jdata = nullptr;
 *     if (jev) jdata = json_object_get(jev, "data");  // NULL safe
 *
 * json_object_get() answers NULL both on a NULL and on a non object, so the
 * whole walk was total. The nlohmann equivalent MUST NOT be operator[]: on a
 * non const Json, jroot["data"] CREATES the key and turns the null into an
 * object, so a document that carried no "data" silently grows one - and the
 * outputs stay empty either way, so no assertion on them can see it.
 *
 * jroot is taken by CONST REFERENCE precisely so that the creating overload
 * cannot be reached from here, and find() answers cend() on every type that is
 * not an object. tests/ScriptWire_test.cpp re-serializes the root after the
 * call and compares it to the document it passed in.
 *
 * "type_str" lives on the EVENT, its parameters live on the event's inner
 * "data". Reading either one level off gives an empty result.
 */
inline void decodeEvent(const Json &jroot, Params &ev, std::string &type_str)
{
    ev.clear();
    type_str.clear();

    const Json::const_iterator jev = jroot.find("data");
    if (jev == jroot.cend())
        return;

    const Json::const_iterator jdata = jev->find("data");
    if (jdata != jev->cend())
        decodeObject(*jdata, ev);

    type_str = stringGet(*jev, "type_str");
}

} //namespace ScriptWire

#endif // S_SCRIPT_WIRE_H
