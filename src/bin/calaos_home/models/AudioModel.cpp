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
#include <Ecore_File.h>

#include "AudioModel.h"
#include "CalaosModel.h"
#include "Prefix.h"

static Eina_Bool exe_callback(void *data, int type, void *event)
{
    AudioModel *model = reinterpret_cast<AudioModel *>(data);
    Ecore_Exe_Event_Del *ev = reinterpret_cast<Ecore_Exe_Event_Del *>(event);

    model->executableDone(ev);

    return EINA_TRUE;
}

AudioModel::AudioModel(CalaosConnection *con):
    connection(con)
{
    ecore_file_mkdir(Utils::getCacheFile(".cover_cache").c_str());
    ecore_event_handler_add(ECORE_EXE_EVENT_DEL, exe_callback, this);
}

AudioModel::~AudioModel()
{
    for_each(players.begin(), players.end(), Delete());
}

void AudioModel::load(json_t *data)
{
    if (!data || !json_is_object(data)) return;

    json_t *jaudio = json_object_get(data, "audio");
    if (!json_is_array(jaudio))
    {
        load_done.emit();
        return;
    }

    size_t idx;
    json_t *value;

    json_array_foreach(jaudio, idx, value)
    {
        AudioPlayer *player = new AudioPlayer(connection);
        player->load(value);
        players.push_back(player);
    }

    load_done.emit();
}

AudioPlayer *AudioModel::getForId(string id)
{
    if (id == "")
        return nullptr;

    for (AudioPlayer *pl: players)
    {
        if (pl->params["input_id"] == id ||
            pl->params["output_id"] == id)
            return pl;
    }

    return nullptr;
}

void AudioPlayer::load(json_t *data)
{
    jansson_decode_object(data, params);

    json_t *jstat = json_object();
    json_t *jaudiolist = json_array();
    json_array_append_new(jaudiolist, json_string(params["id"].c_str()));
    json_object_set_new(jstat, "audio_players", jaudiolist);

    connection->sendCommand("get_state", jstat, sigc::mem_fun(*this, &AudioPlayer::audio_state_get_cb));
    Params p = {{ "audio_action", "get_database_stats"},
                { "player_id", params["id"] }};
    connection->sendCommand("audio", p, sigc::mem_fun(*this, &AudioPlayer::audio_db_stats_get_cb));
}

void AudioPlayer::audio_state_get_cb(json_t *jdata, void *data)
{
    VAR_UNUSED(data);

    json_t *jplayers = json_object_get(jdata, "audio_players");

    size_t idx;
    json_t *value;

    json_array_foreach(jplayers, idx, value)
    {
        string id = jansson_string_get(value, "player_id");
        if (id != params["id"]) continue;

        //volume
        string vol = jansson_string_get(value, "volume");
        from_string(vol, volume);
        player_volume_changed.emit();

        //status
        string st = jansson_string_get(value, "status");
        if (st == "playing") st = "play";
        params.Add("status", st);
        player_status_changed.emit();

        json_t *track = json_object_get(value, "current_track");
        if (track && json_is_object(track))
        {
            jansson_decode_object(track, current_song_info);
            string dur = jansson_string_get(track, "duration");
            from_string(dur, duration);
            current_song_info.Add("duration", Utils::time2string_digit((long)duration));
            player_track_changed.emit();
        }

        //playlist size
        string pls = jansson_string_get(value, "playlist_size");
        from_string(pls, playlist_size);
        player_playlist_changed.emit();
    }
}

void AudioPlayer::notifyChange(const string &msgtype, const Params &evdata)
{
    if (evdata["player_id"] != params["id"]) return;

    if (msgtype == "audio_song_changed")
    {
        //Query status again
        json_t *jstat = json_object();
        json_t *jaudiolist = json_array();
        json_array_append_new(jaudiolist, json_string(params["id"].c_str()));
        json_object_set_new(jstat, "audio_players", jaudiolist);

        connection->sendCommand("get_state", jstat, sigc::mem_fun(*this, &AudioPlayer::audio_state_get_cb));
    }
    else if (msgtype == "audio_status_changed")
    {
        if (evdata["state"] == "play" || evdata["state"] == "playing")
            params.Add("status", "play");
        else if (evdata["state"] == "pause")
            params.Add("status", "pause");
        else if (evdata["state"] == "stop")
            params.Add("status", "stop");
        else
            return;

        player_status_changed.emit();
    }
    else if (msgtype == "audio_volume_changed")
    {
        string v = evdata["volume"];

        if (v[0] == '+')
        {
            v.erase(v.begin());
            int t;
            from_string(v, t);
            volume += t;
            if (volume > 100) volume = 100;
        }
        else if (v[0] == '-')
        {
            v.erase(v.begin());
            int t;
            from_string(v, t);
            volume -= t;
            if (volume < 0) volume = 0;
        }
        else
        {
            from_string(v, volume);
        }

        player_volume_changed.emit();
    }
    else if (msgtype == "playlist_tracks_added")
    {
        Params p = {{ "audio_action", "get_playlist_size"},
                    { "player_id", params["id"] }};
        connection->sendCommand("audio", p, [=](json_t *jdata, void *)
        {
            string sz = jansson_string_get(jdata, "playlist_size");
            if (sz.empty()) return;

            int nb_added = playlist_size;
            from_string(sz, playlist_size);
            nb_added = playlist_size - nb_added;

            if (nb_added < 0)
                player_playlist_changed.emit();
            else
                player_playlist_tracks_added.emit(nb_added);
        });
    }
    else if (msgtype == "playlist_tracks_deleted")
    {
        playlist_size--;
        if (playlist_size < 0) playlist_size = 0;

        int del;
        from_string(evdata["position"], del);

        player_playlist_tracks_deleted.emit(del);
    }
    else if (msgtype == "playlist_tracks_moved")
    {
        int from, to;
        from_string(evdata["from"], from);
        from_string(evdata["to"], to);

        player_playlist_tracks_moved.emit(from, to);
    }
    else if (msgtype == "playlist_reload")
    {
        Params p = {{ "audio_action", "get_playlist_size"},
                    { "player_id", params["id"] }};
        connection->sendCommand("audio", p, [=](json_t *jdata, void *)
        {
            //playlist size
            string sz = jansson_string_get(jdata, "playlist_size");
            if (sz.empty()) return;
            from_string(sz, playlist_size);
            player_playlist_changed.emit();
        });
    }
    else if (msgtype == "playlist_cleared")
    {
        playlist_size = 0;
        player_playlist_changed.emit();
    }
}

void AudioPlayer::setVolume(int _volume)
{
    if (_volume < 0) _volume = 0;
    if (_volume > 100) _volume = 100;

    string s = "volume " + Utils::to_string(_volume);

    Params p = {{ "type", "audio" },
                { "player_id", params["id"] },
                { "value", s }};
    connection->sendCommand("set_state", p);
}

void AudioPlayer::play()
{
    Params p = {{ "type", "audio" },
                { "player_id", params["id"] },
                { "value", "play" }};
    connection->sendCommand("set_state", p);
}

void AudioPlayer::pause()
{
    Params p = {{ "type", "audio" },
                { "player_id", params["id"] },
                { "value", "pause" }};
    connection->sendCommand("set_state", p);
}

void AudioPlayer::stop()
{
    Params p = {{ "type", "audio" },
                { "player_id", params["id"] },
                { "value", "stop" }};
    connection->sendCommand("set_state", p);
}

void AudioPlayer::next()
{
    Params p = {{ "type", "audio" },
                { "player_id", params["id"] },
                { "value", "next" }};
    connection->sendCommand("set_state", p);
}

void AudioPlayer::previous()
{
    Params p = {{ "type", "audio" },
                { "player_id", params["id"] },
                { "value", "previous" }};
    connection->sendCommand("set_state", p);
}

void AudioPlayer::on()
{
    Params p = {{ "type", "audio" },
                { "player_id", params["id"] },
                { "value", "on" }};
    connection->sendCommand("set_state", p);
}

void AudioPlayer::off()
{
    Params p = {{ "type", "audio" },
                { "player_id", params["id"] },
                { "value", "off" }};
    connection->sendCommand("set_state", p);
}

string AudioPlayer::getStatus()
{
    return params["status"];
}

void AudioPlayer::registerChange()
{
    changeReg++;

    //Reload database stats in case of changes
    connection->sendCommand("audio", {{"audio_action", "get_database_stats"},
                                      {"player_id", params["id"].c_str()}},
                            sigc::mem_fun(*this, &AudioPlayer::audio_db_stats_get_cb));

    if (!timer_change)
        timer_change = new EcoreTimer(0.5, (sigc::slot<void>)sigc::mem_fun(*this, &AudioPlayer::timerChangeTick));
}

void AudioPlayer::unregisterChange()
{
    changeReg--;

    if (changeReg < 0)
    {
        cWarningDom("network") << "called too many times !";
        changeReg = 0;
    }

    if (changeReg == 0)
        DELETE_NULL(timer_change);
}

void AudioPlayer::timerChangeTick()
{
    if (time_inprocess) return;

    connection->sendCommand("audio", {{"audio_action", "get_time"},
                                      {"player_id", params["id"].c_str()}},
                            [=](json_t *jdata, void*)
    {
        time_inprocess = false;

        string s = jansson_string_get(jdata, "time_elapsed");
        if (s.empty()) return;
        from_string(s, elapsed_time);
        params.Add("time", Utils::time2string_digit((long)elapsed_time));
        player_time_changed.emit();
    });
}

void AudioPlayer::setTime(double time)
{
    string cmd = "time " + Utils::to_string(time);

    Params p = {{ "type", "audio" },
                { "player_id", params["id"] },
                { "value", cmd }};
    connection->sendCommand("set_state", p);
}

void AudioPlayer::audio_db_stats_get_cb(json_t *jdata, void *data)
{
    jansson_decode_object(jdata, db_stats);
}

void AudioPlayer::playItem(int item)
{
    string cmd = "playlist " + Utils::to_string(item) + " play";

    Params p = {{ "type", "audio" },
                { "player_id", params["id"] },
                { "value", cmd }};
    connection->sendCommand("set_state", p);
}

void AudioPlayer::getPlaylistItem(int item, PlayerInfo_cb callback)
{
    connection->sendCommand("audio", {{"audio_action", "get_playlist_item"},
                                      {"player_id", params["id"].c_str()},
                                      {"item", Utils::to_string(item)}},
                            [=](json_t *jdata, void*)
    {
        Params infos;
        jansson_decode_object(jdata, infos);
        callback(infos);
    });
}

void AudioPlayer::removePlaylistItem(int item)
{
    string cmd = "playlist " + Utils::to_string(item) + " delete";

    Params p = {{ "type", "audio" },
                { "player_id", params["id"] },
                { "value", cmd }};
    connection->sendCommand("set_state", p);
}

void AudioPlayer::getCurrentCover(PlayerInfo_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " cover?";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::cover_cb), data);
    */
}

void AudioPlayer::cover_cb(bool success, vector<string> result, void *data)
{
    PlayerInfoData *user_data = reinterpret_cast<PlayerInfoData *>(data);
    if (!user_data) return; //Probably leaking here !

    if (result.size() < 3) return;

    Params infos;

    vector<string> tmp;
    Utils::split(result[2], tmp, ":", 2);
    if (tmp.size() < 2) return;

    infos.Add("cover", tmp[1]);

    PlayerInfo_signal sig;
    sig.connect(user_data->callback);
    sig.emit(infos);

    delete user_data;
}

bool AudioPlayer::hasCover()
{
    if (current_song_info["coverart"] == "1")
        return true;

    return false;
}

Params &AudioPlayer::getDBStats()
{
    return db_stats;
}

void AudioPlayer::getDBAlbumItem(int item, PlayerInfo_cb callback)
{/*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " database albums " + Utils::to_string(item) + " 1";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
    */
}

void AudioPlayer::getDBAlbumArtistItem(int item, int artist_id, PlayerInfo_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " database artist_albums " + Utils::to_string(item) + " 1 artist_id:" + Utils::to_string(artist_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
    */
}

void AudioPlayer::getDBAlbumYearItem(int item, int year_id, PlayerInfo_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " database year_albums " + Utils::to_string(item) + " 1 year:" + Utils::to_string(year_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
    */
}

void AudioPlayer::getDBArtistGenreItem(int item, int genre_id, PlayerInfo_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " database genre_artists " + Utils::to_string(item) + " 1 genre_id:" + Utils::to_string(genre_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
    */
}

void AudioPlayer::db_default_item_get_cb(bool success, vector<string> result, void *data)
{
    PlayerInfoData *user_data = reinterpret_cast<PlayerInfoData *>(data);
    if (!user_data) return; //Probably leaking here !

    if (result.size() < 4) return;

    Params infos;

    for (uint b = 4;b < result.size();b++)
    {
        vector<string> tmp;
        Utils::split(result[b], tmp, ":", 2);

        if (tmp.size() < 2) continue;

        infos.Add(tmp[0], tmp[1]);
    }

    PlayerInfo_signal sig;
    sig.connect(user_data->callback);
    sig.emit(infos);

    delete user_data;
}

void AudioPlayer::db_default_item_list_get_cb(bool success, vector<string> result, void *data)
{
    PlayerInfoData *user_data = reinterpret_cast<PlayerInfoData *>(data);
    if (!user_data) return; //Probably leaking here !

    if (result.size() < 4) return;

    list<Params> infos;
    Params item;
    int cpt = 0;

    for (uint b = 4;b < result.size();b++)
    {
        string tmp = result[b];
        vector<string> tk;
        split(tmp, tk, ":", 2);
        if (tk.size() != 2) continue;

        if (tk[0] == "id")
        {
            if (cpt > 0) infos.push_back(item);
            item.clear();
            cpt++;
        }

        item.Add(tk[0], url_decode2(tk[1]));
    }

    if (item.size() > 0) infos.push_back(item);

    PlayerInfoList_signal sig;
    sig.connect(user_data->callback_list);
    sig.emit(infos);

    delete user_data;
}

void AudioPlayer::getDBAlbumTrackCount(int album_item, PlayerInfo_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " database album_titles 0 1 album_id:" + Utils::to_string(album_item);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_album_track_count_get_cb), data);
    */
}

void AudioPlayer::getDBArtistAlbumCount(int artist_id, PlayerInfo_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " database artist_albums 0 1 artist_id:" + Utils::to_string(artist_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_album_track_count_get_cb), data);
    */
}

void AudioPlayer::getDBYearAlbumCount(int year_id, PlayerInfo_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " database year_albums 0 1 year:" + Utils::to_string(year_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_album_track_count_get_cb), data);
    */
}

void AudioPlayer::getDBGenreArtistCount(int genre_id, PlayerInfo_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " database genre_artists 0 1 genre_id:" + Utils::to_string(genre_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_album_track_count_get_cb), data);
    */
}

void AudioPlayer::getDBPlaylistTrackCount(int playlist_id, PlayerInfo_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " database playlist_titles 0 1 playlist_id:" + Utils::to_string(playlist_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_album_track_count_get_cb), data);
    */
}

void AudioPlayer::playlistDelete(string id)
{
    string cmd = "database playlist delete playlist_id:" + Utils::to_string(id);

    Params p = {{ "type", "audio" },
                { "player_id", params["id"] },
                { "value", cmd }};
    connection->sendCommand("set_state", p);
}

void AudioPlayer::db_album_track_count_get_cb(bool success, vector<string> result, void *data)
{
    PlayerInfoData *user_data = reinterpret_cast<PlayerInfoData *>(data);
    if (!user_data) return; //Probably leaking here !

    if (result.size() < 5) return;

    Params infos;

    for (uint b = 4;b < result.size();b++)
    {
        vector<string> tmp;
        Utils::split(result[b], tmp, ":", 2);

        if (tmp.size() < 2) continue;

        if (tmp[0] == "count")
        {
            infos.Add(tmp[0], tmp[1]);
            break;
        }
    }

    PlayerInfo_signal sig;
    sig.connect(user_data->callback);
    sig.emit(infos);

    delete user_data;
}

void AudioPlayer::getDBAlbumTrackItem(int album_id, int item, PlayerInfo_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " database album_titles " + Utils::to_string(item) + " 1 album_id:" + Utils::to_string(album_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
    */
}

void AudioPlayer::getDBPlaylistTrackItem(int playlist_id, int item, PlayerInfo_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " database playlist_titles " + Utils::to_string(item) + " 1 playlist_id:" + Utils::to_string(playlist_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
    */
}

void AudioPlayer::getDBAlbumCoverItem(Params &item, PlayerInfo_cb callback, int size)
{
    if (!item.Exists("cover_url")) return;

    string fname = Utils::getCacheFile(".cover_cache/album_") + item["cover_id"];

    string cmdsize;
    switch (size)
    {
    default:
    case AUDIO_COVER_SIZE_SMALL: cmdsize = "40x40"; fname += "_small.jpg"; break;
    case AUDIO_COVER_SIZE_MEDIUM: cmdsize = "100x100"; fname += "_medium.jpg"; break;
    case AUDIO_COVER_SIZE_BIG: cmdsize = "250x250"; fname += "_big.jpg"; break;
    }

    if (ecore_file_exists(fname.c_str()))
    {
        PlayerInfo_signal sig;
        sig.connect(callback);
        Params p;
        p.Add("filename", fname);
        sig.emit(p);

        return;
    }

    PlayerInfoData *data = new PlayerInfoData();
    data->cover_fname = fname;
    data->callback = callback;
    data->item = item;

    string cmd;
    cmd = Prefix::Instance().binDirectoryGet();
    cmd += "calaos_thumb " + item["cover_url"] + " " + fname + " " + cmdsize;
    data->thumb_exe = ecore_exe_run(cmd.c_str(), data);
    if (!data->thumb_exe)
    {
        PlayerInfo_signal sig;
        sig.connect(callback);
        Params p;
        sig.emit(p);

        delete data;
    }
}

void AudioModel::executableDone(Ecore_Exe_Event_Del *event)
{
    if (!event) return;
    PlayerInfoData *data = reinterpret_cast<PlayerInfoData *>(ecore_exe_data_get(event->exe));
    if (!data) return;
    if (data->thumb_exe != event->exe) return;

    PlayerInfo_signal sig;
    sig.connect(data->callback);
    Params p;
    p.Add("filename", data->cover_fname);
    sig.emit(p);

    delete data;
}

IOBase *AudioPlayer::getAmplifier()
{
    if (!params.Exists("amp_id")) return NULL;

    map<string, IOBase *>::const_iterator it = CalaosModel::Instance().getHome()->getCacheOutputs().find(params["amp_id"]);
    if (it == CalaosModel::Instance().getHome()->getCacheOutputs().end())
        return NULL;

    return (*it).second;
}

string AudioPlayer::getAmplifierStatus(string key)
{
    IOBase *amp = getAmplifier();
    if (!amp) return "";

    return amp->params[key];
}

string AudioPlayer::itemTypeToString(int type)
{
    switch (type)
    {
    case DB_ITEM_TRACK: return "track_id";
    case DB_ITEM_ALBUM: return "album_id";
    case DB_ITEM_ARTIST: return "artist_id";
    case DB_ITEM_GENRE: return "genre_id";
    case DB_ITEM_YEAR: return "year";
    case DB_ITEM_PLAYLIST: return "playlist_id";
    case DB_ITEM_FOLDER: return "folder_id";
    case DB_ITEM_RADIO: return "radio_id";
    default:
        return "";
    }
}

int AudioPlayer::itemStringToType(string type)
{
    if (type == "track_id") return DB_ITEM_TRACK;
    else if (type == "album_id") return DB_ITEM_ALBUM;
    else if (type == "artist_id") return DB_ITEM_ARTIST;
    else if (type == "genre_id") return DB_ITEM_GENRE;
    else if (type == "year") return DB_ITEM_YEAR;
    else if (type == "playlist_id") return DB_ITEM_PLAYLIST;
    else if (type == "folder_id") return DB_ITEM_FOLDER;
    else if (type == "radio_id") return DB_ITEM_RADIO;
    else return DB_ITEM_NONE;
}

void AudioPlayer::addItem(int type, string id)
{
    string cmd = "playlist add ";
    if (type == DB_ITEM_DIRECTURL)
        cmd += id;
    else
        cmd += itemTypeToString(type) + ":" + id;

    Params p = {{ "type", "audio" },
                { "player_id", params["id"] },
                { "value", cmd }};
    connection->sendCommand("set_state", p);
}

void AudioPlayer::playItem(int type, string id)
{
    string cmd = "playlist play ";
    if (type == DB_ITEM_DIRECTURL)
        cmd += id;
    else
        cmd += itemTypeToString(type) + ":" + id;

    Params p = {{ "type", "audio" },
                { "player_id", params["id"] },
                { "value", cmd }};
    connection->sendCommand("set_state", p);
}

void AudioPlayer::getDBArtistItem(int item, PlayerInfo_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " database artists " + Utils::to_string(item) + " 1";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
    */
}

void AudioPlayer::getDBYearItem(int item, PlayerInfo_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " database years " + Utils::to_string(item) + " 1";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
    */
}

void AudioPlayer::getDBGenreItem(int item, PlayerInfo_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " database genres " + Utils::to_string(item) + " 1";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
    */
}

void AudioPlayer::getDBPlaylistItem(int item, PlayerInfo_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " database playlists " + Utils::to_string(item) + " 1";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
    */
}

void AudioPlayer::getDBFolder(string folder_id, PlayerInfoList_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback_list = callback;
    string cmd = "audio " + params["id"] + " database music_folder 0 999999";
    if (folder_id != "") cmd += " folder_id:" + folder_id;
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_list_get_cb), data);
    */
}

void AudioPlayer::getDBSearch(string search, PlayerInfoList_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback_list = callback;
    string cmd = "audio " + params["id"] + " database search 0 999999";
    cmd += " search_terms:" + search;
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_list_get_cb), data);
    */
}

void AudioPlayer::getDBTrackInfos(string track_id, PlayerInfo_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["id"] + " database track_infos track_id:" + track_id;
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
    */
}

void AudioPlayer::getDBAllRadio(PlayerInfoList_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback_list = callback;
    string cmd = "audio " + params["id"] + " database radios 0 999999";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_list_get_cb), data);
    */
}

void AudioPlayer::getDBRadio(string radio_id, string subitem_id, PlayerInfoList_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback_list = callback;
    string cmd = "audio " + params["id"] + " database radio_items 0 999999 radio_id:" + radio_id;
    if (subitem_id != "") cmd += " item_id:" + subitem_id;
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_list_get_cb), data);
    */
}

void AudioPlayer::getDBRadioSearch(string radio_id, string subitem_id, string search, PlayerInfoList_cb callback)
{
    /*
    PlayerInfoData *data = new PlayerInfoData();
    data->callback_list = callback;
    string cmd = "audio " + params["id"] + " database radio_items 0 999999 radio_id:" + radio_id;
    cmd += " item_id:" + subitem_id;
    cmd += " search:" + search;
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_list_get_cb), data);
    */
}
