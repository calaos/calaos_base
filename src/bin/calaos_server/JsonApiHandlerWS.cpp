/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#include "CalaosConfig.h"
#include "AudioPlayer.h"
#include "IPCam.h"
#include "InPlageHoraire.h"
#include "HttpCodes.h"
#include "WebSocket.h"

JsonApiHandlerWS::JsonApiHandlerWS(HttpClient *client):
    JsonApi(client)
{
    evcon = EventManager::Instance().newEvent.connect(sigc::mem_fun(*this, &JsonApiHandlerWS::handleEvents));
}

JsonApiHandlerWS::~JsonApiHandlerWS()
{
    evcon.disconnect();
}

void JsonApiHandlerWS::handleEvents(const CalaosEvent &event)
{
    if (!loggedin)
        return;

    cDebugDom("network") << "Handling event: " << event.toString();

    sendJson("event", event.toJson());
}

void JsonApiHandlerWS::sendJson(const string &msg_type, json_t *data, const string &client_id)
{
    json_t *jroot = json_object();
    json_object_set_new(jroot, "msg", json_string(msg_type.c_str()));
    if (client_id != "")
        json_object_set_new(jroot, "msg_id", json_string(client_id.c_str()));
    if (data)
        json_object_set_new(jroot, "data", data);

    sendData.emit(jansson_to_string(jroot));
}

void JsonApiHandlerWS::sendJson(const string &msg_type, const Json &json, const string &client_id)
{
    Json jroot = {{ "msg", msg_type }};

    if (client_id != "")
        jroot["msg_id"] = client_id;

    jroot["data"] = json;

    sendData.emit(jroot.dump());
}

void JsonApiHandlerWS::processApi(const string &data, const Params &paramsGET)
{
    VAR_UNUSED(paramsGET); //not used for websocket

    Params jsonRoot;
    Params jsonData;

    //parse the json data
    json_error_t jerr;
    json_t *jroot = json_loads(data.c_str(), 0, &jerr);

    if (!jroot || !json_is_object(jroot))
    {
        cDebugDom("network") << "Error loading json : " << jerr.text;
        if (jroot) json_decref(jroot);
        return;
    }

    char *d = json_dumps(jroot, JSON_INDENT(4));
    if (d)
    {
        cDebugDom("network") << d;
        free(d);
    }

    //decode the json root object into Params
    jansson_decode_object(jroot, jsonRoot);

    json_t *jdata = json_object_get(jroot, "data");
    if (jdata)
        jansson_decode_object(jdata, jsonData);

    //Format: { msg: "type", msg_id: id, data: {} }

    if (jsonRoot["msg"] == "login")
    {
        //check if username/password matches
        string user = Utils::get_config_option("calaos_user");
        string pass = Utils::get_config_option("calaos_password");

        if (Utils::get_config_option("cn_user") != "" &&
            Utils::get_config_option("cn_pass") != "")
        {
            user = Utils::get_config_option("cn_user");
            pass = Utils::get_config_option("cn_pass");
        }

        //Not logged in, need to wait for a correct login
        if (user != jsonData["cn_user"] || pass != jsonData["cn_pass"])
        {
            cDebugDom("network") << "Login failed!";

            json_t *jret = json_object();
            json_object_set_new(jret, "success", json_string("false"));

            sendJson("login", jret, jsonRoot["msg_id"]);

            //Close the connection on login failure
            closeConnection.emit(WebSocketFrame::CloseCodeNormal, "login failed!");
        }
        else
        {
            sendJson("login", {{ "success", "true" }}, jsonRoot["msg_id"]);

            loggedin = true;
        }
    }
    else if (loggedin) //only process other api if loggedin
    {
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
            processSetParam(jsonData, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "del_param")
            processDelParam(jsonData, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "set_state")
            processSetState(jsonData, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "get_playlist")
            processGetPlaylist(jsonData, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "get_io")
            processGetIO(jdata, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "audio")
            processAudio(jdata, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "audio_db")
            processAudioDb(jdata, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "get_timerange")
            processGetTimerange(jsonData, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "set_timerange")
            processSetTimerange(jdata, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "autoscenario")
            processAutoscenario(jdata, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "eventlog")
            processEventLog(jsonData, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "register_push")
            processRegisterPush(jsonData, jsonRoot["msg_id"]);
        else if (jsonRoot["msg"] == "settings")
            processSettings(jsonData, jsonRoot["msg_id"]);

//        else if (jsonParam["action"] == "get_cover")
//            processGetCover();
//        else if (jsonParam["action"] == "get_camera_pic")
//            processGetCameraPic();
//        else if (jsonParam["action"] == "config")
//            processConfig(jroot);
    }

    json_decref(jroot);
}

void JsonApiHandlerWS::processGetHome(const Params &jsonReq, const string &client_id)
{
    json_t *jret = nullptr;

    jret = json_pack("{s:o, s:o, s:o}",
                     "home", buildJsonHome(),
                     "cameras", buildJsonCameras(),
                     "audio", buildJsonAudio());

    sendJson("get_home", jret, client_id);
}

void JsonApiHandlerWS::processGetState(json_t *jdata, const string &client_id)
{
    if (!jdata)
    {
        sendJson("get_state", nullptr, client_id);
        return;
    }

    vector<string> iolist;
    json_t *jio = json_object_get(jdata, "items");
    if (jio && json_is_array(jio))
    {
        uint idx;
        json_t *value;

        json_array_foreach(jio, idx, value)
        {
            if (json_is_string(value))
                iolist.push_back(json_string_value(value));
        }
    }

    buildJsonState(iolist, [=](json_t *jret)
    {
        sendJson("get_state", jret, client_id);
    });
}

void JsonApiHandlerWS::processGetStates(const Params &jsonReq, const string &client_id)
{
    buildJsonStates(jsonReq, [=](json_t *jret)
    {
        sendJson("get_states", jret, client_id);
    });
}

void JsonApiHandlerWS::processQuery(const Params &jsonReq, const string &client_id)
{
    buildQuery(jsonReq, [=](json_t *jret)
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

void JsonApiHandlerWS::processGetIO(json_t *jdata, const string &client_id)
{
    if (!jdata)
    {
        sendJson("get_io", nullptr, client_id);
        return;
    }

    vector<string> iolist;
    json_t *jio = json_object_get(jdata, "items");
    if (jio && json_is_array(jio))
    {
        uint idx;
        json_t *value;

        json_array_foreach(jio, idx, value)
        {
            if (json_is_string(value))
                iolist.push_back(json_string_value(value));
        }
    }

    sendJson("get_io", buildJsonGetIO(iolist), client_id);
}

void JsonApiHandlerWS::processSetState(Params &jsonReq, const string &client_id)
{
    bool res = decodeSetState(jsonReq);

    if (!client_id.empty())
    {
        json_t *jret = json_object();
        json_object_set_new(jret, "success", json_string(res?"true":"false"));
        sendJson("set_state", jret, client_id);
    }
}

void JsonApiHandlerWS::processGetPlaylist(Params &jsonReq, const string &client_id)
{
    decodeGetPlaylist(jsonReq, [=](json_t *jret)
    {
        sendJson("get_playlist", jret, client_id);
    });
}

void JsonApiHandlerWS::processAudio(json_t *jdata, const string &client_id)
{
    string msg = jansson_string_get(jdata, "audio_action");
    if (msg == "get_playlist_size")
        audioGetPlaylistSize(jdata, [=](json_t *jret)
        {
            sendJson("audio", jret, client_id);
        });
    else if (msg == "get_time")
        audioGetTime(jdata, [=](json_t *jret)
        {
            sendJson("audio", jret, client_id);
        });
    else if (msg == "get_playlist_item")
        audioGetPlaylistItem(jdata, [=](json_t *jret)
        {
            sendJson("audio", jret, client_id);
        });
    else if (msg == "get_cover_url")
        audioGetCoverInfo(jdata, [=](json_t *jret)
        {
            sendJson("audio", jret, client_id);
        });
    else
        sendJson("audio", {{"error", "unkown audio_action" }} , client_id);
}

void JsonApiHandlerWS::processAudioDb(json_t *jdata, const string &client_id)
{
    string msg = jansson_string_get(jdata, "audio_action");
    if (msg == "get_album")
        audioDbGetAlbums(jdata, [=](json_t *jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_stats")
        audioGetDbStats(jdata, [=](json_t *jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_artist_album")
        audioDbGetAlbumArtistItem(jdata, [=](json_t *jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_year_albums")
        audioDbGetYearAlbums(jdata, [=](json_t *jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_genre_artists")
        audioDbGetGenreArtists(jdata, [=](json_t *jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_album_titles")
        audioDbGetAlbumTitles(jdata, [=](json_t *jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_playlist_titles")
        audioDbGetPlaylistTitles(jdata, [=](json_t *jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_artists")
        audioDbGetArtists(jdata, [=](json_t *jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_years")
        audioDbGetYears(jdata, [=](json_t *jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_genres")
        audioDbGetGenres(jdata, [=](json_t *jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_playlists")
        audioDbGetPlaylists(jdata, [=](json_t *jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_music_folder")
        audioDbGetMusicFolder(jdata, [=](json_t *jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_search")
        audioDbGetSearch(jdata, [=](json_t *jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_radios")
        audioDbGetRadios(jdata, [=](json_t *jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_track_infos")
        audioDbGetTrackInfos(jdata, [=](json_t *jret)
        {
            sendJson("audio_db", jret, client_id);
        });
    else if (msg == "get_radio_items")
        audioDbGetRadioItems(jdata, [=](json_t *jret)
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

void JsonApiHandlerWS::processSetTimerange(json_t *jdata, const string &client_id)
{
    sendJson("set_timerange", buildJsonSetTimerange(jdata), client_id);
}

void JsonApiHandlerWS::processAutoscenario(json_t *jdata, const string &client_id)
{
    string msg = jansson_string_get(jdata, "type");
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

        Json ret = {{ "success", ok?"true":"false" }};
        sendJson("settings", ret, client_id);
    }
}
