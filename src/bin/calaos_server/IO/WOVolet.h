/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#ifndef S_WOVolet_H
#define S_WOVolet_H

#include <Calaos.h>
#include <Output.h>
#include <WagoMap.h>
#include <Ecore.h>
#include <EcoreTimer.h>

namespace Calaos
{

class WOVolet : public Output
{
        private:
                int up_address, down_address;
                int time;
                int sens, old_sens;

                std::string host;
                int port;

                EcoreTimer *timer_end, *timer_impulse;
                EcoreTimer *timer_up, *timer_down;
                bool is_impulse_action;
                int impulse_action_time;
                int impulse_time;

                std::string state_volet, cmd_state;

                void TimerEnd();
                void TimerImpulse();

                void Up();
                void Down();
                void UpWait();
                void DownWait();
                void Stop();

                void WagoWriteCallback(bool status, UWord address, bool value);

        public:
                WOVolet(Params &p);
                ~WOVolet();

                virtual DATA_TYPE get_type() { return TSTRING; }

                virtual bool set_value(std::string val);
                virtual std::string get_value_string() { return state_volet; }

                virtual std::string get_command_string() { return cmd_state; }
};

}
#endif
