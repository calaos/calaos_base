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

/* T2.15 — the async audio-player chain of JsonApi::buildJsonState().
 *
 * buildJsonState() builds the state of the audio players through six chained
 * asynchronous callbacks (get_playlist_current -> ... -> get_songinfo). The
 * answers come back from the network, so three things can die while a request
 * is in flight: the JsonApi (client disconnected), the AudioPlayer (IO deleted
 * through the API) and, before this ticket, one reference of the per-player
 * json object was lost at every state built (json_object_set instead of _new).
 *
 * The FakeAudioPlayer below stores the callbacks instead of answering, so the
 * tests control exactly *when* every answer arrives, with no loop and no
 * network. The refcount checks read json_t::refcount directly, which jansson
 * exposes in its public struct.
 */

#include "CalaosCoreFixture.h"

#include "JsonApi.h"
#include "AudioPlayer.h"
#include "ListeRoom.h"

#include <jansson.h>

#include <deque>

using namespace CalaosTest;
using namespace Calaos;

namespace
{

/* An AudioPlayer whose async getters never answer on their own: every request
 * is queued and the test fires (or drops) the answers one by one.
 */
class FakeAudioPlayer: public AudioPlayer
{
public:
    FakeAudioPlayer(Params &p): AudioPlayer(p) {}

    std::deque<AudioRequest_cb> pending;

    void get_playlist_current(AudioRequest_cb callback, AudioPlayerData = AudioPlayerData()) override
    { pending.push_back(callback); }
    void get_volume(AudioRequest_cb callback, AudioPlayerData = AudioPlayerData()) override
    { pending.push_back(callback); }
    void get_playlist_size(AudioRequest_cb callback, AudioPlayerData = AudioPlayerData()) override
    { pending.push_back(callback); }
    void get_current_time(AudioRequest_cb callback, AudioPlayerData = AudioPlayerData()) override
    { pending.push_back(callback); }
    void get_status(AudioRequest_cb callback, AudioPlayerData = AudioPlayerData()) override
    { pending.push_back(callback); }
    void get_songinfo(AudioRequest_cb callback, AudioPlayerData = AudioPlayerData()) override
    { pending.push_back(callback); }

    //Deliver the oldest pending answer
    bool fireNext(const AudioPlayerData &data = AudioPlayerData())
    {
        AudioRequest_cb cb = takeNext();
        if (cb.empty())
            return false;
        cb(data);
        return true;
    }

    /* Detach the oldest pending answer from the player. This is how a real
     * squeezebox connection behaves: the network object owns the callback and
     * can invoke it after the AudioPlayer IO was deleted.
     */
    AudioRequest_cb takeNext()
    {
        if (pending.empty())
            return AudioRequest_cb();
        AudioRequest_cb cb = pending.front();
        pending.pop_front();
        return cb;
    }
};

}

class JsonApiAudioStateTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig();
    }

    /* Register a FakeAudioPlayer in ListeRoom exactly like a config-loaded
     * IO: owned by the room (clearCoreState() deletes it) and reachable
     * through ListeRoom::get_io(). AudioPlayer's constructor sets
     * gui_type=audio_player itself, which is what buildJsonState() keys on.
     */
    FakeAudioPlayer *addFakePlayer(const std::string &id)
    {
        Params p;
        p.Add("id", id);
        p.Add("name", "fake " + id);
        p.Add("type", "FakeAudioPlayer");

        FakeAudioPlayer *player = new FakeAudioPlayer(p);
        firstRoom()->AddIO(player);
        ListeRoom::Instance().addIOHash(player);
        return player;
    }

    //Answer the full 6-callback chain of one player
    static void answerAll(FakeAudioPlayer *player)
    {
        AudioPlayerData d;
        d.ivalue = 3;
        ASSERT_TRUE(player->fireNext(d));   //playlist_current_track
        d.ivalue = 42;
        ASSERT_TRUE(player->fireNext(d));   //volume
        d.ivalue = 12;
        ASSERT_TRUE(player->fireNext(d));   //playlist_size
        d = AudioPlayerData();
        d.dvalue = 12.5;
        ASSERT_TRUE(player->fireNext(d));   //time_elapsed
        d = AudioPlayerData();
        d.ivalue = AudioStop;
        ASSERT_TRUE(player->fireNext(d));   //status
        d = AudioPlayerData();
        d.params.Add("title", "a title");
        d.params.Add("artist", "an artist");
        ASSERT_TRUE(player->fireNext(d));   //songinfo
    }
};

/* No audio player: the answer is synchronous, and the caller receives the
 * only reference (it json_decrefs it, as every call site does).
 */
TEST_F(JsonApiAudioStateTest, SyncStateHandsSingleReference)
{
    JsonApi api;

    json_t *jret = nullptr;
    api.buildJsonState({ ID_BOOL_IN, ID_INT }, [&](json_t *j) { jret = j; });

    ASSERT_NE(jret, nullptr);
    EXPECT_EQ(jret->refcount, (size_t)1);
    EXPECT_TRUE(json_is_string(json_object_get(jret, ID_BOOL_IN)));
    EXPECT_TRUE(json_is_string(json_object_get(jret, ID_INT)));
    json_decref(jret);
}

/* The leak of JsonApi.cpp (json_object_set instead of _new on the per-player
 * object): after a complete chain, every container must end up with exactly
 * one reference, the one held by its parent.
 */
TEST_F(JsonApiAudioStateTest, AudioStateLeaksNoReference)
{
    FakeAudioPlayer *player = addFakePlayer("audio_fake_1");
    JsonApi api;

    json_t *jret = nullptr;
    api.buildJsonState({ ID_BOOL_IN, "audio_fake_1" }, [&](json_t *j) { jret = j; });

    //All six requests are in flight, nothing answered yet
    EXPECT_EQ(jret, nullptr);
    answerAll(player);

    ASSERT_NE(jret, nullptr);
    EXPECT_EQ(jret->refcount, (size_t)1);

    json_t *jplayer = json_object_get(jret, "audio_fake_1");
    ASSERT_NE(jplayer, nullptr);
    //Before T2.15 this was 2: json_object_set leaked one reference per state
    EXPECT_EQ(jplayer->refcount, (size_t)1);

    json_t *jtrack = json_object_get(jplayer, "current_track");
    ASSERT_NE(jtrack, nullptr);
    EXPECT_EQ(jtrack->refcount, (size_t)1);

    EXPECT_STREQ(json_string_value(json_object_get(jplayer, "playlist_current_track")), "3");
    EXPECT_STREQ(json_string_value(json_object_get(jplayer, "volume")), "42");
    EXPECT_STREQ(json_string_value(json_object_get(jplayer, "playlist_size")), "12");
    EXPECT_STREQ(json_string_value(json_object_get(jplayer, "status")), "stop");
    EXPECT_STREQ(json_string_value(json_object_get(jtrack, "title")), "a title");

    json_decref(jret);
}

/* Client disconnected while the answers were in flight: the JsonApi is
 * destroyed, the late answers must not touch it (UAF before T2.15) and the
 * result lambda must never fire.
 */
TEST_F(JsonApiAudioStateTest, ClientGoneBeforeAnswerIsIgnored)
{
    FakeAudioPlayer *player = addFakePlayer("audio_fake_1");

    bool resultCalled = false;
    {
        JsonApi api;
        api.buildJsonState({ "audio_fake_1" }, [&](json_t *) { resultCalled = true; });
        //api dies here, exactly as when the WS client disconnects
    }

    //The squeezebox answer arrives afterwards
    EXPECT_TRUE(player->fireNext());
    //The guarded callback returned before chaining the next request
    EXPECT_TRUE(player->pending.empty());
    EXPECT_FALSE(resultCalled);
}

/* Player IO deleted through the API while its answer was in flight: the chain
 * aborts for that player but the answer still goes out, with the states of
 * everything else in it.
 */
TEST_F(JsonApiAudioStateTest, PlayerDeletedMidFlightStillAnswers)
{
    FakeAudioPlayer *player = addFakePlayer("audio_fake_1");
    JsonApi api;

    json_t *jret = nullptr;
    api.buildJsonState({ ID_BOOL_IN, "audio_fake_1" }, [&](json_t *j) { jret = j; });

    //The connection detaches the callback, then the IO is deleted
    AudioRequest_cb lateAnswer = player->takeNext();
    ASSERT_FALSE(lateAnswer.empty());
    ASSERT_TRUE(deleteIO(player));

    //The late answer must not dereference the dead player
    lateAnswer(AudioPlayerData());
    //Drop our copy of the chain (it owns a reference on the json being
    //built), exactly as the dying connection object would
    lateAnswer = AudioRequest_cb();

    ASSERT_NE(jret, nullptr);
    EXPECT_EQ(jret->refcount, (size_t)1);
    EXPECT_TRUE(json_is_string(json_object_get(jret, ID_BOOL_IN)));
    EXPECT_EQ(json_object_get(jret, "audio_fake_1"), nullptr);
    json_decref(jret);
}

/* Two players, one completes and one dies: the count of pending players must
 * still reach zero and release the answer once.
 */
TEST_F(JsonApiAudioStateTest, MixedCompletionAndDeletionAnswersOnce)
{
    FakeAudioPlayer *p1 = addFakePlayer("audio_fake_1");
    FakeAudioPlayer *p2 = addFakePlayer("audio_fake_2");
    JsonApi api;

    int resultCount = 0;
    json_t *jret = nullptr;
    api.buildJsonState({ "audio_fake_1", "audio_fake_2" }, [&](json_t *j)
    {
        resultCount++;
        jret = j;
    });

    AudioRequest_cb lateAnswer = p2->takeNext();
    ASSERT_TRUE(deleteIO(p2));
    lateAnswer(AudioPlayerData());
    lateAnswer = AudioRequest_cb(); //drop our reference-holding copy
    EXPECT_EQ(resultCount, 0);

    answerAll(p1);

    EXPECT_EQ(resultCount, 1);
    ASSERT_NE(jret, nullptr);
    EXPECT_EQ(jret->refcount, (size_t)1);
    EXPECT_NE(json_object_get(jret, "audio_fake_1"), nullptr);
    EXPECT_EQ(json_object_get(jret, "audio_fake_2"), nullptr);
    json_decref(jret);
}
