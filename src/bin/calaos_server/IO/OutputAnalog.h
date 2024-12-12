/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#ifndef S_OutputAnalog_H
#define S_OutputAnalog_H

#include "Calaos.h"
#include "IOBase.h"
#include "WagoMap.h"

namespace Calaos
{

class OutputAnalog : public IOBase
{
protected:
    double value;
    double coeff_a = 1.0;
    double coeff_b = 0.0;
    double cmd_state = 0.0;

    void readConfig();

    void WagoReadCallback(bool status, UWord address, int count, vector<UWord> &values);
    void WagoWriteCallback(bool status, UWord address, UWord value);

    void emitChange();
    virtual void set_value_real(double val) = 0;

public:
    OutputAnalog(Params &p);
    ~OutputAnalog();

    DATA_TYPE get_type() { return TINT; }

    virtual bool set_value(double val);
    virtual double get_value_double();
    virtual bool set_value(string val);

    virtual double get_command_double() { return cmd_state; }
};

}
#endif
