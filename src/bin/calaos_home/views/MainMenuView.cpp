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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ActivityIntl.h"
#include "MainMenuView.h"
#include "ApplicationMain.h"

#define LIST_ITEM_CALLBACK(_class, it_name) \
static void _item_##it_name##_cb(void *data, Evas_Object *obj, void *event_info) \
{ _class *menu = reinterpret_cast<_class *>(data); if (data) menu->_click_##it_name(); } \
void _class::_click_##it_name() { on_##it_name##_click.emit(); }

LIST_ITEM_CALLBACK(MainMenuView, reboot)
LIST_ITEM_CALLBACK(MainMenuView, widget)
LIST_ITEM_CALLBACK(MainMenuView, addwidget)
LIST_ITEM_CALLBACK(MainMenuView, suspend)

MainMenuView::MainMenuView(Evas *_e, Evas_Object *_p):
        BaseView(_e, _p)
{
        try
        {
                LoadEdje("calaos/main/menu");
                setPartText("menu_buttons_home_label", _("My Home"));
                setPartText("menu_buttons_media_label", _("Medias"));
                setPartText("menu_buttons_scenarios_label", _("Scenarios"));
                setPartText("menu_buttons_configuration_label", _("Configuration"));

        }
        catch(exception const& e)
        {
                cCriticalDom("root") <<  "MainMenuView: Can't load edje";
                throw;
        }

        addCallback("menu", "*", sigc::mem_fun(*this, &MainMenuView::EdjeCallback));
        addCallback("*", "pressed", sigc::mem_fun(*this, &MainMenuView::ButtonPressedCallback));

        elm_object_part_content_set(parent, "calaos.main.menu", getEvasObject());

        list = elm_list_add(parent);
        Swallow(list, "menu.homeboard");

        elm_object_style_set(list, "menu/homeboard");
        elm_list_select_mode_set(list, ELM_OBJECT_SELECT_MODE_ALWAYS);

        evas_object_show(list);

        on_widget_click.connect(sigc::mem_fun(*this, &MainMenuView::editWidgetClick));

        Show();
}

MainMenuView::~MainMenuView()
{
        DELETE_NULL_FUNC(evas_object_del, list)
}

void MainMenuView::EdjeCallback(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
        if (emission == "home")
                on_home_click.emit();
        else if (emission == "media")
                on_media_click.emit();
        else if (emission == "scenario")
                on_scenario_click.emit();
        else if (emission == "config")
                on_config_click.emit();
        else if (emission == "homeboard,open")
        {
                on_menu_open.emit();

                elm_list_clear(list);

                Evas_Object *icon;
                icon = elm_icon_add(list);
                elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/widget");
                elm_image_resizable_set(icon, true, true);
                item_config_addwidget = elm_list_item_append(list, _("Add new widget."), icon, NULL, _item_addwidget_cb, this);

                icon = elm_icon_add(list);
                elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/widget");
                elm_image_resizable_set(icon, true, true);
                item_config_widget = elm_list_item_append(list, _("Widgets configuration."), icon, NULL, _item_widget_cb, this);

                icon = elm_icon_add(list);
                elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/screensaver");
                elm_image_resizable_set(icon, true, true);
                item_sleep_screen = elm_list_item_append(list, _("Shut the screen off."), icon, NULL, _item_suspend_cb, this);

                icon = elm_icon_add(list);
                elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/reboot");
                elm_image_resizable_set(icon, true, true);
                item_reboot = elm_list_item_append(list, _("Reboot the machine."), icon, NULL, _item_reboot_cb, this);

                elm_list_go(list);

                //Update DPMS info on button
                edje_object_part_text_set(elm_list_item_object_get(item_sleep_screen),
                                          "object.description",
                                          _("Touch the screen once to wake up the machine."));
                if (Utils::get_config_option("dpms_enable") != "true")
                        edje_object_part_text_set(elm_list_item_object_get(item_sleep_screen), "object.more_infos",
                                                  _("Auto: Off"));
                else
                {
                        int val;
                        from_string(Utils::get_config_option("dpms_standby"), val);

                        string _t = "Auto: " + Utils::time2string(val);
                        edje_object_part_text_set(elm_list_item_object_get(item_sleep_screen), "object.more_infos",
                                        _t.c_str());
                }

                //Update number of widgets
                string nb = Utils::to_string(ApplicationMain::Instance().getMainController()->getWidgetController()->getWidgetCount());
                nb += " Widgets";

                edje_object_part_text_set(elm_list_item_object_get(item_config_addwidget), "object.more_infos",
                                nb.c_str());
                edje_object_part_text_set(elm_list_item_object_get(item_config_addwidget),
                                          "object.description",
                                          _("Add widgets on the desktop."));

                edje_object_part_text_set(elm_list_item_object_get(item_config_widget), "object.more_infos",
                                nb.c_str());
                edje_object_part_text_set(elm_list_item_object_get(item_config_widget),
                                          "object.description",
                                          _("Move and setup your widgets on the desktop."));

                //Reboot item
                edje_object_part_text_set(elm_list_item_object_get(item_reboot),
                                          "object.description",
                                          _("Please wait during the reboot."));
                edje_object_part_text_set(elm_list_item_object_get(item_reboot),
                                          "object.more_infos", "");
        }
        else if (emission == "homeboard,close")
        {
                on_menu_close.emit();
        }
}

void MainMenuView::ButtonPressedCallback(void *data, Evas_Object *_edje, string emission, string source)
{
        if (source == "button.valid")
        {
                EmitSignal("widget,normal", "calaos");
                on_widget_valid_click.emit();
        }
        else if (source == "button.cancel")
        {
                EmitSignal("widget,normal", "calaos");
                on_widget_cancel_click.emit();
        }
}

void MainMenuView::editWidgetClick()
{
        EmitSignal("widget,edit", "calaos");
}

void MainMenuView::UnselectAllMenu()
{
        EmitSignal("unselect,all", "calaos");
}

void MainMenuView::DisableMenu()
{
        EmitSignal("disable", "calaos");
}

void MainMenuView::EnableMenu()
{
        EmitSignal("enable", "calaos");
}

void MainMenuView::ShowMenu()
{
        EmitSignal("show,menu", "calaos");
        elm_object_signal_emit(parent, "show,menu", "calaos");
}

void MainMenuView::HideMenu()
{
        EmitSignal("hide,menu", "calaos");
        elm_object_signal_emit(parent, "hide,menu", "calaos");
}

void MainMenuView::SelectHome()
{
        EmitSignal("select,home", "calaos");
}

void MainMenuView::SelectMedia()
{
        EmitSignal("select,media", "calaos");
}

void MainMenuView::SelectScenario()
{
        EmitSignal("select,scenario", "calaos");
}

void MainMenuView::SelectConfig()
{
        EmitSignal("select,config", "calaos");
}

void MainMenuView::OpenLinkMenu()
{
        EmitSignal("menu,link,open", "calaos");
}

void MainMenuView::CloseLinkMenu()
{
        EmitSignal("menu,link,close", "calaos");
}

void MainMenuView::setVersionString(string version)
{
        setPartText("calaos.version", version);
}
