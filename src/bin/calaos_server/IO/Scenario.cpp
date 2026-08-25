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
#include "Scenario.h"
#include "AutoScenario.h"
#include "Timer.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(Scenario)

Scenario::Scenario(Params &p):
    IOBase(p, IOBase::IO_INOUT),
    value(false),
    auto_scenario(NULL)
{
    ioDoc->friendlyNameSet("Scenario");
    ioDoc->descriptionSet(_("A scenario variable. Use this like a virtual button to start a scenario (list of actions)"));
    ioDoc->actionAdd("true", _("Start the scenario"));
    ioDoc->actionAdd("false", _("Stop the scenario (only for special looping scenarios)"));
    ioDoc->conditionAdd("true", _("Event triggered when scenario is started"));
    ioDoc->actionAdd("changed", _("Event triggered on any change"));

    ioDoc->paramAdd("auto_scenario", _("Internal use only for Auto Scenario. read only."), IODoc::TYPE_STRING, false, string(), true);

    cInfoDom("output") << "Scenario::Scenario(" << get_param("id") << "): Ok";

    set_param("gui_type", "scenario");

    if (get_param("auto_scenario") != "")
    {
        auto_scenario = new AutoScenario(this);
        setAutoScenario(true);
    }

    if (!get_params().Exists("visible")) set_param("visible", "true");
    if (!get_params().Exists("log_history")) set_param("log_history", "true");
}

Scenario::~Scenario()
{
    DELETE_NULL(auto_scenario);
}

bool Scenario::set_value(bool val)
{
    if (!isEnabled()) return true;

    /* T3.18 - THE cut, and it closes both ways in: the button/set_state, and
     * the schedule (rulePlageStart has a single action, ioScenario = "true",
     * so it comes through here too).
     *
     * ONLY on val == true: set_value(false) is the STOP path (ruleStop), and
     * cutting it would make an already running scenario impossible to stop.
     *
     * The two gates. isBroken() is live and derived from Rule::isDisabled(),
     * so no client can forge it; disabledMissingIo is persisted and sticky, so
     * it survives the reboot and the IO coming back. Either one refuses.
     * `return true` is the convention of the isEnabled() guard just above: the
     * command was accepted and deliberately did nothing.
     */
    if (val && auto_scenario &&
        (auto_scenario->isBroken() || auto_scenario->isDisabledMissingIo()))
    {
        cWarningDom("scenario") << "Scenario '" << get_param("name") << "' ("
                                << get_param("id") << ") did NOT start: "
                                << (auto_scenario->isBroken()?
                                        "it references missing IO(s)":
                                        "it is disabled until it is re-enabled "
                                        "explicitly (disabled_missing_io)")
                                << ". Missing IO(s): "
                                << auto_scenario->getMissingIoDescription();
        return true;
    }

    value = val;
    EmitSignalIO();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", val?"true":"false" } },
                         value); //Only log scenario activation

    //reset input value to 0 after 250ms (simulate button press/release)
    //T3.40: armed through the IO's lifetime tag - a deleteIO() in those
    //250 ms used to leave this write pointing at a destroyed Scenario.
    ioAlive.singleShot(0.250, [this]() { value = false; });

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
                                                          auto_scenario->getIOTimeRange()->get_param("id").c_str():
                                                          "false"));
    json_object_set_new(jret, "category", json_string(auto_scenario->getCategory().c_str()));

    /* T3.18 - three keys, and all three are needed. They are READ ONLY:
     * buildAutoscenarioModify() does not consume any of them.
     *
     * "broken" and "disabled_missing_io" DIVERGE ON PURPOSE. broken=false with
     * the flag still true is "repaired, waiting for a manual re-enable" - the
     * state the whole ticket exists for, and the one an UI has to turn into a
     * "re-enable" button. Emit only one of the two and that state becomes
     * indistinguishable from a healthy scenario, which is the silence T3.18
     * removes.
     * "missing_ios" is what to repair, empty when there is nothing to.
     */
    json_object_set_new(jret, "broken",
                        json_string(auto_scenario->isBroken()?"true":"false"));
    json_object_set_new(jret, "disabled_missing_io",
                        json_string(auto_scenario->isDisabledMissingIo()?"true":"false"));
    json_object_set_new(jret, "missing_ios",
                        json_string(auto_scenario->getMissingIoDescription().c_str()));

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

            //Defensive, same guard as AutoScenario::getCategory(): an index
            //that does not resolve gives back an empty action
            if (!sa.io) continue;

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

            if (!sa.io) continue;

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
