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
#include "ActivityCameraListController.h"
#include "ActivityMediaController.h"

ActivityCameraListController::ActivityCameraListController(Evas *e, Evas_Object *p, ActivityMediaController *pc):
    ActivityController(e, p, ActivityViewFactory::ACTIVITY_VIEW_CAMERA_LIST),
    parentController(pc),
    cameraSelectController(NULL)
{
    CalaosModel::Instance();
}

ActivityCameraListController::~ActivityCameraListController()
{
}

void ActivityCameraListController::createView()
{
    if (view) return;

    ActivityController::createView();

    ActivityCameraListView *cameraView = dynamic_cast<ActivityCameraListView *>(view);

    cameraView->button_left_click.connect(sigc::mem_fun(*this, &ActivityCameraListController::clickLeft));
    cameraView->button_right_click.connect(sigc::mem_fun(*this, &ActivityCameraListController::clickRight));
    cameraView->addCallback("cameras", "animation,done", sigc::mem_fun(*this, &ActivityCameraListController::doneCallback));
    cameraView->addCallback("camera", "select,*", sigc::mem_fun(*this, &ActivityCameraListController::cameraSelectCallback));

    if (!CalaosModel::Instance().isLoaded())
    {
        cameraView->ShowLoading();

        CalaosModel::Instance().home_loaded.connect(sigc::mem_fun(*this, &ActivityCameraListController::load_done));

        return;
    }

    page = 0;
    updatePageView();
    updateScenarios();
}

void ActivityCameraListController::load_done()
{
    ActivityCameraListView *cameraView = dynamic_cast<ActivityCameraListView *>(view);
    cameraView->HideLoading();

    page = 0;
    updatePageView();
    updateScenarios();
}

void ActivityCameraListController::updatePageView()
{
    ActivityCameraListView *cameraView = dynamic_cast<ActivityCameraListView *>(view);

    std::list<Camera *>::iterator it = CalaosModel::Instance().getCamera()->cameras.begin();
    int i = 0;

    for (int j = 0;j < page * 4;j++)
        it++;

    for (int j = 0;j < 4;j++)
    {
        //disable unused camera
        cameraView->disableCamera(j);
    }

    for (;it != CalaosModel::Instance().getCamera()->cameras.end() && i < 4;
         it++, i++)
    {
        Camera *camera = (*it);

        cameraView->setCamera(camera, i);
    }

    if (page == 0)
        cameraView->DisableLeftButton();
    else
        cameraView->EnableLeftButton();

    int page_count = (CalaosModel::Instance().getCamera()->cameras.size() / 4) - 1;
    if (CalaosModel::Instance().getCamera()->cameras.size() % 4 > 0)
        page_count++;

    if (page < page_count &&
        (CalaosModel::Instance().getCamera()->cameras.size() > 4))
        cameraView->EnableRightButton();
    else
        cameraView->DisableRightButton();

    cameraView->EmitSignal("show", "calaos");
}

void ActivityCameraListController::updateScenarios()
{
    ActivityCameraListView *cameraView = dynamic_cast<ActivityCameraListView *>(view);

    const std::list<IOBase *> &scenarios = CalaosModel::Instance().getHome()->getCacheScenariosPref();
    std::list<IOBase *> _page;

    std::list<IOBase *>::const_iterator it = scenarios.begin();
    for (int i = 0;it != scenarios.end();it++, i++)
    {
        IOBase *io = *it;
        _page.push_back(io);

        if (_page.size() >= 6)
        {
            cameraView->addScenarioPage(_page);
            _page.clear();
        }
    }

    if (_page.size() > 0)
    {
        while (_page.size() < 6) _page.push_back(NULL);
        cameraView->addScenarioPage(_page);
    }
}

void ActivityCameraListController::clickLeft()
{
    page--;
    if (page < 0)
    {
        page = 0;
        return;
    }

    ActivityCameraListView *cameraView = dynamic_cast<ActivityCameraListView *>(view);
    cameraView->EmitSignal("hide,right", "calaos");
}

void ActivityCameraListController::clickRight()
{
    page++;
    if (page > (int)(CalaosModel::Instance().getCamera()->cameras.size() / 4))
    {
        page = (CalaosModel::Instance().getCamera()->cameras.size() / 4);
        return;
    }

    ActivityCameraListView *cameraView = dynamic_cast<ActivityCameraListView *>(view);
    cameraView->EmitSignal("hide,left", "calaos");
}

void ActivityCameraListController::doneCallback(void *data, Evas_Object *edje_object, std::string emission, std::string source)
{
    updatePageView();
}

void ActivityCameraListController::cameraSelectCallback(void *data, Evas_Object *edje_object, std::string emission, std::string source)
{
    if (emission.substr(0, 7) == "select,")
        emission = emission.erase(0, 7);
    int position;
    from_string(emission, position);

    if ((page * 4 + position - 1) >= (int)CalaosModel::Instance().getCamera()->cameras.size())
        return;

    std::list<Camera *>::iterator it = CalaosModel::Instance().getCamera()->cameras.begin();
    for (int j = 0;j < page * 4 + position - 1;j++)
        it++;

    ActivityCameraSelectController *controller = new ActivityCameraSelectController(*it, evas, parent);
    parentController->addSubController(controller);
}
