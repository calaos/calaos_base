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
#ifndef S_OutputLightRGB_H
#define S_OutputLightRGB_H

#include <Calaos.h>
#include <Output.h>
#include "EcoreTimer.h"

namespace Calaos
{

class OutputLightRGB : public Output
{
        protected:
                int value;
                int old_value;
                int red, green, blue;

                std::string cmd_state;

                EcoreTimer *timer_auto;
                void TimerAutoChange();
                void setColor();
                void emitChange();

                //call this function whenever state of light changes to update internal status
                void stateUpdated(int r, int g, int b);

                virtual void setColorReal(int r, int g, int b) = 0;

        public:
                OutputLightRGB(Params &p);
                ~OutputLightRGB();

                DATA_TYPE get_type() { return TSTRING; }

                bool set_value(std::string val);
                bool set_value(bool val)
                        { if (val) set_value(std::string("on")); else set_value(std::string("off")); return true; }
                std::string get_value_string() { return Utils::to_string(value); }
                bool get_value_bool() { if (value == 0) return false; else return true; }

                virtual std::string get_command_string() { return cmd_state; }
};

}
#endif
