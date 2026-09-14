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

#include <fcntl.h>

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
    if (rearmTimer)
    {
        rearmTimer->stop();
        rearmTimer->close();
    }

    handleSrv->stop();
    handleSrv->close();
}

/* Whether the number libuv polls still names something this process owns.
 *
 * It is the one question that separates a fault a read can come back from - the
 * descriptor is intact, the failure was the read's - from an accident in this
 * process's own descriptor table, where arming the read again is an abort()
 * inside uv_run and not an error anyone can report.
 *
 * It cannot see a number that was closed and HANDED OUT AGAIN: that one answers
 * "valid" and belongs to somebody else.
 */
bool UDPServer::descriptorIsStillOurs() const
{
    const int fd = (int)handleSrv->fileno();
    return fd >= 0 && fcntl(fd, F_GETFD) != -1;
}

void UDPServer::scheduleRearm()
{
    if (!rearmTimer)
    {
        rearmTimer = uvw::Loop::getDefault()->resource<uvw::TimerHandle>();
        rearmTimer->on<uvw::TimerEvent>([this](const uvw::TimerEvent &,
                                               uvw::TimerHandle &)
        {
            considerListeningAgain();
        });
    }

    /* Zero, which uvw documents as the next iteration of the loop. Arming the
     * read from inside the error callback brings back the very deluge the stop
     * is there to end - and it is the turn of the loop, never a delay, that was
     * measured to hand the queued datagrams back.
     *
     * Only ever armed from the poll phase, never from the timer callback:
     * libuv keeps draining its timer heap after a callback returns, so a timer
     * restarted from inside its own callback runs AGAIN in the same phase,
     * before the read it just armed has had a chance to fail.
     */
    rearmTimer->start(uvw::TimerHandle::Time{0}, uvw::TimerHandle::Time{0});
}

void UDPServer::considerListeningAgain()
{
    if (receiver != Receiver::Retrying)
        return;

    if (!descriptorIsStillOurs())
    {
        giveUpTheReceiver("the descriptor it was polling is gone from this "
                          "process, so arming the read again would abort it");
        return;
    }

    if (rearmsLeft == 0)
    {
        giveUpTheReceiver(Utils::to_string(REARM_ATTEMPTS) +
                          " attempts to listen again all failed");
        return;
    }

    //One attempt, and then nothing until it fails: a read that stays armed is
    //a read that worked, and there is no event for one.
    rearmsLeft--;
    handleSrv->recv();
}

/* Said once, and it is the whole of what an operator gets.
 *
 * No field of the JSON API names the state of the receiver, and the heartbeat
 * of main() is a Utils::Watchdog() that creates its file and never touches it
 * again - so a box that has gone deaf keeps telling its supervisor it is well.
 * This line is the only place the loss is ever stated.
 */
void UDPServer::giveUpTheReceiver(const string &why)
{
    receiver = Receiver::GaveUp;

    if (rearmTimer)
        rearmTimer->stop();

    cErrorDom("network") << "no longer listening on port " << port
                         << ", " << why
                         << ": discovery (CALAOS_DISCOVER), Wago inputs "
                            "(WAGO INT) and KNX inputs (WAGO KNX) stop here "
                            "until the server is restarted";
}

void UDPServer::createUdpSocket()
{
    auto loop = uvw::Loop::getDefault();

    auto subscribe = [this]()
    {
        handleSrv->on<uvw::UDPDataEvent>([this](const uvw::UDPDataEvent &ev, auto &)
        {
            //A datagram read is the only positive news the loop ever gives
            //about the receiver, so it is where a recovery is learnt.
            if (receiver != Receiver::Listening)
            {
                receiver = Receiver::Listening;
                if (rearmTimer)
                    rearmTimer->stop();
                cWarningDom("network") << "listening again on port " << port
                                       << " after "
                                       << (REARM_ATTEMPTS - rearmsLeft)
                                       << " attempt(s)";
            }

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
            /* The first few one by one, then one line per doubling. Who is on
             * the network decides how many answers are refused, so a line per
             * refusal lets it decide how many lines the journal gets; n cost
             * log2(n) lines here. No count is ever the last one, so a failure
             * that lasts keeps being said at a rate that falls - where "the
             * first N, then nothing" goes mute for good.
             */
            refusedSends++;
            if (refusedSends <= REFUSALS_SAID_IN_FULL ||
                (refusedSends & (refusedSends - 1)) == 0)
                cErrorDom("network") << "UDP send to " << peer << " failed: "
                                     << ev.what() << ", still listening on port "
                                     << port << " (" << refusedSends
                                     << " refused so far, one line per doubling)";
            return;
        }

        h.stop();

        //One of our own attempts, and its failure is what asks for the next
        //one. Silent: coming back costs the journal nothing per attempt.
        if (receiver == Receiver::Retrying)
        {
            scheduleRearm();
            return;
        }

        cErrorDom("network") << "UDP server error: " << ev.what();

        if (receiver == Receiver::GaveUp)
            return;

        receiver = Receiver::Retrying;
        rearmsLeft = REARM_ATTEMPTS;
        scheduleRearm();
    });

    handleSrv->recv();
}

void UDPServer::sendTo(const string &ip, unsigned int remotePort,
                       const string &packet)
{
    pendingSends.push_back(ip + ":" + Utils::to_string(remotePort));

    /* Given to uvw, not lent. packet is a local of processRequest(), and a send
     * posted behind another one is read a whole turn of the loop later: lending
     * it sent the contents of a freed buffer to the peer that asked, which for
     * CALAOS_DISCOVER is anything on the network and needs no credentials.
     */
    Calaos::sendDatagram(*handleSrv, ip, remotePort, packet);
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
            /* Same shape as the refused-send ramp above, and for the same
             * reason: the source of the datagram is chosen by whoever speaks
             * on the network and discovery needs no credentials, so one line
             * per datagram lets it decide how long the journal is. The
             * doubling is a rule, not a threshold - the first is always
             * written, n cost log2(n), and no count is ever the last one.
             */
            unanswerableDiscovers++;
            if ((unanswerableDiscovers & (unanswerableDiscovers - 1)) == 0)
                cErrorDom("network") << "No interface found corresponding to "
                                        "network : " << remoteIp << " ("
                                     << unanswerableDiscovers
                                     << " unanswerable so far, one line per "
                                        "doubling)";
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
