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
#ifndef ACTIVITYCONFIGCONTROLLER_H
#define ACTIVITYCONFIGCONTROLLER_H

#include <Utils.h>

#include "ActivityController.h"
#include "ActivityConfigView.h"
#include "CalaosModel.h"

#include "ActivityConfigMenuController.h"


class ActivityConfigController: public ActivityController
{
private:
    ActivityConfigMenuController *mainMenuController;

    virtual void createView();

    void menuIconClick(std::string icon);
    void buttonClick(std::string button);

    void controllerFinished(ActivityController *controller);

public:
    ActivityConfigController(Evas *evas, Evas_Object *parent);
    ~ActivityConfigController();

    void addSubController(ActivityController *controller);

    void setButtonMode(std::string mode);
};

#endif // ACTIVITYCONFIGCONTROLLER_H
