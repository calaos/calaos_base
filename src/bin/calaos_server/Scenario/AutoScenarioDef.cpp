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
#include "AutoScenarioDef.h"

#include "StringUtils.h"

#include <algorithm>
#include <cstdlib>
#include <set>

using namespace Calaos;

const char *const AutoScenarioDef::KEY_UID = "autoscenario_uid";
const char *const AutoScenarioDef::KEY_SCHEMA = "autoscenario_schema";
const char *const AutoScenarioDef::KEY_CYCLE = "autoscenario_cycle";
const char *const AutoScenarioDef::KEY_ENABLED = "autoscenario_enabled";
const char *const AutoScenarioDef::KEY_SCHEDULE = "autoscenario_schedule";
const char *const AutoScenarioDef::KEY_STEPS = "autoscenario_steps";

const char *const AutoScenarioDef::STEP_PREFIX = "as_";
const char *const AutoScenarioDef::PAUSE_SUFFIX = "_pause";
const char *const AutoScenarioDef::ACTIONS_SUFFIX = "_actions";
const char *const AutoScenarioDef::FINAL_TOKEN = "final";

const char *const AutoScenarioDef::SCHEMA_VERSION = "1";

namespace
{

const char HEADER_PREFIX[] = "autoscenario_";

/* The two counters. Process wide, monotonic, NEVER rewound by production code:
 * "never recycled" (E4.6.md D3) means an id is not handed out a second time
 * even after the object carrying it is deleted. observeUid()/observeStepId()
 * push them past everything ever read from a configuration file, which is what
 * makes a fresh allocation safe on a config the process did not write.
 */
unsigned long nextUidCounter = 0;
unsigned long nextStepCounter = 0;

//The numeric tail of "<prefix><digits>", or false when the shape is not that.
bool numericTail(const std::string &id, const std::string &prefix, unsigned long &out)
{
    if (id.size() <= prefix.size()) return false;
    if (id.compare(0, prefix.size(), prefix) != 0) return false;

    const std::string tail = id.substr(prefix.size());
    for (char c: tail)
        if (c < '0' || c > '9') return false;

    //An absurdly long run of digits is not an id we ever wrote: refuse it
    //rather than let strtoul saturate the counter to ULONG_MAX.
    if (tail.size() > 18) return false;

    out = strtoul(tail.c_str(), nullptr, 10);
    return true;
}

int hexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Split on '|', KEEPING nothing empty. Utils::split() is not used on purpose:
 * this codec has to be its own inverse byte for byte, so the emptiness rule
 * has to be stated here rather than inherited from a general purpose helper
 * that may change.
 */
std::vector<std::string> splitPipe(const std::string &s)
{
    std::vector<std::string> out;
    std::string::size_type start = 0;

    while (start <= s.size())
    {
        const std::string::size_type pos = s.find('|', start);
        if (pos == std::string::npos)
        {
            if (start < s.size()) out.push_back(s.substr(start));
            break;
        }
        if (pos > start) out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }

    return out;
}

} //namespace

std::string AutoScenarioDef::encode(const std::string &raw)
{
    std::string out;
    out.reserve(raw.size());

    for (char c: raw)
    {
        //'%' FIRST: it is the escape character, encoding it after the others
        //would double-encode what they just produced.
        if (c == '%') out += "%25";
        else if (c == '|') out += "%7C";
        else if (c == '=') out += "%3D";
        else out += c;
    }

    return out;
}

std::string AutoScenarioDef::decode(const std::string &encoded)
{
    std::string out;
    out.reserve(encoded.size());

    for (std::string::size_type i = 0;i < encoded.size();i++)
    {
        if (encoded[i] != '%' || i + 2 >= encoded.size())
        {
            out += encoded[i];
            continue;
        }

        const int hi = hexValue(encoded[i + 1]);
        const int lo = hexValue(encoded[i + 2]);
        if (hi < 0 || lo < 0)
        {
            //A '%' that is not an escape is a literal '%'. It comes back out
            //as "%25", which is the normalization documented in the header.
            out += encoded[i];
            continue;
        }

        out += (char)((hi << 4) | lo);
        i += 2;
    }

    return out;
}

bool AutoScenarioDef::isDefinitionParam(const std::string &key)
{
    const std::string headerPrefix(HEADER_PREFIX);
    if (key.size() > headerPrefix.size() &&
        key.compare(0, headerPrefix.size(), headerPrefix) == 0)
        return true;

    /* The step namespace, matched TIGHTLY: "as_<safe id>_pause" or
     * "as_<safe id>_actions". Deliberately not "everything starting with as_":
     * this predicate decides what saveToParams() is allowed to DELETE, and a
     * clean up that removes a param it does not understand is the very defect
     * of ListeRoom.cpp:320-330 in miniature.
     */
    const std::string stepPrefix(STEP_PREFIX);
    if (key.size() <= stepPrefix.size()) return false;
    if (key.compare(0, stepPrefix.size(), stepPrefix) != 0) return false;

    const std::string pause(PAUSE_SUFFIX), actions(ACTIONS_SUFFIX);
    std::string id;
    if (key.size() > stepPrefix.size() + pause.size() &&
        key.compare(key.size() - pause.size(), pause.size(), pause) == 0)
        id = key.substr(stepPrefix.size(), key.size() - stepPrefix.size() - pause.size());
    else if (key.size() > stepPrefix.size() + actions.size() &&
             key.compare(key.size() - actions.size(), actions.size(), actions) == 0)
        id = key.substr(stepPrefix.size(), key.size() - stepPrefix.size() - actions.size());
    else
        return false;

    return isValidStepId(id) || id == FINAL_TOKEN;
}

bool AutoScenarioDef::isValidStepId(const std::string &id)
{
    if (id.empty()) return false;
    //FINAL_TOKEN is the final step's param fragment, never a step id: letting
    //it through would let a step alias as_final_actions.
    if (id == FINAL_TOKEN) return false;

    for (char c: id)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }

    return true;
}

void AutoScenarioDef::resetIdAllocators()
{
    nextUidCounter = 0;
    nextStepCounter = 0;
}

std::string AutoScenarioDef::newUid()
{
    const std::string uid = "as_" + Utils::to_string(nextUidCounter);
    nextUidCounter++;
    return uid;
}

std::string AutoScenarioDef::newStepId()
{
    const std::string id = "s" + Utils::to_string(nextStepCounter);
    nextStepCounter++;
    return id;
}

void AutoScenarioDef::observeUid(const std::string &uid)
{
    unsigned long n = 0;
    if (!numericTail(uid, "as_", n)) return;
    if (n + 1 > nextUidCounter) nextUidCounter = n + 1;
}

void AutoScenarioDef::observeStepId(const std::string &stepId)
{
    unsigned long n = 0;
    if (!numericTail(stepId, "s", n)) return;
    if (n + 1 > nextStepCounter) nextStepCounter = n + 1;
}

void AutoScenarioDef::clear()
{
    uid.clear();
    cycle = false;
    enabled = true;
    scheduleIoId.clear();
    steps.clear();
    finalStep = AutoScenarioDefStep();
}

std::string AutoScenarioDef::encodeActions(const std::vector<AutoScenarioDefAction> &actions)
{
    std::string out;

    for (const AutoScenarioDefAction &a: actions)
    {
        if (!out.empty()) out += '|';
        out += encode(a.ioId);
        out += '=';
        out += encode(a.value);
    }

    return out;
}

std::vector<AutoScenarioDefAction> AutoScenarioDef::decodeActions(const std::string &encoded)
{
    std::vector<AutoScenarioDefAction> out;

    for (const std::string &item: splitPipe(encoded))
    {
        /* The FIRST raw '=' separates. Every '=' inside an id or a value is
         * percent-encoded, so a raw one can only be the separator. An item
         * with none at all is an id with an empty value - kept, never dropped:
         * D4 says an action is never silently escamoted, and "the value came
         * back empty" is a repairable state, "the action is gone" is not.
         */
        const std::string::size_type eq = item.find('=');

        AutoScenarioDefAction a;
        if (eq == std::string::npos)
        {
            a.ioId = decode(item);
        }
        else
        {
            a.ioId = decode(item.substr(0, eq));
            a.value = decode(item.substr(eq + 1));
        }

        out.push_back(a);
    }

    return out;
}

bool AutoScenarioDef::loadFromParams(const Params &p)
{
    if (!p.Exists(KEY_UID)) return false;

    const std::string loadedUid = p.get_param_const(KEY_UID);
    if (loadedUid.empty()) return false;

    clear();

    uid = loadedUid;
    observeUid(uid);

    cycle = p.get_param_const(KEY_CYCLE) == "true";
    //ENABLED DEFAULTS TO TRUE, and only the literal "false" disables. The
    //param is the old `disabled` inverted once and for all (D6); a definition
    //whose key went missing must not silently stop running.
    enabled = p.Exists(KEY_ENABLED)? p.get_param_const(KEY_ENABLED) != "false": true;
    scheduleIoId = p.get_param_const(KEY_SCHEDULE);

    /* THE AUTHORITATIVE READ. The order and the identity of the steps come
     * from KEY_STEPS and from nothing else: not from the alphabetical order of
     * the params, not from what `as_*` keys happen to exist. A token that is
     * not a valid step id, or that repeats, is refused; the params of a step
     * this list does not name are ignored here and removed by saveToParams().
     */
    std::set<std::string> seen;
    for (const std::string &token: splitPipe(p.get_param_const(KEY_STEPS)))
    {
        if (!isValidStepId(token)) continue;
        if (!seen.insert(token).second) continue;

        AutoScenarioDefStep step;
        step.stepId = token;
        observeStepId(step.stepId);

        const std::string pauseKey = STEP_PREFIX + token + PAUSE_SUFFIX;
        if (p.Exists(pauseKey))
            Utils::from_string(p.get_param_const(pauseKey), step.pause);

        step.actions = decodeActions(p.get_param_const(STEP_PREFIX + token + ACTIONS_SUFFIX));

        steps.push_back(step);
    }

    finalStep.actions = decodeActions(
                p.get_param_const(std::string(STEP_PREFIX) + FINAL_TOKEN + ACTIONS_SUFFIX));

    return true;
}

void AutoScenarioDef::saveToParams(Params &p) const
{
    /* NOTHING to write means NOTHING to remove. A Scenario IO that is not an
     * auto scenario - configs/solanora has eight of them - must come out of a
     * save byte for byte the file it went in as.
     */
    if (uid.empty()) return;

    std::map<std::string, std::string> produced;

    produced[KEY_UID] = uid;
    produced[KEY_SCHEMA] = SCHEMA_VERSION;
    produced[KEY_CYCLE] = cycle? "true": "false";
    produced[KEY_ENABLED] = enabled? "true": "false";
    //Absent, never empty: an unscheduled scenario carries no schedule key at
    //all, the same contract the API side keeps for an absent member.
    if (!scheduleIoId.empty()) produced[KEY_SCHEDULE] = scheduleIoId;

    std::string stepList;
    for (const AutoScenarioDefStep &step: steps)
    {
        if (!isValidStepId(step.stepId)) continue;

        if (!stepList.empty()) stepList += '|';
        stepList += step.stepId;

        produced[STEP_PREFIX + step.stepId + PAUSE_SUFFIX] = Utils::to_string(step.pause);
        produced[STEP_PREFIX + step.stepId + ACTIONS_SUFFIX] = encodeActions(step.actions);
    }
    //Always written, even empty: its PRESENCE is what says "this definition
    //has no step", as opposed to "this file predates the definition".
    produced[KEY_STEPS] = stepList;

    if (!finalStep.actions.empty())
        produced[std::string(STEP_PREFIX) + FINAL_TOKEN + ACTIONS_SUFFIX] =
                encodeActions(finalStep.actions);

    /* The orphan clean up, and its exact reach: every key of the two owned
     * namespaces that this definition does NOT produce, and nothing else.
     * The legacy marker `auto_scenario` is in neither namespace, so it is
     * untouched - which is what keeps the sweep of ListeRoom.cpp:324 from
     * finding an unadopted rule.
     */
    std::vector<std::string> doomed;
    for (Params::const_iterator it = p.cbegin();it != p.cend();++it)
    {
        if (!isDefinitionParam(it->first)) continue;
        if (produced.find(it->first) != produced.end()) continue;
        doomed.push_back(it->first);
    }
    for (const std::string &key: doomed)
        p.Delete(key);

    for (const auto &kv: produced)
        p.Add(kv.first, kv.second);
}
