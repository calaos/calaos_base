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
#include "JsonApiHandlerWS.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "PollListenner.h"
#include "Prefix.h"
#include "McpServerManager.h"
#include "CalaosConfig.h"
#include "AudioPlayer.h"
#include "IPCam.h"
#include "InPlageHoraire.h"
#include "HttpCodes.h"
#include "WebSocket.h"

namespace
{

/* E4.1q. jansson_string_get()'s contract, kept BY HAND for a nlohmann document
 * so that processAudioDb() can dispatch on the parse processApi() ALREADY did:
 * the DEFAULT on an absent member, on a member that is not a JSON string, and
 * on a root that is not an object (json_object_get(NULL, k) answered NULL).
 * `j["k"].get<string>()` does none of that - it throws.
 *
 * Identical, deliberately, to the copy JsonApi.cpp carries and to the four
 * driver wires: one contract in several copies, to be folded into one place
 * rather than improved here.
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

/* E4.1s. jansson_decode_object()'s FLATTENING CONTRACT, kept BY HAND: a string
 * as is, a boolean as the WORD "true"/"false", any number through
 * Utils::to_string(double) - a bare ostringstream, frozen on purpose and not
 * "fixed" here - and ANY OTHER TYPE (object, array, null) the EMPTY STRING,
 * WITH THE KEY STILL ADDED. A non object iterates zero times, exactly as
 * json_object_foreach() did on a NULL or on a non object.
 *
 * ⚠️ THE ORDER OF ITERATION CHANGES AND IT DOES NOT MATTER: Params is a
 * std::map, so the destination is sorted either way, and duplicate keys were
 * already collapsed at PARSE time (last one wins, measured identical in both
 * libraries).
 *
 * Identical, deliberately, to the copy JsonApi.cpp carries, to the one in
 * JsonApiHandlerHttp.cpp and to ScriptWire::decodeObject(): one contract in
 * several copies, to be folded into one place rather than improved here.
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

/* E4.1s. The "items" array of get_state / get_io, transcribed one for one from
 * json_object_get + json_is_array + json_array_foreach + json_is_string. Every
 * guard is kept: an absent "items", an "items" that is not an array, and a non
 * string element are all SILENTLY SKIPPED and never an error.
 */
inline void collectStringItems(const Json &jdata, vector<string> &iolist)
{
    if (!jdata.is_object())
        return;

    const Json::const_iterator it = jdata.find("items");
    if (it == jdata.cend() || !it->is_array())
        return;

    for (const Json &value: *it)
    {
        if (value.is_string())
            iolist.push_back(value.get<std::string>());
    }
}

} //namespace

JsonApiHandlerWS::JsonApiHandlerWS(HttpClient *client):
    JsonApi(client)
{
    evcon = EventManager::Instance().newEvent.connect(sigc::mem_fun(*this, &JsonApiHandlerWS::handleEvents));
}

JsonApiHandlerWS::~JsonApiHandlerWS()
{
    evcon.disconnect();
}

string JsonApiHandlerWS::clientIp() const
{
    if (!httpClient)
        return "unknown";

    //The address the trusted proxy hop saw, NOT the TCP peer: calaos_server
    //sits behind haproxy, so every client shares the peer address and keying
    //LoginThrottle on it gives the whole installation a single backoff bucket
    //- one attacker locks everybody out, and his own budget is diluted by
    //everybody else. Same identity as the per-IP connection cap, which has
    //always read it (HttpClient::getEffectiveClientIp, and its trust note).
    return httpClient->getEffectiveClientIp();
}

void JsonApiHandlerWS::handleEvents(const CalaosEvent &event)
{
    if (!loggedin)
        return;

    cDebugDom("network") << "Handling event: " << event.toString();

    /* E4.1l: CalaosEvent::toJson() now answers a Json, so this call resolves to
     * the nlohmann overload of sendJson() (:75) instead of the jansson one
     * (:69) - the seam was already there and in service, nothing else was
     * needed. Consequence on the bytes, declared: the envelope keys sort
     * (data before msg) and so do the four members of the event object, and
     * the escaping moves from jansson's UPPERCASE \u00E9 to nlohmann's
     * lowercase \u00e9. Both are pure ASCII; pinned by
     * tests/core/EventWireBytes_test.cpp.
     */
    sendJson("event", event.toJson());
}

/* ⭐ E4.1s. WHAT IS LEFT OF sendJson(const string &, json_t *, const string &)
 * ONCE ITS PAYLOAD IS GONE: the envelope WITHOUT a "data" member.
 *
 * That overload OMITTED the key when the pointer was null. The nlohmann
 * overload below always writes it, so routing the two null callers
 * (processGetState() and processGetIO(), on a message that carries no "data")
 * through it would have turned `{"msg":...,"msg_id":...}` into
 * `{"msg":...,"msg_id":...,"data":null}` - a DIFFERENT document, and one the
 * epic's own contract forbids ("absent key, never null"). Golden
 * e40e_ws_get_state_without_data pins it.
 *
 * ⭐ NOT ONE BYTE MOVES HERE: jansson walked msg then msg_id in INSERTION
 * order, and "msg" < "msg_id" alphabetically, so the sorted order is the same
 * order. The payload is pure ASCII by construction (a message type and a
 * client id), so the escaping question does not arise either. Pinned, on the
 * RAW message and not on a parsed document, by
 * core/JsonApiDispatchWireBytes_test.
 */
void JsonApiHandlerWS::sendJsonNoData(const string &msg_type, const string &client_id)
{
    Json jroot = {{ "msg", msg_type }};

    if (client_id != "")
        jroot["msg_id"] = client_id;

    sendData.emit(jroot.dump(-1, ' ', true, Json::error_handler_t::replace));
}

void JsonApiHandlerWS::sendJson(const string &msg_type, const Json &json, const string &client_id)
{
    Json jroot = {{ "msg", msg_type }};

    if (client_id != "")
        jroot["msg_id"] = client_id;

    jroot["data"] = json;

    //E4.1b: same three invariants as JsonApiHandlerHttp::sendJson(const Json &),
    //and the same reason - the jansson overload this replaced emitted ASCII
    //only, and a bare dump() on a payload holding invalid UTF-8 terminates the
    //process. RemoteUIWebSocketHandler inherits this overload
    //(RemoteUIWebSocketHandler.h:103), so the RemoteUI device wire is covered
    //here and has no emitter of its own.
    //E4.1s: this is now the ONLY payload emitter of this transport.
    sendData.emit(jroot.dump(-1, ' ', true, Json::error_handler_t::replace));
}

void JsonApiHandlerWS::processApi(const string &data, const Params &paramsGET)
{
    VAR_UNUSED(paramsGET); //not used for websocket

    Params jsonRoot;
    Params jsonData;

    /* ⛔⭐⭐ E4.1s. THE REQUEST PARSE, and the ONE change of this ticket that is
     * not about formatting. From E4.1m to E4.1r this nlohmann parse ran
     * ALONGSIDE jansson's - one for the redacted log line and the migrated
     * readers, one for the dispatch. There is only one now, and no second
     * parse anywhere.
     *
     * THE TWO PARSERS DO NOT DRAW THE SAME LINE. Measured, pinned case by case
     * in tests/core/JsonApiDispatchWireBytes_test.cpp, DECLARED in
     * docs/refactoring/RELEASE_NOTES.md, and spelled out at length on the twin
     * line of JsonApiHandlerHttp::processApi(). In one sentence: an escaped
     * "\u0000" and an integer beyond int64 used to be REFUSED and are now
     * served; invalid UTF-8, a lone surrogate, a real-number overflow,
     * trailing garbage and a raw NUL are refused by both and MUST STAY
     * REFUSED. The third widening, an unbounded nesting depth, has been closed
     * again - see the twin line of JsonApiHandlerHttp::processApi() for what
     * the ceiling actually protects.
     */
    Json jsonRootDoc;

    if (!requestNestingWithinLimit(data))
        cWarningDom("network") << "Request nesting deeper than "
                               << MaxRequestNestingDepth << " levels, refused";
    else
        jsonRootDoc = Json::parse(data, nullptr, false);

    if (!jsonRootDoc.is_object())
    {
        //The parser's own message is gone with the parser: Json::parse() in
        //its non throwing form does not produce one. This is a debug line.
        cDebugDom("network") << "Error loading json";
        return;
    }

    if (cDebugDomEnabled("network"))
        cDebugDom("network") << dumpJsonRedacted(jsonRootDoc);

    /* E4.1s: the "data" member as a POINTER, because ABSENT and `"data": null`
     * are two different answers here. json_object_get() gave NULL for the
     * first and a json_null for the second, and processGetState() /
     * processGetIO() branch on precisely that. `jsonRootDoc` is const and
     * outlives every use, so the pointer stays valid.
     */
    const Json *jdata = nullptr;
    {
        const Json::const_iterator it = jsonRootDoc.find("data");
        if (it != jsonRootDoc.cend())
            jdata = &(*it);
    }

    /* E4.1o: Json::object() and not a default constructed Json on the absent
     * path: json_object_get(jroot, "data") answered NULL there, and every
     * reader below treats a missing member as absent, not as null.
     */
    const Json jsonDataDoc = jdata? *jdata : Json::object();

    //decode the json root object into Params
    decodeJsonObject(jsonRootDoc, jsonRoot);

    if (jdata)
        decodeJsonObject(*jdata, jsonData);

    //Format: { msg: "type", msg_id: id, data: {} }

    if (jsonRoot["msg"] == "login")
    {
        const string ip = clientIp();
        const double now = Utils::getMainLoopTime();

        if (LoginThrottle::isBlocked(ip, now))
        {
            cWarningDom("network") << "Too many failed logins from " << ip << ", login refused";

            sendJson("login", {{ "success", "false" }}, jsonRoot["msg_id"]);
            closeConnection.emit(WebSocketFrame::CloseCodeNormal, "login failed!");
        }
        //Not logged in, need to wait for a correct login
        else if (!checkCredentials(jsonData["cn_user"], jsonData["cn_pass"]))
        {
            LoginThrottle::registerFailure(ip, now);

            cDebugDom("network") << "Login failed!";

            //E4.1s: the last caller of the jansson overload that carried a
            //payload. One ASCII pair, byte for byte the same document as the
            //two brace-initialised twins a few lines above and below.
            sendJson("login", Json{{ "success", "false" }}, jsonRoot["msg_id"]);

            //Close the connection on login failure
            closeConnection.emit(WebSocketFrame::CloseCodeNormal, "login failed!");
        }
        else
        {
            LoginThrottle::registerSuccess(ip);

            sendJson("login", {{ "success", "true" }}, jsonRoot["msg_id"]);

            loggedin = true;
        }
    }
    else if (jsonRoot["msg"] == "login_service")
    {
        processLoginService(jsonData, jsonRoot["msg_id"]);
    }
    else if (loggedin) //only process other api if loggedin
    {
        // S2: messages blocked for service-scoped sessions.
        auto scopeDenied = [&](const string &msg)
        {
            cWarningDom("mcp") << "service scope denied action: " << msg;
            sendJson(msg, {{ "error", "scope denied" }}, jsonRoot["msg_id"]);
        };

        if (jsonRoot["msg"] == "get_home")
            processGetHome(jsonData, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "get_state")
            processGetState(jdata, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "get_states")
            processGetStates(jsonData, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "query")
            processQuery(jsonData, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "get_param")
            processGetParam(jsonData, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "set_param")
        {
            if (serviceScope) scopeDenied("set_param");
            else processSetParam(jsonData, jsonRoot["msg_id"]);
        }
        else if (jsonRoot["msg"] == "del_param")
        {
            if (serviceScope) scopeDenied("del_param");
            else processDelParam(jsonData, jsonRoot["msg_id"]);
        }
        else if (jsonRoot["msg"] == "set_state")
            processSetState(jsonData, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "get_playlist")
            processGetPlaylist(jsonData, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "get_io")
            processGetIO(jdata, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "audio")
            processAudio(jsonDataDoc, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "audio_db")
        {
            if (serviceScope) scopeDenied("audio_db");
            else processAudioDb(jsonDataDoc, jsonRoot["msg_id"]);
        }
        else if (jsonRoot["msg"] == "get_timerange")
            processGetTimerange(jsonData, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "set_timerange")
        {
            if (serviceScope) scopeDenied("set_timerange");
            else processSetTimerange(jsonDataDoc, jsonRoot["msg_id"]);
        }
        else if (jsonRoot["msg"] == "autoscenario")
        {
            //E4.6e: it creates, modifies and DELETES scenarios and rules, so it
            //belongs with the other mutating commands. A session refused the
            //time range of a schedule could destroy the scenario owning it.
            if (serviceScope) scopeDenied("autoscenario");
            else processAutoscenario(jsonDataDoc, jsonRoot["msg_id"]);
        }
        else if (jsonRoot["msg"] == "eventlog")
        {
            if (serviceScope) scopeDenied("eventlog");
            else processEventLog(jsonData, jsonRoot["msg_id"]);
        }
        else if (jsonRoot["msg"] == "register_push")
        {
            if (serviceScope) scopeDenied("register_push");
            else processRegisterPush(jsonData, jsonRoot["msg_id"]);
        }
        else if (jsonRoot["msg"] == "settings")
        {
            if (serviceScope) scopeDenied("settings");
            else processSettings(jsonData, jsonRoot["msg_id"]);
        }

//        else if (jsonParam["action"] == "get_cover")
//            processGetCover();
//        else if (jsonParam["action"] == "get_camera_pic")
//            processGetCameraPic();
//        else if (jsonParam["action"] == "config")
//            processConfig(jroot);
    }
}

void JsonApiHandlerWS::processGetHome(const Params &jsonReq, const string &client_id)
{
    /* E4.1m: the three builders answer a Json, so this call resolves to the
     * nlohmann overload of sendJson() (:75) instead of the jansson one (:69).
     * The seam was already there and in service - E4.1b built it, E4.1l
     * crossed it for events - and nothing else was needed here.
     *
     * Consequence on the bytes, declared and pinned by
     * core/JsonApiModelWireBytes_test: the envelope sorts (data before msg),
     * the three members sort (audio, cameras, home, where json_pack() walked
     * them in the exact opposite order), every room and every IO object sorts,
     * and the escaping moves to a lowercase hexadecimal. All of it stays pure
     * ASCII, as this wire has always been.
     */
    Json jret = {{ "home", buildJsonHome() },
                 { "cameras", buildJsonCameras() },
                 { "audio", buildJsonAudio() }};

    sendJson("get_home", jret, client_id);
}

void JsonApiHandlerWS::processGetState(const Json *jdata, const string &client_id)
{
    if (!jdata)
    {
        //E4.1s: sendJsonNoData() and not sendJson(..., Json(), ...) - the
        //answer OMITS "data", it does not carry a null. Golden
        //e40e_ws_get_state_without_data.
        sendJsonNoData("get_state", client_id);
        return;
    }

    vector<string> iolist;
    collectStringItems(*jdata, iolist);

    /* E4.1n: the three builders answer a Json, so these three calls resolve to
     * the nlohmann overload of sendJson() (:90) instead of the jansson one
     * (:78). The seam was already there and in service; no adapter was needed
     * and none was written. Consequence on the bytes, declared: the envelope
     * keys sort (data before msg, msg_id) and, for get_state only, so do the
     * io ids inside data - buildJsonStates()/buildQuery() were already
     * alphabetical, Params being a std::map. Pinned by
     * core/JsonApiStateWireBytes_test.
     *
     * E4.1s: the no-data path above no longer has a jansson overload to lean
     * on; it goes through sendJsonNoData(), which keeps the OMISSION and
     * nothing else. The document is unchanged, byte for byte.
     */
    buildJsonState(iolist, [=](Json jret)
    {
        sendJson("get_state", jret, client_id);
    });
}

void JsonApiHandlerWS::processGetStates(const Params &jsonReq, const string &client_id)
{
    buildJsonStates(jsonReq, [=](Json jret)
    {
        sendJson("get_states", jret, client_id);
    });
}

void JsonApiHandlerWS::processQuery(const Params &jsonReq, const string &client_id)
{
    buildQuery(jsonReq, [=](Json jret)
    {
        sendJson("query", jret, client_id);
    });
}

void JsonApiHandlerWS::processGetParam(const Params &jsonReq, const string &client_id)
{
    sendJson("get_param", buildJsonGetParam(jsonReq), client_id);
}

void JsonApiHandlerWS::processSetParam(const Params &jsonReq, const string &client_id)
{
    sendJson("set_param", buildJsonSetParam(jsonReq), client_id);
}

void JsonApiHandlerWS::processDelParam(const Params &jsonReq, const string &client_id)
{
    sendJson("del_param", buildJsonDelParam(jsonReq), client_id);
}

void JsonApiHandlerWS::processGetIO(const Json *jdata, const string &client_id)
{
    if (!jdata)
    {
        sendJsonNoData("get_io", client_id);
        return;
    }

    vector<string> iolist;
    collectStringItems(*jdata, iolist);

    sendJson("get_io", buildJsonGetIO(iolist), client_id);
}

void JsonApiHandlerWS::processSetState(Params &jsonReq, const string &client_id)
{
    bool res = decodeSetState(jsonReq);

    if (!client_id.empty())
    {
        //E4.1n: one ASCII pair, byte for byte the same payload; what moves is
        //the ENVELOPE, which sorts like every other nlohmann answer.
        sendJson("set_state", Json{{ "success", res?"true":"false" }}, client_id);
    }
}

void JsonApiHandlerWS::processGetPlaylist(Params &jsonReq, const string &client_id)
{
    /* E4.1p: decodeGetPlaylist() answers a Json now, so this resolves to the
     * nlohmann overload of sendJson() (:75) instead of the jansson one (:69) -
     * the seam was already there and in service. Declared consequence on the
     * bytes: the envelope sorts (data before msg), the three keys of the
     * answer sort with it, and the escaping moves to nlohmann's lowercase
     * hexadecimal. Both stay pure ASCII. Pinned by
     * tests/core/JsonApiAudioWireBytes_test.cpp.
     */
    decodeGetPlaylist(jsonReq, [=](const Json &jret)
    {
        sendJson("get_playlist", jret, client_id);
    });
}

/* E4.1s. ONE DOCUMENT - the json_t* twin that carried the DISPATCH is gone
 * with the request parse it came from. jsonStringGet() reproduces
 * jansson_string_get()'s contract on the same document: the DEFAULT on an
 * absent member, on a member that is not a JSON string, and on a root that is
 * not an object.
 */
void JsonApiHandlerWS::processAudio(const Json &jdataDoc, const string &client_id)
{
    string msg = jsonStringGet(jdataDoc, "audio_action");
    if (msg == "get_playlist_size")
        audioGetPlaylistSize(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio", jret, client_id);
        });
    else if (msg == "get_time")
        audioGetTime(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio", jret, client_id);
        });
    else if (msg == "get_playlist_item")
        audioGetPlaylistItem(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio", jret, client_id);
        });
    else if (msg == "get_cover_url")
        audioGetCoverInfo(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio", jret, client_id);
        });
    else
        sendJson("audio", {{"error", "unkown audio_action" }} , client_id);
}

/* E4.1q. THE DOCUMENT IS HOISTED, NOT ADDED - see the twin comment on
 * JsonApiHandlerHttp::processAudioDb(). `jdataDoc` is the "data" member of
 * processApi()'s existing nlohmann parse, and the DISPATCH migrates with the
 * sixteen methods it reaches, so this transport no longer needs a json_t* at
 * all for audio_db.
 */
void JsonApiHandlerWS::processAudioDb(const Json &jdataDoc, const string &client_id)
{
    string msg = jsonStringGet(jdataDoc, "audio_action");
    if (msg == "get_album")
        audioDbGetAlbums(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_stats")
        audioGetDbStats(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_artist_album")
        audioDbGetAlbumArtistItem(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_year_albums")
        audioDbGetYearAlbums(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_genre_artists")
        audioDbGetGenreArtists(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_album_titles")
        audioDbGetAlbumTitles(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_playlist_titles")
        audioDbGetPlaylistTitles(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_artists")
        audioDbGetArtists(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_years")
        audioDbGetYears(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_genres")
        audioDbGetGenres(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_playlists")
        audioDbGetPlaylists(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_music_folder")
        audioDbGetMusicFolder(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_search")
        audioDbGetSearch(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_radios")
        audioDbGetRadios(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_track_infos")
        audioDbGetTrackInfos(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_radio_items")
        audioDbGetRadioItems(jdataDoc, [=](const Json &jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else
        sendJson("audio_db", {{"error", "unkown audio_action" }} , client_id);
}

void JsonApiHandlerWS::processGetTimerange(const Params &jsonReq, const string &client_id)
{
    sendJson("get_timerange", buildJsonGetTimerange(jsonReq), client_id);
}

void JsonApiHandlerWS::processSetTimerange(const Json &jdata, const string &client_id)
{
    sendJson("set_timerange", buildJsonSetTimerange(jdata), client_id);
}

/* E4.1r. Same bascule as the HTTP twin, and the same source: jsonDataDoc, the
 * "data" member E4.1o already lifted out of the parse E4.1m runs on every
 * message. It is Json::object() when "data" is absent, which is what
 * json_object_get(jroot, "data") == NULL used to mean to every reader below.
 */
void JsonApiHandlerWS::processAutoscenario(const Json &jdata, const string &client_id)
{
    string msg = jsonStringGet(jdata, "type");
    if (msg == "list")
        sendJson("autoscenario", buildAutoscenarioList(jdata), client_id);
    else if (msg == "get")
        sendJson("autoscenario", buildAutoscenarioGet(jdata), client_id);
    else if (msg == "create")
        sendJson("autoscenario", buildAutoscenarioCreate(jdata), client_id);
    else if (msg == "delete")
        sendJson("autoscenario", buildAutoscenarioDelete(jdata), client_id);
    else if (msg == "modify")
        sendJson("autoscenario", buildAutoscenarioModify(jdata), client_id);
    else if (msg == "add_schedule")
        sendJson("autoscenario", buildAutoscenarioAddSchedule(jdata), client_id);
    else if (msg == "del_schedule")
        sendJson("autoscenario", buildAutoscenarioDelSchedule(jdata), client_id);
    //T3.18: manual re-enable of a scenario disabled by a missing IO. It can
    //REFUSE, which is why it is a command and not a set_param.
    else if (msg == "reenable")
        sendJson("autoscenario", buildAutoscenarioReenable(jdata), client_id);
    else
        sendJson("autoscenario", {{ "error", "unknown autoscenario type" }}, client_id);
}

void JsonApiHandlerWS::processEventLog(const Params &jsonReq, const string &client_id)
{
    buildJsonEventLog(jsonReq, [=](Json &j)
    {
        sendJson("eventlog", j, client_id);
    });
}

void JsonApiHandlerWS::processRegisterPush(const Params &jsonReq, const string &client_id)
{
    bool r = registerPushToken(jsonReq);

    if (!client_id.empty())
    {
        Json ret = {{ "success", r?"true":"false" }};
        sendJson("register_push", ret, client_id);
    }
}

void JsonApiHandlerWS::processSettings(const Params &jsonReq, const string &client_id)
{
    if (jsonReq["action"] == "change_cred")
    {
        bool ok = changeCredentials(jsonReq["old_user"], jsonReq["old_pw"], jsonReq["new_user"], jsonReq["new_pw"]);
        if (ok)
            loggedin = false; //user must login again with new password

        Json ret = {{ "action", "change_cred" },
                    { "success", ok?"true":"false" }};
        sendJson("settings", ret, client_id);
    }
}

void JsonApiHandlerWS::processLoginService(const Params &jsonData, const string &client_id)
{
    // S2: service account login for the MCP sidecar. Uses the mcp_service_token
    // stored in local_config.xml. Grants a restricted session (serviceScope=true)
    // that cannot modify configuration or access sensitive actions.
    const string &expected = McpServerManager::Instance().getServiceToken();
    const string &received = jsonData["token"];

    const string ip = clientIp();
    const double now = Utils::getMainLoopTime();

    if (LoginThrottle::isBlocked(ip, now))
    {
        cWarningDom("mcp") << "login_service: too many failed logins from " << ip;
        sendJson("login_service", {{ "success", "false" }, { "error", "invalid token" }}, client_id);
        closeConnection.emit(WebSocketFrame::CloseCodeNormal, "login_service failed");
        return;
    }

    //An unset token never grants access, whatever the client sends
    if (expected.empty() || !secureCompare(expected, received))
    {
        LoginThrottle::registerFailure(ip, now);

        cWarningDom("mcp") << "login_service: invalid service token";
        sendJson("login_service", {{ "success", "false" }, { "error", "invalid token" }}, client_id);
        closeConnection.emit(WebSocketFrame::CloseCodeNormal, "login_service failed");
        return;
    }

    LoginThrottle::registerSuccess(ip);

    loggedin = true;
    serviceScope = true;
    cInfoDom("mcp") << "login_service: MCP sidecar authenticated (service scope)";
    sendJson("login_service", {{ "success", "true" }}, client_id);
}
