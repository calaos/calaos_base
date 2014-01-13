/******************************************************************************
**  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include "ScreenSuspendView.h"
#include "ApplicationMain.h"
#include "ScreenManager.h"

static void _window_resize_cb(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
        ScreenSuspendView *view = reinterpret_cast<ScreenSuspendView *>(data);
        if (view)
                view->ResizeCb();
}

ScreenSuspendView::ScreenSuspendView(Evas *_e, Evas_Object *p):
        EdjeObject(ApplicationMain::getTheme(), _e),
        parent(p)
{
        evas_object_data_set(edje, "ScreenSuspendView", this);

        try
        {
                LoadEdje("calaos/screen_suspend");
        }
        catch(exception const& e)
        {
                Utils::logger("root") << Priority::CRIT << "ScreenSuspendView: Can't load edje" << log4cpp::eol;
                throw;
        }

        Move(0, 0);
        setLayer(5000);

        evas_object_event_callback_add(parent, EVAS_CALLBACK_RESIZE, _window_resize_cb, this);

        object_signal.connect(sigc::mem_fun(*this, &ScreenSuspendView::edjeCallback));

        ScreenManager::instance().wakeup_screen.connect([=]()
        {
                EmitSignal("now,wakeup", "calaos");
                EmitSignal("event,repeat,activate", "calaos");
                is_during_wakeup = false;
        });

        ScreenManager::instance().wakeup_screen_start.connect([=]()
        {
                if(is_during_wakeup)
                        return;

                EmitSignal("start,wakeup", "calaos");
                is_during_wakeup = true;

                if(Utils::get_config_option("dpms_block") == "true")
                {
                        //TODO, use a pin code instead of the full password here
                        //and show a PIN code keyboard only with numbers
//                        ApplicationMain::Instance().ShowKeyboard(
//                                                _("Locked screen, please enter your password to unlock."),
//                                                sigc::mem_fun(*this, &ScreenSuspendView::keyboardCallback),
//                                                false
//                        );
                }
        });

        ScreenManager::instance().suspend_screen.connect([=]()
        {
                //TOFIX
                //ApplicationMain::Instance().ForceCloseKeyboard();
        });

        ScreenManager::instance().suspend_screen_start.connect([=]()
        {
                EmitSignal("event,repeat,deactivate", "calaos");
                EmitSignal("start,suspend", "calaos");
                is_during_suspend = true;
        });
}

ScreenSuspendView::~ScreenSuspendView()
{
}

void ScreenSuspendView::ResizeCb()
{
        Evas_Coord w, h;

        evas_object_geometry_get(parent, NULL, NULL, &w, &h);
        Resize(w, h);
        Move(0, 0);
}

void ScreenSuspendView::edjeCallback(void *data, Evas_Object *obj, string emission, string source)
{
        if (!is_during_wakeup)
                ScreenManager::instance().updateTimer();

        if (emission == "event,repeat,deactivate" ||
            emission == "start,suspend" ||
            emission == "event,repeat,activate" ||
            emission == "start,wakeup" ||
            emission == "start,suspend,stop" ||
            emission == "now,wakeup" ||
            (is_during_suspend && emission == "mouse,in"))
        {
                return ;
        }

        if (is_during_suspend && source == "object" && emission == "end,suspend")
        {
                ScreenManager::instance().suspendNow();
                is_during_suspend = false;

                ecore_exe_run("killall -9 eskiss", NULL);
        }
        else if (is_during_wakeup && source == "object" && emission == "end,wakeup")
        {
                ScreenManager::instance().wakeUpNowWhenScreenOn();
        }
        else if (is_during_suspend)
        {
                EmitSignal("event,repeat,activate", "calaos");
                EmitSignal("start,suspend,stop", "calaos");
                EmitSignal("now,wakeup", "calaos");

                is_during_suspend = false;
                ScreenManager::instance().wakeUpNowWhenScreenOn();
        }
}

//void ScreenSuspendView::keyboardCallback(string txt)
//{
//        string pass = Utils::get_config_option("calaos_password");
//        if(txt != pass)
//        {
//                //TODO, don't hide the keyboard
//        }
//}
