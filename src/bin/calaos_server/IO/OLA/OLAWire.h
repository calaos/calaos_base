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
#ifndef S_OLA_WIRE_H
#define S_OLA_WIRE_H

#include <string>
#include <vector>

#include "Params.h"
#include "Utils.h"

/*
 * E4.1f - the OLA wire, in one place.
 *
 * Both ends of this wire are ours and ship together: OLACtrl.cpp inside
 * calaos_server and OLAExternProc_main.cpp inside calaos_ola, built by the
 * same Makefile.am, installed by the same package and updated together. The
 * DMX fixtures downstream never see this JSON - they only ever see a DMX512
 * frame produced by olad. There is therefore NO third party on this wire and
 * the shape of its bytes has nobody to protect; what matters is the
 * structure, the values and their TYPES.
 *
 * The assembly used to live inline in two programs, one of which defines
 * main() and links libola, while the other one is reachable only through a
 * singleton that spawns a subprocess in its constructor - which is why
 * nothing could ever test it. It lives here so that tests/OLAWire_test.cpp
 * can.
 *
 * ⭐ EVERY VALUE ON THIS WIRE IS A JSON INTEGER. This is the ONE wire of the
 * series that does not put everything out as a string, and the reflex of the
 * sibling tickets is the bug here, not the fix: calaos_ola reads `value` back
 * into an unsigned int, and a `value` that came out quoted would still be
 * accepted by the decoder (it flattens everything to strings anyway) while
 * saying nothing about the emitter having regressed. The type is therefore
 * pinned on the EMITTED TEXT by tests/OLAWire_test.cpp, never by a round
 * trip. Nothing here may ever go through Utils::to_string().
 *
 * ⚠️ THE ROOT IS AN ARRAY, NOT AN OBJECT. decodeMessage() keeps is_array()
 * exactly where json_is_array() was; an is_object() would refuse every real
 * message and stop the driver in silence.
 *
 * THE THREE EMISSION INVARIANTS of E4.1 are applied in dumpJson() and nowhere
 * else, so there is exactly one place to check: sorted keys (plain
 * nlohmann::json, never ordered_json), ensure_ascii = true, and
 * error_handler_t::replace.
 */

namespace OLAWire
{

/*******************************************************************************
 * ⭐ THE TYPES - so that a call site cannot permute two homogeneous arguments.
 *
 * Measured across three tickets of this series: a free function cannot cover
 * its own call site, and permuting two positional int arguments there stayed
 * GREEN every time. Typing closes it where a test cannot: with these wrappers
 * the swap does not compile at all.
 *
 * DmxChannel and DmxLevel are distinct, so buildSetValueMessage(channel,
 * percent) cannot take its two arguments the other way round.
 *
 * RedChannel / GreenChannel / BlueChannel are three distinct types over the
 * same DmxChannel, so buildSetColorMessage() cannot take the red channel where
 * it expects the blue one either - which is exactly the swap acceptance
 * criterion 4 of E4.1f asks to defend against. They derive from DmxChannel so
 * they still convert to it on the way in, and never to one another.
 *
 * The constructors are explicit: an int never turns into one of these by
 * accident.
 ******************************************************************************/

//A DMX512 channel number, as written in io.xml.
struct DmxChannel
{
    int v;
    explicit DmxChannel(int channel): v(channel) {}
};

//A DMX512 level, 0-255, i.e. what actually goes on the wire as "value".
struct DmxLevel
{
    int v;
    explicit DmxLevel(int level): v(level) {}
};

//A dimmer setting, 0-100. NOT a DmxLevel: it has to be scaled first.
struct DimmerPercent
{
    int v;
    explicit DimmerPercent(int percent): v(percent) {}
};

struct RedChannel: public DmxChannel
{
    explicit RedChannel(int channel): DmxChannel(channel) {}
};

struct GreenChannel: public DmxChannel
{
    explicit GreenChannel(int channel): DmxChannel(channel) {}
};

struct BlueChannel: public DmxChannel
{
    explicit BlueChannel(int channel): DmxChannel(channel) {}
};

/*******************************************************************************
 * EMISSION - calaos_server (OLACtrl) to calaos_ola
 ******************************************************************************/

/*
 * THE ONLY dump() OF THIS WIRE.
 *  - plain nlohmann::json, so the keys come out SORTED. On this wire that
 *    changes NOTHING at all: the only two keys are "channel" and "value" and
 *    they were already emitted in that (alphabetical) order by jansson. The
 *    byte string of both messages is identical before and after the
 *    migration, and tests/OLAWire_test.cpp freezes it.
 *  - ensure_ascii = true, so the wire stays pure ASCII exactly as jansson's
 *    JSON_ENSURE_ASCII kept it.
 *  - error_handler_t::replace, so an invalid UTF-8 byte becomes U+FFFD
 *    instead of throwing type_error.316 out of a dump() that nobody catches.
 *    NOT a try/catch: a throw here happens inside a subprocess callback with
 *    no handler anywhere on the path, i.e. std::terminate on a live driver.
 *
 *    ⚠️ SAID OUT LOUD RATHER THAN IMPLIED, because it was measured: THE LAST
 *    TWO INVARIANTS ARE DEFENSIVE ON THIS WIRE, NOT LOAD BEARING. The
 *    document assembled below contains exactly two keys, both ASCII string
 *    literals written above, and exactly two values, both C++ int. NO
 *    std::string IS EVER PUT INTO THIS TREE by either end. The channels come
 *    from io.xml through Utils::from_string() into an int; the level comes
 *    from OutputLightDimmer as a percentage or from ColorValue::getRed/
 *    getGreen/getBlue(); the universe - the only string OLACtrl handles - is
 *    passed to the subprocess as argv[1] and never enters the JSON. So unlike
 *    the KNX wire, where raw bus bytes reached a naked dump() and a dimmer at
 *    78% terminated the driver, no reachable input can put a non-ASCII byte
 *    here today. The two invariants guard this FUNCTION against the first
 *    string field somebody adds, and the two byte oracles of
 *    tests/OLAWire_test.cpp keep them red on mutation.
 */
inline std::string dumpJson(const Json &j)
{
    return j.dump(-1, ' ', true, Json::error_handler_t::replace);
}

/*
 * One entry of the array. TWO JSON INTEGERS - this is the contract of the
 * wire, see the head of this file.
 */
inline Json makeEntry(DmxChannel channel, DmxLevel level)
{
    Json entry;
    entry["channel"] = channel.v; //JSON INTEGER, never a string
    entry["value"] = level.v;     //JSON INTEGER, never a string
    return entry;
}

/*
 * A dimmer setting is a PERCENTAGE and the wire carries a DMX LEVEL. The
 * conversion is integer arithmetic and it is frozen: 0 -> 0, 1 -> 2,
 * 50 -> 127, 78 -> 198, 100 -> 255.
 */
inline DmxLevel percentToDmxLevel(DimmerPercent percent)
{
    return DmxLevel(percent.v * 255 / 100);
}

//OLACtrl::setValue(): one entry, the percentage scaled.
inline std::string buildSetValueMessage(DmxChannel channel, DimmerPercent percent)
{
    Json jroot = Json::array();
    jroot.push_back(makeEntry(channel, percentToDmxLevel(percent)));

    return dumpJson(jroot);
}

/*
 * OLACtrl::setColor(): THREE entries, in red / green / blue order.
 *
 * The ColorValue is taken whole rather than as three loose components, on
 * purpose and for the same reason WagoWire::buildReadReply() takes the decoded
 * request: the pairing of a component with its channel then happens HERE, in
 * code the test suite calls, and the call site has no way to hand the red
 * level to the blue channel.
 *
 * ⚠️ NOTE THE ASYMMETRY WITH buildSetValueMessage(), which is the shipped
 * behaviour and not an oversight: the components are already 0-255, so they
 * are emitted RAW. There is no *255/100 here.
 */
inline std::string buildSetColorMessage(const ColorValue &color,
                                        RedChannel red, GreenChannel green, BlueChannel blue)
{
    Json jroot = Json::array();
    jroot.push_back(makeEntry(red, DmxLevel(color.getRed())));
    jroot.push_back(makeEntry(green, DmxLevel(color.getGreen())));
    jroot.push_back(makeEntry(blue, DmxLevel(color.getBlue())));

    return dumpJson(jroot);
}

/*******************************************************************************
 * DECODING - calaos_ola
 ******************************************************************************/

//One decoded entry. NAMED members, so that the call site of SetChannel()
//cannot silently swap them the way two positional unsigned ints could.
struct ChannelValue
{
    unsigned int channel;
    unsigned int value;
};

/*
 * The flattening contract of jansson_decode_object(), kept BY HAND: a string
 * as is, a boolean as the WORD "true"/"false", any number through
 * Utils::to_string(double) - a bare ostringstream that truncates to six
 * significant digits and flips to scientific notation, frozen on purpose - and
 * ANY OTHER TYPE (object, array, null) the EMPTY STRING.
 *
 * Params::fromNJson() is not a substitute: it assigns the json value straight
 * into a std::string and throws type_error.302 on anything that is not a
 * string - which is EVERY entry of this wire, since they are all integers.
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

/*
 * Read a message coming from calaos_server. NON THROWING on purpose: the text
 * comes from another process, over a pipe.
 *
 * Answers false exactly where json_loads() answered NULL or the
 * json_is_array() test failed: malformed text, a top level scalar (jansson was
 * called with no flag, so no JSON_DECODE_ANY) and a top level OBJECT.
 *
 * Inside the array, the behaviour of json_array_foreach +
 * jansson_decode_object is reproduced exactly:
 *   - an entry that is not an object iterates zero times, so it yields an
 *     empty Params and is SKIPPED;
 *   - an entry missing "channel" or "value" is SKIPPED (Exists() is false);
 *   - a key that is present but unreadable ("" for null/object/array, "true"
 *     for a boolean) is NOT skipped: Exists() is true, from_string() fails and
 *     leaves 0. Present-and-empty is not the same answer as absent, and the
 *     difference is visible on the DMX bus.
 * tests/OLAWire_test.cpp pins each of those.
 *
 * The return codes of from_string() were not tested before and are not tested
 * here: since C++11 a failed extraction stores 0, so the behaviour is defined
 * and unchanged.
 */
inline bool decodeMessage(const std::string &msg, std::vector<ChannelValue> &out)
{
    const Json jroot = Json::parse(msg, nullptr, /*allow_exceptions=*/false);

    if (jroot.is_discarded() || !jroot.is_array())
        return false;

    for (const Json &entry: jroot)
    {
        if (!entry.is_object())
            continue; //json_object_foreach() iterated zero times

        Params p;
        for (Json::const_iterator it = entry.cbegin(); it != entry.cend(); ++it)
            p.Add(it.key(), flattenValue(it.value()));

        if (!p.Exists("channel") || !p.Exists("value"))
            continue;

        ChannelValue cv;
        Utils::from_string(p["channel"], cv.channel);
        Utils::from_string(p["value"], cv.value);
        out.push_back(cv);
    }

    return true;
}

} //namespace OLAWire

#endif //S_OLA_WIRE_H
