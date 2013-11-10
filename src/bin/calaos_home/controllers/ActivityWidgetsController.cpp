/******************************************************************************
**  Copyright (c) 2006-2012, Calaos. All Rights Reserved.
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
#include "ActivityWidgetsController.h"

ActivityWidgetsController::ActivityWidgetsController(Evas *e, Evas_Object *p):
        ActivityController(e, p, ActivityViewFactory::ACTIVITY_VIEW_WIDGETS)
{
        CalaosModel::Instance();
}

ActivityWidgetsController::~ActivityWidgetsController()
{
}

void ActivityWidgetsController::dimView()
{
        ActivityWidgetsView *wview = dynamic_cast<ActivityWidgetsView *>(view);
        if (!wview) return;

        wview->dimView();
}

void ActivityWidgetsController::setEditMode()
{
        ActivityWidgetsView *wview = dynamic_cast<ActivityWidgetsView *>(view);
        if (!wview) return;

        wview->EditMode();
}

void ActivityWidgetsController::validEdit()
{
        ActivityWidgetsView *wview = dynamic_cast<ActivityWidgetsView *>(view);
        if (!wview) return;

        wview->SaveWidgets();
        wview->NormalMode();
}

void ActivityWidgetsController::cancelEdit()
{
        ActivityWidgetsView *wview = dynamic_cast<ActivityWidgetsView *>(view);
        if (!wview) return;

        wview->ResetPosition();
        wview->NormalMode();
}

int ActivityWidgetsController::getWidgetCount()
{
        ActivityWidgetsView *wview = dynamic_cast<ActivityWidgetsView *>(view);
        if (!wview) return 0;

        return wview->size();
}
