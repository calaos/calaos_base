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
#ifndef S_NTPClock_H
#define S_NTPClock_H

#include <Utils.h>
#include <CThread.h>
#include <tcpsocket.h>
#include <Ecore.h>
#include <IPC.h>
#include <Calendar.h>
#include <EcoreTimer.h>

/**
 * This class is a singleton
 * It allows to run a ntp on the network (LAN or internet)
 * This class can be use to apply a manual configuration too
 *
 * Signals wait by NTPClock:
 *  - source, emission
 *  - "CalaosCommon::NTPClock","applyCalendar" : apply the calendar
 *      set with setNetworkCmdCalendarApply()
 */

class NTPClock
{
private:

    /**
                 * signal called by the signal 'applyDate'
                 */
    sigc::signal<void, std::string, std::string, void*, void*> sig_applyCalendar;
    /** the command from the server
                 * something as "system date ntp_on"
                 */
    std::vector<std::string> networkCmdCalendarApply;
    /**
                 * Calendar use to apply a manual date (no ntp)
                 */
    Calendar cApply;

    bool restartWhenApply;

    /**
                 * the timer used to launch a ntp every n secondes
                 */
    EcoreTimer *timer;

    void TimerTick();

    /**
                 * the exe used during the object apply a date
                 */
    Ecore_Exe *exe;
    /**
                 * the handle used during the object apply a date
                 */
    Ecore_Event_Handler *handler;

public:


    static NTPClock& Instance();

    NTPClock();
    ~NTPClock();

    /**
                 * When a client retrieve a date command from the server from an udp connection
                 * The udp server is run in a thread.
                 * - it set the command
                 * - send the signal "applyCalendar", this method allows to apply the date
                 *   from the main thread instead of the udp server thread
                 *   ( we use some ecore_* methods to apply the date )
                 */
    void applyCalendarFromServer(std::string source,std::string emission,
                                 void* listener_data, void* sender_data);
    /**
                 * See applyCalendarFromServer()
                 */
    void setNetworkCmdCalendarApply(std::vector<std::string> s);

    void setRestartWhenApply(bool s);
    bool isRestartWhenApply();

    /**
                 * update the clock, launch a ntp
                 */
    void updateClock();

    /**
                 * enable or disable the ntp in the configuration file
                 */
    void enable(bool en);

    //Private, used by Ecore
    void Handle1();
    void Handle2();
};

#endif
