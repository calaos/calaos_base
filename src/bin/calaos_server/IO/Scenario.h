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
#ifndef S_Scenario_H
#define S_Scenario_H

#include "Calaos.h"
#include "IOBase.h"
#include "Jansson_Addition.h"

namespace Calaos
{

class AutoScenario;

class Scenario : public IOBase
{
protected:
    bool value;

    AutoScenario *auto_scenario;

public:
    Scenario(Params &p);
    ~Scenario();

    virtual DATA_TYPE get_type() override { return TBOOL; }

    virtual bool get_value_bool() override { return value; }
    virtual bool set_value(bool val) override;

    AutoScenario *getAutoScenario() { return auto_scenario; }

    virtual bool get_command_bool() override { return value; }

    json_t *toJson();
};

}
#endif
