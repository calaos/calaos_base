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
#ifndef S_WIDigitalTriple_H
#define S_WIDigitalTriple_H

#include <Calaos.h>
#include <Input.h>
#include <WagoMap.h>
#include <EcoreTimer.h>

namespace Calaos
{

class WIDigitalTriple : public Input, public sigc::trackable
{
        protected:
                type_signal_wago::iterator iter;

                int address;
                std::string host;
                int port;

                int count;
                bool udp_value;
                double value;

                EcoreTimer *timer;

                void TimerDone();
                void resetInput();

                void WagoReadCallback(bool status, UWord address, int count, vector<bool> &values);

        public:
                WIDigitalTriple(Params &p);
                ~WIDigitalTriple();

                virtual DATA_TYPE get_type() { return TINT; }

                /* renvoie le numero de l'action:
                        -1: rien
                        1: action 1
                        2: action 2
                        3: action 3
                */
                virtual double get_value_double() { return value; }
                virtual void force_input_double(double v)
                {
                        value = v;
                        EmitSignalInput();
                }

                virtual void hasChanged();
                virtual void ReceiveFromWago(std::string ip, int addr, bool val, std::string intype);
};

}

#endif
