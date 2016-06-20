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
#ifndef S_SQUEEZEBOX_H
#define S_SQUEEZEBOX_H

#include "Calaos.h"
#include "AudioPlayer.h"
#include "EcoreTimer.h"
#include "Ecore.h"
#include "Ecore_Con.h"

namespace Calaos
{

typedef sigc::slot<void, bool, std::string, std::string, AudioPlayerData> SqueezeRequest_cb;
typedef sigc::signal<void, bool, std::string, std::string, AudioPlayerData> SqueezeRequest_signal;

class SqueezeboxCommand
{
public:
    SqueezeboxCommand():
        inProgress(false),
        noCallback(false)
    { }

    bool status;

    std::string request;
    std::string result;

    bool inProgress;
    bool noCallback;

    SqueezeRequest_cb callback;

    AudioPlayerData user_data;
};

class SqueezeboxDB;

class Squeezebox: public AudioPlayer, public sigc::trackable
{
    friend class SqueezeboxDB;

protected:
    Ecore_Con_Server *enotif;
    Ecore_Con_Server *econ;

    Ecore_Event_Handler *ehandler_add;
    Ecore_Event_Handler *ehandler_del;
    Ecore_Event_Handler *ehandler_data;

    EcoreTimer *timer_notification;
    EcoreTimer *timer_con;

    EcoreTimer *timer_timeout;
    std::queue<SqueezeboxCommand> squeeze_commands;

    std::string host, id;
    int port_cli, port_web;

    bool isConnected;

    std::string buffer_notif, buffer_main;

    void timerNotificationReconnect();
    void timerConnReconnect();

    void processNotificationMessage(std::string msg);

    void _sendRequest();
    void requestTimeout_cb();

    void sendRequest(std::string request);
    void sendRequest(std::string request, SqueezeRequest_cb callback, AudioPlayerData user_data);

    void get_songinfo_cb(bool status, std::string request, std::string result, AudioPlayerData data);
    void get_songinfo_cb2(bool status, std::string request, std::string result, AudioPlayerData data);
    void get_songinfo_artist_cb(AudioPlayerData data);
    void get_songinfo_album_cb(AudioPlayerData data);
    void get_songinfo_title_cb(AudioPlayerData data);
    void get_songinfo_duration_cb(AudioPlayerData data);
    void get_songinfo_cover_cb(AudioPlayerData data);

    void get_title_cb(bool status, std::string request, std::string result, AudioPlayerData data);
    void get_title2_cb(bool status, std::string request, std::string result, AudioPlayerData data);

    void get_artist_cb(bool status, std::string request, std::string result, AudioPlayerData data);
    void get_artist2_cb(bool status, std::string request, std::string result, AudioPlayerData data);

    void get_album_cb(bool status, std::string request, std::string result, AudioPlayerData data);

    void get_album_cover_json_cb(std::string result, void *data, void *user_data);
    void get_album_cover_std(AudioPlayerData data);
    void get_album_cover_std_cb(bool status, std::string request, std::string result, AudioPlayerData data);
    void get_album_cover_std2_cb(bool status, std::string request, std::string result, AudioPlayerData data);

    void get_genre_cb(bool status, std::string request, std::string result, AudioPlayerData data);
    void get_current_time_cb(bool status, std::string request, std::string result, AudioPlayerData data);
    void get_duration_cb(bool status, std::string request, std::string result, AudioPlayerData data);

    void get_sleep_cb(bool status, std::string request, std::string result, AudioPlayerData data);
    void get_status_cb(bool status, std::string request, std::string result, AudioPlayerData data);
    void get_sync_status_cb(bool status, std::string request, std::string result, AudioPlayerData data);

    void get_playlist_current_cb(bool status, std::string request, std::string result, AudioPlayerData data);
    void get_playlist_size_cb(bool status, std::string request, std::string result, AudioPlayerData data);

    void get_playlist_item_cb(bool status, std::string request, std::string result, AudioPlayerData data);
    void get_playlist_item2_cb(bool status, std::string request, std::string result, AudioPlayerData data);
    void get_playlist_item3_cb(bool status, std::string request, std::string result, AudioPlayerData data);
    void get_playlist_item4_cb(AudioPlayerData data);
    void get_playlist_item5_cb(bool status, std::string request, std::string result, AudioPlayerData data);

    void get_playlist_info_cb(bool status, std::string request, std::string result, AudioPlayerData data);

    void get_playlist_album_cover_cb(bool status, std::string request, std::string result, AudioPlayerData data);
    void get_playlist_album_cover2_cb(bool status, std::string request, std::string result, AudioPlayerData data);

    void get_album_cover_id_cb(bool status, std::string request, std::string result, AudioPlayerData data);
    void get_volume_cb(bool status, std::string request, std::string result, AudioPlayerData data);
    void get_sync_list_cb(bool status, std::string request, std::string result, AudioPlayerData data);

public:
    Squeezebox(Params &p);
    virtual ~Squeezebox();

    virtual void Play();
    virtual void Pause();
    virtual void Stop();
    virtual void Next();
    virtual void Previous();
    virtual void Power(bool on);
    virtual void Sleep(int seconds);
    virtual void Synchronize(std::string playerid, bool sync);
    virtual void getSynchronizeList(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()); //get all players wich we can sync with

    virtual void get_volume(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void set_volume(int vol);

    //Return a list of option supported by the player
    virtual Params getOptions()
    {
        Params p;
        p.Add("sync", "true");
        p.Add("sleep", "true");
        return p;
    }

    virtual void get_title(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void get_artist(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void get_album(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void get_album_cover(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void get_genre(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void get_songinfo(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void get_current_time(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void set_current_time(double seconds);
    virtual void get_duration(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());

    //retourne le nombre de secondes restante avant le poweroff
    virtual void get_sleep(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void get_status(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void get_sync_status(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()); //get all players synced with

    //playlist functions
    virtual void playlist_moveup(int item);
    virtual void playlist_movedown(int item);
    virtual void playlist_delete(int item);
    virtual void playlist_play(int item);
    virtual void playlist_play_artist(std::string item);
    virtual void playlist_play_album(std::string item);
    virtual void playlist_play_title(std::string item);
    virtual void playlist_add_artist(std::string item);
    virtual void playlist_add_album(std::string item);
    virtual void playlist_add_title(std::string item);
    virtual void playlist_add_items(std::string item);
    virtual void playlist_play_items(std::string item);
    virtual void playlist_clear();
    virtual void playlist_save(std::string name);
    virtual void playlist_delete(std::string id);
    virtual void get_playlist_current(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void get_playlist_size(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void get_playlist_item(int index, AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void get_playlist_basic_info(int index, AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void get_playlist_album_cover(int i, AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());
    virtual void get_album_cover_id(std::string track_id, AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());

    virtual bool canPlaylist() { return true; }
    virtual bool canDatabase() { return true; }

    virtual Params getDatabaseCapabilities()
    {
        Params p;
        p.Add("artist", "true");
        p.Add("album", "true");
        p.Add("genre", "true");
        p.Add("year", "true");
        p.Add("music folder", "true");
        p.Add("playlist", "true");
        p.Add("radio", "true");
        p.Add("search", "true");
        return p;
    }

    /* This is private for C callbacks */
    void addConnection(Ecore_Con_Server *srv);
    void delConnection(Ecore_Con_Server *srv);
    void dataGet(Ecore_Con_Server *srv, void *data, int size);
    void processMessage(bool status, std::string msg);
};

}

#endif
