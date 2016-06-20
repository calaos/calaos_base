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
#include "Prefix.h"
#include "CalaosConfig.h"
#include "AudioPlayer.h"
#include "InPlageHoraire.h"
#include "HttpCodes.h"
#include "EcoreTimer.h"
#include "HttpClient.h"

namespace Calaos {
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
    delete cameraDl;
    ecore_event_handler_del(exe_handler);
    ecore_file_unlink(tempfname.c_str());
}

void JsonApiHandlerHttp::processApi(const std::string &data, const Params &paramsGET)
{
    jsonParam.clear();

    //parse the json data
    json_error_t jerr;
    json_t *jroot = json_loads(data.c_str(), 0, &jerr);

    if (!jroot || !json_is_object(jroot))
    {
        cDebugDom("network") << "Error loading json : " << jerr.text << ". No JSON, trying with GET parameters.";

        jsonParam = paramsGET;

        if (jroot)
        {
            json_decref(jroot);
            jroot = nullptr;
        }
    }
    else
    {
        char *d = json_dumps(jroot, JSON_INDENT(4));
        if (d)
        {
            cDebugDom("network") << d;
            free(d);
        }

        //decode the json root object into jsonParam
        jansson_decode_object(jroot, jsonParam);
    }


    //check for if username/password matches
    std::string user = Utils::get_config_option("calaos_user");
    std::string pass = Utils::get_config_option("calaos_password");

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
        std::string res = httpClient->buildHttpResponse(HTTP_400, headers, HTTP_400_BODY);
        sendData.emit(res);
        closeConnection.emit(0, std::string());

        if (jroot)
        {
            json_decref(jroot);
            jroot = nullptr;
        }

        return;
    }

    //check action now
    if (jsonParam["action"] == "get_home")
        processGetHome();
    else if (jsonParam["action"] == "get_state")
        processGetState(jroot);
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
    else
    {
        if (!jroot)
        {
            Params headers;
            headers.Add("Connection", "close");
            headers.Add("Content-Type", "text/html");
            std::string res = httpClient->buildHttpResponse(HTTP_400, headers, HTTP_400_BODY);
            sendData.emit(res);
            closeConnection.emit(0, std::string());

            return;
        }

        if (jsonParam["action"] == "config")
            processConfig(jroot);
        else if (jsonParam["action"] == "get_io")
            processGetIO(jroot);
        else if (jsonParam["action"] == "audio")
            processAudio(jroot);
        else if (jsonParam["action"] == "audio_db")
            processAudioDb(jroot);
        else if (jsonParam["action"] == "set_timerange")
            processSetTimerange(jroot);
        else if (jsonParam["action"] == "autoscenario")
            processAutoscenario(jroot);
    }

    if (jroot)
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
        std::string res = httpClient->buildHttpResponse(HTTP_500, headers, HTTP_500_BODY);
        sendData.emit(res);
        closeConnection.emit(0, std::string());

        return;
    }
    json_decref(json);

    std::string data(d);
    free(d);

    Params headers;
    headers.Add("Connection", "Close");
    headers.Add("Cache-Control", "no-cache, must-revalidate");
    headers.Add("Expires", "Mon, 26 Jul 1997 05:00:00 GMT");
    headers.Add("Content-Type", "application/json");
    headers.Add("Content-Length", Utils::to_string(data.size()));
    std::string res = httpClient->buildHttpResponse(HTTP_200, headers, data);
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

void JsonApiHandlerHttp::processGetStates()
{
    buildJsonStates(jsonParam, [=](json_t *jret)
    {
        sendJson(jret);
    });
}

void JsonApiHandlerHttp::processQuery()
{
    buildQuery(jsonParam, [=](json_t *jret)
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
        std::string uuid = PollListenner::Instance().Register();
        json_object_set_new(jret, "uuid", json_string(uuid.c_str()));
    }
    else if (jsonParam["type"] == "unregister")
    {
        std::string uuid = jsonParam["uuid"];
        bool success = PollListenner::Instance().Unregister(uuid);
        json_object_set_new(jret, "success", json_string(success?"true":"false"));
    }
    else if (jsonParam["type"] == "get")
    {
        std::string uuid = jsonParam["uuid"];
        std::list<CalaosEvent> events;

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
    AudioPlayer *player = dynamic_cast<AudioPlayer *>(ListeRoom::Instance().get_io(jsonParam["id"]));
    if (!player)
    {
        json_t *jret = json_object();
        json_object_set_new(jret, "success", json_string("false"));
        json_object_set_new(jret, "error_str", json_string("id not set"));
        sendJson(jret);
        return;
    }

    std::string w, h;
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

        std::string cmd = "calaos_thumb \"" + data.svalue + "\" \"" + tempfname + "\"";
        if (w.empty() || h.empty())
            cmd += " " + w + "x" + h;
        exe_thumb = ecore_exe_run(cmd.c_str(), nullptr);
    });
}

void JsonApiHandlerHttp::processGetCameraPic()
{
    IPCam *camera = dynamic_cast<IPCam *>(ListeRoom::Instance().get_io(jsonParam["id"]));
    if (!camera)
    {
        json_t *jret = json_object();
        json_object_set_new(jret, "success", json_string("false"));
        json_object_set_new(jret, "error_str", json_string("id not set"));
        sendJson(jret);
        return;
    }

    std::string w, h;
    if (jsonParam.Exists("width"))
        w = jsonParam["width"];
    if (jsonParam.Exists("height"))
        h = jsonParam["height"];

    std::string cmd = "calaos_thumb \"" + camera->getPictureUrl() + "\" \"" + tempfname + "\"";
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
                    std::string skey = key;
                    if (skey != IO_CONFIG &&
                        skey != RULES_CONFIG &&
                        skey != LOCAL_CONFIG)
                    {
                        cErrorDom("network") << "Error, file " << skey << " is not a valid config filename";
                        ret = false;
                        continue;
                    }

                    std::string filecontent = json_string_value(value);

                    std::ofstream ofs(Utils::getConfigFile(key), std::ios::out | std::ios::trunc);

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
    std::string msg = jansson_string_get(jdata, "audio_action");
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
    else if (msg == "get_cover_url")
        audioGetCoverInfo(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else
        sendJson({{"error", "unkown audio_action" }});
}

void JsonApiHandlerHttp::processAudioDb(json_t *jdata)
{
    std::string msg = jansson_string_get(jdata, "audio_action");
    if (msg == "get_albums")
        audioDbGetAlbums(jdata, [=](json_t *jret)
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
    else if (msg == "get_genre_artists")
        audioDbGetGenreArtists(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_album_titles")
        audioDbGetAlbumTitles(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_playlist_titles")
        audioDbGetPlaylistTitles(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_artists")
        audioDbGetArtists(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_years")
        audioDbGetYears(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_genres")
        audioDbGetGenres(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_playlists")
        audioDbGetPlaylists(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_music_folder")
        audioDbGetMusicFolder(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_search")
        audioDbGetSearch(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_radios")
        audioDbGetRadios(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_track_infos")
        audioDbGetTrackInfos(jdata, [=](json_t *jret)
        {
            sendJson(jret);
        });
    else if (msg == "get_radio_items")
        audioDbGetRadioItems(jdata, [=](json_t *jret)
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

void JsonApiHandlerHttp::processSetTimerange(json_t *jroot)
{
    sendJson(buildJsonSetTimerange(jroot));
}

void JsonApiHandlerHttp::processAutoscenario(json_t *jroot)
{
    std::string msg = jansson_string_get(jroot, "type");
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
        cameraDl = new UrlDownloader(camera->getPictureUrl(), true);
        cameraDl->m_signalCompleteData.connect([=](Eina_Binbuf *downloadedData, int status)
        {
            if (status == 200)
            {
                std::string bodypic((const char *)eina_binbuf_string_get(downloadedData),
                               eina_binbuf_length_get(downloadedData));

                Params headers;
                headers.Add("Connection", "close");
                headers.Add("Content-Type", "image/jpeg");
                std::string res = httpClient->buildHttpResponse(HTTP_200, headers, bodypic);
                sendData.emit(res);
            }
            else
            {
                cErrorDom("network") << "Failed to get image for camera at url: " << camera->getPictureUrl() << " failed with code: " << status;

                Params headers;
                headers.Add("Connection", "close");
                headers.Add("Content-Type", "image/jpeg");
                std::string res = httpClient->buildHttpResponseFromFile(HTTP_200, headers,
                                                                   Prefix::Instance().dataDirectoryGet() + "/camfail.jpg");
                sendData.emit(res);
            }

            cameraDl = nullptr;
        });
        cameraDl->httpGet();
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

            downloadCameraPicture(camera);
        }
        else
        {
            //send mjpeg stream

            cameraDl = new UrlDownloader(camera->getVideoUrl(), true);
            cameraDl->m_signalData.connect([=](int size, unsigned char *data)
            {
                if (!camHeaderSent)
                {
                    std::stringstream sres;
                    //HTTP code
                    sres << HTTP_200 << "\r\n";

                    Params h = cameraDl->getResponseHeaders();
                    for (int i = 0;i < h.size();i++)
                    {
                        std::string key, value;
                        h.get_item(i, key, value);
                        sres << key << ": " << value << "\r\n";
                    }

                    sres << "\r\n";
                    sendData.emit(sres.str());

                    camHeaderSent = true;
                }

                sendData.emit(std::string((char *)data, size));
            });

            cameraDl->m_signalComplete.connect([=](int)
            {
                closeConnection.emit(0, std::string());
                cameraDl = nullptr;
            });
            cameraDl->httpGet();
        }
    }
}

void JsonApiHandlerHttp::downloadCameraPicture(IPCam *camera)
{
    cameraDl = new UrlDownloader(camera->getPictureUrl(), true);
    cameraDl->m_signalCompleteData.connect([=](Eina_Binbuf *downloadedData, int status)
    {
        sendData.emit(HTTP_CAMERA_STREAM_BOUNDARY);
        if (status == 200)
        {
            std::string bodypic((const char *)eina_binbuf_string_get(downloadedData),
                           eina_binbuf_length_get(downloadedData));
            sendData.emit(bodypic);
        }
        else
        {
            cErrorDom("network") << "Failed to get image for camera at url: " << camera->getPictureUrl() << " failed with code: " << status;

            std::ifstream file(Prefix::Instance().dataDirectoryGet() + "/camfail.jpg");
            std::string bodypic((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
            sendData.emit(bodypic);
        }

        cameraDl = nullptr;

        EcoreTimer::singleShot(0, [=]()
        {
            downloadCameraPicture(camera);
        });
    });
    cameraDl->httpGet();
}

}
