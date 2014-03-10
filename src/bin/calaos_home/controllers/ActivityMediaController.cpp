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
#include "ActivityMediaController.h"

#include "ActivityCameraListController.h"
#include "ActivityAudioListController.h"

#include "ApplicationMain.h"

ActivityMediaController::ActivityMediaController(Evas *e, Evas_Object *p):
    ActivityController(e, p, ActivityViewFactory::ACTIVITY_VIEW_MEDIA)
{
    CalaosModel::Instance();
}

ActivityMediaController::~ActivityMediaController()
{
    DELETE_NULL(mainMenuController);
}

void ActivityMediaController::createView()
{
    if (view) return;

    ActivityController::createView();

    ActivityMediaView *mediaView = dynamic_cast<ActivityMediaView *>(view);
    mediaView->button_clicked.connect(sigc::mem_fun(*this, &ActivityMediaController::buttonClick));

    mainMenuController = new ActivityMediaMenuController(evas, parent);
    mainMenuController->menu_icon_click.connect(sigc::mem_fun(*this, &ActivityMediaController::menuIconClick));
    mediaView->addView(mainMenuController->getView());
}

void ActivityMediaController::menuIconClick(string icon)
{
    if (icon == "eskiss")
    {
        ecore_exe_run("eskiss -c=FALSE", NULL);
    }
    else if (icon == "camera")
    {
        ActivityCameraListController *controller = new ActivityCameraListController(evas, parent, this);
        addSubController(controller);
        setButtonMode("mode,back");
    }
    else if (icon == "music")
    {
        ActivityAudioListController *controller = new ActivityAudioListController(evas, parent, this);
        addSubController(controller);
        setButtonMode("mode,back");
    }
    else if (icon == "web")
    {
        ApplicationMain::Instance().ShowWebBrowser();
    }
}

void ActivityMediaController::buttonClick(string button)
{
    ActivityMediaView *mediaView = dynamic_cast<ActivityMediaView *>(view);

    if (mediaView->getTopView()->controller)
    {
        if (mediaView->getTopView()->controller->handleButtonClick(button))
            return;
    }

    if (button != "button.back") return;

    mediaView->EmitSignal("mode,menu", "calaos");

    if (mediaView->getTopView() != mainMenuController->getView())
    {
        mediaView->removeTopView();
        mediaView->setPartText("header.label", mediaView->getTopView()->getTitle());

        //Enable view reactivates view elements (camera video streams, ...)
        ActivityView *_view = reinterpret_cast<ActivityView *>(mediaView->getTopView());
        if (_view) _view->EnableView();
    }
}

void ActivityMediaController::addSubController(ActivityController *controller)
{
    ActivityMediaView *mediaView = dynamic_cast<ActivityMediaView *>(view);
    controller->view_deleted.connect(sigc::mem_fun(*this, &ActivityMediaController::controllerFinished));

    //Disable view disactivates view elements (camera video streams, ...)
    ActivityView *_view = reinterpret_cast<ActivityView *>(mediaView->getTopView());
    if (_view) _view->DisableView();

    mediaView->addView(controller->getView());
    mediaView->setPartText("header.label", controller->getView()->getTitle());
}

void ActivityMediaController::controllerFinished(ActivityController *controller)
{
    DELETE_NULL(controller);
}

void ActivityMediaController::setButtonMode(string mode)
{
    ActivityMediaView *mediaView = dynamic_cast<ActivityMediaView *>(view);
    mediaView->EmitSignal(mode, "calaos");
}
