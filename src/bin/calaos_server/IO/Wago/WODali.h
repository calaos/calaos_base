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
#ifndef S_WODali_H
#define S_WODali_H

#include <Calaos.h>
#include <Output.h>
#include <WagoMap.h>
#include <EcoreTimer.h>

namespace Calaos
{

class WODali : public Output
{
        private:
                int value;
                int old_value;

                std::string host;
                int port;

                EcoreTimer *hold_timer;

                std::string cmd_state;
                bool press_detected;
                bool press_sens;
                bool stop_after_press;

                void HoldPress_cb();

                void WagoUDPCommand_cb(bool status, string command, string result);

        public:
                //two address for this output, one for the on/off action and the other for the dim
                WODali(Params &p);
                ~WODali();

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
