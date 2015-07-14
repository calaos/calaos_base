/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include "CalaosConnection.h"

string CalaosConnection::calaosServerIp = string();

CalaosCmd::CalaosCmd(CommandDone_cb cb, void *d, CalaosConnection *p, const string &id):
    callback(cb),
    user_data(d),
    msgid(id),
    parent(p)
{
    timeout = new EcoreTimer(TIMEOUT_SEND, [=]()
    {
        DELETE_NULL(timeout);
        parent->timeoutSend(this);
    });
}

CalaosConnection::CalaosConnection(string h):
    con_state(CALAOS_CON_NONE),
    host(h),
    timeout(NULL)
{
    calaosServerIp = host.c_str(); //do a deep copy or it will fail later

    wsocket = new WebSocketClient();
    wsocket->websocketConnected.connect(sigc::mem_fun(*this, &CalaosConnection::onConnected));
    wsocket->websocketDisconnected.connect(sigc::mem_fun(*this, &CalaosConnection::onDisconnected));
    wsocket->textMessageReceived.connect(sigc::mem_fun(*this, &CalaosConnection::onMessageReceived));
    wsocket->openConnection("ws://" + host + ":5454/api");

    timeout = new EcoreTimer(TIMEOUT_CONNECT, (sigc::slot<void>)sigc::mem_fun(*this, &CalaosConnection::timeoutConnect));
}

CalaosConnection::~CalaosConnection()
{
    for (auto it: commands)
        delete it.second;

    delete timeout;
    delete wsocket;
}

void CalaosConnection::getCredentials(string &username, string &password)
{
    //Get username/password
    username = Utils::get_config_option("calaos_user");
    password = Utils::get_config_option("calaos_password");

    if (Utils::get_config_option("cn_user") != "" &&
        Utils::get_config_option("cn_pass") != "")
    {
        username = Utils::get_config_option("cn_user");
        password = Utils::get_config_option("cn_pass");
    }
}

void CalaosConnection::onConnected()
{
    if (con_state == CALAOS_CON_NONE)
    {
        con_state = CALAOS_CON_LOGIN;

        //Get username/password
        string username;
        string password;
        getCredentials(username, password);

        json_t *jlogin = json_pack("{s:s, s:s}",
                                   "cn_user", username.c_str(),
                                   "cn_pass", password.c_str());
        sendJson("login", jlogin);

        cDebugDom("network.connection") << "trying to log in.";
    }
}

void CalaosConnection::onDisconnected()
{
    if (con_state == CALAOS_CON_LOGIN)
    {
        error_login.emit();

        cCriticalDom("network.connection") << "Login failed !";

        return;
    }

    lost_connection.emit();

    cCriticalDom("network.connection") << "Connection closed !";
}

void CalaosConnection::sendJson(const string &msg_type, json_t *jdata, const string &client_id)
{
    json_t *jroot = json_object();
    json_object_set_new(jroot, "msg", json_string(msg_type.c_str()));
    if (client_id != "")
        json_object_set_new(jroot, "msg_id", json_string(client_id.c_str()));
    if (jdata)
        json_object_set_new(jroot, "data", jdata);

    wsocket->sendTextMessage(jansson_to_string(jroot));
}

void CalaosConnection::onMessageReceived(const string &data)
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
    jansson_decode_object(jroot, jsonRoot);

    json_t *jdata = json_object_get(jroot, "data");
    if (jdata)
        jansson_decode_object(jdata, jsonData);

    //Format: { msg: "type", msg_id: id, data: {} }

    if (jsonRoot["msg"] == "login" &&
        jsonData["success"] == "true" &&
        con_state == CALAOS_CON_LOGIN)
    {
        DELETE_NULL(timeout);
        con_state = CALAOS_CON_OK;

        cDebugDom("network.connection") << "Successfully logged in.";

        connection_ok.emit();
        return;
    }

    //a message id was sent, get the corresponding
    //query and call the callback
    if (!jsonRoot["msg_id"].empty())
    {
        if (commands.find(jsonRoot["msg_id"]) == commands.end())
        {
            cErrorDom("network") << "msg_id " << jsonRoot["msg_id"] << " not found in commands.";
        }
        else
        {
            CalaosCmd *cmd = commands[jsonRoot["msg_id"]];
            cmd->callback(jdata, cmd->user_data);
            commands.erase(jsonRoot["msg_id"]);
            delete cmd;
        }
    }

    if (jsonRoot["msg"] == "event")
    {
        Params eventData;
        jansson_decode_object(json_object_get(jdata, "data"), eventData);
        string ev = jsonData["type_str"];

        if (ev == "input_added" ||
            ev == "output_added")
            notify_io_new.emit(ev, eventData);

        else if (ev == "input_deleted" ||
                 ev == "output_deleted")
            notify_io_delete.emit(ev, eventData);

        else if (ev == "input_changed" ||
                 ev == "output_changed" ||
                 ev == "input_prop_deleted" ||
                 ev == "output_prop_deleted" ||
                 ev == "timerange_changed")
            notify_io_change.emit(ev, eventData);

        else if (ev == "room_added")
            notify_room_new.emit(ev, eventData);

        else if (ev == "room_deleted")
            notify_room_delete.emit(ev, eventData);

        else if (ev == "room_changed" ||
                 ev == "room_prop_deleted")
            notify_room_change.emit(ev, eventData);

        else if (ev == "scenario_deleted")
            notify_scenario_del.emit(ev, eventData);

        else if (ev == "scenario_added")
            notify_scenario_add.emit(ev, eventData);

        else if (ev == "scenario_changed")
            notify_scenario_change.emit(ev, eventData);

        else if (ev == "audio_song_changed" ||
                 ev == "playlist_tracks_added" ||
                 ev == "playlist_tracks_deleted" ||
                 ev == "playlist_tracks_moved" ||
                 ev == "playlist_reload" ||
                 ev == "playlist_cleared" ||
                 ev == "audio_status_changed" ||
                 ev == "audio_volume_changed")
            notify_audio_change.emit(ev, eventData);
    }
}

void CalaosConnection::timeoutConnect()
{
    if (con_state == CALAOS_CON_NONE)
    {
        timeout_connect.emit();

        cCriticalDom("network.connection") << "Timeout connecting to " << host;
    }

    DELETE_NULL(timeout);
}

void CalaosConnection::timeoutSend(CalaosCmd *cmd)
{
    if (con_state == CALAOS_CON_OK)
    {
        cCriticalDom("network.connection") << "Timeout waiting for answer... give up.";

        cmd->callback(nullptr, cmd->user_data);
        commands.erase(cmd->msgid);
        delete cmd;
    }
}

void CalaosConnection::sendCommand(const string &msg, const Params &param)
{
    sendJson(msg, param.toJson());
}

void CalaosConnection::sendCommand(const string &msg, const Params &param,
                                   CommandDone_cb callback,
                                   void *data)
{
    string clientid = Utils::to_string(rand() & 0xffff);
    while (commands.find(clientid) != commands.end())
        clientid = Utils::to_string(rand() & 0xffff);

    CalaosCmd *cmd = new CalaosCmd(callback, data, this, clientid);
    commands[clientid] = cmd;
    sendJson(msg, param.toJson(), clientid);
}

void CalaosConnection::sendCommand(const string &msg, json_t *jdata,
                                   CommandDone_cb callback,
                                   void *data)
{
    string clientid = Utils::to_string(rand() & 0xffff);
    while (commands.find(clientid) != commands.end())
        clientid = Utils::to_string(rand() & 0xffff);

    CalaosCmd *cmd = new CalaosCmd(callback, data, this, clientid);
    commands[clientid] = cmd;
    sendJson(msg, jdata, clientid);
}
