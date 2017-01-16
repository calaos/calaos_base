/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef S_UDPServer_H
#define S_UDPServer_H

#include <Calaos.h>
#include <tcpsocket.h>
#include <WagoMap.h>

namespace uvw {
//Forward declare classes here to prevent long build time
//because of uvw.hpp being header only
class UDPHandle;
}

class UDPServer
{
protected:
    int port;
    std::shared_ptr<uvw::UDPHandle> handleSrv;

    void createUdpSocket();
    void processRequest(const string &msg, const string &remoteIp, unsigned int port);

public:
    UDPServer(int port); //port to listen
    ~UDPServer();
};

#endif
