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
#include <TCPConnection.h>
#include <CalaosConfig.h>

using namespace Calaos;

void TCPConnection::AudioCommand(Params &request, ProcessDone_cb callback)
{
        Params result = request;

        //camera functions
        if (request["0"] == "audio")
        {
                Output* output = NULL;

                cDebugDom("network") << "TCPConnection::AudioCommand(audio)" << log4cpp::eol;
                if (request["1"] == "?")
                {
                        result.Add("1", Utils::to_string(AudioManager::Instance().get_size()));
                }

                if (request["1"] == "get")
                {
                        if (Utils::is_of_type<int>(request["2"]))
                        {
                                int id;
                                AudioPlayer *player = NULL;
                                Utils::from_string(request["2"], id);

                                if (id >= 0 && id < AudioManager::Instance().get_size())
                                {
                                        player = AudioManager::Instance().get_player(id);

                                        result.Add("1", Utils::to_string(id));
                                        result.Add("2", "id:" + player->get_param("id"));
                                        result.Add("3", "name:" + player->get_param("name"));
                                        if (player->canPlaylist())
                                                result.Add("4", "playlist:true");
                                        else
                                                result.Add("4", "playlist:false");
                                        if (player->canDatabase())
                                                result.Add("5", "database:true");
                                        else
                                                result.Add("5", "database:false");
                                        result.Add("6", "input_id:" + player->get_param("iid"));
                                        result.Add("7", "output_id:" + player->get_param("oid"));

                                        if (player->get_params().Exists("amp"))
                                        {
                                                if (ListeRoom::Instance().get_output(player->get_param("amp")))
                                                        result.Add("8", "amp_id:" + player->get_param("amp"));
                                        }
                                }
                        }
                }
                else if (request["1"] == "stop" && request["2"] == "all")
                {
                        for (int i = 0;i < AudioManager::Instance().get_size();i++)
                                AudioManager::Instance().get_player(i)->Stop();

                        result.Add("2", "ok");
                }
                else if (Utils::is_of_type<int>(request["1"])
                                || (output = ListeRoom::Instance().get_output(request["1"])) )
                {
                        int id;
                        AudioPlayer *player = NULL;

                        if(output)
                        {
                               id =  AudioManager::Instance().searchIdOf(output);
                               if(id < 0)
                               {
                                       ProcessDone_signal sig;
                                       sig.connect(callback);
                                       sig.emit(result);

                                       return;
                               }
                        }
                        else
                        {
                                Utils::from_string(request["1"], id);
                        }

                        if (id >= 0 && id < AudioManager::Instance().get_size())
                        {
                                player = AudioManager::Instance().get_player(id);

                                if (request["2"] == "play")
                                {
                                        player->Play();
                                        result.Add("2", "ok");
                                }
                                else if (request["2"] == "pause")
                                {
                                        player->Pause();
                                        result.Add("2", "ok");
                                }
                                else if (request["2"] == "stop")
                                {
                                        player->Stop();
                                        result.Add("2", "ok");
                                }
                                else if (request["2"] == "next")
                                {
                                        player->Next();
                                        result.Add("2", "ok");
                                }
                                else if (request["2"] == "previous")
                                {
                                        player->Previous();
                                        result.Add("2", "ok");
                                }
                                else if (request["2"] == "off")
                                {
                                        player->Power(false);
                                        result.Add("2", "ok");
                                }
                                else if (request["2"] == "on")
                                {
                                        player->Power(true);
                                        result.Add("2", "ok");
                                }
                                else if (request["2"] == "volume?")
                                {
                                        AudioPlayerData data;
                                        TCPConnectionData *tdata = new TCPConnectionData();
                                        tdata->result = result;
                                        tdata->callback = callback;
                                        data.user_data = tdata;

                                        player->get_volume(sigc::mem_fun(*this, &TCPConnection::get_volume_cb), data);

                                        return;
                                }
                                else if (request["2"] == "volume")
                                {
                                        if (Utils::is_of_type<int>(request["3"]))
                                        {
                                                int vol;
                                                Utils::from_string(request["3"], vol);
                                                player->set_volume(vol);
                                                result.Add("2", "ok");
                                        }
                                }
                                else if (request["2"] == "songinfo?")
                                {
                                        AudioPlayerData data;
                                        TCPConnectionData *tdata = new TCPConnectionData();
                                        tdata->result = result;
                                        tdata->callback = callback;
                                        data.user_data = tdata;

                                        player->get_songinfo(sigc::mem_fun(*this, &TCPConnection::get_songinfo_cb), data);

                                        return;
                                }
                                else if (request["2"] == "cover?")
                                {
                                        AudioPlayerData data;
                                        TCPConnectionData *tdata = new TCPConnectionData();
                                        tdata->result = result;
                                        tdata->callback = callback;
                                        data.user_data = tdata;

                                        player->get_album_cover(sigc::mem_fun(*this, &TCPConnection::get_album_cover_cb), data);

                                        return;
                                }
                                else if (request["2"] == "cover")
                                {
                                        AudioPlayerData data;
                                        TCPConnectionData *tdata = new TCPConnectionData();
                                        tdata->result = result;
                                        tdata->callback = callback;
                                        data.user_data = tdata;

                                        player->get_album_cover_id(request["3"], sigc::mem_fun(*this, &TCPConnection::get_album_cover_id_cb), data);

                                        return;
                                }
                                else if (request["2"] == "time?")
                                {
                                        AudioPlayerData data;
                                        TCPConnectionData *tdata = new TCPConnectionData();
                                        tdata->result = result;
                                        tdata->callback = callback;
                                        data.user_data = tdata;

                                        player->get_current_time(sigc::mem_fun(*this, &TCPConnection::get_current_time_cb), data);

                                        return;
                                }
                                else if (request["2"] == "time")
                                {
                                        if (Utils::is_of_type<double>(request["3"]))
                                        {
                                                double _time;
                                                Utils::from_string(request["3"], _time);
                                                player->set_current_time(_time);
                                                result.Add("2", "ok");
                                        }
                                }
                                else if (request["2"] == "status?")
                                {
                                        AudioPlayerData data;
                                        TCPConnectionData *tdata = new TCPConnectionData();
                                        tdata->result = result;
                                        tdata->callback = callback;
                                        data.user_data = tdata;

                                        player->get_status(sigc::mem_fun(*this, &TCPConnection::get_status_cb), data);

                                        return;
                                }
                                else if (request["2"] == "playlist")
                                { //Current Playlist related stuff

                                        if (request["3"] == "size?")
                                        {
                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_playlist_size(sigc::mem_fun(*this, &TCPConnection::get_playlist_size_cb), data);

                                                return;
                                        }
                                        else if (request["3"] == "current?")
                                        {
                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_playlist_current(sigc::mem_fun(*this, &TCPConnection::get_playlist_current_cb), data);

                                                return;
                                        }
                                        else if (request["3"] == "clear")
                                        {
                                                player->playlist_clear();
                                                result.Add("3", "ok");
                                        }
                                        else if (request["3"] == "save")
                                        {
                                                player->playlist_save(request["4"]);
                                                result.Add("4", "ok");
                                        }
                                        else if (request["3"] == "add")
                                        {
                                                player->playlist_add_items(request["4"]);
                                                result.Add("4", "ok");
                                        }
                                        else if (request["3"] == "play")
                                        {
                                                player->playlist_play_items(request["4"]);
                                                result.Add("4", "ok");
                                        }
                                        else if (Utils::is_of_type<int>(request["3"]))
                                        {
                                                int item;
                                                Utils::from_string(request["3"], item);

                                                if (request["4"] == "getitem?")
                                                {
                                                        result.Add("4", "item:" + Utils::to_string(item));

                                                        AudioPlayerData data;
                                                        TCPConnectionData *tdata = new TCPConnectionData();
                                                        tdata->result = result;
                                                        tdata->callback = callback;
                                                        data.user_data = tdata;

                                                        player->get_playlist_item(item, sigc::mem_fun(*this, &TCPConnection::get_playlist_item_cb), data);

                                                        return;
                                                }
                                                if (request["4"] == "getitembasic?")
                                                {
                                                        result.Add("4", "item:" + Utils::to_string(item));

                                                        AudioPlayerData data;
                                                        TCPConnectionData *tdata = new TCPConnectionData();
                                                        tdata->result = result;
                                                        tdata->callback = callback;
                                                        data.user_data = tdata;

                                                        player->get_playlist_basic_info(item, sigc::mem_fun(*this, &TCPConnection::get_playlist_item_cb), data);

                                                        return;
                                                }
                                                else if (request["4"] == "moveup")
                                                {
                                                        player->playlist_moveup(item);
                                                        result.Add("4", "ok");
                                                }
                                                else if (request["4"] == "movedown")
                                                {
                                                        player->playlist_movedown(item);
                                                        result.Add("4", "ok");
                                                }
                                                else if (request["4"] == "delete")
                                                {
                                                        player->playlist_delete(item);
                                                        result.Add("4", "ok");
                                                }
                                                else if (request["4"] == "play")
                                                {
                                                        player->playlist_play(item);
                                                        result.Add("4", "ok");
                                                }
                                                else if (request["4"] == "cover?")
                                                {
                                                        AudioPlayerData data;
                                                        TCPConnectionData *tdata = new TCPConnectionData();
                                                        tdata->result = result;
                                                        tdata->callback = callback;
                                                        data.user_data = tdata;

                                                        player->get_playlist_album_cover(item, sigc::mem_fun(*this, &TCPConnection::get_playlist_cover_cb), data);

                                                        return;
                                                }
                                        }
                                }
                                else if (request["2"] == "random")
                                { //Music library related stuff

                                        vector<string> tk;
                                        split(request["3"], tk, ":", 2);
                                        if (tk.size() != 2 || tk[0] != "random_id")
                                        {
                                                ProcessDone_signal sig;
                                                sig.connect(callback);
                                                sig.emit(result);

                                                return;
                                        }

                                        player->get_database()->setRandomsType(tk[1]);
                                }
                                else if (request["2"] == "options?")
                                {
                                        Params p = player->getOptions();

                                        result.Add("2", "options");
                                        for (int i = 0;i < p.size();i++)
                                        {
                                                string key, value;
                                                p.get_item(i, key, value);
                                                result.Add(Utils::to_string(i + 3), key + ":" + value);
                                        }
                                }
                                else if (request["2"] == "options" && request["3"] == "sync?")
                                {
                                        AudioPlayerData data;
                                        TCPConnectionData *tdata = new TCPConnectionData();
                                        tdata->result = result;
                                        tdata->callback = callback;
                                        data.user_data = tdata;

                                        player->get_sync_status(sigc::mem_fun(*this, &TCPConnection::get_sync_status_cb), data);

                                        return;
                                }
                                else if (request["2"] == "options" && request["3"] == "sync" && request["4"] == "list?")
                                {
                                        AudioPlayerData data;
                                        TCPConnectionData *tdata = new TCPConnectionData();
                                        tdata->result = result;
                                        tdata->callback = callback;
                                        data.user_data = tdata;

                                        player->getSynchronizeList(sigc::mem_fun(*this, &TCPConnection::get_sync_list_cb), data);

                                        return;
                                }
                                else if (request["2"] == "options" && request["3"] == "sync" && request["4"] == "off")
                                {
                                        player->Synchronize("", false);

                                        result.Add("4", "ok");
                                }
                                else if (request["2"] == "options" && request["3"] == "sync")
                                {
                                        vector<string> seq;
                                        split(request["4"], seq, ":", 2);

                                        if (seq[0] == "id")
                                        {
                                                player->Synchronize(seq[1], true);
                                        }

                                        result.Add("4", "ok");
                                }
                                else if (request["2"] == "database")
                                { //Music library related stuff

                                        if (request["3"] == "capabilities?")
                                        {
                                                Params p = player->getDatabaseCapabilities();

                                                for (int i = 0;i < p.size();i++)
                                                {
                                                        string key, value;
                                                        p.get_item(i, key, value);
                                                        result.Add(Utils::to_string(i + 3), key + ":" + value);
                                                }
                                        }
                                        else if (request["3"] == "stats?")
                                        {
                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getStats(sigc::mem_fun(*this, &TCPConnection::getdb_stats_cb), data);

                                                return;
                                        }
                                        else if (request["3"] == "albums" &&
                                                 Utils::is_of_type<int>(request["4"]) &&
                                                 Utils::is_of_type<int>(request["5"]))
                                        {
                                                int from, nb;
                                                Utils::from_string(request["4"], from);
                                                Utils::from_string(request["5"], nb);

                                                for (int i = 4;i < request.size();i++)
                                                        result.Add(Utils::to_string(i), "");

                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getAlbums(sigc::mem_fun(*this, &TCPConnection::getdb_default_cb), from, nb, data);

                                                return;
                                        }
                                        else if (request["3"] == "album_titles" &&
                                                 Utils::is_of_type<int>(request["4"]) &&
                                                 Utils::is_of_type<int>(request["5"]))
                                        {
                                                int from, nb;
                                                Utils::from_string(request["4"], from);
                                                Utils::from_string(request["5"], nb);

                                                vector<string> tk;
                                                split(request["6"], tk, ":", 2);
                                                if (tk.size() != 2 || tk[0] != "album_id")
                                                {
                                                        ProcessDone_signal sig;
                                                        sig.connect(callback);
                                                        sig.emit(result);

                                                        return;
                                                }

                                                for (int i = 4;i < request.size();i++)
                                                        result.Add(Utils::to_string(i), "");

                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getAlbumsTitles(sigc::mem_fun(*this, &TCPConnection::getdb_default_cb), from, nb, tk[1], data);

                                                return;
                                        }
                                        else if (request["3"] == "artists" &&
                                                 Utils::is_of_type<int>(request["4"]) &&
                                                 Utils::is_of_type<int>(request["5"]))
                                        {
                                                int from, nb;
                                                Utils::from_string(request["4"], from);
                                                Utils::from_string(request["5"], nb);

                                                for (int i = 4;i < request.size();i++)
                                                        result.Add(Utils::to_string(i), "");

                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getArtists(sigc::mem_fun(*this, &TCPConnection::getdb_default_cb), from, nb, data);

                                                return;
                                        }
                                        else if (request["3"] == "artist_albums" &&
                                                 Utils::is_of_type<int>(request["4"]) &&
                                                 Utils::is_of_type<int>(request["5"]))
                                        {
                                                int from, nb;
                                                Utils::from_string(request["4"], from);
                                                Utils::from_string(request["5"], nb);

                                                vector<string> tk;
                                                split(request["6"], tk, ":", 2);
                                                if (tk.size() != 2 || tk[0] != "artist_id")
                                                {
                                                        ProcessDone_signal sig;
                                                        sig.connect(callback);
                                                        sig.emit(result);

                                                        return;
                                                }

                                                for (int i = 4;i < request.size();i++)
                                                        result.Add(Utils::to_string(i), "");

                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getArtistsAlbums(sigc::mem_fun(*this, &TCPConnection::getdb_default_cb), from, nb, tk[1], data);

                                                return;
                                        }
                                        else if (request["3"] == "genres" &&
                                                 Utils::is_of_type<int>(request["4"]) &&
                                                 Utils::is_of_type<int>(request["5"]))
                                        {
                                                int from, nb;
                                                Utils::from_string(request["4"], from);
                                                Utils::from_string(request["5"], nb);

                                                for (int i = 4;i < request.size();i++)
                                                        result.Add(Utils::to_string(i), "");

                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getGenres(sigc::mem_fun(*this, &TCPConnection::getdb_default_cb), from, nb, data);

                                                return;
                                        }
                                        else if (request["3"] == "genre_artists" &&
                                                 Utils::is_of_type<int>(request["4"]) &&
                                                 Utils::is_of_type<int>(request["5"]))
                                        {
                                                int from, nb;
                                                Utils::from_string(request["4"], from);
                                                Utils::from_string(request["5"], nb);

                                                vector<string> tk;
                                                split(request["6"], tk, ":", 2);
                                                if (tk.size() != 2 || tk[0] != "genre_id")
                                                {
                                                        ProcessDone_signal sig;
                                                        sig.connect(callback);
                                                        sig.emit(result);

                                                        return;
                                                }

                                                for (int i = 4;i < request.size();i++)
                                                        result.Add(Utils::to_string(i), "");

                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getGenresArtists(sigc::mem_fun(*this, &TCPConnection::getdb_default_cb), from, nb, tk[1], data);

                                                return;
                                        }
                                        else if (request["3"] == "years" &&
                                                 Utils::is_of_type<int>(request["4"]) &&
                                                 Utils::is_of_type<int>(request["5"]))
                                        {
                                                int from, nb;
                                                Utils::from_string(request["4"], from);
                                                Utils::from_string(request["5"], nb);

                                                for (int i = 4;i < request.size();i++)
                                                        result.Add(Utils::to_string(i), "");

                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getYears(sigc::mem_fun(*this, &TCPConnection::getdb_years_cb), from, nb, data);

                                                return;
                                        }
                                        else if (request["3"] == "year_albums" &&
                                                 Utils::is_of_type<int>(request["4"]) &&
                                                 Utils::is_of_type<int>(request["5"]))
                                        {
                                                int from, nb;
                                                Utils::from_string(request["4"], from);
                                                Utils::from_string(request["5"], nb);

                                                vector<string> tk;
                                                split(request["6"], tk, ":", 2);
                                                if (tk.size() != 2 || tk[0] != "year")
                                                {
                                                        ProcessDone_signal sig;
                                                        sig.connect(callback);
                                                        sig.emit(result);

                                                        return;
                                                }

                                                for (int i = 4;i < request.size();i++)
                                                        result.Add(Utils::to_string(i), "");

                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getYearsAlbums(sigc::mem_fun(*this, &TCPConnection::getdb_default_cb), from, nb, tk[1], data);

                                                return;
                                        }
                                        else if (request["3"] == "playlists" &&
                                                 Utils::is_of_type<int>(request["4"]) &&
                                                 Utils::is_of_type<int>(request["5"]))
                                        {
                                                int from, nb;
                                                Utils::from_string(request["4"], from);
                                                Utils::from_string(request["5"], nb);

                                                for (int i = 4;i < request.size();i++)
                                                        result.Add(Utils::to_string(i), "");

                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getPlaylists(sigc::mem_fun(*this, &TCPConnection::getdb_default_cb), from, nb, data);

                                                return;
                                        }
                                        else if (request["3"] == "playlist_titles" &&
                                                 Utils::is_of_type<int>(request["4"]) &&
                                                 Utils::is_of_type<int>(request["5"]))
                                        {
                                                int from, nb;
                                                Utils::from_string(request["4"], from);
                                                Utils::from_string(request["5"], nb);

                                                vector<string> tk;
                                                split(request["6"], tk, ":", 2);
                                                if (tk.size() != 2 || tk[0] != "playlist_id")
                                                {
                                                        ProcessDone_signal sig;
                                                        sig.connect(callback);
                                                        sig.emit(result);

                                                        return;
                                                }

                                                for (int i = 4;i < request.size();i++)
                                                        result.Add(Utils::to_string(i), "");

                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getPlaylistsTracks(sigc::mem_fun(*this, &TCPConnection::getdb_default_cb), from, nb, tk[1], data);

                                                return;
                                        }
                                        else if (request["3"] == "playlist" &&
                                                 request["4"] == "delete")
                                        {
                                                vector<string> tk;
                                                split(request["5"], tk, ":", 2);
                                                if (tk.size() != 2 || tk[0] != "playlist_id")
                                                {
                                                        ProcessDone_signal sig;
                                                        sig.connect(callback);
                                                        sig.emit(result);

                                                        return;
                                                }

                                                for (int i = 5;i < request.size();i++)
                                                        result.Add(Utils::to_string(i), "");

                                                result.Add("5", "ok");

                                                player->playlist_delete(tk[1]);
                                        }
                                        else if (request["3"] == "radios" &&
                                                 Utils::is_of_type<int>(request["4"]) &&
                                                 Utils::is_of_type<int>(request["5"]))
                                        {
                                                int from, nb;
                                                Utils::from_string(request["4"], from);
                                                Utils::from_string(request["5"], nb);

                                                for (int i = 4;i < request.size();i++)
                                                        result.Add(Utils::to_string(i), "");

                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getRadios(sigc::mem_fun(*this, &TCPConnection::getdb_default_cb), from, nb, data);

                                                return;
                                        }
                                        else if (request["3"] == "radio_items" &&
                                                 Utils::is_of_type<int>(request["4"]) &&
                                                 Utils::is_of_type<int>(request["5"]))
                                        {
                                                int from, nb;
                                                Utils::from_string(request["4"], from);
                                                Utils::from_string(request["5"], nb);
                                                string radio_id, item_id, search;

                                                for (int i = 6;i < request.size();i++)
                                                {
                                                        vector<string> tk;
                                                        split(request[Utils::to_string(i)], tk, ":", 2);
                                                        if (tk.size() != 2) continue;

                                                        if (tk[0] == "radio_id") radio_id = tk[1];
                                                        if (tk[0] == "item_id") item_id = tk[1];
                                                        if (tk[0] == "search") search = tk[1];
                                                }

                                                for (int i = 4;i < request.size();i++)
                                                        result.Add(Utils::to_string(i), "");

                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getRadiosItems(sigc::mem_fun(*this, &TCPConnection::getdb_default_cb), from, nb, radio_id, item_id, search, data);

                                                return;
                                        }
                                        else if (request["3"] == "randoms" &&
                                                 Utils::is_of_type<int>(request["4"]) &&
                                                 Utils::is_of_type<int>(request["5"]))
                                        {
                                                int from, nb;
                                                Utils::from_string(request["4"], from);
                                                Utils::from_string(request["5"], nb);

                                                for (int i = 4;i < request.size();i++)
                                                        result.Add(Utils::to_string(i), "");

                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getRandoms(sigc::mem_fun(*this, &TCPConnection::getdb_default_cb), from, nb, data);

                                                return;
                                        }
                                        else if (request["3"] == "music_folder" &&
                                                 Utils::is_of_type<int>(request["4"]) &&
                                                 Utils::is_of_type<int>(request["5"]))
                                        {
                                                int from, nb;
                                                Utils::from_string(request["4"], from);
                                                Utils::from_string(request["5"], nb);
                                                string folder_id;

                                                for (int i = 6;i < request.size();i++)
                                                {
                                                        vector<string> tk;
                                                        split(request[Utils::to_string(i)], tk, ":", 2);
                                                        if (tk.size() != 2) continue;

                                                        if (tk[0] == "folder_id") folder_id = tk[1];
                                                }

                                                for (int i = 4;i < request.size();i++)
                                                        result.Add(Utils::to_string(i), "");

                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getMusicFolder(sigc::mem_fun(*this, &TCPConnection::getdb_default_cb), from, nb, folder_id, data);

                                                return;
                                        }
                                        else if (request["3"] == "search" &&
                                                 Utils::is_of_type<int>(request["4"]) &&
                                                 Utils::is_of_type<int>(request["5"]))
                                        {
                                                int from, nb;
                                                Utils::from_string(request["4"], from);
                                                Utils::from_string(request["5"], nb);

                                                vector<string> tk;
                                                split(request["6"], tk, ":", 2);
                                                if (tk.size() != 2 || tk[0] != "search_terms")
                                                {
                                                        ProcessDone_signal sig;
                                                        sig.connect(callback);
                                                        sig.emit(result);

                                                        return;
                                                }

                                                for (int i = 4;i < request.size();i++)
                                                        result.Add(Utils::to_string(i), "");

                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getSearch(sigc::mem_fun(*this, &TCPConnection::getdb_default_cb), from, nb, tk[1], data);

                                                return;
                                        }
                                        else if (request["3"] == "track_infos")
                                        {
                                                vector<string> tk;
                                                split(request["4"], tk, ":", 2);
                                                if (tk.size() != 2 || tk[0] != "track_id")
                                                {
                                                        ProcessDone_signal sig;
                                                        sig.connect(callback);
                                                        sig.emit(result);

                                                        return;
                                                }

                                                AudioPlayerData data;
                                                TCPConnectionData *tdata = new TCPConnectionData();
                                                tdata->result = result;
                                                tdata->callback = callback;
                                                data.user_data = tdata;

                                                player->get_database()->getTrackInfos(sigc::mem_fun(*this, &TCPConnection::getdb_default_param_cb), tk[1], data);

                                                return;
                                        }
                                }
                        }
                }
        }

        ProcessDone_signal sig;
        sig.connect(callback);
        sig.emit(result);
}

void TCPConnection::get_volume_cb(AudioPlayerData data)
{
        TCPConnectionData *tdata = reinterpret_cast<TCPConnectionData *>(data.user_data);
        Params &result = tdata->result;

        result.Add("2", "volume:" + Utils::to_string(data.ivalue));

        ProcessDone_signal sig;
        sig.connect(tdata->callback);
        sig.emit(result);
}

void TCPConnection::get_songinfo_cb(AudioPlayerData data)
{
        TCPConnectionData *tdata = reinterpret_cast<TCPConnectionData *>(data.user_data);
        Params &result = tdata->result;

        Params &infos = data.params;
        for (int i = 0;i < infos.size();i++)
        {
                string key, value;
                infos.get_item(i, key, value);
                result.Add(Utils::to_string(i + 2), key + ":" + value);
        }

        ProcessDone_signal sig;
        sig.connect(tdata->callback);
        sig.emit(result);
}

void TCPConnection::get_album_cover_cb(AudioPlayerData data)
{
        TCPConnectionData *tdata = reinterpret_cast<TCPConnectionData *>(data.user_data);
        Params &result = tdata->result;

        result.Add("2", "cover:" + data.svalue);

        ProcessDone_signal sig;
        sig.connect(tdata->callback);
        sig.emit(result);
}

void TCPConnection::get_album_cover_id_cb(AudioPlayerData data)
{
        TCPConnectionData *tdata = reinterpret_cast<TCPConnectionData *>(data.user_data);
        Params &result = tdata->result;

        result.Add("3", data.svalue);

        ProcessDone_signal sig;
        sig.connect(tdata->callback);
        sig.emit(result);
}

void TCPConnection::get_current_time_cb(AudioPlayerData data)
{
        TCPConnectionData *tdata = reinterpret_cast<TCPConnectionData *>(data.user_data);
        Params &result = tdata->result;

        result.Add("2", "time:" + Utils::to_string(data.dvalue));

        ProcessDone_signal sig;
        sig.connect(tdata->callback);
        sig.emit(result);
}

void TCPConnection::get_status_cb(AudioPlayerData data)
{
        TCPConnectionData *tdata = reinterpret_cast<TCPConnectionData *>(data.user_data);
        Params &result = tdata->result;

        string status;
        switch (data.ivalue)
        {
          case PLAY: status = "playing"; break;
          case PAUSE: status = "pause"; break;
          case STOP: status = "stop"; break;
          case ERROR: status = "error"; break;
          case SONG_CHANGE: status = "song_change"; break;
        }
        result.Add("2", "status:" + status);

        ProcessDone_signal sig;
        sig.connect(tdata->callback);
        sig.emit(result);
}

void TCPConnection::get_playlist_size_cb(AudioPlayerData data)
{
        TCPConnectionData *tdata = reinterpret_cast<TCPConnectionData *>(data.user_data);
        Params &result = tdata->result;

        result.Add("3", Utils::to_string(data.ivalue));

        ProcessDone_signal sig;
        sig.connect(tdata->callback);
        sig.emit(result);
}

void TCPConnection::get_playlist_current_cb(AudioPlayerData data)
{
        TCPConnectionData *tdata = reinterpret_cast<TCPConnectionData *>(data.user_data);
        Params &result = tdata->result;

        result.Add("3", Utils::to_string(data.ivalue));

        ProcessDone_signal sig;
        sig.connect(tdata->callback);
        sig.emit(result);
}

void TCPConnection::get_playlist_item_cb(AudioPlayerData data)
{
        TCPConnectionData *tdata = reinterpret_cast<TCPConnectionData *>(data.user_data);
        Params &result = tdata->result;

        Params &infos = data.params;
        for (int i = 0;i < infos.size();i++)
        {
                string key, value;
                infos.get_item(i, key, value);
                result.Add(Utils::to_string(i + 5), key + ":" + value);
        }

        ProcessDone_signal sig;
        sig.connect(tdata->callback);
        sig.emit(result);
}

void TCPConnection::get_playlist_cover_cb(AudioPlayerData data)
{
        TCPConnectionData *tdata = reinterpret_cast<TCPConnectionData *>(data.user_data);
        Params &result = tdata->result;

        result.Add("4", "cover:" + Utils::to_string(data.svalue));

        ProcessDone_signal sig;
        sig.connect(tdata->callback);
        sig.emit(result);
}

void TCPConnection::get_sync_status_cb(AudioPlayerData data)
{
        TCPConnectionData *tdata = reinterpret_cast<TCPConnectionData *>(data.user_data);
        Params &result = tdata->result;

        vector<Params> &vp = data.vparams;
        result.Add("3", "sync");

        int j = 4;
        for (uint i = 0;i < vp.size();i++)
        {
                Params &pp = vp[i];
                if (pp.size() == 2)
                {
                        result.Add(Utils::to_string(j), "id:" + pp["id"]);
                        result.Add(Utils::to_string(j + 1), "name:" + pp["name"]);
                        j += 2;
                }
        }

        ProcessDone_signal sig;
        sig.connect(tdata->callback);
        sig.emit(result);
}

void TCPConnection::get_sync_list_cb(AudioPlayerData data)
{
        TCPConnectionData *tdata = reinterpret_cast<TCPConnectionData *>(data.user_data);
        Params &result = tdata->result;

        vector<Params> p = data.vparams;

        int j = 4;
        for (uint i = 0;i < p.size();i++)
        {
                Params &pp = p[i];
                result.Add(Utils::to_string(j), "id:" + pp["id"]);
                result.Add(Utils::to_string(j + 1), "name:" + pp["name"]);
                j += 2;
        }

        ProcessDone_signal sig;
        sig.connect(tdata->callback);
        sig.emit(result);
}

void TCPConnection::getdb_stats_cb(AudioPlayerData data)
{
        TCPConnectionData *tdata = reinterpret_cast<TCPConnectionData *>(data.user_data);
        Params &result = tdata->result;

        Params &p = data.params;

        for (int i = 0;i < p.size();i++)
        {
                string key, value;
                p.get_item(i, key, value);
                result.Add(Utils::to_string(i + 3), key + ":" + value);
        }

        ProcessDone_signal sig;
        sig.connect(tdata->callback);
        sig.emit(result);
}

void TCPConnection::getdb_default_cb(AudioPlayerData data)
{
        TCPConnectionData *tdata = reinterpret_cast<TCPConnectionData *>(data.user_data);
        Params &result = tdata->result;

        vector<Params> &vp = data.vparams;

        int cpt = 4;

        //add count first
        for (uint i = 0;i < vp.size();i++)
        {
                if (vp[i].Exists("count"))
                {
                        result.Add(Utils::to_string(cpt), "count:" + vp[i].get_param("count"));
                        cpt++;
                }
        }

        for (uint i = 0;i < vp.size();i++)
        {
                //separator
                if (vp[i].get_param("id") == "") continue;
                result.Add(Utils::to_string(cpt), "id:" + vp[i].get_param("id"));
                cpt++;

                for (int j = 0;j < vp[i].size();j++)
                {
                        string key, value;
                        vp[i].get_item(j, key, value);

                        if (key == "id") continue; //separator
                        result.Add(Utils::to_string(cpt), key + ":" + value);

                        cpt++;
                }
        }

        ProcessDone_signal sig;
        sig.connect(tdata->callback);
        sig.emit(result);
}

void TCPConnection::getdb_default_param_cb(AudioPlayerData data)
{
        TCPConnectionData *tdata = reinterpret_cast<TCPConnectionData *>(data.user_data);
        Params &result = tdata->result;

        Params &p = data.params;

        for (int i = 0;i < p.size();i++)
        {
                string key, value;
                p.get_item(i, key, value);
                result.Add(Utils::to_string(i + 5), key + ":" + value);
        }

        ProcessDone_signal sig;
        sig.connect(tdata->callback);
        sig.emit(result);
}

void TCPConnection::getdb_years_cb(AudioPlayerData data)
{
        TCPConnectionData *tdata = reinterpret_cast<TCPConnectionData *>(data.user_data);
        Params &result = tdata->result;

        vector<Params> &vp = data.vparams;

        int cpt = 4;

        //add count first
        for (uint i = 0;i < vp.size();i++)
        {
                if (vp[i].Exists("count"))
                {
                        result.Add(Utils::to_string(cpt), "count:" + vp[i].get_param("count"));
                        cpt++;
                }
        }

        for (uint i = 0;i < vp.size();i++)
        {
                //separator
                if (vp[i].get_param("year") == "") continue;
                result.Add(Utils::to_string(cpt), "year:" + vp[i].get_param("year"));
                cpt++;
        }

        ProcessDone_signal sig;
        sig.connect(tdata->callback);
        sig.emit(result);
}
