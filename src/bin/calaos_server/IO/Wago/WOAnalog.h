/******************************************************************************
**  Copyright (c) 2007-2010, Calaos. All Rights Reserved.
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
#ifndef S_WOAnalog_H
#define S_WOAnalog_H

#include <Calaos.h>
#include <Output.h>
#include <WagoMap.h>

namespace Calaos
{

class WOAnalog : public Output
{
        private:
                double value;
                double real_value_max;
                double wago_value_max;

                int address;

                std::string host;
                int port;

                void readConfig();

                void WagoReadCallback(bool status, UWord address, int count, vector<UWord> &values);
                void WagoWriteCallback(bool status, UWord address, UWord value);

        public:
                WOAnalog(Params &p);
                ~WOAnalog();

                DATA_TYPE get_type() { return TINT; }

                virtual bool set_value(double val);
                virtual double get_value_double();
};

}
#endif
