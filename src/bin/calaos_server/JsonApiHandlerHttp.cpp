/******************************************************************************
 **  Copyright (c) 2007-2015, Calaos. All Rights Reserved.
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
#include "TCPConnection.h"
#include "Prefix.h"
#include "CalaosConfig.h"
#include "AudioManager.h"
#include "AudioPlayer.h"
#include "CamManager.h"
#include "IPCam.h"
#include "InPlageHoraire.h"
#include "HttpCodes.h"
#include "EcoreTimer.h"
#include "HttpClient.h"

Eina_Bool _ecore_exe_finished(void *data, int type, void *event)
{
    JsonApiHandlerHttp *api = reinterpret_cast<JsonApiHandlerHttp *>(data);
    Ecore_Exe_Event_Del *ev = reinterpret_cast<Ecore_Exe_Event_Del *>(event);

    if (ev->exe != api->exe_thumb)
        return ECORE_CALLBACK_PASS_ON;

    api->exeFinished(ev->exe, ev->exit_code);

    return ECORE_CALLBACK_CANCEL;
}

JsonApiHandlerHttp::JsonApiHandlerHttp(HttpClient *client):
    JsonApi(client)
{
    exe_handler = ecore_event_handler_add(ECORE_EXE_EVENT_DEL, _ecore_exe_finished, this);

    int cpt = rand();
    do
    {
        tempfname = "/tmp/calaos_json_temp_" + Utils::to_string(cpt) + ".jpg";
        cpt++;
    }
    while (ecore_file_exists(tempfname.c_str()));
}

JsonApiHandlerHttp::~JsonApiHandlerHttp()
{
    ecore_event_handler_del(exe_handler);
    ecore_file_unlink(tempfname.c_str());
}

void JsonApiHandlerHttp::processApi(const string &data)
{
    jsonParam.clear();

    //parse the json data
    json_error_t jerr;
    json_t *jroot = json_loads(data.c_str(), 0, &jerr);

    if (!jroot || !json_is_object(jroot))
    {
        cDebugDom("network") << "Error loading json : " << jerr.text;

        Params headers;
        headers.Add("Connection", "close");
        headers.Add("Content-Type", "text/html");
        string res = httpClient->buildHttpResponse(HTTP_400, headers, HTTP_400_BODY);

        sendData.emit(res);
        closeConnection.emit(0, string());

        if (jroot)
            json_decref(jroot);

        return;
    }

    char *d = json_dumps(jroot, JSON_INDENT(4));
    if (d)
    {
        cDebugDom("network") << d;
        free(d);
    }

    //decode the json root object into jsonParam
    jansson_decode_object(jroot, jsonParam);

    //check for if username/password matches
    string user = Utils::get_config_option("calaos_user");
    string pass = Utils::get_config_option("calaos_password");

    if (Utils::get_config_option("cn_user") != "" &&
        Utils::get_config_option("cn_pass") != "")
    {
        user = Utils::get_config_option("cn_user");
        pass = Utils::get_config_option("cn_pass");
    }

    if (user != jsonParam["cn_user"] || pass != jsonParam["cn_pass"])
    {
        cDebugDom("network") << "Login failed!";

        Params headers;
        headers.Add("Connection", "close");
        headers.Add("Content-Type", "text/html");
        string res = httpClient->buildHttpResponse(HTTP_400, headers, HTTP_400_BODY);
        sendData.emit(res);
        closeConnection.emit(0, string());

        return;
    }

    //check action now
    if (jsonParam["action"] == "get_home")
        processGetHome();
    else if (jsonParam["action"] == "get_state")
        processGetState(jroot);
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
    else if (jsonParam["action"] == "config")
        processConfig(jroot);
    else if (jsonParam["action"] == "get_io")
        processGetIO(jroot);
    else if (jsonParam["action"] == "audio")
        processAudio(jroot);
    else if (jsonParam["action"] == "audio_db")
        processAudioDb(jroot);

    json_decref(jroot);
}

void JsonApiHandlerHttp::sendJson(json_t *json)
{
    char *d = json_dumps(json, JSON_COMPACT | JSON_ENSURE_ASCII /*| JSON_ESCAPE_SLASH*/);
    if (!d)
    {
        json_decref(json);
        cDebugDom("network") << "json_dumps failed!";

        Params headers;
        headers.Add("Connection", "close");
        headers.Add("Content-Type", "text/html");
        string res = httpClient->buildHttpResponse(HTTP_500, headers, HTTP_500_BODY);
        sendData.emit(res);
        closeConnection.emit(0, string());

        return;
    }
    json_decref(json);

    string data(d);
    free(d);

    Params headers;
    headers.Add("Connection", "Close");
    headers.Add("Cache-Control", "no-cache, must-revalidate");
    headers.Add("Expires", "Mon, 26 Jul 1997 05:00:00 GMT");
    headers.Add("Content-Type", "application/json");
    headers.Add("Content-Length", Utils::to_string(data.size()));
    string res = httpClient->buildHttpResponse(HTTP_200, headers, data);
    sendData.emit(res);
}

void JsonApiHandlerHttp::sendJson(const Params &p)
{
    sendJson(p.toJson());
}

void JsonApiHandlerHttp::processGetHome()
{
    json_t *jret = nullptr;

    jret = json_pack("{s:o, s:o, s:o}",
                     "home", buildJsonHome(),
                     "cameras", buildJsonCameras(),
                     "audio", buildJsonAudio());

    sendJson(jret);
}

void JsonApiHandlerHttp::processGetState(json_t *jroot)
{
    buildJsonState(jroot, [=](json_t *jret)
    {
        sendJson(jret);
    });
}

void JsonApiHandlerHttp::processGetIO(json_t *jroot)
{
    sendJson(buildJsonGetIO(jroot));
}

void JsonApiHandlerHttp::processSetState()
{
    json_t *jret = json_object();
    json_object_set_new(jret, "success", json_string(decodeSetState(jsonParam)?"true":"false"));
    sendJson(jret);
}

void JsonApiHandlerHttp::processGetPlaylist()
{
    decodeGetPlaylist(jsonParam, [=](json_t *jret)
    {
        sendJson(jret);
    });
}

void JsonApiHandlerHttp::processPolling()
{
    json_t *jret = json_object();

    if (jsonParam["type"] == "register")
    {
        string uuid = PollListenner::Instance().Register();
        json_object_set_new(jret, "uuid", json_string(uuid.c_str()));
    }
    else if (jsonParam["type"] == "unregister")
    {
        string uuid = jsonParam["uuid"];
        bool success = PollListenner::Instance().Unregister(uuid);
        json_object_set_new(jret, "success", json_string(success?"true":"false"));
    }
    else if (jsonParam["type"] == "get")
    {
        string uuid = jsonParam["uuid"];
        list<CalaosEvent> events;

        bool res = PollListenner::Instance().GetEvents(uuid, events);
        if (!res)
            json_object_set_new(jret, "success", json_string("false"));
        else
        {
            json_t *jev = json_array();

            for (auto i = events.cbegin();i != events.cend();i++)
            {
                json_array_append_new(jev, i->toJson());
            }

            json_object_set_new(jret, "success", json_string("true"));
            json_object_set_new(jret, "events", jev);
        }

    }

    sendJson(jret);
}

void JsonApiHandlerHttp::processGetCover()
{
    int pid;
    Utils::from_string(jsonParam["player_id"], pid);
    if (pid < 0 || pid >= AudioManager::Instance().get_size())
    {
        json_t *jret = json_object();
        json_object_set_new(jret, "success", json_string("false"));
        json_object_set_new(jret, "error_str", json_string("player_id not set"));
        sendJson(jret);
        return;
    }

    AudioPlayer *player = AudioManager::Instance().get_player(pid);

    string w, h;
    if (jsonParam.Exists("width"))
        w = jsonParam["width"];
    if (jsonParam.Exists("height"))
        h = jsonParam["height"];

    player->get_album_cover([=](AudioPlayerData data)
    {
        //do not start another exe if one is running already
        if (data.svalue == "" || exe_thumb)
        {
            json_t *jret = json_object();
            json_object_set_new(jret, "success", json_string("false"));
            json_object_set_new(jret, "error_str", json_string("unable to get url"));
            sendJson(jret);
            return;
        }

        string cmd = "calaos_thumb \"" + data.svalue + "\" \"" + tempfname + "\"";
        if (w.empty() || h.empty())
            cmd += " " + w + "x" + h;
        exe_thumb = ecore_exe_run(cmd.c_str(), nullptr);
    });
}

void JsonApiHandlerHttp::processGetCameraPic()
{
    int pid;
    Utils::from_string(jsonParam["camera_id"], pid);
    if (pid < 0 || pid >= CamManager::Instance().get_size())
    {
        json_t *jret = json_object();
        json_object_set_new(jret, "success", json_string("false"));
        json_object_set_new(jret, "error_str", json_string("camera_id not set"));
        sendJson(jret);
        return;
    }

    IPCam *camera = CamManager::Instance().get_camera(pid);

    string w, h;
    if (jsonParam.Exists("width"))
        w = jsonParam["width"];
    if (jsonParam.Exists("height"))
        h = jsonParam["height"];

    string cmd = "calaos_thumb \"" + camera->get_picture() + "\" \"" + tempfname + "\"";
    if (w.empty() || h.empty())
        cmd += " " + w + "x" + h;
    exe_thumb = ecore_exe_run(cmd.c_str(), nullptr);
}

void JsonApiHandlerHttp::exeFinished(Ecore_Exe *exe, int exit_code)
{
    if (exit_code != 0)
    {
        json_t *jret = json_object();
        json_object_set_new(jret, "success", json_string("false"));
        json_object_set_new(jret, "error_str", json_string("unable to load data from url"));
        sendJson(jret);
        return;
    }

    ecore_exe_free(exe);

    json_t *jret = json_object();
    json_object_set_new(jret, "success", json_string("true"));
    json_object_set_new(jret, "contenttype", json_string("image/jpeg"));
    json_object_set_new(jret, "encoding", json_string("base64"));
    json_object_set_new(jret, "data", json_string(Utils::getFileContentBase64(tempfname.c_str()).c_str()));
    sendJson(jret);
}

void JsonApiHandlerHttp::processConfig(json_t *jroot)
{
    json_t *jret = json_object();

    if (jsonParam["type"] == "get")
    {
        Config::Instance().SaveConfigIO();
        Config::Instance().SaveConfigRule();

        json_t *jfiles = json_object();
        json_object_set_new(jfiles, "io.xml",
                            json_string(Utils::getFileContent(Utils::getConfigFile(IO_CONFIG).c_str()).c_str()));
        json_object_set_new(jfiles, "rules.xml",
                            json_string(Utils::getFileContent(Utils::getConfigFile(RULES_CONFIG).c_str()).c_str()));
        json_object_set_new(jfiles, "local_config.xml",
                            json_string(Utils::getFileContent(Utils::getConfigFile(LOCAL_CONFIG).c_str()).c_str()));

        json_object_set_new(jret, "config_files", jfiles);
        json_object_set_new(jret, "success", json_string("true"));
    }
    else if (jsonParam["type"] == "put")
    {
        bool ret = true;
        json_t *jfiles = json_object_get(jroot, "config_files");
        if (jfiles && json_is_object(jfiles))
        {
            const char *key;
            json_t *value;

            json_object_foreach(jfiles, key, value)
            {
                if (key && json_is_string(value))
                {
                    string skey = key;
                    if (skey != IO_CONFIG &&
                        skey != RULES_CONFIG &&
                        skey != LOCAL_CONFIG)
                    {
                        cErrorDom("network") << "Error, file " << skey << " is not a valid config filename";
                        ret = false;
                        continue;
                    }

                    string filecontent = json_string_value(value);

                    ofstream ofs(Utils::getConfigFile(key), ios::out | ios::trunc);

                    if (ofs.is_open())
                    {
                        ofs << filecontent;
                        ofs.close();
                    }
                    else
                    {
                        cErrorDom("network") << "Error, key " << key << " is not a string";
                        ret = false;
                    }
                }
                else
                {
                    cErrorDom("network") << "Error, key " << key << " is not a string";
                    ret = false;
                }
            }
        }
        else
        {
            ret = false;
            cErrorDom("network") << "Error, wrong query";
        }

        json_object_set_new(jret, "success", json_string(ret?"true":"false"));

        if (ret)
            httpClient->setNeedRestart(true);
    }
    else
    {
        json_object_set_new(jret, "success", json_string("false"));
    }

    sendJson(jret);
}

void JsonApiHandlerHttp::processAudio(json_t *jdata)
{
    string msg = jansson_string_get(jdata, "audio_action");
    if (msg == "get_playlist_size")
        audioGetPlaylistSize(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_time")
        audioGetTime(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_playlist_item")
        audioGetPlaylistItem(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else
        sendJson({{"error", "unkown audio_action" }});
}

void JsonApiHandlerHttp::processAudioDb(json_t *jdata)
{
    string msg = jansson_string_get(jdata, "audio_action");
    if (msg == "get_album_item")
        audioDbGetAlbumItem(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_stats")
        audioGetDbStats(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_artist_album")
        audioDbGetAlbumArtistItem(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_year_albums")
        audioDbGetYearAlbums(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else
        sendJson({{"error", "unkown audio_action" }});
}
