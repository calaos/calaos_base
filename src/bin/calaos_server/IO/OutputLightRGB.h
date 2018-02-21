/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#ifndef S_OutputLightRGB_H
#define S_OutputLightRGB_H

#include "Calaos.h"
#include "IOBase.h"
#include "Timer.h"

namespace Calaos
{

class OutputLightRGB : public IOBase
{
protected:
    bool state = false;
    bool oldstate = true;
    ColorValue color;

    std::string cmd_state;

    Timer *timer_auto;
    void TimerAutoChange();
    void setColor(const ColorValue &color, bool state);
    void emitChange();

    //By default when calaos set state for an output,
    //it automatically update and emit an event.
    //If changing this bool, calaos will not send the
    //event, but the underlying class has to handle that
    //(real event from hw)
    bool useRealState = false;

    //call this function whenever state of light changes to update internal status
    void stateUpdated(const ColorValue &color, bool state);

    virtual void setColorReal(const ColorValue &color, bool state) = 0;

public:
    OutputLightRGB(Params &p);
    ~OutputLightRGB();

    DATA_TYPE get_type() { return TSTRING; }

    bool set_value(std::string val);
    bool set_value(bool val)
    { if (val) set_value(std::string("on")); else set_value(std::string("off")); return true; }
    std::string get_value_string() { return !state?"0":color.toString(); }
    bool get_value_bool() { return state; }

    virtual std::string get_command_string() { return cmd_state; }

    virtual bool check_condition_value(string cvalue, bool equal);
};

}
#endif
