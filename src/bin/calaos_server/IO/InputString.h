/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef S_InputString_H
#define S_InputString_H

#include <Calaos.h>
#include <Input.h>

namespace Calaos
{

class InputString : public Input
{
protected:
    string value;
    double frequency;
    void readConfig();

    void emitChange();
    virtual void readValue() = 0;

public:
    InputString(Params &p);
    ~InputString();

    DATA_TYPE get_type() { return TSTRING; }

    string get_value_string();
};

}
#endif
