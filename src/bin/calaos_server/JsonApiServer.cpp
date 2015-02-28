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
#include "JsonApiServer.h"
#include "WebSocket.h"

static Eina_Bool _ecore_con_handler_client_add(void *data, int type, Ecore_Con_Event_Client_Add *ev);
static Eina_Bool _ecore_con_handler_data_get(void *data, int type, Ecore_Con_Event_Client_Data *ev);
static Eina_Bool _ecore_con_handler_client_del(void *data, int type, Ecore_Con_Event_Client_Del *ev);
static Eina_Bool _ecore_con_handler_data_write(void *data, int type, Ecore_Con_Event_Client_Write *ev);

JsonApiServer::JsonApiServer(int p):
    port(p),
    tcp_server(NULL)
{
    /* Setup ecore con TCP server and callbacks */

    tcp_server = ecore_con_server_add(ECORE_CON_REMOTE_TCP, "0.0.0.0", port, this);
    ecore_con_server_data_set(tcp_server, this);

    event_handler_client_add = ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_ADD, (Ecore_Event_Handler_Cb)_ecore_con_handler_client_add, this);
    event_handler_client_del = ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_DATA, (Ecore_Event_Handler_Cb)_ecore_con_handler_data_get, this);
    event_handler_client_write = ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_WRITE, (Ecore_Event_Handler_Cb)_ecore_con_handler_data_write, this);
    event_handler_data_get = ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_DEL, (Ecore_Event_Handler_Cb)_ecore_con_handler_client_del, this);

    cDebugDom("network") << "Init TCP Server";
    cInfoDom("network")  << "Listening on port " << port;
}

JsonApiServer::~JsonApiServer()
{
    ecore_con_server_del(tcp_server);
    tcp_server = NULL;

    ecore_event_handler_del(event_handler_client_add);
    ecore_event_handler_del(event_handler_client_del);
    ecore_event_handler_del(event_handler_data_get);
    ecore_event_handler_del(event_handler_client_write);

    cDebugDom("network");
}

Eina_Bool _ecore_con_handler_client_add(void *data, int type, Ecore_Con_Event_Client_Add *ev)
{
    JsonApiServer *tcpserver = reinterpret_cast<JsonApiServer *>(data);

    if (ev && (tcpserver != ecore_con_server_data_get(ecore_con_client_server_get(ev->client))))
    {
        return ECORE_CALLBACK_PASS_ON;
    }

    if (tcpserver)
    {
        tcpserver->addConnection(ev->client);
    }
    else
    {
        cCriticalDom("network") << "failed to get JsonApiServer object !";
    }

    return ECORE_CALLBACK_RENEW;
}

Eina_Bool _ecore_con_handler_client_del(void *data, int type, Ecore_Con_Event_Client_Del *ev)
{
    JsonApiServer *tcpserver = reinterpret_cast<JsonApiServer *>(data);

    if (ev && (tcpserver != ecore_con_server_data_get(ecore_con_client_server_get(ev->client))))
    {
        return ECORE_CALLBACK_PASS_ON;
    }

    if (tcpserver)
    {
        tcpserver->delConnection(ev->client);
    }
    else
    {
        cCriticalDom("network") << "failed to get JsonApiServer object !";
    }

    return ECORE_CALLBACK_CANCEL;
}

Eina_Bool _ecore_con_handler_data_get(void *data, int type, Ecore_Con_Event_Client_Data *ev)
{
    JsonApiServer *tcpserver = reinterpret_cast<JsonApiServer *>(data);

    if (ev && (tcpserver != ecore_con_server_data_get(ecore_con_client_server_get(ev->client))))
    {
        return ECORE_CALLBACK_PASS_ON;
    }

    if (tcpserver)
    {
        tcpserver->getDataConnection(ev->client, ev->data, ev->size);
    }
    else
    {
        cCriticalDom("network") << "failed to get JsonApiServer object !";
    }

    return ECORE_CALLBACK_RENEW;
}

Eina_Bool _ecore_con_handler_data_write(void *data, int type, Ecore_Con_Event_Client_Write *ev)
{
    JsonApiServer *tcpserver = reinterpret_cast<JsonApiServer *>(data);

    if (ev && (tcpserver != ecore_con_server_data_get(ecore_con_client_server_get(ev->client))))
    {
        return ECORE_CALLBACK_PASS_ON;
    }

    if (tcpserver)
    {
        tcpserver->dataWritten(ev->client, ev->size);
    }
    else
    {
        cCriticalDom("network") << "failed to get JsonApiServer object !";
    }

    return ECORE_CALLBACK_RENEW;
}

void JsonApiServer::addConnection(Ecore_Con_Client *client)
{
    cDebugDom("network")
            << "Got a new connection from address "
            << ecore_con_client_ip_get(client);

    WebSocket *conn = new WebSocket(client);
    connections[client] = conn;
}

void JsonApiServer::delConnection(Ecore_Con_Client *client)
{
    cDebugDom("network")
            << "Connection from adress "
            << ecore_con_client_ip_get(client) << " closed.";

    map<Ecore_Con_Client *, WebSocket *>::iterator it = connections.find(client);
    if (it == connections.end())
    {
        cCriticalDom("network") << "Can't find corresponding JsonApiClient !";

        return;
    }

    delete it->second;
    connections.erase(it);
}

void JsonApiServer::getDataConnection(Ecore_Con_Client *client, void *data, int size)
{
    string d((char *)data, size);

    cDebugDom("network")
            << "Got data from client at address "
            << ecore_con_client_ip_get(client);

    map<Ecore_Con_Client *, WebSocket *>::iterator it = connections.find(client);
    if (it == connections.end())
    {
        cCriticalDom("network") << "Can't find corresponding JsonApiClient !";

        return;
    }

    it->second->ProcessData(d);
}

void JsonApiServer::dataWritten(Ecore_Con_Client *client, int size)
{
    cDebugDom("network")
            << Utils::to_string(size) << " bytes written"
            << " to client at address "
            << ecore_con_client_ip_get(client);

    map<Ecore_Con_Client *, WebSocket *>::iterator it = connections.find(client);
    if (it == connections.end())
    {
        cCriticalDom("network") << "Can't find corresponding JsonApiClient !";

        return;
    }

    it->second->DataWritten(size);
}

