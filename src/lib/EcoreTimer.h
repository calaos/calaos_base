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
#ifndef CALAOS_ECORE_TIMER_h
#define CALAOS_ECORE_TIMER_h

#include <Utils.h>
#include <sigc++/sigc++.h>
#include <Ecore.h>

using namespace Utils;

class EcoreTimer
{
private:
    Ecore_Timer *timer;

    sigc::signal<void, void *> event_signal_data;
    sigc::signal<void> event_signal;
    sigc::connection connection_data;
    sigc::connection connection;

    double time;
    bool timer_data;

    void *data;

public:
    EcoreTimer(double time, sigc::slot<void, void *> slot, void *data);
    EcoreTimer(double time, sigc::slot<void> slot);
    ~EcoreTimer();

    static void singleShot(double time, sigc::slot<void> slot);

    void Reset();
    void Reset(double time);

    //private, used by ecore
    void Tick();

    double getTime() { return time; }
};

#endif
