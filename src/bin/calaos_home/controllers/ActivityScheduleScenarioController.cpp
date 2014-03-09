/******************************************************************************
**  Copyright (c) 2006-2011, Calaos. All Rights Reserved.
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
**  along with Calaos; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include "ActivityScheduleScenarioController.h"

ActivityScheduleScenarioController::ActivityScheduleScenarioController(Evas *e, Evas_Object *p):
    ActivityController(e, p, ActivityViewFactory::ACTIVITY_VIEW_SCHEDULE_SCENARIO)
{
    CalaosModel::Instance();
}

ActivityScheduleScenarioController::~ActivityScheduleScenarioController()
{
}

void ActivityScheduleScenarioController::createView()
{
    if (view) return;

    ActivityController::createView();

    ActivityScheduleScenarioView *scView = dynamic_cast<ActivityScheduleScenarioView *>(view);
    if (scenario->isScheduled())
    {
        scView->setTimeRangeInfos(scenario->ioScenario->range_infos, (scenario->scenario_data.params["cycle"] == "true"));
        scView->buttonValidPressed.connect(sigc::mem_fun(*this, &ActivityScheduleScenarioController::validModifySchedule));
    }
    else
    {
        scenario->ioScenario->range_infos = TimeRangeInfos(); //clear if needed
        scView->setTimeRangeInfos(scenario->ioScenario->range_infos, (scenario->scenario_data.params["cycle"] == "true"));
        scView->buttonValidPressed.connect(sigc::mem_fun(*this, &ActivityScheduleScenarioController::validAddSchedule));
    }
}

void ActivityScheduleScenarioController::validAddSchedule(TimeRangeInfos &tr)
{
    //create the schedule IO if needed
    scenario->createSchedule([=,&tr](IOBase *schedule)
    {
        //then set all TimeRanges for the Schedule IO
        scenario->setSchedules(tr);

        wants_quit.emit();
    });
}

void ActivityScheduleScenarioController::validModifySchedule(TimeRangeInfos &tr)
{
    //same protocol for setting timeranges
    validAddSchedule(tr);
}
