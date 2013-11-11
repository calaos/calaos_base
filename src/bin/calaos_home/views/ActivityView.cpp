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
#include "ActivityView.h"

#include "ActivityHomeView.h"
#include "ActivityMediaView.h"
#include "ActivityScenariosView.h"
#include "ActivityConfigView.h"
#include "ActivityWidgetsView.h"

#include "ActivityMediaMenuView.h"
#include "ActivityCameraListView.h"
#include "ActivityCameraSelectView.h"
#include "ActivityAudioListView.h"

#include "ActivityKeyboardView.h"
#include "ActivityWebView.h"

#include "ActivityEditScenarioView.h"
#include "ActivityScheduleScenarioView.h"

#include "ActivityConfigMenuView.h"
#include "ActivityConfigClockView.h"
#include "ActivityConfigPasswordView.h"
#include "ActivityConfigScreensaverView.h"

static void _elm_button_quit(void *data, Evas_Object *obj, void *event_info)
{
        ActivityView *ac = reinterpret_cast<ActivityView *>(data);
        if (!ac) return;

        ac->_button_quit_clicked();
}

ActivityView::ActivityView(Evas *_e, Evas_Object *_parent):
        BaseView(_e, _parent)
{
}

ActivityView::ActivityView(Evas *_e, Evas_Object *_parent, string _collection):
        BaseView(_e, _parent),
        box_buttons(NULL),
        button_quit(NULL)
{
        try
        {
                LoadEdje(_collection);
        }
        catch(exception const& e)
        {
                Utils::logger("root") << Priority::CRIT << "ActivityView: Can't load edje" << log4cpp::eol;
                throw;
        }

        if (edje_object_part_exists(edje, "button.quit"))
        {
                button_quit = edje_object_part_external_object_get(edje, "button.quit");
                evas_object_smart_callback_add(button_quit, "clicked", _elm_button_quit, this);
        }
}

ActivityView::~ActivityView()
{
}

void ActivityView::_button_quit_clicked()
{
        activity_quit.emit();
}

ActivityView *ActivityViewFactory::CreateView(Evas *evas, Evas_Object *parent, int viewType)
{
        ActivityView *view = NULL;

        try
        {
                switch (viewType)
                {
                case ACTIVITY_VIEW_HOME: view = new ActivityHomeView(evas, parent); break;
                case ACTIVITY_VIEW_MEDIA: view = new ActivityMediaView(evas, parent); break;
                case ACTIVITY_VIEW_SCENARIOS: view = new ActivityScenariosView(evas, parent); break;
                case ACTIVITY_VIEW_CONFIG: view = new ActivityConfigView(evas, parent); break;
                case ACTIVITY_VIEW_CONFIG_MENU: view = new ActivityConfigMenuView(evas, parent); break;
                case ACTIVITY_VIEW_CONFIG_CLOCK: view = new ActivityConfigClockView(evas, parent); break;
                case ACTIVITY_VIEW_CONFIG_PASSWORD: view = new ActivityConfigPasswordView(evas, parent); break;
                case ACTIVITY_VIEW_CONFIG_SCREENSAVER: view = new ActivityConfigScreensaverView(evas, parent); break;
                case ACTIVITY_VIEW_MEDIA_MENU: view = new ActivityMediaMenuView(evas, parent); break;
                case ACTIVITY_VIEW_CAMERA_LIST: view = new ActivityCameraListView(evas, parent); break;
                case ACTIVITY_VIEW_CAMERA_SELECT: view = new ActivityCameraSelectView(evas, parent); break;
                case ACTIVITY_VIEW_AUDIO_LIST: view = new ActivityAudioListView(evas, parent); break;
                case ACTIVITY_VIEW_WIDGETS: view = new ActivityWidgetsView(evas, parent); break;
                case ACTIVITY_VIEW_KEYBOARD: view = new ActivityKeyboardView(evas, parent); break;
                case ACTIVITY_VIEW_WEB: view = new ActivityWebView(evas, parent); break;
                case ACTIVITY_VIEW_EDIT_SCENARIO: view = new ActivityEditScenarioView(evas, parent); break;
                case ACTIVITY_VIEW_SCHEDULE_SCENARIO: view = new ActivityScheduleScenarioView(evas, parent); break;
                default:
                case ACTIVITY_VIEW_NONE: break;
                }
        }
        catch (exception const& e)
        {
                Utils::logger("root") << Priority::CRIT << "ActivityViewFactory: Can't create viewType:" << viewTypeString(viewType) << log4cpp::eol;
                throw;
        }

        if (!view)
        {
                Utils::logger("root") << Priority::CRIT << "ActivityViewFactory: Can't create viewType:" << viewTypeString(viewType) << log4cpp::eol;
                throw;
        }

        return view;
}

string ActivityViewFactory::viewTypeString(int viewType)
{
        switch (viewType)
        {
                case ACTIVITY_VIEW_HOME: return "ActivityHomeView";
                case ACTIVITY_VIEW_MEDIA: return "ActivityMediaView";
                case ACTIVITY_VIEW_SCENARIOS: return "ActivityScenariosView";
                case ACTIVITY_VIEW_CONFIG: return "ActivityConfigView";
                case ACTIVITY_VIEW_CONFIG_MENU: return "ActivityConfigMenuView";
                case ACTIVITY_VIEW_CONFIG_CLOCK: return "ActivityConfigClockView";
                case ACTIVITY_VIEW_CONFIG_PASSWORD: return "ActivityConfigPasswordView";
                case ACTIVITY_VIEW_CONFIG_SCREENSAVER: return "ActivityConfigScreensaverView";
                case ACTIVITY_VIEW_CAMERA_SELECT: return "ActivityCameraSelectView";
                case ACTIVITY_VIEW_CAMERA_LIST: return "ActivityCameraListView";
                case ACTIVITY_VIEW_AUDIO_LIST: return "ActivityAudioListView";
                case ACTIVITY_VIEW_WIDGETS: return "ActivityWidgetsView";
                case ACTIVITY_VIEW_MEDIA_MENU: return "ActivityMediaMenuView";
                case ACTIVITY_VIEW_KEYBOARD: return "ActivityKeyboardView";
                case ACTIVITY_VIEW_WEB: return "ActivityWebView";
                case ACTIVITY_VIEW_EDIT_SCENARIO: return "ActivityEditScenarioView";
                case ACTIVITY_VIEW_SCHEDULE_SCENARIO: return "ActivityScheduleScenarioView";
                default:
                case ACTIVITY_VIEW_NONE: return "NONE";
        }
}
