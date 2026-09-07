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
#include <UDPServer.h>
#include "libuvw.h"

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

    auto subscribe = [this]()
    {
        handleSrv->on<uvw::UDPDataEvent>([this](const uvw::UDPDataEvent &ev, auto &)
        {
            string s(ev.data.get(), ev.length);
            this->processRequest(s, Calaos::unmappedLiteral(ev.sender.ip),
                                 ev.sender.port);
        });

        handleSrv->on<uvw::SendEvent>([this](const uvw::SendEvent &, uvw::UDPHandle &)
        {
            if (!pendingSends.empty())
                pendingSends.pop_front();
        });
    };

    auto listenAddr = Utils::get_config_option("listen_address");

    string refused;
    listenAddr = Calaos::listenAddressOrWildcard(listenAddr, refused);
    if (!refused.empty())
        cWarningDom("network") << "listen_address \"" << refused
                               << "\" is not an IP address, "
                               << Calaos::kWidenedListen;

    handleSrv = loop->resource<uvw::UDPHandle>();
    pendingSends.clear();
    subscribe();

    if (!Calaos::bindListenAddress(*handleSrv, listenAddr, port,
                                   uvw::UDPHandle::Bind::REUSEADDR))
    {
        cWarningDom("network") << "listen_address \"" << listenAddr
                               << "\" is not an address of this machine, "
                               << Calaos::kWidenedListen;

        //A second bind on the same handle would not do: libuv keeps the socket
        //it opened for the refused family. The subscriptions go with the handle.
        handleSrv->close();
        handleSrv = loop->resource<uvw::UDPHandle>();
        pendingSends.clear();
        subscribe();
        Calaos::bindListenAddress(*handleSrv, "0.0.0.0", port,
                                  uvw::UDPHandle::Bind::REUSEADDR);
    }

    /* Only now, and never before the binds above: a bind that fails is recovered
     * a few lines up, and this listener would announce a loss that did not
     * happen.
     *
     * uvw republishes the failure of a send request on the HANDLE, as the very
     * ErrorEvent a failed receive uses, so the callback cannot tell the two
     * apart on its own: the datagrams still owed a completion do it. A refused
     * send is one correspondent's business and must cost nothing else; stopping
     * the handle costs every input the box has, so the one case that still does
     * it says what it takes away. A receive that fails while a send is
     * outstanding is charged to the send - the only ambiguity left, and it errs
     * towards listening on.
     */
    handleSrv->on<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, uvw::UDPHandle &h)
    {
        if (!pendingSends.empty())
        {
            const string peer = pendingSends.front();
            pendingSends.pop_front();
            cErrorDom("network") << "UDP send to " << peer << " failed: "
                                 << ev.what() << ", still listening on port "
                                 << port;
            return;
        }

        h.stop();
        cErrorDom("network") << "UDP server error: " << ev.what();
        cErrorDom("network") << "no longer listening on port " << port
                             << ": discovery (CALAOS_DISCOVER), Wago inputs "
                                "(WAGO INT) and KNX inputs (WAGO KNX) stop here "
                                "until the server is restarted";
    });

    handleSrv->recv();
}

void UDPServer::sendTo(const string &ip, unsigned int remotePort,
                       const string &packet)
{
    pendingSends.push_back(ip + ":" + Utils::to_string(remotePort));
    Calaos::sendDatagram(*handleSrv, ip, remotePort,
                         (char *)packet.c_str(), packet.length());
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

            sendTo(remoteIp, remotePort, packet);
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
