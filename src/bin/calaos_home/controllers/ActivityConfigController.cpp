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
#include "ApplicationMain.h"
#include "ActivityConfigController.h"
#include "ActivityConfigClockController.h"
#include "ActivityConfigPasswordController.h"
#include "ActivityConfigScreensaverController.h"

ActivityConfigController::ActivityConfigController(Evas *e, Evas_Object *p):
    ActivityController(e, p, ActivityViewFactory::ACTIVITY_VIEW_CONFIG)
{
    CalaosModel::Instance();
}

ActivityConfigController::~ActivityConfigController()
{
    DELETE_NULL(mainMenuController);
}

void ActivityConfigController::createView()
{
    if (view) return;

    ActivityController::createView();

    ActivityConfigView *configView = dynamic_cast<ActivityConfigView *>(view);
    configView->button_clicked.connect(sigc::mem_fun(*this, &ActivityConfigController::buttonClick));

    mainMenuController = new ActivityConfigMenuController(evas, parent);
    mainMenuController->menu_icon_click.connect(sigc::mem_fun(*this, &ActivityConfigController::menuIconClick));
    configView->addView(mainMenuController->getView());
}

void ActivityConfigController::menuIconClick(std::string icon)
{
    if (icon == "clock")
    {
        ActivityConfigClockController *controller = new ActivityConfigClockController(evas, parent, this);
        addSubController(controller);
        setButtonMode("mode,back");
    }
    else if (icon == "security")
    {
        ActivityConfigPasswordController *controller = new ActivityConfigPasswordController(evas, parent);
        addSubController(controller);
        setButtonMode("mode,back");
    }
    else if (icon == "screensaver")
    {
        ActivityConfigScreensaverController *controller = new ActivityConfigScreensaverController(evas, parent);
        addSubController(controller);
        setButtonMode("mode,back");
    }
}

void ActivityConfigController::buttonClick(std::string button)
{
    ActivityConfigView *configView = dynamic_cast<ActivityConfigView *>(view);

    if (configView->getTopView()->controller)
    {
        if (configView->getTopView()->controller->handleButtonClick(button))
            return;
    }

    if (button != "button.back") return;

    configView->EmitSignal("mode,menu", "calaos");

    if (configView->getTopView() != mainMenuController->getView())
    {
        configView->removeTopView();
        configView->setPartText("header.label", configView->getTopView()->getTitle());

        //Enable view reactivates view elements (camera video streams, ...)
        ActivityView *_view = reinterpret_cast<ActivityView *>(configView->getTopView());
        if (_view) _view->EnableView();
    }
}

void ActivityConfigController::addSubController(ActivityController *controller)
{
    ActivityConfigView *configView = dynamic_cast<ActivityConfigView *>(view);
    controller->view_deleted.connect(sigc::mem_fun(*this, &ActivityConfigController::controllerFinished));

    //Disable view disactivates view elements (camera video streams, ...)
    ActivityView *_view = reinterpret_cast<ActivityView *>(configView->getTopView());
    if (_view) _view->DisableView();

    configView->addView(controller->getView());
    configView->setPartText("header.label", controller->getView()->getTitle());
}

void ActivityConfigController::controllerFinished(ActivityController *controller)
{
    DELETE_NULL(controller);
}

void ActivityConfigController::setButtonMode(std::string mode)
{
    ActivityConfigView *configView = dynamic_cast<ActivityConfigView *>(view);
    configView->EmitSignal(mode, "calaos");
}

