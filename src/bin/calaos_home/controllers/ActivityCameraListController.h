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
#ifndef ACTIVITYCAMERALISTCONTROLLER_H
#define ACTIVITYCAMERALISTCONTROLLER_H

#include <Utils.h>

#include "ActivityController.h"
#include "ActivityCameraListView.h"
#include "CalaosModel.h"
#include "ActivityCameraSelectController.h"

using namespace Utils;

class ActivityMediaController;

class ActivityCameraListController: public ActivityController
{
        private:
                void createView();

                ActivityMediaController *parentController;
                ActivityCameraSelectController *cameraSelectController;

                int page;

                void updatePageView();
                void updateScenarios();

                void clickLeft();
                void clickRight();

                void load_done();

                void doneCallback(void *data, Evas_Object *edje_object, string emission, string source);
                void cameraSelectCallback(void *data, Evas_Object *edje_object, string emission, string source);

        public:
                ActivityCameraListController(Evas *evas, Evas_Object *parent, ActivityMediaController *parentController);
                ~ActivityCameraListController();
};

#endif // ACTIVITYCAMERALISTCONTROLLER_H
