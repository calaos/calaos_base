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
#ifndef __REOLINK_WIRE_H__
#define __REOLINK_WIRE_H__

#include <string>

#include "Params.h"
#include "ReolinkTypes.h"
#include "StringUtils.h"

/*
 * The two ends of the wire between ReolinkCtrl and the calaos_reolink
 * external process (IO/Reolink/ExternProcReolink_main.py, in this
 * repository, decoding with the stdlib "json" module).
 *
 * They live here rather than inside ReolinkCtrl for one reason: ReolinkCtrl
 * is a singleton that spawns a process in its constructor, so nothing can
 * call it from a test. Free functions can, and tests/ReolinkWire_test.cpp
 * calls exactly these - the same text the server ships - so that a mutation
 * of the emitter or of the decoder turns that suite red. Same shape as
 * ReolinkEventRegistry.h next door.
 */
namespace ReolinkWire
{

/*
 * The registration message: {action, hostname, username, password,
 * event_type}, all five values JSON STRINGS.
 *
 * The three emission invariants of E4.1 apply to this dump(), and they go
 * together:
 *   - keys come out SORTED (nlohmann::json, never ordered_json - user
 *     decision of 2026-08-17). jansson emitted them in insertion order; the
 *     python side decodes with a real parser and is indifferent to it;
 *   - ensure_ascii = true, so the wire stays pure ASCII exactly as
 *     JSON_ENSURE_ASCII kept it. Only the CASE of the hexadecimal changes
 *     (the escape of U+00E9 goes from \u00E9 to \u00e9), which no JSON parser can see;
 *   - error_handler_t::replace, because hostname/username/password/
 *     event_type are io.xml parameters, i.e. bytes we do not control. On an
 *     invalid UTF-8 byte nlohmann's dump() throws type_error.316, and this
 *     message is built from a process callback with no try/catch anywhere on
 *     the path: that is std::terminate on a live server. replace turns the
 *     bad byte into U+FFFD instead.
 *
 * NOTE for whoever adds logging here: this message carries the camera
 * password IN CLEAR. Never log the message itself.
 */
/*
 * T3.31 - the four parameters are FOUR DISTINCT TYPES, not four std::string.
 *
 * They used to be four bare strings in a row: any two of them could be
 * exchanged at the call site with no diagnostic whatsoever, and no test could
 * catch it (ReolinkCtrl is not linked into ReolinkWire_test and cannot be).
 * See IO/Reolink/ReolinkTypes.h for what the shape of those wrappers buys and
 * what it does not, and tests/ReolinkWire_test.cpp for the oracle.
 */
inline std::string buildRegisterMessage(const ReolinkTypes::Hostname &hostname,
                                        const ReolinkTypes::Username &username,
                                        const ReolinkTypes::Password &password,
                                        const ReolinkTypes::EventType &event_type)
{
    Json jroot;
    jroot["action"] = "register";
    jroot["hostname"] = hostname.v;
    jroot["username"] = username.v;
    jroot["password"] = password.v;
    jroot["event_type"] = event_type.v;

    return jroot.dump(-1, ' ', true, Json::error_handler_t::replace);
}

/*
 * Flatten one message from the external process into a Params.
 *
 * This is jansson_decode_object()'s contract, kept BY HAND and on purpose:
 * every value becomes a string - a string as is, a boolean as the word
 * "true"/"false", any number through Utils::to_string(double), and ANY OTHER
 * TYPE (object, array, null) the EMPTY STRING, with the key still added.
 *
 * Params::fromJson() is NOT a substitute, measured: it assigns the json
 * value straight into a std::string, which throws type_error.302 on anything
 * that is not a JSON string. Every detection event of the python driver
 * carries "channel" (int), "tcp_push_active" (bool) and "callback_duration"
 * (float), and the health answer carries two nested objects - so fromJson()
 * would throw on every single camera event, inside a callback with no
 * try/catch on the path. tests/ReolinkWire_test.cpp pins that with a
 * tripwire.
 *
 * Answers false exactly where json_loads() answered NULL: a malformed
 * payload, or a top-level scalar (jansson had no JSON_DECODE_ANY). A
 * top-level array is accepted and yields no parameter at all, as before.
 */
inline bool decodeMessage(const std::string &msg, Params &params)
{
    const Json jroot = Json::parse(msg, nullptr, false);

    if (jroot.is_discarded() || (!jroot.is_object() && !jroot.is_array()))
        return false;

    if (!jroot.is_object())
        return true; //an array: nothing to flatten, as json_object_foreach did

    for (Json::const_iterator it = jroot.cbegin(); it != jroot.cend(); ++it)
    {
        std::string svalue;

        if (it.value().is_string())
            svalue = it.value().get<std::string>();
        else if (it.value().is_boolean())
            svalue = it.value().get<bool>() ? "true" : "false";
        else if (it.value().is_number())
            svalue = Utils::to_string(it.value().get<double>());

        params.Add(it.key(), svalue);
    }

    return true;
}

} //namespace ReolinkWire

#endif // __REOLINK_WIRE_H__
