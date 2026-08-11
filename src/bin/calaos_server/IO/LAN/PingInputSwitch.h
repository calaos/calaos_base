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
#ifndef PINGINPUTSWITCH_H
#define PINGINPUTSWITCH_H

#include "InputSwitch.h"
#include "Timer.h"

namespace uvw {
//Forward declare classes here to prevent long build time
//because of uvw.hpp being header only
class ProcessHandle;
}

using namespace Calaos;

class PingInputSwitch: public InputSwitch
{
protected:
    virtual bool readValue();

    bool lastStatus = false;
    std::shared_ptr<uvw::ProcessHandle> ping_exe;

    //True between a successful spawn() and the exit/error event
    bool pingRunning = false;

    //Timer between two pings. Owned here so that it can never tick after
    //this object is destroyed (Timer::singleShot() could not be stopped).
    Timer *pollTimer = nullptr;

    void doPing();
    void pollTimeout();

public:
    PingInputSwitch(Params &p);
    virtual ~PingInputSwitch();
};

#endif // PINGINPUTSWITCH_H
