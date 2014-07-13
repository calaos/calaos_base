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
#include "ActivityAudioPlayerObject.h"
#include "GenlistItems/GenlistItemPlaylist.h"
#include <FileDownloader.h>
#include <ApplicationMain.h>
#include <GenlistItemSimpleHeader.h>
#include <GenlistItemSimple.h>
#include "ActivityIntl.h"

ActivityPlayerObject::ActivityPlayerObject(Evas *_e, Evas_Object *_p):
    evas(_e),
    parent(_p),
    player(NULL),
    gplaylist(NULL),
    cover(NULL),
    cover_small(NULL),
    items_parent(NULL),
    small_player(NULL),
    amp_panel(NULL),
    amp_volume(false),
    object(NULL)
{
}

ActivityPlayerObject::~ActivityPlayerObject()
{
    con_volume.disconnect();
    con_status.disconnect();
    con_track.disconnect();
    con_time.disconnect();

    con_pl_changed.disconnect();
    con_pl_tracksadded.disconnect();
    con_pl_trackdel.disconnect();
    con_pl_trackmoved.disconnect();

    DELETE_NULL_FUNC(evas_object_del, cover);
    DELETE_NULL_FUNC(evas_object_del, cover_small);
}

void ActivityPlayerObject::resetPlayer()
{
    if (!object) return;

    object->setPartText("player.name", _("No zone"));
    object->setPartText("player.volume", "0%");
    object->setPartText("player.type", "Na");
    object->setPartText("player.artist", _("None"));
    object->setPartText("player.album", _("None"));
    object->setPartText("player.title", _("None"));
    object->setPartText("player.year", "Na");
    object->setPartText("player.duration", "0:00");

    object->EmitSignal("player,stop", "calaos");

    if (small_player)
    {
        small_player->setPartText("player.name", _("No zone"));
        small_player->setPartText("player.volume", "0%");
        small_player->setPartText("player.type", "Na");
        small_player->setPartText("player.artist", _("None"));
        small_player->setPartText("player.album", _("None"));
        small_player->setPartText("player.title", _("None"));
        small_player->setPartText("player.year", "Na");
        small_player->setPartText("player.duration", "0:00");

        small_player->EmitSignal("player,stop", "calaos");
    }
}

void ActivityPlayerObject::createEdjeObject(string &theme, Evas *e)
{
    EdjeObject *obj = new EdjeObject(theme, e);
    obj->LoadEdje("calaos/audio/player");
    obj->setPartText("message.text", _("Position :"));
    obj->setPartText("text.artist", _("Artist :"));
    obj->setPartText("text.album", _("Album :"));
    obj->setPartText("text.title", _("Track :"));
    obj->setPartText("text.year", _("Year :"));
    obj->setPartText("text.duration", _("Duration :"));
    obj->Show();
    obj->addCallback("player", "player,volume,*", sigc::mem_fun(*this, &ActivityPlayerObject::changeVolume_cb));
    obj->addCallback("player", "player,play", sigc::mem_fun(*this, &ActivityPlayerObject::play_cb));
    obj->addCallback("player", "player,pause", sigc::mem_fun(*this, &ActivityPlayerObject::pause_cb));
    obj->addCallback("player", "player,stop", sigc::mem_fun(*this, &ActivityPlayerObject::stop_cb));
    obj->addCallback("player", "player,previous", sigc::mem_fun(*this, &ActivityPlayerObject::previous_cb));
    obj->addCallback("player", "player,next", sigc::mem_fun(*this, &ActivityPlayerObject::next_cb));
    obj->addCallback("player", "player,off", sigc::mem_fun(*this, &ActivityPlayerObject::off_cb));
    obj->addCallback("player", "player,on", sigc::mem_fun(*this, &ActivityPlayerObject::on_cb));
    obj->addCallback("player", "player,time,change", sigc::mem_fun(*this, &ActivityPlayerObject::changeTime_cb));
    obj->addCallback("player", "player,select", sigc::mem_fun(*this, &ActivityPlayerObject::selectDetail_cb));
    obj->addCallback("player", "player,amplifier", sigc::mem_fun(*this, &ActivityPlayerObject::amplifierClick_cb));

    object = obj;

    resetPlayer();
}

void ActivityPlayerObject::setPlayer(AudioPlayer *p)
{
    con_volume.disconnect();
    con_status.disconnect();
    con_track.disconnect();
    con_time.disconnect();
    con_pl_changed.disconnect();
    con_pl_tracksadded.disconnect();
    con_pl_trackdel.disconnect();
    con_pl_trackmoved.disconnect();
    player = p;

    if (!player) return;

    //There is an Amplifier linked with this Player
    if (player->params.Exists("amp_id"))
        object->EmitSignal("player,amplifier", "calaos");
    else
        object->EmitSignal("player,no_amplifier", "calaos");

    player->registerChange();
    cover_fname = "/tmp/calaos_cover_" + player->params["num"] + ".jpg";

    onVolumeChange();
    onStatusChange();
    onTrackChange();

    con_volume = player->player_volume_changed.connect(sigc::mem_fun(*this, &ActivityPlayerObject::onVolumeChange));
    con_status = player->player_status_changed.connect(sigc::mem_fun(*this, &ActivityPlayerObject::onStatusChange));
    con_track = player->player_track_changed.connect(sigc::mem_fun(*this, &ActivityPlayerObject::onTrackChange));
    con_time = player->player_time_changed.connect(sigc::mem_fun(*this, &ActivityPlayerObject::onTimeChange));

    con_pl_changed = player->player_playlist_changed.connect(sigc::mem_fun(*this, &ActivityPlayerObject::onPlaylistChanged));
    con_pl_tracksadded = player->player_playlist_tracks_added.connect(sigc::mem_fun(*this, &ActivityPlayerObject::onPlaylistTracksAdded));
    con_pl_trackdel = player->player_playlist_tracks_deleted.connect(sigc::mem_fun(*this, &ActivityPlayerObject::onPlaylistTrackDeleted));
    con_pl_trackmoved = player->player_playlist_tracks_moved.connect(sigc::mem_fun(*this, &ActivityPlayerObject::onPlaylistTrackMoved));
}

void ActivityPlayerObject::unsetPlayer()
{
    if (!player) return;

    player->unregisterChange();
    player = NULL;

    if (gplaylist)
    {
        elm_genlist_clear(gplaylist);
        gplaylist = NULL;
    }
}

void ActivityPlayerObject::handlePlaylist(Evas_Object *p, Evas_Object *playlist, EdjeObject *splayer)
{
    gplaylist = playlist;
    items_parent = p;
    small_player = splayer;

    sigc::connection *con;
    con = small_player->addCallback("player", "player,volume,*", sigc::mem_fun(*this, &ActivityPlayerObject::changeVolumeSmall_cb));
    small_player_connections.push_back(con);
    con = small_player->addCallback("player", "player,play", sigc::mem_fun(*this, &ActivityPlayerObject::play_cb));
    small_player_connections.push_back(con);
    con = small_player->addCallback("player", "player,pause", sigc::mem_fun(*this, &ActivityPlayerObject::pause_cb));
    small_player_connections.push_back(con);
    con = small_player->addCallback("player", "player,stop", sigc::mem_fun(*this, &ActivityPlayerObject::stop_cb));
    small_player_connections.push_back(con);
    con = small_player->addCallback("player", "player,previous", sigc::mem_fun(*this, &ActivityPlayerObject::previous_cb));
    small_player_connections.push_back(con);
    con = small_player->addCallback("player", "player,next", sigc::mem_fun(*this, &ActivityPlayerObject::next_cb));
    small_player_connections.push_back(con);
    con = small_player->addCallback("player", "player,off", sigc::mem_fun(*this, &ActivityPlayerObject::off_cb));
    small_player_connections.push_back(con);
    con = small_player->addCallback("player", "player,on", sigc::mem_fun(*this, &ActivityPlayerObject::on_cb));
    small_player_connections.push_back(con);
    con = small_player->addCallback("player", "player,time,change", sigc::mem_fun(*this, &ActivityPlayerObject::changeTimeSmall_cb));
    small_player_connections.push_back(con);

    //Force update of the small player here.
    onTrackChange();
    onVolumeChange();
    onStatusChange();
    onTimeChange();

    onPlaylistChanged();
}

void ActivityPlayerObject::unsetPlaylist()
{
    for (uint i = 0;i < small_player_connections.size();i++)
        small_player->delCallback(small_player_connections[i]);
    small_player_connections.clear();

    gplaylist = NULL;
    small_player = NULL;
    DELETE_NULL_FUNC(evas_object_del, cover_small);
}

void ActivityPlayerObject::onVolumeChange()
{
    if (!player) return;

    object->setPartText("player.volume", Utils::to_string(player->getVolume()) + "%");
    object->setDragValue("player.volume.slider", 0.0, 1.0 - ((double)player->getVolume() / 100.));

    if (small_player)
    {
        small_player->setPartText("player.volume", Utils::to_string(player->getVolume()) + "%");
        small_player->setDragValue("player.volume.slider", 0.0, 1.0 - ((double)player->getVolume() / 100.));
    }
}

void ActivityPlayerObject::onStatusChange()
{
    if (!player) return;

    if (player->getStatus() == "play")
        object->EmitSignal("player,playing", "calaos");
    else
        object->EmitSignal("player,stop", "calaos");

    if (small_player)
    {
        if (player->getStatus() == "play")
            small_player->EmitSignal("player,playing", "calaos");
        else
            small_player->EmitSignal("player,stop", "calaos");
    }
}

void ActivityPlayerObject::onTrackChange()
{
    if (!player) return;

    string artist, album, title, year, type, duration;
    artist = player->current_song_info["artist"];
    album = player->current_song_info["album"];
    title = player->current_song_info["title"];
    year = player->current_song_info["year"];
    type = player->current_song_info["type"];
    duration = player->current_song_info["duration"];

    if (artist == "") artist = _("None");
    if (album == "") album = _("None");
    if (title == "") title = _("None");
    if (year == "") year = "Na";
    if (type == "") type = "Na";
    if (duration == "") duration = "0:00";

    object->setPartText("player.type", type);
    object->setPartText("player.artist", artist);
    object->setPartText("player.album", album);
    object->setPartText("player.title", title);
    object->setPartText("player.year", year);
    object->setPartText("player.duration", duration);

    if (small_player)
    {
        small_player->setPartText("player.type", type);
        small_player->setPartText("player.artist", artist);
        small_player->setPartText("player.album", album);
        small_player->setPartText("player.title", title);
        small_player->setPartText("player.year", year);
        small_player->setPartText("player.duration", duration);
    }

    if (player->hasCover())
    {
        player->getCurrentCover(sigc::mem_fun(*this, &ActivityPlayerObject::coverGet_cb));
    }
    else
    {
        object->EmitSignal("track,nocover", "calaos");
        if (small_player) small_player->EmitSignal("track,nocover", "calaos");
    }
}

void ActivityPlayerObject::coverGet_cb(Params &infos)
{
    if (infos["cover"] == "")
    {
        object->EmitSignal("track,nocover", "calaos");
        if (small_player) small_player->EmitSignal("track,nocover", "calaos");
        return;
    }

    FileDownloader *downloader = new FileDownloader(infos["cover"], cover_fname, true);
    downloader->addCallback(sigc::mem_fun(*this, &ActivityPlayerObject::coverDownload_cb));
    downloader->Start();
}

void ActivityPlayerObject::coverDownload_cb(string status, void *data)
{
    if (status == "failed" || status == "aborted")
    {
        object->EmitSignal("track,nocover", "calaos");
        if (small_player) small_player->EmitSignal("track,nocover", "calaos");
        return;
    }

    if (status != "done")
        return;

    if (!ecore_file_exists(cover_fname.c_str()))
        return;

    if (!cover)
    {
        cover = elm_icon_add(parent);
        object->Swallow(cover, "player.cover");
        evas_object_show(cover);
    }

    if (!cover_small && small_player)
    {
        cover_small = elm_icon_add(parent);
        small_player->Swallow(cover_small, "player.cover");
        evas_object_show(cover_small);
    }

    elm_image_file_set(cover, cover_fname.c_str(), NULL);
    elm_image_preload_disabled_set(cover, false);
    if (small_player)
    {
        elm_image_file_set(cover_small, cover_fname.c_str(), NULL);
        elm_image_preload_disabled_set(cover_small, false);
    }

    object->EmitSignal("track,cover", "calaos");
    if (small_player) small_player->EmitSignal("track,cover", "calaos");
}

void ActivityPlayerObject::onTimeChange()
{
    if (!player) return;

    object->setPartText("player.time", Utils::time2string_digit((long)player->getTimeElapsed()));
    object->setDragValue("player.time.slider", player->getTimeElapsed() / player->getDuration(), 0.0);

    if (small_player)
    {
        small_player->setPartText("player.time", Utils::time2string_digit((long)player->getTimeElapsed()));
        small_player->setDragValue("player.time.slider", player->getTimeElapsed() / player->getDuration(), 0.0);
    }
}

void ActivityPlayerObject::changeVolume_cb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player) return;

    if (emission == "player,volume,change,move")
    {
        double val;
        object->getDragValue("player.volume.slider", NULL, &val);
        val = 1.0 - val;

        player->setVolume((int)(val * 100.0));
    }
    else if (emission == "player,volume,plus")
    {
        player->setVolume(player->getVolume() + 1);
    }
    else if (emission == "player,volume,moins")
    {
        player->setVolume(player->getVolume() - 1);
    }
}

void ActivityPlayerObject::changeVolumeSmall_cb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player) return;

    if (emission == "player,volume,change,move")
    {
        double val = 0.0;
        if (small_player) small_player->getDragValue("player.volume.slider", NULL, &val);
        val = 1.0 - val;

        player->setVolume((int)(val * 100.0));
    }
    else if (emission == "player,volume,plus")
    {
        player->setVolume(player->getVolume() + 1);
    }
    else if (emission == "player,volume,moins")
    {
        player->setVolume(player->getVolume() - 1);
    }
}

void ActivityPlayerObject::changeTime_cb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player) return;

    double val;
    object->getDragValue("player.time.slider", &val, NULL);

    player->setTime(val * player->getDuration());
}

void ActivityPlayerObject::changeTimeSmall_cb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player) return;

    double val = 0.0;
    if (small_player) small_player->getDragValue("player.time.slider", &val, NULL);

    player->setTime(val * player->getDuration());
}

void ActivityPlayerObject::play_cb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player) return;

    player->play();
}

void ActivityPlayerObject::pause_cb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player) return;

    player->pause();
}

void ActivityPlayerObject::stop_cb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player) return;

    player->stop();
}

void ActivityPlayerObject::next_cb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player) return;

    player->next();
}

void ActivityPlayerObject::previous_cb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player) return;

    player->previous();
}

void ActivityPlayerObject::on_cb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player) return;

    player->on();
}

void ActivityPlayerObject::off_cb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player) return;

    player->off();
}

void ActivityPlayerObject::selectDetail_cb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!player) return;

    player_selected.emit(this);
}

void ActivityPlayerObject::onPlaylistChanged()
{
    if (!gplaylist) return;

    elm_genlist_clear(gplaylist);

    for (int i = 0;i < player->getPlaylistSize();i++)
    {
        GenlistItemPlaylist *item = new GenlistItemPlaylist(evas, items_parent, player, i);
        item->Append(gplaylist);
    }
}

void ActivityPlayerObject::onPlaylistTracksAdded(int count)
{
    if (!gplaylist) return;

    int playlist_size = player->getPlaylistSize();
    for (int i = 0;i < count;i++)
    {
        GenlistItemPlaylist *item = new GenlistItemPlaylist(evas, items_parent, player, playlist_size - count + i);
        item->Append(gplaylist);
    }
}

void ActivityPlayerObject::onPlaylistTrackDeleted(int del_item)
{
    if (!gplaylist) return;

    int playlist_size = player->getPlaylistSize();
    if (del_item < playlist_size / 2)
    {
        Elm_Object_Item *item = elm_genlist_first_item_get(gplaylist);
        int cpt = 0;
        while (cpt < del_item)
        {
            item = elm_genlist_item_next_get(item);
            cpt++;
        }
        elm_object_item_del(item);
    }
    else
    {
        Elm_Object_Item *item = elm_genlist_last_item_get(gplaylist);
        int cpt = playlist_size - 1;
        while (cpt > del_item)
        {
            item = elm_genlist_item_next_get(item);
            cpt--;
        }
        elm_object_item_del(item);
    }
}

void ActivityPlayerObject::onPlaylistTrackMoved(int from, int to)
{
    if (!gplaylist) return;

    cWarning() << "NOT IMPLEMENTED! Track moved ==== " << from << "  -->  " << to;
}

static void _elm_volume_changed(void *data, Evas_Object *obj, void *event_info)
{
    ActivityPlayerObject *o = reinterpret_cast<ActivityPlayerObject *>(data);
    if (!o) return;
    double val = elm_slider_value_get(obj);

    o->amplifierVolumeChanged(val);
}

static void _elm_volume_start(void *data, Evas_Object *obj, void *event_info)
{
    ActivityPlayerObject *o = reinterpret_cast<ActivityPlayerObject *>(data);
    if (!o) return;

    o->amplifierVolumeStartChanged();
}

static void _elm_volume_stop(void *data, Evas_Object *obj, void *event_info)
{
    ActivityPlayerObject *o = reinterpret_cast<ActivityPlayerObject *>(data);
    if (!o) return;

    o->amplifierVolumeStopChanged();
}

static void _elm_avpopup_dissmissed(void *data, Evas_Object *obj, void *event_info)
{
    ActivityPlayerObject *o = reinterpret_cast<ActivityPlayerObject *>(data);
    if (!o) return;

    o->popupAVDismissed();
}

static void _amp_button_poweron(void *data, Evas_Object *obj, const char *emission, const char *source)
{
    ActivityPlayerObject *o = reinterpret_cast<ActivityPlayerObject *>(data);
    if (!o) return;

    o->amplifierPowerOn();
}

static void _amp_button_poweroff(void *data, Evas_Object *obj, const char *emission, const char *source)
{
    ActivityPlayerObject *o = reinterpret_cast<ActivityPlayerObject *>(data);
    if (!o) return;

    o->amplifierPowerOff();
}

void ActivityPlayerObject::amplifierClick_cb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    //Evas_Object *table = createPaddingTable(evas, parent, 420, 420);
    Evas_Object *table = elm_table_add(parent);
    evas_object_size_hint_min_set(table, 420, 420);
    evas_object_show(table);

    Evas_Object *pad = evas_object_rectangle_add(evas);
    evas_object_size_hint_min_set(pad, 400, 10);
    elm_table_pack(table, pad, 1, 0, 1, 1);

    pad = evas_object_rectangle_add(evas);
    evas_object_size_hint_min_set(pad, 400, 10);
    elm_table_pack(table, pad, 1, 2, 1, 1);

    pad = evas_object_rectangle_add(evas);
    evas_object_size_hint_min_set(pad, 10, 400);
    elm_table_pack(table, pad, 0, 1, 1, 1);

    pad = evas_object_rectangle_add(evas);
    evas_object_size_hint_min_set(pad, 10, 400);
    elm_table_pack(table, pad, 2, 1, 1, 1);

    amp_panel = elm_layout_add(table);
    elm_layout_file_set(amp_panel, ApplicationMain::getTheme(), "calaos/audio/amplifier");
    evas_object_size_hint_fill_set(amp_panel, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_size_hint_weight_set(amp_panel, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_show(amp_panel);

    Evas_Object *glist = elm_genlist_add(parent);
    elm_object_style_set(glist, "calaos");
    elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
    evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_show(glist);

    map<int, string> inputs;
    if (player->getAmplifier()) inputs = player->getAmplifier()->amplifier_inputs;
    map<int, string>::iterator it = inputs.begin();

    GenlistItemBase *header = new GenlistItemSimpleHeader(evas, glist, "Source d'entrée");
    header->Append(glist);
    for (;it != inputs.end();it++)
    {
        int source_id = (*it).first;
        int *user_data = new int(source_id);
        GenlistItemSimple *item = new GenlistItemSimple(evas, glist, (*it).second, true, false, user_data, "check");
        item->Append(glist, header);
        item->setAutoDeleteUserData(new DeletorT<int *>);
        if (Utils::to_string(source_id) == player->getAmplifierStatus("input_source"))
            item->setSelected(true);

        item->item_selected.connect(sigc::mem_fun(*this, &ActivityPlayerObject::inputSourceSelected));
    }

    elm_object_part_text_set(amp_panel, "title", player->getAmplifier()->params["name"].c_str());
    elm_object_part_text_set(amp_panel, "display.text", player->getAmplifier()->params["display_text"].c_str());
    elm_object_part_content_set(amp_panel, "inputs.swallow", glist);

    Evas_Object *volume = NULL;
    if (edje_object_part_exists(elm_layout_edje_get(amp_panel), "volume.slider"))
    {
        volume = edje_object_part_external_object_get(elm_layout_edje_get(amp_panel), "volume.slider");

        //double v;
        //from_string(player->getAmplifierStatus("volume"), v);

        //elm_slider_value_set(volume, v / 100.0);
        amp_volume = false;
        EcoreTimer::singleShot(0.1, sigc::mem_fun(*this, &ActivityPlayerObject::amplifierChanged));

        evas_object_smart_callback_add(volume, "slider,drag,start", _elm_volume_start, this);
        evas_object_smart_callback_add(volume, "slider,drag,stop", _elm_volume_stop, this);
        evas_object_smart_callback_add(volume, "changed", _elm_volume_changed, this);
    }

    IOBase *amp = player->getAmplifier();
    amp->io_changed.connect(sigc::mem_fun(*this, &ActivityPlayerObject::amplifierChanged));

    edje_object_signal_callback_add(elm_layout_edje_get(amp_panel), "pressed", "button.poweron", _amp_button_poweron, this);
    edje_object_signal_callback_add(elm_layout_edje_get(amp_panel), "pressed", "button.poweroff", _amp_button_poweroff, this);

    elm_table_pack(table, amp_panel, 1, 1, 1, 1);

    Evas_Object *popup = elm_ctxpopup_add(parent);
    elm_object_content_set(popup, table);
    elm_object_style_set(popup, "calaos");

    evas_object_smart_callback_add(popup, "dismissed", _elm_avpopup_dissmissed, this);

    Evas_Coord x,y;
    evas_pointer_canvas_xy_get(evas, &x, &y);
    evas_object_move(popup, x, y);
    evas_object_show(popup);
}

void ActivityPlayerObject::amplifierVolumeChanged(double vol)
{
    if (!player) return;
    IOBase *amp = player->getAmplifier();
    if (!amp) return;

    int v = vol * 100.0;
    amp->sendAction("volume " + Utils::to_string(v));
}

void ActivityPlayerObject::popupAVDismissed()
{
    amp_panel = NULL;
}

void ActivityPlayerObject::amplifierChanged()
{
    if (!player) return;
    if (!amp_panel) return;
    if (amp_volume) return;

    Evas_Object *volume = NULL;
    if (edje_object_part_exists(elm_layout_edje_get(amp_panel), "volume.slider"))
    {
        volume = edje_object_part_external_object_get(elm_layout_edje_get(amp_panel), "volume.slider");

        double v;
        from_string(player->getAmplifierStatus("volume"), v);
        elm_slider_value_set(volume, v / 100.0);
    }

    elm_object_part_text_set(amp_panel, "display.text", player->getAmplifier()->params["display_text"].c_str());
}

void ActivityPlayerObject::inputSourceSelected(void *data)
{
    if (!player) return;
    IOBase *amp = player->getAmplifier();
    if (!amp) return;

    int *user_data = reinterpret_cast<int *>(data);

    int input = *user_data;
    amp->sendAction("source " + Utils::to_string(input));
}

void ActivityPlayerObject::amplifierVolumeStartChanged()
{
    amp_volume = true;
}

void ActivityPlayerObject::amplifierVolumeStopChanged()
{
    amp_volume = false;
}

void ActivityPlayerObject::amplifierPowerOn()
{
    if (!player) return;
    IOBase *amp = player->getAmplifier();
    if (!amp) return;

    amp->sendAction("power true");
}

void ActivityPlayerObject::amplifierPowerOff()
{
    if (!player) return;
    IOBase *amp = player->getAmplifier();
    if (!amp) return;

    amp->sendAction("power false");
}
