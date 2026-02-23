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
#ifndef AVRRoseNotifServer_H
#define AVRRoseNotifServer_H

#include "Calaos.h"

namespace uvw {
//Forward declare classes here to prevent long build time
//because of uvw.hpp being header only
class TcpHandle;
}

namespace Calaos
{

class AVRRose;

/*
 * HTTP server listening on port 9284 for push notifications from HiFi Rose devices.
 * The RS520 sends POST requests to /device_state_noti and /test with JSON payloads.
 * This server is shared between all AVRRose instances (singleton-like, managed by AVRRose).
 */
class AVRRoseNotifServer
{
private:
    static constexpr int NOTIF_PORT = 9284;

    std::shared_ptr<uvw::TcpHandle> listenHandle;
    list<AVRRose *> receivers;

    void handleClientData(const string &remoteIP, const string &rawData);
    void processHttpRequest(const string &remoteIP, const string &method,
                            const string &path, const string &body);

    AVRRose *findReceiverForIP(const string &remoteIP);

    // Build a minimal HTTP 200 response
    static string buildHttpResponse(const string &body);

public:
    AVRRoseNotifServer();
    ~AVRRoseNotifServer();

    void registerReceiver(AVRRose *receiver);
    void unregisterReceiver(AVRRose *receiver);
};

}

#endif // AVRRoseNotifServer_H
