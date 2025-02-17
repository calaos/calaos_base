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
#ifndef CALAOS_TIMER_H
#define CALAOS_TIMER_H

#include "Utils.h"
#include <sigc++/sigc++.h>

using namespace Utils;

namespace uvw {
//Forward declare classes here to prevent long build time
//because of uvw.hpp being header only
class TimerHandle;
class IdleHandle;
}

class Timer
{
private:
    std::shared_ptr<uvw::TimerHandle> handleTimer;

    sigc::signal<void, void *> event_signal_data;
    sigc::signal<void> event_signal;
    sigc::connection connection_data;
    sigc::connection connection;

    uint64_t time;
    bool timer_data;

    void *data = nullptr;

    void create();

public:
    Timer(double time, sigc::slot<void, void *> slot, void *data);
    Timer(double time, sigc::slot<void> slot);
    ~Timer();

    static void singleShot(double time, sigc::slot<void> slot);

    void Reset();
    void Reset(double time);

    void Tick();

    double getTime() { return time / 1000.0; }
};

class Idler
{
private:
    std::shared_ptr<uvw::IdleHandle> handleIdler;

    void createIdler();
public:
    Idler(sigc::slot<void> slot);
    Idler();
    ~Idler();

    static void singleIdler(sigc::slot<void> slot);

    sigc::signal<void> idlerCallback;

};


#endif
