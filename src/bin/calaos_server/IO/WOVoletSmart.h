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
#ifndef S_WOVoletSmart_H
#define S_WOVoletSmart_H

#include <Calaos.h>
#include <Output.h>
#include <WagoMap.h>
#include <Ecore.h>
#include <EcoreTimer.h>

namespace Calaos
{

class WOVoletSmart : public Output
{
        private:
                std::string host;
                int port;

                int total_time, time_up, time_down;
                int up_address, down_address;
                int sens, old_sens;
                double position; // range: [0..100]

                EcoreTimer *timer_end, *timer_update, *timer_impulse;
                EcoreTimer *timer_up, *timer_down, *timer_calib;
                double start_time;
                double start_position;
                bool is_impulse_action;
                int impulse_action_time;
                int impulse_time;
                bool calibrate;

                std::string cmd_state;

                void Up(double new_value = -1);
                void Down(double new_value = -1);
                void UpWait();
                void DownWait();
                void Stop();

                double readPosition();
                void writePosition(double p);

                void TimerEnd();
                void TimerUpdate();
                void TimerImpulse();
                void TimerCalibrate();

                void WagoWriteCallback(bool status, UWord address, bool value);
                void WagoUDPCommand_cb(bool status, string command, string result);

        public:
                //two address for this output, for the up and down
                WOVoletSmart(Params &p);
                ~WOVoletSmart();

                virtual DATA_TYPE get_type() { return TSTRING; }

                virtual bool set_value(std::string val);
                virtual std::string get_value_string();
                virtual double get_value_double() { return (int)(readPosition() * 100. / (double)time_up); }
                virtual std::string get_command_string() { return cmd_state; }
};

}
#endif
