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

/* T3.17a - the recursive playlist chain: JsonApi::decodeGetPlaylist() and
 * JsonApi::getNextPlaylistItem().
 *
 * WHY THIS FILE EXISTS AT ALL
 * ---------------------------
 * T3.17 adds a lifetime guard (the apiAlive weak_ptr token of T1.10/T2.15) to
 * ~22 JsonApi methods whose async callbacks capture `this` and a raw
 * AudioPlayer* across real network round trips. The E4.0 characterization plan
 * covers most of that surface - but NOT get_playlist: the operation is listed
 * among E4.0's common operations (E4.0.md:38) and then assigned to no
 * sub-ticket at all. So this chain has no net, now or later, and T3.17a has to
 * bring its own before touching the code.
 *
 * THE RISK THIS FILE IS AIMED AT, and it is not the use-after-free.
 * getNextPlaylistItem() recurses: one network round trip per track. A guard
 * dropped in naively either bails out of an intermediate stage (the client
 * gets a playlist SILENTLY MISSING its tail) or only covers the first stage
 * (the recursion stays bare). Neither crashes, neither logs. The cases below
 * pin the WHOLE answer - the exact item count, every index requested, and the
 * content and order of every item - so a truncation cannot pass unnoticed.
 *
 * The cases were written and made green BEFORE the guard was added, and are
 * unchanged by it. Any later edit that makes one of them fail is describing a
 * behaviour change, not a test to update.
 *
 * ON THE HARNESS: the E4.0a harness (JsonApiCharacterization.h) drives the two
 * real transports end to end and gives the semantic JSON oracle. Read its
 * header before touching anything here, in particular the "TRAPS" section.
 * Two of the three traps are actively avoided here: no HttpTestRequest is kept
 * alive across a pump, and the deferred cases use a WS session (no HttpClient
 * read timer at all).
 *
 * ON THE PLAYER: the reference house of the harness carries a RoonPlayer, whose
 * playlist getters never answer without a live Roon helper. A local fake is
 * used instead, in two modes - immediate (the whole recursion unrolls inside
 * processApi(), which is what the goldens capture) and deferred (the test
 * fires the answers one at a time, which is what the lifetime cases need).
 *
 * IO ids are prefixed t317a_ and used nowhere else: Config's IO state cache is
 * process wide and never cleared (see CalaosCoreFixture.h).
 */

#include "JsonApiCharacterization.h"

#include "AudioPlayer.h"
#include "ListeRoom.h"
#include "Utils.h"

#include <deque>
#include <string>
#include <vector>

using namespace CalaosTest;
using namespace Calaos;

namespace
{

const char *const PLAYER_ID = "t317a_player";

/* An AudioPlayer with a deterministic, arbitrarily long playlist.
 *
 * immediate mode (default): every getter answers inside the call, so a whole
 * get_playlist unrolls synchronously and the response is available as soon as
 * processApi() returns.
 *
 * deferred mode: every getter queues its callback WITH the answer it would
 * have given, and the test decides when - or whether - each one fires. That is
 * how a real squeezebox/roon connection behaves: the connection object owns the
 * callback and can invoke it long after the JsonApi, or the AudioPlayer IO
 * itself, is gone.
 */
class FakePlaylistPlayer: public AudioPlayer
{
public:
    FakePlaylistPlayer(Params &p): AudioPlayer(p) {}

    bool deferred = false;
    int currentTrack = 0;
    int playlistSize = 0;

    //Indices get_playlist_item() was actually asked for, in order. This is the
    //anti-truncation probe: the recursion must ask for 0..playlistSize-1.
    std::vector<int> requestedItems;

    void get_playlist_current(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        AudioPlayerData d;
        d.ivalue = currentTrack;
        answer(cb, d);
    }

    void get_playlist_size(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        AudioPlayerData d;
        d.ivalue = playlistSize;
        answer(cb, d);
    }

    void get_playlist_item(int index, AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        requestedItems.push_back(index);
        answer(cb, itemData(index));
    }

    bool canPlaylist() override { return true; }

    //The answer the player would have given for track `index`.
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

    /* --- deferred mode driving -------------------------------------------- */

    size_t pendingCount() const { return pending.size(); }

    //Deliver the oldest queued answer. False when nothing is queued.
    bool fireNext()
    {
        if (pending.empty())
            return false;
        Pending p = pending.front();
        pending.pop_front();
        p.cb(p.data);
        return true;
    }

    //Detach the oldest queued answer from the player, so it can be fired after
    //the player itself has been deleted.
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

    void answer(const AudioRequest_cb &cb, const AudioPlayerData &d)
    {
        if (deferred)
            pending.push_back({ cb, d });
        else
            cb(d);
    }
};

//The expected items of a playlist of `count` tracks, as a JSON array, built
//from the same source of truth the fake answers with. Used by the cases that
//assert the FULL list rather than a golden.
Json expectedItems(int count)
{
    Json items = Json::array();
    for (int i = 0; i < count; i++)
    {
        const std::string idx = Utils::to_string(i);
        items.push_back(Json{
                            { "id", "track_" + idx },
                            { "title", "Title " + idx },
                            { "artist", "Artist " + idx },
                            { "album", "Album " + idx },
                            { "duration", Utils::to_string(100 + i) },
                        });
    }
    return items;
}

}

class JsonApiPlaylistTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();

        forgetIOState(PLAYER_ID);

        loadConfig();
    }

    /* Register a fake player in ListeRoom exactly like a config loaded IO:
     * owned by its room (clearCoreState() deletes it) and reachable through
     * ListeRoom::get_io(), which is what decodeGetPlaylist() looks it up with.
     */
    static FakePlaylistPlayer *addPlayer(int size, int currentTrack = 0)
    {
        Params p;
        p.Add("id", PLAYER_ID);
        p.Add("name", "Fake playlist player");
        p.Add("type", "FakePlaylistPlayer");

        FakePlaylistPlayer *player = new FakePlaylistPlayer(p);
        player->playlistSize = size;
        player->currentTrack = currentTrack;

        firstRoom()->AddIO(player);
        ListeRoom::Instance().addIOHash(player);
        return player;
    }

    static Json wsRequest(const std::string &id)
    {
        return Json{
            { "msg", "get_playlist" },
            { "msg_id", "1" },
            { "data", {{ "id", id }} },
        };
    }

    static Json httpRequest(const std::string &id)
    {
        return authenticated(Json{
                                 { "action", "get_playlist" },
                                 { "id", id },
                             });
    }
};

/*******************************************************************************
 * The nominal answer, both transports. These two goldens are the reference
 * shape of get_playlist: current_track, count, and one object per track.
 ******************************************************************************/

TEST_F(JsonApiPlaylistTest, WsGetPlaylist)
{
    FakePlaylistPlayer *player = addPlayer(3, 1);

    WsTestSession ws;
    ws.send(wsRequest(PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317a_ws_get_playlist", ws.lastMessage());

    //The recursion asked for every index, once, in order.
    EXPECT_EQ(std::vector<int>({ 0, 1, 2 }), player->requestedItems);
}

TEST_F(JsonApiPlaylistTest, HttpGetPlaylist)
{
    FakePlaylistPlayer *player = addPlayer(3, 1);

    HttpTestRequest req;
    req.send(httpRequest(PLAYER_ID));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_JSON_GOLDEN("t317a_http_get_playlist", req.body());

    EXPECT_EQ(std::vector<int>({ 0, 1, 2 }), player->requestedItems);
}

/*******************************************************************************
 * The two degenerate answers.
 ******************************************************************************/

//count <= 0 short circuits the recursion entirely: items is an empty array and
//get_playlist_item() is never called.
TEST_F(JsonApiPlaylistTest, WsGetPlaylistEmpty)
{
    FakePlaylistPlayer *player = addPlayer(0);

    WsTestSession ws;
    ws.send(wsRequest(PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317a_ws_get_playlist_empty", ws.lastMessage());
    EXPECT_TRUE(player->requestedItems.empty());
}

//An id that is not an AudioPlayer answers success:false, synchronously. Note
//the answer carries NO current_track/count/items at all.
TEST_F(JsonApiPlaylistTest, WsGetPlaylistUnknownPlayer)
{
    //ID_BOOL_IN exists (SetUp loaded the minimal config) but is an
    //InternalBool, so the dynamic_cast to AudioPlayer fails.
    WsTestSession ws;
    ws.send(wsRequest(ID_BOOL_IN));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("t317a_ws_get_playlist_unknown_player", ws.lastMessage());
}

TEST_F(JsonApiPlaylistTest, HttpGetPlaylistUnknownPlayer)
{
    HttpTestRequest req;
    req.send(httpRequest("t317a_no_such_io"));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_JSON_GOLDEN("t317a_http_get_playlist_unknown_player", req.body());
}

/*******************************************************************************
 * NON TRUNCATION - the reason this file exists.
 *
 * A long playlist, answered one round trip at a time, so the recursion really
 * is exercised as N distinct async stages and not as one synchronous unroll.
 * Every index must be requested, every item must be in the answer, in order.
 ******************************************************************************/

TEST_F(JsonApiPlaylistTest, LongPlaylistIsAnsweredWhole)
{
    const int SIZE = 7;
    FakePlaylistPlayer *player = addPlayer(SIZE, 4);
    player->deferred = true;

    WsTestSession ws;
    ws.send(wsRequest(PLAYER_ID));

    //Nothing answered yet: only get_playlist_current is in flight.
    EXPECT_EQ(0u, ws.count());
    ASSERT_EQ(1u, player->pendingCount());

    ASSERT_TRUE(player->fireNext());        //current track
    ASSERT_TRUE(player->fireNext());        //playlist size

    //One stage per track, each one chained by the previous answer.
    for (int i = 0; i < SIZE; i++)
    {
        ASSERT_EQ(1u, player->pendingCount()) << "at stage " << i;
        EXPECT_EQ(0u, ws.count()) << "answered before the last track, at stage " << i;
        ASSERT_TRUE(player->fireNext());
    }

    EXPECT_EQ(0u, player->pendingCount());
    ASSERT_EQ(1u, ws.count());

    std::vector<int> allIndices;
    for (int i = 0; i < SIZE; i++)
        allIndices.push_back(i);
    EXPECT_EQ(allIndices, player->requestedItems);

    const Json data = ws.lastData();
    ASSERT_TRUE(data.is_object());
    EXPECT_JSON_EQ(Json("4"), data.value("current_track", Json()));
    EXPECT_JSON_EQ(Json("7"), data.value("count", Json()));
    EXPECT_JSON_EQ(expectedItems(SIZE), data.value("items", Json()));
}

/* Same shape, immediate mode, on a size that would hide an off-by-one in the
 * recursion tail (the last item is appended by the branch that also answers).
 */
TEST_F(JsonApiPlaylistTest, SingleTrackPlaylistIsAnsweredWhole)
{
    FakePlaylistPlayer *player = addPlayer(1, 0);

    WsTestSession ws;
    ws.send(wsRequest(PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ(std::vector<int>({ 0 }), player->requestedItems);

    const Json data = ws.lastData();
    ASSERT_TRUE(data.is_object());
    EXPECT_JSON_EQ(Json("1"), data.value("count", Json()));
    EXPECT_JSON_EQ(expectedItems(1), data.value("items", Json()));
}

/*******************************************************************************
 * LIFETIME - what T3.17a actually fixes.
 *
 * Every stage of the chain is a real network round trip. Two things can die in
 * between: the JsonApi (the client disconnected, HttpClient.cpp:162 deletes the
 * handler, base sub-object included) and the AudioPlayer IO (deleted through
 * the API). Before T3.17a the late answers dereferenced both.
 *
 * These three cases run clean under ASan; before the guard, each of them is a
 * heap-use-after-free.
 ******************************************************************************/

/* Client gone while the answers were in flight. The late answers must not touch
 * the dead handler, the chain must stop, and result_lambda must never fire.
 */
TEST_F(JsonApiPlaylistTest, ClientGoneMidChainIsIgnored)
{
    FakePlaylistPlayer *player = addPlayer(3, 0);
    player->deferred = true;

    {
        WsTestSession ws;
        ws.send(wsRequest(PLAYER_ID));
        ASSERT_EQ(1u, player->pendingCount());
        //ws dies here, exactly as when the client disconnects
    }

    //Drain everything the connection object still holds. Before T3.17a this
    //walked the whole recursion and ended on result_lambda, i.e. sendJson() on
    //the freed handler - ASan reports it as a heap-use-after-free.
    int fired = 0;
    while (player->fireNext())
        fired++;

    //Only the answer that was already in flight ran, and it chained nothing
    EXPECT_EQ(1, fired);
    EXPECT_TRUE(player->requestedItems.empty());
}

/* Same, but the client leaves in the middle of the RECURSION rather than at the
 * first stage: the guard has to be on every stage, not only on the entry one.
 */
TEST_F(JsonApiPlaylistTest, ClientGoneMidRecursionIsIgnored)
{
    FakePlaylistPlayer *player = addPlayer(5, 0);
    player->deferred = true;

    {
        WsTestSession ws;
        ws.send(wsRequest(PLAYER_ID));
        ASSERT_TRUE(player->fireNext());     //current track
        ASSERT_TRUE(player->fireNext());     //playlist size -> item 0 in flight
        ASSERT_TRUE(player->fireNext());     //item 0        -> item 1 in flight
        ASSERT_EQ(std::vector<int>({ 0, 1 }), player->requestedItems);
        ASSERT_EQ(1u, player->pendingCount());
        //ws dies with two tracks still to fetch
    }

    int fired = 0;
    while (player->fireNext())
        fired++;

    //Item 1 answered to nobody and chained nothing
    EXPECT_EQ(1, fired);
    //No further track was requested: the recursion stopped at the guard
    EXPECT_EQ(std::vector<int>({ 0, 1 }), player->requestedItems);
}

/* The player IO is deleted through the API while an answer is in flight. The
 * client is still connected, so it MUST get an answer (T3.17 invariant: no
 * silent disappearance) - and that answer must NOT be a playlist missing its
 * tail. It is the same success:false the entry point already answers for an id
 * that is not a player.
 */
TEST_F(JsonApiPlaylistTest, PlayerDeletedMidRecursionAnswersFalse)
{
    FakePlaylistPlayer *player = addPlayer(5, 0);
    player->deferred = true;

    WsTestSession ws;
    ws.send(wsRequest(PLAYER_ID));

    ASSERT_TRUE(player->fireNext());        //current track
    ASSERT_TRUE(player->fireNext());        //playlist size -> item 0 in flight
    ASSERT_TRUE(player->fireNext());        //item 0        -> item 1 in flight
    ASSERT_EQ(std::vector<int>({ 0, 1 }), player->requestedItems);

    //The connection object owns the pending answer and outlives the IO
    std::function<void()> lateAnswer = player->takeNext();
    ASSERT_TRUE((bool)lateAnswer);
    ASSERT_TRUE(deleteIO(player));
    player = nullptr;

    EXPECT_EQ(0u, ws.count());
    lateAnswer();
    lateAnswer = std::function<void()>();

    ASSERT_EQ(1u, ws.count());
    const Json data = ws.lastData();
    ASSERT_TRUE(data.is_object());
    EXPECT_JSON_EQ(Json("false"), data.value("success", Json()));
    //A truncated playlist would be the silent failure this ticket is about
    EXPECT_TRUE(data.find("items") == data.end());
}

/* The player disappears at the very first stage, before the size is even known.
 */
TEST_F(JsonApiPlaylistTest, PlayerDeletedAtFirstStageAnswersFalse)
{
    FakePlaylistPlayer *player = addPlayer(3, 0);
    player->deferred = true;

    WsTestSession ws;
    ws.send(wsRequest(PLAYER_ID));

    std::function<void()> lateAnswer = player->takeNext();
    ASSERT_TRUE((bool)lateAnswer);
    ASSERT_TRUE(deleteIO(player));
    player = nullptr;

    lateAnswer();
    lateAnswer = std::function<void()>();

    ASSERT_EQ(1u, ws.count());
    const Json data = ws.lastData();
    ASSERT_TRUE(data.is_object());
    EXPECT_JSON_EQ(Json("false"), data.value("success", Json()));
}
