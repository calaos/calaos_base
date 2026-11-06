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
#ifndef S_JSON_PATH_H
#define S_JSON_PATH_H

#include "Utils.h"

/*
 * T3.37 - the `path` parser of the MQTT and Web IOs, in ONE place.
 *
 * It used to live twice, ~55 lines copied between MqttCtrl::getValueJson()
 * and WebCtrl::getValueJson(), and every defect of it was therefore double:
 * the crash on a lone '[' (T3.35), the non numeric index silently reading
 * element 0 (T3.25 family), from_string() outside the try, and the failure
 * message that told the user nothing (T3.29 option C). T3.35 had to write the
 * same fix twice, in two files, and T3.35c had to write the same one line
 * guard twice again. This header is what makes that stop.
 *
 * WHAT STAYS IN THE CALLERS, and why it is not an oversight:
 *
 *  - HOW THE DOCUMENT IS OBTAINED. MQTT parses a payload string with
 *    exceptions disabled and reports is_discarded(); Web opens a downloaded
 *    file and catches. Different sources, different messages, different
 *    things to name in the log.
 *
 *  - THE EMPTY PATH, and this one is a DELIBERATE, TEST FROZEN DIVERGENCE
 *    (T3.29 AnEmptyPathReturnsTheRawPayload / AnEmptyPathReturnsEmpty):
 *    MQTT answers an empty `path` with the RAW PAYLOAD - it is how a device
 *    that publishes a bare value instead of a JSON document is read, and a
 *    real configuration depends on it - while Web has no such shortcut and
 *    falls into the "no path segment" branch below. T3.37 keeps the
 *    divergence: it belongs to "what does this IO do when it is given no
 *    path", not to "how is a path parsed". The MQTT caller therefore returns
 *    BEFORE calling resolve(); everything a path can contain is decided here.
 *
 * THE ERROR FLAG IS NOT OPTIONAL. resolve() reports whether the path
 * RESOLVED, always, to every caller. T3.35b sect. 6.8 left MqttCtrl with a
 * flag and WebCtrl without one, on the grounds that no Web caller asked for
 * it; with one shared parser that is no longer a choice that can be made per
 * copy without duplicating the code again, which is the very thing this file
 * exists to prevent. What each caller DOES with the answer stays its own
 * business - see docs/refactoring/T3.37.md sect. 4 for the measured state of
 * WebCtrl's four call sites and the follow-up they need.
 */

namespace Calaos
{

namespace JsonPath
{

/*
 * Resolve `path` against `root` and write the extracted value into `value`.
 *
 * Returns TRUE when the path resolved. On FALSE, `value` is left untouched
 * and a cWarning() naming the offending token has been emitted.
 *
 * ! "RESOLVED" IS NOT "IS A USABLE NUMBER", and the difference is frozen by
 * test. A path landing on a container answers the markers "object{}" /
 * "array[]" and that is a SUCCESS (T3.29 APathStoppingOnAContainerReturnsAMarker);
 * an index token that carries no readable number reads element 0 and that is
 * a SUCCESS TOO, warned about but not failed (T3.29
 * ANonNumericIndexSilentlyReadsElementZero, kept deliberately in T3.35b - a
 * real configuration may lean on the value, what had to go was the silence).
 * Callers that need a NUMBER check for one on top of this, as
 * MqttCtrl::readStatusNumber() does.
 */
inline bool resolve(const Json &root, const std::string &path, std::string &value)
{
    std::vector<std::string> tokens;
    Utils::split(path, tokens, "/");

    //T3.37. Utils::split() collapses runs of the delimiter and never yields
    //an empty token, so a path made only of separators - "/", "///", and on
    //the Web side the empty path too - produces no token at all and there is
    //nothing to resolve. The two copies used to disagree here and it is the
    //one branch the extraction had to arbitrate: MQTT set its flag and logged
    //NOTHING (a `path` of "///" answered empty, with not one line in the
    //server log), Web logged "Error emtpy path not allowed", which named
    //neither the mistake nor the path that made it. Unified onto the contract
    //every other failure of this parser already honours since T3.35: empty
    //value, no exception, and a warning THAT NAMES THE PATH.
    if (tokens.empty())
    {
        cWarning() << "Error in path " << path << ", no path segment to resolve"
                   << " : a path must name at least one key or index, as in weather/[0]/description";
        return false;
    }

    Json parent = root;
    for (auto it = tokens.begin(); it != tokens.end(); it++)
    {
        std::string val = *it;

        // Test if the token is an array index
        // if it's the case, it must be something like [x]
        if (val[0] == '[')
        {
            /* T3.35, corrected by T3.35b. A well formed index token is
             * "[n]" - an opening bracket AND a closing one. erase() and
             * pop_back() below strip the first and the last character
             * UNCONDITIONALLY, so what has to be checked here is the FORM.
             * A guard on the LENGTH alone (the T3.35 shape, `val.size() <
             * 2`) covered only half of it and the message it printed was
             * not true of the code that printed it:
             *
             *  - a lone '[' was EMPTIED by erase(), pop_back() then
             *    underflowed the size_t length of the string, and the read
             *    that followed escaped getValueJson() as a std::bad_alloc.
             *    Nothing caught it anywhere up to main() and calaos_server
             *    terminated. There is no remote vector - a `path` is only
             *    ever written by calaos_installer - but a typo was enough
             *    to bring the server down. A length guard does stop that.
             *
             *  - "[5" and "[12" are two characters or more, so a length
             *    guard let them straight through; pop_back() then ate a
             *    DIGIT and the parser answered element 0 and element 1,
             *    SILENTLY, with a value nothing distinguishes from a
             *    correct reading. That is worse than the empty string the
             *    same typo produces everywhere else in this parser.
             */
            if (val.size() < 2 || val.back() != ']')
            {
                cWarning() << "Error in path " << path << ", malformed array index " << *it
                           << " : an array index must be written [n], as in weather/[0]/description";
                return false;
            }

            // Remove first and last char
            val.erase(0, 1);
            val.pop_back();

            int idx = 0;

            try
            {
                /* T3.35b. The index is DECIDED here, on both branches,
                 * instead of being left to whatever Utils::from_string()
                 * happens to leave behind. The two failing shapes do not
                 * behave the same way and that asymmetry was the trap:
                 * on a BLANK string - the token "[]" - the stream sentry
                 * fails before num_get ever runs, so the destination is
                 * NOT written and the index was read UNINITIALISED; on a
                 * non blank string that does not parse - "[zz]" - the
                 * sentry succeeds and C++11 num_get stores 0. from_string()
                 * cannot even be interrogated about it: it returns
                 * iss.eof(), which is TRUE for the blank string.
                 *
                 * The two cases are made to agree, deliberately, on
                 * element 0: T3.29 froze that value and a real
                 * configuration may lean on it. What does not stay is the
                 * SILENCE - reading element 0 because the index was
                 * unreadable is precisely the case a user cannot diagnose.
                 *
                 * Everything that touches the index sits inside this try,
                 * so it can only ever fail through the one error path this
                 * branch already has.
                 *
                 * T3.35c. THE TEST IS "DOES IT CARRY A DIGIT", NOT "DOES
                 * from_string() COMPLAIN". from_string() returns
                 * iss.eof(), and a stream that consumed only whitespace -
                 * or only a sign - DID reach its end, so it reports
                 * SUCCESS on "[ ]", "[\t]", "[+]" and "[-]". On the two
                 * blank ones it does not write the destination either,
                 * which is how the index was still being read UNASSIGNED
                 * after T3.35b: `val.empty()` catches "[]" and nothing
                 * else. find_first_of() closes the whole family in one
                 * test, and it subsumes val.empty() - an empty string has
                 * no digit - so no sub-condition here is dead.
                 *
                 * WHY NOT "every character must be a digit"
                 * (find_first_not_of): it would test the WRONG thing
                 * three ways. An empty string has no NON-digit either, so
                 * "[]" would walk back through unguarded; "[+2]" would
                 * stop resolving; and "[-1]" would be answered "is not a
                 * number", which is false about -1. The sign is NOT
                 * rejected here, deliberately: a signed or padded token
                 * carries a number and goes on to at(), which refuses a
                 * negative index as out of range - a different message
                 * for a different mistake.
                 *
                 * INVARIANT this establishes, and the reason the
                 * `int idx = 0` above is now GENUINELY dead - it is kept
                 * as a belt, it is no longer the value anything reads:
                 * when the guard passes, val holds at least one digit, so the
                 * stream sentry succeeds, so num_get RUNS - and C++11
                 * num_get always stores something (the value, 0 on a
                 * failed parse, or the clamped limit on overflow). When
                 * the guard trips, idx = 0 is assigned. Every path into
                 * parent.at(idx) therefore writes idx first.
                 *
                 * ! T3.37: "dead" here is the conclusion of a POISON PROBE,
                 * not of reading the code. `int idx = 0` -> `int idx;`
                 * reddens nothing whether the initialiser is live or not;
                 * only `int idx = 7` tells the two apart, and it was 4 red
                 * before T3.35c and 0 after. Anyone MOVING the guard below
                 * must replay that probe - see T3.35 sect. 6.6.
                 */
                if (val.find_first_of("0123456789") == std::string::npos ||
                    !Utils::from_string(val, idx))
                {
                    idx = 0;
                    cWarning() << "Error in path " << path << ", array index " << *it
                               << " is not a number : reading element 0";
                }

                parent = parent.at(idx);
            }
            catch (const std::exception &e)
            {
                cWarning() << "Error in path " << path << ", index not found " << *it << " : " << e.what();
                return false;
            }
        }
        else
        {
            // Token is a normal object name
            try
            {
                parent = parent.at(val);
            }
            catch (const std::exception &e)
            {
                cWarning() << "Error in path " << path << ", subpath not found " << *it << " : " << e.what();

                /* T3.35 - the "option C" of T3.29 section 5.6. NO FALSE
                 * POSITIVE IS POSSIBLE HERE, by construction and not by
                 * heuristic: this catch is only ever entered when
                 * parent.at(val) has ALREADY thrown. A payload whose key
                 * really is spelled "action[0]" - a real, measured
                 * Zigbee2MQTT shape - has RESOLVED and never reaches this
                 * line. That is exactly what separates it from teaching
                 * the parser to also split a glued index, which T3.29
                 * implemented, measured and rejected because it shadowed
                 * such a key silently.
                 *
                 * Honest about its reach: cWarning() is not a filtered
                 * domain, so this does go to the calaos_server log by
                 * default - but the person who made the typo is sitting in
                 * calaos_installer.
                 */
                if (val.find('[') != std::string::npos)
                {
                    std::string suggestion = val;
                    suggestion.insert(suggestion.find('['), "/");
                    cWarning() << "Error in path " << path << ", did you mean " << suggestion
                               << " ? array indices are their own path segment, not glued to "
                                  "the key that precedes them";
                }

                return false;
            }
        }
    }

    if (parent.is_null())
        value = "null";
    else if (parent.is_boolean())
        value = parent.get<bool>() ? "true" : "false";
    else if (parent.is_number())
        value = Utils::to_string(parent.get<double>());
    else if (parent.is_string())
        value = parent.get<std::string>();
    else if (parent.is_object())
    {
        cWarning() << "Error, path returns an object, not a value";
        value = "object{}";
    }
    else if (parent.is_array())
    {
        cWarning() << "Error, path returns an array, not a value";
        value = "array[]";
    }

    return true;
}

}

}

#endif
