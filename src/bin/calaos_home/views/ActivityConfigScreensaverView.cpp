/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include "ActivityConfigScreensaverView.h"
#include "GengridItemConfig.h"
#include <GenlistItemSimpleHeader.h>
#include <GenlistItemSimple.h>
#include <Calendar.h>
#include <ApplicationMain.h>

#define DPMS_STANDBY_MAX_VALUE 240 // Max value for DPMS standby (in minutes)

ActivityConfigScreensaverView::ActivityConfigScreensaverView(Evas *_e, Evas_Object *_parent):
    ActivityView(_e, _parent, "calaos/page/config/screensaver")
{
    EdjeObject *slider;

    setPartText("tab1.text", _("Configure Touch Screen"));
    setPartText("tab1.text.detail", _("Resume : <light_blue>Touch Screen</light_blue><br><small>Configure your Touch Screen !</small>"));
    setPartText("tab2.text", _("About"));
    setPartText("tab2.text.detail", _("About : <light_blue>Calaos products</light_blue><br><small>Touchscreen solutions.</small>"));

    setPartText("module_screen", _("Configure power management settings"));
    setPartText("module_screen_desc", _("Enable if you want to activate automatic screen blanking and the delay after which the screen will be turned off. You can also ask for a password when the screen is turned on again."));
    setPartText("tab1.title_icon", _("TouchScreen"));
    setPartText("tab1.subtitle_icon", _("TouchScreen configuration."));
    setPartText("tab2.web.label", _("Web Site : "));
    setPartText("tab2.web", CALAOS_WEBSITE_URL);
    setPartText("tab2.mail.label", _("Email : "));
    setPartText("tab2.mail", CALAOS_CONTACT_EMAIL);
    setPartText("tab2.copyright", CALAOS_COPYRIGHT_TEXT);

    setPartText("module_screen_time_desc", _("Time before screensaver activation : "));
    setPartText("module_screen suspend_desc", _("Enable screen saver : "));

    // Create a new slider for the DPMS standby option
    slider = new EdjeObject(ApplicationMain::getTheme(), evas);
    slider->setAutoDelete(true);

    double slider_val;
    std::string option_val;
    bool enabled = false;

    if (get_config_option("dpms_enable") == "true")
        enabled = true;
    else
        enabled = false;

    option_val = get_config_option("dpms_standby");
    from_string(option_val, slider_val);
    if ((slider_val == 0) && enabled)
        enabled = false;

    enabled ?  EmitSignal("screen,suspend,check", "calaos") :
      EmitSignal("screen,suspend,uncheck", "calaos");

    setPartText("module_screen_time_value", Utils::to_string((int)(slider_val)) + _(" minutes"));

    slider->addCallback("object", "*",
                        [=](void *data, Evas_Object *edje_object, std::string emission, std::string source)
    {
        // Change value on screen when slider move
        if (emission == "slider,move")
        {
            double x;
            std::string val;

            slider->getDragValue("slider", &x, NULL);
            val = Utils::to_string((int)(x * DPMS_STANDBY_MAX_VALUE)) +  _(" minutes");
            setPartText("module_screen_time_value", val);
        }
        // Set new value in local_config.xml when slider,changed is received
        else if (emission == "slider,changed")
        {
            double x;

            slider->getDragValue("slider", &x, NULL);
            if (x == 0)
            {
                EmitSignal("screen,suspend,uncheck", "calaos");
                set_config_option("dpms_enable", "false");
            }
            else
            {
                EmitSignal("screen,suspend,check", "calaos");
                set_config_option("dpms_enable", "true");
            }
            // Value is store in seconds
            set_config_option("dpms_standby", Utils::to_string((int)(x * DPMS_STANDBY_MAX_VALUE)));
        }

    }
    );
    slider->LoadEdje("calaos/slider/horizontal/default");
    slider->Show();
    slider->setDragValue("slider", slider_val / DPMS_STANDBY_MAX_VALUE, slider_val / DPMS_STANDBY_MAX_VALUE);
    Swallow(slider, "dpms_standby_slider.swallow", true);

    addCallback("object", "*", [=](void *data, Evas_Object *edje_object, std::string emission, std::string source)
    {
        if (emission == "screen,suspend,check")
        {
            EmitSignal("screen,suspend,uncheck", "calaos");
            set_config_option("dpms_enable", "false");
            set_config_option("dpms_standby", "0.0");
            slider->setDragValue("slider", 0.0 / DPMS_STANDBY_MAX_VALUE, 0.0 / DPMS_STANDBY_MAX_VALUE);
            setPartText("module_screen_time_value", _("0 minutes"));

        }
        else if (emission == "screen,suspend,uncheck")
        {
            EmitSignal("screen,suspend,check", "calaos");
            set_config_option("dpms_enable", "true");
            set_config_option("dpms_standby", "1.0");
            slider->setDragValue("slider", 1.0 / DPMS_STANDBY_MAX_VALUE, 1.0 / DPMS_STANDBY_MAX_VALUE);

            setPartText("module_screen_time_value", _("1 minute"));
        }
    });

}

ActivityConfigScreensaverView::~ActivityConfigScreensaverView()
{

}

void ActivityConfigScreensaverView::resetView()
{
}
