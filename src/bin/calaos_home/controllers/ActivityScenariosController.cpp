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
#include "ActivityScenariosController.h"
#include "ApplicationMain.h"

ActivityScenariosController::ActivityScenariosController(Evas *e, Evas_Object *p):
    ActivityController(e, p, ActivityViewFactory::ACTIVITY_VIEW_SCENARIOS)
{
    CalaosModel::Instance();
}

ActivityScenariosController::~ActivityScenariosController()
{
}

void ActivityScenariosController::createView()
{
    if (view) return;

    ActivityController::createView();

    ActivityScenariosView *scView = dynamic_cast<ActivityScenariosView *>(view);
    scView->buttonCreatePressed.connect(sigc::mem_fun(*this, &ActivityScenariosController::createScenario));
    scView->schedule_add_click.connect(sigc::mem_fun(*this, &ActivityScenariosController::scheduleAdd));
    scView->schedule_modify_click.connect(sigc::mem_fun(*this, &ActivityScenariosController::scheduleModify));
    scView->schedule_del_click.connect(sigc::mem_fun(*this, &ActivityScenariosController::scheduleDel));

    if (!CalaosModel::Instance().isLoaded())
    {
        scView->ShowLoading();

        CalaosModel::Instance().home_loaded.connect(sigc::mem_fun(*this, &ActivityScenariosController::load_done));

        return;
    }

    updateView();

    CalaosModel::Instance().getScenario()->scenario_new.connect(sigc::mem_fun(*this, &ActivityScenariosController::reloadView));
    CalaosModel::Instance().getScenario()->scenario_change.connect(sigc::mem_fun(*this, &ActivityScenariosController::reloadView));
}

void ActivityScenariosController::load_done()
{
    ActivityScenariosView *scView = dynamic_cast<ActivityScenariosView *>(view);
    scView->HideLoading();

    updateView();

    CalaosModel::Instance().getScenario()->scenario_new.connect(sigc::mem_fun(*this, &ActivityScenariosController::reloadView));
    CalaosModel::Instance().getScenario()->scenario_change.connect(sigc::mem_fun(*this, &ActivityScenariosController::reloadView));
}

void ActivityScenariosController::createScenario()
{
    ApplicationMain::Instance().ShowScenarioEditor();
}

void ActivityScenariosController::updateView()
{
    ActivityScenariosView *scView = dynamic_cast<ActivityScenariosView *>(view);
    scView->loadScenarios();
}

void ActivityScenariosController::reloadView(Scenario *sc)
{
    updateView();
}

void ActivityScenariosController::scheduleAdd(Scenario *sc)
{
    ApplicationMain::Instance().ShowScenarioSchedule(sc);
}

void ActivityScenariosController::scheduleModify(Scenario *sc)
{
    ApplicationMain::Instance().ShowScenarioSchedule(sc);
}

void ActivityScenariosController::scheduleDel(Scenario *sc)
{
    sc->deleteSchedule();
}
