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
#ifndef S_IntValue_H
#define S_IntValue_H

#include "Calaos.h"
#include "IOBase.h"
#include "EcoreTimer.h"

namespace Calaos
{

class Internal : public IOBase
{
protected:
    bool bvalue;
    double dvalue;
    string svalue;

    EcoreTimer *timer = NULL;

    vector<BlinkInfo> blinks;
    int current_blink;

    void impulse_extended(string pattern);
    void TimerImpulseExtended();

    void Save(); //save value to file
    void LoadFromConfig(); //load value from config file

public:
    Internal(Params &p);
    ~Internal();

    virtual DATA_TYPE get_type()
    {
        if (get_param("type") == "InternalBool") return TBOOL;
        if (get_param("type") == "InternalInt") return TINT;
        if (get_param("type") == "InternalString") return TSTRING;
        return TUNKNOWN;
    }

    virtual bool get_value_bool() { return bvalue; }
    virtual double get_value_double() { return dvalue; }
    virtual string get_value_string() { return svalue; }

    virtual bool set_value(bool val);
    virtual bool set_value(double val);
    virtual bool set_value(string val);

    virtual bool SaveToXml(TiXmlElement *node);
};

}
#endif
