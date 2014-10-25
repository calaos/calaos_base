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
#ifndef APPLICATIONCONTROLLER_H
#define APPLICATIONCONTROLLER_H

#include <Utils.h>

#include <Evas.h>
#include <Elementary.h>
#include "EdjeObject.h"

#include "MainMenuView.h"
#include "MainContentView.h"
#include "ActivityController.h"

#include "CalaosModel.h"

#include "ActivityHomeController.h"
#include "ActivityMediaController.h"
#include "ActivityScenariosController.h"
#include "ActivityConfigController.h"
#include "ActivityWidgetsController.h"
#include "ActivityKeyboardController.h"
#include "ActivityWebController.h"
#include "ActivityEditScenarioController.h"
#include "ActivityScheduleScenarioController.h"

using namespace Utils;

class EAPI ApplicationController: public sigc::trackable
{
private:
    Evas *evas;
    Evas_Object *layout;
    Evas_Object *main_pager; /* This is the main elementary pager, which is holding all "pages" (home, media, camera, ...) */

    MainMenuView *menuView;
    MainContentView *contentView;
    EdjeObject *mouseCursor;

    bool menu_hidden;

    /* The main "pages" comes here */
    ActivityWidgetsController *widgetsController;
    ActivityHomeController *homeController;
    ActivityMediaController *mediaController;
    ActivityScenariosController *scenariosController;
    ActivityConfigController *configController;
    ActivityKeyboardController *keyboardController;
    ActivityWebController *webController;
    ActivityEditScenarioController *editScController;
    ActivityScheduleScenarioController *scheduleScController;

    void onMenuHomeClick();
    void onMenuMediaClick();
    void onMenuScenarioClick();
    void onMenuConfigClick();

    void onMenuRebootClick();
    void onMenuWidgetClick();
    void onMenuAddWidgetClick();
    void onMenuSuspendClick();

    /* CalaosModel signals */
    void home_loaded();
    void login_failed(string host);

    void activityQuit();
    void activityKeyboardQuit();
    void activityWebQuit();
    void activityEditScenarioQuit();
    void activityScheduleScenarioQuit();

    void controllerFinished(ActivityController *controller);
    void resetOtherViews(bool showLastView = true);

public:
    ApplicationController(Evas *evas, Evas_Object *layout);
    ~ApplicationController();

    void ShowKeyboard(string subtitle, ActivityKeyboardCb callback, bool multiline, string oldtext = "", int type = 0);
    void ShowWebBrowser(string url);

    void ShowScenarioEditor(Scenario *scenario);
    void ShowScenarioSchedule(Scenario *scenario);

    ActivityWidgetsController *getWidgetController() { return widgetsController; }
};

#endif // APPLICATIONCONTROLLER_H
