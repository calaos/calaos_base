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
    handleSrv->once<uvw::ErrorEvent>([](const uvw::ErrorEvent &ev, uvw::TcpHandle &h)
    {
        h.stop();
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
    string ipAddr = client->peer().ip;
    cDebugDom("network")
            << "Got a new connection from address "
            << ipAddr;

    WebSocket *conn = new WebSocket(client);
    connections.push_back(conn);

    //When peer closed the connection, remove it from our map and close it
    client->once<uvw::EndEvent>([](const uvw::EndEvent &, auto &h)
    {
        h.close();
    });

    //When connection is closed
    client->once<uvw::CloseEvent>([ipAddr, conn, this](const uvw::CloseEvent &, auto &)
    {
        cDebugDom("network")
                << "Connection from adress "
                << ipAddr << " closed.";
        connections.remove(conn);
        delete conn;
    });

    client->on<uvw::DataEvent>([ipAddr, conn](const uvw::DataEvent &ev, auto &)
    {
        cDebugDom("network")
                << "Got data from client at address "
                << ipAddr;
        conn->ProcessData(string(ev.data.get(), ev.length));
    });

    client->read();
}

void HttpServer::disconnectAll()
{
    for (auto iter = connections.begin();iter != connections.end();iter++)
    {
        WebSocket *ws = (*iter);
        ws->sendCloseFrame(WebSocketFrame::CloseCodeNormal, "Shutting down", true);
    }
}
