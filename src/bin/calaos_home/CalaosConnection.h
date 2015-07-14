/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef CALAOSCONNECTION_H
#define CALAOSCONNECTION_H

#include <Utils.h>

#include <Ecore.h>
#include <Ecore_Con.h>
#include <EcoreTimer.h>

#include "Jansson_Addition.h"
#include "WebSocketClient.h"

using namespace Utils;

#define TIMEOUT_CONNECT         60.0
#define TIMEOUT_SEND            60.0

typedef std::function<void(json_t *, void *)> CommandDone_cb;

class CalaosConnection;

class CalaosCmd
{
public:
    CalaosCmd(CommandDone_cb cb, void *d, CalaosConnection *p, const string &id);
    CalaosCmd(const CalaosCmd &other) = delete;
    ~CalaosCmd() { delete timeout; }

    CommandDone_cb callback = [](json_t*, void*){ /* no callback */ };
    void *user_data = nullptr;
    string msgid;

private:
    EcoreTimer *timeout = nullptr;
    CalaosConnection *parent;
};

class CalaosConnection: public sigc::trackable
{
private:
    enum { CALAOS_CON_NONE, CALAOS_CON_LOGIN, CALAOS_CON_OK };

    int con_state;

    string host;

    EcoreTimer *timeout;

    unordered_map<string, CalaosCmd *> commands;

    WebSocketClient *wsocket;

    friend class CalaosCmd;
    void timeoutConnect();
    void timeoutSend(CalaosCmd *cmd);

    void onConnected();
    void onDisconnected();
    void onMessageReceived(const string &msg);

    static string calaosServerIp;

public:
    CalaosConnection(string host);
    ~CalaosConnection();

    void sendJson(const string &msg_type, json_t *jdata, const string &client_id = string());
    void sendCommand(const string &msg, const Params &param);
    void sendCommand(const string &msg, const Params &param,
                     CommandDone_cb callback,
                     void *data = nullptr);
    void sendCommand(const string &msg, json_t *jdata,
                     CommandDone_cb callback,
                     void *data = nullptr);

    sigc::signal<void> error_login;
    sigc::signal<void> timeout_connect;
    sigc::signal<void> lost_connection;
    sigc::signal<void> connection_ok;

    //const string &msg_type, json_t *data, const string &client_id
    sigc::signal<void, const string &, json_t *, const string &> messageReceived;

    /* Events signals */

    //RoomModel signals
    sigc::signal<void, const string &, const Params &> notify_io_change;
    sigc::signal<void, const string &, const Params &> notify_io_new;
    sigc::signal<void, const string &, const Params &> notify_io_delete;
    sigc::signal<void, const string &, const Params &> notify_room_change;
    sigc::signal<void, const string &, const Params &> notify_room_new;
    sigc::signal<void, const string &, const Params &> notify_room_delete;

    //AudioModel signals
    sigc::signal<void, const string &, const Params &> notify_audio_change;

    //ScenarioModel signals
    sigc::signal<void, const string &, const Params &> notify_scenario_add;
    sigc::signal<void, const string &, const Params &> notify_scenario_del;
    sigc::signal<void, const string &, const Params &> notify_scenario_change;

    static void getCredentials(string &user, string &pass);
    static string getCalaosServerIp() { return calaosServerIp; }
};

#endif // CALAOSCONNECTION_H
