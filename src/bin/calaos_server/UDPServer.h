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
#ifndef S_UDPServer_H
#define S_UDPServer_H

#include <Calaos.h>
#include <tcpsocket.h>
#include <WagoMap.h>

#include <deque>

namespace uvw {
//Forward declare classes here to prevent long build time
//because of uvw.hpp being header only
class UDPHandle;
class TimerHandle;
}

class UDPServer
{
protected:
    int port;
    std::shared_ptr<uvw::UDPHandle> handleSrv;
    //Correspondents of the datagrams uvw still owes a completion for, oldest
    //first. See createUdpSocket(): it is the only thing that tells a refused
    //send from a broken socket.
    std::deque<std::string> pendingSends;
    //Refused sends counted in the current journal window, for the journal only.
    unsigned long refusedSends = 0;
    //Refusals written out one by one before the doubling ramp takes over. Not a
    //number picked here: a box that loses three correspondents must name the
    //three, which is what UdpSendFailureIsolation_test holds, and three is
    //therefore the floor. Any value up to four keeps the same set, four being
    //a doubling already.
    static constexpr unsigned long REFUSALS_SAID_IN_FULL = 3;
    //CALAOS_DISCOVER datagrams left unanswered because no local address of
    //this machine reaches their source, counted in the current journal window.
    unsigned long unanswerableDiscovers = 0;

    //Start of the window each of the two counts above is counted in.
    double refusedWindowStart = 0.0;
    double unanswerableWindowStart = 0.0;

    /* Both counts feed a doubling ramp, and a ramp read over the whole life of
     * the process is BUYABLE: the same unauthenticated peer that made the
     * journal long drives a count to a power of two, and the next real failure
     * is written nowhere for as long again. Counting inside a window that
     * restarts on its own takes that back.
     *
     * The window is GLOBAL and not per correspondent. A per-peer count is
     * bought with one peer per event - the easiest traffic of all to produce -
     * and it costs one line per peer, which is the defect the ramp exists to
     * close. Same shape and same length as the window RemoteUIManager rearms
     * its per-IP attempt count on: one number, no eviction, so there is no
     * table to fill and no entry to get evicted either.
     */
    static constexpr double JOURNAL_WINDOW_SECONDS = 60.0;

    //Seconds off a monotonic source. Overridden in tests, which must be able to
    //cross a window without waiting for one.
    virtual double journalNow() const;

    //count, advanced inside its window, and the window restarted when it is
    //over. Answers the new count.
    unsigned long countInJournalWindow(unsigned long &count, double &windowStart);

    /* What the receiver is doing, which nothing else in the tree can ask it.
     *
     * A receive error stops the handle - without that one bad datagram is one
     * error per turn of the loop and a core at 100% - and a read armed again a
     * turn later takes the whole queue back WHEN the fault left the descriptor
     * alone. When the fault CLOSED it there is no coming back: libuv hands the
     * number to epoll_ctl, gets EBADF and calls abort() without a word, so the
     * descriptor is looked at before every attempt and a missing one ends the
     * attempts rather than the process.
     */
    enum class Receiver { Listening, Retrying, GaveUp };
    Receiver receiver = Receiver::Listening;

    std::shared_ptr<uvw::TimerHandle> rearmTimer;
    unsigned rearmsLeft = 0;

    /* How many times to come back, and it is the only number in the recovery:
     * the wait between two attempts is a TURN OF THE LOOP, not a duration.
     * Nothing in the tree derives a duration for this, and a delay would bound
     * a rate where what has to be bounded is a count - the same attempts cost
     * the same journal however long the box is left running.
     */
    static constexpr unsigned REARM_ATTEMPTS = 3;

    void createUdpSocket();
    void sendTo(const string &ip, unsigned int remotePort, const string &packet);
    void processRequest(const string &msg, const string &remoteIp, unsigned int port);

    bool descriptorIsStillOurs() const;
    void scheduleRearm();
    void considerListeningAgain();
    void giveUpTheReceiver(const string &why);

public:
    UDPServer(int port); //port to listen
    virtual ~UDPServer();
};

#endif
