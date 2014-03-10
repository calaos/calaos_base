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
#ifndef ACTIVITYCAMERASELECTVIEW_H
#define ACTIVITYCAMERASELECTVIEW_H

#include <Utils.h>

#include "CalaosModel.h"
#include "ActivityView.h"
#include "IOView.h"

using namespace Utils;

class ActivityCameraSelectView: public ActivityView
{
private:
    Evas_Object *camera_video;
    Camera *camera;

    Evas_Object *list_item;

    Evas_Object *pager_position;
    Evas_Object *popup_position;

    void EdjeCallback(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void ButtonCallback(void *data, Evas_Object *_edje, std::string emission, std::string source);

    void buttonSavePositionClick();
    void positionSelected(void *data);
    void positionSaved();

public:
    ActivityCameraSelectView(Evas *evas, Evas_Object *parent);
    ~ActivityCameraSelectView();

    virtual void resetView();

    void ShowLoading();
    void HideLoading();

    virtual string getTitle();

    void setCamera(Camera *camera);
};

#endif // ACTIVITYCAMERASELECTVIEW_H
