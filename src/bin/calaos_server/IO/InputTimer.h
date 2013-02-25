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
#ifndef S_INPUTTIMER_H
#define S_INPUTTIMER_H

#include <Calaos.h>
#include <Input.h>
#include <Output.h>
#include <EcoreTimer.h>

namespace Calaos
{

class InputTimer : public Input, public Output
{
        protected:
                int hour, minute, second, ms;

                EcoreTimer *timer;
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

                virtual void set_param(std::string opt, std::string val)
                        { Input::set_param(opt, val); }
                virtual std::string get_param(std::string opt)
                        { return Input::get_param(opt); }
                virtual Params &get_params()
                        { return Input::get_params(); }
};

}
#endif
