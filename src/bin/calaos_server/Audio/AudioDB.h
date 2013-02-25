/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
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
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#ifndef S_AUDIODB_H
#define S_AUDIODB_H

#include <Calaos.h>
#include <AudioPlayerData.h>

namespace Calaos
{

class AudioDB
{
        protected:
                Params param;

        public:
                AudioDB(Params &p);
                virtual ~AudioDB();

                virtual void getStats(AudioRequest_cb callback, AudioPlayerData user_data = AudioPlayerData()) {}

                //Album
                virtual void getAlbums(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data = AudioPlayerData()) {}
                virtual void getAlbumsTitles(AudioRequest_cb callback, int from, int nb, string album_id, AudioPlayerData user_data = AudioPlayerData()) {}

                //Artist
                virtual void getArtists(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data = AudioPlayerData()) {}
                virtual void getArtistsAlbums(AudioRequest_cb callback, int from, int nb, string artist_id, AudioPlayerData user_data = AudioPlayerData()) {}

                //Genre
                virtual void getGenres(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data = AudioPlayerData()) {}
                virtual void getGenresArtists(AudioRequest_cb callback, int from, int nb, string genre_id, AudioPlayerData user_data = AudioPlayerData()) {}

                //Year
                virtual void getYears(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data = AudioPlayerData()) {}
                virtual void getYearsAlbums(AudioRequest_cb callback, int from, int nb, string year, AudioPlayerData user_data = AudioPlayerData()) {}

                //Playlists
                virtual void getPlaylists(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data = AudioPlayerData()) {}
                virtual void getPlaylistsTracks(AudioRequest_cb callback, int from, int nb, string playlist_id, AudioPlayerData user_data = AudioPlayerData()) {}

                //Radios
                virtual void getRadios(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data = AudioPlayerData()) {}
                virtual void getRadiosItems(AudioRequest_cb callback, int from, int nb, string radio, string item_id = "", string search = "", AudioPlayerData user_data = AudioPlayerData()) {}

                //Random
                virtual void getRandoms(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data = AudioPlayerData()) {}
                virtual void setRandomsType(string type) {}
                //TODO:Random genre selection with  "<playerid>  randomplaygenrelist"

                virtual void getSearch(AudioRequest_cb callback, int from, int nb, string search, AudioPlayerData user_data = AudioPlayerData()) {}

                virtual void getMusicFolder(AudioRequest_cb callback, int from, int nb, string folder_id = "", AudioPlayerData user_data = AudioPlayerData()) {}
                virtual void getTrackInfos(AudioRequest_cb callback, string track_id, AudioPlayerData user_data = AudioPlayerData()) {}

                //TODO:Favorites
};

}

#endif
