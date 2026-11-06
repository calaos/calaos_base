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

/* E4.0f - the audio surface of the JSON API, characterized at VALUE level.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS FILE IS NOT - READ THIS FIRST, IT DETERMINES THE WHOLE SHAPE
 * ---------------------------------------------------------------------------
 * E4.0.md:350 gives E4.0f eight operations: `audio` x5, processDbResult(), and
 * three audio_db representatives (get_albums/get_album, get_stats,
 * get_track_infos).
 *
 * MEASURED WHEN THIS TICKET STARTED: every one of those eight already has a
 * nominal-payload golden, produced by two OTHER sub-tickets that landed while
 * E4.0f was queued.
 *
 *   T3.17b (core/JsonApiPlayerState_test.cpp, goldens t317b_*) - the four
 *          `audio` sub-actions that answer JSON (get_playlist_size, get_time,
 *          get_playlist_item, get_cover_url) plus audio_db/get_stats, EACH ON
 *          BOTH TRANSPORTS, plus the three synchronous error answers
 *          ("unkown player_id", "empty player id", "wrong item").
 *   T3.17c (core/JsonApiMusicDb_test.cpp, goldens t317c_*) - all fifteen
 *          audioDbGet* methods on both transports, the three shapes of
 *          processDbResult() its fake can produce (leading count marker /
 *          count "0" / no count at all), the from/count gate, and the
 *          get_albums vs get_album spelling divergence in BOTH directions.
 *
 * The fifth `audio` sub-action, get_cover, answers image/jpeg through a spawned
 * calaos_picture and is excluded from the whole series (E4.0.md:228-229).
 *
 * COPYING THOSE GOLDENS HERE WOULD BUY NOTHING. So this file deliberately does
 * NOT re-record the nominal answers. It records what those two sub-tickets did
 * not reach, because their angle was OBJECT LIFETIME (does the answer still
 * arrive when the client, or the IO, is gone?) and not the VALUE CONTRACT of
 * the payload. Concretely, four families:
 *
 *   A. processDbResult() beyond the one shape a fake normally emits: where the
 *      count marker actually lands, which marker wins, what a non numeric count
 *      does, and how exact the "0" test is. T3.17c's fake always puts a single
 *      well formed marker first; production databases do not promise that.
 *   B. the SCALAR FORMATTING of the audio answers - the "Formatage numerique"
 *      row of the switch table in E4.0.md:309, never covered by anything. This
 *      is where the only real bug of this ticket lives (see LOSS OF PRECISION
 *      below).
 *   C. the empty string and the non ASCII string on this surface - two more
 *      rows of that same switch table.
 *   D. the WS/HTTP argument placement asymmetry, applied to audio. Nobody had
 *      pinned what the WRONG transport shape answers.
 *
 * ---------------------------------------------------------------------------
 * THE BUG THIS FILE FREEZES: time_elapsed LOSES PRECISION
 * ---------------------------------------------------------------------------
 * audioGetTime() stringifies the player's double through Utils::to_string()
 * (JsonApi.cpp:1010), which is a bare std::ostringstream with no precision set
 * (src/lib/StringUtils.h:112-117). That is SIX SIGNIFICANT DIGITS, and
 * scientific notation past them:
 *
 *      1234.56789  ->  "1234.57"       (9 significant digits in, 6 out:
 *                                       three decimals silently dropped)
 *      123456789.0 ->  "1.23457e+08"   (a client parsing an int now fails)
 *
 * Frozen, not fixed: E4.0 writes no production line. Consigned in the report.
 *
 * ---------------------------------------------------------------------------
 * THE ORACLE
 * ---------------------------------------------------------------------------
 * Semantic, through EXPECT_JSON_EQ / EXPECT_JSON_GOLDEN, and reads go through
 * the total accessors member()/str() (the RULE OF THE SERIES block of
 * JsonApiCharacterization.h). No serialized JSON string is ever compared.
 *
 * Goldens and IO ids are prefixed e40f_ and used nowhere else: Config's IO
 * state cache is process wide and never cleared (CalaosCoreFixture.h).
 */

#include "JsonApiCharacterization.h"

#include "AudioPlayer.h"
#include "AudioDB.h"
#include "ListeRoom.h"
#include "Utils.h"

#include <string>
#include <vector>

using namespace CalaosTest;
using namespace Calaos;

namespace
{

const char *const PLAYER_ID = "e40f_player";

/* The database behind the fake player. Every getter answers IMMEDIATELY with
 * whatever the case put in the corresponding member: this file is about the
 * SHAPE of the answer, so the answer is data, not behaviour. (The deferred
 * machinery T3.17b/c needed for their lifetime cases would be dead weight
 * here - and duplicating it is exactly what this file avoids.)
 */
class FakeAudioDb: public AudioDB
{
public:
    explicit FakeAudioDb(Params &p): AudioDB(p) {}

    //Answers of the three audio_db representatives of E4.0f
    AudioPlayerData listAnswer;
    AudioPlayerData trackAnswer;
    AudioPlayerData statsAnswer;

    //What the last call was actually given, so "the argument reached the
    //database unchanged" is assertable and not merely plausible.
    int lastFrom = -12345;
    int lastNb = -12345;
    std::string lastTrackId = "<getTrackInfos never called>";
    int listCalls = 0;
    int trackCalls = 0;
    int statsCalls = 0;

    void getAlbums(AudioRequest_cb cb, int from, int nb, AudioPlayerData = AudioPlayerData()) override
    {
        listCalls++;
        lastFrom = from;
        lastNb = nb;
        cb(listAnswer);
    }

    void getTrackInfos(AudioRequest_cb cb, string track_id, AudioPlayerData = AudioPlayerData()) override
    {
        trackCalls++;
        lastTrackId = track_id;
        cb(trackAnswer);
    }

    void getStats(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        statsCalls++;
        cb(statsAnswer);
    }
};

/* AudioPlayer::database is a RAW POINTER LEFT NULL BY THE BASE CONSTRUCTOR
 * (AudioPlayer.h:35), and no transport filters the sixteen audio_db actions on
 * canDatabase(): a real player without a database dereferenced null and took
 * the server down. That is a production bug, ticketed as T3.19 and FIXED THERE
 * (the sixteen audio_db methods now answer {"error":"no music database"}); it
 * is quoted because it is the reason this fixture must install a concrete
 * database explicitly instead of relying on any default. Nothing here changes:
 * every case of this file gives its player a real AudioDB, so not one of them
 * ever reaches the new guard.
 */
class FakeAudioPlayer: public AudioPlayer
{
public:
    explicit FakeAudioPlayer(Params &p):
        AudioPlayer(p),
        db(dbParams)
    {
        database = &db;
    }

    //Scalars the four `audio` sub-actions read. Deliberately distinct from one
    //another so that swapping two response keys cannot go unnoticed.
    int playlistSize = 7;
    double currentTime = 4.5;
    std::string coverUrl = "http://calaos.fr/cover_e40f.jpg";
    AudioPlayerData itemAnswer;

    FakeAudioDb &database_() { return db; }

    void get_playlist_size(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        AudioPlayerData d;
        d.ivalue = playlistSize;
        cb(d);
    }

    void get_current_time(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        AudioPlayerData d;
        d.dvalue = currentTime;
        cb(d);
    }

    void get_playlist_item(int index, AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        requestedItems.push_back(index);
        cb(itemAnswer);
    }

    void get_album_cover(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        AudioPlayerData d;
        d.svalue = coverUrl;
        cb(d);
    }

    bool canPlaylist() override { return true; }
    bool canDatabase() override { return true; }

    std::vector<int> requestedItems;

private:
    Params dbParams;
    FakeAudioDb db;
};

//One row of a database answer. Built here rather than inline so that every
//case's fixture reads as a table and a swapped pair is visible on the page.
Params row(const std::string &id, const std::string &name, const std::string &year)
{
    Params p;
    p.Add("id", id);
    p.Add("name", name);
    p.Add("year", year);
    return p;
}

Params countMarker(const std::string &count)
{
    Params p;
    p.Add("count", count);
    return p;
}

}

class JsonApiAudioPayloadTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();

        forgetIOState(PLAYER_ID);

        /* No drain here, and none is needed. E4.0g removed the one that used
         * to sit at the end of this SetUp(): the fixture now pumps AFTER
         * CoreFixture::TearDown(), so the previous case leaves nothing in the
         * queue. Its comment was wrong on the cause anyway - the load raises
         * NOTHING (Room::LoadFromXml() is silent; EventIOAdded has a single
         * call site, ListeRoom::createIO(), ListeRoom.cpp:466, on the runtime
         * path of the JSON API).
         */
        loadConfig();
    }

    //Registered exactly like a config loaded IO: owned by its room, reachable
    //through ListeRoom::get_io(), which is what getAudioPlayer() looks it up
    //with (JsonApi.cpp:927-931).
    FakeAudioPlayer *addPlayer()
    {
        Params p;
        p.Add("id", PLAYER_ID);
        p.Add("name", "Fake payload player");
        p.Add("type", "FakeAudioPlayer");

        FakeAudioPlayer *player = new FakeAudioPlayer(p);

        firstRoom()->AddIO(player);
        ListeRoom::Instance().addIOHash(player);
        return player;
    }

    /***************************************************************************
     * Request builders.
     *
     * The two are NOT interchangeable, and that asymmetry is itself one of the
     * things this file pins: over WS every argument lives under "data", over
     * HTTP every argument lives at the ROOT of the body. Nothing in either
     * transport validates the shape (see the WrongTransportShape cases).
     **************************************************************************/
    static Json wsRequest(const std::string &msg, const std::string &audioAction,
                          const std::string &id, Json extra = Json::object())
    {
        Json data = Json{{ "id", id }, { "audio_action", audioAction }};
        for (auto it = extra.begin(); it != extra.end(); ++it)
            data[it.key()] = it.value();

        return Json{
            { "msg", msg },
            { "msg_id", "1" },
            { "data", data },
        };
    }

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

    //from/count are mandatory for every list method of audio_db
    //(JsonApi.cpp:1114-1121). 2 and 9 rather than a round pair, so a swap of
    //the two is visible in lastFrom/lastNb.
    static Json fromCount() { return Json{{ "from", "2" }, { "count", "9" }}; }

    /***************************************************************************
     * Every leaf of every payload of this API is a JSON STRING today (32
     * json_string and zero json_integer in JsonApi.cpp; Params::toJson()
     * stringifies unconditionally, src/lib/Params.cpp:134-147). Asserted
     * structurally so that a migration turning "9" into 9 fails with a path
     * rather than only through a golden diff.
     **************************************************************************/
    static ::testing::AssertionResult everyLeafIsAString(const Json &j,
                                                        const std::string &path = "")
    {
        if (j.is_object())
        {
            for (auto it = j.begin(); it != j.end(); ++it)
            {
                auto res = everyLeafIsAString(it.value(), path + "/" + it.key());
                if (!res) return res;
            }
            return ::testing::AssertionSuccess();
        }

        if (j.is_array())
        {
            for (size_t i = 0; i < j.size(); i++)
            {
                auto res = everyLeafIsAString(j[i], path + "/" + std::to_string(i));
                if (!res) return res;
            }
            return ::testing::AssertionSuccess();
        }

        if (j.is_string())
            return ::testing::AssertionSuccess();

        return ::testing::AssertionFailure()
                << path << " is " << j.type_name() << ", not a JSON string";
    }
};

/*******************************************************************************
 * A. processDbResult() - WHERE THE COUNT MARKER LANDS AND WHICH ONE WINS
 *
 * processDbResult() (JsonApi.cpp:1079-1101) is thirteen lines that every one of
 * the sixteen audio_db list actions funnels through. It walks data.vparams and
 *   - appends EVERY Params to "items", the count marker included, unchanged and
 *     in place: the marker is emitted as if it were a row,
 *   - remembers the value of the LAST Params carrying a "count" key,
 *   - clears the whole array when that value is the exact string "0",
 *   - emits "total_count" only when some Params carried a count - absent, never
 *     null.
 *
 * T3.17c pinned the three shapes its fake emits, all with a single well formed
 * marker in first position. The cases below are the ones a production database
 * can produce and no test had described.
 *
 * THEY ARE NOT HYPOTHETICAL, AND THIS CORRECTS E4.0.md. The plan (and the
 * ticket brief) assert that "items[0] of every list answer is {count:N}".
 * MEASURED IN THE ONLY REAL AudioDB IN THE TREE, and false:
 *   - SqueezeboxDB::getAlbums_cb() treats "count" exactly like "id", as a
 *     RECORD SEPARATOR that opens a new Params (Audio/SqueezeboxDB.cpp:66-73).
 *     So the marker is wherever the server put it in the stream, and it can
 *     collect the fields that follow it;
 *   - SqueezeboxDB::getRandoms() appends its count Params LAST, after the four
 *     rows (Audio/SqueezeboxDB.cpp:774-775).
 *
 * WHAT THE GOLDENS BELOW DO AND DO NOT PROVE - the two halves have unequal
 * strength and conflating them would be exactly the sin this series exists to
 * prevent. The goldens prove the FIRST half OPPOSABLY: processDbResult() never
 * reorders, relocates or strips anything, whatever position the marker arrives
 * in. The second half - "a real database does put the marker elsewhere" - is
 * NOT proven by any golden here: it rests on reading the two SqueezeboxDB sites
 * cited above, and nothing in this tree drives a real SqueezeboxDB.
 ******************************************************************************/

/* The marker is NOT relocated to the head: it stays exactly where the database
 * put it. A client that reads items[0] as a row gets a row here and a marker in
 * the T3.17c shape - the position is the database's, not the API's.
 */
TEST_F(JsonApiAudioPayloadTest, CountMarkerKeepsThePositionTheDatabaseGaveIt)
{
    FakeAudioPlayer *player = addPlayer();
    player->database_().listAnswer.vparams = {
        row("alb_11", "Homogenic", "1997"),
        countMarker("47"),
        row("alb_29", "Vespertine", "2001"),
    };

    WsTestSession ws;
    ws.send(wsRequest("audio_db", "get_album", PLAYER_ID, fromCount()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("e40f_ws_db_count_marker_in_the_middle", ws.lastMessage());

    const Json data = ws.lastData();
    const Json items = member(data, "items");
    ASSERT_TRUE(items.is_array());
    ASSERT_EQ(3u, items.size());
    //The marker is the SECOND element, and it is still a full row
    EXPECT_EQ("47", str(items[1], "count"));
    EXPECT_EQ("alb_11", str(items[0], "id"));
    EXPECT_EQ("alb_29", str(items[2], "id"));
    //...and total_count is not the first item's anything
    EXPECT_EQ("47", str(data, "total_count"));
    EXPECT_TRUE(member(items[0], "count").is_null());

    EXPECT_TRUE(everyLeafIsAString(data));
}

//Same answer over HTTP: the very same object, with no envelope around it.
TEST_F(JsonApiAudioPayloadTest, HttpCountMarkerKeepsThePositionTheDatabaseGaveIt)
{
    FakeAudioPlayer *player = addPlayer();
    player->database_().listAnswer.vparams = {
        row("alb_11", "Homogenic", "1997"),
        countMarker("47"),
        row("alb_29", "Vespertine", "2001"),
    };

    HttpTestRequest req;
    req.send(httpRequest("audio_db", "get_albums", PLAYER_ID, fromCount()));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_JSON_GOLDEN("e40f_http_db_count_marker_in_the_middle", req.body());

    //The HTTP body is the WS "data" member and nothing else
    EXPECT_TRUE(member(req.bodyJson(), "msg").is_null());
    EXPECT_TRUE(member(req.bodyJson(), "data").is_null());
}

/* THE ANTI SWAP CASE OF THIS FILE. With one marker, total_count and
 * items[<marker>].count carry the same string, so the two are indistinguishable
 * and a mutation exchanging them would pass every existing golden. With TWO
 * markers they must differ: total_count is the LAST one, items keeps BOTH.
 */
TEST_F(JsonApiAudioPayloadTest, TheLastCountMarkerWinsAndTheEarlierOneSurvivesAsARow)
{
    FakeAudioPlayer *player = addPlayer();
    player->database_().listAnswer.vparams = {
        countMarker("11"),
        row("alb_11", "Homogenic", "1997"),
        countMarker("47"),
    };

    WsTestSession ws;
    ws.send(wsRequest("audio_db", "get_album", PLAYER_ID, fromCount()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("e40f_ws_db_last_count_marker_wins", ws.lastMessage());

    const Json data = ws.lastData();
    const Json items = member(data, "items");
    ASSERT_TRUE(items.is_array());
    ASSERT_EQ(3u, items.size());

    //total_count is the LAST marker, NOT the first one and NOT items[0]'s
    EXPECT_EQ("47", str(data, "total_count"));
    EXPECT_EQ("11", str(items[0], "count"));
    EXPECT_EQ("47", str(items[2], "count"));
    EXPECT_NE(str(data, "total_count"), str(items[0], "count"));
}

/* "0" is compared as a STRING, not as a number. "00" is a perfectly ordinary
 * count as far as processDbResult() is concerned: nothing is cleared, and the
 * client receives total_count "00".
 */
TEST_F(JsonApiAudioPayloadTest, OnlyTheExactString0ClearsTheItems)
{
    FakeAudioPlayer *player = addPlayer();
    player->database_().listAnswer.vparams = {
        countMarker("00"),
        row("alb_11", "Homogenic", "1997"),
    };

    WsTestSession ws;
    ws.send(wsRequest("audio_db", "get_album", PLAYER_ID, fromCount()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("e40f_ws_db_count_double_zero_does_not_clear", ws.lastMessage());

    const Json data = ws.lastData();
    EXPECT_EQ("00", str(data, "total_count"));
    ASSERT_TRUE(member(data, "items").is_array());
    EXPECT_EQ(2u, member(data, "items").size());
}

/* And the clearing is total whatever the marker's position: a "0" arriving
 * LAST still wipes the rows that preceded it. A trailing marker is the real
 * shape of SqueezeboxDB::getRandoms() (Audio/SqueezeboxDB.cpp:774-775).
 */
TEST_F(JsonApiAudioPayloadTest, ATrailingZeroCountStillClearsTheRowsBeforeIt)
{
    FakeAudioPlayer *player = addPlayer();
    player->database_().listAnswer.vparams = {
        row("alb_11", "Homogenic", "1997"),
        row("alb_29", "Vespertine", "2001"),
        countMarker("0"),
    };

    WsTestSession ws;
    ws.send(wsRequest("audio_db", "get_album", PLAYER_ID, fromCount()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("e40f_ws_db_trailing_zero_count_clears", ws.lastMessage());

    const Json data = ws.lastData();
    EXPECT_EQ("0", str(data, "total_count"));
    ASSERT_TRUE(member(data, "items").is_array());
    EXPECT_TRUE(member(data, "items").empty());
}

/* A count that is not a number at all is forwarded VERBATIM as total_count, and
 * clears nothing: there is no numeric parsing anywhere in processDbResult().
 */
TEST_F(JsonApiAudioPayloadTest, ANonNumericCountIsForwardedVerbatimAndClearsNothing)
{
    FakeAudioPlayer *player = addPlayer();
    player->database_().listAnswer.vparams = {
        countMarker("beaucoup"),
        row("alb_11", "Homogenic", "1997"),
    };

    WsTestSession ws;
    ws.send(wsRequest("audio_db", "get_album", PLAYER_ID, fromCount()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("e40f_ws_db_count_not_a_number", ws.lastMessage());

    EXPECT_EQ("beaucoup", str(ws.lastData(), "total_count"));
    EXPECT_EQ(2u, member(ws.lastData(), "items").size());
}

/* The marker row is not filtered down to its count either: whatever else the
 * database attached to it reaches the client as a row of the list. Again real:
 * "count" opens a Params in SqueezeboxDB::getAlbums_cb()
 * (Audio/SqueezeboxDB.cpp:66-73), so every field of the stream up to the next
 * separator is added to the SAME Params as the count.
 */
TEST_F(JsonApiAudioPayloadTest, TheCountMarkerRowKeepsItsOtherKeys)
{
    FakeAudioPlayer *player = addPlayer();
    Params marker = row("alb_marker", "I am a row too", "1969");
    marker.Add("count", "5");
    player->database_().listAnswer.vparams = {
        marker,
        row("alb_29", "Vespertine", "2001"),
    };

    WsTestSession ws;
    ws.send(wsRequest("audio_db", "get_album", PLAYER_ID, fromCount()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("e40f_ws_db_count_marker_row_keeps_its_other_keys", ws.lastMessage());

    const Json items = member(ws.lastData(), "items");
    ASSERT_TRUE(items.is_array());
    ASSERT_EQ(2u, items.size());
    EXPECT_EQ("5", str(items[0], "count"));
    EXPECT_EQ("alb_marker", str(items[0], "id"));
    EXPECT_EQ("I am a row too", str(items[0], "name"));
    EXPECT_EQ("1969", str(items[0], "year"));
}

/* NO COUNT ANYWHERE -> NO total_count KEY AT ALL. Absent, never null and never
 * an empty string.
 *
 * ADDED AFTER A REVIEW COUNTER-MUTATION, and it is the reason this case is not
 * a duplicate of anything: replacing the guard `if (!scount.empty())` by
 * `if (true)` at JsonApi.cpp:1096 left the ENTIRE suite green, because every
 * other processDbResult() case of this file primes a count marker. The contract
 * this file's own header states, and that 08_http_api.md publishes to clients,
 * was asserted by NO test reachable from master: T3.17c's t317c_ws_no_count
 * covers it, but T3.17c is not merged (verified on master at the time of
 * writing: no core/JsonApiMusicDb_test, zero t317c_* goldens). Keep this case
 * even once T3.17c lands - a contract this file publishes should be pinned by
 * this file.
 */
TEST_F(JsonApiAudioPayloadTest, NoCountAnywhereMeansNoTotalCountKeyAtAll)
{
    FakeAudioPlayer *player = addPlayer();
    player->database_().listAnswer.vparams = {
        row("alb_11", "Homogenic", "1997"),
        row("alb_29", "Vespertine", "2001"),
    };

    WsTestSession ws;
    ws.send(wsRequest("audio_db", "get_album", PLAYER_ID, fromCount()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("e40f_ws_db_no_count_marker_at_all", ws.lastMessage());

    const Json data = ws.lastData();
    //The key is ABSENT - not null, not ""
    EXPECT_TRUE(member(data, "total_count").is_null());
    EXPECT_FALSE(data.contains("total_count"));
    //...while the rows are all there
    ASSERT_TRUE(member(data, "items").is_array());
    EXPECT_EQ(2u, member(data, "items").size());
}

/*******************************************************************************
 * A''. THE get_albums / get_album SPELLING DIVERGENCE, BOTH DIRECTIONS
 *
 * The SAME builder (audioDbGetAlbums) answers to "get_albums" over HTTP
 * (JsonApiHandlerHttp.cpp:781) and to "get_album" over WS
 * (JsonApiHandlerWS.cpp:380), and each transport REJECTS the other's spelling.
 * Frozen, not fixed.
 *
 * ADDED AFTER REVIEW. The first draft declared this "already covered by
 * T3.17c". Re-measured on master before deciding, and T3.17c is NOT merged
 * (no core/JsonApiMusicDb_test in tests/Makefile.am, zero t317c_* goldens), so
 * nothing on master pinned it. If T3.17c lands first these two become a
 * deliberate duplicate of its WsRejectsTheHttpSpellingOfGetAlbums /
 * HttpRejectsTheWsSpellingOfGetAlbum, which is the cheaper mistake.
 ******************************************************************************/

TEST_F(JsonApiAudioPayloadTest, WsRejectsTheHttpSpellingGetAlbums)
{
    FakeAudioPlayer *player = addPlayer();
    player->database_().listAnswer.vparams = { countMarker("2") };

    WsTestSession ws;
    ws.send(wsRequest("audio_db", "get_albums", PLAYER_ID, fromCount()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("e40f_ws_db_rejects_the_http_spelling", ws.lastMessage());

    EXPECT_EQ("unkown audio_action", str(ws.lastData(), "error"));
    EXPECT_EQ(0, player->database_().listCalls);
}

TEST_F(JsonApiAudioPayloadTest, HttpRejectsTheWsSpellingGetAlbum)
{
    FakeAudioPlayer *player = addPlayer();
    player->database_().listAnswer.vparams = { countMarker("2") };

    HttpTestRequest req;
    req.send(httpRequest("audio_db", "get_album", PLAYER_ID, fromCount()));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_JSON_GOLDEN("e40f_http_db_rejects_the_ws_spelling", req.body());

    EXPECT_EQ("unkown audio_action", str(req.bodyJson(), "error"));
    EXPECT_EQ(0, player->database_().listCalls);
}

/*******************************************************************************
 * A'. THE from/count ARGUMENTS ARE FORWARDED WITHOUT ANY RANGE CHECK
 *
 * The gate of every list method only asks Utils::is_of_type<int>()
 * (JsonApi.cpp:1114-1121). Negative values pass it and reach the database
 * unchanged - there is no clamping, no minimum, no "count must be positive".
 * T3.17c pinned that 2/7 arrive in that order; this pins that -5 and -1 do too.
 ******************************************************************************/
TEST_F(JsonApiAudioPayloadTest, NegativeFromAndCountPassTheGateUntouched)
{
    FakeAudioPlayer *player = addPlayer();
    player->database_().listAnswer.vparams = { countMarker("0") };

    WsTestSession ws;
    ws.send(wsRequest("audio_db", "get_album", PLAYER_ID,
                      Json{{ "from", "-5" }, { "count", "-1" }}));

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ(1, player->database_().listCalls);
    EXPECT_EQ(-5, player->database_().lastFrom);
    EXPECT_EQ(-1, player->database_().lastNb);
}

//A decimal does NOT pass it: is_of_type<int> stops at the dot and iss.eof() is
//false. The answer is the from/count refusal, and the database is never called.
TEST_F(JsonApiAudioPayloadTest, ADecimalFromIsRefusedBeforeTheDatabase)
{
    FakeAudioPlayer *player = addPlayer();

    WsTestSession ws;
    ws.send(wsRequest("audio_db", "get_album", PLAYER_ID,
                      Json{{ "from", "2.5" }, { "count", "9" }}));

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("wrong from/count", str(ws.lastData(), "error"));
    EXPECT_EQ(0, player->database_().listCalls);
}

/* ⭐ T3.25 (review reserve 2). THE OVERFLOW HALF OF THE is_of_type() CHANGE, on
 * the gate that carries it 14 times.
 *
 * JsonApi.cpp has FOURTEEN identical from/count pairs - measured, one per
 * audio_db list method: :1211, :1285, :1325, :1365, :1405, :1445, :1484, :1523,
 * :1562, :1601, :1641, :1681, :1720, :1759. All of them read
 *
 *     if (itfrom.empty() || !Utils::is_of_type<int>(itfrom) || ...) -> refuse
 *
 * BEFORE T3.25, is_of_type<int>("99999999999") answered TRUE because
 * iss.eof() only said "the whole string was consumed", never "it fits": the
 * request was ACCEPTED, from_string() saturated the count to INT_MAX and
 * getAlbums(from, 2147483647) reached the music database. It is now refused.
 *
 * ⚠️ THAT IS A REAL API CHANGE and this case is what pins it, for all fourteen
 * pairs at once: they are byte-identical, so one of them is the oracle of the
 * shape. It is deliberately NOT a golden - the answer is a one key document and
 * the two assertions below say all of it. Contrast it with the row directly
 * above (-5/-1 pass untouched): the gate has never had a RANGE check, and this
 * ticket did not add one. What changed is only that a token which does not fit
 * an int stopped being called an int.
 */
TEST_F(JsonApiAudioPayloadTest, AnOverflowingCountIsRefusedBeforeTheDatabase)
{
    FakeAudioPlayer *player = addPlayer();

    WsTestSession ws;
    ws.send(wsRequest("audio_db", "get_album", PLAYER_ID,
                      Json{{ "from", "0" }, { "count", "99999999999" }}));

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("wrong from/count", str(ws.lastData(), "error"));
    EXPECT_EQ(0, player->database_().listCalls)
            << "a count that does not fit an int reached the database "
               "saturated to INT_MAX";
}

//The same on `from`, and on the exact boundary token that must STILL pass:
//2147483647 is an int, 2147483648 is not, and one character separates them.
TEST_F(JsonApiAudioPayloadTest, TheIntBoundaryIsWhereTheGateNowCuts)
{
    FakeAudioPlayer *player = addPlayer();
    player->database_().listAnswer.vparams = { countMarker("0") };

    WsTestSession ok;
    ok.send(wsRequest("audio_db", "get_album", PLAYER_ID,
                      Json{{ "from", "2147483647" }, { "count", "1" }}));
    ASSERT_EQ(1u, ok.count());
    EXPECT_EQ(1, player->database_().listCalls);
    EXPECT_EQ(2147483647, player->database_().lastFrom);

    WsTestSession ko;
    ko.send(wsRequest("audio_db", "get_album", PLAYER_ID,
                      Json{{ "from", "2147483648" }, { "count", "1" }}));
    ASSERT_EQ(1u, ko.count());
    EXPECT_EQ("wrong from/count", str(ko.lastData(), "error"));
    EXPECT_EQ(1, player->database_().listCalls)
            << "the out-of-range `from` reached the database as well";
}

/*******************************************************************************
 * B. SCALAR FORMATTING - THE "Formatage numerique" ROW OF THE SWITCH TABLE
 *
 * E4.0.md:309 asks for Utils::to_string(double) to be pinned somewhere. It is
 * pinned here, on time_elapsed, because that is the only double the JSON API
 * ever puts on the wire.
 *
 * Utils::to_string() (src/lib/StringUtils.h:112-117) is a bare
 * std::ostringstream with no precision(): SIX SIGNIFICANT DIGITS, scientific
 * notation past them. The two cases below are a real, currently shipping loss
 * of information, frozen as it is.
 ******************************************************************************/

TEST_F(JsonApiAudioPayloadTest, TimeElapsedIsTruncatedToSixSignificantDigits)
{
    FakeAudioPlayer *player = addPlayer();
    player->currentTime = 1234.56789;

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_time", PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("e40f_ws_audio_time_six_significant_digits", ws.lastMessage());

    //Not "1234.56789": three decimals of the player's answer never leave the
    //server (5 decimals in, 2 out)
    EXPECT_EQ("1234.57", str(ws.lastData(), "time_elapsed"));
}

TEST_F(JsonApiAudioPayloadTest, ALargeTimeElapsedGoesScientificOnTheWire)
{
    FakeAudioPlayer *player = addPlayer();
    player->currentTime = 123456789.0;

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_time", PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("e40f_ws_audio_time_large_value_goes_scientific", ws.lastMessage());

    //A client doing parseInt() on this gets 1
    EXPECT_EQ("1.23457e+08", str(ws.lastData(), "time_elapsed"));
}

//A whole double loses its fractional part entirely - "0", not "0.0".
TEST_F(JsonApiAudioPayloadTest, AWholeTimeElapsedHasNoDecimalPoint)
{
    FakeAudioPlayer *player = addPlayer();
    player->currentTime = 0.0;

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_time", PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("0", str(ws.lastData(), "time_elapsed"));
}

/* playlist_size is an int and goes out exactly, sign included. A real player
 * answers -1 when it does not know, and the API forwards that as a size.
 */
TEST_F(JsonApiAudioPayloadTest, ANegativePlaylistSizeIsForwardedAsIs)
{
    FakeAudioPlayer *player = addPlayer();
    player->playlistSize = -1;

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_playlist_size", PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("e40f_ws_audio_playlist_size_negative", ws.lastMessage());

    EXPECT_EQ("-1", str(ws.lastData(), "playlist_size"));
    //...and it is the STRING "-1", never the number -1
    EXPECT_TRUE(member(ws.lastData(), "playlist_size").is_string());
}

/*******************************************************************************
 * C. THE EMPTY STRING AND THE NON ASCII STRING ON THIS SURFACE
 *
 * Two more rows of the switch table of E4.0.md:301-313, neither covered on the
 * audio surface.
 ******************************************************************************/

/* audioGetCoverInfo() builds {"cover", data.svalue} unconditionally
 * (JsonApi.cpp:1069-1070): a player with no cover answers the KEY WITH AN EMPTY
 * STRING, not an absent key and not null. That distinction is exactly what a
 * migration writing j["cover"] = nullptr would destroy.
 */
TEST_F(JsonApiAudioPayloadTest, AnEmptyCoverUrlIsAnEmptyStringNotAnAbsentKey)
{
    FakeAudioPlayer *player = addPlayer();
    player->coverUrl = "";

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_cover_url", PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("e40f_ws_audio_cover_url_empty", ws.lastMessage());

    const Json data = ws.lastData();
    EXPECT_FALSE(member(data, "cover").is_null());
    ASSERT_TRUE(member(data, "cover").is_string());
    EXPECT_EQ("", str(data, "cover"));
}

/* Track metadata is the one place of this API where non ASCII is routine.
 * jansson serializes it as \uXXXX escapes (JSON_ENSURE_ASCII,
 * Jansson_Addition.h:115) while nlohmann will write raw UTF-8; both parse back
 * to the same string, which is the whole point of a SEMANTIC oracle. The five
 * values are pairwise unmistakable so that exchanging two keys cannot pass.
 */
TEST_F(JsonApiAudioPayloadTest, NonAsciiTrackMetadataSurvivesTheEscapingDifference)
{
    FakeAudioPlayer *player = addPlayer();
    player->itemAnswer.params.Add("id", "trk_e40f_17");
    player->itemAnswer.params.Add("title", "Aerodynamique \xc3\xa9t\xc3\xa9");
    player->itemAnswer.params.Add("artist", "Bj\xc3\xb6rk");
    player->itemAnswer.params.Add("album", "Na\xc3\xafve \xc3\x89pilogue");
    player->itemAnswer.params.Add("duration", "231");

    WsTestSession ws;
    ws.send(wsRequest("audio", "get_playlist_item", PLAYER_ID, Json{{ "item", "4" }}));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("e40f_ws_audio_playlist_item_non_ascii", ws.lastMessage());

    //Each key read back individually, so a swapped pair names itself
    const Json data = ws.lastData();
    EXPECT_EQ("trk_e40f_17", str(data, "id"));
    EXPECT_EQ("Aerodynamique \xc3\xa9t\xc3\xa9", str(data, "title"));
    EXPECT_EQ("Bj\xc3\xb6rk", str(data, "artist"));
    EXPECT_EQ("Na\xc3\xafve \xc3\x89pilogue", str(data, "album"));
    EXPECT_EQ("231", str(data, "duration"));

    EXPECT_EQ(std::vector<int>({ 4 }), player->requestedItems);
}

TEST_F(JsonApiAudioPayloadTest, HttpNonAsciiTrackMetadataSurvivesToo)
{
    FakeAudioPlayer *player = addPlayer();
    player->itemAnswer.params.Add("id", "trk_e40f_17");
    player->itemAnswer.params.Add("title", "Aerodynamique \xc3\xa9t\xc3\xa9");
    player->itemAnswer.params.Add("artist", "Bj\xc3\xb6rk");
    player->itemAnswer.params.Add("album", "Na\xc3\xafve \xc3\x89pilogue");
    player->itemAnswer.params.Add("duration", "231");

    HttpTestRequest req;
    req.send(httpRequest("audio", "get_playlist_item", PLAYER_ID, Json{{ "item", "4" }}));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_JSON_GOLDEN("e40f_http_audio_playlist_item_non_ascii", req.body());
    EXPECT_EQ("Bj\xc3\xb6rk", str(req.bodyJson(), "artist"));
}

//get_track_infos answers through data.params and NEVER goes through
//processDbResult(): its payload is flat, with no items and no total_count. The
//non ASCII contract is the same on it.
TEST_F(JsonApiAudioPayloadTest, TrackInfosIsFlatAndCarriesNonAsciiToo)
{
    FakeAudioPlayer *player = addPlayer();
    player->database_().trackAnswer.params.Add("track_id", "trk_e40f_88");
    player->database_().trackAnswer.params.Add("title", "Une journ\xc3\xa9""e ordinaire");
    player->database_().trackAnswer.params.Add("artist", "Beno\xc3\xaet");
    player->database_().trackAnswer.params.Add("album", "\xc3\x80 rebours");
    player->database_().trackAnswer.params.Add("duration", "402");

    WsTestSession ws;
    ws.send(wsRequest("audio_db", "get_track_infos", PLAYER_ID,
                      Json{{ "track_id", "trk_e40f_88" }}));

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("e40f_ws_db_track_infos_non_ascii", ws.lastMessage());

    const Json data = ws.lastData();
    //Flat: no list wrapper anywhere
    EXPECT_TRUE(member(data, "items").is_null());
    EXPECT_TRUE(member(data, "total_count").is_null());
    EXPECT_EQ("Beno\xc3\xaet", str(data, "artist"));
    EXPECT_EQ("\xc3\x80 rebours", str(data, "album"));
    EXPECT_EQ("trk_e40f_88", player->database_().lastTrackId);
}

/*******************************************************************************
 * D. THE WS/HTTP ARGUMENT PLACEMENT ASYMMETRY, APPLIED TO AUDIO
 *
 * Over WS the handler passes json_object_get(jroot, "data") down
 * (JsonApiHandlerWS.cpp:110, :192); over HTTP it passes the ROOT of the body
 * (JsonApiHandlerHttp.cpp:687). Neither validates that shape.
 *
 * THE FINDING THESE THREE CASES RECORD: a client that sends the OTHER
 * transport's shape gets back, byte for byte, the SAME document as a client
 * that misspelled the sub-action name. There is no way, from the answer alone,
 * to tell "you nested your arguments" from "no such audio_action" - which is
 * why the asymmetry costs so much to debug in the field.
 ******************************************************************************/

TEST_F(JsonApiAudioPayloadTest, WsAnswersAnHttpShapedRequestLikeAnUnknownAction)
{
    FakeAudioPlayer *player = addPlayer();

    //HTTP shape: everything at the root of the message, no "data"
    WsTestSession ws;
    ws.send(Json{
        { "msg", "audio" },
        { "msg_id", "1" },
        { "audio_action", "get_time" },
        { "id", PLAYER_ID },
    });

    ASSERT_EQ(1u, ws.count());
    EXPECT_JSON_GOLDEN("e40f_ws_audio_http_shaped_request", ws.lastMessage());
    const std::string wrongShape = ws.lastMessage();

    //Byte for byte what a misspelled sub-action answers
    WsTestSession ws2;
    ws2.send(wsRequest("audio", "get_tim", PLAYER_ID));
    ASSERT_EQ(1u, ws2.count());
    EXPECT_JSON_EQ(wrongShape, ws2.lastMessage());

    EXPECT_TRUE(player->requestedItems.empty());
}

TEST_F(JsonApiAudioPayloadTest, HttpAnswersAWsShapedRequestLikeAnUnknownAction)
{
    addPlayer();

    //WS shape: the arguments nested under "data"
    HttpTestRequest req;
    req.send(authenticated(Json{
        { "action", "audio" },
        { "data", Json{{ "audio_action", "get_time" }, { "id", PLAYER_ID }} },
    }));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_JSON_GOLDEN("e40f_http_audio_ws_shaped_request", req.body());

    HttpTestRequest req2;
    req2.send(httpRequest("audio", "get_tim", PLAYER_ID));
    EXPECT_JSON_EQ(req.body(), req2.body());
}

/* On audio_db the degradation is subtler and therefore worse: the sub-action
 * name is at the root and IS found, so dispatch succeeds - only from/count are
 * nested, and the client is told its from/count are wrong when in fact it never
 * sent them where the server looks.
 */
TEST_F(JsonApiAudioPayloadTest, HttpAudioDbWithNestedArgumentsBlamesFromCount)
{
    FakeAudioPlayer *player = addPlayer();

    HttpTestRequest req;
    req.send(authenticated(Json{
        { "action", "audio_db" },
        { "audio_action", "get_albums" },
        { "id", PLAYER_ID },
        { "data", fromCount() },
    }));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_JSON_GOLDEN("e40f_http_audio_db_ws_shaped_arguments", req.body());

    EXPECT_EQ("wrong from/count", str(req.bodyJson(), "error"));
    EXPECT_EQ(0, player->database_().listCalls);
}

/*******************************************************************************
 * E. THE ENVELOPE NAME OF EACH DISPATCH
 *
 * The two audio dispatches answer under DIFFERENT msg names even though the
 * bodies come from the same JsonApi methods: "audio" for processAudio,
 * "audio_db" for processAudioDb (JsonApiHandlerWS.cpp:356, :382). get_stats is
 * the one that makes the pair non trivial: it is a player-state method reached
 * through the audio_db dispatch, so its answer carries the audio_db name.
 ******************************************************************************/
TEST_F(JsonApiAudioPayloadTest, TheEnvelopeNameComesFromTheDispatchNotFromTheMethod)
{
    FakeAudioPlayer *player = addPlayer();
    player->database_().statsAnswer.params.Add("albums", "31");
    player->database_().statsAnswer.params.Add("artists", "18");
    player->database_().statsAnswer.params.Add("songs", "526");

    WsTestSession wsAudio;
    wsAudio.send(wsRequest("audio", "get_time", PLAYER_ID));
    ASSERT_EQ(1u, wsAudio.count());
    EXPECT_EQ("audio", str(wsAudio.lastEnvelope(), "msg"));

    WsTestSession wsDb;
    wsDb.send(wsRequest("audio_db", "get_stats", PLAYER_ID));
    ASSERT_EQ(1u, wsDb.count());
    EXPECT_EQ("audio_db", str(wsDb.lastEnvelope(), "msg"));

    /* And the audio_action echo is NOT uniform: get_stats emits it
     * (JsonApi.cpp:966), get_time adds it to the player's params and then
     * builds a FRESH Params for the answer, so it is swallowed
     * (JsonApi.cpp:1008-1011). Two sibling sub-actions, two contracts.
     */
    EXPECT_EQ("get_stats", str(wsDb.lastData(), "audio_action"));
    EXPECT_TRUE(member(wsAudio.lastData(), "audio_action").is_null());

    EXPECT_EQ("31", str(wsDb.lastData(), "albums"));
    EXPECT_EQ("18", str(wsDb.lastData(), "artists"));
    EXPECT_EQ("526", str(wsDb.lastData(), "songs"));
}
