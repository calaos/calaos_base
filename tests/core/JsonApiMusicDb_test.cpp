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

/* T3.17c - the fifteen audioDbGet* music-database methods of JsonApi:
 *
 *      audioDbGetAlbums           (audio_db / get_albums HTTP, get_album WS)
 *      audioDbGetAlbumArtistItem  (audio_db / get_artist_album)
 *      audioDbGetYearAlbums       (audio_db / get_year_albums)
 *      audioDbGetGenreArtists     (audio_db / get_genre_artists)
 *      audioDbGetAlbumTitles      (audio_db / get_album_titles)
 *      audioDbGetPlaylistTitles   (audio_db / get_playlist_titles)
 *      audioDbGetArtists          (audio_db / get_artists)
 *      audioDbGetYears            (audio_db / get_years)
 *      audioDbGetGenres           (audio_db / get_genres)
 *      audioDbGetPlaylists        (audio_db / get_playlists)
 *      audioDbGetMusicFolder      (audio_db / get_music_folder)
 *      audioDbGetSearch           (audio_db / get_search)
 *      audioDbGetRadios           (audio_db / get_radios)
 *      audioDbGetRadioItems       (audio_db / get_radio_items)
 *      audioDbGetTrackInfos       (audio_db / get_track_infos)
 *
 * Thirty call sites: each of the fifteen is dispatched from both transports
 * (JsonApiHandlerHttp::processAudioDb and JsonApiHandlerWS::processAudioDb).
 *
 * WHY THIS FILE EXISTS AT ALL
 * ---------------------------
 * Same reason as T3.17a's JsonApiPlaylist_test.cpp and T3.17b's
 * JsonApiPlayerState_test.cpp: T3.17c adds the apiAlive lifetime guard to these
 * fifteen methods, and the E4.0 characterization series that was supposed to
 * cover part of this surface (E4.0f, "Audio et base musicale") is neither
 * delivered nor started - and by its own plan (E4.0.md:342-350) would have
 * covered only three of the fifteen anyway, the other twelve being deliberately
 * left out because they share processDbResult(). Guarding an async path with no
 * net is exactly how a response silently stops being sent, so the net comes
 * first: all fifteen, both transports.
 *
 * Like T3.17b's five, these are SINGLE SHOT - one request, one round trip, one
 * answer, no recursion. The failure mode a badly placed guard produces here is
 * therefore not a truncated list but a missing answer, which is what the cases
 * below pin: for every method, on both transports, the exact answer document.
 *
 * The cases in the first commit of T3.17c were written and made green BEFORE
 * the guard was added, and are unchanged by it. Any later edit that makes one
 * of them fail is describing a behaviour change, not a test to update.
 *
 * ON THE HARNESS: the E4.0a harness (JsonApiCharacterization.h) drives the two
 * real transports end to end and gives the semantic JSON oracle. Read its
 * header before touching anything here, in particular the "TRAPS" section. Two
 * of the three traps are actively avoided: no HttpTestRequest is kept alive
 * across a pump, and the deferred cases use a WS session (no HttpClient read
 * timer at all).
 *
 * ON THE PLAYER AND ITS DATABASE: the reference house of the harness carries a
 * RoonPlayer, which has no AudioDB at all - and AudioPlayer::database is
 * nullptr by default (AudioPlayer.cpp:28) while none of the fifteen methods
 * null-checks get_database(). A local fake supplying a real AudioDB subclass is
 * therefore mandatory, in two modes - immediate (the answer comes back inside
 * processApi(), which is what the goldens capture) and deferred (the test
 * decides when, or whether, each answer fires, which is what the lifetime cases
 * need).
 *
 * ON THE ANSWER QUEUE: it belongs to the FIXTURE, not to the player. A real
 * squeezebox/roon connection object owns the pending callback and outlives the
 * IO, so a queue living inside the player could not express "the IO was deleted
 * and the answer arrived afterwards" - the case this ticket is about.
 *
 * IO ids and goldens are prefixed t317c_ and used nowhere else: Config's IO
 * state cache is process wide and never cleared (see CalaosCoreFixture.h).
 */

#include "JsonApiCharacterization.h"

#include "AudioPlayer.h"
#include "AudioDB.h"
#include "ListeRoom.h"
#include "Utils.h"

#include <deque>
#include <functional>
#include <string>
#include <vector>

using namespace CalaosTest;
using namespace Calaos;

namespace
{

const char *const PLAYER_ID = "t317c_player";

/* The pending answers of a player, held OUTSIDE the player (see the header
 * note). In immediate mode a getter answers inside the call; in deferred mode
 * it queues the callback together with the answer it would have given.
 * Verbatim from T3.17b's JsonApiPlayerState_test.cpp - same need, same shape.
 */
class AnswerQueue
{
public:
    bool deferred = false;

    void answer(const AudioRequest_cb &cb, const AudioPlayerData &d)
    {
        if (deferred)
            pending.push_back({ cb, d });
        else
            cb(d);
    }

    size_t count() const { return pending.size(); }

    //Deliver the oldest queued answer. False when nothing is queued.
    bool fireNext()
    {
        std::function<void()> next = takeNext();
        if (!next)
            return false;
        next();
        return true;
    }

    //Detach the oldest queued answer, so it can be fired after the player - or
    //the JsonApi - has been deleted.
    std::function<void()> takeNext()
    {
        if (pending.empty())
            return std::function<void()>();
        Pending p = pending.front();
        pending.pop_front();
        return [p]() { p.cb(p.data); };
    }

private:
    struct Pending
    {
        AudioRequest_cb cb;
        AudioPlayerData data;
    };
    std::deque<Pending> pending;
};

//What the database was actually asked for. The goldens alone would not catch
//two identifiers being swapped on their way to the AudioDB, since every
//audioDbGet* answers through the same processDbResult().
struct DbCall
{
    std::string method;
    int from = -1;
    int nb = -1;
    std::string id;      //artist_id / year / genre / album_id / playlist_id /
                         //folder_id / search / radio_id / track_id
    std::string itemId;  //get_radio_items only
    std::string search;  //get_radio_items only
};

/* The music database behind the fake player: the fifteen getters the fifteen
 * audioDbGet* methods reach, each answering a payload of its own so that no two
 * goldens can be confused, and each recording its arguments.
 */
class FakeMusicDb: public AudioDB
{
public:
    //How the answer is shaped, to exercise the three branches of
    //processDbResult() (JsonApi.cpp:1079-1101).
    enum Shape
    {
        Normal,     //a leading "count" marker Params, then two items
        ZeroCount,  //count is "0" - the items array is cleared
        NoCount,    //no count anywhere - no total_count key at all
    };

    FakeMusicDb(Params &p, AnswerQueue &q): AudioDB(p), queue(q) {}

    Shape shape = Normal;
    std::vector<DbCall> calls;

    void getStats(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        //Not a T3.17c method (audioGetDbStats belongs to T3.17b), kept so the
        //fake is a complete database and get_stats does not null-deref here.
        AudioPlayerData d;
        d.params.Add("albums", "3");
        queue.answer(cb, d);
    }

    void getAlbums(AudioRequest_cb cb, int from, int nb, AudioPlayerData = AudioPlayerData()) override
    { list(cb, "getAlbums", from, nb, "", "", "", "album"); }

    void getArtistsAlbums(AudioRequest_cb cb, int from, int nb, string artist_id, AudioPlayerData = AudioPlayerData()) override
    { list(cb, "getArtistsAlbums", from, nb, artist_id, "", "", "artistalbum"); }

    void getYearsAlbums(AudioRequest_cb cb, int from, int nb, string year, AudioPlayerData = AudioPlayerData()) override
    { list(cb, "getYearsAlbums", from, nb, year, "", "", "yearalbum"); }

    void getGenresArtists(AudioRequest_cb cb, int from, int nb, string genre_id, AudioPlayerData = AudioPlayerData()) override
    { list(cb, "getGenresArtists", from, nb, genre_id, "", "", "genreartist"); }

    void getAlbumsTitles(AudioRequest_cb cb, int from, int nb, string album_id, AudioPlayerData = AudioPlayerData()) override
    { list(cb, "getAlbumsTitles", from, nb, album_id, "", "", "albumtitle"); }

    void getPlaylistsTracks(AudioRequest_cb cb, int from, int nb, string playlist_id, AudioPlayerData = AudioPlayerData()) override
    { list(cb, "getPlaylistsTracks", from, nb, playlist_id, "", "", "playlisttrack"); }

    void getArtists(AudioRequest_cb cb, int from, int nb, AudioPlayerData = AudioPlayerData()) override
    { list(cb, "getArtists", from, nb, "", "", "", "artist"); }

    void getYears(AudioRequest_cb cb, int from, int nb, AudioPlayerData = AudioPlayerData()) override
    { list(cb, "getYears", from, nb, "", "", "", "year"); }

    void getGenres(AudioRequest_cb cb, int from, int nb, AudioPlayerData = AudioPlayerData()) override
    { list(cb, "getGenres", from, nb, "", "", "", "genre"); }

    void getPlaylists(AudioRequest_cb cb, int from, int nb, AudioPlayerData = AudioPlayerData()) override
    { list(cb, "getPlaylists", from, nb, "", "", "", "playlist"); }

    void getMusicFolder(AudioRequest_cb cb, int from, int nb, string folder_id = "", AudioPlayerData = AudioPlayerData()) override
    { list(cb, "getMusicFolder", from, nb, folder_id, "", "", "folder"); }

    void getSearch(AudioRequest_cb cb, int from, int nb, string search, AudioPlayerData = AudioPlayerData()) override
    { list(cb, "getSearch", from, nb, search, "", "", "searchhit"); }

    void getRadios(AudioRequest_cb cb, int from, int nb, AudioPlayerData = AudioPlayerData()) override
    { list(cb, "getRadios", from, nb, "", "", "", "radio"); }

    void getRadiosItems(AudioRequest_cb cb, int from, int nb, string radio, string item_id = "",
                        string search = "", AudioPlayerData = AudioPlayerData()) override
    { list(cb, "getRadiosItems", from, nb, radio, item_id, search, "radioitem"); }

    //The odd one out: it answers through `params`, not `vparams`, and
    //audioDbGetTrackInfos hands it to the client without processDbResult().
    void getTrackInfos(AudioRequest_cb cb, string track_id, AudioPlayerData = AudioPlayerData()) override
    {
        DbCall c;
        c.method = "getTrackInfos";
        c.id = track_id;
        calls.push_back(c);

        AudioPlayerData d;
        d.params.Add("track_id", track_id);
        d.params.Add("title", "Fake track");
        d.params.Add("artist", "Fake artist");
        d.params.Add("album", "Fake album");
        d.params.Add("duration", "245");
        queue.answer(cb, d);
    }

private:
    void list(const AudioRequest_cb &cb, const std::string &method, int from, int nb,
              const std::string &id, const std::string &itemId, const std::string &search,
              const std::string &kind)
    {
        DbCall c;
        c.method = method;
        c.from = from;
        c.nb = nb;
        c.id = id;
        c.itemId = itemId;
        c.search = search;
        calls.push_back(c);

        queue.answer(cb, listData(kind));
    }

    /* The shape a real AudioDB answers with, copied from SqueezeboxDB: a first
     * Params carrying nothing but "count", then one Params per row. This is the
     * shape processDbResult() reads total_count from.
     */
    AudioPlayerData listData(const std::string &kind) const
    {
        AudioPlayerData d;

        if (shape != NoCount)
        {
            Params marker;
            marker.Add("count", shape == ZeroCount ? "0" : "2");
            d.vparams.push_back(marker);
        }

        for (int i = 1; i <= 2; i++)
        {
            const std::string idx = Utils::to_string(i);
            Params item;
            item.Add("id", kind + "_" + idx);
            item.Add("name", kind + " number " + idx);
            d.vparams.push_back(item);
        }

        return d;
    }

    AnswerQueue &queue;
};

class FakeMusicPlayer: public AudioPlayer
{
public:
    FakeMusicPlayer(Params &p, AnswerQueue &q):
        AudioPlayer(p),
        db(dbParams, q)
    {
        database = &db;
    }

    FakeMusicDb &fakeDb() { return db; }

    bool canPlaylist() override { return true; }
    bool canDatabase() override { return true; }

private:
    Params dbParams;
    FakeMusicDb db;
};

}

class JsonApiMusicDbTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();

        forgetIOState(PLAYER_ID);

        loadConfig();
    }

    AnswerQueue queue;
    FakeMusicDb *db = nullptr;

    /* Register a fake player in ListeRoom exactly like a config loaded IO:
     * owned by its room (clearCoreState() deletes it) and reachable through
     * ListeRoom::get_io(), which is what getAudioPlayer() looks it up with.
     */
    FakeMusicPlayer *addPlayer()
    {
        Params p;
        p.Add("id", PLAYER_ID);
        p.Add("name", "Fake music player");
        p.Add("type", "FakeMusicPlayer");

        FakeMusicPlayer *player = new FakeMusicPlayer(p, queue);

        firstRoom()->AddIO(player);
        ListeRoom::Instance().addIOHash(player);

        db = &player->fakeDb();
        return player;
    }

    //{"msg":"audio_db","msg_id":"1","data":{"id":..., "audio_action":..., ...}}
    static Json wsRequest(const std::string &action, const std::string &id,
                          Json extra = Json::object())
    {
        Json data = Json{{ "id", id }, { "audio_action", action }};
        for (auto it = extra.begin(); it != extra.end(); ++it)
            data[it.key()] = it.value();

        return Json{
            { "msg", "audio_db" },
            { "msg_id", "1" },
            { "data", data },
        };
    }

    //HTTP puts everything flat in the request body.
    static Json httpRequest(const std::string &action, const std::string &id,
                            Json extra = Json::object())
    {
        Json body = Json{
            { "action", "audio_db" },
            { "audio_action", action },
            { "id", id },
        };
        for (auto it = extra.begin(); it != extra.end(); ++it)
            body[it.key()] = it.value();

        return authenticated(body);
    }

    //The jdata a bare JsonApi method takes, for the lifetime cases that do not
    //go through a transport. E4.1q turned the fifteen methods' parameter from a
    //json_t* into a Json; nothing else about these cases changed, and no
    //assertion moved - what they observe is the apiAlive GUARD, not the
    //document.
    static Json bareRequest(Json extra)
    {
        Params p;
        p.Add("id", PLAYER_ID);
        for (auto it = extra.begin(); it != extra.end(); ++it)
            p.Add(it.key(), it.value().get<std::string>());
        return p.toNJson();
    }

    /***************************************************************************
     * The request parameters of each method, as named functions so they can be
     * fed to the case-generating macro below without commas leaking into the
     * preprocessor. `from`/`count` are 2/7 everywhere, so a method reading the
     * wrong one of the two is visible.
     **************************************************************************/
    static Json E_PLAIN()      { return Json{{ "from", "2" }, { "count", "7" }}; }
    static Json E_ARTIST()     { return Json{{ "from", "2" }, { "count", "7" }, { "artist_id", "art_9" }}; }
    static Json E_YEAR()       { return Json{{ "from", "2" }, { "count", "7" }, { "year", "1994" }}; }
    static Json E_GENRE()      { return Json{{ "from", "2" }, { "count", "7" }, { "genre", "gen_4" }}; }
    static Json E_ALBUM()      { return Json{{ "from", "2" }, { "count", "7" }, { "album_id", "alb_3" }}; }
    static Json E_PLAYLIST()   { return Json{{ "from", "2" }, { "count", "7" }, { "playlist_id", "pl_8" }}; }
    static Json E_FOLDER()     { return Json{{ "from", "2" }, { "count", "7" }, { "folder_id", "fold_5" }}; }
    static Json E_SEARCH()     { return Json{{ "from", "2" }, { "count", "7" }, { "search", "nirvana" }}; }
    static Json E_RADIOITEMS() { return Json{{ "from", "2" }, { "count", "7" }, { "radio_id", "rad_1" },
                                             { "item_id", "it_2" }, { "search", "jazz" }}; }
    static Json E_TRACK()      { return Json{{ "track_id", "trk_77" }}; }
};

/*******************************************************************************
 * THE NOMINAL ANSWER OF EACH OF THE FIFTEEN METHODS, ON BOTH TRANSPORTS.
 *
 * The three cases the macro generates per method:
 *
 *   Ws<Name>       the exact WS answer envelope, and every argument the AudioDB
 *                  was called with - method, from, nb and the identifier. This
 *                  is the reference shape the guard must leave untouched.
 *   Http<Name>     the same over HTTP, where the answer is the bare document
 *                  with no envelope. Thirty goldens for thirty call sites, and
 *                  the same four argument assertions: the request parsing is
 *                  per-method code, so proving it on one transport proves
 *                  nothing about the other.
 *   <Name>PlayerDeletedMidFlightStillAnswers
 *                  the IO is deleted while its answer is in flight and the
 *                  client is still there. MEASUREMENT OF THE "SECOND UAF" OF
 *                  T3.17 - see the section comment further down.
 *
 * The fourth shape, "the JsonApi dies before the answer comes back", is NOT
 * here: it is the case the apiAlive guard exists for, and today it does not
 * fail, it CRASHES (measured: SIGSEGV in WsClientGoneBeforeAnswerIsIgnored on
 * an unguarded tree). A characterization commit has to be green, so those
 * cases arrive with the guard, in the second commit - same split as T3.17b.
 ******************************************************************************/

#define DB_METHOD_CASES(Name, slug, httpAction, wsAction, apiMethod, dbMethod, expFrom, expNb, expId, extraFn) \
                                                                               \
TEST_F(JsonApiMusicDbTest, Ws##Name)                                           \
{                                                                              \
    addPlayer();                                                               \
                                                                               \
    WsTestSession ws;                                                          \
    ws.send(wsRequest(wsAction, PLAYER_ID, extraFn()));                        \
                                                                               \
    ASSERT_EQ(1u, ws.count());                                                 \
    EXPECT_JSON_GOLDEN("t317c_ws_" slug, ws.lastMessage());                    \
                                                                               \
    ASSERT_EQ(1u, db->calls.size());                                           \
    EXPECT_EQ(std::string(dbMethod), db->calls[0].method);                     \
    EXPECT_EQ(expFrom, db->calls[0].from);                                     \
    EXPECT_EQ(expNb, db->calls[0].nb);                                         \
    EXPECT_EQ(std::string(expId), db->calls[0].id);                            \
}                                                                              \
                                                                               \
TEST_F(JsonApiMusicDbTest, Http##Name)                                         \
{                                                                              \
    addPlayer();                                                               \
                                                                               \
    HttpTestRequest req;                                                       \
    req.send(httpRequest(httpAction, PLAYER_ID, extraFn()));                   \
                                                                               \
    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());                            \
    EXPECT_JSON_GOLDEN("t317c_http_" slug, req.body());                        \
                                                                               \
    ASSERT_EQ(1u, db->calls.size());                                           \
    EXPECT_EQ(std::string(dbMethod), db->calls[0].method);                     \
    EXPECT_EQ(expFrom, db->calls[0].from);                                     \
    EXPECT_EQ(expNb, db->calls[0].nb);                                         \
    EXPECT_EQ(std::string(expId), db->calls[0].id);                            \
}                                                                              \
                                                                               \
TEST_F(JsonApiMusicDbTest, Name##PlayerDeletedMidFlightStillAnswers)           \
{                                                                              \
    FakeMusicPlayer *player = addPlayer();                                     \
    queue.deferred = true;                                                     \
                                                                               \
    WsTestSession ws;                                                          \
    ws.send(wsRequest(wsAction, PLAYER_ID, extraFn()));                        \
                                                                               \
    /* The connection object owns the pending answer and outlives the IO */    \
    std::function<void()> lateAnswer = queue.takeNext();                       \
    ASSERT_TRUE((bool)lateAnswer);                                             \
    ASSERT_TRUE(deleteIO(player));                                             \
    player = nullptr;                                                          \
    db = nullptr;                                                              \
                                                                               \
    EXPECT_EQ(0u, ws.count());                                                 \
    lateAnswer();                                                              \
    lateAnswer = std::function<void()>();                                      \
                                                                               \
    ASSERT_EQ(1u, ws.count());                                                 \
    EXPECT_JSON_GOLDEN("t317c_ws_" slug, ws.lastMessage());                    \
}

/* `from` is 2 and `count` is 7 in every request (E_* above), and the two are
 * asserted SEPARATELY on every method and every transport. They must stay
 * different from each other: a method reading "from" where it means "count" -
 * a key SWAP, not a rename - is invisible to the goldens, because the fake
 * database answers a payload that does not depend on from/nb at all. Pinning
 * only `from`, as the first version of this file did, let exactly that mutation
 * through on 13 of the 14 list methods (found in review by counter-mutation).
 * If you ever make from == count here, you delete that coverage.
 */
//                Name                slug                 HTTP action           WS action             JsonApi method             AudioDB method        from  nb  id           params
DB_METHOD_CASES(GetAlbums,          "get_albums",          "get_albums",         "get_album",          audioDbGetAlbums,          "getAlbums",          2,    7,  "",          E_PLAIN)
DB_METHOD_CASES(GetArtistAlbum,     "get_artist_album",    "get_artist_album",   "get_artist_album",   audioDbGetAlbumArtistItem, "getArtistsAlbums",   2,    7,  "art_9",     E_ARTIST)
DB_METHOD_CASES(GetYearAlbums,      "get_year_albums",     "get_year_albums",    "get_year_albums",    audioDbGetYearAlbums,      "getYearsAlbums",     2,    7,  "1994",      E_YEAR)
DB_METHOD_CASES(GetGenreArtists,    "get_genre_artists",   "get_genre_artists",  "get_genre_artists",  audioDbGetGenreArtists,    "getGenresArtists",   2,    7,  "gen_4",     E_GENRE)
DB_METHOD_CASES(GetAlbumTitles,     "get_album_titles",    "get_album_titles",   "get_album_titles",   audioDbGetAlbumTitles,     "getAlbumsTitles",    2,    7,  "alb_3",     E_ALBUM)
DB_METHOD_CASES(GetPlaylistTitles,  "get_playlist_titles", "get_playlist_titles","get_playlist_titles",audioDbGetPlaylistTitles,  "getPlaylistsTracks", 2,    7,  "pl_8",      E_PLAYLIST)
DB_METHOD_CASES(GetArtists,         "get_artists",         "get_artists",        "get_artists",        audioDbGetArtists,         "getArtists",         2,    7,  "",          E_PLAIN)
DB_METHOD_CASES(GetYears,           "get_years",           "get_years",          "get_years",          audioDbGetYears,           "getYears",           2,    7,  "",          E_PLAIN)
DB_METHOD_CASES(GetGenres,          "get_genres",          "get_genres",         "get_genres",         audioDbGetGenres,          "getGenres",          2,    7,  "",          E_PLAIN)
DB_METHOD_CASES(GetPlaylists,       "get_playlists",       "get_playlists",      "get_playlists",      audioDbGetPlaylists,       "getPlaylists",       2,    7,  "",          E_PLAIN)
DB_METHOD_CASES(GetMusicFolder,     "get_music_folder",    "get_music_folder",   "get_music_folder",   audioDbGetMusicFolder,     "getMusicFolder",     2,    7,  "fold_5",    E_FOLDER)
DB_METHOD_CASES(GetSearch,          "get_search",          "get_search",         "get_search",         audioDbGetSearch,          "getSearch",          2,    7,  "nirvana",   E_SEARCH)
DB_METHOD_CASES(GetRadios,          "get_radios",          "get_radios",         "get_radios",         audioDbGetRadios,          "getRadios",          2,    7,  "",          E_PLAIN)
DB_METHOD_CASES(GetRadioItems,      "get_radio_items",     "get_radio_items",    "get_radio_items",    audioDbGetRadioItems,      "getRadiosItems",     2,    7,  "rad_1",     E_RADIOITEMS)
//get_track_infos takes neither from nor count: the fake leaves both at -1.
DB_METHOD_CASES(GetTrackInfos,      "get_track_infos",     "get_track_infos",    "get_track_infos",    audioDbGetTrackInfos,      "getTrackInfos",      -1,   -1, "trk_77",    E_TRACK)

/*******************************************************************************
 * THE ARGUMENTS THE DATABASE IS ACTUALLY CALLED WITH.
 *
 * The macro above checks the method name, `from`, `nb` and the single
 * identifier, on both transports. The first case below finishes the job for the
 * only method carrying more than one identifier. The second is subsumed by the
 * macro since `nb` was added to it, and is kept only because it names the
 * property in one place: from and count are two DIFFERENT numbers and must not
 * be allowed to collapse into one.
 ******************************************************************************/

TEST_F(JsonApiMusicDbTest, RadioItemsForwardsItsThreeIdentifiers)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsRequest("get_radio_items", PLAYER_ID, E_RADIOITEMS()));

    ASSERT_EQ(1u, db->calls.size());
    EXPECT_EQ("rad_1", db->calls[0].id);
    EXPECT_EQ("it_2", db->calls[0].itemId);
    EXPECT_EQ("jazz", db->calls[0].search);
}

TEST_F(JsonApiMusicDbTest, FromAndCountAreForwardedInThatOrder)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsRequest("get_artists", PLAYER_ID, E_PLAIN()));

    ASSERT_EQ(1u, db->calls.size());
    EXPECT_EQ(2, db->calls[0].from);
    EXPECT_EQ(7, db->calls[0].nb);
}

/*******************************************************************************
 * THE THREE BRANCHES OF processDbResult() (JsonApi.cpp:1079-1101).
 *
 * Fourteen of the fifteen methods answer through it, so its behaviour IS their
 * response shape. Frozen as it is, oddities included:
 *   - the leading Params carrying the "count" marker is ALSO appended to the
 *     items array, so items[0] of a normal answer is {"count":"2"} and not a
 *     row. Production behaviour of every SqueezeboxDB getter; visible in the
 *     thirty goldens above.
 *   - a count of "0" clears the array but STILL emits "total_count":"0".
 *   - no count anywhere means no total_count key at all - absent, never null.
 ******************************************************************************/

TEST_F(JsonApiMusicDbTest, ZeroCountClearsTheItemsButKeepsTotalCount)
{
    addPlayer();
    db->shape = FakeMusicDb::ZeroCount;

    WsTestSession ws;
    ws.send(wsRequest("get_artists", PLAYER_ID, E_PLAIN()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317c_ws_zero_count", ws.lastMessage());
}

TEST_F(JsonApiMusicDbTest, NoCountMeansNoTotalCountKey)
{
    addPlayer();
    db->shape = FakeMusicDb::NoCount;

    WsTestSession ws;
    ws.send(wsRequest("get_artists", PLAYER_ID, E_PLAIN()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317c_ws_no_count", ws.lastMessage());
}

/*******************************************************************************
 * THE SYNCHRONOUS ERROR ANSWERS.
 *
 * getAudioPlayer() (JsonApi.cpp:918) then the from/count gate answer before any
 * round trip. These are the answers a guard MUST keep giving unchanged.
 ******************************************************************************/

//An id that exists but is not an AudioPlayer. Note the misspelling of
//"unkown player_id": it is production behaviour, frozen as is.
TEST_F(JsonApiMusicDbTest, WsUnknownPlayerId)
{
    WsTestSession ws;
    ws.send(wsRequest("get_artists", ID_BOOL_IN, E_PLAIN()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317c_ws_unknown_player", ws.lastMessage());
}

TEST_F(JsonApiMusicDbTest, HttpUnknownPlayerId)
{
    HttpTestRequest req;
    req.send(httpRequest("get_genres", "t317c_no_such_io", E_PLAIN()));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_JSON_GOLDEN("t317c_http_unknown_player", req.body());
}

//An absent id is a different message from an unknown one.
TEST_F(JsonApiMusicDbTest, WsEmptyPlayerId)
{
    WsTestSession ws;
    ws.send(wsRequest("get_years", "", E_PLAIN()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317c_ws_empty_player_id", ws.lastMessage());
}

//The second gate, shared by fourteen of the fifteen: from and count must both
//be present AND parse as integers.
TEST_F(JsonApiMusicDbTest, WsMissingFromAndCount)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsRequest("get_album", PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317c_ws_wrong_from_count", ws.lastMessage());
    EXPECT_TRUE(db->calls.empty());
}

TEST_F(JsonApiMusicDbTest, WsFromIsNotANumber)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsRequest("get_album", PLAYER_ID, Json{{ "from", "start" }, { "count", "7" }}));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317c_ws_wrong_from_count", ws.lastMessage());
    EXPECT_TRUE(db->calls.empty());
}

TEST_F(JsonApiMusicDbTest, WsCountIsNotANumber)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsRequest("get_radios", PLAYER_ID, Json{{ "from", "2" }, { "count", "many" }}));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317c_ws_wrong_from_count", ws.lastMessage());
    EXPECT_TRUE(db->calls.empty());
}

//get_track_infos is the fifteenth method and has NO from/count gate: it goes
//straight to the database with whatever track_id it was given, empty included.
TEST_F(JsonApiMusicDbTest, WsTrackInfosHasNoFromCountGate)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsRequest("get_track_infos", PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317c_ws_track_infos_no_track_id", ws.lastMessage());

    ASSERT_EQ(1u, db->calls.size());
    EXPECT_EQ("getTrackInfos", db->calls[0].method);
    EXPECT_EQ("", db->calls[0].id);
}

/* The known divergence of JsonApiCharacterization.h: the SAME method answers to
 * "get_albums" over HTTP and to "get_album" over WS, and each transport rejects
 * the other's spelling. Frozen, not fixed.
 */
TEST_F(JsonApiMusicDbTest, WsRejectsTheHttpSpellingOfGetAlbums)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsRequest("get_albums", PLAYER_ID, E_PLAIN()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317c_ws_unknown_audio_action", ws.lastMessage());
    EXPECT_TRUE(db->calls.empty());
}

TEST_F(JsonApiMusicDbTest, HttpRejectsTheWsSpellingOfGetAlbum)
{
    addPlayer();

    HttpTestRequest req;
    req.send(httpRequest("get_album", PLAYER_ID, E_PLAIN()));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_JSON_GOLDEN("t317c_http_unknown_audio_action", req.body());
    EXPECT_TRUE(db->calls.empty());
}

/*******************************************************************************
 * ONE ROUND TRIP, ANSWERED LATE.
 *
 * Same nominal answer, but driven the way the real transport behaves: the
 * request goes out, processApi() returns with nothing sent, and the answer
 * arrives later. This is the "object alive for the whole request" invariant of
 * T3.17: the guard must not swallow an answer whose client is still there.
 ******************************************************************************/

TEST_F(JsonApiMusicDbTest, DeferredAnswerStillReachesALiveClient)
{
    addPlayer();
    queue.deferred = true;

    WsTestSession ws;
    ws.send(wsRequest("get_album", PLAYER_ID, E_PLAIN()));

    //Nothing answered yet: the request is in flight
    EXPECT_EQ(0u, ws.count());
    ASSERT_EQ(1u, queue.count());

    ASSERT_TRUE(queue.fireNext());

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317c_ws_get_albums", ws.lastMessage());
}

TEST_F(JsonApiMusicDbTest, DeferredTrackInfosAnswerStillReachesALiveClient)
{
    addPlayer();
    queue.deferred = true;

    WsTestSession ws;
    ws.send(wsRequest("get_track_infos", PLAYER_ID, E_TRACK()));

    EXPECT_EQ(0u, ws.count());
    ASSERT_EQ(1u, queue.count());

    ASSERT_TRUE(queue.fireNext());

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317c_ws_get_track_infos", ws.lastMessage());
}

/*******************************************************************************
 * THE PLAYER IO DELETED WHILE ITS ANSWER IS IN FLIGHT - THE "SECOND UAF".
 *
 * T3.17's "le bug est DOUBLE" section says the audio family also carries a raw
 * AudioPlayer* across the round trip, which T3.17a reproduced on the recursive
 * playlist chain. T3.17b measured it ABSENT on its five single shots, and the
 * fifteen <Name>PlayerDeletedMidFlightStillAnswers cases generated above
 * measure it ABSENT here too, for the same reason: not one of the fifteen
 * callback bodies names `player`. Fourteen of them are
 * `result_lambda(processDbResult(data))` and the fifteenth is
 * `result_lambda(data.params.toNJson())` - a [=] capture default only captures
 * what the body odr-uses, so the pointer is never even captured. The player,
 * and its AudioDB member, are dereferenced exactly once, synchronously, BEFORE
 * the request goes out.
 *
 * The observable consequence is those fifteen cases: with the client still
 * there, a late answer whose IO is gone is delivered IN FULL, from the data the
 * answer already carried. There is nothing to re-resolve and nothing that would
 * be mutilated, so T3.17c keeps this behaviour instead of turning it into an
 * error - the same call T3.17b and T3.17d made. They are green before the guard
 * and after it, and clean under ASan in both states.
 *
 * The AudioDB is a member of the fake player and dies with it, so those cases
 * also cover "the database the request was issued to is gone".
 ******************************************************************************/


/*******************************************************************************
 * LIFETIME - what T3.17c actually fixes. ADDED WITH THE GUARD, NOT BEFORE.
 *
 * The round trip is real: the request goes out, the JsonApi hands the answer's
 * continuation to the player's database connection, and the answer comes back
 * later. In between, the client can disconnect - and HttpClient::~HttpClient()
 * (HttpClient.cpp:162) deletes the handler, JsonApi base sub-object included.
 * The late answer then calls processDbResult(), a MEMBER of the destroyed
 * JsonApi (this is the `this` the fourteen -Wdeprecated implicit-capture
 * warnings of JsonApi.cpp point at), and then result_lambda, which is the
 * handler's own lambda capturing `this` too, so sendJson() dereferences a freed
 * object.
 *
 * These cases live in the SECOND commit of T3.17c, unlike everything above,
 * because on an unguarded tree they do not fail, they take the process down:
 * measured SIGSEGV in WsClientGoneBeforeAnswerIsIgnored, and under ASan the
 * heap-use-after-free quoted in the commit message. A characterization commit
 * has to be green. Same split as T3.17b.
 *
 * THE FIFTEENTH METHOD. audioDbGetTrackInfos does NOT call processDbResult()
 * and so raises no -Wdeprecated warning - the reason the file has 14 of them
 * and not 15. It needs the guard just the same, for the second of the two
 * reasons above: `result_lambda` is captured by value into its [=] lambda, and
 * that std::function IS the handler's lambda holding the handler's `this`.
 * MEASURED both ways: ApiGoneBeforeGetTrackInfosAnswer below fails on an
 * unguarded tree exactly like the other fourteen, and
 * WsClientGoneBeforeTrackInfosAnswerIsIgnored reports the same
 * heap-use-after-free under ASan. "No implicit this capture" is NOT the same
 * property as "no use-after-free".
 *
 * The cases come in two shapes:
 *   - fifteen on a bare JsonApi (ApiGoneBefore<Name>Answer), whose result
 *     lambda writes into a test local. They observe the guard directly: the
 *     answer must NOT fire. Being on a bare JsonApi and not on a handler, they
 *     FAIL rather than crash without the guard, which is what makes them a
 *     regression net rather than an ASan-only net.
 *   - the three at the end, through a real WS session, which is where the
 *     use-after-free actually lives. They assert nothing beyond "it ran"; ASan
 *     is the oracle.
 ******************************************************************************/

//Drives one method on a bare JsonApi that dies before the answer comes back.
//Fails when the result lambda fired anyway - i.e. when the answer was sent to
//a destroyed object.
#define DB_GUARD_CASE(Name, apiMethod, extraFn)                                \
TEST_F(JsonApiMusicDbTest, ApiGoneBefore##Name##Answer)                        \
{                                                                              \
    addPlayer();                                                               \
    queue.deferred = true;                                                     \
                                                                               \
    const Json jdata = bareRequest(extraFn());                                 \
    bool answered = false;                                                     \
                                                                               \
    {                                                                          \
        JsonApi api;                                                           \
        api.apiMethod(jdata, [&](const Json &)                                 \
        {                                                                      \
            answered = true;                                                   \
        });                                                                    \
        ASSERT_EQ(1u, queue.count());                                          \
        /* api dies here, exactly as when the client disconnects */            \
    }                                                                          \
                                                                               \
    ASSERT_TRUE(queue.fireNext());                                             \
    EXPECT_FALSE(answered);                                                    \
}

DB_GUARD_CASE(GetAlbums,         audioDbGetAlbums,          E_PLAIN)
DB_GUARD_CASE(GetArtistAlbum,    audioDbGetAlbumArtistItem, E_ARTIST)
DB_GUARD_CASE(GetYearAlbums,     audioDbGetYearAlbums,      E_YEAR)
DB_GUARD_CASE(GetGenreArtists,   audioDbGetGenreArtists,    E_GENRE)
DB_GUARD_CASE(GetAlbumTitles,    audioDbGetAlbumTitles,     E_ALBUM)
DB_GUARD_CASE(GetPlaylistTitles, audioDbGetPlaylistTitles,  E_PLAYLIST)
DB_GUARD_CASE(GetArtists,        audioDbGetArtists,         E_PLAIN)
DB_GUARD_CASE(GetYears,          audioDbGetYears,           E_PLAIN)
DB_GUARD_CASE(GetGenres,         audioDbGetGenres,          E_PLAIN)
DB_GUARD_CASE(GetPlaylists,      audioDbGetPlaylists,       E_PLAIN)
DB_GUARD_CASE(GetMusicFolder,    audioDbGetMusicFolder,     E_FOLDER)
DB_GUARD_CASE(GetSearch,         audioDbGetSearch,          E_SEARCH)
DB_GUARD_CASE(GetRadios,         audioDbGetRadios,          E_PLAIN)
DB_GUARD_CASE(GetRadioItems,     audioDbGetRadioItems,      E_RADIOITEMS)
DB_GUARD_CASE(GetTrackInfos,     audioDbGetTrackInfos,      E_TRACK)

/* The real thing: a websocket client that leaves while its answer is in
 * flight. Without the guard this is the heap-use-after-free quoted in the
 * commit message - and, without ASan, a plain SIGSEGV.
 */
TEST_F(JsonApiMusicDbTest, WsClientGoneBeforeAnswerIsIgnored)
{
    addPlayer();
    queue.deferred = true;

    {
        WsTestSession ws;
        ws.send(wsRequest("get_album", PLAYER_ID, E_PLAIN()));
        ASSERT_EQ(1u, queue.count());
        //ws dies here, exactly as when the client disconnects
    }

    //The database answer arrives afterwards and must touch nothing
    EXPECT_TRUE(queue.fireNext());
}

/* The fifteenth method, which reaches result_lambda without going through
 * processDbResult() - the one the warning count does not point at.
 */
TEST_F(JsonApiMusicDbTest, WsClientGoneBeforeTrackInfosAnswerIsIgnored)
{
    addPlayer();
    queue.deferred = true;

    {
        WsTestSession ws;
        ws.send(wsRequest("get_track_infos", PLAYER_ID, E_TRACK()));
        ASSERT_EQ(1u, queue.count());
    }

    EXPECT_TRUE(queue.fireNext());
}

/* Both deaths at once: the client leaves AND the IO is deleted before the
 * answer comes back. The apiAlive check is what stops the chain; nothing here
 * needs the player, so there is no second dereference to guard (see the
 * PlayerDeletedMidFlight section above).
 */
TEST_F(JsonApiMusicDbTest, ClientAndPlayerBothGoneBeforeAnswer)
{
    FakeMusicPlayer *player = addPlayer();
    queue.deferred = true;

    std::function<void()> lateAnswer;
    {
        WsTestSession ws;
        ws.send(wsRequest("get_search", PLAYER_ID, E_SEARCH()));
        lateAnswer = queue.takeNext();
        ASSERT_TRUE((bool)lateAnswer);
    }
    ASSERT_TRUE(deleteIO(player));
    player = nullptr;
    db = nullptr;

    lateAnswer();
    lateAnswer = std::function<void()>();
}
