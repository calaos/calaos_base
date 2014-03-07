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
#ifndef ACTIVITYSCENARIOSVIEW_H
#define ACTIVITYSCENARIOSVIEW_H

#include <Utils.h>

#include "ActivityView.h"
#include "CalaosModel.h"

using namespace Utils;

class ActivityScenariosView: public ActivityView
{
private:
    Evas_Object *calendar;
    Evas_Object *schedule_list, *scenario_list;

    enum { VIEW_MODE_ALL, VIEW_MODE_LIGHT, VIEW_MODE_SHUTTER, VIEW_MODE_SCHEDULE };
    int view_mode;

    void buttonPressed(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void loadScenarioList();

public:
    ActivityScenariosView(Evas *evas, Evas_Object *parent);
    ~ActivityScenariosView();

    virtual void resetView();

    void ShowLoading();
    void HideLoading();

    void loadScenarios();

    sigc::signal<void> buttonCreatePressed;

    sigc::signal<void, Scenario *> schedule_add_click;
    sigc::signal<void, Scenario *> schedule_modify_click;
    sigc::signal<void, Scenario *> schedule_del_click;
};

#endif // ACTIVITYSCENARIOSVIEW_H
