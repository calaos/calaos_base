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
#include <UDPServer.h>

using namespace Calaos;

static Eina_Bool _ecore_con_handler_data_get(void *data, int type, Ecore_Con_Event_Client_Data *ev);
static Eina_Bool _ecore_con_handler_error(void *data, int type, Ecore_Con_Event_Server_Error *ev);

UDPServer::UDPServer(int p):
    port(p),
    udp_server(NULL),
    udp_broadcast(NULL),
    udp_sender(NULL)
{
    createUdpSocket();

    event_handler_data_get = ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_DATA, (Ecore_Event_Handler_Cb)_ecore_con_handler_data_get, this);
    event_handler_error = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_ERROR, (Ecore_Event_Handler_Cb)_ecore_con_handler_error, this);

    cDebugDom("network") << "Starting UDP server...";
    cDebugDom("network") << "Listenning on port " << port;
}

UDPServer::~UDPServer()
{
    if (udp_broadcast)
        ecore_con_server_del(udp_broadcast);

    if (udp_sender)
        ecore_con_server_del(udp_sender);

    if (udp_server)
        ecore_con_server_del(udp_server);

    ecore_event_handler_del(event_handler_data_get);
    ecore_event_handler_del(event_handler_error);
}

void UDPServer::createUdpSocket()
{
    if (!udp_server)
    {
        udp_server = ecore_con_server_add((Ecore_Con_Type)(ECORE_CON_REMOTE_UDP), "0.0.0.0", port, this);
        ecore_con_server_data_set(udp_server, this);
    }
}

Eina_Bool _ecore_con_handler_data_get(void *data, int type, Ecore_Con_Event_Client_Data *ev)
{
    UDPServer *udpserver = reinterpret_cast<UDPServer *>(data);

    if (ev && (udpserver != ecore_con_server_data_get(ecore_con_client_server_get(ev->client))))
    {
        return ECORE_CALLBACK_PASS_ON;
    }

    if (udpserver)
    {
        string d((char *)ev->data, ev->size);

        udpserver->ProcessRequest(ev->client, d);
    }
    else
    {
        cCriticalDom("network")  << "failed to get UDPServer object !" ;
    }

    return ECORE_CALLBACK_RENEW;
}

Eina_Bool _ecore_con_handler_error(void *data, int type, Ecore_Con_Event_Server_Error *ev)
{
    UDPServer *udpserver = reinterpret_cast<UDPServer *>(data);

    if (ev && (udpserver != ecore_con_server_data_get(ev->server)))
    {
        return ECORE_CALLBACK_PASS_ON;
    }

    if (udpserver)
    {
        udpserver->ProcessError(ev->server);
    }

    return ECORE_CALLBACK_RENEW;
}

void UDPServer::ProcessRequest(Ecore_Con_Client *client, string request)
{
    if (request == "CALAOS_DISCOVER")
    {
        cDebugDom("network") << "Got a CALAOS_DISCOVER";

        string remote_ip = ecore_con_client_ip_get(client);

        cDebugDom("network") << "Remote IP: " << remote_ip;

        string ip = TCPSocket::GetLocalIPFor(remote_ip);
        if (ip != "")
        {
            string packet = "CALAOS_IP ";
            packet += ip;


            //sock->Broadcast(packet, BCAST_UDP_PORT);
            //sock->SendTo(packet.c_str(), packet.length(), BCAST_UDP_PORT, remote_ip);
            if (!udp_broadcast)
            {
                udp_broadcast = ecore_con_server_connect(ECORE_CON_REMOTE_BROADCAST,
                                                         "255.255.255.255",
                                                         BCAST_UDP_PORT,
                                                         this);
                ecore_con_server_data_set(udp_broadcast, this);
            }

            if (udp_sender)
            {
                ecore_con_server_del(udp_sender);
            }

            udp_sender = ecore_con_server_connect(ECORE_CON_REMOTE_UDP,
                                                  remote_ip.c_str(),
                                                  BCAST_UDP_PORT,
                                                  this);
            ecore_con_server_data_set(udp_sender, this);

            ecore_con_server_send(udp_broadcast, packet.c_str(), packet.length() + 1);
            ecore_con_server_send(udp_sender, packet.c_str(), packet.length() + 1);

            //Broadcast response
            /*TCPSocket *sock = new TCPSocket();
                        sock->Create(UDP);
                        sock->Broadcast(packet, BCAST_UDP_PORT);
                        sock->SendTo(packet.c_str(), packet.length(), BCAST_UDP_PORT, remote_ip);
                        sock->Close();
                        delete sock;*/

            ecore_con_client_send(client, packet.c_str(), packet.length() + 1);

            cDebugDom("network") << "Sending answer: " << packet;
        }
        else
        {
            cErrorDom("network") << "No interface found corresponding to network : "
                    << remote_ip;
        }
    }
    else if (request.compare(0, 9, "WAGO INT ") == 0)
    {
        Params p;
        p.Parse(request);

        int input = atoi(p["2"].c_str());
        bool val;
        from_string(p["3"], val);

        cInfoDom("network")
                << "received input " << Utils::to_string(input)
                << " state=" << Utils::to_string(val);

        //send a signal
        Utils::signal_wago.emit(std::string(ecore_con_client_ip_get(client)), input, val, "std");
    }
    else if (request.compare(0, 9, "WAGO KNX ") == 0)
    {
        Params p;
        p.Parse(request);

        int input = atoi(p["2"].c_str());
        bool val;
        from_string(p["3"], val);

        cInfoDom("network")
                << "received input " << Utils::to_string(input)
                << " state=" << Utils::to_string(val);

        //send a signal
        Utils::signal_wago.emit(std::string(ecore_con_client_ip_get(client)), input, val, "knx");
    }
}

void UDPServer::ProcessError(Ecore_Con_Server *srv)
{
    if (srv == udp_broadcast)
        udp_broadcast = nullptr;
    else if (srv == udp_sender)
        udp_sender = nullptr;
    else if (srv == udp_server)
    {
        udp_server = nullptr;
        createUdpSocket();
    }
}
