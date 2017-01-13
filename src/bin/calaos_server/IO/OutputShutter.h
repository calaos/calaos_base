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
#ifndef S_OutputShutter_H
#define S_OutputShutter_H

#include "Calaos.h"
#include "IOBase.h"
#include "Ecore.h"
#include "Timer.h"

namespace Calaos
{

class OutputShutter : public IOBase
{
protected:
    int time;
    int sens, old_sens;

    Timer *timer_end, *timer_impulse;
    Timer *timer_up, *timer_down;
    bool is_impulse_action;
    int impulse_action_time;
    int impulse_time;

    std::string state_volet, cmd_state;

    void TimerEnd();
    void TimerImpulse();

    /* Subclass infos:
                 * To subclass OutputShutter you have two options. Use the standard Calaos
                 * logic and only reimplement setOutputUp()/setOutputDown(). These functions
                 * needs to only trigger a hardware output, all the states and logic is
                 * handled by the default Up/Down/Toggle/... functions.
                 * The second option is to leave setOutputUp()/setOutputDown() and reimplement
                 * all Up/Down/Toggle/... virtual functions to behave like the calaos
                 * implementation for your hardware. This is usefull when the shutter
                 * logic is already handled by the hardware and you can't access directly
                 * to the output ports. If you choose this second option, please get it
                 * touch with us to see how to do it correctly.
                 */

    //reimplement this to use Calaos shutter logic
    virtual void setOutputUp(bool enable) {}
    virtual void setOutputDown(bool enable) {}

    //reimplement this to use some hw specific shutter management
    //tips: UpWait/DownWait func are delaying by 200ms the real Up/Down command
    // if the shutter is not stopped to avoid up and down being active
    // at the same time and hurt the shutter motor.
    virtual void Up();
    virtual void Down();
    virtual void UpWait();
    virtual void DownWait();
    virtual void Stop();
    virtual void Toggle();
    virtual void ImpulseUp(int ms);
    virtual void ImpulseDown(int ms);

public:
    OutputShutter(Params &p);
    ~OutputShutter();

    virtual DATA_TYPE get_type() { return TSTRING; }

    virtual bool set_value(std::string val);
    virtual std::string get_value_string() { return state_volet; }

    virtual std::string get_command_string() { return cmd_state; }

    virtual bool check_condition_value(string cvalue, bool equal);
};

}
#endif
