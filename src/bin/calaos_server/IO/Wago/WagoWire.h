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
#ifndef S_WAGO_WIRE_H
#define S_WAGO_WIRE_H

#include <string>
#include <vector>

#include "Params.h"
#include "Utils.h"

/*
 * E4.1h - the Wago wire, in one place.
 *
 * Both ends of this wire are ours and ship together: WagoMap.cpp inside
 * calaos_server and WagoExternProc_main.cpp inside calaos_wago, built by the
 * same Makefile.am, installed by the same package and updated together. The
 * modbus layer under them (WagoCtrl.cpp) contains no JSON at all, and the PLC
 * never sees this JSON - it only ever sees modbus frames. There is therefore
 * NO third party on this wire and the shape of its bytes has nobody to
 * protect; what matters is the structure, the values and their TYPES.
 *
 * The assembly used to live inline in two programs, one of which defines
 * main(), while the other one is reachable only through a singleton that
 * spawns a subprocess in its constructor - which is why nothing could ever
 * test it. It lives here so that tests/WagoWire_test.cpp can.
 *
 * EVERY VALUE ON THIS WIRE IS A JSON STRING, integers and booleans included.
 * That is load bearing, not sloppiness: WagoMap.cpp reads the reply values
 * with a string accessor, and a value that became a real JSON number or a real
 * JSON boolean would read back as nothing at all. The oracle of the test suite
 * is type-strict: 3 is not "3".
 *
 * THE THREE EMISSION INVARIANTS of E4.1 are applied in dumpJson() and nowhere
 * else, so there is exactly one place to check: sorted keys (plain
 * nlohmann::json, never ordered_json), ensure_ascii = true, and
 * error_handler_t::replace.
 */

namespace WagoWire
{

/*
 * THE ONLY dump() OF THIS WIRE.
 *  - plain nlohmann::json, so the keys come out SORTED. On the REQUESTS this
 *    changes nothing at all: they were built from a Params, which is a
 *    std::map, so the jansson adapter already walked them alphabetically. On
 *    the REPLIES, assembled key by key, the order goes from insertion to
 *    sorted - the one byte-level change of this ticket, invisible to the real
 *    JSON parser at the other end.
 *  - ensure_ascii = true, so the wire stays pure ASCII exactly as jansson's
 *    JSON_ENSURE_ASCII kept it. Only the case of the hex digits changes
 *    (é becomes é), which no JSON parser can see.
 *  - error_handler_t::replace, so an invalid UTF-8 byte becomes U+FFFD
 *    instead of throwing type_error.316 out of a dump() that nobody catches.
 *    NOT a try/catch: a throw here happens inside a subprocess callback with
 *    no handler anywhere on the path, i.e. std::terminate on a live driver.
 *    Nothing on this wire carries a byte we do not generate ourselves today -
 *    the values come back from WagoCtrl as vector<bool> / vector<UWord>, never
 *    as a buffer, so unlike the KNX wire no raw bus byte can reach a string
 *    here - but calaos_wago does ECHO four fields of the request it received,
 *    and the handler costs nothing.
 */
inline std::string dumpJson(const Json &j)
{
    return j.dump(-1, ' ', true, Json::error_handler_t::replace);
}

/*
 * The flattening contract of jansson_decode_object(), kept BY HAND: a string
 * as is, a boolean as the WORD "true"/"false", any number through
 * Utils::to_string(double) - a bare ostringstream that truncates to six
 * significant digits and flips to scientific notation, frozen on purpose - and
 * ANY OTHER TYPE (object, array, null) the EMPTY STRING.
 *
 * Params::fromNJson() is not a substitute: it assigns the json value straight
 * into a std::string and throws type_error.302 on anything that is not a
 * string.
 */
inline std::string flattenValue(const Json &v)
{
    if (v.is_string())
        return v.get<std::string>();
    if (v.is_boolean())
        return v.get<bool>()?"true":"false";
    if (v.is_number())
        return Utils::to_string(v.get<double>());
    return std::string();
}

/*******************************************************************************
 * REQUESTS - calaos_server (WagoMap) to calaos_wago
 *
 * The action literal is inside each function on purpose: the call site then
 * has no string to get wrong, and the only mistake it can still make is to
 * exchange address and count. That residual is measured and written up in
 * docs/refactoring/E4.1h.md - a free function cannot cover its own call site.
 ******************************************************************************/

inline std::string buildReadBitsRequest(const std::string &id, Utils::UWord address, int count)
{
    Json jroot;
    jroot["action"] = "read_bits";
    jroot["id"] = id;
    jroot["address"] = Utils::to_string(address);
    jroot["count"] = Utils::to_string(count);

    return dumpJson(jroot);
}

//Same message, other action. The 0x200 offset that turns this into a read of
//the OUTPUT image is applied by calaos_wago, never by the address on the wire.
inline std::string buildReadOutputBitsRequest(const std::string &id, Utils::UWord address, int count)
{
    Json jroot;
    jroot["action"] = "read_output_bits";
    jroot["id"] = id;
    jroot["address"] = Utils::to_string(address);
    jroot["count"] = Utils::to_string(count);

    return dumpJson(jroot);
}

//A single write carries "value" and NO "count".
inline std::string buildWriteBitRequest(const std::string &id, Utils::UWord address, bool value)
{
    Json jroot;
    jroot["action"] = "write_bit";
    jroot["id"] = id;
    jroot["address"] = Utils::to_string(address);
    jroot["value"] = value?"true":"false";

    return dumpJson(jroot);
}

/*
 * ⛔ E4.1h - THE BUG IS PORTED HERE, NOT FIXED.
 *
 * WagoMap::write_multiple_bits() built the values array into a json_t called
 * jret, attached it, then sent a SECOND, FRESH serialization of the four-key
 * Params - so the array never left the process, and jret (with the array it
 * owned) was never decref'd. The leak is gone for free with nlohmann, which
 * has no reference counting; the missing array is DELIBERATELY reproduced, so
 * that this commit changes not one byte of what a real PLC installation puts
 * on its wire.
 *
 * The `values` argument is therefore accepted and dropped on the floor. It is
 * accepted rather than removed so that fixing the bug is a change inside this
 * function only, with no call site to touch.
 *
 * tests/WagoWire_test.cpp pins the absence with two cases whose names end in
 * _BUG; whoever fixes this flips them.
 */
inline std::string buildWriteBitsRequest(const std::string &id, Utils::UWord address, int count,
                                         const std::vector<bool> &values)
{
    Json jroot;
    jroot["action"] = "write_bits";
    jroot["id"] = id;
    jroot["address"] = Utils::to_string(address);
    jroot["count"] = Utils::to_string(count);

    (void)values; //see the comment above: the bug is ported, not fixed

    return dumpJson(jroot);
}

inline std::string buildReadWordsRequest(const std::string &id, Utils::UWord address, int count)
{
    Json jroot;
    jroot["action"] = "read_words";
    jroot["id"] = id;
    jroot["address"] = Utils::to_string(address);
    jroot["count"] = Utils::to_string(count);

    return dumpJson(jroot);
}

inline std::string buildReadOutputWordsRequest(const std::string &id, Utils::UWord address, int count)
{
    Json jroot;
    jroot["action"] = "read_output_words";
    jroot["id"] = id;
    jroot["address"] = Utils::to_string(address);
    jroot["count"] = Utils::to_string(count);

    return dumpJson(jroot);
}

inline std::string buildWriteWordRequest(const std::string &id, Utils::UWord address, Utils::UWord value)
{
    Json jroot;
    jroot["action"] = "write_word";
    jroot["id"] = id;
    jroot["address"] = Utils::to_string(address);
    jroot["value"] = Utils::to_string(value);

    return dumpJson(jroot);
}

//⛔ Same bug as buildWriteBitsRequest(), ported the same way. See there.
inline std::string buildWriteWordsRequest(const std::string &id, Utils::UWord address, int count,
                                          const std::vector<Utils::UWord> &values)
{
    Json jroot;
    jroot["action"] = "write_words";
    jroot["id"] = id;
    jroot["address"] = Utils::to_string(address);
    jroot["count"] = Utils::to_string(count);

    (void)values; //see buildWriteBitsRequest(): the bug is ported, not fixed

    return dumpJson(jroot);
}

/*******************************************************************************
 * REPLIES - calaos_wago to calaos_server (WagoMap)
 *
 * Both builders take the Params the process just decoded, because that is what
 * they echo. Taking the decoded request rather than four loose strings is also
 * what stops the call site from being able to permute id / action / address /
 * count: the echo happens here, in code the test suite calls.
 ******************************************************************************/

//The answer to read_bits, read_output_bits, read_words and read_output_words.
//Every entry of "values" is a STRING: "true"/"false" for bits, decimal digits
//for words.
inline std::string buildReadReply(const Params &request, bool status,
                                  const std::vector<std::string> &values)
{
    Json jroot;
    jroot["id"] = request["id"];
    jroot["action"] = request["action"];
    jroot["address"] = request["address"];
    jroot["count"] = request["count"];
    jroot["status"] = status?"true":"false";

    Json jarr = Json::array();
    for (size_t i = 0;i < values.size();i++)
        jarr.push_back(values[i]);
    jroot["values"] = jarr;

    return dumpJson(jroot);
}

//The answer to write_bit, write_bits, write_word and write_words: two keys and
//nothing else. WagoMap dispatches on the command it remembered under that id,
//so no action is echoed back.
inline std::string buildStatusReply(const Params &request, bool status)
{
    Json jroot;
    jroot["id"] = request["id"];
    jroot["status"] = status?"true":"false";

    return dumpJson(jroot);
}

/*******************************************************************************
 * DECODING - used at BOTH ends
 ******************************************************************************/

/*
 * Read a message coming from the other end. NON THROWING on purpose: the text
 * comes from another process, over a pipe.
 *
 * Answers false exactly where json_loads() answered NULL or the
 * json_is_object() test failed: malformed text, a top level scalar (jansson
 * was called with no flag, so no JSON_DECODE_ANY) and a top level ARRAY. Both
 * ends then log and return without dispatching.
 *
 * When `values` is given, the "values" array is filled from the SAME parse -
 * both call sites used to read it off the raw json_t root, next to the
 * flattened Params. A "values" key that is absent, or present but not an
 * array, yields no value at all, exactly as json_array_foreach() did.
 * An entry that is not a string is flattened by the house rules rather than
 * dereferenced: json_string_value() answered NULL there and the old
 * `string v = json_string_value(value)` was undefined behaviour.
 *
 * Note that "values" ALSO lands in the Params, as an empty string under its
 * own key - that is what jansson_decode_object() did with an array, and a
 * present-but-empty key is not the same answer as an absent one.
 */
inline bool decodeMessage(const std::string &msg, Params &out,
                          std::vector<std::string> *values = nullptr)
{
    const Json jroot = Json::parse(msg, nullptr, /*allow_exceptions=*/false);

    if (jroot.is_discarded() || !jroot.is_object())
        return false;

    for (Json::const_iterator it = jroot.cbegin(); it != jroot.cend(); ++it)
        out.Add(it.key(), flattenValue(it.value()));

    if (values)
    {
        const Json::const_iterator vit = jroot.find("values");
        if (vit != jroot.cend() && vit->is_array())
        {
            for (const Json &v: *vit)
                values->push_back(flattenValue(v));
        }
    }

    return true;
}

} //namespace WagoWire

#endif //S_WAGO_WIRE_H
