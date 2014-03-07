/******************************************************************************
**  Copyright (c) 2006-2012, Calaos. All Rights Reserved.
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
#include "CalaosListener.h"

static Eina_Bool _con_server_add(void *data, int type, Ecore_Con_Event_Server_Add *ev)
{
    CalaosListener *o = reinterpret_cast<CalaosListener *>(data);

    if (ev && (o != ecore_con_server_data_get(ev->server)))
        return ECORE_CALLBACK_PASS_ON;

    if (o)
        o->addConnection(ev->server);
    else
        cCriticalDom("network.listener")
                << "CalaosListener(): _con_server_add, failed to get object !"
                   ;

    return ECORE_CALLBACK_RENEW;
}

static Eina_Bool _con_server_del(void *data, int type, Ecore_Con_Event_Server_Del *ev)
{
    CalaosListener *o = reinterpret_cast<CalaosListener *>(data);

    if (ev && (o != ecore_con_server_data_get(ev->server)))
        return ECORE_CALLBACK_PASS_ON;

    if (o)
        o->delConnection(ev->server);
    else
        cCriticalDom("network.listener")
                << "CalaosListener(): _con_server_del, failed to get object !"
                   ;

    return ECORE_CALLBACK_RENEW;
}

static Eina_Bool _con_server_data(void *data, int type, Ecore_Con_Event_Server_Data *ev)
{
    CalaosListener *o = reinterpret_cast<CalaosListener *>(data);

    if (ev && (o != ecore_con_server_data_get(ev->server)))
        return ECORE_CALLBACK_PASS_ON;

    if (o)
        o->dataGet(ev->server, ev->data, ev->size);
    else
        cCriticalDom("network.listener")
                << "CalaosListener(): _con_server_data, failed to get object !"
                   ;

    return ECORE_CALLBACK_RENEW;
}

static Eina_Bool _con_server_error(void *data, int type, Ecore_Con_Event_Server_Data *ev)
{
    CalaosListener *o = reinterpret_cast<CalaosListener *>(data);

    if (ev && (o != ecore_con_server_data_get(ev->server)))
        return ECORE_CALLBACK_PASS_ON;

    if (o)
        o->errorConnection(ev->server);
    else
        cCriticalDom("network.listener")
                << "CalaosListener(): _con_server_error, failed to get object !"
                   ;

    return ECORE_CALLBACK_RENEW;
}

CalaosListener::CalaosListener(string _address):
    address(_address),
    econ(NULL),
    login(false)
{
    handler_add = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_ADD, (Ecore_Event_Handler_Cb)_con_server_add, this);
    handler_del = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DEL, (Ecore_Event_Handler_Cb)_con_server_del, this);
    handler_data = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DATA, (Ecore_Event_Handler_Cb)_con_server_data, this);
    handler_error = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_ERROR, (Ecore_Event_Handler_Cb)_con_server_error, this);

    //connect the listenner
    cDebugDom("network.listener") << "CalaosListener: Connecting to " << address << ":" << TCP_LISTEN_PORT;
    econ = ecore_con_server_connect(ECORE_CON_REMOTE_TCP, address.c_str(), TCP_LISTEN_PORT, this);
    ecore_con_server_data_set(econ, this);
}

CalaosListener::~CalaosListener()
{
    DELETE_NULL_FUNC(ecore_event_handler_del, handler_add);
    DELETE_NULL_FUNC(ecore_event_handler_del, handler_del);
    DELETE_NULL_FUNC(ecore_event_handler_del, handler_data);

    DELETE_NULL_FUNC(ecore_con_server_del, econ);
}

void CalaosListener::addConnection(Ecore_Con_Server *server)
{
    if (server != econ) return;

    //Login first
    login = true;
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

    cDebugDom("network.listener") << "CalaosListener: trying to log in.";

    ecore_con_server_send(econ, cmd.c_str(), cmd.length());
}

void CalaosListener::delConnection(Ecore_Con_Server *server)
{
    if (server != econ) return;

    if (login)
    {
        cDebugDom("network.listener") << "CalaosListener: Wrong login/password.";

        return;
    }

    cWarningDom("network.listener") << "CalaosListener: Connection closed !";
    cWarningDom("network.listener") << "CalaosListener: Trying to reconnect...";

    lost_connection.emit();
}

void CalaosListener::dataGet(Ecore_Con_Server *server, void *data, int size)
{
    if (server != econ) return;

    string msg((char *)data, size);

    if (login)
    {
        login = false;

        cDebugDom("network.listener") << "CalaosListener: Successfully logged in.";

        string cmd = "listen\n\r";
        ecore_con_server_send(econ, cmd.c_str(), cmd.length());

        return;
    }

    if (msg.find('\n') == string::npos &&
        msg.find('\r') == string::npos)
    {
        //We have not a complete paquet yet, buffurize it.
        buffer += msg;

        cDebugDom("network.listener") << "CalaosListener: Bufferize data.";

        return;
    }

    if (!buffer.empty())
    {
        msg = buffer;
        buffer.clear();
    }

    //Clean data string
    int i = msg.length() - 1;
    while ((msg[i] == '\n' || msg[i] == '\r' || msg[i] == '\0') && i >= 0) i--;

    vector<string> tokens;

    replace_str(msg, "\r\n", "\n");
    replace_str(msg, "\r", "\n");

    split(msg, tokens, "\n");

    cDebugDom("network.listener") << "CalaosListener: Got " << tokens.size() << " messages.";

    for(unsigned int j = 0; j < tokens.size(); j++)
        processMessage(tokens[j]);
}

void CalaosListener::errorConnection(Ecore_Con_Server *server)
{
    if (server != econ) return;

    econ = NULL;

    cWarningDom("network.listener") << "CalaosListener: Connection error !";
    cWarningDom("network.listener") << "CalaosListener: Trying to reconnect...";

    lost_connection.emit();

}

void CalaosListener::processMessage(string msg)
{
    cDebugDom("network.listener") << "CalaosListener: Message: \"" << msg << "\"";

    vector<string> msgSplit;

    split(msg, msgSplit, " ");

    if(msgSplit[0] == "output" || msgSplit[0] == "input" || msgSplit[0] == "input_range_change")
        notify_io_change.emit(msg);
    else if(msgSplit[0] == "new_output" || msgSplit[0] == "new_input")
        notify_io_new.emit(msg);
    else if(msgSplit[0] == "delete_output" || msgSplit[0] == "delete_input")
        notify_io_delete.emit(msg);
    else if(msgSplit[0] == "modify_room")
        notify_room_change.emit(msg);
    else if(msgSplit[0] == "delete_room")
        notify_room_delete.emit(msg);
    else if(msgSplit[0] == "new_room")
        notify_room_new.emit(msg);
    else if (msgSplit[0] == "audio" ||
             msgSplit[0] == "audio_playlist" ||
             msgSplit[0] == "audio_status" ||
             msgSplit[0] == "audio_volume")
    {
        notify_audio_change.emit(msg);
    }
    else if (msgSplit[0] == "new_scenario")
        notify_scenario_add.emit(msg);
    else if (msgSplit[0] == "delete_scenario")
        notify_scenario_del.emit(msg);
    else if (msgSplit[0] == "modify_scenario")
        notify_scenario_change.emit(msg);
}
