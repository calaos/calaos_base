/******************************************************************************
 **  Copyright (c) 2026, Calaos. All Rights Reserved.
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
#ifndef S_KLF200_WIRE_H
#define S_KLF200_WIRE_H

#include <cmath>
#include <string>

#include "Params.h"
#include "StringUtils.h"
#include "Klf200Types.h"

/* JSON messages between Klf200Ctrl and ExternProcKlf200_main.py (shapes at
 * the top of that file, keep both in sync). The register message carries the
 * gateway password in clear: never log a message. */
namespace Klf200Wire
{

inline std::string buildRegisterMessage(const Klf200Types::Host &host,
                                        const Klf200Types::Password &password,
                                        const Klf200Types::NodeId &nodeId,
                                        bool rain)
{
    Json jroot;
    jroot["action"] = "register";
    jroot["host"] = host.v;
    jroot["password"] = password.v;
    jroot["node_id"] = nodeId.v;
    jroot["rain"] = rain;

    return jroot.dump(-1, ' ', true, Json::error_handler_t::replace);
}

/* Position: 0 = fully open (up), 100 = fully closed (down); clamped here so
 * the sidecar never sees a value outside the range it documents. */
inline std::string buildSetPositionMessage(const Klf200Types::Host &host,
                                           const Klf200Types::NodeId &nodeId,
                                           int position)
{
    if (position < 0) position = 0;
    if (position > 100) position = 100;

    Json jroot;
    jroot["action"] = "set_position";
    jroot["host"] = host.v;
    jroot["node_id"] = nodeId.v;
    jroot["position"] = position;

    return jroot.dump(-1, ' ', true, Json::error_handler_t::replace);
}

inline std::string buildStopMessage(const Klf200Types::Host &host,
                                    const Klf200Types::NodeId &nodeId)
{
    Json jroot;
    jroot["action"] = "stop";
    jroot["host"] = host.v;
    jroot["node_id"] = nodeId.v;

    return jroot.dump(-1, ' ', true, Json::error_handler_t::replace);
}

//The last IO of a node is gone: the process stops watching it
inline std::string buildUnregisterMessage(const Klf200Types::Host &host,
                                          const Klf200Types::NodeId &nodeId)
{
    Json jroot;
    jroot["action"] = "unregister";
    jroot["host"] = host.v;
    jroot["node_id"] = nodeId.v;

    return jroot.dump(-1, ' ', true, Json::error_handler_t::replace);
}

/*
 * The error codes this end knows. The sidecar wraps exceptions raised by
 * pyvlx in "message": that text is not ours to shape and does not reach the
 * journal; only a code from this list does. Mirror of the ERR_* constants of
 * ExternProcKlf200_main.py.
 */
inline bool isKnownErrorCode(const std::string &code)
{
    return code == "gateway_connect_failed" ||
           code == "gateway_not_connected" ||
           code == "node_not_found" ||
           code == "node_unsupported" ||
           code == "command_failed";
}

/*
 * Flatten one message from the external process into a Params, every value
 * as a string: a string as is, a boolean as "true"/"false", a number through
 * Utils::to_string(double), anything else (object, array, null) the empty
 * string with the key still present. Same contract as ReolinkWire, and for
 * the same reason: Params::fromJson() throws on any non-string value, and
 * every node event carries "node_id" (int), "position" (int) and
 * "rain_sensor" (bool).
 *
 * Answers false on a malformed payload or a top-level scalar; a top-level
 * array is accepted and yields no parameter.
 */
inline bool decodeMessage(const std::string &msg, Params &params)
{
    const Json jroot = Json::parse(msg, nullptr, false);

    if (jroot.is_discarded() || (!jroot.is_object() && !jroot.is_array()))
        return false;

    if (!jroot.is_object())
        return true;

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

/*
 * The node_id of a decoded message, or -1 when absent, not an integer, or
 * outside 0..255 (a KLF 200 node index is one byte). Numbers cross
 * decodeMessage() as Utils::to_string(double), so "3" and "3.000000" are
 * both accepted here, but "3.7" is refused rather than routed to node 3, and
 * a huge value is refused before the cast, which would be undefined.
 */
inline int nodeIdOf(const Params &params)
{
    if (!params.Exists("node_id"))
        return -1;

    double d;
    if (!Utils::is_of_type<double>(params["node_id"]))
        return -1;
    Utils::from_string(params["node_id"], d);
    if (!(d >= 0 && d <= 255) || d != std::floor(d))
        return -1;

    return static_cast<int>(d);
}

/* The io_style a node event calls for: "window" for a VELUX window opener,
 * "shutter" for everything else the driver accepts. "node_type" is the pyvlx
 * class name the sidecar reports. */
inline std::string styleOf(const Params &event)
{
    return event["node_type"] == "Window" ? "window" : "shutter";
}

/* The position of a node event: 0..100, or -1 when the gateway has not
 * reported one yet, or the field is missing, garbled or not an integer (same
 * rule as nodeIdOf(): a fraction is refused, never truncated). */
inline int positionOf(const Params &params)
{
    if (!params.Exists("position"))
        return -1;

    double d;
    if (!Utils::is_of_type<double>(params["position"]))
        return -1;
    Utils::from_string(params["position"], d);
    if (!(d >= 0 && d <= 100) || d != std::floor(d))
        return -1;

    return static_cast<int>(d);
}

} //namespace Klf200Wire

#endif
