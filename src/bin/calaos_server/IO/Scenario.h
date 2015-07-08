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
#ifndef S_Scenario_H
#define S_Scenario_H

#include "Calaos.h"
#include "Input.h"
#include "Output.h"
#include "Jansson_Addition.h"

namespace Calaos
{

class AutoScenario;

class Scenario : public Input, public Output
{
protected:
    bool value;

    AutoScenario *auto_scenario;

public:
    Scenario(Params &p);
    ~Scenario();

    virtual DATA_TYPE get_type() { return TBOOL; }

    //Input
    virtual bool get_value_bool() { return value; }
    virtual void force_input_bool(bool v);

    //Output
    virtual bool set_value(bool val);

    virtual void set_param(std::string opt, std::string val)
    { Input::set_param(opt, val); }
    virtual std::string get_param(std::string opt)
    { return Input::get_param(opt); }
    virtual Params &get_params()
    { return Input::get_params(); }

    AutoScenario *getAutoScenario() { return auto_scenario; }

    json_t *toJson();
};

}
#endif
