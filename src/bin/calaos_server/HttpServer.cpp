/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#include "McpProxyHandler.h"
#include "libuvw.h"

HttpServer::HttpServer(int p):
    port(p)
{
    auto listenAddr = Utils::get_config_option("listen_address");

    //Both fallbacks name the value they refused. An operator who narrowed the
    //listen and got every interface instead can only tell from the log: the
    //socket looks exactly like a deliberate 0.0.0.0.
    string refused;
    listenAddr = Calaos::listenAddressOrWildcard(listenAddr, refused);
    if (!refused.empty())
        cWarningDom("network") << "listen_address \"" << refused
                               << "\" is not an IP address, "
                               << Calaos::kWidenedListen;

    auto loop = uvw::Loop::getDefault();
    handleSrv = loop->resource<uvw::TcpHandle>();

    if (!Calaos::bindListenAddress(*handleSrv, listenAddr, port))
    {
        cWarningDom("network") << "listen_address \"" << listenAddr
                               << "\" is not an address of this machine, "
                               << Calaos::kWidenedListen;

        //A second bind on the same handle would not do: libuv keeps the socket
        //it opened for the refused family and reuses it whatever address comes
        //next.
        handleSrv->close();
        handleSrv = loop->resource<uvw::TcpHandle>();
        Calaos::bindListenAddress(*handleSrv, "0.0.0.0", port);
    }

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
    string ipAddr = Calaos::tcpPeerAddress(*client);
    cDebugDom("network")
            << "Got a new connection from address "
            << ipAddr;

    //Refuse the connection instead of accumulating clients until the server
    //runs out of file descriptors or memory. Nothing is evicted: an opened
    //connection may be an authenticated websocket receiving events, it is not
    //this code's place to decide it matters less than the new one.
    if (connections.size() >= TransportLimits::maxConnections())
    {
        cWarningDom("network")
                << "Refusing connection from address " << ipAddr << ", "
                << connections.size() << " connections are already opened";

        client->once<uvw::WriteEvent>([](const uvw::WriteEvent &, auto &h)
        {
            h.close();
        });
        client->once<uvw::ErrorEvent>([](const uvw::ErrorEvent &, auto &h)
        {
            h.close();
        });

        Calaos::McpProxyHandler::sendError(client, 503, "Too many connections\n");

        return;
    }

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

    //conn is guarded by its alive token: the handle can still deliver a
    //buffered DataEvent after the CloseEvent above deleted conn, and the raw
    //pointer would dangle (was relying on libuv delivery order).
    client->on<uvw::DataEvent>([ipAddr, conn, token = conn->aliveToken()](const uvw::DataEvent &ev, auto &)
    {
        if (token.expired()) return;

        cDebugDom("network")
                << "Got data from client at address "
                << ipAddr;
        conn->ProcessData(string(ev.data.get(), ev.length));
    });

    client->read();
}

bool HttpServer::trackClientIp(const string &ip)
{
    auto it = ipConnections.find(ip);
    if (it != ipConnections.end() &&
        it->second >= TransportLimits::maxConnectionsPerIp())
        return false;

    ipConnections[ip]++;
    return true;
}

void HttpServer::releaseClientIp(const string &ip)
{
    auto it = ipConnections.find(ip);
    if (it == ipConnections.end())
        return;

    if (--it->second == 0)
        ipConnections.erase(it);
}

void HttpServer::disconnectAll()
{
    for (auto iter = connections.begin();iter != connections.end();iter++)
    {
        WebSocket *ws = (*iter);
        ws->sendCloseFrame(WebSocketFrame::CloseCodeNormal, "Shutting down", true);
    }
}
