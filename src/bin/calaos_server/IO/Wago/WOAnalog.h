/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos.
 **
 **  Calaos is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos is distributed in the hope that it will be useful,
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

#include <OutputAnalog.h>

namespace Calaos
{

class WOAnalog : public OutputAnalog
{
private:
    int address;

    std::string host;
    int port;

    virtual void set_value_real(double val);

    void WagoReadCallback(bool status, WagoTypes::Address address, WagoTypes::Count count, vector<UWord> &values);
    /* T3.46 - taken BY VALUE on purpose. A parameter declared as a
     * non-const lvalue reference makes std::is_invocable_v answer false for
     * the CORRECT order too, so the probes of tests/WagoWriteReply_test.cpp
     * would pass for the wrong reason (T3.31, F-TYPE-5). */
    void WagoWriteCallback(bool status, WagoTypes::Address address, WagoTypes::WordValue value);

public:
    WOAnalog(Params &p);
    ~WOAnalog();
};

}
#endif
