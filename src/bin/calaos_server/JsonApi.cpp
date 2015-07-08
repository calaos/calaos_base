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
#include "JsonApi.h"
#include "HttpClient.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "AutoScenario.h"
#include "CalaosConfig.h"

JsonApi::JsonApi(HttpClient *client):
    httpClient(client)
{
}

JsonApi::~JsonApi()
{
}

template<typename T>
void JsonApi::buildJsonIO(T *io, json_t *jio)
{
    vector<string> params =
    { "id", "name", "type", "hits", "var_type", "visible",
      "chauffage_id", "rw", "unit", "gui_type", "state",
      "auto_scenario", "step" };

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

        json_object_set_new(jio, param.c_str(),
                            json_string(value.c_str()));
    }
}

template<typename T>
json_t *JsonApi::buildJsonRoomIO(Room *room)
{
    json_t *jdata = json_array();

    for (int i = 0;i < room->get_size<T *>();i++)
    {
        json_t *jio = json_object();
        T *io = room->get_io<T *>(i);

        buildJsonIO(io, jio);

        json_array_append_new(jdata, jio);
    }

    return jdata;
}

json_t *JsonApi::buildJsonHome()
{
    json_t *jdata = json_array();

    for (int iroom = 0;iroom < ListeRoom::Instance().size();iroom++)
    {
        Room *room = ListeRoom::Instance().get_room(iroom);
        json_t *jroom = json_object();

        json_t *jitems = json_pack("{s:o, s:o}",
                                   "inputs", buildJsonRoomIO<Input>(room),
                                   "outputs", buildJsonRoomIO<Output>(room));

        json_object_set_new(jroom, "type", json_string(room->get_type().c_str()));
        json_object_set_new(jroom, "name", json_string(room->get_name().c_str()));
        json_object_set_new(jroom, "hits", json_string(Utils::to_string(room->get_hits()).c_str()));
        json_object_set_new(jroom, "items", jitems);

        json_array_append_new(jdata, jroom);
    }

    return jdata;
}

json_t *JsonApi::buildJsonCameras()
{
    json_t *jdata = json_array();

    int cpt = 0;
    for (int i = 0;i < ListeRoom::Instance().get_nb_input();i++)
    {
        Input *in = ListeRoom::Instance().get_input(i);
        CamInput *ipcam = dynamic_cast<CamInput *>(in);
        if (ipcam)
        {
            IPCam *camera = ipcam->get_cam();

            json_t *jcam = json_object();
            json_object_set_new(jcam, "id", json_string(Utils::to_string(cpt).c_str()));
            json_object_set_new(jcam, "input_id", json_string(camera->get_param("iid").c_str()));
            json_object_set_new(jcam, "output_id", json_string(camera->get_param("oid").c_str()));
            json_object_set_new(jcam, "name", json_string(camera->get_param("name").c_str()));
            json_object_set_new(jcam, "type", json_string(camera->get_param("type").c_str()));
            json_object_set_new(jcam, "url_jpeg", json_string(camera->get_picture().c_str()));
            json_object_set_new(jcam, "url_mjpeg", json_string(camera->get_mjpeg_stream().c_str()));
            Params caps = camera->getCapabilities();
            if (caps["ptz"] == "true")
                json_object_set_new(jcam, "ptz", json_string("true"));
            else
                json_object_set_new(jcam, "ptz", json_string("false"));

            cpt++;

            json_array_append_new(jdata, jcam);
        }
    }

    return jdata;
}

json_t *JsonApi::buildJsonAudio()
{
    json_t *jdata = json_array();

    for (int i = 0;i < AudioManager::Instance().get_size();i++)
    {
        AudioPlayer *player = AudioManager::Instance().get_player(i);

        json_t *jaudio = json_object();
        json_object_set_new(jaudio, "id", json_string(Utils::to_string(i).c_str()));
        json_object_set_new(jaudio, "input_id", json_string(player->get_param("iid").c_str()));
        json_object_set_new(jaudio, "output_id", json_string(player->get_param("oid").c_str()));
        json_object_set_new(jaudio, "name", json_string(player->get_param("name").c_str()));
        json_object_set_new(jaudio, "type", json_string(player->get_param("type").c_str()));

        json_object_set_new(jaudio, "playlist", json_string(player->canPlaylist()?"true":"false"));
        json_object_set_new(jaudio, "database", json_string(player->canDatabase()?"true":"false"));

        if (player->get_params().Exists("amp"))
            json_object_set_new(jaudio, "avr", json_string(player->get_param("amp").c_str()));

        json_array_append_new(jdata, jaudio);

        //don't query detailed player infos here, other informations need to be queried to the squeezecenter
        //so the get_home request will be delayed by all the squeezecenter's requests.
        //To be faster, only return the basic infos here, and call get_state for each players to get detailed infos
    }

    return jdata;
}

void JsonApi::buildJsonState(json_t *jroot, std::function<void(json_t *)> result_lambda)
{
    json_incref(jroot);
    json_t *jinputs = json_object();
    json_t *joutputs = json_object();
    json_t *jaudio = json_array();

    json_t *jin = json_object_get(jroot, "inputs");
    if (jin && json_is_array(jin))
    {
        uint idx;
        json_t *value;

        json_array_foreach(jin, idx, value)
        {
            string svalue;

            if (!json_is_string(value)) continue;

            svalue = json_string_value(value);
            Input *input = ListeRoom::Instance().get_input(svalue);
            if (input)
            {
                if (input->get_type() == TBOOL)
                    json_object_set_new(jinputs, svalue.c_str(), json_string(input->get_value_bool()?"true":"false"));
                else if (input->get_type() == TINT)
                    json_object_set_new(jinputs, svalue.c_str(), json_string(Utils::to_string(input->get_value_double()).c_str()));
                else if (input->get_type() == TSTRING)
                    json_object_set_new(jinputs, svalue.c_str(), json_string(input->get_value_string().c_str()));
            }
        }
    }

    json_t *jout = json_object_get(jroot, "outputs");
    if (jout && json_is_array(jout))
    {
        uint idx;
        json_t *value;

        json_array_foreach(jout, idx, value)
        {
            string svalue;

            if (!json_is_string(value)) continue;

            svalue = json_string_value(value);
            Output *output = ListeRoom::Instance().get_output(svalue);
            if (output)
            {
                if (output->get_type() == TBOOL)
                    json_object_set_new(joutputs, svalue.c_str(), json_string(output->get_value_bool()?"true":"false"));
                else if (output->get_type() == TINT)
                    json_object_set_new(joutputs, svalue.c_str(), json_string(Utils::to_string(output->get_value_double()).c_str()));
                else if (output->get_type() == TSTRING)
                    json_object_set_new(joutputs, svalue.c_str(), json_string(output->get_value_string().c_str()));
            }
        }
    }

    string uuid = Utils::createRandomUuid();
    playerCounts[uuid] = 0;

    json_t *jpl = json_object_get(jroot, "audio_players");
    if (jpl && json_is_array(jpl))
    {
        uint idx;
        json_t *value;

        json_array_foreach(jpl, idx, value)
        {
            string svalue;

            if (!json_is_string(value)) continue;
            svalue = json_string_value(value);

            int pid;
            Utils::from_string(svalue, pid);
            if (pid < 0 || pid >= AudioManager::Instance().get_size())
                continue;

            playerCounts[uuid] = playerCounts[uuid] + 1;

            json_t *jplayer = json_object();
            json_object_set_new(jplayer, "player_id", json_string(Utils::to_string(pid).c_str()));

            AudioPlayer *player = AudioManager::Instance().get_player(pid);
            player->get_playlist_current([=](AudioPlayerData data1)
            {
                json_object_set_new(jplayer,
                                    "playlist_current_track",
                                    json_string(Utils::to_string(data1.ivalue).c_str()));

                player->get_volume([=](AudioPlayerData data2)
                {
                    json_object_set_new(jplayer,
                                        "volume",
                                        json_string(Utils::to_string(data2.ivalue).c_str()));

                    player->get_playlist_size([=](AudioPlayerData data3)
                    {
                        json_object_set_new(jplayer,
                                            "playlist_size",
                                            json_string(Utils::to_string(data3.ivalue).c_str()));

                        player->get_current_time([=](AudioPlayerData data4)
                        {
                            json_object_set_new(jplayer,
                                                "time_elapsed",
                                                json_string(Utils::to_string(data4.dvalue).c_str()));

                            player->get_status([=](AudioPlayerData data5)
                            {
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

                                json_object_set_new(jplayer,
                                                    "status",
                                                    json_string(status.c_str()));

                                json_t *jtrack = json_object();
                                player->get_songinfo([=](AudioPlayerData data6)
                                {
                                    Params &infos = data6.params;
                                    for (int i = 0;i < infos.size();i++)
                                    {
                                        string inf_key, inf_value;
                                        infos.get_item(i, inf_key, inf_value);

                                        json_object_set_new(jtrack,
                                                            inf_key.c_str(),
                                                            json_string(inf_value.c_str()));
                                    }

                                    json_object_set_new(jplayer,
                                                        "current_track",
                                                        jtrack);

                                    //Add player to array, and send data back if all players requests are done.
                                    json_array_append_new(jaudio, jplayer);
                                    playerCounts[uuid] = playerCounts[uuid] - 1;

                                    if (playerCounts[uuid] <= 0)
                                    {
                                        playerCounts.erase(uuid);
                                        json_decref(jroot);
                                        json_t *jret = json_object();
                                        jret = json_pack("{s:o, s:o, s:o}",
                                                         "inputs", jinputs,
                                                         "outputs", joutputs,
                                                         "audio_players", jaudio);

                                        result_lambda(jret);
                                    }
                                });
                            });
                        });
                    });
                });
            });
        }
    }

    //only send data if there is not audio players
    if (playerCounts[uuid] == 0)
    {
        playerCounts.erase(uuid);
        json_decref(jroot);
        json_t *jret = json_object();
        jret = json_pack("{s:o, s:o, s:o}",
                         "inputs", jinputs,
                         "outputs", joutputs,
                         "audio_players", jaudio);

        result_lambda(jret);
    }
}

void JsonApi::buildJsonStates(const Params &jParam, std::function<void (json_t *)> result_lambda)
{
    Params res;

    if (jParam.Exists("input_id"))
    {
        Input *o = ListeRoom::Instance().get_input(jParam["input_id"]);
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
    else if (jParam.Exists("output_id"))
    {
        Output *o = ListeRoom::Instance().get_output(jParam["output_id"]);
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

void JsonApi::buildQuery(const Params &jParam, std::function<void (json_t *)> result_lambda)
{
    Params res;

    if (jParam.Exists("input_id"))
    {
        Input *o = ListeRoom::Instance().get_input(jParam["input_id"]);
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
    else if (jParam.Exists("output_id"))
    {
        Output *o = ListeRoom::Instance().get_output(jParam["output_id"]);
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

json_t *JsonApi::buildJsonGetParam(const Params &jParam)
{
    bool success = true;
    Params ret;

    if (jParam["type"] == "input")
    {
        Input *o = ListeRoom::Instance().get_input(jParam["id"]);
        if (!o)
            success = false;
        else
            ret.Add(jParam["param"], o->get_param(jParam["param"]));
    }
    else if (jParam["type"] == "output")
    {
        Output *o = ListeRoom::Instance().get_output(jParam["id"]);
        if (!o)
            success = false;
        else
            ret.Add(jParam["param"], o->get_param(jParam["param"]));
    }

    if (!success)
        ret = {{ "error", "wrong io/param" }};

    return ret.toJson();
}

json_t *JsonApi::buildJsonSetParam(const Params &jParam)
{
    bool success = true;
    Params ret;

    if (jParam["type"] == "input")
    {
        Input *o = ListeRoom::Instance().get_input(jParam["id"]);
        if (!o)
            success = false;
        else
        {
            if (jParam["param"].empty() || jParam["value"].empty())
                success = false;
            else
            {
                o->set_param(jParam["param"], jParam["value"]);

                EventManager::create(CalaosEvent::EventInputChanged,
                                     { { "id", o->get_param("id") },
                                       { jParam["param"], jParam["value"] } });

                //send event for output too

                if (dynamic_cast<Internal *> (o) ||
                    dynamic_cast<Scenario *> (o) ||
                    dynamic_cast<InputTimer *> (o))
                {
                    EventManager::create(CalaosEvent::EventOutputChanged,
                                         { { "id", o->get_param("id") },
                                           { jParam["param"], jParam["value"] } });
                }

                CamInput *icam = dynamic_cast<CamInput *>(o);
                if (icam)
                {
                    IPCam *cam = icam->get_cam();
                    cam->set_param(jParam["param"], jParam["value"]);
                    cam->get_output()->set_param(jParam["param"], jParam["value"]);

                    EventManager::create(CalaosEvent::EventOutputChanged,
                                         { { "id", cam->get_output()->get_param("id") },
                                           { jParam["param"], jParam["value"] } });
                }

                AudioInput *iaudio = dynamic_cast<AudioInput *>(o);
                if (iaudio)
                {
                    AudioPlayer *audio = iaudio->get_player();
                    audio->set_param(jParam["param"], jParam["value"]);
                    audio->get_output()->set_param(jParam["param"], jParam["value"]);

                    EventManager::create(CalaosEvent::EventOutputChanged,
                                         { { "id", audio->get_output()->get_param("id") },
                                           { jParam["param"], jParam["value"] } });
                }
            }
        }
    }
    else if (jParam["type"] == "output")
    {
        Output *o = ListeRoom::Instance().get_output(jParam["id"]);
        if (!o)
            success = false;
        else
        {
            if (jParam["param"].empty() || jParam["value"].empty())
                success = false;
            else
                {
                o->set_param(jParam["param"], jParam["value"]);

                EventManager::create(CalaosEvent::EventOutputChanged,
                                     { { "id", o->get_param("id") },
                                       { jParam["param"], jParam["value"] } });

                //send event for output too

                if (dynamic_cast<Internal *> (o) ||
                    dynamic_cast<Scenario *> (o) ||
                    dynamic_cast<InputTimer *> (o))
                {
                    EventManager::create(CalaosEvent::EventInputChanged,
                                         { { "id", o->get_param("id") },
                                           { jParam["param"], jParam["value"] } });
                }

                CamOutput *icam = dynamic_cast<CamOutput *>(o);
                if (icam)
                {
                    IPCam *cam = icam->get_cam();
                    cam->set_param(jParam["param"], jParam["value"]);
                    cam->get_input()->set_param(jParam["param"], jParam["value"]);

                    EventManager::create(CalaosEvent::EventInputChanged,
                                         { { "id", cam->get_input()->get_param("id") },
                                           { jParam["param"], jParam["value"] } });
                }

                AudioOutput *iaudio = dynamic_cast<AudioOutput *>(o);
                if (iaudio)
                {
                    AudioPlayer *audio = iaudio->get_player();
                    audio->set_param(jParam["param"], jParam["value"]);
                    audio->get_input()->set_param(jParam["param"], jParam["value"]);

                    EventManager::create(CalaosEvent::EventInputChanged,
                                         { { "id", audio->get_input()->get_param("id") },
                                           { jParam["param"], jParam["value"] } });
                }
            }
        }
    }

    if (!success)
        ret = {{ "error", "wrong io/param" }};
    else
        ret = {{ "success", "true" }};

    return ret.toJson();
}

json_t *JsonApi::buildJsonDelParam(const Params &jParam)
{
    bool success = true;
    Params ret;

    if (jParam["type"] == "input")
    {
        Input *o = ListeRoom::Instance().get_input(jParam["id"]);
        if (!o)
            success = false;
        else
        {
            if (jParam["param"].empty())
                success = false;
            else
            {
                o->get_params().Delete(jParam["param"]);

                EventManager::create(CalaosEvent::EventInputPropertyDelete,
                                     { { "id", o->get_param("id") },
                                       { "param", jParam["param"] } });

                //send event for output too

                if (dynamic_cast<Internal *> (o) ||
                    dynamic_cast<Scenario *> (o) ||
                    dynamic_cast<InputTimer *> (o))
                {
                    EventManager::create(CalaosEvent::EventOutputPropertyDelete,
                                         { { "id", o->get_param("id") },
                                           { "param", jParam["param"] } });
                }

                CamInput *icam = dynamic_cast<CamInput *>(o);
                if (icam)
                {
                    IPCam *cam = icam->get_cam();
                    cam->get_params().Delete(jParam["param"]);
                    cam->get_output()->get_params().Delete(jParam["param"]);

                    EventManager::create(CalaosEvent::EventOutputPropertyDelete,
                                         { { "id", cam->get_output()->get_param("id") },
                                           { "param", jParam["param"] } });
                }

                AudioInput *iaudio = dynamic_cast<AudioInput *>(o);
                if (iaudio)
                {
                    AudioPlayer *audio = iaudio->get_player();
                    audio->get_params().Delete(jParam["param"]);
                    audio->get_output()->get_params().Delete(jParam["param"]);

                    EventManager::create(CalaosEvent::EventOutputPropertyDelete,
                                         { { "id", audio->get_output()->get_param("id") },
                                           { "param", jParam["param"] } });
                }
            }
        }
    }
    else if (jParam["type"] == "output")
    {
        Output *o = ListeRoom::Instance().get_output(jParam["id"]);
        if (!o)
            success = false;
        else
        {
            if (jParam["param"].empty())
                success = false;
            else
            {
                o->get_params().Delete(jParam["param"]);

                EventManager::create(CalaosEvent::EventOutputPropertyDelete,
                { { "id", o->get_param("id") },
                  { "param", jParam["param"] } });

                //send event for output too

                if (dynamic_cast<Internal *> (o) ||
                    dynamic_cast<Scenario *> (o) ||
                    dynamic_cast<InputTimer *> (o))
                {
                    EventManager::create(CalaosEvent::EventInputPropertyDelete,
                    { { "id", o->get_param("id") },
                      { "param", jParam["param"] } });
                }

                CamOutput *icam = dynamic_cast<CamOutput *>(o);
                if (icam)
                {
                    IPCam *cam = icam->get_cam();
                    cam->get_params().Delete(jParam["param"]);
                    cam->get_input()->get_params().Delete(jParam["param"]);

                    EventManager::create(CalaosEvent::EventInputPropertyDelete,
                    { { "id", cam->get_input()->get_param("id") },
                      { "param", jParam["param"] } });
                }

                AudioOutput *iaudio = dynamic_cast<AudioOutput *>(o);
                if (iaudio)
                {
                    AudioPlayer *audio = iaudio->get_player();
                    audio->get_params().Delete(jParam["param"]);
                    audio->get_input()->get_params().Delete(jParam["param"]);

                    EventManager::create(CalaosEvent::EventInputPropertyDelete,
                    { { "id", audio->get_input()->get_param("id") },
                      { "param", jParam["param"] } });
                }
            }
        }
    }

    if (!success)
        ret = {{ "error", "wrong io/param" }};
    else
        ret = {{ "success", "true" }};

    return ret.toJson();
}

json_t *JsonApi::buildJsonGetIO(json_t *jroot)
{
    json_incref(jroot);
    json_t *jinputs = json_object();
    json_t *joutputs = json_object();

    json_t *jin = json_object_get(jroot, "inputs");
    if (jin && json_is_array(jin))
    {
        uint idx;
        json_t *value;

        json_array_foreach(jin, idx, value)
        {
            string svalue;

            if (!json_is_string(value)) continue;

            svalue = json_string_value(value);
            Input *input = ListeRoom::Instance().get_input(svalue);
            if (input)
            {
                json_t *jio = json_object();
                buildJsonIO(input, jio);

                json_object_set_new(jinputs, svalue.c_str(), jio);
            }
        }
    }

    json_t *jout = json_object_get(jroot, "outputs");
    if (jout && json_is_array(jout))
    {
        uint idx;
        json_t *value;

        json_array_foreach(jout, idx, value)
        {
            string svalue;

            if (!json_is_string(value)) continue;

            svalue = json_string_value(value);
            Output *output = ListeRoom::Instance().get_output(svalue);
            if (output)
            {
                json_t *jio = json_object();
                buildJsonIO(output, jio);

                json_object_set_new(joutputs, svalue.c_str(), jio);
            }
        }
    }

    json_t *jret = json_object();
    jret = json_pack("{s:o, s:o}",
                     "inputs", jinputs,
                     "outputs", joutputs);

    return jret;
}

bool JsonApi::decodeSetState(Params &jParam)
{
    bool success = true;

    if (jParam["type"] == "input")
    {
        Input *input = ListeRoom::Instance().get_input(jParam["id"]);
        if (!input)
            success = false;
        else
        {
            if (input->get_type() == TBOOL)
                input->force_input_bool(jParam["value"] == "true");
            else
                input->force_input_string(jParam["value"]);
        }
    }
    else if (jParam["type"] == "output")
    {
        Output *output = ListeRoom::Instance().get_output(jParam["id"]);
        if (!output)
            success = false;
        else
        {
            success = false;

            if (output->get_type() == TBOOL)
            {
                if (jParam["value"] == "true") success = output->set_value(true);
                else if (jParam["value"] == "false") success = output->set_value(false);
                else success = output->set_value(jParam["value"]);
            }
            else
                success = output->set_value(jParam["value"]);
        }
    }
    else if (jParam["type"] == "audio")
    {
        int pid;
        Utils::from_string(jParam["player_id"], pid);
        if (pid < 0 || pid >= AudioManager::Instance().get_size())
            success = false;
        else
        {
            AudioPlayer *player = AudioManager::Instance().get_player(pid);

            Params cmd;
            cmd.Parse(jParam["value"]);

            if (cmd["0"] == "play") player->Play();
            else if (cmd["0"] == "pause") player->Pause();
            else if (cmd["0"] == "stop") player->Stop();
            else if (cmd["0"] == "next") player->Next();
            else if (cmd["0"] == "previous") player->Previous();
            else if (cmd["0"] == "off") player->Power(false);
            else if (cmd["0"] == "on") player->Power(true);
            else if (cmd["0"] == "volume")
            {
                int vol = 0;
                Utils::from_string(cmd["1"], vol);
                player->set_volume(vol);
            }
            else if (cmd["0"] == "time")
            {
                int _t = 0;
                Utils::from_string(cmd["1"], _t);
                player->set_current_time(_t);
            }
            else if (cmd["0"] == "playlist")
            {
                if (cmd["1"] == "clear") player->playlist_clear();
                else if (cmd["1"] == "save") player->playlist_save(cmd["2"]);
                else if (cmd["1"] == "add") player->playlist_add_items(cmd["2"]);
                else if (cmd["1"] == "play") player->playlist_play_items(cmd["2"]);
                else if (Utils::is_of_type<int>(cmd["1"]))
                {
                    int item = 0;
                    Utils::from_string(cmd["1"], item);

                    if (cmd["2"] == "moveup") player->playlist_moveup(item);
                    else if (cmd["2"] == "movedown") player->playlist_movedown(item);
                    else if (cmd["2"] == "delete") player->playlist_delete(item);
                    else if (cmd["2"] == "play") player->playlist_play(item);
                }
            }
            else if (cmd["0"] == "random")
            {
                vector<string> tk;
                split(cmd["1"], tk, ":", 2);
                if (tk.size() == 2 && tk[0] == "random_id")
                    player->get_database()->setRandomsType(tk[1]);
            }
            else if (cmd["0"] == "options")
            {
                if (cmd["1"] == "sync" && cmd["2"] == "off")
                    player->Synchronize("", false);
                else if (cmd["1"] == "sync")
                {
                    vector<string> tk;
                    split(cmd["2"], tk, ":", 2);
                    if (tk.size() == 2 && tk[0] == "id")
                        player->Synchronize(tk[1], true);
                }
            }
            else if (cmd["0"] == "database")
            {
                if (cmd["1"] == "playlist" && cmd["2"] == "delete")
                {
                    vector<string> tk;
                    split(cmd["3"], tk, ":", 2);
                    if (tk.size() == 2 && tk[0] == "playlist_id")
                        player->playlist_delete(tk[1]);
                }
            }
        }
    }
    else if (jParam["type"] == "camera")
    {
        int pid;
        Utils::from_string(jParam["camera_id"], pid);
        if (pid < 0 || pid >= CamManager::Instance().get_size())
            success = false;
        else
        {
            IPCam *camera = CamManager::Instance().get_camera(pid);
            if (camera)
            {
                if (jParam["camera_action"] == "move")
                {
                    int action = -1;

                    if (jParam["value"] == "left") action = 1;
                    if (jParam["value"] == "right") action = 1;
                    if (jParam["value"] == "up") action = 1;
                    if (jParam["value"] == "down") action = 1;
                    if (jParam["value"] == "home") action = 1;
                    if (jParam["value"] == "zoomin") action = 1;
                    if (jParam["value"] == "zoomout") action = 1;

                    if (action == 1)
                        camera->activateCapabilities("ptz", "move", jParam["camera_action"]);

                    //move to a preset position
                    if (Utils::is_of_type<int>(jParam["camera_action"]))
                        camera->activateCapabilities("position", "recall", jParam["camera_action"]);
                }
                else if (jParam["camera_action"] == "save")
                {
                    if (Utils::is_of_type<int>(jParam["value"]))
                        camera->activateCapabilities("position", "save", jParam["value"]);
                }
            }
        }
    }

    return success;
}

void JsonApi::decodeGetPlaylist(Params &jParam, std::function<void(json_t *)>result_lambda)
{
    int pid;
    Utils::from_string(jParam["player_id"], pid);
    if (pid < 0 || pid >= AudioManager::Instance().get_size())
    {
        json_t *jret = json_object();
        json_object_set_new(jret, "success", json_string("false"));
        result_lambda(jret);
        return;
    }

    AudioPlayer *player = AudioManager::Instance().get_player(pid);

    json_t *jplayer = json_object();

    player->get_playlist_current([=](AudioPlayerData data)
    {
        json_object_set_new(jplayer,
                            "current_track",
                            json_string(Utils::to_string(data.ivalue).c_str()));

        player->get_playlist_size([=](AudioPlayerData data1)
        {
            json_object_set_new(jplayer,
                                "count",
                                json_string(Utils::to_string(data1.ivalue).c_str()));

            int it_count = data1.ivalue;
            if (it_count <= 0)
            {
                json_object_set_new(jplayer, "items", json_array());
                result_lambda(jplayer);
            }
            else
                getNextPlaylistItem(player, jplayer, json_array(), 0, it_count, result_lambda);
        });
    });
}

void JsonApi::getNextPlaylistItem(AudioPlayer *player, json_t *jplayer, json_t *jplaylist, int it_current, int it_count, std::function<void(json_t *)>result_lambda)
{
    player->get_playlist_item(it_current, [=](AudioPlayerData data)
    {
        json_t *jtrack = json_object();
        Params &infos = data.params;
        for (int i = 0;i < infos.size();i++)
        {
            string inf_key, inf_value;
            infos.get_item(i, inf_key, inf_value);

            json_object_set_new(jtrack,
                                inf_key.c_str(),
                                json_string(inf_value.c_str()));
        }

        json_array_append_new(jplaylist, jtrack);

        int idx = it_current + 1;
        if (idx >= it_count)
        {
            //all track are queried, send back data
            json_object_set_new(jplayer,
                                "items",
                                jplaylist);
            result_lambda(jplayer);
        }
        else
        {
            getNextPlaylistItem(player, jplayer, jplaylist, idx, it_count, result_lambda);
        }
    });
}

AudioPlayer *JsonApi::getAudioPlayer(json_t *jdata, string &err)
{
    AudioPlayer *player = nullptr;
    err.clear();

    string id = jansson_string_get(jdata, "player_id");
    if (id == "")
    {
        err = "empty player id";
        return player;
    }

    int pid;
    Utils::from_string(id, pid);
    if (pid < 0 || pid >= AudioManager::Instance().get_size())
    {
        err = "unkown player_id";
    }
    else
    {
        player = AudioManager::Instance().get_player(pid);
    }

    return player;
}

void JsonApi::audioGetDbStats(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    player->get_database()->getStats([=](AudioPlayerData adata)
    {
        adata.params.Add("audio_action", "get_stats");
        result_lambda(adata.params.toJson());
    });
}

void JsonApi::audioGetPlaylistSize(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    player->get_playlist_size([=](AudioPlayerData adata)
    {
        adata.params.Add("audio_action", "get_playlist_size");
        Params p = {{"playlist_size", Utils::to_string(adata.ivalue)}};
        result_lambda(p.toJson());
    });
}

void JsonApi::audioGetTime(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    player->get_current_time([=](AudioPlayerData adata)
    {
        adata.params.Add("audio_action", "get_time");
        Params p = {{"time_elapsed", Utils::to_string(adata.dvalue)}};
        result_lambda(p.toJson());
    });
}

void JsonApi::audioGetPlaylistItem(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string it = jansson_string_get(jdata, "item");
    if (it.empty() || !Utils::is_of_type<int>(it))
    {
        Params p = {{"error", "wrong item" }};
        result_lambda(p.toJson());
        return;
    }

    int item;
    Utils::from_string(it, item);

    player->get_playlist_item(item, [=](AudioPlayerData data)
    {
        result_lambda(data.params.toJson());
    });
}

void JsonApi::audioGetCoverInfo(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    player->get_album_cover([=](AudioPlayerData data)
    {
        Params p = {{ "cover", data.svalue }};
        result_lambda(p.toJson());
    });
}

json_t *JsonApi::processDbResult(const AudioPlayerData &data)
{
    json_t *ret = json_object();
    json_t *aret = json_array();
    string scount;

    const vector<Params> &vp = data.vparams;
    for (const Params &p: vp)
    {
        if (p.Exists("count"))
            scount = p["count"];
        json_array_append(aret, p.toJson());
    }

    if (scount == "0")
        json_array_clear(aret);

    if (!scount.empty())
        json_object_set_new(ret, "total_count", json_string(scount.c_str()));
    json_object_set_new(ret, "items", aret);

    return ret;
}

void JsonApi::audioDbGetAlbums(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getAlbums([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetAlbumArtistItem(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    string artist_id = jansson_string_get(jdata, "artist_id");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getArtistsAlbums([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count, artist_id);
}

void JsonApi::audioDbGetYearAlbums(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    string year = jansson_string_get(jdata, "year");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getYearsAlbums([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count, year);
}

void JsonApi::audioDbGetGenreArtists(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    string genre = jansson_string_get(jdata, "genre");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getGenresArtists([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count, genre);
}

void JsonApi::audioDbGetAlbumTitles(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    string album_id = jansson_string_get(jdata, "album_id");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getAlbumsTitles([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count, album_id);
}

void JsonApi::audioDbGetPlaylistTitles(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    string pl_id = jansson_string_get(jdata, "playlist_id");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getPlaylistsTracks([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count, pl_id);
}

void JsonApi::audioDbGetArtists(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getArtists([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetYears(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getYears([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetGenres(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getGenres([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetPlaylists(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getPlaylists([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetMusicFolder(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    string folder_id = jansson_string_get(jdata, "folder_id");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getMusicFolder([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count, folder_id);
}

void JsonApi::audioDbGetSearch(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    string search = jansson_string_get(jdata, "search");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getSearch([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count, search);
}

void JsonApi::audioDbGetRadios(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getRadios([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetRadioItems(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    string radio_id = jansson_string_get(jdata, "radio_id");
    string item_id = jansson_string_get(jdata, "item_id");
    string search = jansson_string_get(jdata, "search");

    player->get_database()->getRadiosItems([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count, radio_id, item_id, search);
}

void JsonApi::audioDbGetTrackInfos(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string trackid = jansson_string_get(jdata, "track_id");

    player->get_database()->getTrackInfos([=](AudioPlayerData data)
    {
        result_lambda(data.params.toJson());
    }, trackid);
}

json_t *JsonApi::buildJsonGetTimerange(const Params &jParam)
{
    InPlageHoraire *o = dynamic_cast<InPlageHoraire *>(ListeRoom::Instance().get_input(jParam["id"]));
    if (!o)
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    json_t *ret = json_object();
    json_t *jarr = json_array();

    for (int day = 0;day < 7;day++)
    {
        vector<TimeRange> h;
        if (day == 0) h = o->getLundi();
        if (day == 1) h = o->getMardi();
        if (day == 2) h = o->getMercredi();
        if (day == 3) h = o->getJeudi();
        if (day == 4) h = o->getVendredi();
        if (day == 5) h = o->getSamedi();
        if (day == 6) h = o->getDimanche();
        for (uint i = 0;i < h.size();i++)
            json_array_append(jarr, h[i].toParams(day).toJson());
    }

    json_object_set(ret, "ranges", jarr);

    stringstream ssmonth;
    ssmonth << o->months;
    string str = ssmonth.str();
    std::reverse(str.begin(), str.end());
    json_object_set_new(ret, "months", json_string(str.c_str()));

    return ret;
}

json_t *JsonApi::buildJsonSetTimerange(json_t *jdata)
{
    string id = jansson_string_get(jdata, "id");
    InPlageHoraire *o = dynamic_cast<InPlageHoraire *>(ListeRoom::Instance().get_input(id));
    if (!o)
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    o->clear();

    size_t idx;
    json_t *value;

    json_array_foreach(json_object_get(jdata, "ranges"), idx, value)
    {
        Params p;
        jansson_decode_object(value, p);

        TimeRange tr(p);

        cout << "Adding timerange: " << p.toString() << endl;

        if (p["day"] == "1") o->AddLundi(tr);
        if (p["day"] == "2") o->AddMardi(tr);
        if (p["day"] == "3") o->AddMercredi(tr);
        if (p["day"] == "4") o->AddJeudi(tr);
        if (p["day"] == "5") o->AddVendredi(tr);
        if (p["day"] == "6") o->AddSamedi(tr);
        if (p["day"] == "7") o->AddDimanche(tr);
    }

    //set months
    string m = jansson_string_get(jdata, "months");
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

    if (o->getAutoScenarioPtr())
    {
        EventManager::create(CalaosEvent::EventScenarioChanged,
                             { { "id", o->getAutoScenarioPtr()->getIOScenario()->get_param("id") } });
    }

    //Resave config
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "success", "true" }};
    return p.toJson();
}

json_t *JsonApi::buildAutoscenarioList(json_t *jdata)
{
    VAR_UNUSED(jdata);
    json_t *jret = json_object();
    json_t *jarr = json_array();

    for (auto it: ListeRoom::Instance().getAutoScenarios())
    {
        json_array_append(jarr, it->toJson());
    }

    json_object_set(jret, "scenarios", jarr);
    return jret;
}

json_t *JsonApi::buildAutoscenarioGet(json_t *jdata)
{
    string id = jansson_string_get(jdata, "id");
    Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_input(id));
    if (!sc || !sc->getAutoScenario())
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    return sc->toJson();
}

json_t *JsonApi::buildAutoscenarioCreate(json_t *jdata)
{
    Params params;
    params.Add("auto_scenario", Calaos::get_new_scenario_id());
    params.Add("name", jansson_string_get(jdata, "name", _("New unnamed scenario")));
    params.Add("visible", jansson_string_get(jdata, "visible", "false"));
    params.Add("cycle", jansson_string_get(jdata, "cycle", "false"));
    params.Add("disabled", jansson_string_get(jdata, "disabled", "true"));

    Room *room = ListeRoom::Instance().searchRoomByNameAndType(
               jansson_string_get(jdata, "room_name"),
               jansson_string_get(jdata, "room_type"));
    if (!room)
    {
        cWarningDom("network") << "Wrong room: " << jansson_string_get(jdata, "room_name")
                               << " - " << jansson_string_get(jdata, "room_type");
        room = ListeRoom::Instance().get_room(0);
    }

    //create scenario object
    params.Add("type", "scenario");
    Input *in = ListeRoom::Instance().createInput(params, room);
    Scenario *scenario = dynamic_cast<Scenario *>(in);
    scenario->getAutoScenario()->checkScenarioRules();

    size_t idx;
    json_t *value;

    json_array_foreach(json_object_get(jdata, "steps"), idx, value)
    {
        int index_act;

        if (jansson_string_get(value, "step_type") == "standard")
        {
            double pause;
            from_string(jansson_string_get(value, "step_pause"), pause);
            scenario->getAutoScenario()->addStep(pause);
            index_act = idx;
        }
        else
        {
            index_act = AutoScenario::END_STEP;
        }

        size_t idx_act;
        json_t *value_act;

        json_array_foreach(json_object_get(value, "actions"), idx_act, value_act)
        {

            string id_out = jansson_string_get(value_act, "id");
            Output *out = ListeRoom::Instance().get_output(id_out);
            if (out)
                scenario->getAutoScenario()->addStepAction(index_act, out,
                                                           jansson_string_get(value_act, "action"));
        }
    }

    EventManager::create(CalaosEvent::EventScenarioAdded,
                         { { "id", scenario->get_param("id") } });

    //Resave config, auto scenarios have probably created/deleted ios and rules
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "id", scenario->get_param("id") }};
    return p.toJson();
}
json_t *JsonApi::buildAutoscenarioDelete(json_t *jdata)
{
    string id = jansson_string_get(jdata, "id");
    Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_input(id));
    if (!sc || !sc->getAutoScenario())
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    sc->getAutoScenario()->deleteAll();

    //delete the scenario IO
    ListeRoom::Instance().deleteIO(dynamic_cast<Input *>(sc));

    EventManager::create(CalaosEvent::EventScenarioDeleted,
                         { { "id", id } });

    //Resave config
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "success", "true" }};
    return p.toJson();
}

json_t *JsonApi::buildAutoscenarioModify(json_t *jdata)
{
    string id = jansson_string_get(jdata, "id");
    Scenario *scenario = dynamic_cast<Scenario *>(ListeRoom::Instance().get_input(id));
    if (!scenario || !scenario->getAutoScenario())
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    scenario->getAutoScenario()->deleteRules();

    Params params;
    params.Add("auto_scenario", Calaos::get_new_scenario_id());
    params.Add("name", jansson_string_get(jdata, "name", _("New unnamed scenario")));
    params.Add("visible", jansson_string_get(jdata, "visible", "false"));
    params.Add("cycle", jansson_string_get(jdata, "cycle", "false"));
    params.Add("disabled", jansson_string_get(jdata, "disabled", "true"));

    Room *room = ListeRoom::Instance().searchRoomByNameAndType(
               jansson_string_get(jdata, "room_name"),
               jansson_string_get(jdata, "room_type"));

    size_t idx;
    json_t *value;

    json_array_foreach(json_object_get(jdata, "steps"), idx, value)
    {
        int index_act;

        if (jansson_string_get(value, "step_type") == "standard")
        {
            double pause;
            from_string(jansson_string_get(value, "step_pause"), pause);
            scenario->getAutoScenario()->addStep(pause);
            index_act = idx;
        }
        else
        {
            index_act = AutoScenario::END_STEP;
        }

        size_t idx_act;
        json_t *value_act;

        json_array_foreach(json_object_get(value, "actions"), idx_act, value_act)
        {

            string id_out = jansson_string_get(value_act, "id");
            Output *out = ListeRoom::Instance().get_output(id_out);
            if (out)
                scenario->getAutoScenario()->addStepAction(index_act, out,
                                                           jansson_string_get(value_act, "action"));
        }
    }

    //Check for changes
    if (params["name"] != scenario->get_param("name"))
    {
        scenario->set_param("name", params["name"]);

        EventManager::create(CalaosEvent::EventInputChanged,
                             { { "id", scenario->get_param("id") },
                               { "name", params["name"] }});

        EventManager::create(CalaosEvent::EventOutputChanged,
                             { { "id", scenario->get_param("id") },
                               { "name", params["name"] }});
    }

    if (params["visible"] != scenario->get_param("visible"))
    {
        scenario->set_param("visible", params["visible"]);

        EventManager::create(CalaosEvent::EventInputChanged,
                             { { "id", scenario->get_param("id") },
                               { "visible", params["visible"] }});

        EventManager::create(CalaosEvent::EventOutputChanged,
                             { { "id", scenario->get_param("id") },
                               { "visible", params["visible"] }});
    }

    Room *old_room = ListeRoom::Instance().getRoomByInput(scenario);
    if (room != old_room)
    {
        if (room)
        {
            old_room->RemoveInputFromRoom(dynamic_cast<Input *>(scenario));
            old_room->RemoveOutputFromRoom(dynamic_cast<Output *>(scenario));
            room->AddInput(dynamic_cast<Input *>(scenario));
            room->AddOutput(dynamic_cast<Output *>(scenario));

            EventManager::create(CalaosEvent::EventRoomChanged,
                                 { { "input_id_added", scenario->get_param("id") },
                                   { "room_name", room->get_name() },
                                   { "room_type", room->get_type() }});

            EventManager::create(CalaosEvent::EventRoomChanged,
                                 { { "output_id_added", scenario->get_param("id") },
                                   { "room_name", room->get_name() },
                                   { "room_type", room->get_type() }});
        }
    }

    if (params["cycle"] != scenario->get_param("cycle"))
    {
        if (params["cycle"] == "true")
            scenario->getAutoScenario()->setCycling(true);
        else
            scenario->getAutoScenario()->setCycling(false);
    }

    if (params["disabled"] != scenario->get_param("disabled"))
    {
        if (params["disabled"] == "true")
            scenario->getAutoScenario()->setDisabled(true);
        else
            scenario->getAutoScenario()->setDisabled(false);
    }

    scenario->getAutoScenario()->checkScenarioRules();

    EventManager::create(CalaosEvent::EventScenarioChanged,
                         { { "id", scenario->get_param("id") } });

    //Resave config
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "success", "true" }};
    return p.toJson();
}

json_t *JsonApi::buildAutoscenarioAddSchedule(json_t *jdata)
{
    string id = jansson_string_get(jdata, "id");
    Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_input(id));
    if (!sc || !sc->getAutoScenario())
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    sc->getAutoScenario()->addSchedule();

    EventManager::create(CalaosEvent::EventScenarioChanged,
                         { { "id", sc->get_param("id") } });

    //Resave config
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "id", sc->getAutoScenario()->getIOPlage()->get_param("id") }};
    return p.toJson();
}

json_t *JsonApi::buildAutoscenarioDelSchedule(json_t *jdata)
{
    string id = jansson_string_get(jdata, "id");
    Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_input(id));
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
