/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef ACTIVITYCAMERALISTVIEW_H
#define ACTIVITYCAMERALISTVIEW_H

#include <Utils.h>

#include "CalaosModel.h"
#include "ActivityView.h"
#include "IOView.h"

#include <PagingView.h>

using namespace Utils;

class ActivityCameraListView;
class ActivityCameraObject
{
public:
    Camera *camera; //the Camera Object
    Evas_Object *video; //The elementary video object
    ActivityCameraListView *view; //the view
    int position; //position in view

    ActivityCameraObject():
        camera(NULL),
        video(NULL),
        view(NULL)
    { }
};

class ActivityCameraListView: public ActivityView
{
private:
    vector<ActivityCameraObject> cameras;

    vector<IOView *> scenarios;
    PagingView *page_view;

    void EdjeCallback(void *data, Evas_Object *_edje, std::string emission, std::string source);

    void pagerDragStart();
    void pagerDragStop();

public:
    ActivityCameraListView(Evas *evas, Evas_Object *parent);
    ~ActivityCameraListView();

    virtual void resetView();

    void setCamera(Camera *camera, int position);
    void disableCamera(int position);

    void EnableLeftButton();
    void DisableLeftButton();
    void EnableRightButton();
    void DisableRightButton();

    void ShowLoading();
    void HideLoading();

    void addScenarioPage(list<IOBase *> &scenarios_io);

    virtual string getTitle() { return "Vidéosurveillance"; }

    virtual void EnableView();
    virtual void DisableView();

    sigc::signal<void> button_left_click;
    sigc::signal<void> button_right_click;
};

#endif // ACTIVITYCAMERALISTVIEW_H
