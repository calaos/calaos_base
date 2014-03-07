/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
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
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include <TCPServer.h>

static Eina_Bool _ecore_con_handler_client_add(void *data, int type, Ecore_Con_Event_Client_Add *ev);
static Eina_Bool _ecore_con_handler_data_get(void *data, int type, Ecore_Con_Event_Client_Data *ev);
static Eina_Bool _ecore_con_handler_client_del(void *data, int type, Ecore_Con_Event_Client_Del *ev);

TCPServer::TCPServer(int p): port(p), tcp_server(NULL)
{
    /* Setup ecore con TCP server and callbacks */

    tcp_server = ecore_con_server_add(ECORE_CON_REMOTE_TCP, "0.0.0.0", port, this);
    ecore_con_server_data_set(tcp_server, this);

    event_handler_client_add = ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_ADD, (Ecore_Event_Handler_Cb)_ecore_con_handler_client_add, this);
    event_handler_client_del = ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_DATA, (Ecore_Event_Handler_Cb)_ecore_con_handler_data_get, this);
    event_handler_data_get = ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_DEL, (Ecore_Event_Handler_Cb)_ecore_con_handler_client_del, this);

    cDebugDom("network")
            << "TCPServer::TCPServer(): Init TCP Server";
    cInfoDom("network")
            << "TCPServer::TCPServer(): Listening on port " << port;
}

TCPServer::~TCPServer()
{
    ecore_con_server_del(tcp_server);
    tcp_server = NULL;

    ecore_event_handler_del(event_handler_client_add);
    ecore_event_handler_del(event_handler_client_del);
    ecore_event_handler_del(event_handler_data_get);

    cDebugDom("network")
            << "TCPServer::~TCPServer(): Ok";
}

Eina_Bool _ecore_con_handler_client_add(void *data, int type, Ecore_Con_Event_Client_Add *ev)
{
    TCPServer *tcpserver = reinterpret_cast<TCPServer *>(data);

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
        cCriticalDom("network")
                << "TCPServer(): _ecore_con_handler_client_add, failed to get TCPServer object !"
                   ;
    }

    return ECORE_CALLBACK_RENEW;
}

Eina_Bool _ecore_con_handler_client_del(void *data, int type, Ecore_Con_Event_Client_Del *ev)
{
    TCPServer *tcpserver = reinterpret_cast<TCPServer *>(data);

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
        cCriticalDom("network")
                << "TCPServer(): _ecore_con_handler_client_del, failed to get TCPServer object !"
                   ;
    }

    return ECORE_CALLBACK_CANCEL;
}

Eina_Bool _ecore_con_handler_data_get(void *data, int type, Ecore_Con_Event_Client_Data *ev)
{
    TCPServer *tcpserver = reinterpret_cast<TCPServer *>(data);

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
        cCriticalDom("network")
                << "TCPServer(): _ecore_con_handler_data_get, failed to get TCPServer object !"
                   ;
    }

    return ECORE_CALLBACK_RENEW;
}

void TCPServer::addConnection(Ecore_Con_Client *client)
{
    cDebugDom("network")
            << "TCPServer::addConnection(): Got a new connection from address "
            << ecore_con_client_ip_get(client)
               ;

    TCPConnection *conn = new TCPConnection(client);
    connections[client] = conn;
}

void TCPServer::delConnection(Ecore_Con_Client *client)
{
    cDebugDom("network")
            << "TCPServer::delConnection(): Connection from adress "
            << ecore_con_client_ip_get(client) << " closed."
               ;

    map<Ecore_Con_Client *, TCPConnection *>::iterator it = connections.find(client);
    if (it == connections.end())
    {
        cCriticalDom("network")
                << "TCPServer::delConnection(): Can't find corresponding TCPConnection !"
                   ;

        return;
    }

    delete it->second;
    connections.erase(it);
}

void TCPServer::getDataConnection(Ecore_Con_Client *client, void *data, int size)
{
    string d((char *)data, size);

    cDebugDom("network")
            << "TCPServer::getDataConnection(): Got data from client at address "
            << ecore_con_client_ip_get(client)
               ;

    map<Ecore_Con_Client *, TCPConnection *>::iterator it = connections.find(client);
    if (it == connections.end())
    {
        cCriticalDom("network")
                << "TCPServer::getDataConnection(): Can't find corresponding TCPConnection !"
                   ;

        return;
    }

    it->second->ProcessData(d);
}
