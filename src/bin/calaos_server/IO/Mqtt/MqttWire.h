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
#ifndef S_MQTT_WIRE_H
#define S_MQTT_WIRE_H

#include "Params.h"
#include "Utils.h"

/*
 * E4.1g - the MQTT wire, in one place.
 *
 * Both ends of this wire are ours and ship together: MqttCtrl.cpp inside
 * calaos_server and MqttExternProc_main.cpp inside calaos_mqtt, built by the
 * same Makefile.am and installed by the same package. The broker never sees
 * this JSON: it only ever gets "payload", as a byte passthrough. So the shape
 * of the bytes on this wire has no third party to protect - what matters is
 * the structure, the values, and above all the LENGTH of a payload.
 *
 * The functions used to live twice, in two anonymous namespaces inside two
 * programs that define main(), which is why nothing could ever test them.
 * They live here so that tests/MqttWire_test.cpp can.
 *
 * THE THREE EMISSION INVARIANTS of E4.1 are applied in dumpJson() and nowhere
 * else, so there is exactly one place to check: sorted keys (plain
 * nlohmann::json, never ordered_json), ensure_ascii = true, and
 * error_handler_t::replace.
 */

namespace MqttWire
{

/*
 * Strict UTF-8 acceptance, i.e. exactly what jansson's json_stringn() used to
 * accept: no overlong form, no UTF-16 surrogate, nothing above U+10FFFF, no
 * truncated sequence. A bare NUL byte IS accepted - it is a perfectly legal
 * code point and the whole point of the payload handling below.
 *
 * Measured against json_stringn() over every 1, 2 and 3 byte sequence
 * (16 843 008 of them) plus 4 byte samples: identical verdict on all of them.
 */
inline bool isStrictUtf8(const char *data, size_t len)
{
    size_t i = 0;
    while (i < len)
    {
        const unsigned char c = static_cast<unsigned char>(data[i]);
        size_t seqlen;
        unsigned char lo, hi; //allowed range of the FIRST continuation byte

        if (c <= 0x7F)                   { i++; continue; }
        else if (c >= 0xC2 && c <= 0xDF) { seqlen = 2; lo = 0x80; hi = 0xBF; }
        else if (c == 0xE0)              { seqlen = 3; lo = 0xA0; hi = 0xBF; } //no overlong
        else if (c >= 0xE1 && c <= 0xEC) { seqlen = 3; lo = 0x80; hi = 0xBF; }
        else if (c == 0xED)              { seqlen = 3; lo = 0x80; hi = 0x9F; } //no surrogate
        else if (c >= 0xEE && c <= 0xEF) { seqlen = 3; lo = 0x80; hi = 0xBF; }
        else if (c == 0xF0)              { seqlen = 4; lo = 0x90; hi = 0xBF; } //no overlong
        else if (c >= 0xF1 && c <= 0xF3) { seqlen = 4; lo = 0x80; hi = 0xBF; }
        else if (c == 0xF4)              { seqlen = 4; lo = 0x80; hi = 0x8F; } //<= U+10FFFF
        else return false; //0x80-0xC1 and 0xF5-0xFF are never a first byte

        if (i + seqlen > len)
            return false;
        const unsigned char c1 = static_cast<unsigned char>(data[i + 1]);
        if (c1 < lo || c1 > hi)
            return false;
        for (size_t j = 2;j < seqlen;j++)
        {
            const unsigned char cj = static_cast<unsigned char>(data[i + j]);
            if (cj < 0x80 || cj > 0xBF)
                return false;
        }
        i += seqlen;
    }
    return true;
}

//Replace every byte that is not part of a structurally valid UTF-8 sequence
//with '?'. Deliberately strict on structure only; the caller re-checks with
//isStrictUtf8() and falls back once more. One '?' per rejected byte, so the
//length of the payload never changes here.
inline std::string sanitizeUtf8(const char *data, size_t len)
{
    std::string out;
    out.reserve(len);
    size_t i = 0;
    while (i < len)
    {
        unsigned char c = static_cast<unsigned char>(data[i]);
        size_t seqlen = 0;
        if (c < 0x80) seqlen = 1;
        else if ((c & 0xE0) == 0xC0) seqlen = 2;
        else if ((c & 0xF0) == 0xE0) seqlen = 3;
        else if ((c & 0xF8) == 0xF0) seqlen = 4;

        bool valid = seqlen > 0 && i + seqlen <= len;
        for (size_t j = 1;valid && j < seqlen;j++)
        {
            if ((static_cast<unsigned char>(data[i + j]) & 0xC0) != 0x80)
                valid = false;
        }

        if (valid)
        {
            out.append(data + i, seqlen);
            i += seqlen;
        }
        else
        {
            out.push_back('?');
            i++;
        }
    }
    return out;
}

/*
 * Turn a raw MQTT payload into the string that goes on the wire. The payload
 * is NOT a C string: it can be empty (NULL), carry embedded NUL bytes, or be
 * arbitrary binary. std::string(data, len) - never std::string(data), which
 * would truncate at the first NUL and put back exactly the bug this code was
 * written to fix.
 *
 * Never fails: a payload that is not valid UTF-8 is delivered with its
 * invalid bytes replaced by '?'. That '?' is a shipped user decision (see
 * RELEASE_NOTES, "Fiabilite"), which is why the sanitizing happens HERE, at
 * construction, and not at dump() time: the error handler of dump() would
 * write U+FFFD instead. It stays a real safety net for everything else that
 * reaches dumpJson() - the topic, in particular, which comes from the broker
 * too but carries no such decision.
 */
inline std::string payloadToString(const void *payload, int payloadlen)
{
    const char *data = static_cast<const char *>(payload);
    const size_t len = (data && payloadlen > 0)?static_cast<size_t>(payloadlen):0;
    std::string raw(data?data:"", len);

    if (isStrictUtf8(raw.data(), raw.size()))
        return raw;

    //binary (non UTF-8) payload: sanitize it instead of dropping the message
    cWarningDom("mqtt") << "Binary (non UTF-8) payload received, invalid bytes are replaced with '?'";
    std::string sane = sanitizeUtf8(raw.data(), raw.size());
    if (isStrictUtf8(sane.data(), sane.size()))
        return sane;

    //still refused (overlong or surrogate encodings, which are structurally
    //well formed): keep only ASCII so the field is guaranteed to be there
    for (char &c: sane)
    {
        if (static_cast<unsigned char>(c) > 0x7F)
            c = '?';
    }
    return sane;
}

/*
 * THE ONLY dump() OF THIS WIRE. The three E4.1 emission invariants live here:
 *  - plain nlohmann::json, so the keys come out sorted. Assumed: the order of
 *    the keys is not semantic on this wire, both ends read it with a real
 *    parser.
 *  - ensure_ascii = true, so the wire stays pure ASCII the way it was under
 *    jansson's JSON_ENSURE_ASCII. Only the case of the hex digits changes
 *    (jansson wrote them upper case, nlohmann writes them lower case), and a
 *    JSON parser cannot tell the difference.
 *  - error_handler_t::replace, so invalid UTF-8 becomes U+FFFD instead of
 *    throwing type_error.316 out of a dump() that nobody catches. NOT a
 *    try/catch: a throw here would take down a live process.
 */
inline std::string dumpJson(const Json &j)
{
    return j.dump(-1, ' ', true, Json::error_handler_t::replace);
}

//One MQTT message, in either direction: {topic, payload}. The two are always
//distinct fields and are never interchangeable.
inline std::string encodeMessage(const std::string &topic, const std::string &payload)
{
    Json root;
    root["topic"] = topic;
    root["payload"] = payload;
    return dumpJson(root);
}

//The three broker settings, defaults included: an absent OR EMPTY value falls
//back on the built in one.
inline void resolveBroker(const Params &params,
                          std::string &host, std::string &port, std::string &keepalive)
{
    host = "127.0.0.1";
    port = "1883";
    keepalive = "120";

    if (params.Exists("host") && !params["host"].empty())
        host = params["host"];
    if (params.Exists("port") && !params["port"].empty())
        port = params["port"];
    if (params.Exists("keepalive") && !params["keepalive"].empty())
        keepalive = params["keepalive"];
}

/*
 * The key that tells the broker configuration from a publish request. Both
 * directions of this wire are flat objects, and a configuration whose three
 * broker fields all sat on their defaults would otherwise read as a publish on
 * topic "".
 */
inline const char *configAction() { return "config"; }

/*
 * The broker configuration, sent as the FIRST MESSAGE OF THE SOCKET and never
 * as an argument: /proc/<pid>/cmdline is world readable, so a password in the
 * argv is readable by every account of the machine for the whole life of the
 * sidecar. Same shape as ReolinkWire::buildRegisterMessage(), which hands the
 * camera credentials over the same channel.
 *
 * Credentials are emitted only when BOTH of them are set.
 *
 * NOTE for whoever adds logging here: this message carries the broker password
 * IN CLEAR. Never log the message itself, on either end.
 */
inline std::string encodeConfig(const Params &params)
{
    std::string host, port, keepalive;
    resolveBroker(params, host, port, keepalive);

    Json root;
    root["action"] = configAction();
    root["host"] = host;
    root["port"] = port;
    root["keepalive"] = keepalive;

    if (params.Exists("user") && params.Exists("password"))
    {
        root["user"] = params["user"];
        root["password"] = params["password"];
    }

    return dumpJson(root);
}

/*
 * Read a message coming from the other end. NON THROWING form on purpose: the
 * text comes from another process and, one hop upstream, from the broker.
 * Answers false - and leaves out untouched - for anything that is not a JSON
 * object, which is what both ends already wanted.
 *
 * Every value is flattened to a STRING, exactly like the jansson
 * jansson_decode_object() it replaces: the oracle of the golden suite is type
 * strict (3 is not "3"), so a value that turned into a JSON number would be a
 * break of contract. A null, an array or an object give an empty string, and
 * the key is still added - an absent key stays absent, it does not become null.
 */
inline bool decodeMessage(const std::string &msg, Params &out)
{
    const Json jroot = Json::parse(msg, nullptr, /*allow_exceptions=*/false);

    if (jroot.is_discarded() || !jroot.is_object())
        return false;

    for (Json::const_iterator it = jroot.begin(); it != jroot.end(); ++it)
    {
        std::string svalue;

        if (it.value().is_string())
            svalue = it.value().get<std::string>();
        else if (it.value().is_boolean())
            svalue = it.value().get<bool>()?"true":"false";
        else if (it.value().is_number())
            svalue = Utils::to_string(it.value().get<double>());

        out.Add(it.key(), svalue);
    }

    return true;
}

} //namespace MqttWire

#endif //S_MQTT_WIRE_H
