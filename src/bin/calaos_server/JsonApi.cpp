/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#include "JsonApi.h"
#include "HttpClient.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "AutoScenario.h"
#include "CalaosConfig.h"
#include "HistLogger.h"

#include <openssl/evp.h>
#include <openssl/crypto.h>

map<string, LoginThrottle::Entry> LoginThrottle::entries;

void LoginThrottle::purge(double now)
{
    for (auto it = entries.begin();it != entries.end();)
    {
        if (now - it->second.lastSeen > EntryTimeout)
            it = entries.erase(it);
        else
            ++it;
    }
}

bool LoginThrottle::isBlocked(const string &ip, double now)
{
    auto it = entries.find(ip);
    if (it == entries.end())
        return false;

    if (now - it->second.lastSeen > EntryTimeout)
    {
        entries.erase(it);
        return false;
    }

    return now < it->second.blockedUntil;
}

void LoginThrottle::registerFailure(const string &ip, double now)
{
    purge(now);

    auto it = entries.find(ip);
    if (it == entries.end())
    {
        if ((int)entries.size() >= MaxEntries)
        {
            //Table is full, forget the address not seen for the longest time
            auto oldest = entries.begin();
            for (auto e = entries.begin();e != entries.end();++e)
            {
                if (e->second.lastSeen < oldest->second.lastSeen)
                    oldest = e;
            }
            entries.erase(oldest);
        }

        it = entries.insert({ ip, Entry() }).first;
    }

    Entry &entry = it->second;
    if (entry.failures < 1000)
        entry.failures++;

    double delay = BaseDelay;
    for (int i = 1;i < entry.failures && delay < MaxDelay;i++)
        delay *= 2.0;
    if (delay > MaxDelay)
        delay = MaxDelay;

    entry.lastSeen = now;
    entry.blockedUntil = now + delay;
}

void LoginThrottle::registerSuccess(const string &ip)
{
    entries.erase(ip);
}

int LoginThrottle::trackedCount()
{
    return (int)entries.size();
}

void LoginThrottle::clear()
{
    entries.clear();
}

bool JsonApi::secureCompare(const string &expected, const string &received)
{
    unsigned char de[EVP_MAX_MD_SIZE], dr[EVP_MAX_MD_SIZE];
    unsigned int lene = 0, lenr = 0;

    if (EVP_Digest(expected.data(), expected.size(), de, &lene, EVP_sha256(), nullptr) != 1 ||
        EVP_Digest(received.data(), received.size(), dr, &lenr, EVP_sha256(), nullptr) != 1 ||
        lene != lenr)
        return false;

    return CRYPTO_memcmp(de, dr, lene) == 0;
}

bool JsonApi::checkCredentials(const string &user, const string &pass)
{
    string confUser = Utils::get_config_option("calaos_user");
    string confPass = Utils::get_config_option("calaos_password");

    if (Utils::get_config_option("cn_user") != "" &&
        Utils::get_config_option("cn_pass") != "")
    {
        confUser = Utils::get_config_option("cn_user");
        confPass = Utils::get_config_option("cn_pass");
    }

    //Both comparisons are always done, a wrong user must not answer faster
    bool userOk = secureCompare(confUser, user);
    bool passOk = secureCompare(confPass, pass);

    return userOk && passOk;
}

bool JsonApi::isValidIntParam(const string &value, int minValue, int maxValue)
{
    if (value.empty() || value.size() > 11)
        return false;

    string::size_type i = 0;
    if (value[0] == '-')
    {
        if (value.size() == 1)
            return false;
        i = 1;
    }

    for (;i < value.size();i++)
    {
        if (!isdigit((unsigned char)value[i]))
            return false;
    }

    long v = 0;
    try
    {
        v = std::stol(value);
    }
    catch (...)
    {
        return false;
    }

    return v >= minValue && v <= maxValue;
}

bool JsonApi::resolveEventPicture(const string &picUid, string &outPath)
{
    outPath.clear();

    if (picUid.empty())
        return false;

    return FileUtils::resolveSafePath(Utils::getCacheFile("push_pictures"),
                                      picUid + ".jpg",
                                      outPath);
}

string JsonApi::dumpJsonRedacted(json_t *jroot)
{
    static const vector<string> sensitive =
    { "cn_pass", "password", "passwd", "pass", "token", "old_pw", "new_pw",
      "old_password", "new_password", "secret", "authorization" };

    if (!jroot)
        return string();

    json_t *copy = json_deep_copy(jroot);
    if (!copy)
        return string();

    std::function<void(json_t *)> redact = [&](json_t *j)
    {
        if (json_is_array(j))
        {
            uint idx;
            json_t *value;
            json_array_foreach(j, idx, value)
                redact(value);
            return;
        }

        if (!json_is_object(j))
            return;

        vector<string> keys;
        const char *key;
        json_t *value;
        json_object_foreach(j, key, value)
        {
            if (std::find(sensitive.begin(), sensitive.end(), Utils::str_to_lower(key)) != sensitive.end())
                keys.push_back(key);
            else
                redact(value);
        }

        for (const string &k: keys)
            json_object_set_new(j, k.c_str(), json_string("***"));
    };

    redact(copy);

    char *d = json_dumps(copy, JSON_INDENT(4));
    json_decref(copy);

    if (!d)
        return string();

    string ret(d);
    free(d);

    return ret;
}

JsonApi::JsonApi(HttpClient *client):
    httpClient(client)
{
}

JsonApi::JsonApi()
{
}

JsonApi::~JsonApi()
{
}

void JsonApi::buildJsonIO(IOBase *io, json_t *jio)
{
    vector<string> params =
    { "id", "name", "type", "hits", "var_type", "visible",
      "chauffage_id", "rw", "unit", "gui_type", "state",
      "auto_scenario", "step", "io_type", "io_style",
      "value_warning" };

    for (string &param: params)
    {
        string value;

        if (param == "state")
        {
            if (io->get_type() == TINT)
                value = Utils::to_string(io->get_value_double());
            else if (io->get_type() == TBOOL)
                value = io->get_value_bool()?"true":"false";
            else if (io->get_type() == TSTRING)
                value = io->get_value_string();
        }
        else if (param == "var_type")
        {
            if (io->get_type() == TINT) value = "float";
            else if (io->get_type() == TBOOL) value = "bool";
            else if (io->get_type() == TSTRING) value = "string";
        }
        else
        {
            if (!io->get_params().Exists(param))
                continue;
            value = io->get_param(param);
        }

        json_object_set_new(jio, param.c_str(),
                            json_string(value.c_str()));
    }

    auto jstatus = buildJsonStatusInfo(io);
    if (jstatus)
        json_object_set_new(jio, "status_info", jstatus);
}

json_t *JsonApi::buildJsonRoomIO(Room *room)
{
    json_t *jdata = json_array();

    for (int i = 0;i < room->get_size();i++)
    {
        json_t *jio = json_object();
        IOBase *io = room->get_io(i);

        buildJsonIO(io, jio);

        json_array_append_new(jdata, jio);
    }

    return jdata;
}

json_t *JsonApi::buildJsonHome()
{
    json_t *jdata = json_array();

    for (int iroom = 0;iroom < ListeRoom::Instance().size();iroom++)
    {
        Room *room = ListeRoom::Instance().get_room(iroom);
        json_t *jroom = json_object();

        json_t *jitems = buildJsonRoomIO(room);

        json_object_set_new(jroom, "type", json_string(room->get_type().c_str()));
        json_object_set_new(jroom, "name", json_string(room->get_name().c_str()));
        json_object_set_new(jroom, "hits", json_string(Utils::to_string(room->get_hits()).c_str()));
        json_object_set_new(jroom, "items", jitems);

        json_array_append_new(jdata, jroom);
    }

    return jdata;
}

json_t *JsonApi::buildFlatIOList()
{
    json_t *jdata = json_array();

    for (int iroom = 0;iroom < ListeRoom::Instance().size();iroom++)
    {
        Room *room = ListeRoom::Instance().get_room(iroom);
        for (int i = 0;i < room->get_size();i++)
        {
            json_t *jio = json_object();
            IOBase *io = room->get_io(i);

            buildJsonIO(io, jio);

            json_array_append_new(jdata, jio);
        }
    }

    return jdata;
}

json_t *JsonApi::buildJsonCameras()
{
    json_t *jdata = json_array();

    list<IOBase *> camlist = ListeRoom::Instance().getCameraList();

    int cpt = 0;
    for (IOBase *io: camlist)
    {
        IPCam *camera = dynamic_cast<IPCam *>(io);
        if (!camera) continue;

        json_t *jcam = json_object();
        json_object_set_new(jcam, "id", json_string(camera->get_param("id").c_str()));
        json_object_set_new(jcam, "name", json_string(camera->get_param("name").c_str()));
        json_object_set_new(jcam, "type", json_string(camera->get_param("type").c_str()));
        Params caps = camera->getCapabilities();
        if (caps["ptz"] == "true")
            json_object_set_new(jcam, "ptz", json_string("true"));
        else
            json_object_set_new(jcam, "ptz", json_string("false"));

        cpt++;

        json_array_append_new(jdata, jcam);
    }

    return jdata;
}

json_t *JsonApi::buildJsonAudio()
{
    json_t *jdata = json_array();

    list<IOBase *> audiolist = ListeRoom::Instance().getAudioList();

    for (IOBase *io: audiolist)
    {
        AudioPlayer *player = dynamic_cast<AudioPlayer *>(io);
        if (!player) continue;

        json_t *jaudio = json_object();
        json_object_set_new(jaudio, "id", json_string(player->get_param("id").c_str()));
        json_object_set_new(jaudio, "name", json_string(player->get_param("name").c_str()));
        json_object_set_new(jaudio, "type", json_string(player->get_param("type").c_str()));

        json_object_set_new(jaudio, "playlist", json_string(player->canPlaylist()?"true":"false"));
        json_object_set_new(jaudio, "database", json_string(player->canDatabase()?"true":"false"));

        if (player->get_params().Exists("amp"))
            json_object_set_new(jaudio, "avr", json_string(player->get_param("amp").c_str()));

        json_array_append_new(jdata, jaudio);

        //don't query detailed player infos here, other informations need to be queried to the squeezecenter
        //so the get_home request will be delayed by all the squeezecenter's requests.
        //To be faster, only return the basic infos here, and call get_state for each players to get detailed infos
    }

    return jdata;
}

void JsonApi::buildJsonState(vector<string> iolist, std::function<void(json_t *)> result_lambda)
{
    //The audio-player part below is asynchronous (squeezebox answers come
    //back later on the loop). Every json container is therefore owned by a
    //shared_ptr with json_decref as deleter, captured by value in the
    //callbacks: whatever happens to the chain (client disconnected, player
    //deleted, callback never invoked), the json is released exactly once.
    //Ownership of the result is handed to result_lambda with an extra
    //reference (json_incref), the caller keeps its usual json_decref.
    std::shared_ptr<json_t> sjio(json_object(), json_decref);
    list<AudioPlayer *> audioplayers;

    for (string ioid: iolist)
    {
        IOBase *io = ListeRoom::Instance().get_io(ioid);
        if (!io) continue;

        if (io->get_param("gui_type") != "audio_player")
        {
            if (io->get_type() == TBOOL)
                json_object_set_new(sjio.get(), ioid.c_str(), json_string(io->get_value_bool()?"true":"false"));
            else if (io->get_type() == TINT)
                json_object_set_new(sjio.get(), ioid.c_str(), json_string(Utils::to_string(io->get_value_double()).c_str()));
            else if (io->get_type() == TSTRING)
                json_object_set_new(sjio.get(), ioid.c_str(), json_string(io->get_value_string().c_str()));
        }
        else
        {
            AudioPlayer *p = dynamic_cast<AudioPlayer *>(io);
            if (p) audioplayers.push_back(p);
        }
    }

    string uuid = Utils::createRandomUuid();
    playerCounts[uuid] = 0;

    //Destruction guard: this JsonApi dies with its client connection while
    //squeezebox answers may still be in flight. Every async callback checks
    //the token before touching this/playerCounts or calling result_lambda
    //(which captures raw handler pointers). Same pattern as
    //JsonApiHandlerHttp::handlerAlive.
    std::weak_ptr<bool> alive = apiAlive;

    //One player chain is done (or aborted): decrement the pending count and
    //send the answer when it was the last one. Only call with alive checked.
    auto finishOne = [this, uuid, result_lambda, sjio]()
    {
        playerCounts[uuid] = playerCounts[uuid] - 1;

        if (playerCounts[uuid] <= 0)
        {
            playerCounts.erase(uuid);
            result_lambda(json_incref(sjio.get()));
        }
    };

    for (AudioPlayer *player: audioplayers)
    {
        playerCounts[uuid] = playerCounts[uuid] + 1;

        //The player IO can be deleted through the API while a request is in
        //flight: never keep the raw pointer across an async boundary, look
        //it up again by id at each step instead.
        const string playerId = player->get_param("id");
        auto playerById = [playerId]() -> AudioPlayer *
        {
            return dynamic_cast<AudioPlayer *>(ListeRoom::Instance().get_io(playerId));
        };

        std::shared_ptr<json_t> sjplayer(json_object(), json_decref);

        player->get_playlist_current([=](AudioPlayerData data1)
        {
            if (alive.expired()) return;

            json_object_set_new(sjplayer.get(),
                                "playlist_current_track",
                                json_string(Utils::to_string(data1.ivalue).c_str()));

            AudioPlayer *p1 = playerById();
            if (!p1) { finishOne(); return; }

            p1->get_volume([=](AudioPlayerData data2)
            {
                if (alive.expired()) return;

                json_object_set_new(sjplayer.get(),
                                    "volume",
                                    json_string(Utils::to_string(data2.ivalue).c_str()));

                AudioPlayer *p2 = playerById();
                if (!p2) { finishOne(); return; }

                p2->get_playlist_size([=](AudioPlayerData data3)
                {
                    if (alive.expired()) return;

                    json_object_set_new(sjplayer.get(),
                                        "playlist_size",
                                        json_string(Utils::to_string(data3.ivalue).c_str()));

                    AudioPlayer *p3 = playerById();
                    if (!p3) { finishOne(); return; }

                    p3->get_current_time([=](AudioPlayerData data4)
                    {
                        if (alive.expired()) return;

                        json_object_set_new(sjplayer.get(),
                                            "time_elapsed",
                                            json_string(Utils::to_string(data4.dvalue).c_str()));

                        AudioPlayer *p4 = playerById();
                        if (!p4) { finishOne(); return; }

                        p4->get_status([=](AudioPlayerData data5)
                        {
                            if (alive.expired()) return;

                            string status;
                            switch (data5.ivalue)
                            {
                            case AudioPlay: status = "playing"; break;
                            case AudioPause: status = "pause"; break;
                            case AudioStop: status = "stop"; break;
                            default:
                            case AudioError: status = "error"; break;
                            case AudioSongChange: status = "song_change"; break;
                            }

                            json_object_set_new(sjplayer.get(),
                                                "status",
                                                json_string(status.c_str()));

                            AudioPlayer *p5 = playerById();
                            if (!p5) { finishOne(); return; }

                            std::shared_ptr<json_t> sjtrack(json_object(), json_decref);
                            p5->get_songinfo([=](AudioPlayerData data6)
                            {
                                if (alive.expired()) return;

                                Params &infos = data6.params;
                                for (int i = 0;i < infos.size();i++)
                                {
                                    string inf_key, inf_value;
                                    infos.get_item(i, inf_key, inf_value);

                                    json_object_set_new(sjtrack.get(),
                                                        inf_key.c_str(),
                                                        json_string(inf_value.c_str()));
                                }

                                //json_object_set (not _new): the shared_ptr
                                //keeps its own reference and releases it, the
                                //container ends up with exactly one.
                                json_object_set(sjplayer.get(),
                                                "current_track",
                                                sjtrack.get());

                                //Add player to array, and send data back if all players requests are done.
                                json_object_set(sjio.get(), playerId.c_str(), sjplayer.get());
                                finishOne();
                            });
                        });
                    });
                });
            });
        });
    }

    //only send data if there is not audio players
    if (playerCounts[uuid] == 0)
    {
        playerCounts.erase(uuid);
        result_lambda(json_incref(sjio.get()));
    }
}

void JsonApi::buildJsonStates(const Params &jParam, std::function<void (json_t *)> result_lambda)
{
    Params res;

    if (jParam.Exists("id"))
    {
        IOBase *o = ListeRoom::Instance().get_io(jParam["id"]);
        if (!o)
        {
            Params p = {{ "error", "wrong id" }};
            result_lambda(p.toJson());
            return;
        }

        switch (o->get_type())
        {
        case TINT:
        {
            for (auto it: o->get_all_values_double())
                res.Add(it.first, Utils::to_string(it.second));
            break;
        }
        case TBOOL:
        {
            for (auto it: o->get_all_values_bool())
                res.Add(it.first, (it.second)?"true":"false");
            break;
        }
        case TSTRING:
        {
            for (auto it: o->get_all_values_string())
                res.Add(it.first, it.second);
            break;
        }
        default: break;
        }
    }

    result_lambda(res.toJson());
}

void JsonApi::buildQuery(const Params &jParam, std::function<void (json_t *)> result_lambda)
{
    Params res;

    if (jParam.Exists("id"))
    {
        IOBase *o = ListeRoom::Instance().get_io(jParam["input_id"]);
        if (!o)
        {
            Params p = {{ "error", "wrong id" }};
            result_lambda(p.toJson());
            return;
        }

        map<string, string> m = o->query_param(jParam["param"]);
        for (auto it: m)
            res.Add(it.first, it.second);
    }

    result_lambda(res.toJson());
}

json_t *JsonApi::buildJsonGetParam(const Params &jParam)
{
    bool success = true;
    Params ret;

    IOBase *o = ListeRoom::Instance().get_io(jParam["id"]);
    if (!o)
        success = false;
    else
        ret.Add(jParam["param"], o->get_param(jParam["param"]));

    if (!success)
        ret = {{ "error", "wrong io/param" }};

    return ret.toJson();
}

json_t *JsonApi::buildJsonSetParam(const Params &jParam)
{
    bool success = true;
    Params ret;

    IOBase *o = ListeRoom::Instance().get_io(jParam["id"]);
    if (!o)
        success = false;
    else
    {
        if (jParam["param"].empty() || jParam["value"].empty())
            success = false;
        else
        {
            o->set_param(jParam["param"], jParam["value"]);

            EventManager::create(CalaosEvent::EventIOChanged,
            { { "id", o->get_param("id") },
              { jParam["param"], jParam["value"] } });
        }
    }

    if (!success)
        ret = {{ "error", "wrong io/param" }};
    else
        ret = {{ "success", "true" }};

    return ret.toJson();
}

json_t *JsonApi::buildJsonDelParam(const Params &jParam)
{
    bool success = true;
    Params ret;

    IOBase *o = ListeRoom::Instance().get_io(jParam["id"]);
    if (!o)
        success = false;
    else
    {
        if (jParam["param"].empty())
            success = false;
        else
        {
            o->get_params().Delete(jParam["param"]);

            EventManager::create(CalaosEvent::EventIOPropertyDelete,
            { { "id", o->get_param("id") },
              { "param", jParam["param"] } });
        }
    }

    if (!success)
        ret = {{ "error", "wrong io/param" }};
    else
        ret = {{ "success", "true" }};

    return ret.toJson();
}

json_t *JsonApi::buildJsonGetIO(vector<string> iolist)
{
    json_t *jret = json_object();

    for (string ioid: iolist)
    {
        IOBase *io = ListeRoom::Instance().get_io(ioid);
        if (io)
        {
            json_t *jio = json_object();
            buildJsonIO(io, jio);

            json_object_set_new(jret, ioid.c_str(), jio);
        }
    }

    return jret;
}

bool JsonApi::decodeSetState(Params &jParam)
{
    bool success = true;

    IOBase *io = ListeRoom::Instance().get_io(jParam["id"]);
    if (!io)
        success = false;
    else
    {
        success = false;

        if (io->get_type() == TBOOL)
        {
            if (jParam["value"] == "true") success = io->set_value(true);
            else if (jParam["value"] == "false") success = io->set_value(false);
            else success = io->set_value(jParam["value"]);
        }
        else
            success = io->set_value(jParam["value"]);
    }

    return success;
}

//The one answer get_playlist gives when it cannot produce a playlist: the
//requested IO is not (or no longer) an audio player. Shared by the entry point
//and by the async stages, so a player deleted mid-flight gives the client the
//SAME answer as an unknown id - never a playlist silently missing its tail.
static json_t *playlistNoPlayerAnswer()
{
    json_t *jret = json_object();
    json_object_set_new(jret, "success", json_string("false"));
    return jret;
}

void JsonApi::decodeGetPlaylist(Params &jParam, std::function<void(json_t *)>result_lambda)
{
    const string playerId = jParam["id"];
    IOBase *io = ListeRoom::Instance().get_io(playerId);
    AudioPlayer *player = dynamic_cast<AudioPlayer *>(io);

    if (!player)
    {
        result_lambda(playlistNoPlayerAnswer());
        return;
    }

    /* Destruction guard: this JsonApi dies with its client connection
     * (HttpClient::~HttpClient() deletes the handler, base sub-object
     * included) while player answers may still be in flight. Every async
     * callback of the chain checks the token before touching this or calling
     * result_lambda, which captures raw handler pointers. Same pattern as
     * buildJsonState() (T2.15) and JsonApiHandlerHttp::handlerAlive.
     */
    std::weak_ptr<bool> alive = apiAlive;

    json_t *jplayer = json_object();

    player->get_playlist_current([=](AudioPlayerData data)
    {
        //jplayer is only reachable from this chain: releasing it here is what
        //keeps the guard from leaking the partial answer.
        if (alive.expired()) { json_decref(jplayer); return; }

        json_object_set_new(jplayer,
                            "current_track",
                            json_string(Utils::to_string(data.ivalue).c_str()));

        //The player IO can be deleted through the API while a request is in
        //flight: never keep the raw pointer across an async boundary, look it
        //up again by id at each step instead.
        AudioPlayer *p1 = dynamic_cast<AudioPlayer *>(ListeRoom::Instance().get_io(playerId));
        if (!p1)
        {
            json_decref(jplayer);
            result_lambda(playlistNoPlayerAnswer());
            return;
        }

        p1->get_playlist_size([=](AudioPlayerData data1)
        {
            if (alive.expired()) { json_decref(jplayer); return; }

            json_object_set_new(jplayer,
                                "count",
                                json_string(Utils::to_string(data1.ivalue).c_str()));

            int it_count = data1.ivalue;
            if (it_count <= 0)
            {
                json_object_set_new(jplayer, "items", json_array());
                result_lambda(jplayer);
            }
            else
                //getNextPlaylistItem() looks the player up itself
                getNextPlaylistItem(playerId, jplayer, json_array(), 0, it_count, result_lambda);
        });
    });
}

void JsonApi::getNextPlaylistItem(const string &playerId, json_t *jplayer, json_t *jplaylist, int it_current, int it_count, std::function<void(json_t *)>result_lambda)
{
    //Entered either from decodeGetPlaylist() or from the recursion below, in
    //both cases right after an alive check, so this is safe to touch.
    AudioPlayer *player = dynamic_cast<AudioPlayer *>(ListeRoom::Instance().get_io(playerId));
    if (!player)
    {
        //The IO went away between two items. The client is still there and
        //must get an answer, but not a truncated playlist.
        json_decref(jplayer);
        json_decref(jplaylist);
        result_lambda(playlistNoPlayerAnswer());
        return;
    }

    //One check per stage: a chain of N callbacks needs N checks, not one at
    //the top. Here N is the length of the playlist.
    std::weak_ptr<bool> alive = apiAlive;

    player->get_playlist_item(it_current, [=](AudioPlayerData data)
    {
        if (alive.expired())
        {
            //jplaylist is not attached to jplayer yet: release both.
            json_decref(jplayer);
            json_decref(jplaylist);
            return;
        }

        json_t *jtrack = json_object();
        Params &infos = data.params;
        for (int i = 0;i < infos.size();i++)
        {
            string inf_key, inf_value;
            infos.get_item(i, inf_key, inf_value);

            json_object_set_new(jtrack,
                                inf_key.c_str(),
                                json_string(inf_value.c_str()));
        }

        json_array_append_new(jplaylist, jtrack);

        int idx = it_current + 1;
        if (idx >= it_count)
        {
            //all track are queried, send back data
            json_object_set_new(jplayer,
                                "items",
                                jplaylist);
            result_lambda(jplayer);
        }
        else
        {
            getNextPlaylistItem(playerId, jplayer, jplaylist, idx, it_count, result_lambda);
        }
    });
}

AudioPlayer *JsonApi::getAudioPlayer(json_t *jdata, string &err)
{
    AudioPlayer *player = nullptr;
    err.clear();

    string id = jansson_string_get(jdata, "id");
    if (id == "")
    {
        err = "empty player id";
        return player;
    }

    IOBase *io = ListeRoom::Instance().get_io(id);
    player = dynamic_cast<AudioPlayer *>(io);

    if (!player)
        err = "unkown player_id";

    return player;
}

void JsonApi::audioGetDbStats(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    /* Destruction guard, shared by the five single-shot player-state methods
     * below. This JsonApi dies with its client connection
     * (HttpClient::~HttpClient() deletes the handler, base sub-object included,
     * HttpClient.cpp:162) while a player answer is still in flight, and
     * result_lambda is the handler's own lambda capturing raw handler pointers.
     * Same pattern as buildJsonState() (T2.15) and the playlist chain (T3.17a).
     *
     * One check is enough here, unlike the playlist chain: these are single
     * shot, one round trip and one answer, no recursion. And nothing is
     * allocated before the check, so the guarded branch has nothing to release
     * - the json objects of these methods are all built after it, from the
     * answer the player just gave.
     */
    std::weak_ptr<bool> alive = apiAlive;

    player->get_database()->getStats([=](AudioPlayerData adata)
    {
        if (alive.expired()) return;

        adata.params.Add("audio_action", "get_stats");
        result_lambda(adata.params.toJson());
    });
}

void JsonApi::audioGetPlaylistSize(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    std::weak_ptr<bool> alive = apiAlive;

    player->get_playlist_size([=](AudioPlayerData adata)
    {
        if (alive.expired()) return;

        adata.params.Add("audio_action", "get_playlist_size");
        Params p = {{"playlist_size", Utils::to_string(adata.ivalue)}};
        result_lambda(p.toJson());
    });
}

void JsonApi::audioGetTime(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    std::weak_ptr<bool> alive = apiAlive;

    player->get_current_time([=](AudioPlayerData adata)
    {
        if (alive.expired()) return;

        adata.params.Add("audio_action", "get_time");
        Params p = {{"time_elapsed", Utils::to_string(adata.dvalue)}};
        result_lambda(p.toJson());
    });
}

void JsonApi::audioGetPlaylistItem(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string it = jansson_string_get(jdata, "item");
    if (it.empty() || !Utils::is_of_type<int>(it))
    {
        Params p = {{"error", "wrong item" }};
        result_lambda(p.toJson());
        return;
    }

    int item;
    Utils::from_string(it, item);

    std::weak_ptr<bool> alive = apiAlive;

    player->get_playlist_item(item, [=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        result_lambda(data.params.toJson());
    });
}

void JsonApi::audioGetCoverInfo(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    std::weak_ptr<bool> alive = apiAlive;

    player->get_album_cover([=](AudioPlayerData data)
    {
        if (alive.expired()) return;

        Params p = {{ "cover", data.svalue }};
        result_lambda(p.toJson());
    });
}

json_t *JsonApi::processDbResult(const AudioPlayerData &data)
{
    json_t *ret = json_object();
    json_t *aret = json_array();
    string scount;

    const vector<Params> &vp = data.vparams;
    for (const Params &p: vp)
    {
        if (p.Exists("count"))
            scount = p["count"];
        json_array_append_new(aret, p.toJson());
    }

    if (scount == "0")
        json_array_clear(aret);

    if (!scount.empty())
        json_object_set_new(ret, "total_count", json_string(scount.c_str()));
    json_object_set_new(ret, "items", aret);

    return ret;
}

void JsonApi::audioDbGetAlbums(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getAlbums([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetAlbumArtistItem(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    string artist_id = jansson_string_get(jdata, "artist_id");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getArtistsAlbums([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count, artist_id);
}

void JsonApi::audioDbGetYearAlbums(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    string year = jansson_string_get(jdata, "year");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getYearsAlbums([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count, year);
}

void JsonApi::audioDbGetGenreArtists(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    string genre = jansson_string_get(jdata, "genre");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getGenresArtists([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count, genre);
}

void JsonApi::audioDbGetAlbumTitles(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    string album_id = jansson_string_get(jdata, "album_id");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getAlbumsTitles([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count, album_id);
}

void JsonApi::audioDbGetPlaylistTitles(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    string pl_id = jansson_string_get(jdata, "playlist_id");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getPlaylistsTracks([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count, pl_id);
}

void JsonApi::audioDbGetArtists(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getArtists([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetYears(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getYears([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetGenres(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getGenres([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetPlaylists(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getPlaylists([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetMusicFolder(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    string folder_id = jansson_string_get(jdata, "folder_id");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getMusicFolder([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count, folder_id);
}

void JsonApi::audioDbGetSearch(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    string search = jansson_string_get(jdata, "search");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getSearch([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count, search);
}

void JsonApi::audioDbGetRadios(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    player->get_database()->getRadios([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count);
}

void JsonApi::audioDbGetRadioItems(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string itfrom = jansson_string_get(jdata, "from");
    string itcount = jansson_string_get(jdata, "count");
    if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) ||
        itcount.empty() || !Utils::is_of_type<int>(itcount))
    {
        Params p = {{"error", "wrong from/count" }};
        result_lambda(p.toJson());
        return;
    }

    int from, count;
    Utils::from_string(itfrom, from);
    Utils::from_string(itcount, count);

    string radio_id = jansson_string_get(jdata, "radio_id");
    string item_id = jansson_string_get(jdata, "item_id");
    string search = jansson_string_get(jdata, "search");

    player->get_database()->getRadiosItems([=](AudioPlayerData data)
    {
        result_lambda(processDbResult(data));
    }, from, count, radio_id, item_id, search);
}

void JsonApi::audioDbGetTrackInfos(json_t *jdata, std::function<void(json_t *)>result_lambda)
{
    string err;
    AudioPlayer *player = getAudioPlayer(jdata, err);

    if (!err.empty())
    {
        Params p = {{"error", err }};
        result_lambda(p.toJson());
        return;
    }

    string trackid = jansson_string_get(jdata, "track_id");

    player->get_database()->getTrackInfos([=](AudioPlayerData data)
    {
        result_lambda(data.params.toJson());
    }, trackid);
}

json_t *JsonApi::buildJsonGetTimerange(const Params &jParam)
{
    InPlageHoraire *o = dynamic_cast<InPlageHoraire *>(ListeRoom::Instance().get_io(jParam["id"]));
    if (!o)
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    json_t *ret = json_object();
    json_t *jarr = json_array();

    for (int day = 0;day < 7;day++)
    {
        vector<TimeRange> h;
        if (day == 0) h = o->getMonday();
        if (day == 1) h = o->getTuesday();
        if (day == 2) h = o->getWednesday();
        if (day == 3) h = o->getThursday();
        if (day == 4) h = o->getFriday();
        if (day == 5) h = o->getSaturday();
        if (day == 6) h = o->getSunday();
        for (uint i = 0;i < h.size();i++)
            json_array_append_new(jarr, h[i].toParams(day).toJson());
    }

    json_object_set_new(ret, "ranges", jarr);

    stringstream ssmonth;
    ssmonth << o->months;
    string str = ssmonth.str();
    std::reverse(str.begin(), str.end());
    json_object_set_new(ret, "months", json_string(str.c_str()));

    return ret;
}

json_t *JsonApi::buildJsonSetTimerange(json_t *jdata)
{
    string id = jansson_string_get(jdata, "id");
    InPlageHoraire *o = dynamic_cast<InPlageHoraire *>(ListeRoom::Instance().get_io(id));
    if (!o)
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    o->clear();

    size_t idx;
    json_t *value;

    json_array_foreach(json_object_get(jdata, "ranges"), idx, value)
    {
        Params p;
        jansson_decode_object(value, p);

        TimeRange tr(p);

        cout << "Adding timerange: " << p.toString() << endl;

        if (p["day"] == "1") o->AddMonday(tr);
        if (p["day"] == "2") o->AddTuesday(tr);
        if (p["day"] == "3") o->AddWednesday(tr);
        if (p["day"] == "4") o->AddThursday(tr);
        if (p["day"] == "5") o->AddFriday(tr);
        if (p["day"] == "6") o->AddSaturday(tr);
        if (p["day"] == "7") o->AddSunday(tr);
    }

    //set months
    string m = jansson_string_get(jdata, "months");
    if (!m.empty())
    {
        //reverse to have a left to right months representation
        std::reverse(m.begin(), m.end());

        try
        {
            bitset<12> mset(m);
            o->months = mset;
        }
        catch(...)
        {
            cErrorDom("network") << "wrong parameters for months: " << m;
        }
    }

    EventManager::create(CalaosEvent::EventTimeRangeChanged,
                         { { "id", o->get_param("id") } });

    if (o->getAutoScenarioPtr())
    {
        EventManager::create(CalaosEvent::EventScenarioChanged,
                             { { "id", o->getAutoScenarioPtr()->getIOScenario()->get_param("id") } });
    }

    //Resave config
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "success", "true" }};
    return p.toJson();
}

json_t *JsonApi::buildAutoscenarioList(json_t *jdata)
{
    VAR_UNUSED(jdata);
    json_t *jret = json_object();
    json_t *jarr = json_array();

    for (auto it: ListeRoom::Instance().getAutoScenarios())
    {
        json_array_append_new(jarr, it->toJson());
    }

    json_object_set_new(jret, "scenarios", jarr);
    return jret;
}

json_t *JsonApi::buildAutoscenarioGet(json_t *jdata)
{
    string id = jansson_string_get(jdata, "id");
    Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_io(id));
    if (!sc || !sc->getAutoScenario())
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    return sc->toJson();
}

json_t *JsonApi::buildAutoscenarioCreate(json_t *jdata)
{
    Params params;
    params.Add("auto_scenario", Calaos::get_new_scenario_id());
    params.Add("name", jansson_string_get(jdata, "name", _("New unnamed scenario")));
    params.Add("visible", jansson_string_get(jdata, "visible", "false"));
    params.Add("cycle", jansson_string_get(jdata, "cycle", "false"));
    params.Add("disabled", jansson_string_get(jdata, "disabled", "true"));

    Room *room = ListeRoom::Instance().searchRoomByNameAndType(
                     jansson_string_get(jdata, "room_name"),
                     jansson_string_get(jdata, "room_type"));
    if (!room)
    {
        cWarningDom("network") << "Wrong room: " << jansson_string_get(jdata, "room_name")
                               << " - " << jansson_string_get(jdata, "room_type");
        room = ListeRoom::Instance().get_room(0);
    }

    //create scenario object
    params.Add("type", "scenario");
    IOBase *in = ListeRoom::Instance().createIO(params, room);
    Scenario *scenario = dynamic_cast<Scenario *>(in);
    if (!scenario || !scenario->getAutoScenario())
    {
        //createIO() returns null on an IO factory miss, and the dynamic_cast
        //rejects anything else than a Scenario: answer an error instead of
        //crashing on the null pointer
        cErrorDom("network") << "Scenario creation failed: "
                             << (in? "created IO is not a scenario": "IO factory returned null");
        if (in)
            ListeRoom::Instance().deleteIO(in);

        Params perr = {{ "error", "scenario creation failed" }};
        return perr.toJson();
    }

    if (!scenario->getAutoScenario()->checkScenarioRules())
    {
        //The internal IOs of the scenario could not be created: roll the
        //half-created scenario back and answer an error
        cErrorDom("network") << "Scenario creation failed: unable to create the scenario rules";
        scenario->getAutoScenario()->deleteAll();
        ListeRoom::Instance().deleteIO(scenario);

        Params perr = {{ "error", "scenario creation failed" }};
        return perr.toJson();
    }

    size_t idx;
    json_t *value;

    json_array_foreach(json_object_get(jdata, "steps"), idx, value)
    {
        int index_act;

        if (jansson_string_get(value, "step_type") == "standard")
        {
            double pause;
            from_string(jansson_string_get(value, "step_pause"), pause);
            scenario->getAutoScenario()->addStep(pause);
            index_act = idx;
        }
        else
        {
            index_act = AutoScenario::END_STEP;
        }

        size_t idx_act;
        json_t *value_act;

        json_array_foreach(json_object_get(value, "actions"), idx_act, value_act)
        {

            string id_out = jansson_string_get(value_act, "id");
            IOBase *out = ListeRoom::Instance().get_io(id_out);
            if (out)
                scenario->getAutoScenario()->addStepAction(index_act, out,
                                                           jansson_string_get(value_act, "action"));
        }
    }

    EventManager::create(CalaosEvent::EventScenarioAdded,
                         { { "id", scenario->get_param("id") } });

    //Resave config, auto scenarios have probably created/deleted ios and rules
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "id", scenario->get_param("id") }};
    return p.toJson();
}

json_t *JsonApi::buildAutoscenarioDelete(json_t *jdata)
{
    string id = jansson_string_get(jdata, "id");
    Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_io(id));
    if (!sc || !sc->getAutoScenario())
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    sc->getAutoScenario()->deleteAll();

    //delete the scenario IO
    ListeRoom::Instance().deleteIO(sc);

    EventManager::create(CalaosEvent::EventScenarioDeleted,
                         { { "id", id } });

    //Resave config
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "success", "true" }};
    return p.toJson();
}

json_t *JsonApi::buildAutoscenarioModify(json_t *jdata)
{
    string id = jansson_string_get(jdata, "id");
    Scenario *scenario = dynamic_cast<Scenario *>(ListeRoom::Instance().get_io(id));
    if (!scenario || !scenario->getAutoScenario())
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    scenario->getAutoScenario()->deleteRules();

    Params params;
    params.Add("auto_scenario", Calaos::get_new_scenario_id());
    params.Add("name", jansson_string_get(jdata, "name", _("New unnamed scenario")));
    params.Add("visible", jansson_string_get(jdata, "visible", "false"));
    params.Add("cycle", jansson_string_get(jdata, "cycle", "false"));
    params.Add("disabled", jansson_string_get(jdata, "disabled", "true"));

    Room *room = ListeRoom::Instance().searchRoomByNameAndType(
                     jansson_string_get(jdata, "room_name"),
                     jansson_string_get(jdata, "room_type"));

    size_t idx;
    json_t *value;

    json_array_foreach(json_object_get(jdata, "steps"), idx, value)
    {
        int index_act;

        if (jansson_string_get(value, "step_type") == "standard")
        {
            double pause;
            from_string(jansson_string_get(value, "step_pause"), pause);
            scenario->getAutoScenario()->addStep(pause);
            index_act = idx;
        }
        else
        {
            index_act = AutoScenario::END_STEP;
        }

        size_t idx_act;
        json_t *value_act;

        json_array_foreach(json_object_get(value, "actions"), idx_act, value_act)
        {

            string id_out = jansson_string_get(value_act, "id");
            IOBase *out = ListeRoom::Instance().get_io(id_out);
            cDebugDom("network") << "scenario: " << scenario << " index_act: " << index_act << " out: " << out << " action: " << jansson_string_get(value_act, "action");
            if (out)
                scenario->getAutoScenario()->addStepAction(index_act, out,
                                                           jansson_string_get(value_act, "action"));
        }
    }

    //Check for changes
    if (params["name"] != scenario->get_param("name"))
    {
        scenario->set_param("name", params["name"]);

        EventManager::create(CalaosEvent::EventIOChanged,
                             { { "id", scenario->get_param("id") },
                               { "name", params["name"] }});
    }

    if (params["visible"] != scenario->get_param("visible"))
    {
        scenario->set_param("visible", params["visible"]);

        EventManager::create(CalaosEvent::EventIOChanged,
                             { { "id", scenario->get_param("id") },
                               { "visible", params["visible"] }});
    }

    Room *old_room = ListeRoom::Instance().getRoomByIO(scenario);
    if (room != old_room)
    {
        if (room)
        {
            old_room->RemoveIOFromRoom(scenario);
            room->AddIO(scenario);

            EventManager::create(CalaosEvent::EventRoomChanged,
                                 { { "io_id_added", scenario->get_param("id") },
                                   { "room_name", room->get_name() },
                                   { "room_type", room->get_type() }});
        }
    }

    if (params["cycle"] != scenario->get_param("cycle"))
    {
        if (params["cycle"] == "true")
            scenario->getAutoScenario()->setCycling(true);
        else
            scenario->getAutoScenario()->setCycling(false);
    }

    if (params["disabled"] != scenario->get_param("disabled"))
    {
        if (params["disabled"] == "true")
            scenario->getAutoScenario()->setDisabled(true);
        else
            scenario->getAutoScenario()->setDisabled(false);
    }

    if (!scenario->getAutoScenario()->checkScenarioRules())
    {
        //One of the internal scenario IOs disappeared or was replaced: the
        //rules cannot be rebuilt, answer an error instead of crashing
        cErrorDom("network") << "Scenario modification failed: unable to rebuild the scenario rules";
        Params perr = {{ "error", "scenario modification failed" }};
        return perr.toJson();
    }

    EventManager::create(CalaosEvent::EventScenarioChanged,
                         { { "id", scenario->get_param("id") } });

    //Resave config
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "success", "true" }};
    return p.toJson();
}

json_t *JsonApi::buildAutoscenarioAddSchedule(json_t *jdata)
{
    string id = jansson_string_get(jdata, "id");
    Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_io(id));
    if (!sc || !sc->getAutoScenario())
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    sc->getAutoScenario()->addSchedule();

    EventManager::create(CalaosEvent::EventScenarioChanged,
                         { { "id", sc->get_param("id") } });

    //Resave config
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "id", sc->getAutoScenario()->getIOTimeRange()->get_param("id") }};
    return p.toJson();
}

json_t *JsonApi::buildAutoscenarioDelSchedule(json_t *jdata)
{
    string id = jansson_string_get(jdata, "id");
    Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_io(id));
    if (!sc || !sc->getAutoScenario())
    {
        Params p = {{ "error", "wrong input" }};
        return p.toJson();
    }

    sc->getAutoScenario()->deleteSchedule();

    EventManager::create(CalaosEvent::EventScenarioChanged,
                         { { "id", sc->get_param("id") } });

    //Resave config
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    Params p = {{ "success", "true" }};
    return p.toJson();
}

void JsonApi::buildJsonEventLog(const Params &jParam, std::function<void(Json &)> callback)
{
    int page = 0;
    int perPage = 100;

    Utils::from_string(jParam["page"], page);
    Utils::from_string(jParam["per_page"], perPage);

    if (jParam["uuid"] != "")
    {
        //Only load 1 event
        HistLogger::Instance().getEvent(jParam["uuid"],
                [=](bool success, string errorMsg, const HistEvent &event)
        {
            if (!success)
            {
                Json err = {{ "error", errorMsg }};
                callback(err);
                return;
            }

            Json j = event.toJson();
            callback(j);
        });

        return;
    }

    HistLogger::Instance().getEvents(page, perPage,
            [=](bool success, string errorMsg, const vector<HistEvent> &events, int total_page, int total_count)
    {
        Json jevents = Json::array();

        if (!success)
        {
            Json err = {{ "error", errorMsg }};
            callback(err);
            return;
        }

        for (const auto &e: events)
            jevents.emplace_back(e.toJson());

        Json jroot = {
            { "total_page", total_page },
            { "total_count", total_count },
            { "page", page },
            { "per_page", perPage },
            { "events", jevents }
        };

        callback(jroot);
    });
}

bool JsonApi::registerPushToken(const Params &jParam)
{
    if (jParam["token"] == "")
        return false;
    if (jParam["hardware"] != "android" &&
        jParam["hardware"] != "ios")
        return false;

    HistLogger::Instance().registerPushToken(jParam["token"],
            jParam["hardware"] == "ios"? HistLogger::PUSH_HW_IOS: HistLogger::PUSH_HW_ANDROID);

    return true;
}

bool JsonApi::changeCredentials(string olduser, string oldpass, string newuser, string newpass)
{
    if (!checkCredentials(olduser, oldpass))
        return false; //wrong old user/pass

    //change user/pass
    Utils::set_config_option("cn_user", newuser);
    Utils::set_config_option("cn_pass", newpass);
    //remove old entries if needed
    Utils::del_config_option("calaos_user");
    Utils::del_config_option("calaos_password");
    sync();

    return true;
}

json_t *JsonApi::buildJsonStatusInfo(IOBase *io)
{
    if (!io || !io->hasStatusInfo()) return nullptr;

    json_t *jret = json_object();

    Params p = io->getStatusInfo();

    for (int i = 0;i < p.size();i++)
    {
        string key, value;
        p.get_item(i, key, value);

        json_object_set_new(jret, key.c_str(), json_string(value.c_str()));
    }

    return jret;
}
