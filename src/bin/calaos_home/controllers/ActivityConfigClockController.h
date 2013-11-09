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
#ifndef ACTIVITYCONFIGCLOCKCONTROLLER_H
#define ACTIVITYCONFIGCLOCKCONTROLLER_H

#include <Utils.h>

#include "ActivityController.h"
#include "ActivityConfigView.h"
#include "CalaosModel.h"
#include "ActivityConfigController.h"

using namespace Utils;

class ActivityConfigClockController: public ActivityController
{
        private:
                void createView();

                ActivityConfigController *parentController;

        public:
                ActivityConfigClockController(Evas *evas, Evas_Object *parent, ActivityConfigController *parentController);
                ~ActivityConfigClockController();
};

#endif // ACTIVITYCONFIGCLOCKCONTROLLER_H
