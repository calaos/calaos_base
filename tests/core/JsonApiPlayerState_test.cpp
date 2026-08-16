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

/* T3.17b - the five single-shot player-state methods of JsonApi:
 *
 *      audioGetDbStats       (audio_db / get_stats)
 *      audioGetPlaylistSize  (audio / get_playlist_size)
 *      audioGetTime          (audio / get_time)
 *      audioGetPlaylistItem  (audio / get_playlist_item)
 *      audioGetCoverInfo     (audio / get_cover_url)
 *
 * Ten call sites: each of the five is dispatched from both transports
 * (JsonApiHandlerHttp.cpp:692,697,702,707,787 and
 *  JsonApiHandlerWS.cpp:354,359,364,369,386).
 *
 * WHY THIS FILE EXISTS AT ALL
 * ---------------------------
 * Same reason as T3.17a's JsonApiPlaylist_test.cpp: T3.17b adds the apiAlive
 * lifetime guard to these five methods, and the E4.0 characterization series
 * that was supposed to cover them (E4.0f, "Audio et base musicale") is neither
 * delivered nor started. Guarding an async path with no net is how a response
 * silently stops being sent, so the net comes first.
 *
 * Unlike the playlist chain these five are SINGLE SHOT - one request, one
 * network round trip, one answer, no recursion. The failure mode a badly placed
 * guard produces here is therefore not a truncated list but a missing answer,
 * which is what the cases below pin: for every method, both transports, the
 * exact answer document.
 *
 * The cases in the first commit of T3.17b were written and made green BEFORE
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
 * ON THE PLAYER: the reference house of the harness carries a RoonPlayer, whose
 * getters never answer without a live Roon helper, and no AudioDB at all. A
 * local fake is used instead, in two modes - immediate (the answer comes back
 * inside processApi(), which is what the goldens capture) and deferred (the
 * test decides when, or whether, each answer fires, which is what the lifetime
 * cases need).
 *
 * ON THE ANSWER QUEUE: it belongs to the FIXTURE, not to the player. A real
 * squeezebox/roon connection object owns the pending callback and outlives the
 * IO, so a queue living inside the player could not express "the IO was deleted
 * and the answer arrived afterwards" - the case this ticket is about.
 *
 * IO ids and goldens are prefixed t317b_ and used nowhere else: Config's IO
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

using namespace CalaosTest;
using namespace Calaos;

namespace
{

const char *const PLAYER_ID = "t317b_player";

/* The pending answers of a player, held OUTSIDE the player (see the header
 * note). In immediate mode a getter answers inside the call; in deferred mode
 * it queues the callback together with the answer it would have given.
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

//The music database behind the fake player. Only getStats() is in the T3.17b
//perimeter; the other 15 getters belong to T3.17c.
class FakeStateDb: public AudioDB
{
public:
    FakeStateDb(Params &p, AnswerQueue &q): AudioDB(p), queue(q) {}

    void getStats(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        AudioPlayerData d;
        d.params.Add("albums", "12");
        d.params.Add("artists", "7");
        d.params.Add("songs", "134");
        queue.answer(cb, d);
    }

private:
    AnswerQueue &queue;
};

class FakeStatePlayer: public AudioPlayer
{
public:
    FakeStatePlayer(Params &p, AnswerQueue &q):
        AudioPlayer(p),
        db(dbParams, q),
        queue(q)
    {
        database = &db;
    }

    int playlistSize = 12;
    double currentTime = 12.5;
    std::string coverUrl = "http://calaos.fr/cover_t317b.jpg";

    //Indices get_playlist_item() was actually asked for, in order.
    std::vector<int> requestedItems;

    void get_playlist_size(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        AudioPlayerData d;
        d.ivalue = playlistSize;
        queue.answer(cb, d);
    }

    void get_current_time(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        AudioPlayerData d;
        d.dvalue = currentTime;
        queue.answer(cb, d);
    }

    void get_playlist_item(int index, AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        requestedItems.push_back(index);
        queue.answer(cb, itemData(index));
    }

    void get_album_cover(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        AudioPlayerData d;
        d.svalue = coverUrl;
        queue.answer(cb, d);
    }

    bool canPlaylist() override { return true; }
    bool canDatabase() override { return true; }

    static AudioPlayerData itemData(int index)
    {
        const std::string idx = Utils::to_string(index);
        AudioPlayerData d;
        d.params.Add("id", "track_" + idx);
        d.params.Add("title", "Title " + idx);
        d.params.Add("artist", "Artist " + idx);
        d.params.Add("album", "Album " + idx);
        d.params.Add("duration", Utils::to_string(100 + index));
        return d;
    }

private:
    Params dbParams;
    FakeStateDb db;
    AnswerQueue &queue;
};

}

class JsonApiPlayerStateTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();

        forgetIOState(PLAYER_ID);

        loadConfig();
    }

    AnswerQueue queue;

    /* Register a fake player in ListeRoom exactly like a config loaded IO:
     * owned by its room (clearCoreState() deletes it) and reachable through
     * ListeRoom::get_io(), which is what getAudioPlayer() looks it up with.
     */
    FakeStatePlayer *addPlayer()
    {
        Params p;
        p.Add("id", PLAYER_ID);
        p.Add("name", "Fake state player");
        p.Add("type", "FakeStatePlayer");

        FakeStatePlayer *player = new FakeStatePlayer(p, queue);

        firstRoom()->AddIO(player);
        ListeRoom::Instance().addIOHash(player);
        return player;
    }

    //{"msg":"audio","msg_id":"1","data":{"id":..., "audio_action":...}}
    static Json wsRequest(const std::string &msg, const std::string &action,
                          const std::string &id, Json extra = Json::object())
    {
        Json data = Json{{ "id", id }, { "audio_action", action }};
        for (auto it = extra.begin(); it != extra.end(); ++it)
            data[it.key()] = it.value();

        return Json{
            { "msg", msg },
            { "msg_id", "1" },
            { "data", data },
        };
    }

    //HTTP puts everything flat in the request body.
    static Json httpRequest(const std::string &action, const std::string &audioAction,
                            const std::string &id, Json extra = Json::object())
    {
        Json body = Json{
            { "action", action },
            { "audio_action", audioAction },
            { "id", id },
        };
        for (auto it = extra.begin(); it != extra.end(); ++it)
            body[it.key()] = it.value();

        return authenticated(body);
    }
};

/*******************************************************************************
 * THE NOMINAL ANSWER OF EACH OF THE FIVE METHODS, ON BOTH TRANSPORTS.
 *
 * These ten goldens are the reference shape the guard must leave untouched.
 * Note two things they record as they are, on purpose (see the KNOWN
 * DIVERGENCES policy of JsonApiCharacterization.h):
 *   - get_playlist_size and get_time add "audio_action" to the player's answer
 *     params and then build a FRESH Params for the response, so that
 *     audio_action never reaches the client (JsonApi.cpp:967-972, :987-992).
 *     The WS envelope carries the "audio" msg name, the HTTP body carries
 *     nothing but the single value.
 *   - get_playlist_item and get_cover_url do not add it at all.
 ******************************************************************************/

TEST_F(JsonApiPlayerStateTest, WsAudioGetPlaylistSize)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_playlist_size", PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317b_ws_audio_get_playlist_size", ws.lastMessage());
}

TEST_F(JsonApiPlayerStateTest, HttpAudioGetPlaylistSize)
{
    addPlayer();

    HttpTestRequest req;
    req.send(httpRequest("audio", "get_playlist_size", PLAYER_ID));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_JSON_GOLDEN("t317b_http_audio_get_playlist_size", req.body());
}

TEST_F(JsonApiPlayerStateTest, WsAudioGetTime)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_time", PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317b_ws_audio_get_time", ws.lastMessage());
}

TEST_F(JsonApiPlayerStateTest, HttpAudioGetTime)
{
    addPlayer();

    HttpTestRequest req;
    req.send(httpRequest("audio", "get_time", PLAYER_ID));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_JSON_GOLDEN("t317b_http_audio_get_time", req.body());
}

TEST_F(JsonApiPlayerStateTest, WsAudioGetPlaylistItem)
{
    FakeStatePlayer *player = addPlayer();

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_playlist_item", PLAYER_ID, Json{{ "item", "3" }}));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317b_ws_audio_get_playlist_item", ws.lastMessage());
    EXPECT_EQ(std::vector<int>({ 3 }), player->requestedItems);
}

TEST_F(JsonApiPlayerStateTest, HttpAudioGetPlaylistItem)
{
    FakeStatePlayer *player = addPlayer();

    HttpTestRequest req;
    req.send(httpRequest("audio", "get_playlist_item", PLAYER_ID, Json{{ "item", "3" }}));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_JSON_GOLDEN("t317b_http_audio_get_playlist_item", req.body());
    EXPECT_EQ(std::vector<int>({ 3 }), player->requestedItems);
}

TEST_F(JsonApiPlayerStateTest, WsAudioGetCoverUrl)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_cover_url", PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317b_ws_audio_get_cover_url", ws.lastMessage());
}

TEST_F(JsonApiPlayerStateTest, HttpAudioGetCoverUrl)
{
    addPlayer();

    HttpTestRequest req;
    req.send(httpRequest("audio", "get_cover_url", PLAYER_ID));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_JSON_GOLDEN("t317b_http_audio_get_cover_url", req.body());
}

//get_stats is dispatched from processAudioDb, not processAudio, but the method
//it reaches (audioGetDbStats) is a player-state single shot like the four
//above - hence its place in T3.17b rather than T3.17c.
TEST_F(JsonApiPlayerStateTest, WsAudioDbGetStats)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsRequest("audio_db", "get_stats", PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317b_ws_audio_db_get_stats", ws.lastMessage());
}

TEST_F(JsonApiPlayerStateTest, HttpAudioDbGetStats)
{
    addPlayer();

    HttpTestRequest req;
    req.send(httpRequest("audio_db", "get_stats", PLAYER_ID));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_JSON_GOLDEN("t317b_http_audio_db_get_stats", req.body());
}

/*******************************************************************************
 * THE SYNCHRONOUS ERROR ANSWERS.
 *
 * getAudioPlayer() (JsonApi.cpp:918) is the single entry gate of the five
 * methods and answers before any round trip. These are the answers a guard
 * MUST keep giving, and - for the deleted-player case further down - the exact
 * wording a late stage would have to reuse if it ever had to give up.
 ******************************************************************************/

//An id that exists but is not an AudioPlayer. Note the misspelling of
//"unkown player_id": it is production behaviour, frozen as is.
TEST_F(JsonApiPlayerStateTest, WsAudioUnknownPlayerId)
{
    WsTestSession ws;
    ws.send(wsRequest("audio", "get_playlist_size", ID_BOOL_IN));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317b_ws_audio_unknown_player", ws.lastMessage());
}

TEST_F(JsonApiPlayerStateTest, HttpAudioUnknownPlayerId)
{
    HttpTestRequest req;
    req.send(httpRequest("audio", "get_time", "t317b_no_such_io"));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_JSON_GOLDEN("t317b_http_audio_unknown_player", req.body());
}

//An absent id is a different message from an unknown one.
TEST_F(JsonApiPlayerStateTest, WsAudioEmptyPlayerId)
{
    WsTestSession ws;
    ws.send(wsRequest("audio", "get_cover_url", ""));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317b_ws_audio_empty_player_id", ws.lastMessage());
}

TEST_F(JsonApiPlayerStateTest, WsAudioDbGetStatsUnknownPlayerId)
{
    WsTestSession ws;
    ws.send(wsRequest("audio_db", "get_stats", ID_BOOL_IN));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317b_ws_audio_db_get_stats_unknown_player", ws.lastMessage());
}

//get_playlist_item has a second gate of its own, after the player lookup.
TEST_F(JsonApiPlayerStateTest, WsAudioGetPlaylistItemMissingItem)
{
    FakeStatePlayer *player = addPlayer();

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_playlist_item", PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317b_ws_audio_get_playlist_item_missing_item", ws.lastMessage());
    EXPECT_TRUE(player->requestedItems.empty());
}

TEST_F(JsonApiPlayerStateTest, WsAudioGetPlaylistItemNotANumber)
{
    FakeStatePlayer *player = addPlayer();

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_playlist_item", PLAYER_ID, Json{{ "item", "third" }}));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317b_ws_audio_get_playlist_item_missing_item", ws.lastMessage());
    EXPECT_TRUE(player->requestedItems.empty());
}

/*******************************************************************************
 * ONE ROUND TRIP, ANSWERED LATE.
 *
 * Same nominal answers, but driven the way the real transport behaves: the
 * request goes out, processApi() returns with nothing sent, and the answer
 * arrives later. This is the shape every lifetime case builds on, and on its
 * own it is the "object alive for the whole request" invariant of T3.17: the
 * guard must not swallow an answer whose client is still there.
 ******************************************************************************/

TEST_F(JsonApiPlayerStateTest, DeferredAnswerStillReachesALiveClient)
{
    addPlayer();
    queue.deferred = true;

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_playlist_size", PLAYER_ID));

    //Nothing answered yet: the request is in flight
    EXPECT_EQ(0u, ws.count());
    ASSERT_EQ(1u, queue.count());

    ASSERT_TRUE(queue.fireNext());

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317b_ws_audio_get_playlist_size", ws.lastMessage());
}

/*******************************************************************************
 * THE PLAYER IO DELETED WHILE ITS ANSWER IS IN FLIGHT.
 *
 * T3.17 (section "le bug est DOUBLE") states that these methods, like the
 * playlist chain, dereference a raw AudioPlayer* after the round trip. MEASURED
 * HERE, AND IT IS NOT THE CASE FOR THESE FIVE: not one of the five callback
 * bodies names `player` (JsonApi.cpp:950-954, :969-974, :989-994, :1019-1022,
 * :1036-1040), so the [=] default does not even capture it - a lambda capture
 * default only captures what the body odr-uses. The player is dereferenced
 * exactly once, synchronously, BEFORE the async call is issued.
 *
 * The observable consequence is these five cases: with the client still there,
 * a late answer whose IO is gone is delivered in full, from the data the answer
 * already carried. There is nothing to re-resolve and nothing that would be
 * mutilated, so T3.17b keeps this behaviour instead of turning it into an
 * error. They are green before the guard and after it, and clean under ASan in
 * both states - which is the proof that the second UAF of T3.17 has no
 * instance in this sub-ticket's perimeter.
 ******************************************************************************/

TEST_F(JsonApiPlayerStateTest, PlaylistSizePlayerDeletedMidFlightStillAnswers)
{
    FakeStatePlayer *player = addPlayer();
    queue.deferred = true;

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_playlist_size", PLAYER_ID));

    //The connection object owns the pending answer and outlives the IO
    std::function<void()> lateAnswer = queue.takeNext();
    ASSERT_TRUE((bool)lateAnswer);
    ASSERT_TRUE(deleteIO(player));
    player = nullptr;

    EXPECT_EQ(0u, ws.count());
    lateAnswer();
    lateAnswer = std::function<void()>();

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317b_ws_audio_get_playlist_size", ws.lastMessage());
}

TEST_F(JsonApiPlayerStateTest, TimePlayerDeletedMidFlightStillAnswers)
{
    FakeStatePlayer *player = addPlayer();
    queue.deferred = true;

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_time", PLAYER_ID));

    std::function<void()> lateAnswer = queue.takeNext();
    ASSERT_TRUE((bool)lateAnswer);
    ASSERT_TRUE(deleteIO(player));
    player = nullptr;

    lateAnswer();
    lateAnswer = std::function<void()>();

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317b_ws_audio_get_time", ws.lastMessage());
}

TEST_F(JsonApiPlayerStateTest, PlaylistItemPlayerDeletedMidFlightStillAnswers)
{
    FakeStatePlayer *player = addPlayer();
    queue.deferred = true;

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_playlist_item", PLAYER_ID, Json{{ "item", "3" }}));

    std::function<void()> lateAnswer = queue.takeNext();
    ASSERT_TRUE((bool)lateAnswer);
    ASSERT_TRUE(deleteIO(player));
    player = nullptr;

    lateAnswer();
    lateAnswer = std::function<void()>();

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317b_ws_audio_get_playlist_item", ws.lastMessage());
}

TEST_F(JsonApiPlayerStateTest, CoverInfoPlayerDeletedMidFlightStillAnswers)
{
    FakeStatePlayer *player = addPlayer();
    queue.deferred = true;

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_cover_url", PLAYER_ID));

    std::function<void()> lateAnswer = queue.takeNext();
    ASSERT_TRUE((bool)lateAnswer);
    ASSERT_TRUE(deleteIO(player));
    player = nullptr;

    lateAnswer();
    lateAnswer = std::function<void()>();

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317b_ws_audio_get_cover_url", ws.lastMessage());
}

//The AudioDB is a member of the player and dies with it, so this case also
//covers "the database the request was issued to is gone".
TEST_F(JsonApiPlayerStateTest, DbStatsPlayerDeletedMidFlightStillAnswers)
{
    FakeStatePlayer *player = addPlayer();
    queue.deferred = true;

    WsTestSession ws;
    ws.send(wsRequest("audio_db", "get_stats", PLAYER_ID));

    std::function<void()> lateAnswer = queue.takeNext();
    ASSERT_TRUE((bool)lateAnswer);
    ASSERT_TRUE(deleteIO(player));
    player = nullptr;

    lateAnswer();
    lateAnswer = std::function<void()>();

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317b_ws_audio_db_get_stats", ws.lastMessage());
}
