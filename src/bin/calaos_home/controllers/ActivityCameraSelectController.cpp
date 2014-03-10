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
#include "ActivityCameraSelectController.h"

ActivityCameraSelectController::ActivityCameraSelectController(Camera *cam, Evas *e, Evas_Object *p):
    ActivityController(e, p, ActivityViewFactory::ACTIVITY_VIEW_CAMERA_SELECT),
    camera(cam)
{
    CalaosModel::Instance();
}

ActivityCameraSelectController::~ActivityCameraSelectController()
{
}

void ActivityCameraSelectController::createView()
{
    if (view) return;

    ActivityController::createView();

    ActivityCameraSelectView *cameraView = dynamic_cast<ActivityCameraSelectView *>(view);

    if (!CalaosModel::Instance().isLoaded())
    {
        cameraView->ShowLoading();

        CalaosModel::Instance().home_loaded.connect(sigc::mem_fun(*this, &ActivityCameraSelectController::load_done));

        return;
    }

    updateView();
}

void ActivityCameraSelectController::load_done()
{
    ActivityCameraSelectView *cameraView = dynamic_cast<ActivityCameraSelectView *>(view);
    cameraView->HideLoading();

    updateView();
}

void ActivityCameraSelectController::updateView()
{
    ActivityCameraSelectView *cameraView = dynamic_cast<ActivityCameraSelectView *>(view);
    cameraView->setCamera(camera);
}
