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

    void WagoReadCallback(bool status, UWord address, int count, vector<UWord> &values);
    void WagoWriteCallback(bool status, UWord address, UWord value);

public:
    WOAnalog(Params &p);
    ~WOAnalog();
};

}
#endif
