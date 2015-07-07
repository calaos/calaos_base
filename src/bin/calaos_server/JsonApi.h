/******************************************************************************
 **  Copyright (c) 2007-2015, Calaos. All Rights Reserved.
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
#ifndef JSONAPI_H
#define JSONAPI_H

#include "Calaos.h"
#include <jansson.h>
#include "Jansson_Addition.h"
#include "Room.h"
#include "AudioPlayer.h"

using namespace Calaos;

class HttpClient;

class JsonApi: public sigc::trackable
{
public:
    JsonApi(HttpClient *client);
    virtual ~JsonApi();

    virtual void processApi(const string &data) = 0;

    sigc::signal<void, const string &> sendData;
    sigc::signal<void, int, const string &> closeConnection;

protected:

    HttpClient *httpClient;

    map<string, int> playerCounts;

    json_t *buildJsonHome();
    json_t *buildJsonCameras();
    json_t *buildJsonAudio();

    template<typename T> void buildJsonIO(T *io, json_t *jio);
    template<typename T> json_t *buildJsonRoomIO(Room *room);

    //result is given with a call to a lambda because we may need to wait for
    //network queries
    void buildJsonState(json_t *jroot, std::function<void(json_t *)>result_lambda);
    void buildJsonStates(const Params &jParam, std::function<void(json_t *)>result_lambda);
    void buildQuery(const Params &jParam, std::function<void(json_t *)>result_lambda);

    json_t *buildJsonGetParam(const Params &jParam);
    json_t *buildJsonSetParam(const Params &jParam);
    json_t *buildJsonDelParam(const Params &jParam);

    json_t *buildJsonGetIO(json_t *jroot);

    bool decodeSetState(Params &jParam);
    void decodeGetPlaylist(Params &jParam, std::function<void(json_t *)>result_lambda);
    void getNextPlaylistItem(AudioPlayer *player, json_t *jplayer, json_t *jplaylist, int it_current, int it_count, std::function<void(json_t *)>result_lambda);

    AudioPlayer *getAudioPlayer(json_t *jdata, string &err);
    void audioGetDbStats(json_t *jdata, std::function<void(json_t *)>result_lambda);
    void audioGetPlaylistSize(json_t *jdata, std::function<void(json_t *)>result_lambda);
    void audioGetTime(json_t *jdata, std::function<void(json_t *)>result_lambda);
    void audioGetPlaylistItem(json_t *jdata, std::function<void(json_t *)>result_lambda);
    void audioGetCoverInfo(json_t *jdata, std::function<void(json_t *)>result_lambda);

    json_t *processDbResult(const AudioPlayerData &data);
    void audioDbGetAlbums(json_t *jdata, std::function<void(json_t *)>result_lambda);
    void audioDbGetArtists(json_t *jdata, std::function<void(json_t *)>result_lambda);
    void audioDbGetYears(json_t *jdata, std::function<void(json_t *)>result_lambda);
    void audioDbGetGenres(json_t *jdata, std::function<void(json_t *)>result_lambda);
    void audioDbGetPlaylists(json_t *jdata, std::function<void(json_t *)>result_lambda);
    void audioDbGetMusicFolder(json_t *jdata, std::function<void(json_t *)>result_lambda);
    void audioDbGetSearch(json_t *jdata, std::function<void(json_t *)>result_lambda);
    void audioDbGetRadios(json_t *jdata, std::function<void(json_t *)>result_lambda);

    void audioDbGetAlbumArtistItem(json_t *jdata, std::function<void(json_t *)>result_lambda);
    void audioDbGetYearAlbums(json_t *jdata, std::function<void(json_t *)>result_lambda);
    void audioDbGetGenreArtists(json_t *jdata, std::function<void(json_t *)>result_lambda);
    void audioDbGetAlbumTitles(json_t *jdata, std::function<void(json_t *)>result_lambda);
    void audioDbGetPlaylistTitles(json_t *jdata, std::function<void(json_t *)>result_lambda);
    void audioDbGetRadioItems(json_t *jdata, std::function<void(json_t *)>result_lambda);

    void audioDbGetTrackInfos(json_t *jdata, std::function<void(json_t *)>result_lambda);
};

#endif // JSONAPI_H

