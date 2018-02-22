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

JsonApiHandlerHttp::JsonApiHandlerHttp(HttpClient *client):
    JsonApi(client)
{
    tempfname = Utils::getTmpFilename("jpg", "_json_temp");
}

JsonApiHandlerHttp::~JsonApiHandlerHttp()
{
    if (exe_thumb && exe_thumb->referenced())
    {
        exe_thumb->kill(SIGTERM);
        exe_thumb->close();
    }

    delete cameraDl;
    FileUtils::unlink(tempfname);
}

void JsonApiHandlerHttp::processApi(const string &data, const Params &paramsGET)
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
    else if (jsonParam["action"] == "get_io")
        processGetIO(jroot);
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
    else
    {
        if (!jroot)
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
            processConfig(jroot);
        else if (jsonParam["action"] == "audio")
            processAudio(jroot);
        else if (jsonParam["action"] == "audio_db")
            processAudioDb(jroot);
        else if (jsonParam["action"] == "set_timerange")
            processSetTimerange(jroot);
        else if (jsonParam["action"] == "autoscenario")
            processAutoscenario(jroot);
        else
            sendJson({{ "error", "unknown action" }});
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

void JsonApiHandlerHttp::sendJson(const Json &json)
{
    string data = json.dump();

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
    json_t *jret = nullptr;

    jret = json_pack("{s:o, s:o, s:o}",
                     "home", buildJsonHome(),
                     "cameras", buildJsonCameras(),
                     "audio", buildJsonAudio());

    sendJson(jret);
}

void JsonApiHandlerHttp::processGetState(json_t *jroot)
{
    vector<string> iolist;

    if (jroot)
    {
        json_t *jio = json_object_get(jroot, "items");
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
    }
    else
    {
        if (jsonParam.Exists("items"))
        {
            Utils::split(jsonParam["items"], iolist, ",");
        }
    }

    buildJsonState(iolist, [=](json_t *jret)
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
    vector<string> iolist;

    if (jroot)
    {
        json_t *jio = json_object_get(jroot, "items");
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
    AudioPlayer *player = dynamic_cast<AudioPlayer *>(ListeRoom::Instance().get_io(jsonParam["id"]));
    if (!player)
    {
        json_t *jret = json_object();
        json_object_set_new(jret, "success", json_string("false"));
        json_object_set_new(jret, "error_str", json_string("id not set"));
        sendJson(jret);
        return;
    }

    string w;
    if (jsonParam.Exists("width"))
        w = jsonParam["width"];

    player->get_album_cover([=](AudioPlayerData data)
    {
        //do not start another exe if one is running already
        if (data.svalue == "" || exe_thumb->active())
        {
            json_t *jret = json_object();
            json_object_set_new(jret, "success", json_string("false"));
            json_object_set_new(jret, "error_str", json_string("unable to get url"));
            sendJson(jret);
            return;
        }

        string cmd = "calaos_picture " + data.svalue + " " + tempfname;
        if (!w.empty())
            cmd += " " + w;

        exe_thumb = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
        exe_thumb->once<uvw::ExitEvent>([this](const uvw::ExitEvent &ev, auto &h)
        {
            h.close();
            this->exeFinished(ev.status);
        });
        exe_thumb->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, auto &h)
        {
            cDebugDom("process") << "Process error: " << ev.what();
            h.close();
            this->exeFinished(1);
        });

        Utils::CStrArray arr(cmd);
        cInfoDom("network") << "Executing command: " << arr.toString();
        exe_thumb->spawn(arr.at(0), arr.data());
    });
}

void JsonApiHandlerHttp::processGetCameraPic()
{
    IPCam *camera = dynamic_cast<IPCam *>(ListeRoom::Instance().get_io(jsonParam["id"]));
    if (!camera || exe_thumb->active())
    {
        json_t *jret = json_object();
        json_object_set_new(jret, "success", json_string("false"));
        json_object_set_new(jret, "error_str", json_string("id not set"));
        sendJson(jret);
        return;
    }

    string w;
    if (jsonParam.Exists("width"))
        w = jsonParam["width"];

    string cmd = "calaos_picture " + camera->getPictureUrl() + " " + tempfname;
    if (!w.empty())
        cmd += " " + w;

    exe_thumb = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
    exe_thumb->once<uvw::ExitEvent>([this](const uvw::ExitEvent &ev, auto &h)
    {
        h.close();
        this->exeFinished(ev.status);
    });
    exe_thumb->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, auto &h)
    {
        cDebugDom("process") << "Process error: " << ev.what();
        h.close();
        this->exeFinished(1);
    });

    Utils::CStrArray arr(cmd);
    cInfoDom("network") << "Executing command: " << arr.toString();
    exe_thumb->spawn(arr.at(0), arr.data());
}

void JsonApiHandlerHttp::exeFinished(int exit_code)
{
    if (exit_code != 0)
    {
        json_t *jret = json_object();
        json_object_set_new(jret, "success", json_string("false"));
        json_object_set_new(jret, "error_str", json_string("unable to load data from url"));
        sendJson(jret);
        return;
    }

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
    string msg = jansson_string_get(jdata, "audio_action");
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

void JsonApiHandlerHttp::processEventLog()
{
    buildJsonEventLog(jsonParam, [this](Json &j) { sendJson(j); });
}

void JsonApiHandlerHttp::processAutoscenario(json_t *jroot)
{
    string msg = jansson_string_get(jroot, "type");
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
        cameraDl->m_signalCompleteData.connect([=](const string &downloadedData, int status)
        {
            if (status == 200)
            {
                Params headers;
                headers.Add("Connection", "close");
                headers.Add("Content-Type", "image/jpeg");
                string res = httpClient->buildHttpResponse(HTTP_200, headers, downloadedData);
                sendData.emit(res);
            }
            else
            {
                cErrorDom("network") << "Failed to get image for camera at url: " << camera->getPictureUrl() << " failed with code: " << status;

                Params headers;
                headers.Add("Connection", "close");
                headers.Add("Content-Type", "image/jpeg");
                string res = httpClient->buildHttpResponseFromFile(HTTP_200, headers,
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
            cameraDl->m_signalData.connect([=](int size, const char *data)
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

            cameraDl->m_signalComplete.connect([=](int)
            {
                closeConnection.emit(0, string());
                cameraDl = nullptr;
            });
            cameraDl->httpGet();
        }
    }
}

void JsonApiHandlerHttp::downloadCameraPicture(IPCam *camera)
{
    cameraDl = new UrlDownloader(camera->getPictureUrl(), true);
    cameraDl->m_signalCompleteData.connect([=](const string &downloadedData, int status)
    {
        sendData.emit(HTTP_CAMERA_STREAM_BOUNDARY);
        if (status == 200)
        {
            sendData.emit(downloadedData);
        }
        else
        {
            cErrorDom("network") << "Failed to get image for camera at url: " << camera->getPictureUrl() << " failed with code: " << status;

            ifstream file(Prefix::Instance().dataDirectoryGet() + "/camfail.jpg");
            string bodypic((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
            sendData.emit(bodypic);
        }

        cameraDl = nullptr;

        Timer::singleShot(0, [=]()
        {
            downloadCameraPicture(camera);
        });
    });
    cameraDl->httpGet();
}

void JsonApiHandlerHttp::processEventPicture()
{
    string pic_uid = jsonParam["pic_uid"];
    string file = Utils::getCacheFile("push_pictures") + "/" + pic_uid + ".jpg";

    if (!FileUtils::exists(file))
    {
        cDebugDom("network") << "Picture " << file << " not found";

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

