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

#include <sys/types.h>

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

    /* A ramp read over the whole life of the process is BUYABLE: an
     * unauthenticated peer drives a count to a power of two and the next real
     * failure is written nowhere for as long again.
     *
     * GLOBAL and not per correspondent: a per-peer count is bought with one
     * peer per event, which is the defect the ramp exists to close. Same shape
     * and length as the window RemoteUIManager rearms its per-IP count on.
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
     * A receive error stops the handle - without that, one bad datagram is one
     * error per turn of the loop - and a read armed again takes the whole queue
     * back WHEN the number still names the socket that was bound. When it does
     * not, arming the read is no error anyone can report: libuv abort()s on a
     * free number, and blocks for ever on a number handed out again.
     */
    enum class Receiver { Listening, Retrying, GaveUp };
    Receiver receiver = Receiver::Listening;

    std::shared_ptr<uvw::TimerHandle> rearmTimer;
    unsigned rearmsLeft = 0;

    /* How many times to come back. The hold before each attempt is
     * ExternProcServer::respawnDelay() of the attempts already made - 0 (the
     * next turn of the loop), then kRespawnDelayMin, then double - so the only
     * numbers here are the ceiling and constants that already exist.
     */
    static constexpr unsigned REARM_ATTEMPTS = 3;

    /* What the number libuv polls must still name for a read to be armed on it.
     * The inode identifies the file description itself; the bound address is
     * what a number handed out again cannot carry, since a fresh socket is
     * unbound. Captured once the bind has settled.
     */
    dev_t ownedDevice = 0;
    ino_t ownedInode = 0;
    string ownedAddress;
    bool descriptorRemembered = false;

    void createUdpSocket();
    void rememberTheDescriptor();
    void sendTo(const string &ip, unsigned int remotePort, const string &packet);
    void processRequest(const string &msg, const string &remoteIp, unsigned int port);

    //Virtual for the suite that has to arm a read the guard would refuse, which
    //is the only way to price what the guard is there to avoid.
    virtual bool descriptorIsStillOurs() const;
    void scheduleRearm();
    void considerListeningAgain();
    void giveUpTheReceiver(const string &why);

public:
    UDPServer(int port); //port to listen
    virtual ~UDPServer();
};

#endif
