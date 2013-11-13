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

#include "GenlistItemTrack.h"
#include <ApplicationMain.h>
#include <GenlistItemSimpleHeader.h>
#include <GenlistItemSimple.h>

ITEM_BUTTON_CALLBACK(GenlistItemTrack, Play)
ITEM_BUTTON_CALLBACK(GenlistItemTrack, Add)
ITEM_BUTTON_CALLBACK(GenlistItemTrack, More)

GenlistItemTrack::GenlistItemTrack(Evas *_evas, Evas_Object *_parent, AudioPlayer *_player, int _item_id, int request_type, int _command_id = 0, void *data):
        GenlistItemBase(_evas, _parent, "browser/track", ELM_GENLIST_ITEM_NONE, data),
        player(_player),
        item_id(_item_id),
        in_query(false),
        reqtype(request_type),
        command_id(_command_id)
{
}

GenlistItemTrack::~GenlistItemTrack()
{
}

string GenlistItemTrack::getLabelItem(Evas_Object *obj, string part)
{
        string text;

        if (!in_query)
        {
                switch (reqtype)
                {
                case TRACK_ALBUM: player->getDBAlbumTrackItem(command_id, item_id, sigc::mem_fun(*this, &GenlistItemTrack::albumItemGet_cb)); break;
                case TRACK_PLAYLIST: player->getDBPlaylistTrackItem(command_id, item_id, sigc::mem_fun(*this, &GenlistItemTrack::albumItemGet_cb)); break;
                case TRACK_ID: player->getDBTrackInfos(Utils::to_string(item_id), sigc::mem_fun(*this, &GenlistItemTrack::albumItemGet_cb));
                }

                in_query = true;
        }

        if (part == "text")
        {
                if (item_infos.size() <= 0)
                        text = "Chargement...";
                else
                        text = item_infos["title"];
        }

        return text;
}

Evas_Object *GenlistItemTrack::getPartItem(Evas_Object *obj, string part)
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
        else if (part == "calaos.button.more")
        {
                o = elm_button_add(parent);
                elm_object_style_set(o, "calaos/button/position/info");
                evas_object_smart_callback_add(o, "clicked", _item_button_More, this);
        }

        return o;
}

void GenlistItemTrack::buttonClickPlay()
{
        switch (reqtype)
        {
        case TRACK_PLAYLIST:
                if (item_infos["remote"] == "1" &&
                    item_infos["url"] != "") //it's a radio, play url
                {
                        player->playItem(AudioPlayer::DB_ITEM_DIRECTURL, item_infos["url"]);
                        break;
                }
        case TRACK_ID:
        case TRACK_ALBUM: player->playItem(AudioPlayer::DB_ITEM_TRACK, item_infos["id"]); break;
        }
}

void GenlistItemTrack::buttonClickAdd()
{
        switch (reqtype)
        {
        case TRACK_PLAYLIST:
                if (item_infos["remote"] == "1" &&
                    item_infos["url"] != "") //it's a radio, play url
                {
                        player->addItem(AudioPlayer::DB_ITEM_DIRECTURL, item_infos["url"]);
                        break;
                }
        case TRACK_ID:
        case TRACK_ALBUM: player->addItem(AudioPlayer::DB_ITEM_TRACK, item_infos["id"]); break;
        }
}

void GenlistItemTrack::albumItemGet_cb(Params &infos)
{
        item_infos = infos;
        if (!item_infos.Exists("id") && item_infos.Exists("track_id"))
                item_infos.Add("id", item_infos["track_id"]);

        cout << "Got infos..." << item_infos.toString() << endl;

        elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
}

void GenlistItemTrack::buttonClickMore()
{
        Evas_Object *table = createPaddingTable(evas, parent, 260, 200);

        Evas_Object *glist = elm_genlist_add(parent);
        elm_object_style_set(glist, "calaos");
        elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
        evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
        evas_object_show(glist);

        string title_label = "Info de la <light_blue>piste #" + Utils::to_string(item_id + 1) + "</light_blue><br><small>Détails de la piste.</small>";
        GenlistItemBase *header = new GenlistItemSimpleHeader(evas, glist, title_label);
        header->Append(glist);

        string infolabel;
        if (item_infos["artist"] != "")
        {
                infolabel = item_infos["artist"];
                GenlistItemSimpleKeyValue *it = new GenlistItemSimpleKeyValue(evas, glist, "Artiste :", infolabel);
                it->Append(glist, header);
        }
        if (item_infos["album"] != "")
        {
                infolabel = item_infos["album"];
                GenlistItemSimpleKeyValue *it = new GenlistItemSimpleKeyValue(evas, glist, "Album :", infolabel);
                it->Append(glist, header);
        }
        if (item_infos["title"] != "")
        {
                infolabel = item_infos["title"];
                GenlistItemSimpleKeyValue *it = new GenlistItemSimpleKeyValue(evas, glist, "Titre :", infolabel);
                it->Append(glist, header);
        }
        if (item_infos["duration"] != "")
        {
                double dur;
                from_string(item_infos["duration"], dur);
                infolabel = Utils::time2string_digit((long)dur);
                GenlistItemSimpleKeyValue *it = new GenlistItemSimpleKeyValue(evas, glist, "Durée :", infolabel);
                it->Append(glist, header);
        }
        if (item_infos["genre"] != "")
        {
                infolabel = item_infos["genre"];
                GenlistItemSimpleKeyValue *it = new GenlistItemSimpleKeyValue(evas, glist, "Genre :", infolabel);
                it->Append(glist, header);
        }
        if (item_infos["type"] != "")
        {
                infolabel = item_infos["type"];
                GenlistItemSimpleKeyValue *it = new GenlistItemSimpleKeyValue(evas, glist, "Type de flux :", infolabel);
                it->Append(glist, header);
        }
        if (item_infos["bitrate"] != "")
        {
                infolabel = item_infos["bitrate"];
                GenlistItemSimpleKeyValue *it = new GenlistItemSimpleKeyValue(evas, glist, "Débit :", infolabel);
                it->Append(glist, header);
        }
        if (item_infos["filesize"] != "")
        {
                long int s;
                from_string(item_infos["filesize"], s);
                s /= 1024;
                s /= 1024;
                infolabel = Utils::to_string(s) + " Mo";
                GenlistItemSimpleKeyValue *it = new GenlistItemSimpleKeyValue(evas, glist, "Taille :", infolabel);
                it->Append(glist, header);
        }
        if (item_infos["samplerate"] != "")
        {
                infolabel = item_infos["samplerate"] + " Hz";
                GenlistItemSimpleKeyValue *it = new GenlistItemSimpleKeyValue(evas, glist, "Echantillonage :", infolabel);
                it->Append(glist, header);
        }
        if (item_infos["tagversion"] != "")
        {
                infolabel = item_infos["tagversion"];
                GenlistItemSimpleKeyValue *it = new GenlistItemSimpleKeyValue(evas, glist, "Version du tag :", infolabel);
                it->Append(glist, header);
        }
        if (item_infos["comment"] != "")
        {
                infolabel = item_infos["comment"];
                GenlistItemSimpleKeyValue *it = new GenlistItemSimpleKeyValue(evas, glist, "Commentaire :", infolabel);
                it->Append(glist, header);
        }

        elm_table_pack(table, glist, 1, 1, 1, 1);

        Evas_Object *popup_position = elm_ctxpopup_add(parent);
        elm_object_style_set(popup_position, "calaos");
        evas_object_size_hint_min_set(popup_position, 300, 240);

        elm_object_content_set(popup_position, table);

        Evas_Coord x,y;
        evas_pointer_canvas_xy_get(evas, &x, &y);
        evas_object_move(popup_position, x, y);
        evas_object_show(popup_position);
}
