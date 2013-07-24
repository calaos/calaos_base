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
#ifndef S_X10OUTPUT_H
#define S_X10OUTPUT_H

#include <Calaos.h>
#include <Output.h>

namespace Calaos
{

class X10Output : public Output
{
        private:
                std::string housecode;

                int value;
                bool state_value;
                int address, relay_addr;
                int old_value;

                std::string cmd_state;

                bool X10Command(std::string cmd, int *dval = NULL);

        public:
                X10Output(Params &p);
                ~X10Output();

                DATA_TYPE get_type() { return TSTRING; }

                bool set_value(std::string val);
                bool set_value(bool val) { if (val) set_value(std::string("on")); else set_value(std::string("off")); return true; }
                std::string get_value_string() { if (value == -1) return "off"; else return Utils::to_string(value); }
                bool get_value_bool() { return state_value; }

                virtual std::string get_command_string() { return cmd_state; }
};

}
#endif
