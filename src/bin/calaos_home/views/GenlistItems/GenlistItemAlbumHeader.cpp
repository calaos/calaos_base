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

#include "GenlistItemAlbumHeader.h"
#include <ApplicationMain.h>

ITEM_BUTTON_CALLBACK(GenlistItemAlbumHeader, Play)
ITEM_BUTTON_CALLBACK(GenlistItemAlbumHeader, Add)

GenlistItemAlbumHeader::GenlistItemAlbumHeader(Evas *_evas, Evas_Object *_parent, AudioPlayer *_player, Params &_album_infos, int _album_id, void *data):
    GenlistItemBase(_evas, _parent, "browser/album_header", ELM_GENLIST_ITEM_GROUP, data),
    player(_player),
    album_infos(_album_infos),
    album_id(_album_id),
    in_query(false),
    cover_downloaded(false),
    dltimer(NULL)
{
}

GenlistItemAlbumHeader::~GenlistItemAlbumHeader()
{
    DELETE_NULL(dltimer);
}

string GenlistItemAlbumHeader::getLabelItem(Evas_Object *obj, string part)
{
    string text;

    if (part == "text")
    {
        string album = _("Unknown album");
        if (album_infos.Exists("name")) album = album_infos["name"];
        text = album;
    }
    else if (part == "text.artist")
    {
        if (album_infos.Exists("artist"))
            text = album_infos["artist"];
        else
            text = _("Na");
    }
    else if (part == "text.album")
    {
        if (album_infos.Exists("name"))
            text = album_infos["name"];
        else
            text = _("Na");
    }
    else if (part == "text.year")
    {
        if (album_infos.Exists("year"))
            text = album_infos["year"];
        else
            text = _("Na");
    }
    else if (part == "text.count")
    {
        if (album_infos.Exists("count"))
            text = album_infos["count"];
        else
            text = _("Na");
    }

    return text;
}

Evas_Object *GenlistItemAlbumHeader::getPartItem(Evas_Object *obj, string part)
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
        elm_object_style_set(o, "calaos/action_button/label");
        elm_object_text_set(o, _("Add"));
        evas_object_smart_callback_add(o, "clicked", _item_button_Add, this);
    }
    else if (part == "calaos.cover")
    {
        if (cover_downloaded &&
            ecore_file_exists(cover_fname.c_str()))
        {
            o = elm_icon_add(parent);
            elm_image_file_set(o, cover_fname.c_str(), NULL);
            elm_image_preload_disabled_set(o, false);

            elm_object_item_signal_emit(item, "show,cover", "calaos");
        }
        else
        {
            if (!dltimer)
            {
                dltimer = new EcoreTimer(0.2, (sigc::slot<void>)
                                         sigc::bind(sigc::mem_fun(*player, &AudioPlayer::getDBAlbumCoverItem),
                                                    album_infos, sigc::mem_fun(*this, &GenlistItemAlbumHeader::albumItemCoverGet_cb), AudioPlayer::AUDIO_COVER_SIZE_MEDIUM));
            }
        }
    }

    return o;
}

void GenlistItemAlbumHeader::buttonClickPlay()
{
    player->playItem(AudioPlayer::DB_ITEM_ALBUM, album_infos["id"]);
}

void GenlistItemAlbumHeader::buttonClickAdd()
{
    player->addItem(AudioPlayer::DB_ITEM_ALBUM, album_infos["id"]);
}

void GenlistItemAlbumHeader::albumItemCoverGet_cb(Params &res)
{
    DELETE_NULL(dltimer);
    if (res["filename"] == "")
        return;

    if (!ecore_file_exists(res["filename"].c_str()))
        return;

    cover_fname = res["filename"];
    cover_downloaded = true;

    elm_genlist_item_fields_update(item, "calaos.cover", ELM_GENLIST_ITEM_FIELD_CONTENT);
}
