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

JsonApi::JsonApi(HttpClient *client):
    httpClient(client)
{
}

JsonApi::~JsonApi()
{
}

void JsonApi::decodeJsonObject(json_t *jroot, Params &params)
{
    const char *key;
    json_t *value;

    json_object_foreach(jroot, key, value)
    {
        string svalue;

        if (json_is_string(value))
            svalue = json_string_value(value);
        else if (json_is_boolean(value))
            svalue = json_is_true(value)?"true":"false";
        else if (json_is_number(value))
            svalue = Utils::to_string(json_number_value(value));

        params.Add(key, svalue);
    }
}

template<typename T>
json_t *JsonApi::buildJsonRoomIO(Room *room)
{
    json_t *jdata = json_array();

    for (int i = 0;i < room->get_size<T *>();i++)
    {
        json_t *jinput = json_object();
        T *io = room->get_io<T *>(i);

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

            json_object_set_new(jinput, param.c_str(),
                                json_string(value.c_str()));
        }

        json_array_append_new(jdata, jinput);
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
            else if (input->get_type() == TINT)
            {
                double dv;
                Utils::from_string(jParam["value"], dv);
                input->force_input_double(dv);
            }
            else if (input->get_type() == TSTRING)
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
            else if (output->get_type() == TINT)
            {
                double dv;
                Utils::from_string(jParam["value"], dv);
                success = output->set_value(dv);
            }
            else if (output->get_type() == TSTRING)
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
