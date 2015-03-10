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
#include "JsonApiV3.h"
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
#include "WebSocket.h"

JsonApiV3::JsonApiV3(HttpClient *client):
    JsonApi(client)
{
}

JsonApiV3::~JsonApiV3()
{
}

void JsonApiV3::sendJson(const string &msg_type, json_t *data, const string &client_id)
{
    json_t *jroot = json_object();
    json_object_set_new(jroot, "msg", json_string(msg_type.c_str()));
    if (client_id != "")
        json_object_set_new(jroot, "msg_id", json_string(client_id.c_str()));
    if (data)
        json_object_set(jroot, "data", data);

    char *d = json_dumps(jroot, JSON_COMPACT | JSON_ENSURE_ASCII /*| JSON_ESCAPE_SLASH*/);
    if (!d)
    {
        cErrorDom("network") << "json_dumps failed!";
        json_decref(jroot);

        //close connection
        closeConnection.emit(WebSocket::CloseCodeNormal, "json_dumps failed!");

        return;
    }

    json_decref(jroot);
    string res(d);
    free(d);

    sendData.emit(res);
}

void JsonApiV3::processApi(const string &data)
{
    Params jsonRoot;
    Params jsonData;

    //parse the json data
    json_error_t jerr;
    json_t *jroot = json_loads(data.c_str(), 0, &jerr);

    if (!jroot || !json_is_object(jroot))
    {
        cDebugDom("network") << "Error loading json : " << jerr.text;
        return;
    }

    char *d = json_dumps(jroot, JSON_INDENT(4));
    if (d)
    {
        cDebugDom("network") << d;
        free(d);
    }

    //decode the json root object into jsonParam
    decodeJsonObject(jroot, jsonRoot);

    json_t *jdata = json_object_get(jroot, "data");
    if (jdata)
        decodeJsonObject(jdata, jsonData);

    //Format: { msg: "type", msg_id: id, data: {} }

    if (jsonRoot["msg"] == "login")
    {
        //check for if username/password matches
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
            closeConnection.emit(WebSocket::CloseCodeNormal, "login failed!");
        }
        else
        {
            json_t *jret = json_object();
            json_object_set_new(jret, "success", json_string("true"));

            sendJson("login", jret, jsonRoot["msg_id"]);

            loggedin = true;
        }
    }
    else if (loggedin) //only process other api if loggedin
    {
        if (jsonRoot["msg"] == "get_home")
            processGetHome(jsonData, jsonRoot["msg_id"]);

//        //check action now
//        if (jsonParam["action"] == "get_home")
//            processGetHome();
//        else if (jsonParam["action"] == "get_state")
//            processGetState(jroot);
//        else if (jsonParam["action"] == "set_state")
//            processSetState();
//        else if (jsonParam["action"] == "get_playlist")
//            processGetPlaylist();
//        else if (jsonParam["action"] == "poll_listen")
//            processPolling();
//        else if (jsonParam["action"] == "get_cover")
//            processGetCover();
//        else if (jsonParam["action"] == "get_camera_pic")
//            processGetCameraPic();
//        else if (jsonParam["action"] == "config")
//            processConfig(jroot);
    }


    json_decref(jroot);
    json_decref(jdata);
}

void JsonApiV3::processGetHome(const Params &jsonReq, const string &client_id)
{
    json_t *jret = nullptr;

    jret = json_pack("{s:o, s:o, s:o}",
                     "home", buildJsonHome(),
                     "cameras", buildJsonCameras(),
                     "audio", buildJsonAudio());

    sendJson("get_home", jret, client_id);
}
