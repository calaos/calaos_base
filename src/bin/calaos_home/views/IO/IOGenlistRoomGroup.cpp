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
#include "IOGenlistRoomGroup.h"
#include <ApplicationMain.h>

IOGenlistRoomGroup::IOGenlistRoomGroup(Evas *_evas, Evas_Object *_parent, Room *_room, string style_addition):
    GenlistItemBase(_evas, _parent, string("group_room") + style_addition, ELM_GENLIST_ITEM_GROUP),
    room(_room)
{
}

IOGenlistRoomGroup::~IOGenlistRoomGroup()
{
}

void IOGenlistRoomGroup::itemAdded()
{
    elm_genlist_item_select_mode_set(item, ELM_OBJECT_SELECT_MODE_DISPLAY_ONLY);
}

Evas_Object *IOGenlistRoomGroup::getPartItem(Evas_Object *obj, string part)
{
    Evas_Object *o = NULL;

    return o;
}

string IOGenlistRoomGroup::getLabelItem(Evas_Object *obj, string part)
{
    return room->name;
}

IOGenlistRoomGroupIcon::IOGenlistRoomGroupIcon(Evas *_evas, Evas_Object *_parent, Room *_room, string style_addition):
    GenlistItemBase(_evas, _parent, string("group_room/icon") + style_addition, ELM_GENLIST_ITEM_GROUP),
    room(_room)
{
}

IOGenlistRoomGroupIcon::~IOGenlistRoomGroupIcon()
{
}

void IOGenlistRoomGroupIcon::itemAdded()
{
    elm_genlist_item_select_mode_set(item, ELM_OBJECT_SELECT_MODE_DISPLAY_ONLY);
}

Evas_Object *IOGenlistRoomGroupIcon::getPartItem(Evas_Object *obj, string part)
{
    Evas_Object *o = NULL;

    if (part == "icon")
    {
        string type = room->type;
        string t = type;

        if (type == "salon") t = "lounge";
        if (type == "chambre") t = "bedroom";
        if (type == "cuisine") t = "kitchen";
        if (type == "bureau") t = "office";
        if (type == "sam") t = "diningroom";
        if (type == "cave") t = "cellar";
        if (type == "divers") t = "various";
        if (type == "misc") t = "various";
        if (type == "exterieur") t = "outside";
        if (type == "sdb") t = "bathroom";
        if (type == "hall") t = "corridor";
        if (type == "couloir") t = "corridor";

        EdjeObject *icon = new EdjeObject(ApplicationMain::getTheme(), evas);
        string group = "calaos/icons/room/";

        try
        {
            icon->LoadEdje(group + t);
        }
        catch(...)
        {
            //room not found, load default
            icon->LoadEdje("calaos/icons/room/various");
        }

        icon->setAutoDelete(true);
        o = icon->getEvasObject();
    }

    return o;
}

string IOGenlistRoomGroupIcon::getLabelItem(Evas_Object *obj, string part)
{
    return room->name;
}

