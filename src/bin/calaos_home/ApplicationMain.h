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
#ifndef APPLICATIONMAIN_H
#define APPLICATIONMAIN_H

#include <Utils.h>

#include <Eina.h>
#include <Ecore.h>
#include <Ecore_Con.h>
#include <Evas.h>
#include <Ecore_Evas.h>
#include <Edje.h>
#include <Elementary.h>

#include "ApplicationController.h"

#include "ScenarioModel.h"

using namespace Utils;

#define EDJE_GROUP_MAIN_LAYOUT  "calaos/main/layout"

class EAPI ApplicationMain
{
        private:

                //The main canvas
                Evas *evas;

                //The main window object
                Evas_Object *window;

                //The elementary layout, it contains the app
                Evas_Object *layout;

                //The main application controller
                ApplicationController *controller;

                static string theme;

                ApplicationMain(int argc, char **argv);
        public:
                static ApplicationMain &Instance(int argc = 0, char **argv = NULL)
                {
                        static ApplicationMain appmain(argc, argv);
                        return appmain;
                }
                ~ApplicationMain();

                void Run();
                void Stop();

                void Resize();

                static const char *getTheme()
                {
                        return theme.c_str();
                }

                //type is 0 for UTF-8 text, 1 is for keeping evas textblock markup instead
                void ShowKeyboard(string subtitle, ActivityKeyboardCb callback, bool multiline, string oldtext = "", int type = 0)
                {
                        if (controller) controller->ShowKeyboard(subtitle, callback, multiline, oldtext, type);
                }

                void ShowWebBrowser(string url = "")
                {
                        if (controller) controller->ShowWebBrowser(url);
                }

                void ShowScenarioEditor(Scenario *scenario = NULL)
                {
                        if (controller) controller->ShowScenarioEditor(scenario);
                }

                void ShowScenarioSchedule(Scenario *scenario)
                {
                        if (controller) controller->ShowScenarioSchedule(scenario);
                }

                ApplicationController *getMainController() { return controller; }
};

#endif // APPLICATIONMAIN_H
