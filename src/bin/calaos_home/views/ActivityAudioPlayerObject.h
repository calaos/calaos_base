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
#ifndef ACTIVITYAUDIOPLAYEROBJECT_H
#define ACTIVITYAUDIOPLAYEROBJECT_H

#include <Utils.h>

#include "ActivityView.h"
#include "CalaosModel.h"

using namespace Utils;

class ActivityPlayerObject
{
private:
    Evas *evas;
    Evas_Object *parent;

    AudioPlayer *player; //the Audio player Object
    Evas_Object *gplaylist;
    Evas_Object *cover, *cover_small;
    Evas_Object *items_parent;
    EdjeObject *small_player;

    //AV Amplifier
    Evas_Object *amp_panel;
    bool amp_volume;

    string cover_fname;

    sigc::connection con_volume;
    sigc::connection con_status;
    sigc::connection con_track;
    sigc::connection con_time;
    sigc::connection con_pl_changed;
    sigc::connection con_pl_tracksadded;
    sigc::connection con_pl_trackdel;
    sigc::connection con_pl_trackmoved;

    vector<sigc::connection *> small_player_connections;

    void onVolumeChange();
    void onStatusChange();
    void onTrackChange();
    void onTimeChange();

    void onPlaylistChanged();
    void onPlaylistTracksAdded(int count);
    void onPlaylistTrackDeleted(int del_item);
    void onPlaylistTrackMoved(int from, int to);

    void changeVolume_cb(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void changeTime_cb(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void changeVolumeSmall_cb(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void changeTimeSmall_cb(void *data, Evas_Object *_edje, std::string emission, std::string source);

    void play_cb(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void pause_cb(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void stop_cb(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void previous_cb(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void next_cb(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void on_cb(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void off_cb(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void amplifierClick_cb(void *data, Evas_Object *_edje, std::string emission, std::string source);

    void selectDetail_cb(void *data, Evas_Object *_edje, std::string emission, std::string source);

    void coverGet_cb(Params &infos);
    void coverDownload_cb(string status, void *data);

    void amplifierChanged();
    void inputSourceSelected(void *data);

public:
    EdjeObject *object; //The elementary video object

    ActivityPlayerObject(Evas *e, Evas_Object *parent);
    ~ActivityPlayerObject();

    void createEdjeObject(string &theme, Evas *evas);
    void resetPlayer();

    void setPlayer(AudioPlayer *p);
    void unsetPlayer();
    AudioPlayer *getPlayer() { return player; }

    void handlePlaylist(Evas_Object *parent, Evas_Object *playlist, EdjeObject *small_player);
    void unsetPlaylist();

    void amplifierVolumeStartChanged();
    void amplifierVolumeStopChanged();
    void amplifierVolumeChanged(double vol);
    void popupAVDismissed();
    void amplifierPowerOn();
    void amplifierPowerOff();

    sigc::signal<void, ActivityPlayerObject *> player_selected;
};

#endif // ACTIVITYAUDIOPLAYEROBJECT_H
