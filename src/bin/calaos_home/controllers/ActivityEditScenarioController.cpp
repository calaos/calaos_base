/******************************************************************************
**  Copyright (c) 2006-2012, Calaos. All Rights Reserved.
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
#include "ActivityEditScenarioController.h"

ActivityEditScenarioController::ActivityEditScenarioController(Evas *e, Evas_Object *p):
    ActivityController(e, p, ActivityViewFactory::ACTIVITY_VIEW_EDIT_SCENARIO)
{
    CalaosModel::Instance();
}

ActivityEditScenarioController::~ActivityEditScenarioController()
{
}

void ActivityEditScenarioController::createView()
{
    if (view) return;

    ActivityController::createView();

    ActivityEditScenarioView *scView = dynamic_cast<ActivityEditScenarioView *>(view);
    scView->buttonValidPressed.connect(sigc::mem_fun(*this, &ActivityEditScenarioController::createScenario));
    if (scenario)
    {
        scView->setScenarioData(scenario->scenario_data);
    }
    else
    {
        ScenarioData sd;
        scView->setScenarioData(sd);
    }
}

void ActivityEditScenarioController::createScenario()
{
    ActivityEditScenarioView *scView = dynamic_cast<ActivityEditScenarioView *>(view);

    if (scenario)
    {
        scenario->scenario_data = scView->getScenarioData();
        CalaosModel::Instance().getScenario()->modifyScenario(scenario);
    }
    else
    {
        CalaosModel::Instance().getScenario()->createScenario(scView->getScenarioData());
    }

    wants_quit.emit();
}
