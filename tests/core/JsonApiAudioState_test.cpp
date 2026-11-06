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
 * network.
 *
 * ---------------------------------------------------------------------------
 * E4.1n - WHY THE REFCOUNT ASSERTIONS ARE GONE, AND WHAT REPLACED THEM
 * ---------------------------------------------------------------------------
 * They used to read the reference counter of the jansson object directly (it is
 * public in that struct) because the T2.15 defect WAS a reference count:
 * json_object_set instead of _new leaked one per state built. buildJsonState()
 * answers a Json by value now, and NO SUCH COUNTER EXISTS ANY MORE - the
 * containers own their subtrees and the caller owns its copy.
 *
 * The ticket's acceptance asked for these cases "green without modifying an
 * assertion". THAT IS NOT POSSIBLE and pretending otherwise would have been
 * worse than saying so: an assertion on a field of a type no longer involved
 * cannot survive. What those three assertions really pinned was "the document
 * handed to the caller is complete, and holding it keeps nothing else alive";
 * that is what the replacements assert, structurally, plus the string typing of
 * every value - which a port could have broken with no golden noticing.
 *
 * The two cases that matter most are UNCHANGED in substance and are the ones to
 * watch: ClientGoneBeforeAnswerIsIgnored (the UAF T2.15 fixed) and
 * MixedCompletionAndDeletionAnswersOnce (the answer released exactly once).
 * Neither ever looked at a reference count.
 */

#include "CalaosCoreFixture.h"

#include "JsonApi.h"
#include "AudioPlayer.h"
#include "ListeRoom.h"

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

/* No audio player: the answer is synchronous, and the caller receives a
 * complete document by value.
 *
 * The is_object() check is not decoration: a default-constructed Json is null,
 * not {}, so this case also guards the shape of the container the builder
 * starts from.
 */
TEST_F(JsonApiAudioStateTest, SyncStateHandsACompleteDocument)
{
    JsonApi api;

    bool called = false;
    Json jret;
    api.buildJsonState({ ID_BOOL_IN, ID_INT }, [&](Json j) { jret = j; called = true; });

    ASSERT_TRUE(called);
    ASSERT_TRUE(jret.is_object()) << jret.dump();
    EXPECT_EQ(2u, jret.size()) << jret.dump();
    ASSERT_TRUE(jret.contains(ID_BOOL_IN));
    ASSERT_TRUE(jret.contains(ID_INT));
    EXPECT_TRUE(jret[ID_BOOL_IN].is_string()) << jret.dump();
    EXPECT_TRUE(jret[ID_INT].is_string()) << jret.dump();
}

/* What the reference-count assertions of T2.15 really pinned, said
 * structurally: after a complete chain the caller holds the WHOLE document,
 * nested subtrees included, and every value is a STRING (the type-strict
 * oracle: 3 != "3").
 *
 * The int and the double both leave as strings - "42" and "12.5" - which is
 * exactly the contract Utils::to_string() carries and which a port to nlohmann
 * could have broken without a single golden noticing.
 */
TEST_F(JsonApiAudioStateTest, AudioStateHandsTheWholeNestedDocument)
{
    FakeAudioPlayer *player = addFakePlayer("audio_fake_1");
    JsonApi api;

    bool called = false;
    Json jret;
    api.buildJsonState({ ID_BOOL_IN, "audio_fake_1" }, [&](Json j) { jret = j; called = true; });

    //All six requests are in flight, nothing answered yet
    EXPECT_FALSE(called);
    answerAll(player);

    ASSERT_TRUE(called);
    ASSERT_TRUE(jret.is_object()) << jret.dump();

    ASSERT_TRUE(jret.contains("audio_fake_1")) << jret.dump();
    const Json &jplayer = jret["audio_fake_1"];
    ASSERT_TRUE(jplayer.is_object()) << jret.dump();

    ASSERT_TRUE(jplayer.contains("current_track")) << jret.dump();
    const Json &jtrack = jplayer["current_track"];
    ASSERT_TRUE(jtrack.is_object()) << jret.dump();

    EXPECT_EQ("3",    jplayer["playlist_current_track"].get<std::string>()) << jret.dump();
    EXPECT_EQ("42",   jplayer["volume"].get<std::string>()) << jret.dump();
    EXPECT_EQ("12",   jplayer["playlist_size"].get<std::string>()) << jret.dump();
    EXPECT_EQ("12.5", jplayer["time_elapsed"].get<std::string>()) << jret.dump();
    EXPECT_EQ("stop", jplayer["status"].get<std::string>()) << jret.dump();
    EXPECT_EQ("a title", jtrack["title"].get<std::string>()) << jret.dump();
    EXPECT_EQ("an artist", jtrack["artist"].get<std::string>()) << jret.dump();

    //Not a JSON number, on any of them. A number here would break every client
    //that compares the state to a string.
    EXPECT_TRUE(jplayer["volume"].is_string()) << jret.dump();
    EXPECT_TRUE(jplayer["time_elapsed"].is_string()) << jret.dump();
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
        api.buildJsonState({ "audio_fake_1" }, [&](Json) { resultCalled = true; });
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

    bool called = false;
    Json jret;
    api.buildJsonState({ ID_BOOL_IN, "audio_fake_1" }, [&](Json j) { jret = j; called = true; });

    //The connection detaches the callback, then the IO is deleted
    AudioRequest_cb lateAnswer = player->takeNext();
    ASSERT_FALSE(lateAnswer.empty());
    ASSERT_TRUE(deleteIO(player));

    //The late answer must not dereference the dead player
    lateAnswer(AudioPlayerData());
    //Drop our copy of the chain (it holds a copy of the shared document being
    //built), exactly as the dying connection object would
    lateAnswer = AudioRequest_cb();

    ASSERT_TRUE(called);
    ASSERT_TRUE(jret.is_object()) << jret.dump();
    EXPECT_TRUE(jret[ID_BOOL_IN].is_string()) << jret.dump();
    //The dead player is ABSENT, not null: the contract is a skipped key.
    EXPECT_FALSE(jret.contains("audio_fake_1")) << jret.dump();
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
    Json jret;
    api.buildJsonState({ "audio_fake_1", "audio_fake_2" }, [&](Json j)
    {
        resultCount++;
        jret = j;
    });

    AudioRequest_cb lateAnswer = p2->takeNext();
    ASSERT_TRUE(deleteIO(p2));
    lateAnswer(AudioPlayerData());
    lateAnswer = AudioRequest_cb(); //drop our copy of the chain
    EXPECT_EQ(resultCount, 0);

    answerAll(p1);

    //ANSWERED EXACTLY ONCE - the counter is the point of this case, and it
    //never had anything to do with reference counts.
    EXPECT_EQ(resultCount, 1);
    ASSERT_TRUE(jret.is_object()) << jret.dump();
    EXPECT_TRUE(jret.contains("audio_fake_1")) << jret.dump();
    EXPECT_FALSE(jret.contains("audio_fake_2")) << jret.dump();
}
