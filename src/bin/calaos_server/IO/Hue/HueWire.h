/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
#ifndef HUE_WIRE_H
#define HUE_WIRE_H

#include <string>

#include "Utils.h"

/*
 * E4.1d - the Philips Hue state reader, in one place.
 *
 * ⭐ READ ONLY, and there is NO dump() in this file or in the driver that uses
 * it. HueOutputLightRGB polls http://<bridge>/api/<key>/lights/<id> and
 * decodes state.{sat,bri,hue,on,reachable}; it never builds JSON - setOff()
 * sends the literal "{\"on\":false}" and setColor() concatenates three
 * integers into a string. ensure_ascii and error_handler_t::replace have
 * NOTHING to apply to here: there is no emission, not a defensive one either.
 *
 * The functions live here rather than inside HueOutputLightRGB because the
 * decoding sits in a lambda captured by a Timer inside the constructor of an
 * IO, which nothing can call from a test. Free functions can, and
 * tests/HueWire_test.cpp calls exactly these - the same code the server ships.
 * Same shape as MqttWire.h / ReolinkWire.h / WagoWire.h.
 *
 * ⛔ THE RISK OF THIS FILE IS TYPE TOLERANCE, NOT ESCAPING. Under jansson,
 * json_integer_value() answered 0 for an ABSENT key and for a key of the WRONG
 * TYPE, in silence, and jansson_bool_get() answered its default for anything
 * that was not a real JSON boolean. The "obvious" nlohmann port
 * j.value("sat", 0) does NOT reproduce that (measured):
 *
 *     payload        json_integer_value()   j.value("sat", 0)
 *     {"sat":12.5}   0                      12          silent wrong value
 *     {"sat":"77"}   0                      THROWS type_error.302
 *     {"sat":null}   0                      THROWS type_error.302
 *     {"sat":true}   0                      1           silent wrong value
 *     absent         0                      0           agree
 *
 * A throw here is a throw inside a UrlDownloader completion callback with no
 * try/catch anywhere on the path - std::terminate, on a poll that repeats
 * every two seconds, triggered by a THIRD PARTY device. Hence
 * integerOrDefault() and booleanOrDefault() below, written by hand.
 * tests/HueWire_test.cpp pins the contract shape by shape.
 */
namespace HueWire
{

/*
 * What decoding one bridge answer found. FOUR outcomes, because the driver
 * logs three different lines:
 *   - Ok          : root is an object and root.state is an object;
 *   - Malformed   : the answer is not JSON at all -> "Json received malformed".
 *                   ⚠️ A TOP LEVEL SCALAR lands here, because jansson's
 *                   json_loads() had no JSON_DECODE_ANY and refused `null`,
 *                   `3` and `"ok"`. Json::parse() accepts them, so
 *                   decodeLightState() guards explicitly to keep the shipped
 *                   acceptance set;
 *   - NotAnObject : parsed, root is not an object -> "Protocol changed ?".
 *                   A real bridge answers a top level ARRAY on a bad API key:
 *                   [{"error":{"type":1,"description":"unauthorized user"}}];
 *   - NoState     : root is an object, "state" is absent or not an object
 *                   -> "Protocol changed ?" (the same line).
 * All four end the same way: the poll returns without emitting any state
 * update at all, not even an unreachable one.
 */
enum class Decode
{
    Ok,
    Malformed,
    NotAnObject,
    NoState,
};

/*
 * The five fields the driver reads out of "state", and nothing else.
 *
 * ⚠️ THE RESIDUAL OF THE NAMED-STRUCT CLOSURE, written down because it is
 * exactly the kind of guarantee one believes is acquired. Passing a LightState
 * instead of three ints makes toStateUpdate(hue, sat, bri) impossible to
 * permute AT THE CALL SITE - but LightState is an AGGREGATE of three int and
 * two bool, so
 *      HueWire::LightState st{100, 200, 30000, true, true};
 * compiles (measured with -fsyntax-only) and silently permutes sat and bri.
 * Nothing writes that today - the driver default-constructs and fills BY NAME,
 * and so does the test - and the closure holds only for as long as that stays
 * true.
 *
 * ⭐ T3.31 CORRECTS THE ADVICE THAT USED TO END THIS COMMENT. It said "give
 * the fields distinct types OR delete the aggregate-ness with a constructor".
 * The second half is MEASURED FALSE: a constructor does remove aggregate-ness,
 * and it is ITSELF positional, so
 *      LightState(100, 200, 30000, true, true)
 * still compiles and still permutes sat and bri, with zero warnings at -Wall
 * -Wextra -Wconversion. It is not the aggregate that opens the hole, it is the
 * ORDER. ⇒ only DISTINCT TYPES PER FIELD close this one. See
 * docs/refactoring/T3.31.md section 7.2 and tests/ReolinkRegistry_test.cpp,
 * TheAggregateProbesActuallyDiscriminate.
 */
struct LightState
{
    int sat = 0;
    int bri = 0;
    int hue = 0;
    bool on = false;
    bool reachable = false;
};

//What the driver hands to updateHueState(): a colour and a state.
struct StateUpdate
{
    ColorValue color;
    bool on = false;
};

/*
 * json_integer_value(json_object_get(o, key)) with its default, EXACTLY.
 * Absent key -> def. Present but not a JSON integer (float, string, boolean,
 * null, object, array) -> def. Never throws. See the table at the top of this
 * file for why j.value(key, def) is not a substitute.
 *
 * is_number_integer() covers both the signed and the unsigned internal number
 * types, which together are jansson's single JSON_INTEGER.
 */
inline int integerOrDefault(const Json &j, const char *key, int def = 0)
{
    const Json::const_iterator it = j.find(key);

    if (it == j.end() || !it->is_number_integer())
        return def;

    return it->get<int>();
}

/*
 * jansson_bool_get() (Jansson_Addition.h), EXACTLY. Absent key -> def.
 * Present but not a JSON boolean -> def; in particular the NUMBER 1 and the
 * STRING "true" are not booleans. Never throws, so not get<bool>().
 */
inline bool booleanOrDefault(const Json &j, const char *key, bool def = false)
{
    const Json::const_iterator it = j.find(key);

    if (it == j.end() || !it->is_boolean())
        return def;

    return it->get<bool>();
}

/*
 * Decode one answer of the Hue bridge.
 *
 * `st` is written ONLY on Ok: a refused answer must never leave a half filled
 * state behind, because the driver returns without updating anything.
 *
 * ⚠️ TWO MEASURED DIVERGENCES, both deliberately not reproduced, both pinned by
 * TheTwoDeclaredAcceptanceDivergencesFromJansson in tests/HueWire_test.cpp:
 *
 *  1. AN ESCAPED NUL, "\u0000". jansson refused it outright ("\u0000 is not
 *     allowed without JSON_ALLOW_NUL" in a value, "NUL byte in object key not
 *     supported" in a key), so the answer was Malformed. nlohmann accepts it
 *     and decodes a real 0x00 byte into the std::string, so the answer becomes
 *     Ok. A RAW NUL byte is still refused by BOTH (jansson: "control character
 *     0x0"; nlohmann: discarded), so handing this function the whole
 *     std::string rather than a c_str() digs no hole - measured both ways.
 *  2. AN INTEGER BEYOND int64, e.g. {"sat":99999999999999999999}. jansson
 *     refused the WHOLE document ("too big integer"); nlohmann parses it as a
 *     float, which is not is_number_integer(), so integerOrDefault() answers
 *     its default. The answer moves from Malformed to Ok-with-sat-0.
 *     ⚠️ On that same payload j.value("sat", 0) answers -2147483648. One more
 *     reason the helper is written by hand.
 *
 * No Hue bridge sends either shape; reproducing them would mean re-scanning
 * the escapes and re-implementing a number lexer.
 *
 * ⚠️ MEASURED, so that nobody takes it for load bearing: the `is_discarded()`
 * half of the guard below is REDUNDANT with the other half - a discarded value
 * answers false to BOTH is_object() and is_array(). Removing it changes
 * nothing and turns no test red (an equivalent mutant, verified). It is kept
 * because it states the intent - "the parse failed" and "the root is a scalar"
 * are two different reasons - and because the day someone replaces the
 * container test, the parse test must still be there.
 */
inline Decode decodeLightState(const std::string &data, LightState &st)
{
    const Json jroot = Json::parse(data, nullptr, false);

    //where json_loads(data.c_str(), 0, &error) answered NULL, top level
    //scalars included: jansson had no JSON_DECODE_ANY
    if (jroot.is_discarded() || (!jroot.is_object() && !jroot.is_array()))
        return Decode::Malformed;

    if (!jroot.is_object())
        return Decode::NotAnObject;

    const Json::const_iterator tstate = jroot.find("state");
    if (tstate == jroot.end() || !tstate->is_object())
        return Decode::NoState;

    st.sat = integerOrDefault(*tstate, "sat");
    st.bri = integerOrDefault(*tstate, "bri");
    st.hue = integerOrDefault(*tstate, "hue");
    st.on = booleanOrDefault(*tstate, "on");
    st.reachable = booleanOrDefault(*tstate, "reachable");

    return Decode::Ok;
}

/*
 * Turn a decoded state into the pair the driver reports.
 *
 * An UNREACHABLE light is reported with an INVALID colour and OFF whatever
 * "on" said - the shipped branch passes `reachable`, not `on`, as the state.
 *
 * ⚠️ The three scalings are the shipped ones and they sit on TWO different
 * axes: hue is 0..65535 over 360 degrees, sat and bri are 0..255 over 100
 * percent. Taking a NAMED STRUCT rather than three ints is what makes
 * permuting sat and bri at the call site fail to compile; a test can only ever
 * catch the permutation inside the function.
 */
inline StateUpdate toStateUpdate(const LightState &st)
{
    StateUpdate u;

    if (!st.reachable)
    {
        u.color = ColorValue();
        u.on = false;
        return u;
    }

    u.color = ColorValue::fromHsl(static_cast<int>(st.hue * 360.0 / 65535.0),
                                  static_cast<int>(st.sat * 100.0 / 255.0),
                                  static_cast<int>(st.bri * 100.0 / 255.0));
    u.on = st.on;

    return u;
}

} //namespace HueWire

#endif // HUE_WIRE_H
