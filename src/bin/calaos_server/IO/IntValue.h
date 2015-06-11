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
#include "Input.h"
#include "Output.h"
#include "EcoreTimer.h"

namespace Calaos
{

class Internal : public Input, public Output
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
        if (Input::get_param("type") == "InternalBool") return TBOOL;
        if (Input::get_param("type") == "InternalInt") return TINT;
        if (Input::get_param("type") == "InternalString") return TSTRING;
        return TUNKNOWN;
    }

    //Input
    virtual bool get_value_bool() { return bvalue; }
    virtual double get_value_double() { return dvalue; }
    virtual string get_value_string() { return svalue; }

    virtual void force_input_bool(bool v);
    virtual void force_input_double(double v);
    virtual void force_input_string(string v);

    //Output
    virtual bool set_value(bool val);
    virtual bool set_value(double val);
    virtual bool set_value(string val);

    //Use common params for input and output
    virtual void set_param(std::string opt, std::string val)
    { Input::set_param(opt, val); }
    virtual std::string get_param(std::string opt)
    { return Input::get_param(opt); }
    virtual Params &get_params()
    { return Input::get_params(); }

    virtual bool LoadFromXml(TiXmlElement *node)
    { return false; }
    virtual bool SaveToXml(TiXmlElement *node);
};

}
#endif
