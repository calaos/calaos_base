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
#include "AudioModel.h"
#include "CalaosModel.h"
#include <Ecore_File.h>

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

void AudioModel::load()
{
    connection->SendCommand("audio ?", sigc::mem_fun(*this, &AudioModel::audio_count_cb));
}

void AudioModel::audio_count_cb(bool success, vector<string> result, void *data)
{
    if (!success) return;

    if (result.size() < 2) return;

    if (is_of_type<int>(result[1]))
    {
        int count;
        from_string(result[1], count);

        load_count = 0;

        if (count == 0)
        {
            //No audio players found.
            load_done.emit();
        }

        for (int i = 0;i < count;i++)
        {
            AudioPlayer *player = new AudioPlayer(connection);
            players.push_back(player);

            player->params.Add("num", Utils::to_string(i));

            load_count++;
            player->load_done.connect(sigc::mem_fun(*this, &AudioModel::load_audio_done));

            string cmd = "audio get " + Utils::to_string(i);
            connection->SendCommand(cmd, sigc::mem_fun(*player, &AudioPlayer::audio_get_cb));
        }
    }
    else
    {
        //Load of audio players failed because of a wrong reply
        load_done.emit();
    }
}

void AudioModel::load_audio_done(AudioPlayer *audio)
{
    load_count--;

    cDebug() << "[AUDIO load done]";

    if (load_count <= 0)
    {
        cDebug() << "[AUDIO LOAD DONE sending signal]";
        load_done.emit();
    }
}

void AudioPlayer::audio_get_cb(bool success, vector<string> result, void *data)
{
    for (uint b = 2;b < result.size();b++)
    {
        vector<string> tmp;
        Utils::split(result[b], tmp, ":", 2);

        if (tmp.size() < 2) continue;

        params.Add(tmp[0], tmp[1]);
    }

    string cmd;

    //Query some more infos
    cmd = "audio " + params["num"] + " volume?";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::audio_volume_get_cb));

    cmd = "audio " + params["num"] + " status?";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::audio_status_get_cb));

    cmd = "audio " + params["num"] + " songinfo?";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::audio_songinfo_get_cb));

    cmd = "audio " + params["num"] + " playlist size?";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::audio_playlist_size_get_cb));

    cmd = "audio " + params["num"] + " database stats?";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::audio_db_stats_get_cb));

    IOBase *amp = getAmplifier();
    if (amp)
    {
        amp->sendUserCommand("states?", sigc::mem_fun(*this, &AudioPlayer::getAmpStates_cb));
        amp->sendUserCommand("query input_sources ?", sigc::mem_fun(*this, &AudioPlayer::getInputSource_cb));
    }

    load_done.emit(this);
}

void AudioPlayer::audio_volume_get_cb(bool success, vector<string> result, void *data)
{
    if (result.size() != 3) return;

    vector<string> tmp;
    Utils::split(result[2], tmp, ":", 2);

    from_string(tmp[1], volume);
    player_volume_changed.emit();
}

void AudioPlayer::audio_status_get_cb(bool success, vector<string> result, void *data)
{
    if (result.size() != 3) return;

    vector<string> tmp;
    Utils::split(result[2], tmp, ":", 2);

    if (tmp[1] == "playing") tmp[1] = "play";
    params.Add("status", tmp[1]);
    player_status_changed.emit();
}

void AudioPlayer::audio_songinfo_get_cb(bool success, vector<string> result, void *data)
{
    if (result.size() < 3) return;

    for (uint i = 2;i < result.size();i++)
    {
        vector<string> tmp;
        Utils::split(result[i], tmp, ":", 2);

        if (tmp.size() != 2) continue;

        if (tmp[0] == "duration")
        {
            from_string(tmp[1], duration);
            current_song_info.Add(tmp[0], Utils::time2string_digit((long)duration));
        }
        else
        {
            current_song_info.Add(tmp[0], tmp[1]);
        }
    }

    player_track_changed.emit();
}

void AudioPlayer::notifyChange(string msg)
{
    vector<string> notif;
    split(msg, notif, " ");

    if (notif.size() < 3) return;
    if (notif[1] != params["num"]) return;

    if (notif[0] == "audio")
    {
        if (notif[2] == "songchanged")
        {
            string cmd = "audio " + params["num"] + " status?";
            connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::audio_status_get_cb));

            cmd = "audio " + params["num"] + " songinfo?";
            connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::audio_songinfo_get_cb));
        }
    }
    else if (notif[0] == "audio_playlist")
    {
        if (notif[2] != "playlist") return;

        if (notif[3] == "cleared")
        {
            playlist_size = 0;
            player_playlist_changed.emit();
        }
        else if (notif[3] == "tracksadded")
        {
            string cmd = "audio " + params["num"] + " playlist size?";
            connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::audio_playlist_size_get_added_cb));
        }
        else if (notif[3] == "reload")
        {
            string cmd = "audio " + params["num"] + " playlist size?";
            connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::audio_playlist_size_get_cb));
        }
        else if (notif[3] == "delete")
        {
            playlist_size--;
            if (playlist_size < 0) playlist_size = 0;

            int del;
            from_string(notif[4], del);

            player_playlist_tracks_deleted.emit(del);
        }
        else if (notif[3] == "move")
        {
            int from, to;
            from_string(notif[4], from);
            from_string(notif[5], to);

            player_playlist_tracks_moved.emit(from, to);
        }
    }
    else if (notif[0] == "audio_status")
    {
        if (notif[2] == "play" || notif[2] == "playing")
            params.Add("status", "play");
        else if (notif[2] == "pause")
            params.Add("status", "pause");
        else if (notif[2] == "stop")
            params.Add("status", "stop");
        else
            return;

        player_status_changed.emit();
    }
    else if (notif[0] == "audio_volume")
    {
        if (notif[2] != "change") return;
        string v = Utils::url_decode(notif[3]);

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
}

void AudioPlayer::setVolume(int _volume)
{
    if (_volume < 0) _volume = 0;
    if (_volume > 100) _volume = 100;

    string cmd = "audio " + params["num"] + " volume " + Utils::to_string(_volume);
    connection->SendCommand(cmd);
}

void AudioPlayer::play()
{
    string cmd = "audio " + params["num"] + " play";
    connection->SendCommand(cmd);
}

void AudioPlayer::pause()
{
    string cmd = "audio " + params["num"] + " pause";
    connection->SendCommand(cmd);
}

void AudioPlayer::stop()
{
    string cmd = "audio " + params["num"] + " stop";
    connection->SendCommand(cmd);
}

void AudioPlayer::next()
{
    string cmd = "audio " + params["num"] + " next";
    connection->SendCommand(cmd);
}

void AudioPlayer::previous()
{
    string cmd = "audio " + params["num"] + " previous";
    connection->SendCommand(cmd);
}

void AudioPlayer::on()
{
    string cmd = "audio " + params["num"] + " on";
    connection->SendCommand(cmd);
}

void AudioPlayer::off()
{
    string cmd = "audio " + params["num"] + " off";
    connection->SendCommand(cmd);
}

string AudioPlayer::getStatus()
{
    return params["status"];
}

void AudioPlayer::registerChange()
{
    changeReg++;

    //Reload database stats in case of changes
    string cmd = "audio " + params["num"] + " database stats?";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::audio_db_stats_get_cb));

    if (!timer_change)
        timer_change = new EcoreTimer(0.5, (sigc::slot<void>)sigc::mem_fun(*this, &AudioPlayer::timerChangeTick));
}

void AudioPlayer::unregisterChange()
{
    changeReg--;

    if (changeReg < 0)
    {
        cWarningDom("network") << "AudioPlayer: WARNING, unregisterChange() called too many times !";
        changeReg = 0;
    }

    if (changeReg == 0)
        DELETE_NULL(timer_change);
}

void AudioPlayer::timerChangeTick()
{
    if (time_inprocess) return;

    string cmd = "audio " + params["num"] + " time?";
    time_inprocess = true;
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::audio_time_get_cb));
}

void AudioPlayer::audio_time_get_cb(bool success, vector<string> result, void *data)
{
    time_inprocess = false;

    if (result.size() != 3) return;

    vector<string> tmp;
    Utils::split(result[2], tmp, ":", 2);

    from_string(tmp[1], elapsed_time);
    params.Add("time", Utils::time2string_digit((long)elapsed_time));
    player_time_changed.emit();
}

void AudioPlayer::setTime(double time)
{
    string cmd = "audio " + params["num"] + " time " + Utils::to_string(time);
    connection->SendCommand(cmd);
}

void AudioPlayer::audio_playlist_size_get_cb(bool success, vector<string> result, void *data)
{
    if (result.size() != 4) return;

    from_string(result[3], playlist_size);
    player_playlist_changed.emit();
}

void AudioPlayer::audio_playlist_size_get_added_cb(bool success, vector<string> result, void *data)
{
    if (result.size() != 4) return;

    int nb_added = playlist_size;
    from_string(result[3], playlist_size);
    nb_added = playlist_size - nb_added;

    if (nb_added < 0)
        player_playlist_changed.emit();
    else
        player_playlist_tracks_added.emit(nb_added);
}

void AudioPlayer::audio_db_stats_get_cb(bool success, vector<string> result, void *data)
{
    if (result.size() < 4) return;

    for (uint b = 3;b < result.size();b++)
    {
        vector<string> tmp;
        Utils::split(result[b], tmp, ":", 2);

        if (tmp.size() < 2) continue;

        db_stats.Add(tmp[0], tmp[1]);
    }
}

void AudioPlayer::playItem(int item)
{
    string cmd = "audio " + params["num"] + " playlist " + Utils::to_string(item) + " play";
    connection->SendCommand(cmd);
}

void AudioPlayer::getPlaylistItem(int item, PlayerInfo_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " playlist " + Utils::to_string(item) + " getitem?";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::playlist_item_get_cb), data);
}

void AudioPlayer::playlist_item_get_cb(bool success, vector<string> result, void *data)
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

void AudioPlayer::removePlaylistItem(int item)
{
    string cmd = "audio " + params["num"] + " playlist " + Utils::to_string(item) + " delete";
    connection->SendCommand(cmd);
}

void AudioPlayer::getCurrentCover(PlayerInfo_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " cover?";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::cover_cb), data);
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
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " database albums " + Utils::to_string(item) + " 1";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
}

void AudioPlayer::getDBAlbumArtistItem(int item, int artist_id, PlayerInfo_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " database artist_albums " + Utils::to_string(item) + " 1 artist_id:" + Utils::to_string(artist_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
}

void AudioPlayer::getDBAlbumYearItem(int item, int year_id, PlayerInfo_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " database year_albums " + Utils::to_string(item) + " 1 year:" + Utils::to_string(year_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
}

void AudioPlayer::getDBArtistGenreItem(int item, int genre_id, PlayerInfo_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " database genre_artists " + Utils::to_string(item) + " 1 genre_id:" + Utils::to_string(genre_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
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
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " database album_titles 0 1 album_id:" + Utils::to_string(album_item);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_album_track_count_get_cb), data);
}

void AudioPlayer::getDBArtistAlbumCount(int artist_id, PlayerInfo_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " database artist_albums 0 1 artist_id:" + Utils::to_string(artist_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_album_track_count_get_cb), data);
}

void AudioPlayer::getDBYearAlbumCount(int year_id, PlayerInfo_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " database year_albums 0 1 year:" + Utils::to_string(year_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_album_track_count_get_cb), data);
}

void AudioPlayer::getDBGenreArtistCount(int genre_id, PlayerInfo_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " database genre_artists 0 1 genre_id:" + Utils::to_string(genre_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_album_track_count_get_cb), data);
}

void AudioPlayer::getDBPlaylistTrackCount(int playlist_id, PlayerInfo_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " database playlist_titles 0 1 playlist_id:" + Utils::to_string(playlist_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_album_track_count_get_cb), data);
}

void AudioPlayer::playlistDelete(string id)
{
    string cmd = "audio " + params["num"] + " database playlist delete playlist_id:" + Utils::to_string(id);
    connection->SendCommand(cmd);
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
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " database album_titles " + Utils::to_string(item) + " 1 album_id:" + Utils::to_string(album_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
}

void AudioPlayer::getDBPlaylistTrackItem(int playlist_id, int item, PlayerInfo_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " database playlist_titles " + Utils::to_string(item) + " 1 playlist_id:" + Utils::to_string(playlist_id);
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
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

    string cmd = "calaos_thumb " + item["cover_url"] + " " + fname + " " + cmdsize;
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

void AudioPlayer::getInputSource_cb(bool success, vector<string> result, void *data)
{
    if (!success) return;

    amplifier_inputs.clear();
    for (unsigned int i = 4;i < result.size();i++)
    {
        vector<string> tok;
        int num;
        split(result[i], tok, ":", 2);
        from_string(tok[0], num);

        amplifier_inputs[num] = tok[1];
    }
}

void AudioPlayer::getAmpStates_cb(bool success, vector<string> result, void *data)
{
    if (!success) return;

    IOBase *amp = getAmplifier();
    if (!amp) return;

    for (unsigned int i = 2;i < result.size();i++)
    {
        vector<string> tok;
        split(result[i], tok, ":", 2);

        amp->params.Add(tok[0], tok[1]);
    }
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
    string cmd = "audio " + params["num"] + " playlist add ";
    if (type == DB_ITEM_DIRECTURL)
        cmd += id;
    else
        cmd += itemTypeToString(type) + ":" + id;

    connection->SendCommand(cmd);
}

void AudioPlayer::playItem(int type, string id)
{
    string cmd = "audio " + params["num"] + " playlist play ";
    if (type == DB_ITEM_DIRECTURL)
        cmd += id;
    else
        cmd += itemTypeToString(type) + ":" + id;

    connection->SendCommand(cmd);
}

void AudioPlayer::getDBArtistItem(int item, PlayerInfo_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " database artists " + Utils::to_string(item) + " 1";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
}

void AudioPlayer::getDBYearItem(int item, PlayerInfo_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " database years " + Utils::to_string(item) + " 1";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
}

void AudioPlayer::getDBGenreItem(int item, PlayerInfo_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " database genres " + Utils::to_string(item) + " 1";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
}

void AudioPlayer::getDBPlaylistItem(int item, PlayerInfo_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " database playlists " + Utils::to_string(item) + " 1";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
}

void AudioPlayer::getDBFolder(string folder_id, PlayerInfoList_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback_list = callback;
    string cmd = "audio " + params["num"] + " database music_folder 0 999999";
    if (folder_id != "") cmd += " folder_id:" + folder_id;
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_list_get_cb), data);
}

void AudioPlayer::getDBSearch(string search, PlayerInfoList_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback_list = callback;
    string cmd = "audio " + params["num"] + " database search 0 999999";
    cmd += " search_terms:" + search;
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_list_get_cb), data);
}

void AudioPlayer::getDBTrackInfos(string track_id, PlayerInfo_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback = callback;
    string cmd = "audio " + params["num"] + " database track_infos track_id:" + track_id;
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_get_cb), data);
}

void AudioPlayer::getDBAllRadio(PlayerInfoList_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback_list = callback;
    string cmd = "audio " + params["num"] + " database radios 0 999999";
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_list_get_cb), data);
}

void AudioPlayer::getDBRadio(string radio_id, string subitem_id, PlayerInfoList_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback_list = callback;
    string cmd = "audio " + params["num"] + " database radio_items 0 999999 radio_id:" + radio_id;
    if (subitem_id != "") cmd += " item_id:" + subitem_id;
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_list_get_cb), data);
}

void AudioPlayer::getDBRadioSearch(string radio_id, string subitem_id, string search, PlayerInfoList_cb callback)
{
    PlayerInfoData *data = new PlayerInfoData();
    data->callback_list = callback;
    string cmd = "audio " + params["num"] + " database radio_items 0 999999 radio_id:" + radio_id;
    cmd += " item_id:" + subitem_id;
    cmd += " search:" + search;
    connection->SendCommand(cmd, sigc::mem_fun(*this, &AudioPlayer::db_default_item_list_get_cb), data);
}
