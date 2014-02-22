/******************************************************************************
**  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#include <Zibase.h>

ZibaseManager Zibase::zibasemaps;

Eina_Bool zibase_udpClientData(void *data, int type, Ecore_Con_Event_Client_Data *ev)
{
        Zibase *z = reinterpret_cast<Zibase *>(data);

        if (ev && ev->client && (z != ecore_con_server_data_get(ecore_con_client_server_get(ev->client))))
                return ECORE_CALLBACK_PASS_ON;

        if (z) z->udpClientData(ev);

        return ECORE_CALLBACK_RENEW;
}

Eina_Bool zibase_udpListenData(void *data, int type, Ecore_Con_Event_Server_Data *ev)
{
        Zibase *z = reinterpret_cast<Zibase *>(data);

        if (ev && ev->server && (z != ecore_con_server_data_get(ev->server)))
                return ECORE_CALLBACK_PASS_ON;

        if (z) z->udpListenData(ev);

        return ECORE_CALLBACK_RENEW;
}

Zibase::Zibase(std::string h, int p):
                host(h),
                port(p),
                econ_client(nullptr),
                econ_listen(nullptr)
{
        //Ecore handler
        event_handler_data_cl = ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_DATA, (Ecore_Event_Handler_Cb)zibase_udpClientData, this);
        event_handler_data_listen = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DATA, (Ecore_Event_Handler_Cb)zibase_udpListenData, this);

        //Create listening udp server on local port to receive frame from zibase
        econ_listen = ecore_con_server_add(ECORE_CON_REMOTE_UDP,
                                           "0.0.0.0", //listen from anyone
                                           port,
                                           this);
        ecore_con_server_data_set(econ_listen, this);

        //Create udp socket to send data (discover zibase, registering, etc...)
        econ_client = ecore_con_server_connect(ECORE_CON_REMOTE_UDP,
                                               host.c_str(), //zibase host from io.xml
                                               ZIBASE_UDP_PORT,
                                               this);
        ecore_con_server_data_set(econ_client, this);

        Utils::logger("zibase") << Priority::INFO << "Zibase(" << host << "," << port << "): Ok" << log4cpp::eol;
}

Zibase::~Zibase()
{
        ecore_event_handler_del(event_handler_data_cl);
        ecore_event_handler_del(event_handler_data_listen);
        ecore_con_server_del(econ_client);
        ecore_con_server_del(econ_listen);

        Utils::logger("zibase") << Priority::INFO << "Zibase::~Zibase(): Ok" << log4cpp::eol;
}

Zibase &Zibase::Instance(std::string h, int p)
{
        for (uint i = 0;i < zibasemaps.maps.size();i++)
        {
                if (zibasemaps.maps[i]->get_host() == h &&
                    zibasemaps.maps[i]->get_port() == p)
                {
                        return *zibasemaps.maps[i];
                }
        }

        // Create a new zibase object
        Zibase *zbase = new Zibase(h, p);
        zibasemaps.maps.push_back(zbase);

        return *zibasemaps.maps[zibasemaps.maps.size() - 1];
}

void Zibase::stopAllZibase()
{
        std::for_each(zibasemaps.maps.begin(), zibasemaps.maps.end(), Delete());
        zibasemaps.maps.clear();
}

void Zibase::udpClientData(Ecore_Con_Event_Client_Data *ev)
{

}

void Zibase::udpListenData(Ecore_Con_Event_Server_Data *ev)
{
/*
 TODO: here handle udp packet and emit the signal to all connected IO classes

        ZibaseInfoSensor *sensor;

        ...

        sig_newframe.emit(sensor);

*/
}

