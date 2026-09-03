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
#include "JsonApi.h"
#include "HttpClient.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "AutoScenario.h"
#include "AutoScenarioDef.h"
#include "CalaosConfig.h"
#include "HistLogger.h"

#include <openssl/evp.h>
#include <openssl/crypto.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <set>

namespace
{

/* E4.1o. jansson_string_get()'s contract, kept BY HAND for a nlohmann
 * document: the DEFAULT on an absent member, on a member that is not a JSON
 * string, and on a root that is not an object (json_object_get(NULL, k)
 * answered NULL). `j["k"].get<string>()` does none of that - it throws.
 */
inline std::string jsonStringGet(const Json &j, const char *key,
                                 const std::string &defaultValue = std::string())
{
    if (!j.is_object())
        return defaultValue;

    const Json::const_iterator it = j.find(key);
    if (it == j.cend() || !it->is_string())
        return defaultValue;

    return it->get<std::string>();
}

/* E4.1o. jansson_decode_object()'s FLATTENING CONTRACT, kept BY HAND: a
 * string as is, a boolean as the WORD "true"/"false", any number through
 * Utils::to_string(double) - a bare ostringstream, frozen on purpose and not
 * "fixed" here - and ANY OTHER TYPE (object, array, null) the EMPTY STRING,
 * WITH THE KEY STILL ADDED. That last clause matters downstream: an absent
 * key and a key at "" are not the same thing to TimeRange(Params).
 *
 * A non object (a null, which is what an absent key parses to, included)
 * iterates zero times, exactly as json_object_foreach() did on a NULL or on a
 * non object.
 *
 * Identical, deliberately, to ScriptWire::decodeObject() and to the four
 * driver wires that carry the same helper: five copies of one contract, still
 * to be folded into one place.
 */
inline void decodeJsonObject(const Json &j, Params &params)
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

/* The auto scenario payload on the way in. The schema is the one
 * Scenario::toJson() emits, key for key: what a client reads back is what it
 * may send. The derived keys it also reads - id, category, broken,
 * disabled_missing_io, missing_ios, schedule and an action's `resolved` - are
 * accepted and ignored here, which is what makes get -> modify -> get an
 * identity instead of a slow amputation. Parsing is separate from applying:
 * create and modify commit nothing until the whole document is accepted.
 */

struct ScenarioPayload
{
    string name;
    Room *room = nullptr;
    bool visible = false;
    bool cycle = false;
    bool enabled = false;
    vector<AutoScenarioDefStep> steps;
    AutoScenarioDefStep finalStep;
};

inline bool jsonBoolGet(const Json &j, const char *key, bool defaultValue)
{
    return jsonStringGet(j, key, defaultValue? "true": "false") == "true";
}

//A pause is client text. Anything that is not a finite number is refused
//rather than silently read as zero, which is how a typo became a missing wait.
bool parseScenarioPause(const string &raw, double &out)
{
    out = 0.0;
    if (raw.empty()) return true;

    char *end = nullptr;
    errno = 0;
    const double v = strtod(raw.c_str(), &end);

    if (!end || *end != '\0' || end == raw.c_str()) return false;
    if (errno == ERANGE || !std::isfinite(v)) return false;

    out = v;
    return true;
}

bool parseScenarioActions(const Json &jactions, vector<AutoScenarioDefAction> &out,
                          const string &where, string &err)
{
    if (jactions.is_null()) return true;

    if (!jactions.is_array())
    {
        err = where + ": actions must be an array";
        return false;
    }

    int n = 0;
    for (const Json &jact: jactions)
    {
        if (!jact.is_object())
        {
            err = where + ": action " + Utils::to_string(n) + " is not an object";
            return false;
        }

        AutoScenarioDefAction a;
        a.ioId = jsonStringGet(jact, "io");
        if (a.ioId.empty())
        {
            err = where + ": action " + Utils::to_string(n) + " names no io";
            return false;
        }

        /* The id is NOT resolved. An action pointing at an IO that no longer
         * exists is a scenario to repair, not a request to refuse: refusing it
         * would make a broken scenario impossible to rename, and dropping it
         * would erase the only trace of what is missing.
         */
        a.value = jsonStringGet(jact, "value");

        out.push_back(a);
        n++;
    }

    return true;
}

bool parseScenarioPayload(const Json &jdata, ScenarioPayload &out, string &err)
{
    if (!jdata.is_object())
    {
        err = "invalid payload: not an object";
        return false;
    }

    out.name = jsonStringGet(jdata, "name", _("New unnamed scenario"));
    out.visible = jsonBoolGet(jdata, "visible", false);
    out.cycle = jsonBoolGet(jdata, "cycle", false);
    out.enabled = jsonBoolGet(jdata, "enabled", false);

    const string roomName = jsonStringGet(jdata, "room_name");
    const string roomType = jsonStringGet(jdata, "room_type");
    out.room = ListeRoom::Instance().searchRoomByNameAndType(roomName, roomType);
    if (!out.room)
    {
        err = "invalid payload: no room \"" + roomName + "\" of type \"" + roomType + "\"";
        return false;
    }

    std::set<string> stepIds;
    const Json::const_iterator itSteps = jdata.find("steps");
    if (itSteps != jdata.cend() && !itSteps->is_null())
    {
        if (!itSteps->is_array())
        {
            err = "invalid payload: steps must be an array";
            return false;
        }

        int n = 0;
        for (const Json &jstep: *itSteps)
        {
            const string where = "invalid payload: step " + Utils::to_string(n);

            if (!jstep.is_object())
            {
                err = where + " is not an object";
                return false;
            }

            AutoScenarioDefStep step;
            step.stepId = jsonStringGet(jstep, "step_id");

            if (step.stepId.empty())
            {
                step.stepId = AutoScenarioDef::newStepId();
            }
            else if (!AutoScenarioDef::isValidStepId(step.stepId))
            {
                err = where + ": step_id \"" + step.stepId + "\" is not a valid id";
                return false;
            }
            else
            {
                //A client sent us an id: fold it into the allocator so it can
                //never be handed out a second time.
                AutoScenarioDef::observeStepId(step.stepId);
            }

            if (!stepIds.insert(step.stepId).second)
            {
                err = where + ": step_id \"" + step.stepId + "\" is used twice";
                return false;
            }

            if (!parseScenarioPause(jsonStringGet(jstep, "pause"), step.pause))
            {
                err = where + ": pause is not a number";
                return false;
            }

            const Json::const_iterator itActions = jstep.find("actions");
            if (itActions != jstep.cend() &&
                !parseScenarioActions(*itActions, step.actions, where, err))
                return false;

            out.steps.push_back(step);
            n++;
        }
    }

    const Json::const_iterator itFinal = jdata.find("final_step");
    if (itFinal != jdata.cend() && !itFinal->is_null())
    {
        if (!itFinal->is_object())
        {
            err = "invalid payload: final_step must be an object";
            return false;
        }

        const Json::const_iterator itActions = itFinal->find("actions");
        if (itActions != itFinal->cend() &&
            !parseScenarioActions(*itActions, out.finalStep.actions,
                                  "invalid payload: final_step", err))
            return false;
    }

    return true;
}

/* The auto scenario a time range IO belongs to, or null. A LOOKUP and not a
 * back-pointer on the IO: such a pointer has to be cleared from three places,
 * and any one of them missed leaves it naming a destroyed object.
 */
AutoScenario *autoScenarioOfTimeRange(IOBase *o)
{
    if (!o) return nullptr;

    for (Scenario *sc: ListeRoom::Instance().getAutoScenarios())
    {
        AutoScenario *as = sc? sc->getAutoScenario(): nullptr;
        if (as && as->getIOTimeRange() == o)
            return as;
    }

    return nullptr;
}

} //namespace

map<string, LoginThrottle::Entry> LoginThrottle::entries;

void LoginThrottle::purge(double now)
{
    for (auto it = entries.begin();it != entries.end();)
    {
        if (now - it->second.lastSeen > EntryTimeout)
            it = entries.erase(it);
        else
            ++it;
    }
}

bool LoginThrottle::isBlocked(const string &ip, double now)
{
    auto it = entries.find(ip);
    if (it == entries.end())
        return false;

    if (now - it->second.lastSeen > EntryTimeout)
    {
        entries.erase(it);
        return false;
    }

    return now < it->second.blockedUntil;
}

void LoginThrottle::registerFailure(const string &ip, double now)
{
    purge(now);

    auto it = entries.find(ip);
    if (it == entries.end())
    {
        if ((int)entries.size() >= MaxEntries)
        {
            //Table is full, forget the address not seen for the longest time
            auto oldest = entries.begin();
            for (auto e = entries.begin();e != entries.end();++e)
            {
                if (e->second.lastSeen < oldest->second.lastSeen)
                    oldest = e;
            }
            entries.erase(oldest);
        }

        it = entries.insert({ ip, Entry() }).first;
    }

    Entry &entry = it->second;
    if (entry.failures < 1000)
        entry.failures++;

    double delay = BaseDelay;
    for (int i = 1;i < entry.failures && delay < MaxDelay;i++)
        delay *= 2.0;
    if (delay > MaxDelay)
        delay = MaxDelay;

    entry.lastSeen = now;
    entry.blockedUntil = now + delay;
}

void LoginThrottle::registerSuccess(const string &ip)
{
    entries.erase(ip);
}

int LoginThrottle::trackedCount()
{
    return (int)entries.size();
}

void LoginThrottle::clear()
{
    entries.clear();
}

bool JsonApi::secureCompare(const string &expected, const string &received)
{
    unsigned char de[EVP_MAX_MD_SIZE], dr[EVP_MAX_MD_SIZE];
    unsigned int lene = 0, lenr = 0;

    if (EVP_Digest(expected.data(), expected.size(), de, &lene, EVP_sha256(), nullptr) != 1 ||
        EVP_Digest(received.data(), received.size(), dr, &lenr, EVP_sha256(), nullptr) != 1 ||
        lene != lenr)
        return false;

    return CRYPTO_memcmp(de, dr, lene) == 0;
}

bool JsonApi::checkCredentials(const string &user, const string &pass)
{
    string confUser = Utils::get_config_option("calaos_user");
    string confPass = Utils::get_config_option("calaos_password");

    if (Utils::get_config_option("cn_user") != "" &&
        Utils::get_config_option("cn_pass") != "")
    {
        confUser = Utils::get_config_option("cn_user");
        confPass = Utils::get_config_option("cn_pass");
    }

    //Both comparisons are always done, a wrong user must not answer faster
    bool userOk = secureCompare(confUser, user);
    bool passOk = secureCompare(confPass, pass);

    return userOk && passOk;
}

bool JsonApi::isValidIntParam(const string &value, int minValue, int maxValue)
{
    if (value.empty() || value.size() > 11)
        return false;

    string::size_type i = 0;
    if (value[0] == '-')
    {
        if (value.size() == 1)
            return false;
        i = 1;
    }

    for (;i < value.size();i++)
    {
        if (!isdigit((unsigned char)value[i]))
            return false;
    }

    long v = 0;
    try
    {
        v = std::stol(value);
    }
    catch (...)
    {
        return false;
    }

    return v >= minValue && v <= maxValue;
}

bool JsonApi::resolveEventPicture(const string &picUid, string &outPath)
{
    outPath.clear();

    if (picUid.empty())
        return false;

    return FileUtils::resolveSafePath(Utils::getCacheFile("push_pictures"),
                                      picUid + ".jpg",
                                      outPath);
}

bool JsonApi::requestNestingWithinLimit(const string &data)
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (unsigned char c: data)
    {
        //Both parsers treat 0x00 as end of input, so bytes past it are never
        //looked at: counting them would refuse a document nobody parses.
        if (c == '\0')
            break;

        if (inString)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
            continue;
        }

        if (c == '"')
            inString = true;
        else if (c == '{' || c == '[')
        {
            if (++depth > MaxRequestNestingDepth)
                return false;
        }
        else if (c == '}' || c == ']')
            depth--;
    }

    return true;
}

/* E4.1m. The redaction walk moved to nlohmann. TWO deliberate choices, both
 * argued in docs/refactoring/E4.1m.md:
 *
 * (a) THE PARAMETER IS A Json, NOT THE RAW REQUEST TEXT. The two callers parse
 *     the client's message anyway; they hand a document, not a string, so this
 *     function keeps having exactly one job.
 *
 * (b) ensure_ascii IS FALSE HERE, and NOWHERE ELSE IN THE EPIC. This output is
 *     a LOG LINE, not a wire, and it has NEVER been ASCII: the previous dump
 *     asked for INDENT(4) only, so an accented device name has always been
 *     written in raw UTF-8 in calaos_server's log. Escaping it now would change
 *     the bytes of a stream the epic does not migrate, which is precisely what
 *     invariant 3 exists to forbid; the same reasoning already exempts
 *     lib/ConfigOptions.cpp, bin/tools/calaos_config.cpp and CalaosConfig.cpp,
 *     the three other indented dumps of the tree. MEASURED, on a probe
 *     compiled against both libraries: with ensure_ascii = false the two
 *     dumps agree BYTE FOR BYTE on ASCII, on U+00E9 and on U+007F. So this
 *     migration changes NOTHING in the log, and
 *     core/JsonApiModelWireBytes_test has no case here for that reason.
 *     error_handler_t::replace IS applied: it is the invariant that stops a
 *     dump() from throwing type_error.316 on a live connection, and it costs
 *     no byte on anything that is valid.
 */
string JsonApi::dumpJsonRedacted(const Json &jroot)
{
    static const vector<string> sensitive =
    { "cn_pass", "password", "passwd", "pass", "token", "old_pw", "new_pw",
      "old_password", "new_password", "secret", "authorization" };

    //A null Json is the translation of the null pointer this used to refuse,
    //and a discarded one is what a non throwing parse answers on garbage.
    if (jroot.is_null() || jroot.is_discarded())
        return string();

    Json copy = jroot;

    std::function<void(Json &)> redact = [&](Json &j)
    {
        if (j.is_array())
        {
            for (Json &value: j)
                redact(value);
            return;
        }

        if (!j.is_object())
            return;

        //The keys are collected first and rewritten after the walk: writing
        //into the object while iterating it invalidates the iterator.
        vector<string> keys;
        for (Json::iterator it = j.begin(); it != j.end(); ++it)
        {
            if (std::find(sensitive.begin(), sensitive.end(), Utils::str_to_lower(it.key())) != sensitive.end())
                keys.push_back(it.key());
            else
                redact(it.value());
        }

        for (const string &k: keys)
            j[k] = "***";
    };

    redact(copy);

    return copy.dump(4, ' ', false, Json::error_handler_t::replace);
}

JsonApi::JsonApi(HttpClient *client):
    httpClient(client)
{
}

JsonApi::JsonApi()
{
}

JsonApi::~JsonApi()
{
}

void JsonApi::buildJsonIO(IOBase *io, Json &jio)
{
    /* The published scenario marker is the definition uid, not `auto_scenario`.
     * The legacy key survives on IOs the server no longer treats as scenarios
     * at all - and on the internal IOs of every scenario - so publishing it
     * names something the API cannot be asked about.
     */
    vector<string> params =
    { "id", "name", "type", "hits", "var_type", "visible",
      "chauffage_id", "rw", "unit", "gui_type", "state",
      AutoScenarioDef::KEY_UID, "step", "io_type", "io_style",
      "value_warning" };

    for (string &param: params)
    {
        string value;

        if (param == "state")
        {
            if (io->get_type() == TINT)
                value = Utils::to_string(io->get_value_double());
            else if (io->get_type() == TBOOL)
                value = io->get_value_bool()?"true":"false";
            else if (io->get_type() == TSTRING)
                value = io->get_value_string();
        }
        else if (param == "var_type")
        {
            if (io->get_type() == TINT) value = "float";
            else if (io->get_type() == TBOOL) value = "bool";
            else if (io->get_type() == TSTRING) value = "string";
        }
        else
        {
            if (!io->get_params().Exists(param))
                continue;
            value = io->get_param(param);
        }

        /* E4.1m. THE `continue` ABOVE IS THE CONTRACT: an absent param emits NO
         * KEY, never a null and never an empty string. This assignment is only
         * ever reached for a param that exists, or for the two computed ones.
         *
         * value is passed WHOLE here, where json_string(value.c_str()) used to
         * stop at the first NUL and answer NULL outright on invalid UTF-8 -
         * and neither its return code nor json_object_set_new()'s was tested,
         * so the pair vanished from the payload in silence. Pinned by the R_
         * and Z_ cases of core/JsonApiModelWireBytes_test.
         */
        jio[param] = value;
    }

    //A null Json means "this IO has no status info" - NOT an empty object,
    //which is truthy and would add "status_info":{} to every IO of the API.
    Json jstatus = buildJsonStatusInfo(io);
    if (!jstatus.is_null())
        jio["status_info"] = jstatus;
}

Json JsonApi::buildJsonRoomIO(Room *room)
{
    Json jdata = Json::array();

    for (int i = 0;i < room->get_size();i++)
    {
        Json jio = Json::object();
        IOBase *io = room->get_io(i);

        buildJsonIO(io, jio);

        jdata.emplace_back(std::move(jio));
    }

    return jdata;
}

Json JsonApi::buildJsonHome()
{
    Json jdata = Json::array();

    for (int iroom = 0;iroom < ListeRoom::Instance().size();iroom++)
    {
        Room *room = ListeRoom::Instance().get_room(iroom);
        Json jroom = Json::object();

        Json jitems = buildJsonRoomIO(room);

        //hits STAYS A STRING. Zero json_integer() in this file, and the oracle
        //of the whole E4.0 series is type strict: 3 is not "3".
        jroom["type"] = room->get_type();
        jroom["name"] = room->get_name();
        jroom["hits"] = Utils::to_string(room->get_hits());
        jroom["items"] = std::move(jitems);

        jdata.emplace_back(std::move(jroom));
    }

    return jdata;
}

Json JsonApi::buildFlatIOList()
{
    Json jdata = Json::array();

    for (int iroom = 0;iroom < ListeRoom::Instance().size();iroom++)
    {
        Room *room = ListeRoom::Instance().get_room(iroom);
        for (int i = 0;i < room->get_size();i++)
        {
            Json jio = Json::object();
            IOBase *io = room->get_io(i);

            buildJsonIO(io, jio);

            jdata.emplace_back(std::move(jio));
        }
    }

    return jdata;
}

Json JsonApi::buildJsonCameras()
{
    Json jdata = Json::array();

    list<IOBase *> camlist = ListeRoom::Instance().getCameraList();

    int cpt = 0;
    for (IOBase *io: camlist)
    {
        IPCam *camera = dynamic_cast<IPCam *>(io);
        if (!camera) continue;

        Json jcam = Json::object();
        jcam["id"] = camera->get_param("id");
        jcam["name"] = camera->get_param("name");
        jcam["type"] = camera->get_param("type");
        Params caps = camera->getCapabilities();
        //THE WORD "true", not the JSON literal. Same reason as hits above.
        if (caps["ptz"] == "true")
            jcam["ptz"] = "true";
        else
            jcam["ptz"] = "false";

        cpt++;

        jdata.emplace_back(std::move(jcam));
    }

    return jdata;
}

Json JsonApi::buildJsonAudio()
{
    Json jdata = Json::array();

    list<IOBase *> audiolist = ListeRoom::Instance().getAudioList();

    for (IOBase *io: audiolist)
    {
        AudioPlayer *player = dynamic_cast<AudioPlayer *>(io);
        if (!player) continue;

        Json jaudio = Json::object();
        jaudio["id"] = player->get_param("id");
        jaudio["name"] = player->get_param("name");
        jaudio["type"] = player->get_param("type");

        //Four capability flags, all of them WORDS. A real JSON boolean here
        //would break every client that compares them to the string "true".
        jaudio["playlist"] = player->canPlaylist()?"true":"false";
        jaudio["database"] = player->canDatabase()?"true":"false";

        //Optional: absent param, absent key. Never null, never "".
        if (player->get_params().Exists("amp"))
            jaudio["avr"] = player->get_param("amp");

        jdata.emplace_back(std::move(jaudio));

        //don't query detailed player infos here, other informations need to be queried to the squeezecenter
        //so the get_home request will be delayed by all the squeezecenter's requests.
        //To be faster, only return the basic infos here, and call get_state for each players to get detailed infos
    }

    return jdata;
}

void JsonApi::buildJsonState(vector<string> iolist, std::function<void(Json)> result_lambda)
{
    /* The audio-player part below is asynchronous (squeezebox answers come back
     * later on the loop), so the document being filled is SHARED between the
     * player chains and the completion: it stays behind a shared_ptr.
     *
     * E4.1n: what the shared_ptr no longer carries is a REFCOUNT. The jansson
     * version needed json_decref as a deleter and an explicit json_incref to
     * hand the result over; with a Json the container owns its subtree, the
     * result goes out BY VALUE, and there is nothing left to release on any
     * path - including the one where the callback is never invoked at all.
     * The T2.15 leak (json_object_set instead of _new, one reference lost per
     * state built) cannot be written any more.
     *
     * Json::object() and NOT `Json sjio;`: a default-constructed Json is
     * `null`, not `{}`, and an empty state answered as null would be a silent
     * change of document that no golden can see (they compare parsed
     * documents; null and {} are both valid). Pinned by
     * core/JsonApiStateWireBytes_test.
     */
    auto sjio = std::make_shared<Json>(Json::object());
    list<AudioPlayer *> audioplayers;

    for (string ioid: iolist)
    {
        IOBase *io = ListeRoom::Instance().get_io(ioid);
        if (!io) continue;

        if (io->get_param("gui_type") != "audio_player")
        {
            //Everything leaves as a STRING, int and double included: the
            //oracle of the golden suite is type-strict (3 != "3") and a real
            //JSON number here would break every client. std::string and not
            //c_str(): jansson truncated a value at an embedded NUL in silence,
            //this does not.
            if (io->get_type() == TBOOL)
                (*sjio)[ioid] = io->get_value_bool()?"true":"false";
            else if (io->get_type() == TINT)
                (*sjio)[ioid] = Utils::to_string(io->get_value_double());
            else if (io->get_type() == TSTRING)
                (*sjio)[ioid] = io->get_value_string();
        }
        else
        {
            AudioPlayer *p = dynamic_cast<AudioPlayer *>(io);
            if (p) audioplayers.push_back(p);
        }
    }

    string uuid = Utils::createRandomUuid();
    playerCounts[uuid] = 0;

    //Destruction guard: this JsonApi dies with its client connection while
    //squeezebox answers may still be in flight. Every async callback checks
    //the token before touching this/playerCounts or calling result_lambda
    //(which captures raw handler pointers). Same pattern as
    //JsonApiHandlerHttp::handlerAlive.
    std::weak_ptr<bool> alive = apiAlive;

    //One player chain is done (or aborted): decrement the pending count and
    //send the answer when it was the last one. Only call with alive checked.
    auto finishOne = [this, uuid, result_lambda, sjio]()
    {
        playerCounts[uuid] = playerCounts[uuid] - 1;

        if (playerCounts[uuid] <= 0)
        {
            playerCounts.erase(uuid);
            result_lambda(*sjio);
        }
    };

    for (AudioPlayer *player: audioplayers)
    {
        playerCounts[uuid] = playerCounts[uuid] + 1;

        //The player IO can be deleted through the API while a request is in
        //flight: never keep the raw pointer across an async boundary, look
        //it up again by id at each step instead.
        const string playerId = player->get_param("id");
        auto playerById = [playerId]() -> AudioPlayer *
        {
            return dynamic_cast<AudioPlayer *>(ListeRoom::Instance().get_io(playerId));
        };

        auto sjplayer = std::make_shared<Json>(Json::object());

        player->get_playlist_current([=](AudioPlayerData data1)
        {
            if (alive.expired()) return;

            (*sjplayer)["playlist_current_track"] = Utils::to_string(data1.ivalue);

            AudioPlayer *p1 = playerById();
            if (!p1) { finishOne(); return; }

            p1->get_volume([=](AudioPlayerData data2)
            {
                if (alive.expired()) return;

                (*sjplayer)["volume"] = Utils::to_string(data2.ivalue);

                AudioPlayer *p2 = playerById();
                if (!p2) { finishOne(); return; }

                p2->get_playlist_size([=](AudioPlayerData data3)
                {
                    if (alive.expired()) return;

                    (*sjplayer)["playlist_size"] = Utils::to_string(data3.ivalue);

                    AudioPlayer *p3 = playerById();
                    if (!p3) { finishOne(); return; }

                    p3->get_current_time([=](AudioPlayerData data4)
                    {
                        if (alive.expired()) return;

                        (*sjplayer)["time_elapsed"] = Utils::to_string(data4.dvalue);

                        AudioPlayer *p4 = playerById();
                        if (!p4) { finishOne(); return; }

                        p4->get_status([=](AudioPlayerData data5)
                        {
                            if (alive.expired()) return;

                            string status;
                            switch (data5.ivalue)
                            {
                            case AudioPlay: status = "playing"; break;
                            case AudioPause: status = "pause"; break;
                            case AudioStop: status = "stop"; break;
                            default:
                            case AudioError: status = "error"; break;
                            case AudioSongChange: status = "song_change"; break;
                            }

                            (*sjplayer)["status"] = status;

                            AudioPlayer *p5 = playerById();
                            if (!p5) { finishOne(); return; }

                            auto sjtrack = std::make_shared<Json>(Json::object());
                            p5->get_songinfo([=](AudioPlayerData data6)
                            {
                                if (alive.expired()) return;

                                Params &infos = data6.params;
                                for (int i = 0;i < infos.size();i++)
                                {
                                    string inf_key, inf_value;
                                    infos.get_item(i, inf_key, inf_value);

                                    (*sjtrack)[inf_key] = inf_value;
                                }

                                //A copy of the subtree, owned by its parent.
                                //The reference dance of T2.15 (json_object_set
                                //rather than _new, so the shared_ptr keeps its
                                //own reference) has no equivalent and needs
                                //none.
                                (*sjplayer)["current_track"] = *sjtrack;

                                //Add player to array, and send data back if all players requests are done.
                                (*sjio)[playerId] = *sjplayer;
                                finishOne();
                            });
                        });
                    });
                });
            });
        });
    }

    //only send data if there is not audio players
    if (playerCounts[uuid] == 0)
    {
        playerCounts.erase(uuid);
        result_lambda(*sjio);
    }
}

void JsonApi::buildJsonStates(const Params &jParam, std::function<void (Json)> result_lambda)
{
    /* E4.1n: jansson_from_params() -> Params::toJson(), which is Json(map).
     * Two consequences, both declared:
     *   - the key order does NOT move. Params IS a std::map, so the jansson
     *     adapter already walked it alphabetically, and so does nlohmann.
     *   - a pair whose VALUE is invalid UTF-8 stops DISAPPEARING. json_string()
     *     answered NULL, json_object_set_new() answered -1, and neither return
     *     code was ever tested (the adapter's own header says so): the client
     *     silently received one pair fewer. It now arrives, with one U+FFFD per
     *     invalid byte, written by the emitter's error handler.
     * An empty Params answers {} and not null: Json(std::map) builds an OBJECT,
     * measured, and core/JsonApiStateWireBytes_test pins it.
     */
    Params res;

    if (jParam.Exists("id"))
    {
        IOBase *o = ListeRoom::Instance().get_io(jParam["id"]);
        if (!o)
        {
            Params p = {{ "error", "wrong id" }};
            result_lambda(p.toJson());
            return;
        }

        switch (o->get_type())
        {
        case TINT:
        {
            for (auto it: o->get_all_values_double())
                res.Add(it.first, Utils::to_string(it.second));
            break;
        }
        case TBOOL:
        {
            for (auto it: o->get_all_values_bool())
                res.Add(it.first, (it.second)?"true":"false");
            break;
        }
        case TSTRING:
        {
            for (auto it: o->get_all_values_string())
                res.Add(it.first, it.second);
            break;
        }
        default: break;
        }
    }

    result_lambda(res.toJson());
}

void JsonApi::buildQuery(const Params &jParam, std::function<void (Json)> result_lambda)
{
    Params res;

    if (jParam.Exists("id"))
    {
        IOBase *o = ListeRoom::Instance().get_io(jParam["input_id"]);
        if (!o)
        {
            Params p = {{ "error", "wrong id" }};
            result_lambda(p.toJson());
            return;
        }

        map<string, string> m = o->query_param(jParam["param"]);
        for (auto it: m)
            res.Add(it.first, it.second);
    }

    result_lambda(res.toJson());
}

/* E4.1o. ⛔ THE ONE PLACE WHERE THE HAZARD OF E4.0 BECOMES REACHABLE.
 *
 * jParam["param"] is a CLIENT SUPPLIED STRING that this function uses as a
 * KEY and re-emits. Over HTTP it can carry ARBITRARY BYTES: hef::HfURISyntax
 * percent-decodes the query before HttpClient.cpp:328-337 splits it, so
 * `?action=get_param&id=<io>&param=%ff%80x` puts FF 80 'x' in there.
 *
 * While this answered a json_t*, jansson refused the key (json_string() ->
 * NULL, json_object_set_new() -> -1, neither tested) and the client got 200
 * with a SILENTLY TRUNCATED {}. Now that it answers a Json, nlohmann takes
 * those bytes into the tree without a word and dump() throws type_error.316 -
 * std::terminate on a live connection, since nothing catches above
 * processApi(). The only reason it does not is error_handler_t::replace on
 * both emitters (JsonApiHandlerHttp.cpp:271, JsonApiHandlerWS.cpp:105).
 * E4.1b installed it FOR THIS TICKET. Do not remove it, and do not add a
 * try/catch here instead: the handler treats the cause.
 *
 * BEHAVIOUR CHANGE, ASSUMED (user decision of 2026-08-17, declared in
 * RELEASE_NOTES.md): the pair is no longer DROPPED, it is KEPT with one
 * U+FFFD per invalid byte. A client that used to lose the param silently now
 * sees it, mangled and visible.
 */
Json JsonApi::buildJsonGetParam(const Params &jParam)
{
    bool success = true;
    Params ret;

    IOBase *o = ListeRoom::Instance().get_io(jParam["id"]);
    if (!o)
        success = false;
    else
        ret.Add(jParam["param"], o->get_param(jParam["param"]));

    if (!success)
        ret = {{ "error", "wrong io/param" }};

    return ret.toJson();
}

/* E4.1o. ⛔ READ THE ANSWER AS A DOCUMENT, NEVER AS A BOOLEAN.
 * LuaScript/ScriptExec.cpp calls this one outside of any handler; `if (!ret)`
 * on a Json compiles silently and throws type_error.302 on the event loop.
 * The failure is in the document: {"error":"wrong io/param"}.
 */
Json JsonApi::buildJsonSetParam(const Params &jParam)
{
    bool success = true;
    Params ret;

    IOBase *o = ListeRoom::Instance().get_io(jParam["id"]);
    if (!o)
        success = false;
    else
    {
        if (jParam["param"].empty() || jParam["value"].empty())
            success = false;
        else
        {
            o->set_param(jParam["param"], jParam["value"]);

            EventManager::create(CalaosEvent::EventIOChanged,
            { { "id", o->get_param("id") },
              { jParam["param"], jParam["value"] } });
        }
    }

    if (!success)
        ret = {{ "error", "wrong io/param" }};
    else
        ret = {{ "success", "true" }};

    return ret.toJson();
}

Json JsonApi::buildJsonDelParam(const Params &jParam)
{
    bool success = true;
    Params ret;

    IOBase *o = ListeRoom::Instance().get_io(jParam["id"]);
    if (!o)
        success = false;
    else
    {
        if (jParam["param"].empty())
            success = false;
        else
        {
            o->get_params().Delete(jParam["param"]);

            EventManager::create(CalaosEvent::EventIOPropertyDelete,
            { { "id", o->get_param("id") },
              { "param", jParam["param"] } });
        }
    }

    if (!success)
        ret = {{ "error", "wrong io/param" }};
    else
        ret = {{ "success", "true" }};

    return ret.toJson();
}

Json JsonApi::buildJsonGetIO(vector<string> iolist)
{
    Json jret = Json::object();

    for (string ioid: iolist)
    {
        IOBase *io = ListeRoom::Instance().get_io(ioid);
        if (io)
        {
            Json jio = Json::object();
            buildJsonIO(io, jio);

            //An id the tree does not know is SKIPPED, not answered as null:
            //the `if` above is the contract, pinned by E4.0b.
            jret[ioid] = std::move(jio);
        }
    }

    return jret;
}

/* T3.25. THE API BOUNDARY, and it is the belt to from_string()'s braces.
 *
 * A set_state value reaches io->set_value(std::string) VERBATIM (:774 and
 * :777 below). Every IO of the tree that understands a "<command> <argument>"
 * form parses that argument as a NUMBER and throws the return code away:
 * "impulse up " / "impulse down " (OutputShutter, OutputShutterSmart),
 * "set ", "set off ", "up ", "down ", "impulse " (OutputLightDimmer,
 * OutputLightRGB), "inc ", "dec " (Internal, OutputAnalog). A value that
 * carries the command and then STOPS ON ITS SEPARATOR is one of those
 * commands with its argument missing - and that is precisely the payload that
 * made an OutputShutter arm an impulse on an indeterminate int (T3.25 §2).
 *
 * from_string() no longer leaves a destination unwritten, so such a request
 * would now be executed with a 0 instead of with rubbish. That is defined, but
 * it is still NOT WHAT THE CLIENT ASKED FOR, and it would stay defined only as
 * long as every future caller keeps using from_string(). Refusing the shape
 * here does not depend on that.
 *
 * ⚠️ THE COST, stated rather than discovered: the rule is grammar free, so it
 * also refuses a TEXT value that ends in whitespace ("note " on an
 * InternalString). That is a real, deliberate narrowing - pinned by
 * core/SetStateGarbage_test::AValueEndingInWhitespaceIsRefusedForEveryIoType
 * and written up in RELEASE_NOTES. The alternative, a list of the command
 * prefixes that take an argument, was rejected: it would have had to be kept
 * in step with every IO grammar and would protect nothing a future one adds.
 *
 * The refusal reuses set_state's own vocabulary, {"success":"false"}. No new
 * error shape, per the T3.17 rule.
 */
static bool setStateValueLostItsArgument(const string &value)
{
    //T3.25 (review): the SET comes from Utils::BLANK_CHARS, shared with
    //from_string_unless_blank(), so the two can never drift apart. The
    //QUESTION stays this function's own: not "is it blank" but "does it END
    //on a blank".
    static const string blanks = Utils::BLANK_CHARS;
    return !value.empty() && blanks.find(value[value.size() - 1]) != string::npos;
}

bool JsonApi::decodeSetState(Params &jParam)
{
    bool success = true;

    IOBase *io = ListeRoom::Instance().get_io(jParam["id"]);
    if (!io)
        success = false;
    else if (setStateValueLostItsArgument(jParam["value"]))
    {
        cWarningDom("network") << "set_state refused for io " << jParam["id"]
                               << ": the value ends on its separator (\""
                               << jParam["value"] << "\"), which is a command "
                               << "with no argument";
        success = false;
    }
    else
    {
        success = false;

        if (io->get_type() == TBOOL)
        {
            if (jParam["value"] == "true") success = io->set_value(true);
            else if (jParam["value"] == "false") success = io->set_value(false);
            else success = io->set_value(jParam["value"]);
        }
        else
            success = io->set_value(jParam["value"]);
    }

    return success;
}

//The one answer get_playlist gives when it cannot produce a playlist: the
//requested IO is not (or no longer) an audio player. Shared by the entry point
//and by the async stages, so a player deleted mid-flight gives the client the
//SAME answer as an unknown id - never a playlist silently missing its tail.
static Json playlistNoPlayerAnswer()
{
    return Json{{ "success", "false" }};
}

/* E4.1p. THE DOCUMENT IS NOW A VALUE, AND THAT IS THE WHOLE POINT.
 *
 * jansson gave this chain a refcounted pointer that every stage could share,
 * at the price of three json_decref() on the guarded paths - one per stage,
 * each of them the only thing standing between a client disconnection and a
 * leaked partial answer. A Json OWNS its subtree, so those three releases have
 * no successor: the closure that held the document dies with the callback and
 * takes the document with it.
 *
 * ⛔ NOT a Json&. The ticket sheet is explicit and it is right: a reference
 * would have to outlive an ASYNCHRONOUS round trip, and the only frame that
 * could own it is the one that returns immediately after arming the callback.
 * That is a use after free, not a slow path. The document therefore travels
 * BY VALUE, moved from one stage into the next (see getNextPlaylistItem()).
 */
void JsonApi::decodeGetPlaylist(Params &jParam, std::function<void(const Json &)>result_lambda)
{
    const string playerId = jParam["id"];
    IOBase *io = ListeRoom::Instance().get_io(playerId);
    AudioPlayer *player = dynamic_cast<AudioPlayer *>(io);

    if (!player)
    {
        result_lambda(playlistNoPlayerAnswer());
        return;
    }

    /* Destruction guard: this JsonApi dies with its client connection
     * (HttpClient::~HttpClient() deletes the handler, base sub-object
     * included) while player answers may still be in flight. Every async
     * callback of the chain checks the token before touching this or calling
     * result_lambda, which captures raw handler pointers. Same pattern as
     * buildJsonState() (T2.15) and JsonApiHandlerHttp::handlerAlive.
     */
    std::weak_ptr<bool> alive = apiAlive;

    player->get_playlist_current([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        //Built HERE rather than before the call: with a value there is nothing
        //to allocate up front and nothing to release on the guarded path.
        Json jplayer = Json::object();
        jplayer["current_track"] = Utils::to_string(data.ivalue);

        //The player IO can be deleted through the API while a request is in
        //flight: never keep the raw pointer across an async boundary, look it
        //up again by id at each step instead.
        AudioPlayer *p1 = dynamic_cast<AudioPlayer *>(ListeRoom::Instance().get_io(playerId));
        if (!p1)
        {
            result_lambda(playlistNoPlayerAnswer());
            return;
        }

        /* Explicit capture list, and `mutable`: the document is MOVED into the
         * closure instead of copied, and the closure has to be allowed to
         * write to it. `this` is captured by name because the recursion below
         * is a member call - it was captured implicitly by the old [=] and the
         * apiAlive token above is what makes it safe, unchanged.
         */
        p1->get_playlist_size([this, alive, playerId, result_lambda,
                               jplayer = std::move(jplayer)](AudioPlayerData data1) mutable
        {
            if (alive.expired()) return;

            jplayer["count"] = Utils::to_string(data1.ivalue);

            int it_count = data1.ivalue;
            if (it_count <= 0)
            {
                jplayer["items"] = Json::array();
                result_lambda(jplayer);
            }
            else
                //getNextPlaylistItem() looks the player up itself
                getNextPlaylistItem(playerId, std::move(jplayer), Json::array(),
                                    0, it_count, result_lambda);
        });
    });
}

/* E4.1p. THE RECURSIVE, ASYNCHRONOUS STAGE - the one place of this epic where
 * a document has to survive a network round trip AND a self call.
 *
 * ⛔ BY VALUE, AND MOVED. Both documents are taken by value, moved into the
 * closure, and moved again into the next recursion. The move is what keeps
 * this linear: taken by value and captured with [=], every stage would deep
 * copy an array that grows with the playlist, i.e. O(N^2) node copies on a
 * library playlist of a few thousand tracks - a real cost that jansson's
 * refcount did not have.
 *
 * A Json& taken instead would be a use after free: the only frame that could
 * own the referent is the caller's, and the caller returns as soon as
 * get_playlist_item() has armed its callback. Nothing crashes on the
 * SYNCHRONOUS unroll of a test fake, which is exactly why the deferred cases
 * of tests/core/JsonApiAudioWireBytes_test.cpp drive the stages one at a time.
 *
 * The three json_decref() this function used to carry are gone with the
 * pointer: the closure owns its two documents and destroys them when the
 * player releases the callback, on the guarded path as on any other.
 */
void JsonApi::getNextPlaylistItem(const string &playerId, Json jplayer, Json jplaylist, int it_current, int it_count, std::function<void(const Json &)>result_lambda)
{
    //Entered either from decodeGetPlaylist() or from the recursion below, in
    //both cases right after an alive check, so this is safe to touch.
    AudioPlayer *player = dynamic_cast<AudioPlayer *>(ListeRoom::Instance().get_io(playerId));
    if (!player)
    {
        //The IO went away between two items. The client is still there and
        //must get an answer, but not a truncated playlist.
        result_lambda(playlistNoPlayerAnswer());
        return;
    }

    //One check per stage: a chain of N callbacks needs N checks, not one at
    //the top. Here N is the length of the playlist.
    std::weak_ptr<bool> alive = apiAlive;

    player->get_playlist_item(it_current,
                              [this, alive, playerId, it_current, it_count, result_lambda,
                               jplayer = std::move(jplayer),
                               jplaylist = std::move(jplaylist)](AudioPlayerData data) mutable
    {
        if (alive.expired())
            return;

        Json jtrack = Json::object();
        Params &infos = data.params;
        for (int i = 0;i < infos.size();i++)
        {
            string inf_key, inf_value;
            infos.get_item(i, inf_key, inf_value);

            jtrack[inf_key] = inf_value;
        }

        jplaylist.push_back(std::move(jtrack));

        int idx = it_current + 1;
        if (idx >= it_count)
        {
            //all track are queried, send back data
            jplayer["items"] = std::move(jplaylist);
            result_lambda(jplayer);
        }
        else
        {
            getNextPlaylistItem(playerId, std::move(jplayer), std::move(jplaylist),
                                idx, it_count, result_lambda);
        }
    });
}

/* E4.1p. The resolution itself, unchanged, extracted so that the two readers
 * below cannot drift apart. Its two messages are wire contract, spelling
 * included ("unkown").
 */
AudioPlayer *JsonApi::audioPlayerById(const string &id, string &err)
{
    AudioPlayer *player = nullptr;
    err.clear();

    if (id == "")
    {
        err = "empty player id";
        return player;
    }

    IOBase *io = ListeRoom::Instance().get_io(id);
    player = dynamic_cast<AudioPlayer *>(io);

    if (!player)
        err = "unkown player_id";

    return player;
}

AudioPlayer *JsonApi::getAudioPlayer(const Json &jdata, string &err)
{
    return audioPlayerById(jsonStringGet(jdata, "id"), err);
}

/* T3.19. AudioPlayer::database is a RAW POINTER the base constructor leaves
 * NULL (AudioPlayer.cpp:28), get_database() (AudioPlayer.h:105) hands it back
 * unguarded, and Squeezebox.cpp:81 is the ONLY assignment in the entire tree.
 * The sixteen audio_db actions - audioGetDbStats() and the fifteen
 * audioDbGet* - all dereferenced it with no check, so `audio_db` on any other
 * concrete player (RoonPlayer, for one) was a SIGSEGV of calaos_server
 * reachable by any AUTHENTICATED client. Not a malformed request: an UI that
 * offers music browsing without reading the published capability first took the
 * whole server down.
 *
 * THE TEST IS THE POINTER, NOT canDatabase(). The capability is a per class
 * constant (AudioPlayer.h:101 false, Squeezebox.h:204 true, RoonPlayer.h:177
 * false) whose only reader in the tree is buildJsonAudio() (:406), where it is
 * merely PUBLISHED; the precondition of the dereference is
 * `database != nullptr`, and the two are only accidentally equal today. Pinned
 * from both sides in JsonApiInputGuards_test.cpp by a player that announces a
 * database it does not own and one that owns a database it does not announce.
 *
 * NO HANDLER IS EDITED. Filtering on canDatabase() in the transports was
 * considered and measured: 32 dispatch branches across JsonApiHandlerHttp.cpp
 * and JsonApiHandlerWS.cpp, a duplicated getAudioPlayer() in both, and a new
 * ordering invented between three refusals E4.0e froze - to cover the same 16
 * call sites this one check covers on both transports.
 *
 * THE ANSWER REUSES THE EXISTING VOCABULARY (rule of the T3.17 series): these
 * very methods already answer {"error": <message>} for an unknown player id and
 * for bad paging arguments, and both transports already wrap such a document
 * for an unknown audio_action (JsonApiHandlerWS.cpp:461,
 * JsonApiHandlerHttp.cpp:862). NOT an empty item list, which would be
 * indistinguishable from a database that really holds nothing - a mutilated
 * answer is worse than a plain refusal.
 *
 * CALL IT IMMEDIATELY BEFORE THE DEREFERENCE, never at the top of the method.
 * The from/count gate refuses first today and must keep refusing first, so that
 * the only requests whose answer changes are the ones that used to kill the
 * process.
 */
bool JsonApi::audioDbUnavailable(AudioPlayer *player,
                                 const std::function<void(const Json &)> &result_lambda)
{
    if (player->get_database())
        return false;

    //E4.1q. Same single member document as before, built directly instead of
    //through a one-entry Params: same key, same words, same bytes. The sixteen
    //methods of the family share this refusal and a byte case asserts they
    //still do (JsonApiMusicDbWireBytes_test.cpp).
    result_lambda(Json{{ "error", "no music database" }});
    return true;
}

void JsonApi::audioGetDbStats(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    /* Destruction guard, shared by the five single-shot player-state methods
     * below. This JsonApi dies with its client connection
     * (HttpClient::~HttpClient() deletes the handler, base sub-object included,
     * HttpClient.cpp:162) while a player answer is still in flight, and
     * result_lambda is the handler's own lambda capturing raw handler pointers.
     * Same pattern as buildJsonState() (T2.15) and the playlist chain (T3.17a).
     *
     * One check is enough here, unlike the playlist chain: these are single
     * shot, one round trip and one answer, no recursion. And nothing is
     * allocated before the check, so the guarded branch has nothing to release
     * - the json objects of these methods are all built after it, from the
     * answer the player just gave.
     */
    if (audioDbUnavailable(player, result_lambda))
        return;

    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getStats([=](AudioPlayerData adata)
    {
        if (alive.expired()) return;

        adata.params.Add("audio_action", "get_stats");
        result_lambda(adata.params.toJson());
    });
}

void JsonApi::audioGetPlaylistSize(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    std::weak_ptr<bool> alive = apiAlive;

    player->get_playlist_size([=](AudioPlayerData adata)
    {
        if (alive.expired()) return;

        //Kept verbatim: this Add() writes into a Params the answer below does
        //NOT use, so audio_action never reaches the client. Dead since T3.17b
        //and pinned as such by the goldens - removing it is not this ticket's.
        adata.params.Add("audio_action", "get_playlist_size");
        //⛔ Utils::to_string(int) - a STRING, never a JSON number. The oracle
        //of the goldens is type strict: 3 is not "3".
        result_lambda(Json{{ "playlist_size", Utils::to_string(adata.ivalue) }});
    });
}

void JsonApi::audioGetTime(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    std::weak_ptr<bool> alive = apiAlive;

    player->get_current_time([=](AudioPlayerData adata)
    {
        if (alive.expired()) return;

        //Same dead Add() as audioGetPlaylistSize(), kept verbatim.
        adata.params.Add("audio_action", "get_time");
        /* ⛔ Utils::to_string(double) IS A BARE OSTRINGSTREAM AND STAYS ONE.
         * 1234.56789 leaves as "1234.57" and 123456789.0 as "1.23457e+08" -
         * six significant digits, scientific notation past them. It looks like
         * a bug and it is the contract: pinned by E4.0f goldens and, at the
         * byte level, by JsonApiAudioWireBytes_test. Handing the double to
         * nlohmann instead would change BOTH the formatting and the TYPE.
         */
        result_lambda(Json{{ "time_elapsed", Utils::to_string(adata.dvalue) }});
    });
}

void JsonApi::audioGetPlaylistItem(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    //jsonStringGet() and not jdata["item"].get<string>(): a member that is not
    //a JSON STRING must read as ABSENT here, exactly as jansson_string_get()
    //made it, so that {"item":2} is still refused instead of throwing.
    string it = jsonStringGet(jdata, "item");
    if (it.empty() || !Utils::is_of_type<int>(it))
    {
        result_lambda(Json{{ "error", "wrong item" }});
        return;
    }

    int item;
    Utils::from_string(it, item);

    std::weak_ptr<bool> alive = apiAlive;

    player->get_playlist_item(item, [=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(data.params.toJson());
    });
}

void JsonApi::audioGetCoverInfo(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    std::weak_ptr<bool> alive = apiAlive;

    player->get_album_cover([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        /* BEHAVIOUR CHANGE, ASSUMED - same one E4.1o declared for get_param.
         * A cover URL comes back from the player and ultimately from a
         * filesystem, which guarantees nothing about UTF-8. jansson dropped
         * the whole pair on an invalid byte and answered {}; the emitter's
         * error_handler_t::replace (E4.1b) now keeps it with one U+FFFD per
         * bad byte. Pinned by JsonApiAudioWireBytes_test.
         */
        result_lambda(Json{{ "cover", data.svalue }});
    });
}

/* E4.1q. TRANSCRIBED FROM ITS JANSSON BODY, NOT RETYPED - every oddity below is
 * production behaviour of every SqueezeboxDB getter and is pinned by the T3.17c
 * goldens and by JsonApiMusicDbWireBytes_test.cpp:
 *
 *   - the leading Params carrying the "count" marker is read for total_count
 *     AND appended to the items array, so items[0] of a normal answer is
 *     {"count":"2"} and not a row;
 *   - a count of "0" CLEARS the array but STILL emits "total_count":"0";
 *   - no count anywhere means NO total_count key at all - absent, never null;
 *   - everything stays a STRING. A count that became a JSON number would break
 *     the type-strict oracle of the goldens (3 != "3").
 *
 * TWO THINGS MOVE, both declared:
 *   1. KEY ORDER. total_count is still assigned before items, but nlohmann
 *      sorts, so "items" comes FIRST on the wire from now on. No golden sees it
 *      - they compare parsed documents - which is exactly why the byte cases
 *      exist.
 *   2. INVALID UTF-8. jansson_from_params() dropped the whole pair in silence
 *      (json_string() answered NULL, json_object_set_new() answered -1 and
 *      nobody looked); Params::toJson() keeps the bytes and the emitter's
 *      error_handler_t::replace turns each bad byte into U+FFFD. An ABSENT KEY
 *      BECOMES PRESENT, and an embedded NUL no longer truncates. This is the
 *      most exposed spot of the whole epic for that delta: album, artist,
 *      genre and FOLDER names are file tags and filesystem paths.
 */
Json JsonApi::processDbResult(const AudioPlayerData &data)
{
    Json ret = Json::object();
    //Json::array(), not a default constructed Json: a default constructed one
    //is `null`, and an answer with no row at all would emit "items":null
    //instead of "items":[] - a change no golden would catch.
    Json aret = Json::array();
    string scount;

    const vector<Params> &vp = data.vparams;
    for (const Params &p: vp)
    {
        if (p.Exists("count"))
            scount = p["count"];
        aret.push_back(p.toJson());
    }

    if (scount == "0")
        aret.clear();

    if (!scount.empty())
        ret["total_count"] = scount;
    ret["items"] = aret;

    return ret;
}

void JsonApi::audioDbGetAlbums(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    string itfrom = jsonStringGet(jdata, "from");
    string itcount = jsonStringGet(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        result_lambda(Json{{ "error", "wrong from/count" }});
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    /* Destruction guard, shared by the fifteen audioDbGet* music-database
     * methods below. This JsonApi dies with its client connection
     * (HttpClient::~HttpClient() deletes the handler, base sub-object included,
     * HttpClient.cpp:162) while a database answer is still in flight, and the
     * callback then touches TWO freed objects.
     *
     * The FIRST is `result_lambda`. It is captured by value into the [=]
     * lambda, and that std::function IS the handler's own lambda, holding the
     * handler's `this`; calling it after the handler died sends to freed
     * memory. This one is present in ALL FIFTEEN methods and is the one the
     * ASan traces land on.
     *
     * The SECOND is `this`: fourteen of the fifteen call processDbResult(), a
     * member of this very JsonApi, from inside the callback. This file emits 17
     * -Wdeprecated implicit-capture warnings in all - 14 for this family and 3
     * for the recursive playlist chain of T3.17a further up - and those 14
     * point here.
     *
     * DO NOT USE THE WARNING AS THE CRITERION FOR "NEEDS A GUARD". It
     * undercounts by construction: it only sees the second object. The
     * fifteenth method, audioDbGetTrackInfos(), calls no member and raises no
     * warning, yet it produces the same heap-use-after-free through the
     * captured std::function alone - measured under ASan, see
     * JsonApiMusicDb_test.cpp. All fifteen would need this guard even if
     * processDbResult() did not exist.
     *
     * Same pattern as buildJsonState() (T2.15), the playlist chain (T3.17a) and
     * the five single-shot player-state methods above (T3.17b).
     *
     * One check is enough here, as in T3.17b: these are single shot, one round
     * trip and one answer, no recursion. And nothing is allocated before the
     * check, so the guarded branch has nothing to release - processDbResult()
     * builds its json AFTER it, from the answer the database just gave.
     */
    if (audioDbUnavailable(player, result_lambda))
        return;

    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getAlbums([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetAlbumArtistItem(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    string itfrom = jsonStringGet(jdata, "from");
    string itcount = jsonStringGet(jdata, "count");
    string artist_id = jsonStringGet(jdata, "artist_id");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        result_lambda(Json{{ "error", "wrong from/count" }});
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    if (audioDbUnavailable(player, result_lambda))
        return;

    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getArtistsAlbums([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(processDbResult(data));
    }, from, count, artist_id);
}

void JsonApi::audioDbGetYearAlbums(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    string itfrom = jsonStringGet(jdata, "from");
    string itcount = jsonStringGet(jdata, "count");
    string year = jsonStringGet(jdata, "year");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        result_lambda(Json{{ "error", "wrong from/count" }});
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    if (audioDbUnavailable(player, result_lambda))
        return;

    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getYearsAlbums([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(processDbResult(data));
    }, from, count, year);
}

void JsonApi::audioDbGetGenreArtists(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    string itfrom = jsonStringGet(jdata, "from");
    string itcount = jsonStringGet(jdata, "count");
    string genre = jsonStringGet(jdata, "genre");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        result_lambda(Json{{ "error", "wrong from/count" }});
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    if (audioDbUnavailable(player, result_lambda))
        return;

    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getGenresArtists([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(processDbResult(data));
    }, from, count, genre);
}

void JsonApi::audioDbGetAlbumTitles(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    string itfrom = jsonStringGet(jdata, "from");
    string itcount = jsonStringGet(jdata, "count");
    string album_id = jsonStringGet(jdata, "album_id");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        result_lambda(Json{{ "error", "wrong from/count" }});
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    if (audioDbUnavailable(player, result_lambda))
        return;

    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getAlbumsTitles([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(processDbResult(data));
    }, from, count, album_id);
}

void JsonApi::audioDbGetPlaylistTitles(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    string itfrom = jsonStringGet(jdata, "from");
    string itcount = jsonStringGet(jdata, "count");
    string pl_id = jsonStringGet(jdata, "playlist_id");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        result_lambda(Json{{ "error", "wrong from/count" }});
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    if (audioDbUnavailable(player, result_lambda))
        return;

    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getPlaylistsTracks([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(processDbResult(data));
    }, from, count, pl_id);
}

void JsonApi::audioDbGetArtists(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    string itfrom = jsonStringGet(jdata, "from");
    string itcount = jsonStringGet(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        result_lambda(Json{{ "error", "wrong from/count" }});
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    if (audioDbUnavailable(player, result_lambda))
        return;

    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getArtists([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetYears(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    string itfrom = jsonStringGet(jdata, "from");
    string itcount = jsonStringGet(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        result_lambda(Json{{ "error", "wrong from/count" }});
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    if (audioDbUnavailable(player, result_lambda))
        return;

    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getYears([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetGenres(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    string itfrom = jsonStringGet(jdata, "from");
    string itcount = jsonStringGet(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        result_lambda(Json{{ "error", "wrong from/count" }});
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    if (audioDbUnavailable(player, result_lambda))
        return;

    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getGenres([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetPlaylists(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    string itfrom = jsonStringGet(jdata, "from");
    string itcount = jsonStringGet(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        result_lambda(Json{{ "error", "wrong from/count" }});
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    if (audioDbUnavailable(player, result_lambda))
        return;

    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getPlaylists([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetMusicFolder(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    string itfrom = jsonStringGet(jdata, "from");
    string itcount = jsonStringGet(jdata, "count");
    string folder_id = jsonStringGet(jdata, "folder_id");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        result_lambda(Json{{ "error", "wrong from/count" }});
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    if (audioDbUnavailable(player, result_lambda))
        return;

    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getMusicFolder([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(processDbResult(data));
    }, from, count, folder_id);
}

void JsonApi::audioDbGetSearch(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    string itfrom = jsonStringGet(jdata, "from");
    string itcount = jsonStringGet(jdata, "count");
    string search = jsonStringGet(jdata, "search");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        result_lambda(Json{{ "error", "wrong from/count" }});
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    if (audioDbUnavailable(player, result_lambda))
        return;

    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getSearch([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(processDbResult(data));
    }, from, count, search);
}

void JsonApi::audioDbGetRadios(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    string itfrom = jsonStringGet(jdata, "from");
    string itcount = jsonStringGet(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        result_lambda(Json{{ "error", "wrong from/count" }});
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    if (audioDbUnavailable(player, result_lambda))
        return;

    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getRadios([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetRadioItems(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    string itfrom = jsonStringGet(jdata, "from");
    string itcount = jsonStringGet(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        result_lambda(Json{{ "error", "wrong from/count" }});
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    string radio_id = jsonStringGet(jdata, "radio_id");
    string item_id = jsonStringGet(jdata, "item_id");
    string search = jsonStringGet(jdata, "search");

    if (audioDbUnavailable(player, result_lambda))
        return;

    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getRadiosItems([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(processDbResult(data));
    }, from, count, radio_id, item_id, search);
}

void JsonApi::audioDbGetTrackInfos(const Json &jdata, std::function<void(const Json &)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        result_lambda(Json{{ "error", err }});
        return;
    }

    string trackid = jsonStringGet(jdata, "track_id");

    if (audioDbUnavailable(player, result_lambda))
        return;

    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getTrackInfos([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(data.params.toJson());
    }, trackid);
}

/* E4.1o. ⭐ THE ORDER OF "ranges" IS SEMANTIC AND MUST NOT MOVE.
 *
 * The array is flattened over the seven days by the ladder below, so the
 * POSITION of an entry IS its day - it is the only order of this series that
 * carries meaning. A JSON array stays an array in nlohmann, so the ladder is
 * transcribed as it was, day by day, and
 * core/JsonApiParamsWireBytes_test.cpp asserts the resulting order ON THE RAW
 * BYTES with a fixture whose three days differ.
 *
 * What DOES move, declared: "ranges" is inserted first and "months" second,
 * and nlohmann sorts - so months now comes first on the wire. The nine E4.0c
 * goldens cannot see it, they compare parsed documents.
 *
 * Everything stays a STRING: TimeRange::toParams() fills a Params, which is a
 * map<string,string>. An hour that became a JSON number would break the
 * type-strict oracle (3 != "3").
 */
Json JsonApi::buildJsonGetTimerange(const Params &jParam)
{
    InPlageHoraire *o = dynamic_cast<InPlageHoraire *>(ListeRoom::Instance().get_io(jParam["id"]));
    if (!o)
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    //Json::array(), not a default constructed Json: a default constructed one
    //is `null`, and an IO with no range at all would answer "ranges":null
    //instead of "ranges":[] - a change no golden would catch.
    Json jarr = Json::array();

    for (int day = 0;day < 7;day++)
    {
        vector<TimeRange> h;
        if (day == 0) h = o->getMonday();
        if (day == 1) h = o->getTuesday();
        if (day == 2) h = o->getWednesday();
        if (day == 3) h = o->getThursday();
        if (day == 4) h = o->getFriday();
        if (day == 5) h = o->getSaturday();
        if (day == 6) h = o->getSunday();
        for (uint i = 0;i < h.size();i++)
            jarr.push_back(h[i].toParams(day).toJson());
    }

    stringstream ssmonth;
    ssmonth << o->months;
    string str = ssmonth.str();
    std::reverse(str.begin(), str.end());

    Json ret = Json::object();
    ret["ranges"] = jarr;
    ret["months"] = str;

    return ret;
}

/* E4.1o. THE ONLY BUILDER OF THIS CHAIN THAT READS CLIENT JSON.
 *
 * The document is handed down by the dispatch, which parses it (that parse is
 * E4.1s's to migrate, not this ticket's); what changed here is the type
 * traversed. Two contracts had to be transcribed by hand rather than reached
 * for:
 *
 *  - jansson_string_get() answers the DEFAULT on a member that is not a
 *    string, and on a NULL/non object root. jsonStringGet() below does the
 *    same. `jdata["id"]` would have thrown on a numeric id.
 *  - jansson_decode_object()'s FLATTENING CONTRACT, see decodeJsonObject().
 *    ⛔ Params::fromJson() is NOT a substitute: it assigns the json value
 *    straight into a std::string and throws type_error.302 on anything that
 *    is not a JSON string. A client sending {"day": 1} is served today and
 *    would have terminated the process tomorrow. Same tripwire as ScriptWire,
 *    ReolinkWire, WagoWire, OLAWire and the two KNX ends carry.
 */
Json JsonApi::buildJsonSetTimerange(const Json &jdata)
{
    string id = jsonStringGet(jdata, "id");
    InPlageHoraire *o = dynamic_cast<InPlageHoraire *>(ListeRoom::Instance().get_io(id));
    if (!o)
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    o->clear();

    /* json_array_foreach() ran json_array_size(NULL) == 0 and never entered
     * the loop: an absent "ranges", or one that is not an array, must stay a
     * no-op and NOT an error - the caller still gets success:true, and
     * o->clear() above has already run. Frozen, not tidied.
     */
    Json jranges = Json::array();
    if (jdata.is_object())
    {
        const Json::const_iterator it = jdata.find("ranges");
        if (it != jdata.cend() && it->is_array())
            jranges = *it;
    }

    for (const Json &value: jranges)
    {
        Params p;
        decodeJsonObject(value, p);

        TimeRange tr(p);

        cout << "Adding timerange: " << p.toString() << endl;

        if (p["day"] == "1") o->AddMonday(tr);
        if (p["day"] == "2") o->AddTuesday(tr);
        if (p["day"] == "3") o->AddWednesday(tr);
        if (p["day"] == "4") o->AddThursday(tr);
        if (p["day"] == "5") o->AddFriday(tr);
        if (p["day"] == "6") o->AddSaturday(tr);
        if (p["day"] == "7") o->AddSunday(tr);
    }

    //set months
    string m = jsonStringGet(jdata, "months");
    if (!m.empty())
    {
        //reverse to have a left to right months representation
        std::reverse(m.begin(), m.end());

        try
        {
            bitset<12> mset(m);
            o->months = mset;
        }
        catch(...)
        {
            cErrorDom("network") << "wrong parameters for months: " << m;
        }
    }

    EventManager::create(CalaosEvent::EventTimeRangeChanged,
                         { { "id", o->get_param("id") } });

    AutoScenario *as = autoScenarioOfTimeRange(o);
    if (as)
    {
        EventManager::create(CalaosEvent::EventScenarioChanged,
                             { { "id", as->getIOScenario()->get_param("id") } });
    }

    //Resave config
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "success", "true" }};
    return p.toJson();
}

Json JsonApi::buildAutoscenarioList(const Json &jdata)
{
    VAR_UNUSED(jdata);
    Json jret;
    Json jarr = Json::array();

    for (auto it: ListeRoom::Instance().getAutoScenarios())
        jarr.push_back(it->toJson());

    jret["scenarios"] = jarr;
    return jret;
}

Json JsonApi::buildAutoscenarioGet(const Json &jdata)
{
    string id = jsonStringGet(jdata, "id");
    Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_io(id));
    if (!sc || !sc->getAutoScenario())
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    return sc->toJson();
}

Json JsonApi::buildAutoscenarioCreate(const Json &jdata)
{
    ScenarioPayload payload;
    string err;
    if (!parseScenarioPayload(jdata, payload, err))
        return Params({{ "error", err }}).toJson();

    Params params;
    params.Add("auto_scenario", Calaos::get_new_scenario_id());
    params.Add("name", payload.name);
    params.Add("visible", payload.visible? "true": "false");
    params.Add("cycle", payload.cycle? "true": "false");
    params.Add("disabled", payload.enabled? "false": "true");
    params.Add("type", "scenario");

    IOBase *in = ListeRoom::Instance().createIO(params, payload.room);
    Scenario *scenario = dynamic_cast<Scenario *>(in);
    if (!scenario || !scenario->getAutoScenario())
    {
        //createIO() returns null on an IO factory miss, and the dynamic_cast
        //rejects anything else than a Scenario: answer an error instead of
        //crashing on the null pointer
        cErrorDom("network") << "Scenario creation failed: "
                             << (in? "created IO is not a scenario": "IO factory returned null");
        if (in)
            ListeRoom::Instance().deleteIO(in);

        Params perr = {{ "error", "scenario creation failed" }};
        return perr.toJson();
    }

    //The definition IS the scenario: it is written whole, then projected into
    //rules once. There is no step by step authoring path any more.
    AutoScenarioDef *def = scenario->getDefinition();
    def->steps = payload.steps;
    def->finalStep = payload.finalStep;

    if (!scenario->getAutoScenario()->rebuildRules())
    {
        //The internal IOs of the scenario could not be created: roll the
        //half-created scenario back and answer an error
        cErrorDom("network") << "Scenario creation failed: unable to create the scenario rules";
        scenario->getAutoScenario()->deleteAll();
        ListeRoom::Instance().deleteIO(scenario);

        Params perr = {{ "error", "scenario creation failed" }};
        return perr.toJson();
    }

    EventManager::create(CalaosEvent::EventScenarioAdded,
                         { { "id", scenario->get_param("id") } });

    //Resave config, auto scenarios have probably created/deleted ios and rules
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "id", scenario->get_param("id") }};
    return p.toJson();
}

Json JsonApi::buildAutoscenarioDelete(const Json &jdata)
{
    string id = jsonStringGet(jdata, "id");
    Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_io(id));
    if (!sc || !sc->getAutoScenario())
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    sc->getAutoScenario()->deleteAll();

    //delete the scenario IO
    ListeRoom::Instance().deleteIO(sc);

    EventManager::create(CalaosEvent::EventScenarioDeleted,
                         { { "id", id } });

    //Resave config
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "success", "true" }};
    return p.toJson();
}

Json JsonApi::buildAutoscenarioModify(const Json &jdata)
{
    string id = jsonStringGet(jdata, "id");
    Scenario *scenario = dynamic_cast<Scenario *>(ListeRoom::Instance().get_io(id));
    if (!scenario || !scenario->getAutoScenario())
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    /* VALIDATE, THEN MUTATE. Everything below this line changes the
     * configuration; nothing above it does. The other order destroys the rules
     * and only then discovers a malformed payload, with the scenario gone.
     */
    ScenarioPayload payload;
    string err;
    if (!parseScenarioPayload(jdata, payload, err))
        return Params({{ "error", err }}).toJson();

    AutoScenario *as = scenario->getAutoScenario();

    if (payload.name != scenario->get_param("name"))
    {
        scenario->set_param("name", payload.name);

        EventManager::create(CalaosEvent::EventIOChanged,
                             { { "id", scenario->get_param("id") },
                               { "name", payload.name }});
    }

    const string visible = payload.visible? "true": "false";
    if (visible != scenario->get_param("visible"))
    {
        scenario->set_param("visible", visible);

        EventManager::create(CalaosEvent::EventIOChanged,
                             { { "id", scenario->get_param("id") },
                               { "visible", visible }});
    }

    Room *old_room = ListeRoom::Instance().getRoomByIO(scenario);
    if (payload.room != old_room)
    {
        //getRoomByIO() answers null for an IO no room holds; the target room
        //is guaranteed non null by the validation above.
        if (old_room)
            old_room->RemoveIOFromRoom(scenario);
        payload.room->AddIO(scenario);

        EventManager::create(CalaosEvent::EventRoomChanged,
                             { { "io_id_added", scenario->get_param("id") },
                               { "room_name", payload.room->get_name() },
                               { "room_type", payload.room->get_type() }});
    }

    //Both before the rebuild: `cycle` decides how the last step chains back.
    as->setCycling(payload.cycle);
    as->setDisabled(!payload.enabled);

    /* deleteRules() is the authoring call that takes the rules back from a
     * configuration that predates the definition - the generator stands down
     * over rules it did not write until something claims them explicitly.
     */
    as->deleteRules();

    AutoScenarioDef *def = scenario->getDefinition();
    def->steps = payload.steps;
    def->finalStep = payload.finalStep;

    if (!as->rebuildRules())
    {
        //One of the internal scenario IOs disappeared or was replaced: the
        //rules cannot be rebuilt, answer an error instead of crashing
        cErrorDom("network") << "Scenario modification failed: unable to rebuild the scenario rules";
        Params perr = {{ "error", "scenario modification failed" }};
        return perr.toJson();
    }

    EventManager::create(CalaosEvent::EventScenarioChanged,
                         { { "id", scenario->get_param("id") } });

    //Resave config
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "success", "true" }};
    return p.toJson();
}

Json JsonApi::buildAutoscenarioAddSchedule(const Json &jdata)
{
    string id = jsonStringGet(jdata, "id");
    Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_io(id));
    if (!sc || !sc->getAutoScenario())
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    sc->getAutoScenario()->addSchedule();

    //addSchedule() leaves the time range null when the IO could not be built
    //(a factory miss, or an IO of another type squatting the derived id).
    InPlageHoraire *range = sc->getAutoScenario()->getIOTimeRange();
    if (!range)
    {
        cErrorDom("network") << "Scenario schedule creation failed for " << id;
        Params perr = {{ "error", "scenario schedule creation failed" }};
        return perr.toJson();
    }

    EventManager::create(CalaosEvent::EventScenarioChanged,
                         { { "id", sc->get_param("id") } });

    //Resave config
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "id", range->get_param("id") }};
    return p.toJson();
}

Json JsonApi::buildAutoscenarioDelSchedule(const Json &jdata)
{
    string id = jsonStringGet(jdata, "id");
    Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_io(id));
    if (!sc || !sc->getAutoScenario())
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    sc->getAutoScenario()->deleteSchedule();

    EventManager::create(CalaosEvent::EventScenarioChanged,
                         { { "id", sc->get_param("id") } });

    //Resave config
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "success", "true" }};
    return p.toJson();
}

Json JsonApi::buildAutoscenarioReenable(const Json &jdata)
{
    string id = jsonStringGet(jdata, "id");
    Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_io(id));
    if (!sc || !sc->getAutoScenario())
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    /* The refusal is the point of this command: tryReenable() answers false
     * while the scenario still references IOs that do not resolve, and fills
     * the message with their ids. Answering success and letting the next
     * detection pass disable the scenario again would reproduce, one level up,
     * the silent no-op the arbitration removes.
     */
    string err;
    if (!sc->getAutoScenario()->tryReenable(err))
    {
        Params perr = {{ "error", err }};
        return perr.toJson();
    }

    //The flag lives in io.xml (a param of the Scenario IO), so only that one
    //has to be rewritten: no rule was touched.
    Config::Instance().SaveConfigIO();

    Params p = {{ "success", "true" }};
    return p.toJson();
}

void JsonApi::buildJsonEventLog(const Params &jParam, std::function<void(Json &)> callback)
{
    int page = 0;
    int perPage = 100;

    /* T3.25. NOT from_string(): since T3.25 a blank value writes T{} instead of
     * leaving the destination alone, and an ABSENT per_page (Params::operator[]
     * returns "") would therefore have become 0 - which the guard 60 lines below
     * refuses outright. A request that answers a full page of 100 events today
     * would have started answering {"error":"per_page is out of range"}.
     *
     * ⛔ AND NOT from_string_or_keep() EITHER - this is the one site in src/
     * where the difference matters. Since the T3.25 review, _or_keep() also
     * keeps the default on an OVERFLOW, and T3.19's documented behaviour
     * requires the opposite here: per_page:"99999999999" must be ANSWERED with
     * the saturated 2147483647 echoed back (JsonApiInputGuards_test
     * AHugePerPageSaturatesToIntMaxAndIsHarmless, JsonApiSession_test
     * EventLogSaturatesAVeryLargePerPage). _unless_blank() keeps T3.19 EXACTLY,
     * all three halves of it: blank keeps the 100, "abc" reads as 0 and is
     * refused, "1,5" still reads as 1, "99999999999" still saturates.
     */
    Utils::from_string_unless_blank(jParam["page"], page);
    Utils::from_string_unless_blank(jParam["per_page"], perPage);

    /* T3.17f. Both callbacks below are armed here and run MUCH later: HistLogger
     * queues the query for its sqlite worker thread and wakes the loop back up
     * through a uvw::AsyncHandle it owns (HistLogger.cpp:128-161). The client
     * can disconnect in that window, and ~HttpClient() (HttpClient.cpp:162)
     * then deletes the handler, this JsonApi sub-object included.
     *
     * Neither body odr-uses `this`, so `[=]` captures no `this` and the
     * -Wdeprecated warning the T3.17 series long used as its danger detector is
     * SILENT here. That criterion is retired (T3.17.md, "Correction du critere
     * de diagnostic de la serie"): the freed object travels inside `callback`,
     * a std::function captured by value which IS the handler's own lambda and
     * holds the handler's `this` - JsonApiHandlerWS::processEventLog() and
     * JsonApiHandlerHttp::processEventLog(). One guard here therefore covers
     * both transports, and neither handler needs editing: they are one object
     * with one owner and one destruction.
     *
     * One check per stage, first statement of each body, and nothing is emitted
     * when it fires: the client is already gone, so there is no one left to
     * answer and touching any member would be the use-after-free itself.
     * Nothing to free either - this is the only method of the file that builds
     * its answer with nlohmann rather than raw json_t*, and it is built after
     * this point, so a bare return leaks nothing (T3.17a needed json_decref,
     * T3.17b/c did not).
     */
    std::weak_ptr<bool> alive = apiAlive;

    if (jParam["uuid"] != "")
    {
        //Only load 1 event
        HistLogger::Instance().getEvent(jParam["uuid"],
                [=](bool success, string errorMsg, const HistEvent &event)
        {
            if (alive.expired()) return;

            if (!success)
            {
                Json err = {{ "error", errorMsg }};
                callback(err);
                return;
            }

            Json j = event.toJson();
            callback(j);
        });

        return;
    }

    /* T3.19. `perPage` comes out of Utils::from_string() (StringUtils.h:104-111)
     * whose return code is ignored, and since C++11 a FAILED extraction WRITES
     * ZERO into its destination: "abc", "true", "1,5" - and "0" itself - all
     * reached HistLogger as zero. HistLogger::getEvents() does not clamp either,
     * and HistLogger.cpp:268 then computes `rowcount / ac->per_page` INSIDE the
     * sqlite worker thread: integer division by zero, SIGFPE, process dead. The
     * try of :257 catches nothing, a signal is not an exception. Reachable by
     * any AUTHENTICATED client on both transports with per_page=0.
     * COUNTER-INTUITIVE, and why it went unseen: a HUGE per_page saturates to
     * INT_MAX and is harmless; an ABSURD one is fatal.
     *
     * A NEGATIVE per_page is refused by the same test rather than left to
     * HistLogger's page check: sqlite reads a negative LIMIT as "no limit", so
     * on a table small enough for the page check to pass the query would return
     * EVERY row while the document claimed per_page:-5 - a payload lying about
     * itself. That window is WIDER than it looks: with per_page:-5 the integer
     * arithmetic of HistLogger.cpp:268-273 lets the page check pass for every
     * rowcount up to 9 EXCEPT 5 - the single refusal sat in the middle of the
     * window, and it named the wrong parameter ("page is out of range").
     *
     * THE TEST IS ON THE VALUE, NOT ON from_string()'s RETURN CODE, on purpose.
     * Every per_page that gets an answer today keeps exactly that answer (an
     * empty or absent one keeps the default 100, a partial parse like "1,5"
     * keeps its 1), and the premise E4.0e pinned in
     * FromStringWritesZeroOnFailureWhichIsWhyEventLogCanDivideByZero stays true
     * word for word - from_string() still writes 0 on failure, and that is
     * still exactly why this guard has to exist.
     *
     * AFTER the uuid branch above, never before it: that branch returns without
     * ever calling getEvents(), so per_page is meaningless there and refusing it
     * would take away an answer that works today.
     *
     * The document is the {"error": <message>} shape this method's two callbacks
     * already produce, and the message is worded after HistLogger's own "page is
     * out of range" (:276) - no new error shape, per the T3.17 rule.
     *
     * NO `page` GUARD IS ADDED HERE, AND THAT IS DELIBERATE, NOT AN OVERSIGHT.
     * The ticket asked to clamp per_page "and check page"; page needs no check
     * because HistLogger already owns one that cannot be bypassed.
     * HistLogger.cpp:270-277 refuses `page < 0` and `page > total_page` BEFORE
     * `start` (:269, `page * per_page`) is ever used in a query, so the only
     * page values that reach the LIMIT clause are already in range and
     * `start` cannot overflow. Duplicating that check here would add a second
     * owner for one refusal and invent an ordering between the two.
     * The resulting behaviour is pinned from the outside, so it cannot drift
     * silently: ANonNumericPageIsStillReadAsPageZero (a failed from_string()
     * writes 0, which is also the default, so a garbage page is
     * indistinguishable from page 0) and
     * APageOutOfRangeIsStillHistLoggersOwnRefusal (the refusal stays
     * HistLogger's, asynchronous, worded by HistLogger). DO NOT add a page
     * check here "just in case", and do not remove HistLogger's - it is the
     * one doing the work.
     */
    if (perPage <= 0)
    {
        Json err = {{ "error", "per_page is out of range" }};
        callback(err);
        return;
    }

    HistLogger::Instance().getEvents(page, perPage,
            [=](bool success, string errorMsg, const vector<HistEvent> &events, int total_page, int total_count)
    {
        if (alive.expired()) return;

        Json jevents = Json::array();

        if (!success)
        {
            Json err = {{ "error", errorMsg }};
            callback(err);
            return;
        }

        for (const auto &e: events)
            jevents.emplace_back(e.toJson());

        Json jroot = {
            { "total_page", total_page },
            { "total_count", total_count },
            { "page", page },
            { "per_page", perPage },
            { "events", jevents }
        };

        callback(jroot);
    });
}

bool JsonApi::registerPushToken(const Params &jParam)
{
    if (jParam["token"] == "")
        return false;
    if (jParam["hardware"] != "android" &&
        jParam["hardware"] != "ios")
        return false;

    HistLogger::Instance().registerPushToken(jParam["token"],
            jParam["hardware"] == "ios"? HistLogger::PUSH_HW_IOS: HistLogger::PUSH_HW_ANDROID);

    return true;
}

bool JsonApi::changeCredentials(string olduser, string oldpass, string newuser, string newpass)
{
    if (!checkCredentials(olduser, oldpass))
        return false; //wrong old user/pass

    //change user/pass
    Utils::set_config_option("cn_user", newuser);
    Utils::set_config_option("cn_pass", newpass);
    //remove old entries if needed
    Utils::del_config_option("calaos_user");
    Utils::del_config_option("calaos_password");
    sync();

    return true;
}

Json JsonApi::buildJsonStatusInfo(IOBase *io)
{
    //A NULL Json, deliberately, not an empty object: buildJsonIO() tests it to
    //decide whether the "status_info" key exists at all, and Json::object() is
    //NOT null - it would put "status_info":{} on every IO of every payload.
    if (!io || !io->hasStatusInfo()) return Json();

    Json jret = Json::object();

    Params p = io->getStatusInfo();

    for (int i = 0;i < p.size();i++)
    {
        string key, value;
        p.get_item(i, key, value);

        jret[key] = value;
    }

    return jret;
}
