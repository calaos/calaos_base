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

/* T3.19 - the two REMOTE CRASHES of JsonApi.cpp reachable by an AUTHENTICATED
 * client on a perfectly legitimate API call. Same file, same class of defect:
 * an input that is never validated before it is used.
 *
 *  BUG 1 - SIGSEGV. AudioPlayer::database is a raw pointer left null by the
 *          base constructor (AudioPlayer.cpp:28) and get_database()
 *          (AudioPlayer.h:105) hands it back unguarded. The sixteen audio_db
 *          actions - audioGetDbStats() plus the fifteen audioDbGet* - all do
 *          `player->get_database()->getXxx(...)` with no null check
 *          (JsonApi.cpp:966, :1165, :1202, :1239, :1276, :1313, :1350, :1386,
 *          :1422, :1458, :1494, :1531, :1568, :1604, :1644, :1668).
 *          Squeezebox.cpp:81 is the ONLY assignment in the whole tree, so every
 *          other concrete player - RoonPlayer, and the one the reference house
 *          carries - dereferences null.
 *
 *  BUG 2 - SIGFPE. buildJsonEventLog() parses per_page with
 *          Utils::from_string() and ignores its return code. Since C++11 a
 *          failed extraction WRITES ZERO into the destination, so a non empty
 *          non numeric per_page - and "0" itself - reaches HistLogger as zero,
 *          and HistLogger.cpp:268 computes `rowcount / ac->per_page` INSIDE the
 *          sqlite worker thread. The try of :257 catches nothing: a signal is
 *          not an exception.
 *
 * COUNTER-INTUITIVE, AND WHY NEITHER WAS SEEN: for bug 2, a HUGE per_page
 * saturates to INT_MAX and is harmless while an ABSURD one is fatal; for bug 1,
 * the capability IS published (JsonApi.cpp:406 emits "database" in get_home
 * from canDatabase()) - the information exists, it is simply never read back
 * before the call.
 *
 * ---------------------------------------------------------------------------
 * WHAT THE FIX ANSWERS, AND WHY THAT WORDING
 * ---------------------------------------------------------------------------
 * The rule of the T3.17 series is to REUSE THE EXISTING VOCABULARY, never to
 * invent a new error shape. Both answers below are the shape the neighbouring
 * refusal of the very same function already produces:
 *
 *   audio_db without a database -> {"error": "no music database"}
 *       The sixteen methods already answer {"error": "unkown player_id"} when
 *       getAudioPlayer() fails and {"error": "wrong from/count"} when the
 *       paging arguments are bad, and both transports already wrap an
 *       {"error": ...} document for the unknown audio_action
 *       (JsonApiHandlerWS.cpp:461, JsonApiHandlerHttp.cpp:862). Same document,
 *       one more reason. NOT an empty list: an empty "items" array would be a
 *       MUTILATED ANSWER - indistinguishable from a database that really holds
 *       no album, which is the one thing a client must not be told here.
 *
 *   eventlog with per_page <= 0 -> {"error": "per_page is out of range"}
 *       buildJsonEventLog()'s two callbacks answer {"error": <message>} and
 *       nothing else, and the message mirrors HistLogger's own "page is out of
 *       range" (HistLogger.cpp:276), already goldened by T3.17f. NOT a silent
 *       clamp to 100: answering a hundred rows to a client that asked for zero
 *       is answering a question nobody asked.
 *
 * THE GUARD IS ON THE POINTER, NOT ON THE CAPABILITY, and that is a decision
 * with a test of its own. canDatabase() is a per class constant
 * (AudioPlayer.h:101 false, Squeezebox.h:204 true, RoonPlayer.h:177 false); the
 * precondition of the dereference is `database != nullptr`. The two happen to
 * agree in today's tree and there is no reason to make the API depend on that
 * coincidence, so CapabilityFalseButDatabasePresentStillAnswersEverything and
 * CapabilityTrueButDatabaseNullIsStillRefused pin the two disagreeing corners.
 *
 * NO HANDLER IS EDITED. Filtering on canDatabase() in the transports was
 * considered and MEASURED: it would touch JsonApiHandlerHttp.cpp AND
 * JsonApiHandlerWS.cpp, duplicate getAudioPlayer() in both, and invent an
 * ordering between three refusals that E4.0e froze ("unkown player_id",
 * "unkown audio_action", and the new one) - 32 dispatch branches edited to
 * cover 16 call sites that one guard already covers, on both transports, by the
 * same transitive mechanism T3.17e measured.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS EXERCISABLE, HONESTLY
 * ---------------------------------------------------------------------------
 * BUG 1 IS REPRODUCED. The crash is in-process on the reference house's own
 * RoonPlayer - which is exactly what T3.17b hit while writing its fake, and
 * what forced T3.17c and E4.0f to install a concrete database explicitly
 * (JsonApiAudioPayload_test.cpp:158-164 quotes the bug for that reason). The
 * thirty-four cases that reproduce it CANNOT live in the first commit: on the
 * unguarded tree they do not fail, they take the binary down, so they arrive
 * with the fix. This is the derogation T3.17b/c/d/f already used.
 *
 * BUG 2 IS PROVEN BY THE CLAMP, NOT BY THE SIGNAL, in the permanent suite, and
 * deliberately so. A SIGFPE raised in HistLogger's sqlite WORKER THREAD cannot
 * be caught by EXPECT_EXIT: the fixture that owns a live worker thread also
 * owns the singleton, the thread is shared with every other case of the
 * process, and gtest's death tests fork a process whose sqlite handle and uvw
 * loop are duplicated in an undefined state. So the standing proof is the
 * OBSERVABLE CONSEQUENCE of the clamp: an invalid per_page is answered
 * SYNCHRONOUSLY, inside send(), before any loop turn. A HistLogger answer is
 * impossible synchronously - it needs a queue push, a worker wake-up, an
 * AsyncHandle and a pump, and AValidPerPageGoesAllTheWayToTheWorker,
 * APageOutOfRangeIsStillHistLoggersOwnRefusal and
 * HttpAValidPerPageIsAnsweredThroughTheWorkerToo assert that nothing has
 * arrived before the first pump. A synchronous answer therefore PROVES the
 * request never reached the worker, which is the whole claim.
 *
 * THE SIGNAL ITSELF WAS OBSERVED, ONCE, AS A COUNTER-MUTATION AND NOT AS A
 * CASE. Weakening the guard by one character - `perPage <= 0` to `perPage < 0`,
 * which lets exactly per_page:"0" and the from_string() zeroes back through -
 * makes this binary die with "Floating point exception (core dumped)", exit
 * 136, on PerPageZeroIsRefusedWithoutEverReachingTheWorker. That measurement is
 * the reason the boundary is `<=` and the reason
 * PerPageOneIsTheSmallestValueTheGuardMustLetThrough guards the other side of
 * it. It stays a mutation because as a case it would end the run: every test
 * after it would simply never execute. The same experiment on bug 1 - removing
 * the guard from ONE of the sixteen call sites, audioDbGetYears - gives exit
 * 139, SIGSEGV.
 *
 * THE PREMISE OF BUG 2 IS UNTOUCHED. The fix reads the parsed VALUE, not
 * from_string()'s return code, so
 * JsonApiSession_test.cpp:FromStringWritesZeroOnFailureWhichIsWhyEventLogCanDivideByZero
 * (E4.0e) stays green with not one assertion changed: from_string() still
 * writes 0 on failure, and that is still precisely why this guard has to exist.
 * The narrow reading is also what makes the diff minimal - every per_page that
 * gets an answer today gets the SAME answer, byte for byte.
 *
 * ---------------------------------------------------------------------------
 * ON THE HARNESS
 * ---------------------------------------------------------------------------
 * Read JsonApiCharacterization.h first, in particular "TRAPS THIS HARNESS SETS
 * FOR YOU" and the RULE OF THE SERIES on member()/str(): no captured payload is
 * ever indexed with operator[] here.
 *
 * The eventlog half brings HistLogger into the harness exactly as E4.0e and
 * T3.17f do - the singleton is built ONCE before the first CoreFixture::SetUp()
 * against a directory that lives as long as the process, because a singleton
 * born inside a case would point at a per-case cache directory that TearDown()
 * rm -rf's, and `sqlite::database db(dbname)` (HistLogger.cpp:190) sits OUTSIDE
 * the try of :192: cantopen would be an uncaught exception in a std::thread.
 * The directory template and the id prefix differ from both of theirs, so the
 * three binaries can never share a database.
 *
 * IO ids, seeded rows and goldens are prefixed t319_ and used nowhere else:
 * Config's IO state cache is process wide and never cleared (see
 * CalaosCoreFixture.h).
 */

#include "JsonApiCharacterization.h"

#include "AudioPlayer.h"
#include "AudioDB.h"
#include "EventManager.h"
#include "HistLogger.h"
#include "ListeRoom.h"
#include "Utils.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

/*******************************************************************************
 * PART ONE - audio_db against a player that has no music database
 ******************************************************************************/

namespace
{

const char *const CAPABLE_EMPTY_ID = "t319_capable_but_empty";
const char *const STOCKED_INCAPABLE_ID = "t319_incapable_but_stocked";

//What the database was actually asked for. The payload alone would not catch
//`from` and `count` being swapped on their way to the AudioDB.
struct DbCall
{
    std::string method;
    int from = -1;
    int nb = -1;
};

/* A minimal but REAL music database. Only the getter the "capability says no,
 * pointer says yes" cases use is overridden; AudioDB's other methods are
 * non-pure and answer nothing, which is exactly what a player with a partially
 * implemented database does.
 */
class OneShotDb: public AudioDB
{
public:
    explicit OneShotDb(Params &p): AudioDB(p) {}

    std::vector<DbCall> calls;

    void getArtists(AudioRequest_cb cb, int from, int nb,
                    AudioPlayerData = AudioPlayerData()) override
    {
        DbCall c;
        c.method = "getArtists";
        c.from = from;
        c.nb = nb;
        calls.push_back(c);

        //The shape a real AudioDB answers with (copied from SqueezeboxDB, same
        //as T3.17c's fake): a leading Params carrying nothing but "count",
        //then one Params per row. This is what processDbResult() reads.
        AudioPlayerData d;
        Params marker;
        marker.Add("count", "2");
        d.vparams.push_back(marker);
        for (int i = 1;i <= 2;i++)
        {
            Params item;
            item.Add("id", "t319_artist_" + Utils::to_string(i));
            item.Add("name", "Artist number " + Utils::to_string(i));
            d.vparams.push_back(item);
        }
        cb(d);
    }
};

/* The player the SERVER ACTUALLY SHIPS in the shape that crashes: capability
 * false, pointer null. The reference house already carries one (a RoonPlayer),
 * and it is the one the cases below drive; this class exists for the OTHER
 * corner - a player that ANNOUNCES a database and has none. There is no such
 * class in the tree today, and that is the point: the guard must not depend on
 * the two staying in agreement.
 */
class CapableButEmptyPlayer: public AudioPlayer
{
public:
    explicit CapableButEmptyPlayer(Params &p): AudioPlayer(p) {}

    //database stays the nullptr AudioPlayer's constructor left there.
    bool canDatabase() override { return true; }
};

//And the mirror corner: capability false, pointer valid. It must ANSWER.
class IncapableButStockedPlayer: public AudioPlayer
{
public:
    explicit IncapableButStockedPlayer(Params &p): AudioPlayer(p), db(dbParams)
    {
        database = &db;
    }

    OneShotDb &fakeDb() { return db; }

    bool canDatabase() override { return false; }

private:
    Params dbParams;
    OneShotDb db;
};

}

class JsonApiAudioDbGuardTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();

        forgetIOState(CAPABLE_EMPTY_ID);
        forgetIOState(STOCKED_INCAPABLE_ID);

        //Carries HOUSE_PLAYER, a real RoonPlayer: canDatabase() false and
        //database null, which is the crashing configuration, straight out of
        //the production XML loader. loadReferenceHouse() also drains the event
        //backlog of the previous case.
        loadReferenceHouse();
    }

    /* Register a fake player in ListeRoom exactly like a config loaded IO:
     * owned by its room (clearCoreState() deletes it) and reachable through
     * ListeRoom::get_io(), which is what getAudioPlayer() looks it up with.
     * Same registration as T3.17c's addPlayer().
     */
    template<typename T>
    T *addPlayer(const char *id, const char *name)
    {
        Params p;
        p.Add("id", id);
        p.Add("name", name);
        p.Add("type", "FakePlayer");

        T *player = new T(p);

        firstRoom()->AddIO(player);
        ListeRoom::Instance().addIOHash(player);

        return player;
    }

    /* ONE request payload for all sixteen actions. Every identifier any of them
     * can read is present, so only "audio_action" varies between the cases, and
     * from/count are 2 and 7 - two different values, so a method reading the
     * wrong one of the two is visible.
     */
    static Json fullArguments()
    {
        return Json{
            { "from", "2" }, { "count", "7" },
            { "artist_id", "t319_art" }, { "year", "1994" },
            { "genre", "t319_gen" }, { "album_id", "t319_alb" },
            { "playlist_id", "t319_pl" }, { "folder_id", "t319_fold" },
            { "search", "t319_search" }, { "radio_id", "t319_rad" },
            { "item_id", "t319_it" }, { "track_id", "t319_trk" },
        };
    }

    //{"msg":"audio_db","msg_id":"t319","data":{"id":..,"audio_action":..,..}}
    static Json wsRequest(const std::string &action, const std::string &id,
                          Json extra = fullArguments())
    {
        Json data = Json{{ "id", id }, { "audio_action", action }};
        for (auto it = extra.begin(); it != extra.end(); ++it)
            data[it.key()] = it.value();

        return Json{
            { "msg", "audio_db" },
            { "msg_id", "t319" },
            { "data", data },
        };
    }

    //HTTP puts everything flat in the request body.
    static Json httpRequest(const std::string &action, const std::string &id,
                            Json extra = fullArguments())
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

    //The audio entry of get_home for one player id, or a null Json.
    static Json audioEntry(const Json &home, const std::string &id)
    {
        for (const Json &a: member(home, "audio"))
            if (str(a, "id") == id)
                return a;
        return Json();
    }
};

/*******************************************************************************
 * THE REFUSALS THAT ALREADY EXIST, AND THEIR ORDER
 *
 * These are green BEFORE the guard and must stay green after it: they are the
 * paths that never reach get_database() today, so a guard placed too early
 * would silently steal their answers. This is where the placement of the new
 * check is pinned - immediately before the dereference, never at the top of the
 * method.
 ******************************************************************************/

TEST_F(JsonApiAudioDbGuardTest, WsWrongFromCountStillWinsOverTheMissingDatabase)
{
    /* HOUSE_PLAYER has NO database, and this request would crash the server if
     * it got as far as the dereference. It does not: the from/count gate
     * (JsonApi.cpp:1117-1123) refuses first. Moving the database check above
     * that gate turns this case red - which is the whole reason it exists.
     */
    WsTestSession ws;
    ws.send(wsRequest("get_album", HOUSE_PLAYER, Json::object()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("audio_db", str(ws.lastEnvelope(), "msg"));
    EXPECT_EQ("wrong from/count", str(ws.lastData(), "error"));
    EXPECT_EQ(1u, ws.lastData().size()) << "the error replaces the whole document";
}

TEST_F(JsonApiAudioDbGuardTest, HttpWrongFromCountStillWinsOverTheMissingDatabase)
{
    HttpTestRequest req;
    req.send(httpRequest("get_albums", HOUSE_PLAYER, Json::object()));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_EQ("wrong from/count", str(req.bodyJson(), "error"));
    EXPECT_EQ(1u, req.bodyJson().size());
}

TEST_F(JsonApiAudioDbGuardTest, AnUnknownPlayerIdIsStillRefusedBeforeAnythingElse)
{
    //getAudioPlayer() runs first of all, so this answer is unchanged by the
    //guard - and it is the answer T3.17a chose to align on for a player that
    //went away. Misspelling included (frozen divergence).
    WsTestSession ws;
    ws.send(wsRequest("get_artists", "t319_no_such_player"));

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("unkown player_id", str(ws.lastData(), "error"));
}

TEST_F(JsonApiAudioDbGuardTest, AnEmptyPlayerIdIsStillRefusedBeforeAnythingElse)
{
    WsTestSession ws;
    ws.send(wsRequest("get_artists", ""));

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("empty player id", str(ws.lastData(), "error"));
}

/*******************************************************************************
 * THE POINTER IS THE PRECONDITION, NOT THE CAPABILITY
 *
 * The mirror case (capability true, pointer null) crashes on the unguarded tree
 * and therefore lives in the second commit.
 ******************************************************************************/

TEST_F(JsonApiAudioDbGuardTest, CapabilityFalseButDatabasePresentStillAnswersEverything)
{
    /* canDatabase() is false and the database is REAL. A guard written on the
     * capability instead of the pointer would refuse this request; the answer
     * below is the full processDbResult() document, and the two arguments the
     * database was called with are asserted too - a payload alone would not
     * notice `from` and `count` being swapped on the way in.
     */
    IncapableButStockedPlayer *player =
            addPlayer<IncapableButStockedPlayer>(STOCKED_INCAPABLE_ID, "Stocked");

    WsTestSession ws;
    ws.send(wsRequest("get_artists", STOCKED_INCAPABLE_ID));

    ASSERT_EQ(1u, ws.count());
    const Json data = ws.lastData();

    //processDbResult() (JsonApi.cpp:1079-1101) appends EVERY Params of the
    //answer, the leading "count" marker row included, and lifts its value into
    //total_count - so three items for two rows. Frozen shape, pinned by E4.0f's
    //e40f_ws_db_count_marker_row_keeps_its_other_keys.
    EXPECT_EQ("2", str(data, "total_count"));
    ASSERT_TRUE(member(data, "items").is_array());
    ASSERT_EQ(3u, member(data, "items").size());
    EXPECT_EQ("2", str(member(data, "items")[0], "count"));
    EXPECT_EQ("t319_artist_1", str(member(data, "items")[1], "id"));
    EXPECT_EQ("Artist number 1", str(member(data, "items")[1], "name"));
    EXPECT_EQ("t319_artist_2", str(member(data, "items")[2], "id"));

    ASSERT_EQ(1u, player->fakeDb().calls.size());
    EXPECT_EQ("getArtists", player->fakeDb().calls[0].method);
    EXPECT_EQ(2, player->fakeDb().calls[0].from);
    EXPECT_EQ(7, player->fakeDb().calls[0].nb);
}

TEST_F(JsonApiAudioDbGuardTest, HttpCapabilityFalseButDatabasePresentStillAnswers)
{
    //The guard goes in JsonApi.cpp and nowhere else, so the claim that it
    //covers both transports has to be observable on both.
    IncapableButStockedPlayer *player =
            addPlayer<IncapableButStockedPlayer>(STOCKED_INCAPABLE_ID, "Stocked");

    HttpTestRequest req;
    req.send(httpRequest("get_artists", STOCKED_INCAPABLE_ID));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    const Json body = req.bodyJson();
    EXPECT_EQ("2", str(body, "total_count"));
    ASSERT_EQ(3u, member(body, "items").size());
    EXPECT_EQ("t319_artist_1", str(member(body, "items")[1], "id"));
    EXPECT_EQ("t319_artist_2", str(member(body, "items")[2], "id"));

    ASSERT_EQ(1u, player->fakeDb().calls.size());
    EXPECT_EQ(2, player->fakeDb().calls[0].from);
    EXPECT_EQ(7, player->fakeDb().calls[0].nb);
}

TEST_F(JsonApiAudioDbGuardTest, GetHomePublishesACapabilityThatSaysNothingAboutThePointer)
{
    /* JsonApi.cpp:406 is canDatabase()'s ONLY reader in the whole tree, and all
     * it does is publish the flag. The three players below make the published
     * flag and the actual pointer disagree in both directions inside ONE
     * payload, which is why the guard cannot be written on the flag:
     *
     *    HOUSE_PLAYER (RoonPlayer)   "false", pointer null
     *    CAPABLE_EMPTY_ID            "true" , pointer null   <- the liar
     *    STOCKED_INCAPABLE_ID        "false", pointer valid  <- the modest one
     *
     * The values are STRINGS "true"/"false", not booleans (the oracle is type
     * strict), and the paired canPlaylist flag is read too so that swapping the
     * two keys in buildJsonAudio() cannot go unnoticed - CAPABLE_EMPTY_ID has
     * database "true" and playlist "false", a pair no other entry repeats.
     */
    addPlayer<CapableButEmptyPlayer>(CAPABLE_EMPTY_ID, "Capable");
    addPlayer<IncapableButStockedPlayer>(STOCKED_INCAPABLE_ID, "Stocked");

    WsTestSession ws;
    ws.send(Json{{ "msg", "get_home" }, { "msg_id", "t319" }});

    ASSERT_EQ(1u, ws.count());
    const Json home = ws.lastData();

    const Json roon = audioEntry(home, HOUSE_PLAYER);
    ASSERT_TRUE(roon.is_object()) << "the reference house player is not in get_home";
    EXPECT_EQ("false", str(roon, "database"));

    const Json liar = audioEntry(home, CAPABLE_EMPTY_ID);
    ASSERT_TRUE(liar.is_object());
    EXPECT_EQ("true", str(liar, "database"))
            << "get_home announces a database this player does not own";
    EXPECT_EQ("false", str(liar, "playlist"));

    const Json modest = audioEntry(home, STOCKED_INCAPABLE_ID);
    ASSERT_TRUE(modest.is_object());
    EXPECT_EQ("false", str(modest, "database"))
            << "get_home hides a database this player does own";
    EXPECT_EQ("false", str(modest, "playlist"));
}

/*******************************************************************************
 * THE CRASH ITSELF, ON ALL SIXTEEN ACTIONS AND BOTH TRANSPORTS
 *
 * THESE THIRTY-FOUR CASES CANNOT LIVE IN THE FIRST COMMIT. On the unguarded
 * tree they do not fail, they SIGSEGV inside processApi() and take the binary
 * with them, and a characterization commit has to be green - so they arrive
 * with the fix, exactly as T3.17b/c/d/f had to do. Measured before the fix:
 * `player->get_database()->getXxx(...)` on HOUSE_PLAYER is a null dereference.
 *
 * The player is not a fake here. It is HOUSE_PLAYER, the reference house's own
 * RoonPlayer, built by the production XML loader from a `<calaos:audio
 * type="Roon">` node - the configuration any Calaos installation without a
 * Squeezebox has. Squeezebox.cpp:81 is the only line of the tree that ever
 * assigns `database`.
 *
 * Thirty-two of the cases share two goldens, because the answer of the sixteen
 * actions IS the same document on a given transport; what differs between them
 * is the CALL SITE, and there are sixteen of those per transport. A guard
 * forgotten at one site does not make a case fail, it makes the binary die at
 * that case.
 ******************************************************************************/

#define DB_NO_DATABASE_CASES(Name, wsAction, httpAction)                        \
                                                                               \
TEST_F(JsonApiAudioDbGuardTest, Ws##Name##IsRefusedInsteadOfCrashing)           \
{                                                                              \
    WsTestSession ws;                                                          \
    ws.send(wsRequest(wsAction, HOUSE_PLAYER));                                \
                                                                               \
    ASSERT_EQ(1u, ws.count());                                                 \
    EXPECT_EQ("audio_db", str(ws.lastEnvelope(), "msg"));                      \
    EXPECT_EQ("t319", str(ws.lastEnvelope(), "msg_id"));                       \
    EXPECT_EQ("no music database", str(ws.lastData(), "error"));               \
    EXPECT_JSON_GOLDEN("t319_ws_audio_db_no_database", ws.lastMessage());      \
}                                                                              \
                                                                               \
TEST_F(JsonApiAudioDbGuardTest, Http##Name##IsRefusedInsteadOfCrashing)        \
{                                                                              \
    HttpTestRequest req;                                                       \
    req.send(httpRequest(httpAction, HOUSE_PLAYER));                           \
                                                                               \
    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());                            \
    EXPECT_EQ("no music database", str(req.bodyJson(), "error"));              \
    EXPECT_JSON_GOLDEN("t319_http_audio_db_no_database", req.body());          \
}

//The WS and HTTP spellings differ on the first one only - the frozen divergence
//of the harness header ("get_albums" over HTTP, "get_album" over WS).
DB_NO_DATABASE_CASES(GetStats,          "get_stats",           "get_stats")
DB_NO_DATABASE_CASES(GetAlbums,         "get_album",           "get_albums")
DB_NO_DATABASE_CASES(GetArtistAlbum,    "get_artist_album",    "get_artist_album")
DB_NO_DATABASE_CASES(GetYearAlbums,     "get_year_albums",     "get_year_albums")
DB_NO_DATABASE_CASES(GetGenreArtists,   "get_genre_artists",   "get_genre_artists")
DB_NO_DATABASE_CASES(GetAlbumTitles,    "get_album_titles",    "get_album_titles")
DB_NO_DATABASE_CASES(GetPlaylistTitles, "get_playlist_titles", "get_playlist_titles")
DB_NO_DATABASE_CASES(GetArtists,        "get_artists",         "get_artists")
DB_NO_DATABASE_CASES(GetYears,          "get_years",           "get_years")
DB_NO_DATABASE_CASES(GetGenres,         "get_genres",          "get_genres")
DB_NO_DATABASE_CASES(GetPlaylists,      "get_playlists",       "get_playlists")
DB_NO_DATABASE_CASES(GetMusicFolder,    "get_music_folder",    "get_music_folder")
DB_NO_DATABASE_CASES(GetSearch,         "get_search",          "get_search")
DB_NO_DATABASE_CASES(GetRadios,         "get_radios",          "get_radios")
DB_NO_DATABASE_CASES(GetRadioItems,     "get_radio_items",     "get_radio_items")
DB_NO_DATABASE_CASES(GetTrackInfos,     "get_track_infos",     "get_track_infos")

TEST_F(JsonApiAudioDbGuardTest, CapabilityTrueButDatabaseNullIsStillRefused)
{
    /* THE OTHER HALF OF THE ARBITRATION. This player ANNOUNCES a database and
     * owns none - the exact shape a canDatabase() filter, in the handlers or
     * here, would wave straight through to the null dereference. There is no
     * such class in the tree today; the guard must not depend on that staying
     * true, and this case is why it is written on the pointer.
     */
    addPlayer<CapableButEmptyPlayer>(CAPABLE_EMPTY_ID, "Capable");

    WsTestSession ws;
    ws.send(wsRequest("get_artists", CAPABLE_EMPTY_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("no music database", str(ws.lastData(), "error"));
    EXPECT_EQ(1u, ws.lastData().size());
}

TEST_F(JsonApiAudioDbGuardTest, HttpCapabilityTrueButDatabaseNullIsStillRefused)
{
    addPlayer<CapableButEmptyPlayer>(CAPABLE_EMPTY_ID, "Capable");

    HttpTestRequest req;
    req.send(httpRequest("get_stats", CAPABLE_EMPTY_ID));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_EQ("no music database", str(req.bodyJson(), "error"));
}

/*******************************************************************************
 * PART TWO - eventlog and its per_page
 ******************************************************************************/

class JsonApiEventLogGuardTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        //MUST run before CoreFixture::SetUp() installs the per-case directories.
        ensureHistLogger();

        JsonApiCharacterizationTest::SetUp();

        /* NO DEFENSIVE DRAIN HERE, and that is a decision, not an omission.
         * E4.0g moved the fixture's pump AFTER CoreFixture::TearDown(), so the
         * EventIODeleted that ~Room() raises per IO no longer survives into the
         * next case: this binary inherits an EMPTY queue (see the contract on
         * JsonApiCharacterizationTest::TearDown()). E4.0g's own measurement is
         * that five such workarounds had accumulated and two of them had never
         * absorbed anything - they were copied, not needed. If a case of this
         * file ever seems to need a pump here, that is a SECOND source of
         * events and it must be named, not pumped.
         *
         * Nothing here pins the absence of a message across a pump anyway: the
         * eventlog answers are located by their "msg" member, and the audio_db
         * cases never pump at all - their answer is produced inside send().
         */
    }

    /***************************************************************************
     * HistLogger in the harness. E4.0e's solution, reused verbatim in shape by
     * T3.17f and again here rather than reinvented: the Meyers singleton is
     * built ONCE, before the first CoreFixture::SetUp(), against a directory
     * that lives as long as the process. Its own template, so no two binaries
     * of this tree can ever share a database.
     **************************************************************************/
    static void ensureHistLogger()
    {
        static bool done = false;
        if (done)
            return;
        done = true;

        static char tmpl[] = "/tmp/calaos_t319_hist_XXXXXX";
        ASSERT_TRUE(::mkdtemp(tmpl) != nullptr)
                << "could not create the history database directory";

        const std::string base = tmpl;
        const std::string conf = base + "/config";
        const std::string cache = base + "/cache";
        ASSERT_EQ(0, ::mkdir(conf.c_str(), 0755));
        ASSERT_EQ(0, ::mkdir(cache.c_str(), 0755));

        std::vector<char> c(conf.begin(), conf.end());
        c.push_back('\0');
        std::vector<char> k(cache.begin(), cache.end());
        k.push_back('\0');
        Utils::initConfigOptions(c.data(), k.data(), true);

        //Round trip through the worker thread: it only answers once the
        //database is open and the two tables exist.
        auto opened = std::make_shared<bool>(false);
        HistLogger::Instance().getEvents(0, 100,
                [opened](bool, string, const vector<HistEvent> &, int, int)
        {
            *opened = true;
        });
        waitUntil([opened]() { return *opened; });
        ASSERT_TRUE(*opened) << "the history database never opened";

        ::atexit([]()
        {
            const std::string cmd = std::string("rm -rf '") + tmpl + "'";
            if (::system(cmd.c_str()) != 0) {}
        });
    }

    //Loop pumping with a deadline. The 2s bound stays well under the 30s read
    //timeout Timer HttpClient's constructor installs (trap 2 of the harness).
    static bool waitUntil(const std::function<bool()> &done)
    {
        for (int i = 0;i < 400;i++)
        {
            if (done())
                return true;
            pumpEventLoop(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return done();
    }

    /***************************************************************************
     * FIVE rows, seeded once per process. Five read two at a time gives
     * total_count=5, total_page=3, page=0, per_page=2 - FOUR DISTINCT VALUES
     * for four keys, so no two of the counters can be swapped in production
     * without a case here going red. That is the exact hole E4.0b, E4.0c and
     * E4.0e were each caught on.
     **************************************************************************/
    static int seededEventCount() { return 5; }

    static std::vector<std::string> &seededUuids()
    {
        static std::vector<std::string> uuids;
        return uuids;
    }

    static void seedEventLog()
    {
        static bool done = false;
        if (done)
            return;
        done = true;

        for (int i = 0;i < seededEventCount();i++)
        {
            HistEvent e = HistEvent::create();
            seededUuids().push_back(e.uuid);
            e.event_type = CalaosEvent::EventIOChanged;
            e.io_id = "t319_logged_io_" + Utils::to_string(i);
            e.io_state = (i % 2)? "true": "false";
            e.pic_uid = "t319_pic_" + Utils::to_string(i);
            e.event_raw = "{\"type_str\":\"io_changed\",\"data\":{\"id\":\"" +
                          e.io_id + "\"}}";
            HistLogger::Instance().appendEvent(e);
        }

        //Wait for the worker to have committed all of them. The result is held
        //in a shared_ptr, never captured by reference: a timed out query's
        //callback would still be alive inside the uvw AsyncHandle.
        int seen = 0;
        for (int attempt = 0;attempt < 20 && seen < seededEventCount();attempt++)
        {
            auto state = std::make_shared<std::pair<bool, int>>(false, 0);
            HistLogger::Instance().getEvents(0, 100,
                    [state](bool success, string, const vector<HistEvent> &,
                            int, int total_count)
            {
                state->first = true;
                if (success)
                    state->second = total_count;
            });

            waitUntil([state]() { return state->first; });
            seen = state->second;
        }

        ASSERT_GE(seen, seededEventCount())
                << "the event log could not be seeded, every case below would "
                   "characterize an empty database";
    }

    //A WS session is also an event sink, so the eventlog answer is located by
    //its "msg" member, never by position or by count.
    static bool hasEventLogAnswer(const WsTestSession &ws)
    {
        for (const auto &m: ws.messages())
            if (str(asJsonDocument(m), "msg") == "eventlog")
                return true;
        return false;
    }

    static Json wsEventLogAnswer(const WsTestSession &ws)
    {
        for (const auto &m: ws.messages())
        {
            const Json env = asJsonDocument(m);
            if (str(env, "msg") == "eventlog")
                return member(env, "data");
        }
        return Json();
    }

    static void sendEventLog(WsTestSession &ws, Json data)
    {
        ws.clear();
        ws.send(Json{{ "msg", "eventlog" }, { "msg_id", "t319" },
                     { "data", data }});
    }

    //Sends an eventlog request over WS and pumps until the async answer lands.
    static Json wsEventLog(WsTestSession &ws, Json data)
    {
        sendEventLog(ws, data);
        waitUntil([&ws]() { return hasEventLogAnswer(ws); });
        return wsEventLogAnswer(ws);
    }
};

/*******************************************************************************
 * THE per_page VALUES THAT ARE ANSWERED TODAY, AND MUST BE ANSWERED IDENTICALLY
 * AFTER THE GUARD.
 *
 * Every one of these is green before the fix. Together they are the control the
 * clamp is measured against: they all take the ASYNCHRONOUS route through the
 * sqlite worker, which is what makes a synchronous answer in the refusal cases
 * a proof rather than a coincidence.
 ******************************************************************************/

TEST_F(JsonApiEventLogGuardTest, AValidPerPageGoesAllTheWayToTheWorker)
{
    /* The positive control, and the asymmetry the refusal cases rest on: the
     * answer CANNOT be there before the loop turns. Four distinct counters, so
     * no two of them are interchangeable.
     */
    seedEventLog();

    WsTestSession ws;
    sendEventLog(ws, Json{{ "page", "0" }, { "per_page", "2" }});

    EXPECT_FALSE(hasEventLogAnswer(ws))
            << "a HistLogger answer arrived synchronously, the round trip is "
               "not being exercised and the refusal cases prove nothing";

    ASSERT_TRUE(waitUntil([&ws]() { return hasEventLogAnswer(ws); }));
    const Json data = wsEventLogAnswer(ws);

    ASSERT_TRUE(data.is_object()) << "eventlog did not answer";
    EXPECT_JSON_EQ(Json(seededEventCount()), member(data, "total_count"));
    EXPECT_JSON_EQ(Json(3), member(data, "total_page"));
    EXPECT_JSON_EQ(Json(0), member(data, "page"));
    EXPECT_JSON_EQ(Json(2), member(data, "per_page"));
    EXPECT_EQ(2u, member(data, "events").size());
    EXPECT_EQ(5u, data.size());
}

TEST_F(JsonApiEventLogGuardTest, PerPageOneIsTheSmallestValueTheGuardMustLetThrough)
{
    //The boundary. `perPage <= 0` and `perPage < 0` differ by exactly this case
    //and by PerPageZero; a guard written one off would take it away.
    seedEventLog();

    WsTestSession ws;
    const Json data = wsEventLog(ws, Json{{ "page", "0" }, { "per_page", "1" }});

    ASSERT_TRUE(data.is_object());
    EXPECT_JSON_EQ(Json(1), member(data, "per_page"));
    EXPECT_JSON_EQ(Json(seededEventCount()), member(data, "total_page"));
    EXPECT_EQ(1u, member(data, "events").size());
}

TEST_F(JsonApiEventLogGuardTest, AnEmptyPerPageKeepsTheDefaultHundred)
{
    /* MEASURED, and it is the counter-intuitive half of the bug: an EMPTY value
     * makes the istream sentry fail BEFORE num_get runs, so nothing is written
     * and the 100 survives. It is the NON EMPTY garbage that writes 0.
     * Pinned by E4.0e at the from_string() level; pinned here on the wire.
     */
    seedEventLog();

    WsTestSession ws;
    const Json data = wsEventLog(ws, Json{{ "page", "0" }, { "per_page", "" }});

    ASSERT_TRUE(data.is_object()) << "an empty per_page must still be answered";
    EXPECT_JSON_EQ(Json(100), member(data, "per_page"));
    EXPECT_JSON_EQ(Json(1), member(data, "total_page"));
    EXPECT_EQ((size_t)seededEventCount(), member(data, "events").size());
}

TEST_F(JsonApiEventLogGuardTest, AnAbsentPerPageKeepsTheDefaultHundred)
{
    seedEventLog();

    WsTestSession ws;
    const Json data = wsEventLog(ws, Json{{ "page", "0" }});

    ASSERT_TRUE(data.is_object());
    EXPECT_JSON_EQ(Json(100), member(data, "per_page"));
    EXPECT_EQ((size_t)seededEventCount(), member(data, "events").size());
}

TEST_F(JsonApiEventLogGuardTest, AHugePerPageSaturatesToIntMaxAndIsHarmless)
{
    /* THE REASON NOBODY FOUND THIS BUG. Every instinct says the dangerous input
     * is the huge one; it is the absurd one. 99999999999 saturates to INT_MAX
     * and is answered normally, and the saturated value is ECHOED, so this case
     * also pins that the guard does not quietly rewrite it.
     */
    seedEventLog();

    WsTestSession ws;
    const Json data = wsEventLog(ws, Json{{ "page", "0" },
                                          { "per_page", "99999999999" }});

    ASSERT_TRUE(data.is_object()) << "a huge per_page must still be answered";
    EXPECT_JSON_EQ(Json(2147483647), member(data, "per_page"));
    EXPECT_JSON_EQ(Json(1), member(data, "total_page"));
    EXPECT_EQ((size_t)seededEventCount(), member(data, "events").size());
}

TEST_F(JsonApiEventLogGuardTest, ANonNumericPageIsStillReadAsPageZero)
{
    /* `page` goes through the SAME unchecked from_string(), and 0 is both what
     * a failed extraction writes and the default - so its observable behaviour
     * is already indistinguishable, and the fix must leave it exactly there.
     * A fix written on from_string()'s RETURN CODE would also land on 0 here,
     * but it would change "1,5" and "5x" from a partial parse to the default;
     * the guard is written on the VALUE precisely so that nothing but the
     * crashing inputs moves.
     */
    seedEventLog();

    WsTestSession ws;
    const Json data = wsEventLog(ws, Json{{ "page", "abc" }, { "per_page", "2" }});

    ASSERT_TRUE(data.is_object()) << "a non numeric page must still be answered";
    EXPECT_JSON_EQ(Json(0), member(data, "page"));
    EXPECT_JSON_EQ(Json(2), member(data, "per_page"));
    EXPECT_EQ(2u, member(data, "events").size());
}

TEST_F(JsonApiEventLogGuardTest, APageOutOfRangeIsStillHistLoggersOwnRefusal)
{
    //The refusal that already exists, and the one the new message is worded
    //after. It comes from the WORKER (HistLogger.cpp:276), so it is
    //asynchronous - the opposite of the new one.
    seedEventLog();

    WsTestSession ws;
    sendEventLog(ws, Json{{ "page", "9" }, { "per_page", "2" }});

    EXPECT_FALSE(hasEventLogAnswer(ws))
            << "this refusal comes from the sqlite worker, it cannot be synchronous";

    ASSERT_TRUE(waitUntil([&ws]() { return hasEventLogAnswer(ws); }));
    const Json data = wsEventLogAnswer(ws);

    EXPECT_EQ("page is out of range", str(data, "error"));
    EXPECT_EQ(1u, data.size()) << "the error replaces the whole document";
}

TEST_F(JsonApiEventLogGuardTest, TheUuidPathIgnoresPerPageEntirelyIncludingZero)
{
    /* THE PLACEMENT PIN OF BUG 2. `per_page:"0"` is fatal on the paginated path
     * and MEANINGLESS on the uuid path, which returns before getEvents() is
     * ever called (JsonApi.cpp:2135-2155) - so this request does not crash even
     * on the unguarded tree, and it must not start being refused either.
     * A guard placed at the top of buildJsonEventLog() turns this case red.
     */
    seedEventLog();
    ASSERT_FALSE(seededUuids().empty());
    const std::string uuid = seededUuids()[1];

    WsTestSession ws;
    const Json data = wsEventLog(ws, Json{{ "uuid", uuid }, { "per_page", "0" }});

    ASSERT_TRUE(data.is_object()) << "the uuid path did not answer";
    EXPECT_TRUE(member(data, "error").is_null())
            << "the uuid path was refused for a per_page it never reads";
    EXPECT_EQ(uuid, str(data, "id"));
    EXPECT_EQ("t319_logged_io_1", str(data, "io_id"));
    EXPECT_EQ("true", str(data, "io_state"));
    EXPECT_EQ(7u, data.size());
}

TEST_F(JsonApiEventLogGuardTest, HttpAValidPerPageIsAnsweredThroughTheWorkerToo)
{
    //Both transports reach the same buildJsonEventLog(), and the guard is added
    //there and nowhere else, so both sides of the claim are measured here.
    seedEventLog();

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "eventlog" },
                                { "page", "0" }, { "per_page", "2" }}));

    EXPECT_EQ(0u, req.count())
            << "a HistLogger answer arrived synchronously over HTTP";

    ASSERT_TRUE(waitUntil([&req]() { return req.count() > 0; }));
    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());

    const Json body = req.bodyJson();
    EXPECT_JSON_EQ(Json(seededEventCount()), member(body, "total_count"));
    EXPECT_JSON_EQ(Json(3), member(body, "total_page"));
    EXPECT_JSON_EQ(Json(0), member(body, "page"));
    EXPECT_JSON_EQ(Json(2), member(body, "per_page"));
    EXPECT_EQ(2u, member(body, "events").size());
}

/*******************************************************************************
 * THE per_page VALUES THAT USED TO KILL THE PROCESS
 *
 * THESE FOUR CASES CANNOT LIVE IN THE FIRST COMMIT EITHER, and for a worse
 * reason than the audio_db ones: the SIGFPE is raised in HistLogger's sqlite
 * WORKER THREAD, so on the unguarded tree the binary dies with no failure
 * report at all and every later case simply never runs.
 *
 * WHAT IS PROVEN HERE, HONESTLY: not the signal, the CLAMP. The signal is not
 * exercisable in this fixture and pretending otherwise would be the dishonest
 * option. EXPECT_EXIT was considered and rejected as unreliable here: the
 * fixture owns a LIVE sqlite worker thread and a uvw loop shared with every
 * other case of the process, and gtest's death tests fork a child that
 * inherits both in an undefined state - a flaky proof is not a proof.
 *
 * What IS proven is the observable consequence: THE ANSWER IS SYNCHRONOUS. It
 * is there before a single loop turn, which a HistLogger answer can never be -
 * that route needs a queue push, a worker wake-up, a uvw AsyncHandle and a
 * pump, and AValidPerPageGoesAllTheWayToTheWorker,
 * APageOutOfRangeIsStillHistLoggersOwnRefusal and
 * HttpAValidPerPageIsAnsweredThroughTheWorkerToo (first commit, green on the
 * unguarded tree) each assert that nothing has arrived before the first pump.
 * A synchronous answer therefore proves the request never reached the worker,
 * which is the entire claim: the value that used to divide by zero no longer
 * gets there.
 ******************************************************************************/

TEST_F(JsonApiEventLogGuardTest, PerPageZeroIsRefusedWithoutEverReachingTheWorker)
{
    //`per_page:"0"` parses cleanly to 0 and used to reach HistLogger.cpp:268
    //as the divisor. The answer now comes back inside send().
    seedEventLog();

    WsTestSession ws;
    sendEventLog(ws, Json{{ "page", "0" }, { "per_page", "0" }});

    ASSERT_TRUE(hasEventLogAnswer(ws))
            << "no synchronous answer: the request went to the sqlite worker, "
               "which is exactly the path that divides by zero";

    const Json data = wsEventLogAnswer(ws);
    EXPECT_EQ("per_page is out of range", str(data, "error"));
    EXPECT_EQ(1u, data.size()) << "the error replaces the whole document";
    EXPECT_JSON_GOLDEN("t319_ws_eventlog_per_page_out_of_range", data);

    //And nothing arrives later either - one answer, not two.
    pumpEventLoop(8);
    EXPECT_EQ(1u, ws.count());
}

TEST_F(JsonApiEventLogGuardTest, ANonNumericPerPageIsRefusedWithoutEverReachingTheWorker)
{
    /* THE SUBTLE ONE. "abc" is not zero, but a failed extraction WRITES zero
     * (C++11 num_get, pinned by E4.0e), so it arrived at HistLogger as the same
     * divisor. This case and the previous one must therefore give the SAME
     * answer, and they are the pair that shows the guard reads the value rather
     * than the text.
     */
    seedEventLog();

    WsTestSession ws;
    sendEventLog(ws, Json{{ "page", "0" }, { "per_page", "abc" }});

    ASSERT_TRUE(hasEventLogAnswer(ws)) << "no synchronous answer";
    EXPECT_JSON_GOLDEN("t319_ws_eventlog_per_page_out_of_range",
                       wsEventLogAnswer(ws));
}

TEST_F(JsonApiEventLogGuardTest, ANegativePerPageIsRefusedByTheSameGuard)
{
    /* DELIBERATE BEHAVIOUR CHANGE, and the only one of this ticket that is not
     * a crash. MEASURED BEFORE THE FIX: per_page:"-5" did not kill anything, it
     * reached the worker, made total_page negative and came back with
     * HistLogger's "page is out of range" - an error naming the wrong
     * parameter. It is refused here instead, synchronously and by name, because
     * sqlite reads a negative LIMIT as "no limit": on a table small enough for
     * the page check to pass, the old path would have returned EVERY row under
     * a document claiming per_page:-5.
     */
    seedEventLog();

    WsTestSession ws;
    sendEventLog(ws, Json{{ "page", "0" }, { "per_page", "-5" }});

    ASSERT_TRUE(hasEventLogAnswer(ws))
            << "a negative per_page still went to the worker";
    EXPECT_EQ("per_page is out of range", str(wsEventLogAnswer(ws), "error"));
}

TEST_F(JsonApiEventLogGuardTest, HttpPerPageZeroIsRefusedWithoutEverReachingTheWorker)
{
    //The guard is in JsonApi.cpp and nowhere else; HTTP reaches the same
    //buildJsonEventLog(), and its answer is synchronous for the same reason.
    seedEventLog();

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "eventlog" },
                                { "page", "0" }, { "per_page", "0" }}));

    ASSERT_EQ(1u, req.count())
            << "no synchronous answer over HTTP: the request reached the worker";
    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_EQ("per_page is out of range", str(req.bodyJson(), "error"));
    EXPECT_JSON_GOLDEN("t319_http_eventlog_per_page_out_of_range", req.body());
}
