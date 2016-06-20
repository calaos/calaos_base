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
#ifndef S_OutputLightDimmer_H
#define S_OutputLightDimmer_H

#include "Calaos.h"
#include "IOBase.h"
#include "EcoreTimer.h"

namespace Calaos
{

class OutputLightDimmer : public IOBase
{
protected:
    int value;
    int old_value;

    EcoreTimer *hold_timer = nullptr;
    EcoreTimer *impulseTimer = nullptr;

    std::vector<BlinkInfo> blinks;
    int current_blink;

    std::string cmd_state;
    bool press_detected;
    bool press_sens;
    bool stop_after_press;

    //By default when calaos set state for an output,
    //it automatically update and emit an event.
    //If changing this bool, calaos will not send the
    //event, but the underlying class has to handle that
    //(real event from hw)
    bool useRealState = false;

    void TimerImpulse();
    void TimerImpulseExtended();

    //impulse, time is in ms
    void impulse(int time);

    // extended impulse using pattern
    void impulse_extended(std::string pattern);

    void HoldPress_cb();

    void emitChange();

    virtual bool set_on_real();
    virtual bool set_off_real();
    virtual bool set_value_real(int val) = 0;
    virtual bool set_dim_up_real(int percent);
    virtual bool set_dim_down_real(int percent);

public:
    OutputLightDimmer(Params &p);
    ~OutputLightDimmer();

    DATA_TYPE get_type() { return TSTRING; }

    bool set_value(std::string val);
    bool set_value(bool val)
    { if (val) set_value(std::string("on")); else set_value(std::string("off")); return true; }
    std::string get_value_string() { return Utils::to_string(value); }
    bool get_value_bool() { if (value == 0) return false; else return true; }

    virtual std::string get_command_string() { return cmd_state; }

    virtual bool check_condition_value(std::string cvalue, bool equal);
};

}
#endif
