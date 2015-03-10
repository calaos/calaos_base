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
