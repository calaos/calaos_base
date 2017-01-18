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
#ifndef OUTPUTLIGHT_H
#define OUTPUTLIGHT_H

#include "Calaos.h"
#include "IOBase.h"
#include "Timer.h"

namespace Calaos {

class OutputLight : public IOBase
{
private:
    Timer *timer = nullptr;

    vector<BlinkInfo> blinks;
    int current_blink;

    void TimerImpulse();
    void TimerImpulseExtended();

    bool _set_value(bool val);

    //impulse, time is in ms
    void impulse(int time);

    // extended impulse using pattern
    void impulse_extended(string pattern);

protected:
    bool value;

    //By default when calaos set state for an output,
    //it automatically update and emit an event.
    //If changing this bool, calaos will not send the
    //event, but the underlying class has to handle that
    //(real event from hw)
    bool useRealState = false;

    void emitChange();

    virtual bool set_value_real(bool val) = 0;

public:
    OutputLight(Params &p);
    virtual ~OutputLight();

    DATA_TYPE get_type() { return TBOOL; }

    virtual bool set_value(bool val);
    virtual bool get_value_bool() { return value; }
    virtual bool set_value(string val);
};

}
#endif
