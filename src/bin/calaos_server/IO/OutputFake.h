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
#ifndef S_OUTPUTFAKE_H
#define S_OUTPUTFAKE_H

#include "Calaos.h"
#include "IOBase.h"

namespace Calaos
{

class OutputFake : public IOBase
{
private:
    bool value;

public:
    OutputFake(Params &p);
    ~OutputFake();

    DATA_TYPE get_type() { return TBOOL; }

    bool set_value(bool val);
    bool get_value_bool() { return value; }

    virtual bool get_command_bool() { return value; }
};

}
#endif
