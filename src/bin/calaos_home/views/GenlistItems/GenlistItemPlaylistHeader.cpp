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

#include "GenlistItemPlaylistHeader.h"
#include <ApplicationMain.h>

ITEM_BUTTON_CALLBACK(GenlistItemPlaylistHeader, Play)
ITEM_BUTTON_CALLBACK(GenlistItemPlaylistHeader, Add)
ITEM_BUTTON_CALLBACK(GenlistItemPlaylistHeader, Del)

GenlistItemPlaylistHeader::GenlistItemPlaylistHeader(Evas *_evas, Evas_Object *_parent, AudioPlayer *_player, Params &playlist_infos, int playlist_id, void *data):
        GenlistItemBase(_evas, _parent, "browser/playlist_header", ELM_GENLIST_ITEM_GROUP, data),
        player(_player),
        pl_infos(playlist_infos),
        pl_id(playlist_id)
{
}

GenlistItemPlaylistHeader::~GenlistItemPlaylistHeader()
{
}

string GenlistItemPlaylistHeader::getLabelItem(Evas_Object *obj, string part)
{
        string text;

        if (part == "text")
                text = "Liste de lecture: <light_blue>" + pl_infos["name"] + "</light_blue>";
        else if (part == "text.count")
                text = pl_infos["count"];

        return text;
}

Evas_Object *GenlistItemPlaylistHeader::getPartItem(Evas_Object *obj, string part)
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
        else if (part == "calaos.button.del")
        {
                o = elm_button_add(parent);
                elm_object_style_set(o, "calaos/action_button/label");
                elm_object_text_set(o, "Supprimer");
                evas_object_smart_callback_add(o, "clicked", _item_button_Del, this);
        }

        return o;
}

void GenlistItemPlaylistHeader::buttonClickPlay()
{
        player->playItem(AudioPlayer::DB_ITEM_PLAYLIST, pl_infos["id"]);
}

void GenlistItemPlaylistHeader::buttonClickAdd()
{
        player->addItem(AudioPlayer::DB_ITEM_PLAYLIST, pl_infos["id"]);
}

void GenlistItemPlaylistHeader::buttonClickDel()
{
        player->playlistDelete(pl_infos["id"]);
}
