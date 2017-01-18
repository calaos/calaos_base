/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
#include "HttpServer.h"
#include "WebSocket.h"
#include "uvw/src/uvw.hpp"

HttpServer::HttpServer(int p):
    port(p)
{
    auto loop = uvw::Loop::getDefault();
    handleSrv = loop->resource<uvw::TcpHandle>();
    handleSrv->bind("0.0.0.0", port);
    handleSrv->listen();

    handleSrv->on<uvw::ListenEvent>([this](const uvw::ListenEvent &, uvw::TcpHandle &)
    {
        //new client has just connected to us
        std::shared_ptr<uvw::TcpHandle> client = uvw::Loop::getDefault()->resource<uvw::TcpHandle>();
        handleSrv->accept(*client);
        addConnection(client);
    });
    handleSrv->once<uvw::CloseEvent>([](const uvw::CloseEvent &, uvw::TcpHandle &) mutable
    {
        cDebugDom("network") << "Closed";
    });
    handleSrv->on<uvw::ErrorEvent>([](const uvw::ErrorEvent &ev, uvw::TcpHandle &)
    {
        cDebugDom("network") << "Error: " << ev.what();
    });

    cDebugDom("network") << "Init TCP Server";
    cInfoDom("network")  << "Listening on port " << port;
}

HttpServer::~HttpServer()
{
    handleSrv->stop();
    handleSrv->close();
}

void HttpServer::addConnection(const std::shared_ptr<uvw::TcpHandle> &client)
{
    cDebugDom("network")
            << "Got a new connection from address "
            << client->peer().ip;

    WebSocket *conn = new WebSocket(client);
    connections[client] = conn;

    //When peer closed the connection, remove it from our map and close it
    client->on<uvw::EndEvent>([client](const uvw::EndEvent &, auto &)
    {
        client->close();
    });

    //When connection is closed
    client->on<uvw::CloseEvent>([this, client](const uvw::CloseEvent &, auto &)
    {
        this->delConnection(client);
    });

    client->on<uvw::DataEvent>([this, client](const uvw::DataEvent &ev, auto &)
    {
        this->getDataConnection(client, ev.data.get(), ev.length);
    });

    client->read();
}

void HttpServer::delConnection(const std::shared_ptr<uvw::TcpHandle> &client)
{
    cDebugDom("network")
            << "Connection from adress "
            << client->peer().ip << " closed.";

    auto it = connections.find(client);
    if (it == connections.end())
    {
        cCriticalDom("network") << "Can't find corresponding HttpClient !";
        return;
    }

    delete it->second;
    connections.erase(it);
}

void HttpServer::getDataConnection(const std::shared_ptr<uvw::TcpHandle> &client, void *data, int size)
{
    string d((char *)data, size);

    cDebugDom("network")
            << "Got data from client at address "
            << client->peer().ip;

    auto it = connections.find(client);
    if (it == connections.end())
    {
        cCriticalDom("network") << "Can't find corresponding HttpClient !";

        return;
    }

    it->second->ProcessData(d);
}

void HttpServer::disconnectAll()
{
    for (auto iter = connections.begin();iter != connections.end();iter++)
    {
        WebSocket *ws = (*iter).second;
        ws->sendCloseFrame(WebSocketFrame::CloseCodeNormal, "Shutting down", true);
    }
}
