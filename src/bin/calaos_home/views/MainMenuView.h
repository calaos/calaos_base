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
#ifndef MAINMENUVIEW_H
#define MAINMENUVIEW_H

#include <Utils.h>

#include "BaseView.h"

using namespace Utils;

class MainMenuView: public BaseView
{
        public:
                //These are for private use to get callbacks from C code.
                void _click_reboot();
                void _click_suspend();
                void _click_addwidget();
                void _click_widget();

        private:
                Evas_Object *list;

                Elm_Object_Item *item_config_addwidget;
                Elm_Object_Item *item_config_widget;
                Elm_Object_Item *item_sleep_screen;
                Elm_Object_Item *item_reboot;

                void EdjeCallback(void *data, Evas_Object *_edje, std::string emission, std::string source);
                void ButtonPressedCallback(void *data, Evas_Object *_edje, std::string emission, std::string source);
                void editWidgetClick();

        public:
                MainMenuView(Evas *evas, Evas_Object *parent);
                ~MainMenuView();

                void UnselectAllMenu();
                void DisableMenu();
                void EnableMenu();

                void ShowMenu();
                void HideMenu();

                void SelectHome();
                void SelectMedia();
                void SelectScenario();
                void SelectConfig();

                void OpenLinkMenu();
                void CloseLinkMenu();

                void setVersionString(string version);

                /**
                 * UI Signals
                 */
                sigc::signal<void> on_home_click;
                sigc::signal<void> on_media_click;
                sigc::signal<void> on_scenario_click;
                sigc::signal<void> on_config_click;

                sigc::signal<void> on_reboot_click;
                sigc::signal<void> on_suspend_click;
                sigc::signal<void> on_widget_click;
                sigc::signal<void> on_addwidget_click;

                sigc::signal<void> on_menu_open;
                sigc::signal<void> on_menu_close;

                sigc::signal<void> on_widget_valid_click;
                sigc::signal<void> on_widget_cancel_click;
};

#endif // MAINMENUVIEW_H
