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
#include <WagoHeartbeat.h>

using namespace Calaos;

WagoHeartbeat::WagoHeartbeat():
                active(true),
                type(HB_SETIP)
{
        Utils::logger("root") << Priority::INFO << "Starting WagoHeartbeat..." << log4cpp::eol;

        timer = new EcoreTimer(0.1,
                        (sigc::slot<void>)sigc::mem_fun(*this, &WagoHeartbeat::TimerTick) );
}

WagoHeartbeat::~WagoHeartbeat()
{
        delete timer;

        list<Ecore_Con_Server *>::iterator iter = connections.begin();
        for (;iter != connections.end();iter++)
        {
                Ecore_Con_Server *c = *iter;
                ecore_con_server_del(c);
        }

        connections.clear();

        Utils::logger("root") << Priority::DEBUG
                        << "WagoHeartbeat: Closing connections. " << log4cpp::eol;
}

void WagoHeartbeat::SendIP()
{
        vector<WagoMap> &v = WagoMap::get_maps();
        for (int i = 0;i < v.size();i++)
        {
                bool found_ip = false;
                string ip;
                for (int j = 0;j < 4 && !found_ip;j++)
                {
                        if (j == 3)
                                ip = TCPSocket::GetLocalIP("lo"); //This is only for wago_simulator
                        else
                                ip = TCPSocket::GetLocalIP("eth" + Utils::to_string(j));

                        if (ip == "") continue;
                        vector<string> splitter, splitter2;
                        Utils::split(ip, splitter, ".", 4);
                        Utils::split(v[i].get_host(), splitter2, ".", 4);
                        if (splitter[0] == splitter2[0] &&
                            splitter[1] == splitter2[1] &&
                            splitter[2] == splitter2[2])
                                found_ip = true;
                }

                if (found_ip)
                {
                        string cmd = "WAGO_SET_SERVER_IP ";
                        cmd += ip;

                        ecore_con_server_send(getConnection(v[i].get_host()), cmd.c_str(), cmd.size() + 1);
                }
                else
                        Utils::logger("root") << Priority::DEBUG
                                        << "WagoHeartbeat: No interface found corresponding to network : "
                                        << v[i].first << log4cpp::eol;
        }
}

void WagoHeartbeat::SendHeartbeat()
{
        string heartbeat = "WAGO_HEARTBEAT";

        vector<WagoMap> &v = WagoMap::get_maps();
        for (int i = 0;i < v.size();i++)
        {
                ecore_con_server_send(getConnection(v[i].get_host()), heartbeat.c_str(), heartbeat.length() + 1);
        }
}

Ecore_Con_Server *WagoHeartbeat::getConnection(string host)
{
        Ecore_Con_Server *conn = NULL;

        list<Ecore_Con_Server *>::iterator iter = connections.begin();
        for (;iter != connections.end();iter++)
        {
                Ecore_Con_Server *c = *iter;
                if (host == ecore_con_server_name_get(c))
                {
                        conn = c;
                        break;
                }
        }

        if (!conn)
        {
                conn = ecore_con_server_connect(ECORE_CON_REMOTE_UDP,
                                                          host.c_str(),
                                                          WAGO_LISTEN_PORT,
                                                          this);
                connections.push_back(conn);
        }

        return conn;
}

void WagoHeartbeat::TimerTick()
{
        if (active)
        {
                if (type == HB_SETIP)
                        SendIP();
                else
                        SendHeartbeat();
        }

        if (type == HB_SETIP)
        {
                timer->Reset(0.5);
                type = HB_HEARTBEAT;
        }
        else
        {
                timer->Reset(10.0);
                type = HB_SETIP;
        }
}
