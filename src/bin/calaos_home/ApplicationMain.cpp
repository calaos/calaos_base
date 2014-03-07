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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ApplicationMain.h"

#include <openssl/ssl.h>
#include <curl/curl.h>
#include <Ecore_Evas.h>
#include "ScreenSuspendView.h"

string ApplicationMain::theme = THEME_DIR"/default.edj";

static void _window_resize_cb(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
    ApplicationMain *app = reinterpret_cast<ApplicationMain *>(data);
    if (app)
        app->Resize();
}

ApplicationMain::ApplicationMain(int argc, char **argv)
{
    cInfo() <<  "Calaos Home, starting...";

    if (system("killall -9 eskiss") == -1)
        cCritical() <<  "Error forking !";

    //init random generator
    srand(time(NULL));

    //Init SSL and CURL
    SSL_load_error_strings();
    SSL_library_init();
    curl_global_init(CURL_GLOBAL_ALL);

    char *themefile = argvOptionParam(argv, argv + argc, "--theme");
    if (themefile)
    {
        ApplicationMain::theme = themefile;
        cInfo() <<  "Using specified theme file: " << ApplicationMain::getTheme();
    }

    //Init efl core
    if (!eina_init())
        throw (runtime_error("Unable to init Eina"));
    if (!ecore_init())
        throw (runtime_error("Unable to init Ecore"));
    if (!ecore_con_init())
        throw (runtime_error("Unable to init Ecore-Con"));
    if (!ecore_con_url_init())
        throw (runtime_error("Unable to init Ecore-Con-Url"));
    if (!evas_init())
        throw (runtime_error("Unable to init Evas"));
    if (!ecore_evas_init())
        throw (runtime_error("Unable to init Ecore-Evas"));
    if (!edje_init())
        throw (runtime_error("Unable to init Edje"));

    edje_frametime_set(1.0 / 60.0);
    edje_scale_set(1.0);

    if (!elm_init(argc, argv))
        throw (runtime_error("Unable to init Elementary"));

    //Load Calaos specific ELM extensions
    elm_theme_extension_add(NULL, ApplicationMain::getTheme());

    //Create the main window
    window = elm_win_add(NULL, "calaos-home", ELM_WIN_BASIC);
    elm_win_title_set(window, "Calaos Home");
    elm_win_borderless_set(window, true);

    bool bFullscreen = argvOptionCheck(argv, argv + argc, "--fullscreen");
    elm_win_fullscreen_set(window, bFullscreen);

    //Automatically quit main loop when the window is closed
    elm_policy_set(ELM_POLICY_QUIT, ELM_POLICY_QUIT_LAST_WINDOW_CLOSED);
    elm_win_autodel_set(window, true);

    if (argvOptionCheck(argv, argv + argc, "--set-elm-config"))
    {
        //force setting the correct elementary options for touchscreen
        elm_config_finger_size_set(10);
        elm_config_scroll_bounce_enabled_set(true);
        elm_config_scroll_thumbscroll_enabled_set(true);
        elm_config_scroll_thumbscroll_threshold_set(24);
        elm_config_scroll_thumbscroll_momentum_threshold_set(100.0);
        elm_config_scroll_bounce_friction_set(0.5);
        elm_config_scroll_page_scroll_friction_set(0.5);
        elm_config_scroll_bring_in_scroll_friction_set(0.5);
        elm_config_scroll_zoom_friction_set(0.5);
        elm_config_scroll_thumbscroll_friction_set(1.0);
        elm_config_scroll_thumbscroll_border_friction_set(0.5);
        elm_config_scroll_thumbscroll_sensitivity_friction_set(0.25);
    }

    evas_object_event_callback_add(window, EVAS_CALLBACK_RESIZE, _window_resize_cb, this);
    evas = evas_object_evas_get(window);

    Evas_Object *bg = elm_bg_add(window);
    evas_object_size_hint_weight_set(bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    elm_win_resize_object_add(window, bg);
    evas_object_show(bg);
    evas_object_size_hint_min_set(bg, 200, 200);
    elm_bg_color_set(bg, 0, 0, 0);

    layout = elm_layout_add(window);
    if (!elm_layout_file_set(layout, ApplicationMain::getTheme(), EDJE_GROUP_MAIN_LAYOUT))
    {
        string e = "Unable to find group \"";
        e += EDJE_GROUP_MAIN_LAYOUT;
        e += "\" in theme \"";
        e += ApplicationMain::getTheme();
        e += "\"";
        throw (runtime_error(e));
    }

    //create the screen suspend object and put it on the canvas
    ScreenSuspendView *screen_suspend = new ScreenSuspendView(evas, window);
    screen_suspend->Show();
    screen_suspend->setAutoDelete(true);

    evas_object_size_hint_weight_set(layout, 1.0, 1.0);
    evas_object_show(layout);

    evas_object_resize(window, 1024, 768);
    evas_object_show(window);
    ecore_evas_focus_set(ecore_evas_ecore_evas_get(evas_object_evas_get(window)), true);

    Resize();

    try
    {
        controller = new ApplicationController(evas, layout);
    }
    catch(exception const& e)
    {
        cCritical() <<  "Can't create ApplicationController";
        throw;
    }
}

ApplicationMain::~ApplicationMain()
{
    DELETE_NULL(controller)

            elm_shutdown();
    edje_shutdown();
    ecore_evas_shutdown();
    evas_shutdown();
    ecore_con_url_shutdown();
    ecore_con_shutdown();
    ecore_shutdown();
    eina_shutdown();
}

void ApplicationMain::Run()
{
    elm_run();
}

void ApplicationMain::Stop()
{
    elm_exit();
}

void ApplicationMain::Resize()
{
    Evas_Coord w, h;

    evas_object_geometry_get(window, NULL, NULL, &w, &h);
    evas_object_resize(layout, w, h);
    evas_object_move(layout, 0, 0);
}
