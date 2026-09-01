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
    static string dumpJsonRedacted(const Json &jroot);



    /* API calls helpers */

    Json buildJsonHome();
    Json buildJsonCameras();
    Json buildJsonAudio();
    Json buildFlatIOList();

    void buildJsonIO(IOBase *io, Json &jio);
    Json buildJsonRoomIO(Room *room);

    /* result is given with a call to a lambda because we may need to wait for
     * network queries.
     *
     * E4.1n: the three of them answer a Json BY VALUE, and that is the whole
     * change of ownership. With json_t* the callee handed over a reference and
     * the caller had to json_decref() it on EVERY path, including the abandon
     * path of an async chain; with a value there is nothing to release. THE
     * LIFE GUARDS ARE NOT PART OF THAT, and must not be removed with the
     * decrefs: apiAlive (:256) protects `this` and the raw handler pointers the
     * result lambda captures, not the document.
     *
     * The copy is real: buildJsonState() captures by copy through six nested
     * lambdas. It is correct, and it is the price of the guarantee. If it ever
     * matters, std::move - but NEVER a captured `Json &`, the lambda outlives
     * its scope.
     */
    void buildJsonState(vector<string> iolist, std::function<void(Json)>result_lambda);
    void buildJsonStates(const Params &jParam, std::function<void(Json)>result_lambda);
    void buildQuery(const Params &jParam, std::function<void(Json)>result_lambda);

    /* E4.1o. The five of them answer a Json BY VALUE, and buildJsonSetTimerange()
     * READS one - the only builder of this chain that consumes client JSON.
     *
     * ⛔ buildJsonSetParam() IS ALSO CALLED OUTSIDE ANY HANDLER, by
     * LuaScript/ScriptExec.cpp. Its result must be READ AS A DOCUMENT there:
     * JSON_USE_IMPLICIT_CONVERSIONS is 1 in this tree, so `if (!answer)`
     * compiles without a warning on a Json and throws type_error.302 at
     * runtime, on the uvw loop, with nothing catching it. The failure of these
     * three builders is reported IN the document, as {"error":"wrong io/param"}.
     *
     * The input parsing itself still belongs to the dispatch (E4.1s); what
     * changed here is only the type buildJsonSetTimerange() traverses.
     */
    Json buildJsonGetParam(const Params &jParam);
    Json buildJsonSetParam(const Params &jParam);
    Json buildJsonDelParam(const Params &jParam);

    Json buildJsonGetTimerange(const Params &jParam);
    Json buildJsonSetTimerange(const Json &jdata);

    /* E4.1r: the nine autoscenario builders answer a Json. MECHANICAL
     * migration (Q1 of E4.1.md, settled 2026-09-01): E4.6d rewrites them
     * whole, nothing here is improved on the way.
     * Scenario::toJson() still answers a json_t* - IO/Scenario.cpp is excluded
     * from E4.1 (decision Q5) - so buildAutoscenarioGet() and
     * buildAutoscenarioList() cross the two libraries through
     * janssonScenarioPayloadBridge() (JsonApi.cpp), which E4.6d deletes.
     */
    Json buildAutoscenarioList(const Json &jdata);
    Json buildAutoscenarioGet(const Json &jdata);
    Json buildAutoscenarioCreate(const Json &jdata);
    Json buildAutoscenarioDelete(const Json &jdata);
    Json buildAutoscenarioModify(const Json &jdata);
    Json buildAutoscenarioAddSchedule(const Json &jdata);
    Json buildAutoscenarioDelSchedule(const Json &jdata);
    /* T3.18. The manual re-enable of a scenario disabled because one of the IOs
     * one of its steps uses disappeared. A command of its own and not a
     * set_param on `disabled_missing_io`, because set_param CANNOT REFUSE:
     * re-enabling a still broken scenario would answer success and change
     * nothing, which is exactly the silent no-op this ticket removes. */
    Json buildAutoscenarioReenable(const Json &jdata);

    Json buildJsonGetIO(vector<string> iolist);

    Json buildJsonStatusInfo(IOBase *io);

    void buildJsonEventLog(const Params &jParam, std::function<void(Json &)> callback);
    bool registerPushToken(const Params &jParam);

    bool decodeSetState(Params &jParam);
    void decodeGetPlaylist(Params &jParam, std::function<void(const Json &)>result_lambda);

    /* E4.1q. THE ONLY READER OF THE REQUEST ID LEFT. E4.1p had a second,
     * transitional json_t* overload because processAudioDb() still dispatched
     * its sixteen callers with one; E4.1q migrated that dispatcher and DELETED
     * the overload, which is that ticket's stated acceptance criterion - the
     * name of this function no longer appears anywhere in src/ next to a
     * jansson type, and there must be no reason to bring one back.
     * audioPlayerById()
     * stays where it is: it is the shared resolution, and it is what guarantees
     * the two halves of the audio family cannot answer two different refusals.
     */
    AudioPlayer *getAudioPlayer(const Json &jdata, string &err);
    /* T3.19. Answers the refusal and returns true when the player owns no music
     * database. Called by the sixteen audio_db methods IMMEDIATELY BEFORE their
     * get_database() dereference, never at the top of the method - see the
     * comment on the definition in JsonApi.cpp.
     */
    bool audioDbUnavailable(AudioPlayer *player,
                            const std::function<void(const Json &)> &result_lambda);
    /* ⛔ audioGetDbStats() is NOT an `audio` action: processAudioDb() is what
     * dispatches it (JsonApiHandlerHttp.cpp, JsonApiHandlerWS.cpp), together
     * with the fourteen audioDbGet*. That is why it belongs to E4.1q and not
     * to the player ticket; the four below are the ones processAudio() reaches.
     */
    void audioGetDbStats(const Json &jdata, std::function<void(const Json &)>result_lambda);
    void audioGetPlaylistSize(const Json &jdata, std::function<void(const Json &)>result_lambda);
    void audioGetTime(const Json &jdata, std::function<void(const Json &)>result_lambda);
    void audioGetPlaylistItem(const Json &jdata, std::function<void(const Json &)>result_lambda);
    void audioGetCoverInfo(const Json &jdata, std::function<void(const Json &)>result_lambda);

    /* E4.1q. The answer shape of FOURTEEN of the fifteen methods below. Its
     * contract is unchanged and deliberately so - the count marker is read AND
     * appended to the items array, a count of "0" clears the array but still
     * emits total_count, and no count at all means NO total_count key rather
     * than a null. What DID change, declared: "total_count" was inserted before
     * "items" and nlohmann sorts, so "items" now comes first on the wire.
     */
    Json processDbResult(const AudioPlayerData &data);
    void audioDbGetAlbums(const Json &jdata, std::function<void(const Json &)>result_lambda);
    void audioDbGetArtists(const Json &jdata, std::function<void(const Json &)>result_lambda);
    void audioDbGetYears(const Json &jdata, std::function<void(const Json &)>result_lambda);
    void audioDbGetGenres(const Json &jdata, std::function<void(const Json &)>result_lambda);
    void audioDbGetPlaylists(const Json &jdata, std::function<void(const Json &)>result_lambda);
    void audioDbGetMusicFolder(const Json &jdata, std::function<void(const Json &)>result_lambda);
    void audioDbGetSearch(const Json &jdata, std::function<void(const Json &)>result_lambda);
    void audioDbGetRadios(const Json &jdata, std::function<void(const Json &)>result_lambda);

    void audioDbGetAlbumArtistItem(const Json &jdata, std::function<void(const Json &)>result_lambda);
    void audioDbGetYearAlbums(const Json &jdata, std::function<void(const Json &)>result_lambda);
    void audioDbGetGenreArtists(const Json &jdata, std::function<void(const Json &)>result_lambda);
    void audioDbGetAlbumTitles(const Json &jdata, std::function<void(const Json &)>result_lambda);
    void audioDbGetPlaylistTitles(const Json &jdata, std::function<void(const Json &)>result_lambda);
    void audioDbGetRadioItems(const Json &jdata, std::function<void(const Json &)>result_lambda);

    void audioDbGetTrackInfos(const Json &jdata, std::function<void(const Json &)>result_lambda);


protected:

    /* Internal stage of decodeGetPlaylist()'s recursion, never called from
     * outside JsonApi.cpp. Takes the player by ID, not by pointer: one network
     * round trip happens between two consecutive items, and the IO can be
     * deleted through the API in between, so the player is looked up again at
     * every step - exactly like buildJsonState() does (T2.15).
     */
    void getNextPlaylistItem(const string &playerId, Json jplayer, Json jplaylist, int it_current, int it_count, std::function<void(const Json &)>result_lambda);

    /* E4.1p. The player resolution shared by the two getAudioPlayer()
     * overloads, so that the reader that is migrated and the one that is not
     * cannot answer two different refusals.
     */
    AudioPlayer *audioPlayerById(const string &id, string &err);

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

