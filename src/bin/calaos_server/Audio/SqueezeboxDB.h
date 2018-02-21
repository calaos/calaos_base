/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#ifndef S_SQDB_H
#define S_SQDB_H

#include "Calaos.h"
#include "AudioPlayer.h"
#include "AudioDB.h"

namespace Calaos
{

class Squeezebox;

class SqueezeboxDB: public AudioDB
{
private:
    Squeezebox *player;

    void getAlbums_cb(bool status, string request, string result, AudioPlayerData data);
    void getAlbumsTitles_cb(bool status, string request, string result, AudioPlayerData data);

    void getArtists_cb(bool status, string request, string result, AudioPlayerData data);
    void getArtistsAlbums_cb(bool status, string request, string result, AudioPlayerData data);

    void getGenres_cb(bool status, string request, string result, AudioPlayerData data);
    void getGenresArtists_cb(bool status, string request, string result, AudioPlayerData data);

    void getYears_cb(bool status, string request, string result, AudioPlayerData data);
    void getYearsAlbums_cb(bool status, string request, string result, AudioPlayerData data);

    void getPlaylists_cb(bool status, string request, string result, AudioPlayerData data);
    void getPlaylistsTracks_cb(bool status, string request, string result, AudioPlayerData data);

    void getRadios_cb(bool status, string request, string result, AudioPlayerData data);
    void getRadiosItems_cb(bool status, string request, string result, AudioPlayerData data);

    void getRandoms_cb(bool status, string request, string result, AudioPlayerData data);
    void getRandomType_cb(bool status, string request, string result, AudioPlayerData data);

    void getSearch_cb(bool status, string request, string result, AudioPlayerData data);

    void getMusicFolder_cb(bool status, string request, string result, AudioPlayerData data);

    void getStats_genre_cb(bool status, string request, string result, AudioPlayerData data);
    void getStats_artist_cb(bool status, string request, string result, AudioPlayerData data);
    void getStats_album_cb(bool status, string request, string result, AudioPlayerData data);
    void getStats_song_cb(bool status, string request, string result, AudioPlayerData data);
    void getStats_playlist_cb(bool status, string request, string result, AudioPlayerData data);
    void getStats_year_cb(bool status, string request, string result, AudioPlayerData data);

    void getTrackInfos_cb(bool status, string request, string result, AudioPlayerData data);

public:
    SqueezeboxDB(Squeezebox *squeezebox, Params &p);
    ~SqueezeboxDB();

    virtual void getStats(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData());

    //Album
    virtual void getAlbums(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data = AudioPlayerData());
    virtual void getAlbumsTitles(AudioRequest_cb callback, int from, int nb, string album_id, AudioPlayerData user_data = AudioPlayerData());

    //Artist
    virtual void getArtists(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data = AudioPlayerData());
    virtual void getArtistsAlbums(AudioRequest_cb callback, int from, int nb, string artist_id, AudioPlayerData user_data = AudioPlayerData());

    //Genre
    virtual void getGenres(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data = AudioPlayerData());
    virtual void getGenresArtists(AudioRequest_cb callback, int from, int nb, string genre_id, AudioPlayerData user_data = AudioPlayerData());

    //Year
    virtual void getYears(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data = AudioPlayerData());
    virtual void getYearsAlbums(AudioRequest_cb callback, int from, int nb, string year, AudioPlayerData user_data = AudioPlayerData());

    //Playlists
    virtual void getPlaylists(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data = AudioPlayerData());
    virtual void getPlaylistsTracks(AudioRequest_cb callback, int from, int nb, string playlist_id, AudioPlayerData user_data = AudioPlayerData());

    //Radios
    virtual void getRadios(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data = AudioPlayerData());
    virtual void getRadiosItems(AudioRequest_cb callback, int from, int nb, string radio, string item_id = "", string search = "", AudioPlayerData user_data = AudioPlayerData());

    //Random
    virtual void getRandoms(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data = AudioPlayerData());
    virtual void setRandomsType(string type);
    //TODO:Random genre selection with  "<playerid>  randomplaygenrelist"

    virtual void getSearch(AudioRequest_cb callback, int from, int nb, string search, AudioPlayerData user_data = AudioPlayerData());

    virtual void getMusicFolder(AudioRequest_cb callback, int from, int nb, string folder_id = "", AudioPlayerData user_data = AudioPlayerData());
    virtual void getTrackInfos(AudioRequest_cb callback, string track_id, AudioPlayerData user_data = AudioPlayerData());

    //TODO:Favorites
};

}

#endif
