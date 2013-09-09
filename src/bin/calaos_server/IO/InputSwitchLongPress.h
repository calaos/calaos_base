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
#ifndef INPUTSWITCHLONG_H
#define INPUTSWITCHLONG_H

#include <Calaos.h>
#include <Input.h>
#include <EcoreTimer.h>

namespace Calaos
{

class InputSwitchLongPress : public Input
{
        protected:
                double value;

                EcoreTimer *timer;

                void longPress_timer();
                void emitChange();
                void resetInput();

                virtual bool readValue() = 0;

        public:
                InputSwitchLongPress(Params &p);
                ~InputSwitchLongPress();

                virtual DATA_TYPE get_type() { return TINT; }
                virtual double get_value_double() { return value; }
                virtual void force_input_double(double v)
                {
                        value = v;
                        EmitSignalInput();
                }

                virtual void hasChanged();
};

}
#endif
