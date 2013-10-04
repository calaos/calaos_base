/******************************************************************************
**  Copyright (c) 2007-2013, Calaos. All Rights Reserved.
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
#ifndef OUTPUTLIGHT_H
#define OUTPUTLIGHT_H

#include <Calaos.h>
#include <Output.h>
#include <EcoreTimer.h>

namespace Calaos {

typedef struct _BlinkInfo
{
        bool state;
        int duration;
        int next;
} BlinkInfo;

class OutputLight : public Output
{
        private:
                EcoreTimer *timer;
                int time;

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
