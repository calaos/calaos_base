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
#ifndef S_WODigital_H
#define S_WODigital_H

#include <Calaos.h>
#include <Output.h>
#include <WagoMap.h>

namespace Calaos
{

typedef struct _BlinkInfo
{
        bool state;
        int duration;
        int next;
} BlinkInfo;

class WODigital : public Output
{
        private:
                bool value;
                int address;

                std::string host;
                int port;

                EcoreTimer *timer;
                int time;

                vector<BlinkInfo> blinks;
                int current_blink;

                void TimerImpulse();
                void TimerImpulseExtended();

                bool _set_value(bool val);

                void WagoReadCallback(bool status, UWord address, int count, vector<bool> &values);
                void WagoWriteCallback(bool status, UWord address, bool value);

        public:
                WODigital(Params &p);
                ~WODigital();

                DATA_TYPE get_type() { return TBOOL; }

                bool set_value(bool val);
                bool get_value_bool() { return value; }

                //impulse, time is in ms
                void impulse(int time);

                // extended impulse using pattern
                void impulse_extended(string pattern);
};

}
#endif
