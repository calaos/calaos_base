/******************************************************************************
**  Copyright (c) 2006-2011, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Calaos; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include "CalaosConnection.h"

static Eina_Bool _ecore_con_handler_add(void *data, int type, Ecore_Con_Event_Server_Add *ev)
{
    CalaosConnection *o = reinterpret_cast<CalaosConnection *>(data);

    if (ev && (o != ecore_con_server_data_get(ev->server)))
        return ECORE_CALLBACK_PASS_ON;

    if (o)
        o->addConnection(ev->server);
    else
        cCriticalDom("network.connection")
                << "CalaosConnection(): _con_server_add, failed to get object !"
                   ;

    return ECORE_CALLBACK_RENEW;
}

static Eina_Bool _ecore_con_handler_del(void *data, int type, Ecore_Con_Event_Server_Del *ev)
{
    CalaosConnection *o = reinterpret_cast<CalaosConnection *>(data);

    if (ev && (o != ecore_con_server_data_get(ev->server)))
        return ECORE_CALLBACK_PASS_ON;

    if (o)
        o->delConnection(ev->server);
    else
        cCriticalDom("network.connection")
                << "CalaosConnection(): _con_server_del, failed to get object !"
                   ;

    return ECORE_CALLBACK_RENEW;
}

static Eina_Bool _ecore_con_handler_data_get(void *data, int type, Ecore_Con_Event_Server_Data *ev)
{
    CalaosConnection *o = reinterpret_cast<CalaosConnection *>(data);

    if (ev && (o != ecore_con_server_data_get(ev->server)))
        return ECORE_CALLBACK_PASS_ON;

    if (o)
        o->dataGet(ev->server, ev->data, ev->size);
    else
        cCriticalDom("network.connection")
                << "CalaosConnection(): _con_server_data, failed to get object !"
                   ;

    return ECORE_CALLBACK_RENEW;
}

CalaosConnection::CalaosConnection(string h, bool no_listenner):
    econ(NULL),
    con_state(CALAOS_CON_NONE),
    host(h),
    timeout(NULL),
    sendInProgress(false),
    listener(NULL)
{
    event_handler_data_get = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DATA, (Ecore_Event_Handler_Cb)_ecore_con_handler_data_get, this);
    event_handler_add = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_ADD, (Ecore_Event_Handler_Cb)_ecore_con_handler_add, this);
    event_handler_del = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DEL, (Ecore_Event_Handler_Cb)_ecore_con_handler_del, this);

    econ = ecore_con_server_connect(ECORE_CON_REMOTE_TCP,
                                    host.c_str(),
                                    TCP_LISTEN_PORT,
                                    this);
    ecore_con_server_data_set(econ, this);

    timeout = new EcoreTimer(TIMEOUT_CONNECT, (sigc::slot<void>)sigc::mem_fun(*this, &CalaosConnection::TimeoutTick));

    if (!no_listenner)
        listener = new CalaosListener(host);
}

CalaosConnection::~CalaosConnection()
{
    DELETE_NULL_FUNC(ecore_event_handler_del, event_handler_data_get);
    DELETE_NULL_FUNC(ecore_event_handler_del, event_handler_add);
    DELETE_NULL_FUNC(ecore_event_handler_del, event_handler_del);

    DELETE_NULL(listener);
    DELETE_NULL(timeout);
    DELETE_NULL_FUNC(ecore_con_server_del, econ);
}

void CalaosConnection::addConnection(Ecore_Con_Server *server)
{
    if (server != econ) return;

    if (con_state == CALAOS_CON_NONE)
    {
        con_state = CALAOS_CON_LOGIN;

        string cmd = "login ";

        //Get username/password
        string username = Utils::get_config_option("calaos_user");
        string password = Utils::get_config_option("calaos_password");

        if (Utils::get_config_option("cn_user") != "" &&
            Utils::get_config_option("cn_pass") != "")
        {
            username = Utils::get_config_option("cn_user");
            password = Utils::get_config_option("cn_pass");
        }

        cmd += Utils::url_encode(username) + " ";
        cmd += Utils::url_encode(password);
        cmd += "\r\n";

        cDebugDom("network.connection") << "CalaosConnection: trying to log in.";

        ecore_con_server_send(econ, cmd.c_str(), cmd.length());
    }
}

void CalaosConnection::delConnection(Ecore_Con_Server *server)
{
    if (server != econ) return;

    if (con_state == CALAOS_CON_LOGIN)
    {
        error_login.emit();

        cCriticalDom("network.connection") << "CalaosConnection: Login failed !";

        return;
    }

    lost_connection.emit();

    cCriticalDom("network.connection") << "CalaosConnection: Connection closed !";
}

void CalaosConnection::dataGet(Ecore_Con_Server *server, void *data, int size)
{
    if (server != econ) return;

    string msg((char *)data, size);

    if (con_state == CALAOS_CON_LOGIN)
    {
        con_state = CALAOS_CON_OK;

        cDebugDom("network.connection") << "CalaosConnection: Successfully logged in.";

        connection_ok.emit();
    }
    else if (con_state == CALAOS_CON_OK)
    {
        DELETE_NULL(timeout)

                //Clean string
                while( (msg[msg.length() - 1] == '\n' || msg[msg.length() - 1] == '\r')
                && !msg.empty() )
                msg.erase(msg.length() - 1, 1);

        cDebugDom("network.connection") << "CalaosConnection: Received: " << msg;

        //Here we split command result.
        vector<string> v;
        split(msg, v);

        for_each(v.begin(), v.end(), UrlDecode());

        CalaosCmd &cmd = commands.front();
        CommandDone_sig sig;
        sig.connect(cmd.callback);
        sig.emit(true, v, cmd.user_data);

        commands.pop();

        sendAndDequeue();
    }
}

void CalaosConnection::TimeoutTick()
{
    if (con_state == CALAOS_CON_NONE)
    {
        timeout_connect.emit();

        cCriticalDom("network.connection") << "CalaosConnection: Timeout connecting to " << host;
    }

    if (con_state == CALAOS_CON_OK)
    {
        cCriticalDom("network.connection") << "CalaosConnection: Timeout waiting answer...";

        vector<string> v;
        CalaosCmd &cmd = commands.front();
        CommandDone_sig sig;
        sig.connect(cmd.callback);
        sig.emit(false, v, cmd.user_data);

        commands.pop();

        sendAndDequeue();
    }

    if (timeout)
    {
        delete timeout;
        timeout = NULL;
    }
}

void CalaosConnection::SendCommand(string scmd, CommandDone_cb callback, void *data)
{
    CalaosCmd cmd;

    cmd.command = scmd;
    cmd.callback = callback;
    cmd.user_data = data;

    commands.push(cmd);

    sendAndDequeue();
}

void CalaosConnection::SendCommand(string scmd)
{
    CalaosCmd cmd;

    cmd.command = scmd;
    cmd.noCallback = true;

    commands.push(cmd);

    sendAndDequeue();
}

void CalaosConnection::sendAndDequeue()
{
    if (commands.empty() || commands.front().inProgress)
        return;

    CalaosCmd &cmd = commands.front();
    cmd.inProgress = true;

    if (!timeout)
        timeout = new EcoreTimer(TIMEOUT_SEND, (sigc::slot<void>)sigc::mem_fun(*this, &CalaosConnection::TimeoutTick));

    cDebugDom("network.connection") << "CalaosConnection: Sending command: " << cmd.command;

    cmd.command += "\n\r";
    ecore_con_server_send(econ, cmd.command.c_str(), cmd.command.length());
}
