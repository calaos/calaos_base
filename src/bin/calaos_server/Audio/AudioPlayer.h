/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef S_AUDIOPLAYER_H
#define S_AUDIOPLAYER_H

#include <Calaos.h>
#include <AudioPlayerData.h>
#include <Output.h>
#include <Input.h>
#include <AudioDB.h>
#include <AudioInput.h>
#include <AudioOutput.h>

namespace Calaos
{

class AudioPlayer
{
protected:
    Params param;
    Output *aoutput;
    Input *ainput;

    AudioDB *database;

public:
    AudioPlayer(Params &p);
    virtual ~AudioPlayer();

    virtual void Play() { }
    virtual void Pause() { }
    virtual void Stop() { }
    virtual void Next() { }
    virtual void Previous() { }
    virtual void Power(bool on) { }
    virtual void Sleep(int seconds) { }
    virtual void Synchronize(string playerid, bool sync) { }
    virtual void getSynchronizeList(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { } //get all players wich we can sync with

    virtual void get_volume(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }
    virtual void set_volume(int vol) { }

    //Return a list of option supported by the player
    virtual Params getOptions() { Params p; return p; }

    virtual void get_title(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }
    virtual void get_artist(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }
    virtual void get_album(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }
    virtual void get_album_cover(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }
    virtual void get_genre(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }
    virtual void get_songinfo(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }
    virtual void get_current_time(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }
    virtual void set_current_time(double seconds) { }
    virtual void get_duration(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }

    //retourne le nombre de secondes restante avant le poweroff
    virtual void get_sleep(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }
    virtual void get_status(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }
    virtual void get_sync_status(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { } //get all players synced with

    //playlist functions
    virtual void playlist_moveup(int item) { }
    virtual void playlist_movedown(int item) { }
    virtual void playlist_delete(int item) { }
    virtual void playlist_play(int item) { }
    virtual void playlist_play_artist(string item) { }
    virtual void playlist_play_album(string item) { }
    virtual void playlist_play_title(string item) { }
    virtual void playlist_add_artist(string item) { }
    virtual void playlist_add_album(string item) { }
    virtual void playlist_add_title(string item) { }
    virtual void playlist_add_items(string item) { }
    virtual void playlist_play_items(string item) { }
    virtual void playlist_clear() { }
    virtual void playlist_save(string name) { }
    virtual void playlist_delete(string id) { }
    virtual void get_playlist_current(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }
    virtual void get_playlist_size(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }
    virtual void get_playlist_item(int index, AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }
    virtual void get_playlist_basic_info(int index, AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }
    virtual void get_playlist_album_cover(int i, AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }
    virtual void get_album_cover_id(string track_id, AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) { }

    virtual bool canPlaylist() { return false; }
    virtual bool canDatabase() { return false; }

    virtual Params getDatabaseCapabilities() { Params p; return p; }

    string get_param(string opt) { return param[opt]; }
    void set_param(string opt, string val) { param.Add(opt, val); }
    Params &get_params() { return param; }
    Output *get_output() { return aoutput; }
    Input *get_input() { return ainput; }

    AudioDB *get_database() { return database; }

    virtual bool LoadFromXml(TiXmlElement *node)
    { return false; }
    virtual bool SaveToXml(TiXmlElement *node);
};

}

#endif
