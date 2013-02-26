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
#include <Scenario.h>
#include <IPC.h>
#include <AutoScenario.h>

using namespace Calaos;

Scenario::Scenario(Params &p):
                Input(p),
                Output(p),
                value(false),
                auto_scenario(NULL)
{
        Utils::logger("output") << Priority::INFO << "Scenario::Scenario(" << get_param("id") << "): Ok" << log4cpp::eol;

        if (Input::get_param("auto_scenario") != "")
        {
                auto_scenario = new AutoScenario(this);
                Input::setAutoScenario(true);
        }

        if (!Input::get_params().Exists("visible")) set_param("visible", "true");
}

Scenario::~Scenario()
{
        DELETE_NULL(auto_scenario);

        Utils::logger("output") << Priority::INFO << "Scenario::~Scenario(): Ok" << log4cpp::eol;
}

void Scenario::force_input_bool(bool v)
{
        value = v;
        EmitSignalInput();

        string sig = "input ";
        sig += Input::get_param("id") + " ";
        if (v)
                sig += Utils::url_encode(string("state:true"));
        else
                sig += Utils::url_encode(string("state:false"));
        IPC::Instance().SendEvent("events", sig);
}

bool Scenario::set_value(bool val)
{
        force_input_bool(val);

        string sig = "output ";
        sig += Input::get_param("id") + " ";
        if (val)
                sig += Utils::url_encode(string("state:true"));
        else
                sig += Utils::url_encode(string("state:false"));
        IPC::Instance().SendEvent("events", sig);

        return true;
}
