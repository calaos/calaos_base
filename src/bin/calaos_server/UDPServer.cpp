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
#include <UDPServer.h>
#include "uvw/src/uvw.hpp"

using namespace Calaos;

UDPServer::UDPServer(int p):
    port(p)
{
    createUdpSocket();

    cDebugDom("network") << "Starting UDP server...";
    cDebugDom("network") << "Listenning on port " << port;
}

UDPServer::~UDPServer()
{
    handleSrv->stop();
    handleSrv->close();
}

void UDPServer::createUdpSocket()
{
    auto loop = uvw::Loop::getDefault();
    handleSrv = loop->resource<uvw::UDPHandle>();

    handleSrv->on<uvw::UDPDataEvent>([this](const uvw::UDPDataEvent &ev, auto &)
    {
        string s(ev.data.get(), ev.length);
        this->processRequest(s, ev.sender.ip, ev.sender.port);
    });

    handleSrv->on<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, uvw::UDPHandle &)
    {
        cErrorDom("network") << "UDP server error: " << ev.what();
    });

    handleSrv->bind("0.0.0.0", port, uvw::UDPHandle::Bind::REUSEADDR);
    handleSrv->recv();
}

void UDPServer::processRequest(const string &request, const string &remoteIp, unsigned int remotePort)
{
    if (request == "CALAOS_DISCOVER")
    {
        cDebugDom("network") << "Got a CALAOS_DISCOVER";

        cDebugDom("network") << "Remote IP: " << remoteIp;

        string ip = TCPSocket::GetLocalIPFor(remoteIp);
        if (ip != "")
        {
            string packet = "CALAOS_IP ";
            packet += ip;

            handleSrv->send(remoteIp, remotePort, (char *)packet.c_str(), packet.length());
            handleSrv->once<uvw::SendEvent>([](const uvw::SendEvent &, uvw::UDPHandle &)
            {
                cDebugDom("network") << "Answer packet sent.";
            });

            cDebugDom("network") << "Sending answer: " << packet;
        }
        else
        {
            cErrorDom("network") << "No interface found corresponding to network : " << remoteIp;
        }
    }
    else if (request.compare(0, 9, "WAGO INT ") == 0)
    {
        Params p;
        p.Parse(request);

        int input = atoi(p["2"].c_str());
        bool val;
        from_string(p["3"], val);

        cInfoDom("network")
                << "received input " << Utils::to_string(input)
                << " state=" << Utils::to_string(val);

        //send a signal
        Utils::signal_wago.emit(remoteIp, input, val, "std");
    }
    else if (request.compare(0, 9, "WAGO KNX ") == 0)
    {
        Params p;
        p.Parse(request);

        int input = atoi(p["2"].c_str());
        bool val;
        from_string(p["3"], val);

        cInfoDom("network")
                << "received input " << Utils::to_string(input)
                << " state=" << Utils::to_string(val);

        //send a signal
        Utils::signal_wago.emit(remoteIp, input, val, "knx");
    }
}
