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

#include "GenlistItemAlbum.h"
#include <ApplicationMain.h>

ITEM_BUTTON_CALLBACK(GenlistItemAlbum, Play)
ITEM_BUTTON_CALLBACK(GenlistItemAlbum, Add)

GenlistItemAlbum::GenlistItemAlbum(Evas *_evas, Evas_Object *_parent, AudioPlayer *_player, int _item_id, int request_type, int _command_id, void *data):
        GenlistItemBase(_evas, _parent, "browser/album", ELM_GENLIST_ITEM_NONE, data),
        player(_player),
        item_id(_item_id),
        in_query(false),
        cover_downloaded(false),
        reqtype(request_type),
        command_id(_command_id)
{
}

GenlistItemAlbum::~GenlistItemAlbum()
{
}

string GenlistItemAlbum::getLabelItem(Evas_Object *obj, string part)
{
        if (!in_query)
        {
                switch (reqtype)
                {
                case ALBUM_LIST: player->getDBAlbumItem(item_id, sigc::mem_fun(*this, &GenlistItemAlbum::albumItemGet_cb)); break;
                case ALBUM_ARTIST: player->getDBAlbumArtistItem(item_id, command_id, sigc::mem_fun(*this, &GenlistItemAlbum::albumItemGet_cb)); break;
                case ALBUM_YEAR: player->getDBAlbumYearItem(item_id, command_id, sigc::mem_fun(*this, &GenlistItemAlbum::albumItemGet_cb)); break;
                }

                in_query = true;
        }

        if (part == "album.text")
        {
                if (label_album == "")
                        return "Chargement ...";
                else
                        return label_album;
        }
        else if (part == "artist.text")
                return label_artist;
        else if (part == "year.text")
                return label_year;

        return "";
}

Evas_Object *GenlistItemAlbum::getPartItem(Evas_Object *obj, string part)
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
        else if (part == "calaos.cover" &&
                 cover_downloaded &&
                 ecore_file_exists(cover_fname.c_str()))
        {
                o = elm_icon_add(parent);
                elm_image_file_set(o, cover_fname.c_str(), NULL);
                elm_image_preload_disabled_set(o, false);

                elm_object_item_signal_emit(item, "show,cover", "calaos");
        }

        return o;
}

void GenlistItemAlbum::buttonClickPlay()
{
        player->playItem(AudioPlayer::DB_ITEM_ALBUM, item_infos["id"]);
}

void GenlistItemAlbum::buttonClickAdd()
{
        player->addItem(AudioPlayer::DB_ITEM_ALBUM, item_infos["id"]);
}

void GenlistItemAlbum::albumItemGet_cb(Params &infos)
{
        cout << "Got infos..." << infos.toString() << endl;
        item_infos = infos;

        label_album = infos["name"];
        label_artist = infos["artist"];
        label_year = infos["year"];

        player->getDBAlbumCoverItem(infos, sigc::mem_fun(*this, &GenlistItemAlbum::albumItemCoverGet_cb));

        elm_genlist_item_update(item);
}

void GenlistItemAlbum::albumItemCoverGet_cb(Params &res)
{
        if (res["filename"] == "")
                return;

        if (!ecore_file_exists(res["filename"].c_str()))
                return;

        cover_fname = res["filename"];
        cover_downloaded = true;
        elm_genlist_item_update(item);
}
