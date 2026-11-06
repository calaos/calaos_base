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
#ifndef S_SQUEEZEBOX_WIRE_H
#define S_SQUEEZEBOX_WIRE_H

#include <string>

#include "Utils.h"

/*
 * E4.1d - the Squeezebox JSON reader, in one place.
 *
 * ⭐ THIS IS NOT A WIRE, IT IS A READER. Squeezebox talks to a THIRD PARTY
 * server (Logitech Media Server) over /jsonrpc.js, but it only ever READS the
 * answer: get_album_cover() builds its request by STRING CONCATENATION, not
 * with a JSON library. Nothing this header produces is ever sent to LMS.
 *
 * The functions live here rather than inside Squeezebox because
 * get_album_cover_json_cb() is a member of a driver that opens a TCP socket in
 * its constructor and is not even linked into the test binaries, so nothing
 * could call it from a test. Free functions can, and
 * tests/SqueezeboxWire_test.cpp calls exactly these - the same code the server
 * ships - so that a mutation of the shipped reader turns that suite red. Same
 * shape as MqttWire.h / ReolinkWire.h / WagoWire.h.
 *
 * ⚠️ WHAT THE THREE EMISSION INVARIANTS OF E4.1 MEAN HERE, stated so that
 * nobody credits this header with a protection it does not give:
 *
 *  - sorted keys and ensure_ascii apply to exactly ONE dump(), prettyPrint(),
 *    and its output goes to a cDebug() TRACE. There is no third party to
 *    protect and no parser downstream. What changed with the migration is the
 *    bytes of a LOG LINE: jansson emitted in insertion order with the raw
 *    UTF-8 bytes (JSON_INDENT(4) carries no JSON_ENSURE_ASCII), nlohmann emits
 *    sorted and escaped.
 *  - error_handler_t::replace on that dump() is DEFENSIVE, NOT LOAD BEARING,
 *    and that is MEASURED, not assumed: the only tree that ever reaches
 *    prettyPrint() comes out of parseResponse(), and Json::parse() REFUSES
 *    invalid UTF-8 (it answers a discarded value) exactly as json_loads()
 *    refused it before. So no LMS answer can make it throw type_error.316.
 *    tests/SqueezeboxWire_test.cpp pins that measurement in a tripwire that
 *    goes red the day it stops holding - i.e. the day the handler starts
 *    carrying weight and somebody has to be told.
 *    ⚠️ This is NOT the KNX situation, where raw bus bytes reached a bare
 *    dump() and killed the driver: here every byte has been through a parser
 *    that validates UTF-8.
 */
namespace SqueezeboxWire
{

/*
 * Parse one LMS answer.
 *
 * Answers false EXACTLY where json_loads(result.c_str(), 0, &jerr) answered
 * NULL, and that includes a TOP LEVEL SCALAR: jansson has no JSON_DECODE_ANY,
 * so `null`, `3` and `"ok"` were parse errors. Json::parse() accepts all
 * three, so the guard is explicit here - without it an LMS answering `null`
 * would move from "malformed, fall back to the CLI" to "parsed fine, nothing
 * found", a different acceptance set for the same user outcome.
 *
 * A top level ARRAY is accepted, as it was before, and falls through
 * findArtworkUrl() as NoResultObject.
 *
 * ⚠️ ONE MEASURED DIVERGENCE, declared and deliberately not reproduced, pinned
 * by TheDeclaredAcceptanceDivergence_EscapedNulIsNowAccepted: an ESCAPED NUL,
 * "\u0000", was refused by jansson ("\u0000 is not allowed without
 * JSON_ALLOW_NUL" in a value, "NUL byte in object key not supported" in a key)
 * and is ACCEPTED by nlohmann, which decodes a real 0x00 byte into the
 * std::string. So such an answer moves from "malformed, fall back to the CLI"
 * to "parsed, walk it" - and a NUL in artwork_url ends up in an HTTP URL. A RAW
 * NUL byte is still refused by BOTH libraries, so nothing here rested on the
 * c_str() the old code passed - measured both ways.
 *
 * `out` is left untouched when the answer is refused.
 *
 * ⚠️ MEASURED, so that nobody takes it for load bearing: the `is_discarded()`
 * half of the guard below is REDUNDANT with the other half - a discarded value
 * answers false to BOTH is_object() and is_array(). Removing it changes
 * nothing and turns no test red (an equivalent mutant, verified). It is kept
 * because it states the intent - "the parse failed" and "the root is a scalar"
 * are two different reasons - and because the day someone replaces the
 * container test, the parse test must still be there.
 */
inline bool parseResponse(const std::string &response, Json &out)
{
    Json jroot = Json::parse(response, nullptr, false);

    if (jroot.is_discarded() || (!jroot.is_object() && !jroot.is_array()))
        return false;

    out = std::move(jroot);
    return true;
}

/*
 * THE ONLY dump() OF THIS PERIMETER: the cDebug() trace of the whole answer.
 * Four space indentation, as JSON_INDENT(4) did. See the caveat at the top of
 * this file about what ensure_ascii and the error handler actually protect
 * here - a log line, and nothing else.
 */
inline std::string prettyPrint(const Json &jroot)
{
    return jroot.dump(4, ' ', true, Json::error_handler_t::replace);
}

/*
 * What the walk down result.remoteMeta.artwork_url found.
 *
 * FOUR outcomes, because the driver logs three different things and they are
 * three different diagnoses:
 *   - Found          : artwork_url is a JSON string;
 *   - NoArtworkUrl   : remoteMeta is an object, artwork_url is absent or not a
 *                      string  -> "artwork_url not found in remoteMeta!";
 *   - NoRemoteMeta   : result is an object, remoteMeta is not
 *                      -> "remoteMeta not found!";
 *   - NoResultObject : the root is not an object, OR "result" is not an
 *                      object. Those two are INDISTINGUISHABLE in the shipped
 *                      driver - both fall through in SILENCE - so they are one
 *                      value here and not two.
 * All four end the same way for the user: get_album_cover_std().
 */
enum class ArtworkLookup
{
    Found,
    NoArtworkUrl,
    NoRemoteMeta,
    NoResultObject,
};

/*
 * Walk result.remoteMeta.artwork_url.
 *
 * `artworkUrl` is written ONLY on Found, so a caller that ignores the outcome
 * cannot pick up a stale or half-built value.
 *
 * is_string() is the guard jansson's json_is_string() was, and it must stay
 * one: an artwork_url that came back as a NUMBER must not be stringified into
 * "3" and concatenated into an HTTP URL.
 */
inline ArtworkLookup findArtworkUrl(const Json &jroot, std::string &artworkUrl)
{
    if (!jroot.is_object())
        return ArtworkLookup::NoResultObject;

    const Json::const_iterator jresult = jroot.find("result");
    if (jresult == jroot.end() || !jresult->is_object())
        return ArtworkLookup::NoResultObject;

    const Json::const_iterator remoteMeta = jresult->find("remoteMeta");
    if (remoteMeta == jresult->end() || !remoteMeta->is_object())
        return ArtworkLookup::NoRemoteMeta;

    const Json::const_iterator url = remoteMeta->find("artwork_url");
    if (url == remoteMeta->end() || !url->is_string())
        return ArtworkLookup::NoArtworkUrl;

    artworkUrl = url->get<std::string>();
    return ArtworkLookup::Found;
}

/*
 * The LMS host, as a NAMED TYPE and not as a bare std::string.
 *
 * ⚠️ This is not decoration. Measured three times in this series: permuting
 * two positional arguments AT A CALL SITE stayed green through the whole test
 * suite, because a test can only exercise the function, never the line that
 * calls it. buildCoverUrl(host, artworkUrl) with two std::string parameters
 * would compile and ship. With this struct it does not compile, which is the
 * only closure that actually holds.
 */
struct LmsHost
{
    std::string value;
};

/*
 * Turn an artwork_url into the cover URL the player hands back.
 *
 * An artwork_url that already starts with "http" is taken AS IS - the test is
 * those four bytes and nothing more, so "https://..." matches and so does
 * "httpFOO"; that is the shipped contract. Anything else is hung under the LMS
 * host on port 9000.
 *
 * compare(0, 4, ...) clamps on a shorter string rather than throwing, so ""
 * and "ht" go through the relative branch. (substr(0, 4) would behave the same
 * here - pos == 0 is always valid and the length clamps too; an earlier version
 * of this comment claimed otherwise and was WRONG. What must not change is the
 * BRANCH, which is pinned by
 * ShortAndEmptyArtworkUrlsGoThroughTheRelativeBranch.)
 */
inline std::string buildCoverUrl(const std::string &artworkUrl, const LmsHost &host)
{
    if (artworkUrl.compare(0, 4, "http") == 0)
        return artworkUrl;

    std::string s = "http://";
    s += host.value + ":9000/";
    s += artworkUrl;

    return s;
}

} //namespace SqueezeboxWire

#endif // S_SQUEEZEBOX_WIRE_H
