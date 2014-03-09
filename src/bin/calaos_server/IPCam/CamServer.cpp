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
#include <CamServer.h>

using namespace Calaos;

CamServer::CamServer(int p): port(p)
{
    cDebugDom("network");
}

CamServer::~CamServer()
{
    delete socket;
    socket = NULL;
    cDebugDom("network");
}

void CamServer::ThreadProc()
{
    cDebugDom("network") << "Init IPCam relay server";
    socket = new TCPSocket();

    socket->Create(port);
    socket->SetReuse();
    socket->Listen();
    cDebugDom("network") << "Listening on port " << port;
    quit = false;

    while (!quit)
    {
        socket->Accept();

        if (quit) break;

        cDebugDom("network") << "Got a connection from address "
                             << socket->GetRemoteIP();

        vector<CamConnection *>::iterator iter = connections.begin();
        for (uint i = 0;i < connections.size();iter++, i++)
        {
            //on supprime les connections qui sont finis
            if (connections[i]->get_end())
            {
                delete connections[i];
                connections.erase(iter);
            }
        }

        CamConnection *connection = new CamConnection(*socket);
        connections.push_back(connection);
        connection->Start(); //Start thread
    }
}

void CamServer::Clean()
{
    quit = true;
    if (socket)
    {
        socket->Shutdown();
        socket->Close();
    }

    for(uint i = 0;i < connections.size();i++)
        connections[i]->Clean();
}
