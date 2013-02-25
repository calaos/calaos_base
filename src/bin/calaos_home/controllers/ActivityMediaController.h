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
#ifndef ACTIVITYMEDIACONTROLLER_H
#define ACTIVITYMEDIACONTROLLER_H

#include <Utils.h>

#include "ActivityController.h"
#include "ActivityMediaView.h"
#include "CalaosModel.h"

#include "ActivityMediaMenuController.h"

using namespace Utils;

class ActivityMediaController: public ActivityController
{
        private:
                ActivityMediaMenuController *mainMenuController;

                virtual void createView();

                void menuIconClick(string icon);
                void buttonClick(string button);

                void controllerFinished(ActivityController *controller);

        public:
                ActivityMediaController(Evas *evas, Evas_Object *parent);
                ~ActivityMediaController();

                void addSubController(ActivityController *controller);

                void setButtonMode(string mode);
};

#endif // ACTIVITYMEDIACONTROLLER_H
