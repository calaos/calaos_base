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

#include "GenlistItemBrowserPlaylist.h"
#include <ApplicationMain.h>

ITEM_BUTTON_CALLBACK(GenlistItemBrowserPlaylist, Play)
ITEM_BUTTON_CALLBACK(GenlistItemBrowserPlaylist, Add)

GenlistItemBrowserPlaylist::GenlistItemBrowserPlaylist(Evas *_evas, Evas_Object *_parent, AudioPlayer *_player, int _item_id, void *data):
    GenlistItemBase(_evas, _parent, "browser/default", ELM_GENLIST_ITEM_NONE, data),
    player(_player),
    item_id(_item_id),
    in_query(false),
    no_query(false)
{
}

GenlistItemBrowserPlaylist::GenlistItemBrowserPlaylist(Evas *_evas, Evas_Object *_parent, AudioPlayer *_player, Params &infos, void *data):
    GenlistItemBase(_evas, _parent, "browser/default", ELM_GENLIST_ITEM_NONE, data),
    player(_player),
    item_infos(infos),
    in_query(false),
    no_query(true)
{
}

GenlistItemBrowserPlaylist::~GenlistItemBrowserPlaylist()
{
}

string GenlistItemBrowserPlaylist::getLabelItem(Evas_Object *obj, string part)
{
    string text;

    if (!in_query)
    {
        if (!no_query) player->getDBPlaylistItem(item_id, sigc::mem_fun(*this, &GenlistItemBrowserPlaylist::playlistItemGet_cb));
        in_query = true;
    }

    if (part == "text")
    {
        if (item_infos.size() <= 0)
            text = "Chargement...";
        else
            text = item_infos["name"];
    }

    return text;
}

Evas_Object *GenlistItemBrowserPlaylist::getPartItem(Evas_Object *obj, string part)
{
    Evas_Object *o = NULL;

    if (part == "calaos.button.play")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/play");
        elm_object_style_set(o, "calaos/action_button/blue");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_Play, this);
    }
    else if (part == "calaos.button.add")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/more");
        elm_object_style_set(o, "calaos/action_button/blue");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_Add, this);
    }
    else if (part == "icon")
    {
        o = elm_icon_add(parent);
        elm_image_file_set(o, ApplicationMain::getTheme(), "calaos/icons/genlist/playlist");
    }

    return o;
}

void GenlistItemBrowserPlaylist::buttonClickPlay()
{
    player->playItem(AudioPlayer::DB_ITEM_PLAYLIST, item_infos["id"]);
}

void GenlistItemBrowserPlaylist::buttonClickAdd()
{
    player->addItem(AudioPlayer::DB_ITEM_PLAYLIST, item_infos["id"]);
}

void GenlistItemBrowserPlaylist::playlistItemGet_cb(Params &infos)
{
    cDebug() << "Got infos..." << infos.toString();
    item_infos = infos;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
}
