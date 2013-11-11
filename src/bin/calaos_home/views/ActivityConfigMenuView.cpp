/******************************************************************************
**  Copyright (c) 2006-2013, Calaos. All Rights Reserved.
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
#include "ActivityConfigMenuView.h"
#include "GengridItemConfig.h"

ActivityConfigMenuView::ActivityConfigMenuView(Evas *_e, Evas_Object *_parent):
        ActivityView(_e, _parent, "calaos/config/menu")
{

    grid = elm_gengrid_add(_parent);

    elm_gengrid_select_mode_set(grid, ELM_OBJECT_SELECT_MODE_ALWAYS);

    elm_object_style_set(grid, "calaos");
    evas_object_show(grid);

    elm_gengrid_group_item_size_set(grid, 200, 120);

    naviframe = elm_naviframe_add(parent);
    evas_object_show(naviframe);
    Swallow(naviframe, "naviframe.swallow");

    GengridItemConfig *item;

    item = new GengridItemConfig(evas, grid, "Heure et Dates", "clock");
    item->Append(grid);
    item->item_selected.connect([=](void *data)
    {
        cout << "click on item clock!" << endl;
        menu_item_clicked.emit("clock");
    });

    item = new GengridItemConfig(evas, grid, "Mot de passe", "security");
    item->Append(grid);
    item->item_selected.connect([=](void *data)
    {
        cout << "click on item security!" << endl;
        menu_item_clicked.emit("security");
    });

    item = new GengridItemConfig(evas, grid, "Mise en Veille", "veille");
    item->Append(grid);
    item->item_selected.connect([=](void *data)
    {
        cout << "click on item veille!" << endl;
        menu_item_clicked.emit("screensaver");
    });

    item = new GengridItemConfig(evas, grid, "Elements de la maison", "info");
    item->Append(grid);
    item->item_selected.connect([=](void *data)
    {
        cout << "click on item house items!" << endl;
    });

    item = new GengridItemConfig(evas, grid, "Les Scénarios", "unknown");
    item->Append(grid);
    item->item_selected.connect([=](void *data)
    {
        cout << "click on item scenarios!" << endl;
    });

    item = new GengridItemConfig(evas, grid, "Calaos network", "network");
    item->Append(grid);
    item->item_selected.connect([=](void *data)
    {
        cout << "click on item network!" << endl;
    });

    elm_naviframe_item_push(naviframe, NULL, NULL, NULL, grid, "calaos");

}

ActivityConfigMenuView::~ActivityConfigMenuView()
{
    evas_object_del(grid);
}

void ActivityConfigMenuView::addIcon(int position, string type)
{

        //if (Utils::get_config_option("enable_media_" + type) != "false")
        {
                EdjeObject *obj = new EdjeObject(theme, evas);
                obj->LoadEdje("calaos/icons/menu/media/" + type);
                obj->addCallback("menu", "click," + type, sigc::mem_fun(*this, &ActivityConfigMenuView::ItemCallback));
                Swallow(obj, string("icon.") + to_string(position));
                obj->EmitSignal("show", "calaos");
                obj->Show();
                items.push_back(obj);
                position++;
        }

}

void ActivityConfigMenuView::ItemCallback(void *data, Evas_Object *_edje, string emission, string source)
{
        if (source != "menu") return;

        if (emission == "click,music")
        {
                menu_item_clicked.emit("music");
        }
        else if (emission == "click,camera")
        {
                menu_item_clicked.emit("camera");
        }
        else if (emission == "click,photos")
        {
                menu_item_clicked.emit("photos");
        }
        else if (emission == "click,web")
        {
                menu_item_clicked.emit("web");
        }
        else if (emission == "click,eskiss")
        {
                menu_item_clicked.emit("eskiss");
        }

}

void ActivityConfigMenuView::resetView()
{
}

