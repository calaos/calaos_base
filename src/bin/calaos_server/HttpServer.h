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
#ifndef S_HttpServer_H
#define S_HttpServer_H

#include "Calaos.h"
#include "WebSocket.h"
#include "HttpClient.h"
#include <unordered_map>

using namespace std;

namespace uvw {
//Forward declare classes here to prevent long build time
//because of uvw.hpp being header only
class TcpHandle;
}

class HttpServer
{
private:
    int port;

    std::shared_ptr<uvw::TcpHandle> handleSrv;

    list<WebSocket *> connections;

    //Connections opened per client identity (X-Forwarded-For through haproxy,
    //TCP peer otherwise, see TransportLimits::effectiveClientIp). Entries only
    //exist while at least one connection of that client is opened, so the map
    //is bounded by TransportLimits::maxConnections().
    unordered_map<string, size_t> ipConnections;

    HttpServer(int port); //port to listen

    void addConnection(const std::shared_ptr<uvw::TcpHandle> &client);

public:
    static HttpServer &Instance(int port = 0)
    {
        static HttpServer server(port);

        return server;
    }
    ~HttpServer();

    void disconnectAll();

    //Per-client connection cap (TransportLimits::maxConnectionsPerIp).
    //trackClientIp() counts one more connection for ip and returns false when
    //the cap is reached (nothing counted then). Every successful call must be
    //balanced by one releaseClientIp() with the same ip (HttpClient does both).
    bool trackClientIp(const string &ip);
    void releaseClientIp(const string &ip);
};
#endif
