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
#ifndef ACTIVITYVIEW_H
#define ACTIVITYVIEW_H

#include <Utils.h>

#include "BaseView.h"

using namespace Utils;

class ActivityView: public BaseView
{
public:
    void _button_quit_clicked();

protected:
    Evas_Object *box_buttons;
    Evas_Object *button_quit;

public:
    ActivityView(Evas *evas, Evas_Object *parent);
    ActivityView(Evas *evas, Evas_Object *parent, string collection);
    virtual ~ActivityView();

    virtual void resetView() = 0;

    virtual void DisableView() { }
    virtual void EnableView() { }

    sigc::signal<void> activity_quit;
};

class ActivityViewFactory
{
private:
    static string viewTypeString(int viewType);

public:
    enum { ACTIVITY_VIEW_NONE, ACTIVITY_VIEW_HOME, ACTIVITY_VIEW_MEDIA,
           ACTIVITY_VIEW_SCENARIOS, ACTIVITY_VIEW_CONFIG, ACTIVITY_VIEW_WIDGETS,
           ACTIVITY_VIEW_MEDIA_MENU, ACTIVITY_VIEW_CONFIG_MENU,
           ACTIVITY_VIEW_CONFIG_CLOCK, ACTIVITY_VIEW_CONFIG_PASSWORD,
           ACTIVITY_VIEW_CONFIG_SCREENSAVER,
           ACTIVITY_VIEW_CAMERA_LIST, ACTIVITY_VIEW_CAMERA_SELECT,
           ACTIVITY_VIEW_AUDIO_LIST,
           ACTIVITY_VIEW_KEYBOARD,
           ACTIVITY_VIEW_WEB,
           ACTIVITY_VIEW_EDIT_SCENARIO, ACTIVITY_VIEW_SCHEDULE_SCENARIO
         };

    static ActivityView *CreateView(Evas *evas, Evas_Object *parent, int viewType);
};

#endif // ACTIVITYVIEW_H
