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
#include "ApplicationController.h"
#include "ApplicationMain.h"
#include "Modules.h"
#include "GenlistItemWidget.h"
#include "ScreenManager.h"
#include "ActivityIntl.h"

ApplicationController::ApplicationController(Evas *_e, Evas_Object *_l):
        evas(_e),
        layout(_l),
        mouseCursor(NULL),
        menu_hidden(false),
        homeController(NULL),
        mediaController(NULL),
        scenariosController(NULL),
        configController(NULL),
        keyboardController(NULL),
        webController(NULL),
        editScController(NULL),
        scheduleScController(NULL)
{
        if (Utils::get_config_option("show_cursor") == "true")
        {
                mouseCursor = new EdjeObject(ApplicationMain::getTheme(), evas);

                try
                {
                        mouseCursor->LoadEdje("calaos/cursor");
                }
                catch(exception const& e)
                {
                        cCritical() <<  "ApplicationController: Can't load mouse cursor";
                        throw;
                }

                int w, h;
                edje_object_size_min_get(mouseCursor->getEvasObject(), &w, &h);
                mouseCursor->Resize(w, h);
                mouseCursor->Show();

                ecore_evas_object_cursor_set(ecore_evas_ecore_evas_get(evas), mouseCursor->getEvasObject(), EVAS_LAYER_MAX - 32, w / 2, h / 2);
        }
        else
        {
                Evas_Object *cursor = evas_object_rectangle_add(evas);
                evas_object_color_set(cursor, 0, 0, 0, 0);
                evas_object_resize(cursor, 1, 1);
                evas_object_show(cursor);

                ecore_evas_object_cursor_set(ecore_evas_ecore_evas_get(evas), cursor, EVAS_LAYER_MAX - 32, 0, 0);
        }

        menuView = new MainMenuView(evas, layout);

        menuView->setVersionString(get_config_option("fw_version"));

        menuView->on_home_click.connect(sigc::mem_fun(*this, &ApplicationController::onMenuHomeClick));
        menuView->on_media_click.connect(sigc::mem_fun(*this, &ApplicationController::onMenuMediaClick));
        menuView->on_scenario_click.connect(sigc::mem_fun(*this, &ApplicationController::onMenuScenarioClick));
        menuView->on_config_click.connect(sigc::mem_fun(*this, &ApplicationController::onMenuConfigClick));

        menuView->on_reboot_click.connect(sigc::mem_fun(*this, &ApplicationController::onMenuRebootClick));
        menuView->on_suspend_click.connect(sigc::mem_fun(*this, &ApplicationController::onMenuSuspendClick));
        menuView->on_widget_click.connect(sigc::mem_fun(*this, &ApplicationController::onMenuWidgetClick));
        menuView->on_addwidget_click.connect(sigc::mem_fun(*this, &ApplicationController::onMenuAddWidgetClick));

        //Start the model instance
        CalaosModel::Instance().home_loaded.connect(sigc::mem_fun(*this, &ApplicationController::home_loaded));
        CalaosModel::Instance().login_failed.connect(sigc::mem_fun(*this, &ApplicationController::login_failed));

        contentView = new MainContentView(evas, layout);
        elm_object_part_content_set(layout, "calaos.main.content", contentView->getSmartObject());

        widgetsController = new ActivityWidgetsController(evas, layout);

        contentView->addView(widgetsController->getView());

        menuView->on_menu_open.connect(sigc::mem_fun(*widgetsController, &ActivityWidgetsController::dimView));
        menuView->on_menu_close.connect(sigc::mem_fun(*widgetsController, &ActivityWidgetsController::resetView));
        menuView->on_widget_valid_click.connect(sigc::mem_fun(*widgetsController, &ActivityWidgetsController::validEdit));
        menuView->on_widget_cancel_click.connect(sigc::mem_fun(*widgetsController, &ActivityWidgetsController::cancelEdit));
}

ApplicationController::~ApplicationController()
{
        DELETE_NULL(mouseCursor);
        DELETE_NULL(menuView);
        DELETE_NULL(contentView);

        DELETE_NULL(widgetsController);
        DELETE_NULL(homeController);
        DELETE_NULL(mediaController);
        DELETE_NULL(scenariosController);
        DELETE_NULL(configController);

        DELETE_NULL(keyboardController);
        DELETE_NULL(webController);

        DELETE_NULL(editScController);
        DELETE_NULL(scheduleScController);
}

void ApplicationController::resetOtherViews()
{
        if (contentView->getTopView() != widgetsController->getView())
        {
                contentView->removeTopView();
                homeController = NULL;
                mediaController = NULL;
                scenariosController = NULL;
                configController = NULL;
        }
}

void ApplicationController::onMenuHomeClick()
{
        if (!homeController)
        {
                resetOtherViews();

                homeController = new ActivityHomeController(evas, layout);
                homeController->wants_quit.connect(sigc::mem_fun(*this, &ApplicationController::activityQuit));
                homeController->view_deleted.connect(sigc::mem_fun(*this, &ApplicationController::controllerFinished));

                contentView->addView(homeController->getView());
        }
        else
        {
                homeController->resetView();
        }
}

void ApplicationController::onMenuMediaClick()
{
        if (!mediaController)
        {
                resetOtherViews();

                mediaController = new ActivityMediaController(evas, layout);
                mediaController->wants_quit.connect(sigc::mem_fun(*this, &ApplicationController::activityQuit));
                mediaController->view_deleted.connect(sigc::mem_fun(*this, &ApplicationController::controllerFinished));

                contentView->addView(mediaController->getView());
        }
        else
        {
                mediaController->resetView();
        }
}

void ApplicationController::onMenuScenarioClick()
{
        if (!scenariosController)
        {
                resetOtherViews();

                scenariosController = new ActivityScenariosController(evas, layout);
                scenariosController->wants_quit.connect(sigc::mem_fun(*this, &ApplicationController::activityQuit));
                scenariosController->view_deleted.connect(sigc::mem_fun(*this, &ApplicationController::controllerFinished));

                contentView->addView(scenariosController->getView());
        }
        else
        {
                scenariosController->resetView();
        }
}

void ApplicationController::onMenuConfigClick()
{
        if (!configController)
        {
                resetOtherViews();

                configController = new ActivityConfigController(evas, layout);
                configController->wants_quit.connect(sigc::mem_fun(*this, &ApplicationController::activityQuit));
                configController->view_deleted.connect(sigc::mem_fun(*this, &ApplicationController::controllerFinished));

                contentView->addView(configController->getView());
        }
        else
        {
                configController->resetView();
        }
}

void ApplicationController::ShowKeyboard(string subtitle, ActivityKeyboardCb callback, bool multiline, string oldtext, int type)
{
        if (!keyboardController)
        {
                keyboardController = new ActivityKeyboardController(evas, layout, subtitle, callback, multiline, type);
                keyboardController->wants_quit.connect(sigc::mem_fun(*this, &ApplicationController::activityKeyboardQuit));
                keyboardController->view_deleted.connect(sigc::mem_fun(*this, &ApplicationController::controllerFinished));

                contentView->addView(keyboardController->getView());
                if (menu_hidden) menuView->ShowMenu();
                menuView->DisableMenu();

                keyboardController->setText(oldtext);
        }
}

void ApplicationController::ShowWebBrowser(string url)
{
        if (!webController)
        {
                webController = new ActivityWebController(evas, layout, url);
                webController->wants_quit.connect(sigc::mem_fun(*this, &ApplicationController::activityWebQuit));
                webController->view_deleted.connect(sigc::mem_fun(*this, &ApplicationController::controllerFinished));

                contentView->addView(webController->getView());
                menuView->HideMenu();
                menu_hidden = true;
        }
}

void ApplicationController::ShowScenarioEditor(Scenario *scenario)
{
        if (!editScController)
        {
                editScController = new ActivityEditScenarioController(evas, layout);
                editScController->wants_quit.connect(sigc::mem_fun(*this, &ApplicationController::activityEditScenarioQuit));
                editScController->view_deleted.connect(sigc::mem_fun(*this, &ApplicationController::controllerFinished));
                editScController->setScenario(scenario);

                contentView->addView(editScController->getView());
                menuView->DisableMenu();
        }
}

void ApplicationController::ShowScenarioSchedule(Scenario *scenario)
{
        if (!scheduleScController)
        {
                scheduleScController = new ActivityScheduleScenarioController(evas, layout);
                scheduleScController->wants_quit.connect(sigc::mem_fun(*this, &ApplicationController::activityScheduleScenarioQuit));
                scheduleScController->view_deleted.connect(sigc::mem_fun(*this, &ApplicationController::controllerFinished));
                scheduleScController->setScenario(scenario);

                contentView->addView(scheduleScController->getView());
                menuView->DisableMenu();
        }
}

void ApplicationController::onMenuRebootClick()
{
        Evas_Object *table = createPaddingTable(evas, layout, 260, 200);

        Evas_Object *glist = elm_genlist_add(layout);
        elm_object_style_set(glist, "calaos");
        elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
        evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
        evas_object_show(glist);

        Evas_Object *popup = elm_ctxpopup_add(layout);
        elm_object_style_set(popup, "calaos");
        evas_object_size_hint_min_set(popup, 300, 240);

        GenlistItemSimple *item;

        item = new GenlistItemSimple(evas, glist, _("Restart only application"), true);
        item->Append(glist);
        item->item_selected.connect([this, popup](void *)
        {
                elm_ctxpopup_dismiss(popup);
                menuView->CloseLinkMenu();

                system("systemctl restart calaos-home");
        });

        item = new GenlistItemSimple(evas, glist, _("Reboot the machine"), true);
        item->Append(glist);
        item->item_selected.connect([this, popup](void *)
        {
                elm_ctxpopup_dismiss(popup);
                menuView->CloseLinkMenu();

                system("sync");
                system("reboot");
        });

        elm_table_pack(table, glist, 1, 1, 1, 1);

        elm_object_content_set(popup, table);

        Evas_Coord x,y;
        evas_pointer_canvas_xy_get(evas, &x, &y);
        evas_object_move(popup, x, y);
        evas_object_show(popup);
}

void ApplicationController::onMenuWidgetClick()
{
        menuView->CloseLinkMenu();

        widgetsController->setEditMode();
}

void ApplicationController::onMenuAddWidgetClick()
{
        Evas_Object *table = createPaddingTable(evas, layout, 260, 200);

        Evas_Object *glist = elm_genlist_add(layout);
        elm_object_style_set(glist, "calaos");
        elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
        evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
        evas_object_show(glist);

        Evas_Object *popup = elm_ctxpopup_add(layout);
        elm_object_style_set(popup, "calaos");
        evas_object_size_hint_min_set(popup, 300, 240);

        vector<ModuleDef> mods = ModuleManager::Instance().getAvailableModules();

        for (uint i = 0;i < mods.size();i++)
        {
                ModuleDef def = mods[i];
                GenlistItemWidget *item = new GenlistItemWidget(evas, glist, def);
                item->Append(glist);
                item->item_selected.connect([this,i,popup](void *)
                {
                        vector<ModuleDef> m = ModuleManager::Instance().getAvailableModules();
                        widgetsController->AddWidget(m[i], 200, 200);
                        elm_ctxpopup_dismiss(popup);
                        menuView->CloseLinkMenu();
                });
        }

        elm_table_pack(table, glist, 1, 1, 1, 1);

        elm_object_content_set(popup, table);

        Evas_Coord x,y;
        evas_pointer_canvas_xy_get(evas, &x, &y);
        evas_object_move(popup, x, y);
        evas_object_show(popup);
}

void ApplicationController::onMenuSuspendClick()
{
        //delay a little the start of screen suspend
        //because the mouse move is going to reset the timer
        EcoreTimer::singleShot(0.1, sigc::slot<void>([]()
        {
                ScreenManager::instance().suspend();
        }));
        menuView->CloseLinkMenu();
}

void ApplicationController::home_loaded()
{
        //cout << CalaosModel::Instance().toString() << endl;
}

void ApplicationController::login_failed(string host)
{
        //TODO: Do something with that information
}

void ApplicationController::controllerFinished(ActivityController *controller)
{
        DELETE_NULL(controller);
}

void ApplicationController::activityQuit()
{
        resetOtherViews();
        menuView->UnselectAllMenu();
}

void ApplicationController::activityKeyboardQuit()
{
        if (!keyboardController) return;
        if (contentView->getTopView() == keyboardController->getView())
        {
                contentView->removeTopView();
                menuView->EnableMenu();
                keyboardController = NULL;

                if (menu_hidden) menuView->HideMenu();
        }
}

void ApplicationController::activityWebQuit()
{
        if (!webController) return;
        if (contentView->getTopView() == webController->getView())
        {
                contentView->removeTopView();
                menuView->ShowMenu();
                webController = NULL;
                menu_hidden = false;
        }
}

void ApplicationController::activityEditScenarioQuit()
{
        if (!editScController) return;
        if (contentView->getTopView() == editScController->getView())
        {
                contentView->removeTopView();
                menuView->EnableMenu();
                editScController = NULL;
        }
}

void ApplicationController::activityScheduleScenarioQuit()
{
        if (!scheduleScController) return;
        if (contentView->getTopView() == scheduleScController->getView())
        {
                contentView->removeTopView();
                menuView->EnableMenu();
                scheduleScController = NULL;
        }
}
