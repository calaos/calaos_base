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
#ifndef INPUTSWITCHTRIPLE_H
#define INPUTSWITCHTRIPLE_H

#include "Calaos.h"
#include "IOBase.h"
#include "Timer.h"

namespace Calaos
{

class InputSwitchTriple : public IOBase
{
protected:
    int count;
    double value;

    Timer *timer;

    void TimerDone();
    void resetInput();
    void emitChange();

    virtual bool readValue() = 0;

public:
    InputSwitchTriple(Params &p);
    ~InputSwitchTriple();

    virtual DATA_TYPE get_type() { return TINT; }

    /* Send the action number
                        -1: nothing
                        1: action 1
                        2: action 2
                        3: action 3
                */
    virtual double get_value_double() { return value; }
    virtual bool set_value(double v);

    virtual void hasChanged();
};

}

#endif
