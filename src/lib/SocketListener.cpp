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
#include <SocketListener.h>
#include <IPC.h>

using namespace CalaosNetwork;

static Eina_Bool _con_server_add(void *data, int type, Ecore_Con_Event_Server_Add *ev)
{
        SocketListener *o = reinterpret_cast<SocketListener *>(data);

        if (ev && (o != ecore_con_server_data_get(ev->server)))
        {
                return ECORE_CALLBACK_PASS_ON;
        }

        if (o)
        {
                o->addConnection();
        }
        else
        {
                Utils::logger("network") << Priority::CRIT
                                << "SocketListener(): _con_server_add, failed to get object !"
                                << log4cpp::eol;
        }

        return ECORE_CALLBACK_RENEW;
}

static Eina_Bool _con_server_del(void *data, int type, Ecore_Con_Event_Server_Del *ev)
{
        SocketListener *o = reinterpret_cast<SocketListener *>(data);

        if (ev && (o != ecore_con_server_data_get(ev->server)))
        {
                return ECORE_CALLBACK_PASS_ON;
        }

        if (o)
        {
                o->delConnection();
        }
        else
        {
                Utils::logger("network") << Priority::CRIT
                                << "SocketListener(): _con_server_del, failed to get object !"
                                << log4cpp::eol;
        }

        return ECORE_CALLBACK_RENEW;
}

static Eina_Bool _con_server_data(void *data, int type, Ecore_Con_Event_Server_Data *ev)
{
        SocketListener *o = reinterpret_cast<SocketListener *>(data);

        if (ev && (o != ecore_con_server_data_get(ev->server)))
        {
                return ECORE_CALLBACK_PASS_ON;
        }

        if (o)
        {
                o->dataGet(ev->data, ev->size);
        }
        else
        {
                Utils::logger("network") << Priority::CRIT
                                << "SocketListener(): _con_server_data, failed to get object !"
                                << log4cpp::eol;
        }

        return ECORE_CALLBACK_RENEW;
}

SocketListener::SocketListener(string _address, int _port):
                address(_address),
                port(_port),
                econ(NULL),
                login(false),
                timer(NULL)
{
        ecore_event_handler_add(ECORE_CON_EVENT_SERVER_ADD, (Ecore_Event_Handler_Cb)_con_server_add, this);
        ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DEL, (Ecore_Event_Handler_Cb)_con_server_del, this);
        ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DATA, (Ecore_Event_Handler_Cb)_con_server_data, this);

        timerReconnect();

        timer = new EcoreTimer(1000.,
                (sigc::slot<void>)sigc::mem_fun(*this, &SocketListener::timerReconnect));
}

SocketListener::~SocketListener()
{
        if (timer)
        {
                delete timer;
                timer = NULL;
        }

        ecore_con_server_del(econ);
}

void SocketListener::timerReconnect()
{
        Utils::logger("network") << Priority::DEBUG << "SocketListener: Connecting to " << address << ":" << port << log4cpp::eol;

        if (econ) ecore_con_server_del(econ);
        econ = ecore_con_server_connect(ECORE_CON_REMOTE_TCP, address.c_str(), port, this);
        ecore_con_server_data_set(econ, this);
}

void SocketListener::addConnection()
{
        if (timer)
        {
                delete timer;
                timer = NULL;
        }

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

        Utils::logger("network") << Priority::DEBUG << "SocketListener: trying to log in." << log4cpp::eol;

        ecore_con_server_send(econ, cmd.c_str(), cmd.length());
}

void SocketListener::delConnection()
{
        if (timer)
        {
                delete timer;
                timer = NULL;
        }

        if (login)
        {
                Utils::logger("network") << Priority::DEBUG << "SocketListener: Wrong login/password." << log4cpp::eol;

                IPC::Instance().SendEvent("wrongPassword", "wrongPassword", NULL);

                return;
        }

        Utils::logger("network") << Priority::WARN << "SocketListener: Connection closed !" << log4cpp::eol;
        Utils::logger("network") << Priority::WARN << "SocketListener: Trying to reconnect..." << log4cpp::eol;

        timer = new EcoreTimer(5 / 100.,
                (sigc::slot<void>)sigc::mem_fun(*this, &SocketListener::timerReconnect));
}

void SocketListener::dataGet(void *data, int size)
{
        string msg((char *)data, size);

        if (login)
        {
                login = false;

                Utils::logger("network") << Priority::DEBUG << "SocketListener: Successfully logged in." << log4cpp::eol;

                string cmd = "listen\n\r";
                ecore_con_server_send(econ, cmd.c_str(), cmd.length());

                IPC::Instance().SendEvent("CalaosNetwork::Notify", "reconnect", NULL);

                return;
        }

        if (msg.find('\n') == string::npos &&
            msg.find('\r') == string::npos)
        {
                //We have not a complete paquet yet, buffurize it.
                buffer += msg;

                Utils::logger("network") << Priority::DEBUG << "SocketListener: Bufferize data." << log4cpp::eol;

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

        Utils::logger("network") << Priority::DEBUG << "SocketListener: Got " << tokens.size() << " messages." << log4cpp::eol;

        for(int i = 0; i < tokens.size(); i++)
                processMessage(tokens[i]);
}

void SocketListener::processMessage(string msg)
{
        Utils::logger("network") << Priority::DEBUG << "SocketListener: Message: \"" << msg << "\"" << log4cpp::eol;

        vector<string> msgSplit;

        split(msg, msgSplit, " ");

        if(msgSplit[0] == "output" || msgSplit[0] == "input")
                IPC::Instance().SendEvent("CalaosNetwork::Notify,io,change", msg, NULL);
        else if(msgSplit[0] == "new_output" || msgSplit[0] == "new_input")
                IPC::Instance().SendEvent("CalaosNetwork::Notify,io,new", msg, NULL);
        else if(msgSplit[0] == "delete_output" || msgSplit[0] == "delete_input")
                IPC::Instance().SendEvent("CalaosNetwork::Notify,io,del", msg, NULL);
        else if(msgSplit[0] == "modify_room")
                IPC::Instance().SendEvent("CalaosNetwork::Notify,room,modify", msg, NULL);
        else if(msgSplit[0] == "delete_room")
                IPC::Instance().SendEvent("CalaosNetwork::Notify,room,del", msg, NULL);
        else if(msgSplit[0] == "new_room")
                IPC::Instance().SendEvent("CalaosNetwork::Notify,room,new", msg, NULL);
}
