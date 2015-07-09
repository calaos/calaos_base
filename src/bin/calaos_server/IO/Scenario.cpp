/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include "Scenario.h"
#include "AutoScenario.h"
#include "EcoreTimer.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_INPUT(Scenario)

Scenario::Scenario(Params &p):
    Input(p),
    Output(p),
    value(false),
    auto_scenario(NULL)
{
    cInfoDom("output") << "Scenario::Scenario(" << get_param("id") << "): Ok";

    set_param("gui_type", "scenario");

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

    cInfoDom("output") << "Scenario::~Scenario(): Ok";
}

void Scenario::force_input_bool(bool v)
{
    if (!Input::isEnabled()) return;

    value = v;
    EmitSignalInput();

    EventManager::create(CalaosEvent::EventInputChanged,
                         { { "id", Input::get_param("id") },
                           { "state", v?"true":"false" } });

    //reset input value to 0 after 250ms (simulate button press/release)
    EcoreTimer::singleShot(0.250, [=]() { value = false; });
}

bool Scenario::set_value(bool val)
{
    if (!Input::isEnabled()) return true;

    force_input_bool(val);

    EventManager::create(CalaosEvent::EventOutputChanged,
                         { { "id", Input::get_param("id") },
                           { "state", val?"true":"false" } });

    return true;
}

json_t *Scenario::toJson()
{
    json_t *jret = json_object();

    if (!auto_scenario)
        return jret;

    json_object_set_new(jret, "id", json_string(get_param("id").c_str()));
    json_object_set_new(jret, "cycle", json_string(auto_scenario->isCycling()?"true":"false"));
    json_object_set_new(jret, "enabled", json_string(auto_scenario->isDisabled()?"false":"true"));
    json_object_set_new(jret, "schedule", json_string(auto_scenario->isScheduled()?
                                                          auto_scenario->getIOPlage()->get_param("id").c_str():
                                                          "false"));
    json_object_set_new(jret, "category", json_string(auto_scenario->getCategory().c_str()));
    json_object_set_new(jret, "steps_count", json_string(Utils::to_string(auto_scenario->getRuleSteps().size()).c_str()));

    json_t *jsteps = json_array();

    for (uint i = 0;i < auto_scenario->getRuleSteps().size();i++)
    {
        json_t *jstep = json_object();
        json_object_set_new(jstep, "step_pause", json_string(Utils::to_string(auto_scenario->getStepPause(i)).c_str()));
        json_object_set_new(jstep, "step_type", json_string("standard"));

        json_t *jacts = json_array();
        for (int j = 0;j < auto_scenario->getStepActionCount(i);j++)
        {
            ScenarioAction sa = auto_scenario->getStepAction(i, j);
            json_t *jact = json_object();
            json_object_set_new(jact, "id", json_string(sa.io->get_param("id").c_str()));
            json_object_set_new(jact, "action", json_string(sa.action.c_str()));
            json_array_append_new(jacts, jact);

        }
        json_object_set_new(jstep, "actions", jacts);

        json_array_append_new(jsteps, jstep);
    }

    //add end step
    {
        json_t *jstep = json_object();
        json_object_set_new(jstep, "step_type", json_string("end"));

        json_t *jacts = json_array();
        for (int j = 0;j < auto_scenario->getEndStepActionCount();j++)
        {
            ScenarioAction sa = auto_scenario->getEndStepAction(j);
            json_t *jact = json_object();
            json_object_set_new(jact, "id", json_string(sa.io->get_param("id").c_str()));
            json_object_set_new(jact, "action", json_string(sa.action.c_str()));
            json_array_append_new(jacts, jact);
        }
        json_object_set_new(jstep, "actions", jacts);
        json_array_append_new(jsteps, jstep);
    }

    json_object_set_new(jret, "steps", jsteps);

    return jret;
}
