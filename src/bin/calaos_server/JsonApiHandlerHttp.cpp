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
#include "JsonApiHandlerHttp.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "PollListenner.h"
#include "Prefix.h"
#include "CalaosConfig.h"
#include "AudioPlayer.h"
#include "InPlageHoraire.h"
#include "HttpCodes.h"
#include "Timer.h"
#include "HttpClient.h"
#include "libuvw.h"
#include "McpServerManager.h"

namespace
{

/* E4.1q. jansson_string_get()'s contract, kept BY HAND for a nlohmann document
 * so that processAudioDb() can dispatch on the parse processApi() ALREADY did:
 * the DEFAULT on an absent member, on a member that is not a JSON string, and
 * on a root that is not an object (json_object_get(NULL, k) answered NULL).
 * `j["k"].get<string>()` does none of that - it throws.
 *
 * Identical, deliberately, to the copy JsonApi.cpp carries (E4.1o) and to the
 * four driver wires: this is a contract E4.1x will fold once
 * Jansson_Addition.h goes away, not a helper to improve here.
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
 * std::map, so the destination is sorted either way, and jansson had already
 * collapsed duplicate keys at PARSE time (last one wins, measured identical in
 * both libraries).
 *
 * Identical, deliberately, to the copy JsonApi.cpp carries (E4.1o), to the one
 * in JsonApiHandlerWS.cpp and to ScriptWire::decodeObject(): this is a
 * contract E4.1x folds once Jansson_Addition.h goes away, not a helper to
 * improve here.
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
 *
 *      json_t *jio = json_object_get(jroot, "items");
 *      if (jio && json_is_array(jio))
 *          json_array_foreach(jio, idx, value)
 *              if (json_is_string(value)) iolist.push_back(...);
 *
 * Every guard is kept: an absent "items", an "items" that is not an array, and
 * a non-string element are all SILENTLY SKIPPED and never an error. Frozen,
 * not tidied.
 */
inline void collectStringItems(const Json &jroot, vector<string> &iolist)
{
    if (!jroot.is_object())
        return;

    const Json::const_iterator it = jroot.find("items");
    if (it == jroot.cend() || !it->is_array())
        return;

    for (const Json &value: *it)
    {
        if (value.is_string())
            iolist.push_back(value.get<std::string>());
    }
}

} //namespace

JsonApiHandlerHttp::JsonApiHandlerHttp(HttpClient *client):
    JsonApi(client)
{
    tempfname = Utils::getTmpFilename("jpg", "_json_temp");
}

JsonApiHandlerHttp::~JsonApiHandlerHttp()
{
    //pid > 0 guard: a handle whose spawn failed keeps pid 0, and killing
    //pid 0 would SIGTERM our whole process group (see ~ExternProcServer)
    if (exe_thumb && exe_thumb->referenced() && exe_thumb->pid() > 0)
    {
        exe_thumb->kill(SIGTERM);
        exe_thumb->close();
    }

    releaseCameraDl();
    FileUtils::unlink(tempfname);
}

string JsonApiHandlerHttp::clientIp() const
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

void JsonApiHandlerHttp::sendLoginFailed()
{
    Params headers;
    headers.Add("Connection", "close");
    headers.Add("Content-Type", "text/html");
    string res = httpClient->buildHttpResponse(HTTP_400, headers, HTTP_400_BODY);
    sendData.emit(res);
    closeConnection.emit(0, string());
}

void JsonApiHandlerHttp::processApi(const string &data, const Params &paramsGET)
{
    jsonParam.clear();

    /* ⛔⭐⭐ E4.1s. THE REQUEST PARSE, and the ONE change of this ticket that is
     * not about formatting: json_loads() is gone and Json::parse() decides what
     * the API accepts. From E4.1m to E4.1r this parse ran ALONGSIDE jansson's,
     * for the redacted log line; it is now the only one, and there is no second
     * parse anywhere - the document below is handed down to every reader.
     *
     * THE TWO PARSERS DO NOT DRAW THE SAME LINE. Measured against the real
     * jansson and this repository's json.hpp (3.11.3), pinned case by case in
     * tests/core/JsonApiDispatchWireBytes_test.cpp, and DECLARED in
     * docs/refactoring/RELEASE_NOTES.md:
     *
     *   WIDER, three inputs that used to be refused and are now served:
     *     - an ESCAPED NUL, "\u0000", in a value or in a key. jansson refused
     *       it outright ("\u0000 is not allowed without JSON_ALLOW_NUL"). ⭐
     *       THIS IS THE ONE THAT MATTERS: it is a path that did not exist
     *       before this line changed, and Scenario::toJson() - IO/Scenario.cpp,
     *       EXCLUDED by decision Q5 - still TRUNCATES a value at the first zero
     *       byte. The truncation is E4.6d's to fix; knowing it and writing it
     *       down is this ticket's part, and the case that pins it lives in
     *       tests/core/JsonApiScenarioWireBytes_test.cpp.
     *     - an integer beyond int64: "too big integer" against a double.
     *     - a nesting depth above 2048: jansson caps at JSON_PARSER_MAX_DEPTH,
     *       nlohmann has no limit. NOT a crash - 100000 levels parse and
     *       destruct without a stack overflow, json.hpp destroys iteratively -
     *       and the body is still bounded by the HTTP request size.
     *
     *   UNCHANGED, the locks that must NOT move: invalid UTF-8 and a lone
     *   surrogate are refused by BOTH parsers, and so are a real-number
     *   overflow and trailing garbage. A raw NUL still ends the body on both
     *   sides - json.hpp lists '\0' next to eof() in its lexer, exactly as
     *   json_loads(data.c_str()) stopped at the C string.
     *
     * The document stays `null` on the GET-parameter fallback, which is exactly
     * the branch where set_timerange answers 400 before reaching the dispatch.
     */
    Json jsonRootDoc = Json::parse(data, nullptr, false);

    //Same test as json_is_object() on the jansson tree, and it must STAY an
    //object test: a valid JSON array is not a request either.
    const bool hasJsonBody = jsonRootDoc.is_object();

    if (!hasJsonBody)
    {
        //The parser's own message is gone with the parser: Json::parse() in its
        //non throwing form does not produce one. This is a debug line.
        cDebugDom("network") << "Error loading json. No JSON, trying with GET parameters.";

        jsonParam = paramsGET;
        jsonRootDoc = Json();
    }
    else
    {
        cDebugDom("network") << dumpJsonRedacted(jsonRootDoc);

        //decode the json root object into jsonParam
        decodeJsonObject(jsonRootDoc, jsonParam);
    }

    const string ip = clientIp();
    const double now = Utils::getMainLoopTime();

    if (LoginThrottle::isBlocked(ip, now))
    {
        cWarningDom("network") << "Too many failed logins from " << ip << ", request refused";

        sendLoginFailed();

        return;
    }

    //check if username/password matches
    if (!checkCredentials(jsonParam["cn_user"], jsonParam["cn_pass"]))
    {
        LoginThrottle::registerFailure(ip, now);

        cDebugDom("network") << "Login failed!";

        sendLoginFailed();

        return;
    }

    LoginThrottle::registerSuccess(ip);

    //check action now
    if (jsonParam["action"] == "get_home")
        processGetHome();
    else if (jsonParam["action"] == "get_state")
        processGetState(jsonRootDoc);
    else if (jsonParam["action"] == "get_io")
        processGetIO(jsonRootDoc);
    else if (jsonParam["action"] == "get_states")
        processGetStates();
    else if (jsonParam["action"] == "query")
        processQuery();
    else if (jsonParam["action"] == "get_param")
        processGetParam();
    else if (jsonParam["action"] == "set_param")
        processSetParam();
    else if (jsonParam["action"] == "del_param")
        processDelParam();
    else if (jsonParam["action"] == "set_state")
        processSetState();
    else if (jsonParam["action"] == "get_playlist")
        processGetPlaylist();
    else if (jsonParam["action"] == "poll_listen")
        processPolling();
    else if (jsonParam["action"] == "get_cover")
        processGetCover();
    else if (jsonParam["action"] == "get_camera_pic")
        processGetCameraPic();
    else if (jsonParam["action"] == "get_timerange")
        processGetTimerange();
    else if (jsonParam["action"] == "camera")
        processCamera();
    else if (jsonParam["action"] == "eventlog")
        processEventLog();
    else if (jsonParam["action"] == "event_picture")
        processEventPicture();
    else if (jsonParam["action"] == "register_push")
        processRegisterPush();
    else if (jsonParam["action"] == "get_mcp_info")
    {
        // Return MCP connection info (URL path + bearer token) for the
        // authenticated admin user — never exposed without credentials.
        //E4.1s: DECLARED byte delta - the three keys sorted (url_path, token,
        //hint -> hint, token, url_path). Pure ASCII on both sides, and no
        //value moves. Pinned by core/JsonApiDispatchWireBytes_test.
        const string &token = McpServerManager::Instance().bearerToken();
        sendJson(Json{{ "url_path", "/mcp" },
                      { "token", token },
                      { "hint", "Use token as Bearer in Authorization header. "
                                "Append /mcp to your Calaos HTTPS base URL." }});
    }
    else
    {
        if (!hasJsonBody)
        {
            Params headers;
            headers.Add("Connection", "close");
            headers.Add("Content-Type", "text/html");
            string res = httpClient->buildHttpResponse(HTTP_400, headers, HTTP_400_BODY);
            sendData.emit(res);
            closeConnection.emit(0, string());

            return;
        }

        if (jsonParam["action"] == "config")
            processConfig(jsonRootDoc);
        else if (jsonParam["action"] == "audio")
            processAudio(jsonRootDoc);
        else if (jsonParam["action"] == "audio_db")
            processAudioDb(jsonRootDoc);
        else if (jsonParam["action"] == "set_timerange")
            processSetTimerange(jsonRootDoc);
        else if (jsonParam["action"] == "autoscenario")
            processAutoscenario(jsonRootDoc);
        else
            sendJson({{ "error", "unknown action" }});
    }
}

/* ⭐ E4.1s. sendJson(json_t *) IS GONE, and with it the HTTP 500 branch it
 * carried: json_dumps() answered NULL on a tree holding invalid UTF-8, and
 * THAT was the only way this transport ever produced a 500. It cannot happen
 * any more - error_handler_t::replace turns the bad bytes into U+FFFD and the
 * dump always succeeds - so the branch is not "removed", it is UNREACHABLE and
 * would have been dead code. E4.0e measured that it was already all but dead:
 * jansson refused the bytes at CONSTRUCTION, so the pair vanished long before
 * the dump and the client got a 200 with a mutilated payload.
 */
void JsonApiHandlerHttp::sendJson(const Json &json)
{
    /* E4.1b, the three emission invariants of the epic, and this is now the
     * ONLY emitter of this transport.
     * ensure_ascii = true: this wire has ALWAYS been ASCII only (the jansson
     * overload dumped with JSON_ENSURE_ASCII). Only the case of the
     * hexadecimal differs from jansson's (\u00e9 against \u00E9) - and U+007F,
     * which jansson left raw and nlohmann escapes, which is why the payload
     * can grow and Content-Length follows it below.
     * error_handler_t::replace: dump() THROWS type_error.316 on invalid UTF-8
     * in the tree, and nothing catches it above this line - that is
     * std::terminate on a live connection. Reachable today: eventlog reflects
     * io_id/io_state/pic_uid straight out of sqlite (HistLogger.cpp:82-103),
     * EventManager.cpp:93 puts an IO state there without any JSON parser on the
     * way in, and since this ticket processConfig() reflects the RAW BYTES of
     * io.xml / rules.xml / local_config.xml, which are user files and
     * guarantee nothing. NOT a try/catch: the handler treats the cause.
     */
    string data = json.dump(-1, ' ', true, Json::error_handler_t::replace);

    Params headers;
    headers.Add("Connection", "Close");
    headers.Add("Cache-Control", "no-cache, must-revalidate");
    headers.Add("Expires", "Mon, 26 Jul 1997 05:00:00 GMT");
    headers.Add("Content-Type", "application/json");
    headers.Add("Content-Length", Utils::to_string(data.size()));
    string res = httpClient->buildHttpResponse(HTTP_200, headers, data);
    sendData.emit(res);
}

void JsonApiHandlerHttp::processGetHome()
{
    //E4.1m: same bascule as JsonApiHandlerWS::processGetHome(), same deltas,
    //and here Content-Length follows the body because sendJson(const Json &)
    //computes it from the dumped string (:272).
    Json jret = {{ "home", buildJsonHome() },
                 { "cameras", buildJsonCameras() },
                 { "audio", buildJsonAudio() }};

    sendJson(jret);
}

void JsonApiHandlerHttp::processGetState(const Json &jroot)
{
    vector<string> iolist;

    //E4.1s: `is_object()` is the transcription of the old `if (jroot)` - the
    //pointer was non null exactly when the body had parsed as an object. The
    //comma-separated GET form is the other branch, untouched.
    if (jroot.is_object())
    {
        collectStringItems(jroot, iolist);
    }
    else
    {
        if (jsonParam.Exists("items"))
        {
            Utils::split(jsonParam["items"], iolist, ",");
        }
    }

    //E4.1n: same seam as on the websocket - the builders answer a Json, so
    //these three resolve to sendJson(const Json &) (:267), which already
    //carries the three emission invariants of E4.1b.
    buildJsonState(iolist, [=](Json jret)
    {
        sendJson(jret);
    });
}

void JsonApiHandlerHttp::processGetStates()
{
    buildJsonStates(jsonParam, [=](Json jret)
    {
        sendJson(jret);
    });
}

void JsonApiHandlerHttp::processQuery()
{
    buildQuery(jsonParam, [=](Json jret)
    {
        sendJson(jret);
    });
}

void JsonApiHandlerHttp::processGetParam()
{
    sendJson(buildJsonGetParam(jsonParam));
}

void JsonApiHandlerHttp::processSetParam()
{
    sendJson(buildJsonSetParam(jsonParam));
}

void JsonApiHandlerHttp::processDelParam()
{
    sendJson(buildJsonDelParam(jsonParam));
}

void JsonApiHandlerHttp::processGetIO(const Json &jroot)
{
    vector<string> iolist;

    if (jroot.is_object())
    {
        collectStringItems(jroot, iolist);
    }
    else
    {
        if (jsonParam.Exists("items"))
        {
            Utils::split(jsonParam["items"], iolist, ",");
        }
    }

    sendJson(buildJsonGetIO(iolist));
}

void JsonApiHandlerHttp::processSetState()
{
    //E4.1n: one ASCII pair, and it does not move by one byte on this transport
    //(there is no envelope on HTTP). Only the emitter changes.
    sendJson(Json{{ "success", decodeSetState(jsonParam)?"true":"false" }});
}

void JsonApiHandlerHttp::processGetPlaylist()
{
    /* E4.1p: decodeGetPlaylist() answers a Json now, so this resolves to the
     * nlohmann overload of sendJson() (:259) instead of the jansson one - the
     * seam was already there and in service, no adapter was needed and none
     * was written. Declared consequence on the bytes: the three keys of the
     * answer sort (current_track,count,items -> count,current_track,items) and
     * the escaping moves from jansson's UPPERCASE \u00E9 to nlohmann's
     * lowercase \u00e9. Both stay pure ASCII. Pinned by
     * tests/core/JsonApiAudioWireBytes_test.cpp.
     */
    decodeGetPlaylist(jsonParam, [=](const Json &jret)
    {
        sendJson(jret);
    });
}

void JsonApiHandlerHttp::processPolling()
{
    /* E4.1l: poll_listen is the HTTP delivery of CalaosEvent::toJson(), so it
     * follows the constructor onto the nlohmann emitter that E4.1b already
     * hardened - sendJson(const Json &), :259, with the three invariants.
     * No transitional adapter in this direction, and none is to be written:
     * the overload exists and is in service.
     * The whole function moves, not only the toJson() line: appending a Json
     * to a json_t array is not a thing, and the alternative would have been to
     * transcode a document this handler is about to serialize anyway.
     * DELTA, declared: the answer's own members now travel sorted (events
     * before success) and so do the four members of each event object. Every
     * value stays a JSON STRING, "success" included - never a JSON boolean.
     */
    Json jret = Json::object();

    if (jsonParam["type"] == "register")
    {
        string uuid = PollListenner::Instance().Register();
        jret["uuid"] = uuid;
    }
    else if (jsonParam["type"] == "unregister")
    {
        string uuid = jsonParam["uuid"];
        bool success = PollListenner::Instance().Unregister(uuid);
        jret["success"] = success?"true":"false";
    }
    else if (jsonParam["type"] == "get")
    {
        string uuid = jsonParam["uuid"];
        list<CalaosEvent> events;

        bool res = PollListenner::Instance().GetEvents(uuid, events);
        if (!res)
            jret["success"] = "false";
        else
        {
            Json jev = Json::array();

            for (auto i = events.cbegin();i != events.cend();i++)
            {
                jev.emplace_back(i->toJson());
            }

            jret["success"] = "true";
            jret["events"] = jev;
        }

    }

    sendJson(jret);
}

void JsonApiHandlerHttp::processGetCover()
{
    AudioPlayer *player = dynamic_cast<AudioPlayer *>(ListeRoom::Instance().get_io(jsonParam["id"]));
    if (!player)
    {
        //E4.1s: DECLARED byte delta on the four refusals of the two binary
        //operations - the pair sorts (success, error_str -> error_str,
        //success). Both values stay JSON STRINGS, never JSON booleans (Q2 of
        //E4.6). Pinned by core/JsonApiDispatchWireBytes_test.
        sendJson(Json{{ "success", "false" }, { "error_str", "id not set" }});
        return;
    }

    string width;
    if (jsonParam.Exists("width"))
        width = jsonParam["width"];

    string rotate;
    if (jsonParam.Exists("rotate"))
        rotate = jsonParam["rotate"];

    if (!checkPictureParams(width, rotate))
    {
        sendJson(Json{{ "success", "false" },
                      { "error_str", "invalid width or rotate parameter" }});
        return;
    }

    std::weak_ptr<bool> alive = handlerAlive;

    player->get_album_cover([this, alive, width, rotate](AudioPlayerData data)
    {
        if (alive.expired())
            return;

        //do not start another exe if one is running already
        if (data.svalue == "" || exe_thumb_running)
        {
            sendJson(Json{{ "success", "false" },
                          { "error_str", "unable to get url" }});
            return;
        }

        exe_thumb = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
        exe_thumb->once<uvw::ExitEvent>([this](const uvw::ExitEvent &ev, auto &h)
        {
            h.close();
            exe_thumb_running = false;
            this->exeFinished(ev.status);
        });
        exe_thumb->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, auto &h)
        {
            cDebugDom("process") << "Process error: " << ev.what();
            h.close();
            exe_thumb_running = false;
            this->exeFinished(1);
        });

        Utils::CStrArray arr(buildPictureCommand(data.svalue, width, rotate));
        cInfoDom("network") << "Executing command: " << arr.toString();
        exe_thumb->spawn(arr.at(0), arr.data());
        exe_thumb_running = true;
    });
}

void JsonApiHandlerHttp::processGetCameraPic()
{
    IPCam *camera = dynamic_cast<IPCam *>(ListeRoom::Instance().get_io(jsonParam["id"]));
    if (!camera || exe_thumb_running)
    {
        sendJson(Json{{ "success", "false" }, { "error_str", "id not set" }});
        return;
    }

    string width;
    if (jsonParam.Exists("width"))
        width = jsonParam["width"];

    string rotate;
    if (jsonParam.Exists("rotate"))
        rotate = jsonParam["rotate"];

    if (!checkPictureParams(width, rotate))
    {
        sendJson(Json{{ "success", "false" },
                      { "error_str", "invalid width or rotate parameter" }});
        return;
    }

    exe_thumb = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
    exe_thumb->once<uvw::ExitEvent>([this](const uvw::ExitEvent &ev, auto &h)
    {
        h.close();
        exe_thumb_running = false;
        this->exeFinished(ev.status);
    });
    exe_thumb->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, auto &h)
    {
        cDebugDom("process") << "Process error: " << ev.what();
        h.close();
        exe_thumb_running = false;
        this->exeFinished(1);
    });

    Utils::CStrArray arr(buildPictureCommand(camera->getPictureUrl(), width, rotate));
    cInfoDom("network") << "Executing command: " << arr.toString();
    exe_thumb->spawn(arr.at(0), arr.data());
    exe_thumb_running = true;
}

bool JsonApiHandlerHttp::checkPictureParams(const string &width, const string &rotate)
{
    return (width.empty() || isValidIntParam(width, 1, 10000)) &&
           (rotate.empty() || isValidIntParam(rotate, -360, 360));
}

vector<string> JsonApiHandlerHttp::buildPictureCommand(const string &url, const string &width, const string &rotate)
{
    //The arguments are given to spawn() one by one, never through a string
    //split on spaces: an url or a parameter cannot inject an extra argument
    vector<string> args =
    { Prefix::Instance().binDirectoryGet() + "/calaos_picture", url, tempfname };

    if (!width.empty())
    {
        args.push_back("-w");
        args.push_back(width);
    }

    if (!rotate.empty())
    {
        args.push_back("-r");
        args.push_back(rotate);
    }

    return args;
}

void JsonApiHandlerHttp::exeFinished(int exit_code)
{
    if (exit_code != 0)
    {
        sendJson(Json{{ "success", "false" },
                      { "error_str", "unable to load data from url" }});
        return;
    }

    //E4.1s: DECLARED byte delta - the four keys sort (success, contenttype,
    //encoding, data -> contenttype, data, encoding, success). The base64 body
    //is ASCII by construction, so nothing else moves on this payload.
    sendJson(Json{{ "success", "true" },
                  { "contenttype", "image/jpeg" },
                  { "encoding", "base64" },
                  { "data", Utils::getFileContentBase64(tempfname.c_str()) }});
}

/* ⭐⭐ E4.1s. THE BIGGEST PAYLOAD OF THE API, AND THE ONE EMITTER OF THIS
 * PERIMETER THAT CARRIES BYTES A CLIENT CAN INFLUENCE.
 *
 * "get" reflects the RAW TEXT of io.xml, rules.xml and local_config.xml. Those
 * are USER FILES: nothing on the way in guarantees they hold valid UTF-8, and
 * an IO name is enough to put any byte in them. Three consequences, all
 * DECLARED and all pinned by core/JsonApiDispatchWireBytes_test:
 *
 *   1. INVALID UTF-8 STOPS BEING A SILENT AMPUTATION. json_string() answered
 *      NULL on the file content, json_object_set_new() then returned -1, and
 *      NEITHER return code was tested here: the client got a 200 whose
 *      "config_files" was MISSING io.xml entirely - indistinguishable from a
 *      configuration that has none. error_handler_t::replace now writes one
 *      U+FFFD per bad byte and the key survives, mangled and VISIBLE.
 *      ⛔ This is also the reason error_handler_t::replace is not optional on
 *      sendJson(): a bare dump() here is type_error.316, uncaught, on a live
 *      connection - std::terminate on a payload the client can shape.
 *   2. The three file names SORT (io.xml, rules.xml, local_config.xml ->
 *      io.xml, local_config.xml, rules.xml) and the escaping moves to form 3.
 *   3. A U+007F inside a configuration file is now ESCAPED, so the payload
 *      GROWS and Content-Length follows it.
 *
 * ⛔ THE UPLOAD WHITELIST IS AN INVARIANT OF OPERATION (user decision,
 * 2026-08-24) and is NOT touched: the three names stay hard coded and the
 * comparison stays a comparison of whole strings, which is what keeps an
 * embedded NUL - now that one can reach this far - from smuggling a fourth
 * name past it.
 *
 * The iteration order of "put" moves from insertion to sorted. It has no
 * observable effect: each name writes a different file, the whitelist test is
 * per key, and `ret` is the AND of independent outcomes.
 */
void JsonApiHandlerHttp::processConfig(const Json &jroot)
{
    Json jret = Json::object();

    if (jsonParam["type"] == "get")
    {
        Config::Instance().SaveConfigIO();
        Config::Instance().SaveConfigRule();

        //The three names stay SPELLED OUT, as they were: they are the wire
        //contract, and IO_CONFIG happens to hold the same text only because
        //nothing has ever moved the file. Strict transcription, not tidying.
        Json jfiles = Json::object();
        jfiles["io.xml"] = Utils::getFileContent(Utils::getConfigFile(IO_CONFIG).c_str());
        jfiles["rules.xml"] = Utils::getFileContent(Utils::getConfigFile(RULES_CONFIG).c_str());
        jfiles["local_config.xml"] = Utils::getFileContent(Utils::getConfigFile(LOCAL_CONFIG).c_str());

        jret["config_files"] = jfiles;
        jret["success"] = "true";
    }
    else if (jsonParam["type"] == "put")
    {
        bool ret = true;

        /* json_object_get(jroot, "config_files") answered NULL on an absent
         * member and on a non object root, and json_is_object() then decided.
         * Transcribed with a find() so the "absent" and "present but not an
         * object" paths stay the SAME path, as they were.
         */
        Json jfiles;
        if (jroot.is_object())
        {
            const Json::const_iterator it = jroot.find("config_files");
            if (it != jroot.cend())
                jfiles = *it;
        }

        if (jfiles.is_object())
        {
            //Do a backup before overwriting new files
            Config::Instance().BackupFiles();

            for (Json::const_iterator it = jfiles.cbegin(); it != jfiles.cend(); ++it)
            {
                const string skey = it.key();

                if (it.value().is_string())
                {
                    if (skey != IO_CONFIG &&
                        skey != RULES_CONFIG &&
                        skey != LOCAL_CONFIG)
                    {
                        cErrorDom("network") << "Error, file " << skey << " is not a valid config filename";
                        ret = false;
                        continue;
                    }

                    string filecontent = it.value().get<std::string>();

                    if (!Utils::strStartsWith(filecontent, "<?xml"))
                    {
                        cErrorDom("network") << "Error, file content for " << skey << " is not XML, skipping...";
                        cDebugDom("network") << filecontent;
                        continue;
                    }

                    //c_str() and not skey: getConfigFile() takes a const
                    //char *, exactly as it did with jansson's key. The
                    //whitelist above has already reduced skey to one of three
                    //literals, so a NUL smuggled into a key cannot reach here.
                    ofstream ofs(Utils::getConfigFile(skey.c_str()), ios::out | ios::trunc);

                    if (ofs.is_open())
                    {
                        ofs << filecontent;
                        ofs.close();
                    }
                    else
                    {
                        cErrorDom("network") << "Error, key " << skey << " is not a string";
                        ret = false;
                    }
                }
                else
                {
                    cErrorDom("network") << "Error, key " << skey << " is not a string";
                    ret = false;
                }
            }
        }
        else
        {
            ret = false;
            cErrorDom("network") << "Error, wrong query";
        }

        jret["success"] = ret?"true":"false";

        if (ret)
            httpClient->setNeedRestart(true);
    }
    else
    {
        jret["success"] = "false";
    }

    sendJson(jret);
}

/* E4.1p. TWO DOCUMENTS, AND THE SECOND ONE IS HOISTED, NOT ADDED.
 *
 * `jdataDoc` is processApi()'s own nlohmann parse of the very same bytes
 * (E4.1m parsed them for the redacted log line, E4.1o gave that parse a name).
 * Zero extra parse, no json_dumps + Json::parse bridge, and none is to be
 * written. The DISPATCH itself stays jansson - reading `audio_action` is the
 * dispatcher's business and its migration is E4.1s, exactly as E4.1o left the
 * set_timerange dispatch alone.
 *
 * ⭐ E4.1s TOOK IT. The json_t* twin is gone with the request parse it came
 * from, and jsonStringGet() reproduces jansson_string_get()'s contract on the
 * SAME document: the DEFAULT on an absent member, on a member that is not a
 * JSON string, and on a root that is not an object.
 */
void JsonApiHandlerHttp::processAudio(const Json &jdataDoc)
{
    string msg = jsonStringGet(jdataDoc, "audio_action");
    if (msg == "get_playlist_size")
        audioGetPlaylistSize(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_time")
        audioGetTime(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_playlist_item")
        audioGetPlaylistItem(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_cover_url")
        audioGetCoverInfo(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_cover")
    {
        /* NOT migrated, deliberately: this branch answers an IMAGE, not JSON,
         * and its two error payloads are hand built here and covered by no
         * case of the suite (measured: no test in tests/ mentions "unable to
         * get url"). Moving them would sort their keys with nothing to catch a
         * mistake. They go with the dispatch, in E4.1s.
         *
         * ⚠️ E4.1q: the ONE line that had to move here anyway. This was the
         * sixteenth caller of the transitional jansson overload of
         * getAudioPlayer() that E4.1p left for that ticket.
         *
         * ⭐ E4.1s finishes it: the three payloads below cross to the nlohmann
         * emitter with the rest of the file. The refusal is ONE pair, so no
         * order can move and no byte does - it is the witness of the deletion
         * of the last jansson_from_params() call site of this transport.
         */
        string err;
        AudioPlayer *player = getAudioPlayer(jdataDoc, err);

        if (!err.empty())
        {
            Params p = {{"error", err }};
            sendJson(p.toNJson());
            return;
        }

        std::weak_ptr<bool> alive = handlerAlive;

        player->get_album_cover([this, alive](AudioPlayerData data)
        {
            if (alive.expired())
                return;

            if (data.svalue == "" || exe_thumb_running)
            {
                sendJson(Json{{ "success", "false" },
                              { "error_str", "unable to get url" }});
                return;
            }

            exe_thumb = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
            exe_thumb->once<uvw::ExitEvent>([this](const uvw::ExitEvent &ev, auto &h)
            {
                h.close();
                exe_thumb_running = false;
                if (ev.status != 0)
                {
                    this->sendJson(Json{{ "success", "false" },
                                        { "error_str", "unable to load data from url" }});
                    return;
                }

                Params headers;
                headers.Add("Connection", "close");
                headers.Add("Content-Type", "image/jpeg");
                string res = httpClient->buildHttpResponse(HTTP_200, headers, Utils::getFileContent(tempfname.c_str()));
                sendData.emit(res);

            });
            exe_thumb->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, auto &h)
            {
                cDebugDom("process") << "Process error: " << ev.what();
                h.close();
                exe_thumb_running = false;
                this->exeFinished(1);
            });

            Utils::CStrArray arr(buildPictureCommand(data.svalue, string(), string()));
            cInfoDom("network") << "Executing command: " << arr.toString();
            exe_thumb->spawn(arr.at(0), arr.data());
            exe_thumb_running = true;
        });
    }
    else
        sendJson({{"error", "unkown audio_action" }});
}

/* E4.1q. THE DOCUMENT IS HOISTED, NOT ADDED - same move as processAudio()
 * (E4.1p). `jdataDoc` is processApi()'s existing nlohmann parse of the request
 * body; there is NO second parse and no json_dumps/Json::parse bridge, which is
 * precisely what would have moved the fate of invalid UTF-8 into this ticket by
 * accident. Unlike processAudio(), the DISPATCH migrates here too: the sixteen
 * methods below all take a Json now, so nothing was left for the json_t* to
 * feed - and that is what makes the transitional jansson overload of
 * getAudioPlayer(), left behind by E4.1p, disappear.
 */
void JsonApiHandlerHttp::processAudioDb(const Json &jdataDoc)
{
    string msg = jsonStringGet(jdataDoc, "audio_action");
    if (msg == "get_albums")
        audioDbGetAlbums(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_stats")
        audioGetDbStats(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_artist_album")
        audioDbGetAlbumArtistItem(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_year_albums")
        audioDbGetYearAlbums(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_genre_artists")
        audioDbGetGenreArtists(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_album_titles")
        audioDbGetAlbumTitles(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_playlist_titles")
        audioDbGetPlaylistTitles(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_artists")
        audioDbGetArtists(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_years")
        audioDbGetYears(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_genres")
        audioDbGetGenres(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_playlists")
        audioDbGetPlaylists(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_music_folder")
        audioDbGetMusicFolder(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_search")
        audioDbGetSearch(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_radios")
        audioDbGetRadios(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_track_infos")
        audioDbGetTrackInfos(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_radio_items")
        audioDbGetRadioItems(jdataDoc, [=](const Json &jret)
        {
            sendJson(jret);
        });
    else
        sendJson({{"error", "unkown audio_action" }});
}

void JsonApiHandlerHttp::processGetTimerange()
{
    sendJson(buildJsonGetTimerange(jsonParam));
}

void JsonApiHandlerHttp::processSetTimerange(const Json &jroot)
{
    sendJson(buildJsonSetTimerange(jroot));
}

void JsonApiHandlerHttp::processEventLog()
{
    buildJsonEventLog(jsonParam, [this](Json &j) { sendJson(j); });
}

/* E4.1r. HOISTED, NOT ADDED: the document handed down here is jsonRootDoc,
 * the nlohmann parse E4.1m already ran on every request body. Zero extra parse,
 * no json_dumps + Json::parse bridge - the one that would have moved the fate
 * of invalid UTF-8 by accident.
 * jsonStringGet() and not jroot["type"].get<string>(): jansson_string_get()
 * answered the DEFAULT on an absent member, on a member that is not a string
 * and on a non object root, and an unknown "type" answers NOTHING AT ALL
 * (frozen bug, E4.0c) - a throw here would be a 500 instead of a silence.
 */
void JsonApiHandlerHttp::processAutoscenario(const Json &jroot)
{
    string msg = jsonStringGet(jroot, "type");
    if (msg == "list")
        sendJson(buildAutoscenarioList(jroot));
    else if (msg == "get")
        sendJson(buildAutoscenarioGet(jroot));
    else if (msg == "create")
        sendJson(buildAutoscenarioCreate(jroot));
    else if (msg == "delete")
        sendJson(buildAutoscenarioDelete(jroot));
    else if (msg == "modify")
        sendJson(buildAutoscenarioModify(jroot));
    else if (msg == "add_schedule")
        sendJson(buildAutoscenarioAddSchedule(jroot));
    else if (msg == "del_schedule")
        sendJson(buildAutoscenarioDelSchedule(jroot));
    //T3.18: manual re-enable of a scenario disabled by a missing IO. It can
    //REFUSE, which is why it is a command and not a set_param.
    else if (msg == "reenable")
        sendJson(buildAutoscenarioReenable(jroot));
}

void JsonApiHandlerHttp::processCamera()
{
    //Get camera object
    IPCam *camera = dynamic_cast<IPCam *>(ListeRoom::Instance().get_io(jsonParam["id"]));
    if (!camera)
    {
        sendJson({{"error", "unkown camera id" }});
        return;
    }

    if (jsonParam["type"] == "get_picture")
    {
        /* T3.17d: the snapshot is a real HTTP round trip to the camera, and
         * IPCam parks this callback in a member of its own until the transfer
         * completes (IPCam.cpp:130). The client can hang up in between, and
         * that deletes the whole handler - HttpClient::~HttpClient() does
         * `delete jsonApi` (HttpClient.cpp:162). A late answer would then
         * format its response through a freed httpClient and emit it on a
         * freed sendData. The token says the handler is gone; the invalidation
         * is implicit, handlerAlive simply dies with the object.
         *
         * Nothing to answer when it fires: the connection object it would be
         * written to is exactly what no longer exists. This is the same bare
         * return as the three guarded callbacks already in this file
         * (processGetCover() :458, the get_cover branch :723,
         * downloadCameraPicture() :1027), and no json is in flight here to
         * release.
         *
         * The camera needs no by-id re-lookup, unlike the audio chains of
         * T3.17a-c: this callback never dereferences `camera`, and ~IPCam()
         * deletes the downloader that owns the callback, so a camera deleted
         * mid-transfer cannot call us back at all.
         */
        std::weak_ptr<bool> alive = handlerAlive;

        camera->downloadSnapshot([this, alive](const string &downloadedData)
        {
            if (alive.expired())
                return;

            if (downloadedData.empty())
            {
                Params headers;
                headers.Add("Connection", "close");
                headers.Add("Content-Type", "image/jpeg");
                string res = httpClient->buildHttpResponseFromFile(HTTP_200, headers,
                                                                   Prefix::Instance().dataDirectoryGet() + "/camfail.jpg");
                sendData.emit(res);
            }
            else
            {
                Params headers;
                headers.Add("Connection", "close");
                headers.Add("Content-Type", "image/jpeg");
                string res = httpClient->buildHttpResponse(HTTP_200, headers, downloadedData);
                sendData.emit(res);
            }
        });
    }
    else if (jsonParam["type"] == "get_video")
    {
        if (camera->getVideoUrl().empty())
        {
            //Empty mjpeg url, build the stream with single pictures

            if (!camHeaderSent)
            {
                sendData.emit(HTTP_CAMERA_STREAM);
                camHeaderSent = true;
            }

            downloadCameraPicture(jsonParam["id"]);
        }
        else
        {
            //send mjpeg stream

            releaseCameraDl();

            cameraDl = new UrlDownloader(camera->getVideoUrl(), true);
            //T2.19: honor the camera's per-device insecure param (default
            //true), same policy as the snapshot path in
            //IPCam::downloadSnapshot()
            if (camera->tlsInsecure())
                cameraDl->setInsecure();
            //T2.10: the mjpeg frames are relayed live through m_signalData,
            //the downloader's internal accumulation is dead weight here, keep
            //its bound minimal
            cameraDl->bufferMaxSizeSet(64 * 1024);
            camConnData = cameraDl->m_signalData.connect([this](int size, const char *data)
            {
                if (!camHeaderSent)
                {
                    stringstream sres;
                    //HTTP code
                    sres << HTTP_200 << "\r\n";

                    Params h = cameraDl->getResponseHeaders();
                    for (int i = 0;i < h.size();i++)
                    {
                        string key, value;
                        h.get_item(i, key, value);
                        sres << key << ": " << value << "\r\n";
                    }

                    sres << "\r\n";
                    sendData.emit(sres.str());

                    camHeaderSent = true;
                }

                sendData.emit(string((char *)data, size));
            });

            camConnComplete = cameraDl->m_signalComplete.connect([this](int)
            {
                //The downloader deletes itself right after this signal
                camConnData.disconnect();
                camConnComplete.disconnect();
                cameraDl = nullptr;

                closeConnection.emit(0, string());
            });
            cameraDl->httpGet();
        }
    }
}

void JsonApiHandlerHttp::releaseCameraDl()
{
    if (!cameraDl)
        return;

    /* T2.10: cancel() really interrupts the transfer (terminates curl, closes
     * the stdio pipe, disconnects every signal) and the autodelete object then
     * frees itself safely. Our connections are disconnected first so the slots
     * are dead even before cancel() clears the signals.
     */
    camConnData.disconnect();
    camConnComplete.disconnect();
    cameraDl->cancel();
    cameraDl = nullptr;
}

void JsonApiHandlerHttp::downloadCameraPicture(const string &cameraId)
{
    IPCam *camera = dynamic_cast<IPCam *>(ListeRoom::Instance().get_io(cameraId));
    if (!camera)
        return;

    /* The snapshot callback is kept by the camera and the re-arm goes through a
     * timer: both outlive this handler when the client closes the connection in
     * the middle of the stream. The token tells them the handler is gone.
     */
    std::weak_ptr<bool> alive = handlerAlive;

    camera->downloadSnapshot([this, alive, cameraId](const string &downloadedData)
    {
        if (alive.expired())
            return;

        sendData.emit(HTTP_CAMERA_STREAM_BOUNDARY);
        if (!downloadedData.empty())
        {
            sendData.emit(downloadedData);
        }
        else
        {
            ifstream file(Prefix::Instance().dataDirectoryGet() + "/camfail.jpg");
            string bodypic((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
            sendData.emit(bodypic);
        }

        Timer::singleShot(0, [this, alive, cameraId]()
        {
            if (alive.expired())
                return;

            downloadCameraPicture(cameraId);
        });
    });
}

void JsonApiHandlerHttp::processEventPicture()
{
    string file;

    if (!resolveEventPicture(jsonParam["pic_uid"], file))
    {
        cDebugDom("network") << "Picture " << jsonParam["pic_uid"] << " not found";

        Params headers;
        headers.Add("Connection", "close");
        headers.Add("Content-Type", "text/html");
        string res = httpClient->buildHttpResponse(HTTP_404, headers, HTTP_404_BODY);
        sendData.emit(res);
        return;
    }

    Params headers;
    headers.Add("Connection", "close");
    headers.Add("Content-Type", "image/jpeg");
    string res = httpClient->buildHttpResponseFromFile(HTTP_200, headers, file);
    sendData.emit(res);
}

void JsonApiHandlerHttp::processRegisterPush()
{
    Json ret = {{ "success", registerPushToken(jsonParam)?"true":"false" }};
    sendJson(ret);
}
