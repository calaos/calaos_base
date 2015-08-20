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
#include "SqueezeboxDB.h"
#include "Squeezebox.h"

using namespace Calaos;

SqueezeboxDB::SqueezeboxDB(Squeezebox *squeezebox, Params &p):
    AudioDB(p),
    player(squeezebox)
{
    cDebugDom("squeezebox") <<  "new Database at " << param["host"];
}

SqueezeboxDB::~SqueezeboxDB()
{
    cDebugDom("squeezebox");
}

void SqueezeboxDB::getAlbums(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data)
{
    string cmd = "albums " + Utils::to_string(from) + " " + Utils::to_string(nb) + " tags:lyja";

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getAlbums_cb), data);
}
void SqueezeboxDB::getAlbums_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<Params> &result = data.get_chain_data().vparams;
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    Params item;
    int cpt = 0;
    for (uint i = 0;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;

        split(tmp, tk, ":", 2);

        if (tk.size() != 2) continue;

        if (tk[0] == "id" || tk[0] == "count")
        {
            if (cpt > 0) result.push_back(item);
            item.clear();
            cpt++;

            item.Add(tk[0], url_decode2(tk[1]));
        }

        if (tk[0] == "album") item.Add("name", url_decode2(tk[1]));
        if (tk[0] == "year" && tk[1] != "0") item.Add("year", url_decode2(tk[1]));
        if (tk[0] == "artist") item.Add("artist", url_decode2(tk[1]));
        if (tk[0] == "artwork_track_id")
        {
            item.Add("cover_id", url_decode2(tk[1]));
            stringstream aurl;
            aurl << "http://" << player->host << ":" << player->port_web << "/music/" << item["cover_id"] << "/cover.jpg";
            item.Add("cover_url", aurl.str());
        }
    }

    result.push_back(item);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void SqueezeboxDB::getAlbumsTitles(AudioRequest_cb callback, int from, int nb, string album_id, AudioPlayerData user_data)
{
    string cmd = "titles " + Utils::to_string(from) + " " + Utils::to_string(nb);
    cmd += " album_id:" + album_id + " sort:tracknum tags:galdyorJilkmqtTvf";

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getAlbumsTitles_cb), data);
}
void SqueezeboxDB::getAlbumsTitles_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<Params> &result = data.get_chain_data().vparams;
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    Params item;
    int cpt = 0;
    for (uint i = 0;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;

        split(tmp, tk, ":", 2);

        if (tk.size() != 2) continue;

        if (tk[0] == "id" || tk[0] == "count")
        {
            if (cpt > 0) result.push_back(item);
            item.clear();
            cpt++;

            item.Add(tk[0], url_decode2(tk[1]));
        }

        if (tk[0] == "title") item.Add("title", url_decode2(tk[1]));
        if (tk[0] == "year" && tk[1] != "0") item.Add("year", url_decode2(tk[1]));
        if (tk[0] == "genre") item.Add("genre", url_decode2(tk[1]));
        if (tk[0] == "album") item.Add("album", url_decode2(tk[1]));
        if (tk[0] == "artist") item.Add("artist", url_decode2(tk[1]));
        if (tk[0] == "artwork_track_id")
        {
            item.Add("cover_id", url_decode2(tk[1]));
            stringstream aurl;
            aurl << "http://" << player->host << ":" << player->port_web << "/music/" << item["cover_id"] << "/cover.jpg";
            item.Add("cover_url", aurl.str());
        }
        if (tk[0] == "duration") item.Add("duration", url_decode2(tk[1]));
        if (tk[0] == "type") item.Add("type", url_decode2(tk[1]));
        if (tk[0] == "bitrate") item.Add("bitrate", url_decode2(tk[1]));
        if (tk[0] == "comment") item.Add("comment", url_decode2(tk[1]));
        if (tk[0] == "samplerate") item.Add("samplerate", url_decode2(tk[1]));
        if (tk[0] == "tagversion") item.Add("tagversion", url_decode2(tk[1]));
        if (tk[0] == "filesize") item.Add("filesize", url_decode2(tk[1]));
        if (tk[0] == "bpm") item.Add("bpm", url_decode2(tk[1]));
    }

    result.push_back(item);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void SqueezeboxDB::getArtists(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data)
{
    string cmd = "artists " + Utils::to_string(from) + " " + Utils::to_string(nb);

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getArtists_cb), data);
}
void SqueezeboxDB::getArtists_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<Params> &result = data.get_chain_data().vparams;
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    Params item;
    int cpt = 0;
    for (uint i = 0;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;

        split(tmp, tk, ":", 2);

        if (tk.size() != 2) continue;

        if (tk[0] == "id" || tk[0] == "count")
        {
            if (cpt > 0) result.push_back(item);
            item.clear();
            cpt++;

            item.Add(tk[0], url_decode2(tk[1]));
        }

        if (tk[0] == "artist") item.Add("name", url_decode2(tk[1]));
    }

    result.push_back(item);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void SqueezeboxDB::getArtistsAlbums(AudioRequest_cb callback, int from, int nb, string artist_id, AudioPlayerData user_data)
{
    string cmd = "albums " + Utils::to_string(from) + " " + Utils::to_string(nb);
    cmd += " artist_id:" + artist_id + " tags:lyja";

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getArtistsAlbums_cb), data);
}
void SqueezeboxDB::getArtistsAlbums_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<Params> &result = data.get_chain_data().vparams;
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    Params item;
    int cpt = 0;
    for (uint i = 0;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;

        split(tmp, tk, ":", 2);

        if (tk.size() != 2) continue;

        if (tk[0] == "id" || tk[0] == "count")
        {
            if (cpt > 0) result.push_back(item);
            item.clear();
            cpt++;

            item.Add(tk[0], url_decode2(tk[1]));
        }

        if (tk[0] == "album") item.Add("name", url_decode2(tk[1]));
        if (tk[0] == "year" && tk[1] != "0") item.Add("year", url_decode2(tk[1]));
        if (tk[0] == "artist") item.Add("artist", url_decode2(tk[1]));
        if (tk[0] == "artwork_track_id")
        {
            item.Add("cover_id", url_decode2(tk[1]));
            stringstream aurl;
            aurl << "http://" << player->host << ":" << player->port_web << "/music/" << item["cover_id"] << "/cover.jpg";
            item.Add("cover_url", aurl.str());
        }
    }

    result.push_back(item);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void SqueezeboxDB::getGenres(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data)
{
    string cmd = "genres " + Utils::to_string(from) + " " + Utils::to_string(nb);

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getGenres_cb), data);
}
void SqueezeboxDB::getGenres_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<Params> &result = data.get_chain_data().vparams;
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    Params item;
    int cpt = 0;
    for (uint i = 0;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;

        split(tmp, tk, ":", 2);

        if (tk.size() != 2) continue;

        if (tk[0] == "id" || tk[0] == "count")
        {
            if (cpt > 0) result.push_back(item);
            item.clear();
            cpt++;

            item.Add(tk[0], url_decode2(tk[1]));
        }

        if (tk[0] == "genre") item.Add("name", url_decode2(tk[1]));
    }

    result.push_back(item);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void SqueezeboxDB::getGenresArtists(AudioRequest_cb callback, int from, int nb, string genre_id, AudioPlayerData user_data)
{
    string cmd = "artists " + Utils::to_string(from) + " " + Utils::to_string(nb) + " genre_id:" + genre_id;

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getGenresArtists_cb), data);
}
void SqueezeboxDB::getGenresArtists_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<Params> &result = data.get_chain_data().vparams;
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    Params item;
    int cpt = 0;
    for (uint i = 0;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;

        split(tmp, tk, ":", 2);

        if (tk.size() != 2) continue;

        if (tk[0] == "id" || tk[0] == "count")
        {
            if (cpt > 0) result.push_back(item);
            item.clear();
            cpt++;

            item.Add(tk[0], url_decode2(tk[1]));
        }

        if (tk[0] == "artist") item.Add("name", url_decode2(tk[1]));
    }

    result.push_back(item);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void SqueezeboxDB::getYears(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data)
{
    string cmd = "years " + Utils::to_string(from) + " " + Utils::to_string(nb);

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getYears_cb), data);
}
void SqueezeboxDB::getYears_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<Params> &result = data.get_chain_data().vparams;
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    Params item;
    int cpt = 0;
    for (uint i = 0;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;

        split(tmp, tk, ":", 2);

        if (tk.size() != 2) continue;

        if (tk[0] == "year" || tk[0] == "count")
        {
            if (cpt > 0) result.push_back(item);
            item.clear();
            cpt++;

            item.Add(tk[0], url_decode2(tk[1]));
        }
    }

    result.push_back(item);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void SqueezeboxDB::getYearsAlbums(AudioRequest_cb callback, int from, int nb, string year, AudioPlayerData user_data)
{
    string cmd = "albums " + Utils::to_string(from) + " " + Utils::to_string(nb);
    cmd += " year:" + year + " tags:lyja";

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getYearsAlbums_cb), data);
}
void SqueezeboxDB::getYearsAlbums_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<Params> &result = data.get_chain_data().vparams;
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    Params item;
    int cpt = 0;
    for (uint i = 0;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;

        split(tmp, tk, ":", 2);

        if (tk.size() != 2) continue;

        if (tk[0] == "id" || tk[0] == "count")
        {
            if (cpt > 0) result.push_back(item);
            item.clear();
            cpt++;

            item.Add(tk[0], url_decode2(tk[1]));
        }

        if (tk[0] == "album") item.Add("name", url_decode2(tk[1]));
        if (tk[0] == "year" && tk[1] != "0") item.Add("year", url_decode2(tk[1]));
        if (tk[0] == "artist") item.Add("artist", url_decode2(tk[1]));
        if (tk[0] == "artwork_track_id")
        {
            item.Add("cover_id", url_decode2(tk[1]));
            stringstream aurl;
            aurl << "http://" << player->host << ":" << player->port_web << "/music/" << item["cover_id"] << "/cover.jpg";
            item.Add("cover_url", aurl.str());
        }
    }

    result.push_back(item);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void SqueezeboxDB::getPlaylists(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data)
{
    string cmd = "playlists " + Utils::to_string(from) + " " + Utils::to_string(nb);

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getPlaylists_cb), data);
}
void SqueezeboxDB::getPlaylists_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<Params> &result = data.get_chain_data().vparams;
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    Params item;
    int cpt = 0;
    for (uint i = 0;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;

        split(tmp, tk, ":", 2);

        if (tk.size() != 2) continue;

        if (tk[0] == "id" || tk[0] == "count")
        {
            if (cpt > 0) result.push_back(item);
            item.clear();
            cpt++;

            item.Add(tk[0], url_decode2(tk[1]));
        }

        if (tk[0] == "playlist") item.Add("name", url_decode2(tk[1]));
    }

    result.push_back(item);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void SqueezeboxDB::getPlaylistsTracks(AudioRequest_cb callback, int from, int nb, string playlist_id, AudioPlayerData user_data)
{
    /* We need to add the current player for gettting playlist tracks
           This is a bug in the squeezebox server
           http://bugs.slimdevices.com/show_bug.cgi?id=16454
         */
    string cmd = player->id;
    cmd += " playlists tracks " + Utils::to_string(from) + " " + Utils::to_string(nb);
    cmd += " playlist_id:" + playlist_id + " tags:galdyorJxuilkmqtTvf";

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getPlaylistsTracks_cb), data);
}
void SqueezeboxDB::getPlaylistsTracks_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<Params> &result = data.get_chain_data().vparams;
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    Params item;
    int cpt = 0;
    for (uint i = 0;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;

        split(tmp, tk, ":", 2);

        if (tk.size() != 2) continue;

        if (tk[0] == "playlist index" || tk[0] == "count")
        {
            if (cpt > 0) result.push_back(item);
            item.clear();
            cpt++;

            item.Add(tk[0], url_decode2(tk[1]));
        }

        if (tk[0] == "id") item.Add("id", url_decode2(tk[1]));
        if (tk[0] == "title") item.Add("title", url_decode2(tk[1]));
        if (tk[0] == "year" && tk[1] != "0") item.Add("year", url_decode2(tk[1]));
        if (tk[0] == "genre") item.Add("genre", url_decode2(tk[1]));
        if (tk[0] == "album") item.Add("album", url_decode2(tk[1]));
        if (tk[0] == "artist") item.Add("artist", url_decode2(tk[1]));
        if (tk[0] == "artwork_track_id")
        {
            item.Add("cover_id", url_decode2(tk[1]));
            stringstream aurl;
            aurl << "http://" << player->host << ":" << player->port_web << "/music/" << item["cover_id"] << "/cover.jpg";
            item.Add("cover_url", aurl.str());
        }
        if (tk[0] == "duration") item.Add("duration", url_decode2(tk[1]));
        if (tk[0] == "type") item.Add("type", url_decode2(tk[1]));
        if (tk[0] == "bitrate") item.Add("bitrate", url_decode2(tk[1]));
        if (tk[0] == "remote") item.Add("remote", url_decode2(tk[1]));
        if (tk[0] == "url") item.Add("url", url_decode2(tk[1]));
        if (tk[0] == "comment") item.Add("comment", url_decode2(tk[1]));
        if (tk[0] == "samplerate") item.Add("samplerate", url_decode2(tk[1]));
        if (tk[0] == "tagversion") item.Add("tagversion", url_decode2(tk[1]));
        if (tk[0] == "filesize") item.Add("filesize", url_decode2(tk[1]));
        if (tk[0] == "bpm") item.Add("bpm", url_decode2(tk[1]));
    }

    result.push_back(item);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void SqueezeboxDB::getRadios(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data)
{
    string cmd = "radios";
    cmd += " " + Utils::to_string(from) + " " + Utils::to_string(nb);

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.ivalue = from;
    data.dvalue = nb;
    data.svalue = "radios";

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getRadios_cb), data);
}

void SqueezeboxDB::getRadios_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<Params> &result = data.get_chain_data().vparams;
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    Params item;
    for (uint i = 3;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;
        split(tmp, tk, ":", 2);

        if (tk.size() != 2) continue;

        if (item.Exists(tk[0]))
        {
            if (item.Exists("icon"))
            {
                stringstream aurl;
                aurl << "http://" << player->host << ":" << player->port_web << "/" << item["icon"];
                item.Add("icon", aurl.str());
            }

            item.Add("id", item["cmd"]);
            item.Delete("cmd");

            result.push_back(item);
            item.clear();
        }

        item.Add(tk[0], tk[1]);
    }

    if (item.size() > 0)
    {
        if (item.Exists("icon"))
        {
            stringstream aurl;
            aurl << "http://" << player->host << ":" << player->port_web << "/" << item["icon"];
            item.Add("icon", aurl.str());
        }
        item.Add("id", item["cmd"]);
        item.Delete("cmd");

        result.push_back(item);
    }

    if (data.svalue == "radios")
        data.svalue = "music_services";
    else if (data.svalue == "music_services")
        data.svalue = "apps";
    else if (data.svalue == "apps")
        data.svalue.clear();

    if (data.svalue.empty())
    {
        AudioRequest_signal sig;
        sig.connect(data.callback);
        sig.emit(data.get_chain_data());
    }
    else
    {
        string cmd = data.svalue;
        cmd += " " + Utils::to_string(data.ivalue) + " " + Utils::to_string(data.dvalue);

        player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getRadios_cb), data);
    }
}

void SqueezeboxDB::getRadiosItems(AudioRequest_cb callback, int from, int nb, string radio, string item_id, string search, AudioPlayerData user_data)
{
    string cmd = radio + " items " + Utils::to_string(from) + " " + Utils::to_string(nb);
    if (item_id != "") cmd += " item_id:" + item_id;
    if (search != "") cmd += " search:" + search;

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getRadiosItems_cb), data);
}
void SqueezeboxDB::getRadiosItems_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<Params> &result = data.get_chain_data().vparams;
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    Params item;
    int cpt = 0;
    for (uint i = 0;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;

        split(tmp, tk, ":", 2);

        if (tk.size() != 2) continue;

        if (tk[0] == "id")
        {
            if (cpt > 0) result.push_back(item);
            item.clear();
            cpt++;

            item.Add(tk[0], url_decode2(tk[1]));
        }

        if (tk[0] == "name") item.Add("name", url_decode2(tk[1]));
        if (tk[0] == "title") item.Add("name", url_decode2(tk[1]));
        if (tk[0] == "isaudio") item.Add("isaudio", url_decode2(tk[1]));
        if (tk[0] == "hasitems") item.Add("hasitems", url_decode2(tk[1]));
        if (tk[0] == "type") item.Add("type", url_decode2(tk[1]));
        if (tk[0] == "icon") //Menu icons
        {
            stringstream aurl;
            aurl << "http://" << param["host"] << ":9000" << url_decode2(tk[1]);

            item.Add("coverart", "1");
            item.Add("coverart_url", aurl.str());
        }
        if (tk[0] == "image") //Radios coverart
        {
            item.Add("coverart", "1");
            item.Add("coverart_url", url_decode2(tk[1]));
        }
        //TODO: SqueezeCenter is able to get some more infos from streams
        //like nb of listeners, current playing track, ...
        //We could add a way to also display these infos
    }

    result.push_back(item);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void SqueezeboxDB::getRandoms(AudioRequest_cb callback, int from, int nb, AudioPlayerData user_data)
{
    vector<Params> &result = user_data.vparams;
    Params p;

    p.Add("id", "tracks");
    p.Add("name", "Random Tracks");
    result.push_back(p);
    p.clear();

    p.Add("id", "albums");
    p.Add("name", "Random Albums");
    result.push_back(p);
    p.clear();

    p.Add("id", "contributors");
    p.Add("name", "Random Artists");
    result.push_back(p);
    p.clear();

    p.Add("id", "years");
    p.Add("name", "Random Years");
    result.push_back(p);
    p.clear();

    p.Add("count", Utils::to_string(result.size()));
    result.push_back(p);
    p.clear();

    AudioRequest_signal sig;
    sig.connect(callback);
    sig.emit(user_data);
}

void SqueezeboxDB::setRandomsType(string type)
{
    string cmd, res;
    cmd = param["id"] + " randomplay " + Utils::to_string(type);

    player->sendRequest(cmd);
}

void SqueezeboxDB::getStats(AudioRequest_cb callback, AudioPlayerData user_data)
{
    //Get total genres
    string cmd = "info total genres ?";

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getStats_genre_cb), data);
}
void SqueezeboxDB::getStats_genre_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    if (tokens.size() == 4)
        data.get_chain_data().params.Add("genres", tokens[3]);

    //Get total artists
    string cmd = "info total artists ?";

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getStats_artist_cb), data);
}
void SqueezeboxDB::getStats_artist_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    if (tokens.size() == 4)
        data.get_chain_data().params.Add("artists", tokens[3]);

    //Get total album
    string cmd = "info total albums ?";

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getStats_album_cb), data);
}
void SqueezeboxDB::getStats_album_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    if (tokens.size() == 4)
        data.get_chain_data().params.Add("albums", tokens[3]);

    //Get total tracks
    string cmd = "info total songs ?";

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getStats_song_cb), data);
}
void SqueezeboxDB::getStats_song_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    if (tokens.size() == 4)
        data.get_chain_data().params.Add("tracks", tokens[3]);

    //Get total playlists
    string cmd = "playlists 0 0";

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getStats_playlist_cb), data);
}
void SqueezeboxDB::getStats_playlist_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    if (tokens.size() == 4)
    {
        vector<string> tk;
        split(tokens[3], tk, ":");
        if (tk.size() == 2)
            data.get_chain_data().params.Add("playlists", tk[1]);
    }

    //Get total years
    string cmd = "years 0 0";

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getStats_year_cb), data);
}
void SqueezeboxDB::getStats_year_cb(bool status, string request, string res, AudioPlayerData data)
{
    vector<string> tokens;
    split(res, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    if (tokens.size() == 4)
    {
        vector<string> tk;
        split(tokens[3], tk, ":");
        if (tk.size() == 2)
            data.get_chain_data().params.Add("years", tk[1]);
    }

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void SqueezeboxDB::getSearch(AudioRequest_cb callback, int from, int nb, string search, AudioPlayerData user_data)
{
    string cmd = "search " + Utils::to_string(from) + " " + Utils::to_string(nb) + " term:" + url_encode(search);

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getSearch_cb), data);
}
void SqueezeboxDB::getSearch_cb(bool status, string request, string res, AudioPlayerData data)
{
    if (res != "")
    {
        data.svalue = res;
        data.ivalue = 0;
        data.dvalue = 0.0;
    }

    vector<Params> &result = data.get_chain_data().vparams;
    vector<string> tokens;
    split(data.svalue, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    Params &item = data.params;
    int cpt = (int)data.dvalue;
    for (uint i = data.ivalue;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;

        split(tmp, tk, ":", 2);

        if (tk.size() != 2) continue;

        if (tk[0] == "contributor_id" || tk[0] == "album_id" || tk[0] == "track_id" || tk[0] == "genre_id" || tk[0] == "count")
        {
            if (cpt > 0) result.push_back(item);
            item.clear();
            cpt++;

            if (tk[0] == "count")
                item.Add("count", url_decode2(tk[1]));
            else
                item.Add("id", url_decode2(tk[1]));

            if (tk[0] == "contributor_id") item.Add("type", "artist");
            if (tk[0] == "album_id") item.Add("type", "album");
            if (tk[0] == "track_id") item.Add("type", "track");
            if (tk[0] == "genre_id") item.Add("type", "genre");
        }

        if (tk[0] == "contributor") item.Add("name", url_decode2(tk[1]));
        if (tk[0] == "album") item.Add("name", url_decode2(tk[1]));
        if (tk[0] == "track") item.Add("name", url_decode2(tk[1]));
        if (tk[0] == "genre") item.Add("name", url_decode2(tk[1]));
    }

    result.push_back(item);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void SqueezeboxDB::getMusicFolder(AudioRequest_cb callback, int from, int nb, string folder_id, AudioPlayerData user_data)
{
    string cmd = "musicfolder " + Utils::to_string(from) + " " + Utils::to_string(nb);
    if (folder_id != "") cmd += " folder_id:" + folder_id;

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getMusicFolder_cb), data);
}
void SqueezeboxDB::getMusicFolder_cb(bool status, string request, string res, AudioPlayerData data)
{
    if (res != "")
    {
        data.svalue = res;
        data.ivalue = 0;
        data.dvalue = 0.0;
    }

    vector<Params> &result = data.get_chain_data().vparams;
    vector<string> tokens;
    split(data.svalue, tokens);

    for_each(tokens.begin(), tokens.end(), UrlDecode());

    Params &item = data.params;
    int cpt = (int)data.dvalue;
    for (uint i = data.ivalue;i < tokens.size();i++)
    {
        string tmp = tokens[i];
        vector<string> tk;

        split(tmp, tk, ":", 2);

        if (tk.size() != 2) continue;

        if (tk[0] == "id" || tk[0] == "count")
        {
            if (cpt > 0) result.push_back(item);
            item.clear();
            cpt++;

            item.Add(tk[0], url_decode2(tk[1]));
        }

        if (tk[0] == "type") item.Add("type", url_decode2(tk[1]));
        if (tk[0] == "filename") item.Add("name", url_decode2(tk[1]));
    }

    result.push_back(item);

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}

void SqueezeboxDB::getTrackInfos(AudioRequest_cb callback, string track_id, AudioPlayerData user_data)
{
    string cmd = "songinfo 0 99999 tags:galdyorJilkmqtTvf track_id:" + track_id;

    AudioPlayerData data;
    data.callback = callback;
    data.set_chain_data(new AudioPlayerData(user_data));

    player->sendRequest(cmd, sigc::mem_fun(*this, &SqueezeboxDB::getTrackInfos_cb), data);
}

void SqueezeboxDB::getTrackInfos_cb(bool status, string request, string res, AudioPlayerData data)
{
    Params p;
    p.Parse(res);

    Params &item = data.get_chain_data().params;

    for (int i = 4;i < p.size();i++)
    {
        string value = p[Utils::to_string(i)];
        vector<string> tk;
        Utils::split(Utils::url_decode2(value), tk, ":", 2);

        if (tk.size() != 2)
            if (tk[0] == "id") item.Add("track_id", url_decode2(tk[1]));
        if (tk[0] == "title") item.Add("title", url_decode2(tk[1]));
        if (tk[0] == "year" && tk[1] != "0") item.Add("year", url_decode2(tk[1]));
        if (tk[0] == "genre") item.Add("genre", url_decode2(tk[1]));
        if (tk[0] == "album") item.Add("album", url_decode2(tk[1]));
        if (tk[0] == "artist") item.Add("artist", url_decode2(tk[1]));
        if (tk[0] == "artwork_track_id")
        {
            item.Add("cover_id", url_decode2(tk[1]));
            stringstream aurl;
            aurl << "http://" << player->host << ":" << player->port_web << "/music/" << item["cover_id"] << "/cover.jpg";
            item.Add("cover_url", aurl.str());
        }
        if (tk[0] == "duration") item.Add("duration", url_decode2(tk[1]));
        if (tk[0] == "type") item.Add("audiotype", url_decode2(tk[1]));
        if (tk[0] == "bitrate") item.Add("bitrate", url_decode2(tk[1]));
        if (tk[0] == "comment") item.Add("comment", url_decode2(tk[1]));
        if (tk[0] == "samplerate") item.Add("samplerate", url_decode2(tk[1]));
        if (tk[0] == "tagversion") item.Add("tagversion", url_decode2(tk[1]));
        if (tk[0] == "filesize") item.Add("filesize", url_decode2(tk[1]));
        if (tk[0] == "bpm") item.Add("bpm", url_decode2(tk[1]));
    }

    AudioRequest_signal sig;
    sig.connect(data.callback);
    sig.emit(data.get_chain_data());
}
