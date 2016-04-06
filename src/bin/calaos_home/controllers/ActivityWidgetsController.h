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
#ifndef ACTIVITYWIDGETSCONTROLLER_H
#define ACTIVITYWIDGETSCONTROLLER_H

#include <Utils.h>

#include "ActivityController.h"
#include "ActivityWidgetsView.h"
#include "CalaosModel.h"


class ActivityWidgetsController: public ActivityController
{
public:
    ActivityWidgetsController(Evas *evas, Evas_Object *parent);
    ~ActivityWidgetsController();

    void dimView();
    void setEditMode();

    void validEdit();
    void cancelEdit();

    int getWidgetCount();

    bool AddWidget(ModuleDef &mtype, int x, int y, int w = 0, int h = 0, std::string id = "");
};

#endif // ACTIVITYWIDGETSCONTROLLER_H
