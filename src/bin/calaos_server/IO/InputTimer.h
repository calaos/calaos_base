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
#ifndef S_INPUTTIMER_H
#define S_INPUTTIMER_H

#include "Calaos.h"
#include "IOBase.h"
#include "Timer.h"

namespace Calaos
{

class InputTimer : public IOBase
{
protected:
    int hour, minute, second, ms;

    Timer *timer;
    string value;
    bool start;

    void StartTimer();
    void StopTimer();
    void TimerDone();

public:
    InputTimer(Params &prm);
    ~InputTimer();

    //Input
    virtual DATA_TYPE get_type() { return TSTRING; }
    virtual string get_value_string() { return value; }

    //Output
    virtual bool set_value(string val);

    virtual void hasChanged();
};

}
#endif
