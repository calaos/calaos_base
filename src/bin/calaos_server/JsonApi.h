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
#ifndef JSONAPI_H
#define JSONAPI_H

#include "Calaos.h"
#include <jansson.h>
#include "Jansson_Addition.h"
#include "Room.h"
#include "AudioPlayer.h"

using namespace Calaos;

class HttpClient;

/*
 * Login brute force protection, per source address.
 *
 * Every failed login grows a backoff window for the source address (1s, 2s,
 * 4s... capped at 60s). While that window is open, logins from the address are
 * refused without even looking at the credentials. There is no hard lockout: a
 * legitimate user is only slowed down, and the window is cleared as soon as one
 * login succeeds.
 *
 * Nothing here ever sleeps. calaos_server runs everything on a single libuv
 * loop, so delaying an answer by blocking would stall the whole server. Only
 * timestamps are compared, the caller passes the current loop time.
 *
 * The table is bounded and quiet entries expire, so an attacker spoofing source
 * addresses cannot grow it indefinitely.
 */
class LoginThrottle
{
public:
    //Delay of the first backoff window, doubled at each subsequent failure
    static constexpr double BaseDelay = 1.0;
    //Longest backoff window
    static constexpr double MaxDelay = 60.0;
    //An address not seen for that long is forgotten
    static constexpr double EntryTimeout = 900.0;
    //Highest number of tracked addresses, oldest one is evicted above that
    static constexpr int MaxEntries = 1024;

    //True when ip is inside its backoff window and must not be allowed to login
    static bool isBlocked(const string &ip, double now);

    static void registerFailure(const string &ip, double now);
    static void registerSuccess(const string &ip);

    //Number of tracked addresses, and full reset. Both for the tests.
    static int trackedCount();
    static void clear();

private:
    struct Entry
    {
        int failures = 0;
        double blockedUntil = 0.0;
        double lastSeen = 0.0;
    };

    static map<string, Entry> entries;

    static void purge(double now);
};

class JsonApi: public sigc::trackable
{
public:
    JsonApi(HttpClient *client);
    JsonApi();
    virtual ~JsonApi();

    virtual void processApi(const string &data, const Params &paramsGET) { VAR_UNUSED(data); VAR_UNUSED(paramsGET); }

    sigc::signal<void, const string &> sendData;
    sigc::signal<void, int, const string &> closeConnection;

    /* Security helpers, shared by every transport */

    /* Compares two secrets in constant time. Both are hashed first, so neither
     * the content nor the length of the expected secret leaks through the time
     * taken by the comparison.
     */
    static bool secureCompare(const string &expected, const string &received);

    /* Single implementation of the credential check: reads the configured
     * user/password (cn_user/cn_pass when set, calaos_user/calaos_password
     * otherwise) and compares them with secureCompare().
     */
    static bool checkCredentials(const string &user, const string &pass);

    /* True when value is a decimal integer inside [minValue, maxValue]. Used to
     * validate every parameter given to an external command.
     */
    static bool isValidIntParam(const string &value, int minValue, int maxValue);

    /* Resolves the picture of a push event to a path inside the push_pictures
     * cache directory. False when picUid tries to escape the directory or when
     * the file does not exist, and the caller must then answer a 404.
     */
    static bool resolveEventPicture(const string &picUid, string &outPath);

    /* json dump for the logs, with the value of every credential field replaced
     * by ***. Never log a request before it has gone through this.
     */
    static string dumpJsonRedacted(json_t *jroot);



    /* API calls helpers */

    json_t *buildJsonHome();
    json_t *buildJsonCameras();
    json_t *buildJsonAudio();
    json_t *buildFlatIOList();

    void buildJsonIO(IOBase *io, json_t *jio);
    json_t *buildJsonRoomIO(Room *room);

    //result is given with a call to a lambda because we may need to wait for
    //network queries
    void buildJsonState(vector<string> iolist, std::function<void(json_t *)>result_lambda);
    void buildJsonStates(const Params &jParam, std::function<void(json_t *)>result_lambda);
    void buildQuery(const Params &jParam, std::function<void(json_t *)>result_lambda);

    json_t *buildJsonGetParam(const Params &jParam);
    json_t *buildJsonSetParam(const Params &jParam);
    json_t *buildJsonDelParam(const Params &jParam);

    json_t *buildJsonGetTimerange(const Params &jParam);
    json_t *buildJsonSetTimerange(json_t *jdata);

    json_t *buildAutoscenarioList(json_t *jdata);
    json_t *buildAutoscenarioGet(json_t *jdata);
    json_t *buildAutoscenarioCreate(json_t *jdata);
    json_t *buildAutoscenarioDelete(json_t *jdata);
    json_t *buildAutoscenarioModify(json_t *jdata);
    json_t *buildAutoscenarioAddSchedule(json_t *jdata);
    json_t *buildAutoscenarioDelSchedule(json_t *jdata);
    /* T3.18. The manual re-enable of a scenario disabled because one of the IOs
     * one of its steps uses disappeared. A command of its own and not a
     * set_param on `disabled_missing_io`, because set_param CANNOT REFUSE:
     * re-enabling a still broken scenario would answer success and change
     * nothing, which is exactly the silent no-op this ticket removes. */
    json_t *buildAutoscenarioReenable(json_t *jdata);

    json_t *buildJsonGetIO(vector<string> iolist);

    json_t *buildJsonStatusInfo(IOBase *io);

    void buildJsonEventLog(const Params &jParam, std::function<void(Json &)> callback);
    bool registerPushToken(const Params &jParam);

    bool decodeSetState(Params &jParam);
    void decodeGetPlaylist(Params &jParam, std::function<void(json_t *)>result_lambda);

    AudioPlayer *getAudioPlayer(json_t *jdata, string &err);
    /* T3.19. Answers the refusal and returns true when the player owns no music
     * database. Called by the sixteen audio_db methods IMMEDIATELY BEFORE their
     * get_database() dereference, never at the top of the method - see the
     * comment on the definition in JsonApi.cpp.
     */
    bool audioDbUnavailable(AudioPlayer *player,
                            const std::function<void(json_t *)> &result_lambda);
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


protected:

    /* Internal stage of decodeGetPlaylist()'s recursion, never called from
     * outside JsonApi.cpp. Takes the player by ID, not by pointer: one network
     * round trip happens between two consecutive items, and the IO can be
     * deleted through the API in between, so the player is looked up again at
     * every step - exactly like buildJsonState() does (T2.15).
     */
    void getNextPlaylistItem(const string &playerId, json_t *jplayer, json_t *jplaylist, int it_current, int it_count, std::function<void(json_t *)>result_lambda);

    HttpClient *httpClient = nullptr;

    map<string, int> playerCounts;

    /* Destruction guard for the async audio-player callbacks that outlive
     * their request: they capture a weak_ptr on it and no-op once the JsonApi
     * is gone (client disconnected while a player answer was in flight). It
     * covers every transport, since both handlers derive from JsonApi and die
     * with it. Same pattern as JsonApiHandlerHttp::handlerAlive.
     * Guarded: buildJsonState() (T2.15), the recursive playlist chain
     * decodeGetPlaylist()/getNextPlaylistItem() (T3.17a), the five single-shot
     * player-state methods audioGetDbStats()/audioGetPlaylistSize()
     * /audioGetTime()/audioGetPlaylistItem()/audioGetCoverInfo() (T3.17b), the
     * 15 audioDbGet* music-database methods (T3.17c) and buildJsonEventLog()
     * (T3.17f), whose two callbacks wait on HistLogger's sqlite worker thread.
     * That last one is also the warning to keep: it odr-uses no `this` at all,
     * so it raises no -Wdeprecated implicit-capture warning and every earlier
     * inventory of this file walked past it. What it carries is the
     * std::function of the handler, captured by value. The danger is any freed
     * object REACHABLE from the closure, not just a captured `this`.
     * Every async callback of this file is guarded now, and T3.17e measured
     * that the WS transport needs no token of its own on top of this one.
     *
     * SCOPE OF THIS MEMBER, MEASURED - do not confuse it with its sibling.
     * `apiAlive` is read from JsonApi.cpp and NOWHERE ELSE in the tree: the
     * whole of src/ mentions it in exactly two files, this header (the
     * declaration) and JsonApi.cpp (24 guards, one per asynchronous callback of
     * the 26 std::function methods declared above, the two synchronous ones -
     * buildJsonStates() and buildQuery() - needing none). The five guarded
     * callbacks that live in JsonApiHandlerHttp.cpp (:462, :727, :936, :1057,
     * :1075 - get_cover twice, the camera snapshots, the singleShot re-arm) use
     * the handler's OWN token, JsonApiHandlerHttp::handlerAlive
     * (JsonApiHandlerHttp.h:64), never this one; RemoteUIWebSocketHandler has a
     * third, again its own. Two class levels, two tokens, and a callback must
     * take the one belonging to the object whose members it will touch.
     */
    std::shared_ptr<bool> apiAlive { std::make_shared<bool>(true) };

    bool changeCredentials(string olduser, string oldpass, string newuser, string newpass);
};

#endif // JSONAPI_H

