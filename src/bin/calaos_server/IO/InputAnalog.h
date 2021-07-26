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
#ifndef InputAnalog_H
#define InputAnalog_H

#include "Calaos.h"
#include "IOBase.h"

namespace Calaos
{

class InputAnalog : public IOBase
{
protected:
    double coeff_a = 1.0;
    double coeff_b = 0.0;

    double value;
    double timer;
    double offset;
    double frequency;
    int precision;

    //timer that triggers a property when value has not been updated for some time
    Timer *timerChanged = nullptr;

    void readConfig();

    void emitChange();
    virtual void readValue() = 0;
public:
    InputAnalog(Params &p);
    virtual ~InputAnalog();

    virtual DATA_TYPE get_type() { return TINT; }
    virtual bool set_value(double v);

    virtual bool set_value(string v);

    virtual double get_value_double();

    virtual void hasChanged();
};

}
#endif
