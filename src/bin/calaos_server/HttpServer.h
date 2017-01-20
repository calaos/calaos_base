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
#ifndef S_HttpServer_H
#define S_HttpServer_H

#include "Calaos.h"
#include "WebSocket.h"
#include "HttpClient.h"

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

    map<std::shared_ptr<uvw::TcpHandle>, WebSocket *> connections;

    HttpServer(int port); //port to listen

    void addConnection(const std::shared_ptr<uvw::TcpHandle> &client);
    void delConnection(const std::shared_ptr<uvw::TcpHandle> &client);
    void getDataConnection(const std::shared_ptr<uvw::TcpHandle> &client, void *data, int size);

public:
    static HttpServer &Instance(int port = 0)
    {
        static HttpServer server(port);

        return server;
    }
    ~HttpServer();

    void disconnectAll();
};
#endif
