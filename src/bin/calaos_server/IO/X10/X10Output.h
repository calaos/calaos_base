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

#include <OutputLightDimmer.h>

namespace Calaos
{

class X10Output : public OutputLightDimmer
{
private:
    std::string housecode;
    bool state_value;

    virtual bool set_on_real();
    virtual bool set_off_real();
    virtual bool set_value_real(int val);
    virtual bool set_dim_up_real(int percent);
    virtual bool set_dim_down_real(int percent);

    bool X10Command(std::string cmd, int *dval = NULL);

public:
    X10Output(Params &p);
    ~X10Output();
};

}
#endif
