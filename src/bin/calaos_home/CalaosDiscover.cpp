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
#include "CalaosDiscover.h"

static Eina_Bool _con_server_data(void *data, int type, Ecore_Con_Event_Client_Data *ev)
{
        CalaosDiscover *o = reinterpret_cast<CalaosDiscover *>(data);

        if (ev && (o != ecore_con_server_data_get(ecore_con_client_server_get(ev->client))))
                return ECORE_CALLBACK_PASS_ON;

        if (o)
                o->dataGet(ecore_con_client_server_get(ev->client), ev->data, ev->size);
        else
                Utils::logger("network") << Priority::CRIT
                                << "CalaosDiscover(): _con_server_data, failed to get object !"
                                << log4cpp::eol;

        return ECORE_CALLBACK_RENEW;
}

CalaosDiscover::CalaosDiscover():
        econ(NULL),
        econ_sender(NULL),
        timer(NULL),
        connection(NULL)
{
        event_handler_data_get = ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_DATA, (Ecore_Event_Handler_Cb)_con_server_data, this);

        econ = ecore_con_server_add(ECORE_CON_REMOTE_UDP, "0.0.0.0", BCAST_UDP_PORT, this);
        ecore_con_server_data_set(econ, this);

        econ_sender = ecore_con_server_connect(ECORE_CON_REMOTE_BROADCAST, "255.255.255.255", BCAST_UDP_PORT, this);

        timerDiscover();

        timer = new EcoreTimer(1.,
                (sigc::slot<void>)sigc::mem_fun(*this, &CalaosDiscover::timerDiscover));
}

CalaosDiscover::~CalaosDiscover()
{
        DELETE_NULL_FUNC(ecore_event_handler_del, event_handler_data_get);

        DELETE_NULL(timer);
        DELETE_NULL_FUNC(ecore_con_server_del, econ);
        DELETE_NULL_FUNC(ecore_con_server_del, econ_sender);
        DELETE_NULL(connection);
}

void CalaosDiscover::timerDiscover()
{
        cDebugDom("network") << "CalaosDiscover: try to discover server..." << log4cpp::eol;

        string packet = "CALAOS_DISCOVER";
        if (!ecore_con_server_send(econ_sender, packet.c_str(), packet.length()))
        {
                econ_sender = ecore_con_server_connect(ECORE_CON_REMOTE_BROADCAST, "0.0.0.0", BCAST_UDP_PORT, this);
                ecore_con_server_send(econ_sender, packet.c_str(), packet.length());
        }
}

void CalaosDiscover::dataGet(Ecore_Con_Server *server, void *data, int size)
{
        if (server != econ) return;

        string msg((char *)data, size);

        cDebugDom("network") << "CalaosDiscover: DataServer: some data arrived msg: \"" << msg << "\"" << log4cpp::eol;

        if (msg.substr(0, 10) == "CALAOS_IP " && !connection)
        {
                msg.erase(0, 10);
                address = msg;

                DELETE_NULL(timer);
                EcoreTimer::singleShot(0.0, sigc::mem_fun(*this, &CalaosDiscover::delayDel));

                connection = new CalaosConnection(address, true);
                connection->connection_ok.connect(sigc::mem_fun(*this, &CalaosDiscover::loginSuccess));
                connection->error_login.connect(sigc::mem_fun(*this, &CalaosDiscover::loginFailed));
        }
}

void CalaosDiscover::delayDel()
{
        DELETE_NULL_FUNC(ecore_con_server_del, econ);
        DELETE_NULL_FUNC(ecore_con_server_del, econ_sender);
}

void CalaosDiscover::loginSuccess()
{
        cDebugDom("network") << "CalaosDiscover: Login to host " << address << " successfully" << log4cpp::eol;

        DELETE_NULL(connection);

        server_found.emit(address);
}

void CalaosDiscover::loginFailed()
{
        cDebugDom("network") << "CalaosDiscover: Wrong login/password on host " << address << log4cpp::eol;

        DELETE_NULL(connection);

        login_error.emit(address);

        //Restart timer to search again for a valid server
        timer = new EcoreTimer(1.,
                (sigc::slot<void>)sigc::mem_fun(*this, &CalaosDiscover::timerDiscover));
}
