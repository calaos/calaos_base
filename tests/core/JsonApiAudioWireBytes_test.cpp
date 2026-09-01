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
/*******************************************************************************
 * E4.1p - THE BYTES THE AUDIO PLAYER CHAIN PUTS ON THE WIRE, AND THE ONE
 *         RECURSION OF THE EPIC THAT CROSSES AN ASYNC BOUNDARY.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS FILE EXISTS
 * ---------------------------------------------------------------------------
 * E4.1p migrates the player half of JsonApi to nlohmann:
 *
 *      decodeGetPlaylist()      -> get_playlist            (HTTP, WS)
 *      getNextPlaylistItem()    -> its RECURSIVE, ASYNC stage
 *      getAudioPlayer()         -> the id lookup of every audio action
 *      audioGetPlaylistSize()   -> audio audio_action=get_playlist_size
 *      audioGetTime()           -> audio audio_action=get_time
 *      audioGetPlaylistItem()   -> audio audio_action=get_playlist_item
 *      audioGetCoverInfo()      -> audio audio_action=get_cover_url
 *
 * and the two transports' processAudio()/processGetPlaylist(). It does NOT
 * migrate audioGetDbStats(), audioDbUnavailable() and processDbResult(): all
 * three are reached only through processAudioDb(), which is E4.1q's dispatcher
 * - see section 10 below and E4.1p.md.
 *
 * The regime of proof of the epic, restated because it is the whole reason for
 * this file: the 145 goldens compare PARSED DOCUMENTS. They are, by
 * construction, blind to key order, to the case of a \uXXXX escape and to
 * whether a byte was escaped at all. The 20 audio goldens of E4.0f / T3.17a /
 * T3.17b cover STRUCTURE and VALUES on this perimeter and cover them well;
 * NOTHING covers its BYTE dimension until this file exists.
 *
 * Every byte assertion below reads RAW RESPONSE BYTES - HttpTestRequest::body()
 * and WsTestSession::lastMessage(), never bodyJson() / lastData().
 *
 * ---------------------------------------------------------------------------
 * DELTA CASES vs INVARIANT CASES - READ BEFORE EDITING
 * ---------------------------------------------------------------------------
 * This file shipped in two commits. The first pinned the JANSSON bytes, case by
 * case, on an untouched tree, with the suffix ...Today on every case that had
 * to move; the migration commit rewrote exactly those assertions and dropped
 * the suffix. That is the proof the path is EXERCISED and not merely compiled -
 * a case that had to be edited is a case that ran.
 *
 * MEASURED: 43 cases, 15 of them ...Today. The migration made those 15 fail and
 * NOT ONE of the other 28, and no other test of the tree moved - the 145
 * goldens included. Every case now carries either MOVED (it was a ...Today) or
 * INVARIANT in its comment.
 *
 * The INVARIANTS held on both sides. If one of them ever moves, a VALUE or a
 * STRUCTURE changed - stop and understand why before touching it.
 *
 * ---------------------------------------------------------------------------
 * THE DELTAS, ON THIS PERIMETER
 * ---------------------------------------------------------------------------
 *   1. KEY ORDER      jansson keeps insertion order, nlohmann sorts. Visible
 *                     HERE and nowhere else on this chain: decodeGetPlaylist()
 *                     inserts current_track, then count, then items, and those
 *                     three sort to count, current_track, items. Every OTHER
 *                     answer of this perimeter is built from a Params, which is
 *                     a std::map and therefore ALREADY alphabetical - so its
 *                     key order does NOT move, and a case says so.
 *                     The WS envelope moves too (msg,msg_id,data -> data,msg,
 *                     msg_id), as it did for every other migrated action.
 *   2. HEX CASE       jansson writes an accent UPPER case, nlohmann lower case.
 *   3. INVALID UTF-8  jansson drops the WHOLE PAIR, nlohmann keeps it with one
 *                     U+FFFD per invalid byte. A STRUCTURE delta: an absent key
 *                     becomes present.
 *   4. DEL (0x7F)     jansson writes the raw byte, nlohmann escapes it, because
 *                     with ensure_ascii it escapes every codepoint >= 0x7F
 *                     (json.hpp:18467). +5 bytes, and Content-Length follows.
 *   5. EMBEDDED NUL   jansson takes a const char*: the value is TRUNCATED IN
 *                     SILENCE. nlohmann takes a std::string and keeps it whole,
 *                     escaping the NUL.
 *
 * ---------------------------------------------------------------------------
 * WHY THE POISON IS IN THE PLAYER'S ANSWER AND NOT IN THE REQUEST
 * ---------------------------------------------------------------------------
 * On the param chain (E4.1o) the injection channel was the client: a GET query
 * is percent-decoded before it is split, so ?param=%ff%80x reaches a JSON KEY.
 * There is no such channel here - every field this perimeter reads out of the
 * request is an id or an integer, and a JSON body cannot carry invalid UTF-8 at
 * all (both parsers refuse it, pinned in JsonApiSession_test.cpp).
 *
 * The bytes come from the OTHER side: track titles, album names and cover URLs
 * are what a squeezebox or a roon helper answers over the network, and
 * ultimately what a filesystem holds - and a filesystem guarantees nothing
 * about UTF-8. That is the realistic channel for this chain, and it is the one
 * the fake player below drives. Once decodeGetPlaylist() answers a Json, such a
 * byte would be a std::terminate on a live connection if the emitter did not
 * carry error_handler_t::replace. It does, since E4.1b
 * (JsonApiHandlerHttp.cpp:271, JsonApiHandlerWS.cpp:105). The cases named
 * ...IsDeliveredAndTheConnectionSurvives are the proof it holds: they assert a
 * delivered response, a 200, and an EMPTY closes() list, on both sides of the
 * migration.
 *
 * ---------------------------------------------------------------------------
 * Utils::to_string(double) IS A BARE OSTRINGSTREAM AND STAYS ONE
 * ---------------------------------------------------------------------------
 * The playing time, the playlist size and the track durations of this chain all
 * go through it, and it is the most tempting thing in the whole epic to
 * "correct": 1234.56789 becomes "1234.57" and 123456789.0 becomes "1.23457e+08".
 * Those two exact values are asserted below THROUGH THE API - on the response
 * bytes, WITH their surrounding quotes, so that neither the formatting nor the
 * STRING typing can drift. Both are already pinned by E4.0f goldens; here they
 * are pinned at the byte level as well, because a golden compares parsed
 * documents and would not see "1234.57" become 1234.57.
 *
 * ---------------------------------------------------------------------------
 * FIXTURE, AND THE "POOR FIXTURE" TRAP (13 recorded relapses in this series)
 * ---------------------------------------------------------------------------
 * ON A PLAYLIST THIS TRAP HAS A NAME: two items that answer the same bytes.
 * Exchanging two such items is invisible, and the counter-mutation the ticket
 * sheet asks for by name (exchange two consecutive items) would come back GREEN
 * for the wrong reason. The three tracks below therefore differ on EVERY ONE of
 * their five members - id, title, artist, album and duration - and the three
 * durations are numbers of THREE DIFFERENT WIDTHS (61, 122, 183), so an
 * exchange moves both the text and the length of the payload.
 *
 * Ids are prefixed e41p_. Taken so far: e40_, e40b_ .. e40f_, e41b_, e41n_,
 * e41o_, t317a_ .. t317f_, t319_.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "AudioPlayer.h"
#include "AudioDB.h"
#include "ListeRoom.h"
#include "Utils.h"

#include <deque>
#include <functional>
#include <string>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char *const PLAYER_ID    = "e41p_player";
const char *const NO_DB_PLAYER = "e41p_nodb_player";

/*******************************************************************************
 * The byte shapes, spelled in escapes so no editor and no locale can rewrite
 * them silently.
 ******************************************************************************/
const char *const RAW_E_ACUTE   = "\xc3\xa9";       //UTF-8 of U+00E9
const char *const ASCII_E_UPPER = "\\u00E9";        //what jansson writes
const char *const ASCII_E_LOWER = "\\u00e9";        //what nlohmann writes

//0xFF is not a legal UTF-8 lead byte in any position and 0x80 is a
//continuation byte with nothing to continue: this cannot become valid text by
//accident. A test probe must be a value that cannot become valid.
const char *const INVALID_UTF8_PROBE = "\xff\x80" "x";
//What error_handler_t::replace makes of the probe: ONE U+FFFD per bad byte, and
//the trailing 'x' untouched - replace substitutes, it does not truncate.
const char *const ASCII_FFFD_FFFD_X  = "\\ufffd\\ufffdx";

const char *const RAW_DEL       = "\x7f";
const char *const ASCII_DEL     = "\\u007f";
const char *const ASCII_NUL     = "\\u0000";

/* The pending answers of a player, held OUTSIDE the player: a real
 * squeezebox/roon connection object owns the pending callback and outlives the
 * IO, so a queue living inside the player could not express "the IO was deleted
 * and the answer arrived afterwards". Same shape as JsonApiPlayerState_test.
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

    bool fireNext()
    {
        std::function<void()> next = takeNext();
        if (!next)
            return false;
        next();
        return true;
    }

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

class FakeAudioDb: public AudioDB
{
public:
    FakeAudioDb(Params &p, AnswerQueue &q): AudioDB(p), queue(q) {}

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

/* THE THREE TRACKS. Every member differs on every track, and the three
 * durations have three different widths - see the "poor fixture" note in the
 * file header. This is the fixture the exchange counter-mutation bites on.
 */
Params trackParams(const std::string &id, const std::string &title,
                   const std::string &artist, const std::string &album,
                   const std::string &duration)
{
    Params p;
    p.Add("id", id);
    p.Add("title", title);
    p.Add("artist", artist);
    p.Add("album", album);
    p.Add("duration", duration);
    return p;
}

std::vector<Params> referenceTracks()
{
    std::vector<Params> v;
    v.push_back(trackParams("track_a", "Alpha", "Ann", "First",  "61"));
    v.push_back(trackParams("track_b", "Beta",  "Bob", "Second", "122"));
    v.push_back(trackParams("track_c", "Gamma", "Cid", "Third",  "183"));
    return v;
}

class FakeWirePlayer: public AudioPlayer
{
public:
    FakeWirePlayer(Params &p, AnswerQueue &q, bool withDb):
        AudioPlayer(p),
        db(dbParams, q),
        queue(q)
    {
        if (withDb)
            database = &db;
        items = referenceTracks();
    }

    int currentTrack = 1;
    double currentTime = 12.5;
    //By default the player answers as many tracks as `items` holds. A case
    //that needs another count - a negative one, for instance - sets
    //overrideSize as well, so that a NEGATIVE size is not confused with the
    //"use items.size()" default.
    bool overrideSize = false;
    int playlistSize = 0;
    std::string coverUrl = "http://calaos.fr/cover_e41p.jpg";
    std::vector<Params> items;

    //Indices get_playlist_item() was actually asked for, in order. This is the
    //anti-truncation probe: the recursion must ask for 0..size-1.
    std::vector<int> requestedItems;

    int size() const { return overrideSize ? playlistSize : (int)items.size(); }

    void get_playlist_current(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        AudioPlayerData d;
        d.ivalue = currentTrack;
        queue.answer(cb, d);
    }

    void get_playlist_size(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        AudioPlayerData d;
        d.ivalue = size();
        queue.answer(cb, d);
    }

    void get_playlist_item(int index, AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        requestedItems.push_back(index);
        AudioPlayerData d;
        if (index >= 0 && index < (int)items.size())
            d.params = items[index];
        queue.answer(cb, d);
    }

    void get_current_time(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        AudioPlayerData d;
        d.dvalue = currentTime;
        queue.answer(cb, d);
    }

    void get_album_cover(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        AudioPlayerData d;
        d.svalue = coverUrl;
        queue.answer(cb, d);
    }

    bool canPlaylist() override { return true; }
    bool canDatabase() override { return true; }

private:
    Params dbParams;
    FakeAudioDb db;
    AnswerQueue &queue;
};

bool contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

size_t occurrences(const std::string &haystack, const std::string &needle)
{
    size_t n = 0;
    for (size_t p = haystack.find(needle); p != std::string::npos;
         p = haystack.find(needle, p + needle.size()))
        n++;
    return n;
}

//The exact bytes one track object takes on the wire. Params is a std::map, so
//the five members come out ALPHABETICALLY on both libraries - that is the
//invariant, and it is asserted as such below.
std::string trackBytes(const std::string &album, const std::string &artist,
                       const std::string &duration, const std::string &id,
                       const std::string &title)
{
    return "{\"album\":\"" + album + "\",\"artist\":\"" + artist +
           "\",\"duration\":\"" + duration + "\",\"id\":\"" + id +
           "\",\"title\":\"" + title + "\"}";
}

bool isPureAscii(const std::string &s)
{
    for (size_t i = 0; i < s.size(); i++)
        if ((unsigned char)s[i] >= 0x80)
            return false;
    return true;
}

} //namespace

class JsonApiAudioWireBytesTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();

        //Config's IO state cache is process wide and never cleared: a value
        //left behind by a previous case of this binary would leak in.
        forgetIOState(PLAYER_ID);
        forgetIOState(NO_DB_PLAYER);

        loadConfig();
    }

    AnswerQueue queue;

    /* Register a fake player in ListeRoom exactly like a config loaded IO:
     * owned by its room (clearCoreState() deletes it) and reachable through
     * ListeRoom::get_io(), which is what getAudioPlayer() and
     * decodeGetPlaylist() look it up with.
     */
    FakeWirePlayer *addPlayer(const std::string &id = PLAYER_ID, bool withDb = true)
    {
        Params p;
        p.Add("id", id);
        p.Add("name", "Fake wire player");
        p.Add("type", "FakeWirePlayer");

        FakeWirePlayer *player = new FakeWirePlayer(p, queue, withDb);

        firstRoom()->AddIO(player);
        ListeRoom::Instance().addIOHash(player);
        return player;
    }

    //{"msg":"get_playlist","msg_id":"1","data":{"id":...}}
    static Json wsPlaylistRequest(const std::string &id)
    {
        return Json{{ "msg", "get_playlist" }, { "msg_id", "1" },
                    { "data", Json{{ "id", id }} }};
    }

    static Json httpPlaylistRequest(const std::string &id)
    {
        return authenticated(Json{{ "action", "get_playlist" }, { "id", id }});
    }

    //{"msg":"audio","msg_id":"1","data":{"id":...,"audio_action":...}}
    static Json wsAudioRequest(const std::string &action, Json data)
    {
        data["audio_action"] = action;
        return Json{{ "msg", "audio" }, { "msg_id", "1" }, { "data", data }};
    }

    //HTTP puts everything flat in the request body.
    static Json httpAudioRequest(const std::string &action, Json body)
    {
        body["action"] = "audio";
        body["audio_action"] = action;
        return authenticated(body);
    }

    //The OTHER dispatcher - processAudioDb(), which E4.1q owns.
    static Json httpAudioDbRequest(const std::string &action, Json body)
    {
        body["action"] = "audio_db";
        body["audio_action"] = action;
        return authenticated(body);
    }
};

/*******************************************************************************
 * 1. THE WHOLE PLAYLIST ANSWER, BYTE FOR BYTE, ON BOTH TRANSPORTS.
 *
 * These two cases are the reference bytes of get_playlist. They carry, in one
 * string: the envelope, the key order of the player object, the array order of
 * the items, the alphabetical order inside each item, and the STRING typing of
 * current_track, count and duration.
 ******************************************************************************/

//MOVED by E4.1p, as announced: the three keys of the player object were
//inserted current_track, count, items and now SORT to count, current_track,
//items; the WS envelope sorts with them (data before msg). Nothing else in
//this string moved - not one item, not one member, not one quote.
TEST_F(JsonApiAudioWireBytesTest, WsGetPlaylistWholeAnswerBytes)
{
    FakeWirePlayer *player = addPlayer();

    WsTestSession ws;
    ws.send(wsPlaylistRequest(PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("{\"data\":{\"count\":\"3\",\"current_track\":\"1\",\"items\":["
              + trackBytes("First",  "Ann", "61",  "track_a", "Alpha") + ","
              + trackBytes("Second", "Bob", "122", "track_b", "Beta")  + ","
              + trackBytes("Third",  "Cid", "183", "track_c", "Gamma") + "]},"
              "\"msg\":\"get_playlist\",\"msg_id\":\"1\"}",
              ws.lastMessage());

    //The recursion asked for every index, once, in order.
    EXPECT_EQ(std::vector<int>({ 0, 1, 2 }), player->requestedItems);
}

//MOVED: same three keys, no envelope on this transport.
TEST_F(JsonApiAudioWireBytesTest, HttpGetPlaylistWholeAnswerBytes)
{
    FakeWirePlayer *player = addPlayer();

    HttpTestRequest req;
    req.send(httpPlaylistRequest(PLAYER_ID));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_EQ("{\"count\":\"3\",\"current_track\":\"1\",\"items\":["
              + trackBytes("First",  "Ann", "61",  "track_a", "Alpha") + ","
              + trackBytes("Second", "Bob", "122", "track_b", "Beta")  + ","
              + trackBytes("Third",  "Cid", "183", "track_c", "Gamma") + "]}",
              req.body());

    EXPECT_EQ(std::vector<int>({ 0, 1, 2 }), player->requestedItems);
}

/*******************************************************************************
 * 2. THE ARRAY ORDER - INVARIANT, AND THE TARGET OF THE EXCHANGE MUTATION.
 *
 * A JSON array is ordered in both libraries. Exchanging two consecutive items
 * of the playlist must therefore be visible on the wire, and these cases are
 * what makes it visible. They must stay green across the migration.
 ******************************************************************************/

TEST_F(JsonApiAudioWireBytesTest, ThePlaylistItemsAreOnTheWireInPlayerOrder)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsPlaylistRequest(PLAYER_ID));
    ASSERT_EQ(1u, ws.count());
    const std::string wire = ws.lastMessage();

    const size_t a = wire.find(trackBytes("First",  "Ann", "61",  "track_a", "Alpha"));
    const size_t b = wire.find(trackBytes("Second", "Bob", "122", "track_b", "Beta"));
    const size_t c = wire.find(trackBytes("Third",  "Cid", "183", "track_c", "Gamma"));

    ASSERT_NE(std::string::npos, a) << wire;
    ASSERT_NE(std::string::npos, b) << wire;
    ASSERT_NE(std::string::npos, c) << wire;

    EXPECT_LT(a, b);
    EXPECT_LT(b, c);
}

TEST_F(JsonApiAudioWireBytesTest, HttpPlaylistItemsAreOnTheWireInPlayerOrder)
{
    addPlayer();

    HttpTestRequest req;
    req.send(httpPlaylistRequest(PLAYER_ID));
    const std::string wire = req.body();

    const size_t a = wire.find("\"Alpha\"");
    const size_t b = wire.find("\"Beta\"");
    const size_t c = wire.find("\"Gamma\"");

    ASSERT_NE(std::string::npos, a) << wire;
    ASSERT_NE(std::string::npos, b) << wire;
    ASSERT_NE(std::string::npos, c) << wire;
    EXPECT_LT(a, b);
    EXPECT_LT(b, c);

    //The durations follow their own track, and they have three widths.
    EXPECT_LT(wire.find("\"61\""), wire.find("\"122\""));
    EXPECT_LT(wire.find("\"122\""), wire.find("\"183\""));
}

//INVARIANT. Params is a std::map: the five members of a track come out
//alphabetically under jansson (insertion order IS map order) and under
//nlohmann (which sorts). This is the case that says the item objects do NOT
//move even though the player object does.
TEST_F(JsonApiAudioWireBytesTest, ATrackObjectKeepsItsAlphabeticalMemberOrder)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsPlaylistRequest(PLAYER_ID));
    ASSERT_EQ(1u, ws.count());
    const std::string wire = ws.lastMessage();

    EXPECT_TRUE(contains(wire, trackBytes("First", "Ann", "61", "track_a", "Alpha")))
        << wire;
    EXPECT_EQ(3u, occurrences(wire, "\"album\":"));
}

/*******************************************************************************
 * 3. TYPE STRICTNESS ON THE WIRE.
 *
 * Everything this chain emits is a STRING, including the counters. An int that
 * became a JSON number would break the type-strict oracle of the goldens
 * (3 != "3"), and it would be invisible to a reader who only looks at values.
 * These read the QUOTES.
 ******************************************************************************/

TEST_F(JsonApiAudioWireBytesTest, CountAndCurrentTrackAreQuotedStrings)
{
    addPlayer();

    HttpTestRequest req;
    req.send(httpPlaylistRequest(PLAYER_ID));
    const std::string wire = req.body();

    EXPECT_TRUE(contains(wire, "\"count\":\"3\"")) << wire;
    EXPECT_TRUE(contains(wire, "\"current_track\":\"1\"")) << wire;
    EXPECT_FALSE(contains(wire, "\"count\":3"));
    EXPECT_FALSE(contains(wire, "\"current_track\":1"));
}

TEST_F(JsonApiAudioWireBytesTest, PlaylistSizeIsAQuotedString)
{
    FakeWirePlayer *player = addPlayer();
    player->overrideSize = true;
    player->playlistSize = 42;

    HttpTestRequest req;
    req.send(httpAudioRequest("get_playlist_size", Json{{ "id", PLAYER_ID }}));

    EXPECT_EQ("{\"playlist_size\":\"42\"}", req.body());
}

TEST_F(JsonApiAudioWireBytesTest, ANegativePlaylistSizeIsForwardedAsAString)
{
    FakeWirePlayer *player = addPlayer();
    player->overrideSize = true;
    player->playlistSize = -7;

    HttpTestRequest req;
    req.send(httpAudioRequest("get_playlist_size", Json{{ "id", PLAYER_ID }}));

    EXPECT_EQ("{\"playlist_size\":\"-7\"}", req.body());
}

/*******************************************************************************
 * 4. Utils::to_string(double) THROUGH THE API - THE MOST TEMPTING THING IN THE
 *    EPIC TO "CORRECT". These read the bytes WITH their quotes.
 ******************************************************************************/

TEST_F(JsonApiAudioWireBytesTest, TimeElapsedKeepsTheBareOstringstreamShape)
{
    FakeWirePlayer *player = addPlayer();
    player->currentTime = 1234.56789;

    HttpTestRequest req;
    req.send(httpAudioRequest("get_time", Json{{ "id", PLAYER_ID }}));

    //Six significant digits, and it is a STRING.
    EXPECT_EQ("{\"time_elapsed\":\"1234.57\"}", req.body());
}

TEST_F(JsonApiAudioWireBytesTest, ALargeTimeElapsedGoesScientificOnTheWire)
{
    FakeWirePlayer *player = addPlayer();
    player->currentTime = 123456789.0;

    HttpTestRequest req;
    req.send(httpAudioRequest("get_time", Json{{ "id", PLAYER_ID }}));

    EXPECT_EQ("{\"time_elapsed\":\"1.23457e+08\"}", req.body());
}

TEST_F(JsonApiAudioWireBytesTest, AWholeTimeElapsedHasNoDecimalPointOnTheWire)
{
    FakeWirePlayer *player = addPlayer();
    player->currentTime = 42.0;

    HttpTestRequest req;
    req.send(httpAudioRequest("get_time", Json{{ "id", PLAYER_ID }}));

    EXPECT_EQ("{\"time_elapsed\":\"42\"}", req.body());
}

//Same value, WS side: only the envelope differs, the payload bytes do not.
//MOVED: the envelope sorts. "1234.57" is untouched, and that is the point.
TEST_F(JsonApiAudioWireBytesTest, WsTimeElapsedCarriesTheSamePayloadBytes)
{
    FakeWirePlayer *player = addPlayer();
    player->currentTime = 1234.56789;

    WsTestSession ws;
    ws.send(wsAudioRequest("get_time", Json{{ "id", PLAYER_ID }}));

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("{\"data\":{\"time_elapsed\":\"1234.57\"},"
              "\"msg\":\"audio\",\"msg_id\":\"1\"}",
              ws.lastMessage());
}

/*******************************************************************************
 * 5. ESCAPING. The wire has ALWAYS been pure ASCII on this chain (jansson dumps
 *    with JSON_ENSURE_ASCII), and it must stay pure ASCII: that INVARIANT is
 *    the one thing that protects a music database full of accented album names
 *    from a wire change nobody asked for. What moves is only the CASE of the
 *    hexadecimal.
 ******************************************************************************/

//INVARIANT, and the important one: not one byte >= 0x80 leaves the process.
TEST_F(JsonApiAudioWireBytesTest, TheWireStaysPureAsciiOnAnAccentedPlaylist)
{
    FakeWirePlayer *player = addPlayer();
    player->items[1] = trackParams("track_b", std::string("Caf") + RAW_E_ACUTE,
                                   "Bob", "Second", "122");

    WsTestSession ws;
    ws.send(wsPlaylistRequest(PLAYER_ID));
    ASSERT_EQ(1u, ws.count());

    const std::string wire = ws.lastMessage();
    EXPECT_TRUE(isPureAscii(wire)) << wire;
    EXPECT_FALSE(contains(wire, RAW_E_ACUTE));

    HttpTestRequest req;
    req.send(httpPlaylistRequest(PLAYER_ID));
    EXPECT_TRUE(isPureAscii(req.body())) << req.body();
}

//MOVED: jansson wrote the hexadecimal in UPPER case, nlohmann writes it lower.
//The wire is still pure ASCII, which is the invariant that matters.
TEST_F(JsonApiAudioWireBytesTest, WsAnAccentedTrackTitleIsEscapedLowerCase)
{
    FakeWirePlayer *player = addPlayer();
    player->items[1] = trackParams("track_b", std::string("Caf") + RAW_E_ACUTE,
                                   "Bob", "Second", "122");

    WsTestSession ws;
    ws.send(wsPlaylistRequest(PLAYER_ID));
    ASSERT_EQ(1u, ws.count());

    const std::string wire = ws.lastMessage();
    EXPECT_TRUE(contains(wire, std::string("\"title\":\"Caf") + ASCII_E_LOWER + "\"")) << wire;
    EXPECT_FALSE(contains(wire, ASCII_E_UPPER));
}

TEST_F(JsonApiAudioWireBytesTest, HttpAnAccentedTrackTitleIsEscapedLowerCase)
{
    FakeWirePlayer *player = addPlayer();
    player->items[0] = trackParams("track_a", std::string("Caf") + RAW_E_ACUTE,
                                   "Ann", "First", "61");

    HttpTestRequest req;
    req.send(httpPlaylistRequest(PLAYER_ID));

    const std::string wire = req.body();
    EXPECT_TRUE(contains(wire, std::string("\"title\":\"Caf") + ASCII_E_LOWER + "\"")) << wire;
    EXPECT_FALSE(contains(wire, ASCII_E_UPPER));
}

//The single shot methods carry player text too - a cover URL comes from the
//same helper as the track metadata.
TEST_F(JsonApiAudioWireBytesTest, HttpAnAccentedCoverUrlIsEscapedLowerCase)
{
    FakeWirePlayer *player = addPlayer();
    player->coverUrl = std::string("http://calaos.fr/caf") + RAW_E_ACUTE + ".jpg";

    HttpTestRequest req;
    req.send(httpAudioRequest("get_cover_url", Json{{ "id", PLAYER_ID }}));

    EXPECT_EQ(std::string("{\"cover\":\"http://calaos.fr/caf") + ASCII_E_LOWER + ".jpg\"}",
              req.body());
}

//MOVED: 0x7F is ASCII, so jansson wrote the raw byte; nlohmann escapes
//everything >= 0x7F when ensure_ascii is on (json.hpp:18467). +5 bytes on the
//wire, and Content-Length follows.
TEST_F(JsonApiAudioWireBytesTest, ADelByteInATrackTitleIsEscaped)
{
    FakeWirePlayer *player = addPlayer();
    player->items[0] = trackParams("track_a", std::string("A") + RAW_DEL + "B",
                                   "Ann", "First", "61");

    HttpTestRequest req;
    req.send(httpPlaylistRequest(PLAYER_ID));

    const std::string wire = req.body();
    EXPECT_TRUE(contains(wire, std::string("\"title\":\"A") + ASCII_DEL + "B\"")) << wire;
    EXPECT_FALSE(contains(wire, RAW_DEL));
}

/* MOVED, and this one is a VALUE that stops being silently lost: json_string()
 * took a const char*, so the title STOPPED at the NUL and the client got "A".
 * nlohmann takes the std::string whole and escapes the NUL. Nothing in the
 * tree produces such a title today - it is a robustness contract, not a
 * behaviour anyone depends on.
 */
TEST_F(JsonApiAudioWireBytesTest, AnEmbeddedNulNoLongerTruncatesATrackTitle)
{
    FakeWirePlayer *player = addPlayer();
    player->items[0] = trackParams("track_a", std::string("A\0B", 3),
                                   "Ann", "First", "61");

    HttpTestRequest req;
    req.send(httpPlaylistRequest(PLAYER_ID));

    const std::string wire = req.body();
    EXPECT_TRUE(contains(wire, std::string("\"title\":\"A") + ASCII_NUL + "B\"")) << wire;
    EXPECT_FALSE(contains(wire, "\"title\":\"A\""));
}

/*******************************************************************************
 * 6. INVALID UTF-8 COMING BACK FROM THE PLAYER.
 *
 * The realistic channel of this chain (see the file header). Three assertions
 * must hold on BOTH sides of the migration - the response is DELIVERED, it is a
 * 200, and NOTHING was closed - because the moment this builder answers a Json,
 * a bare dump() would be std::terminate on a live connection.
 ******************************************************************************/

/* MOVED, and it is a STRUCTURE delta - the only kind a golden could ever see.
 * jansson dropped the whole pair (json_string() answered NULL and
 * json_object_set_new() answered -1, neither return code tested); the pair is
 * now KEPT, with one U+FFFD per invalid byte, by error_handler_t::replace.
 * Same behaviour change E4.1o declared for get_param, on the same grounds:
 * a mangled, visible value beats a silently missing one.
 */
TEST_F(JsonApiAudioWireBytesTest, AnInvalidUtf8TrackTitleIsNoLongerDropped)
{
    FakeWirePlayer *player = addPlayer();
    player->items[0] = trackParams("track_a", INVALID_UTF8_PROBE,
                                   "Ann", "First", "61");

    HttpTestRequest req;
    req.send(httpPlaylistRequest(PLAYER_ID));

    const std::string wire = req.body();
    //The pair is there, replaced, and the other four members are untouched.
    EXPECT_TRUE(contains(wire, std::string("{\"album\":\"First\",\"artist\":\"Ann\","
                                           "\"duration\":\"61\",\"id\":\"track_a\","
                                           "\"title\":\"") + ASCII_FFFD_FFFD_X + "\"}")) << wire;
    //Three tracks, three titles - the count is what says the pair came back.
    EXPECT_EQ(3u, occurrences(wire, "\"title\":"));
}

//INVARIANT - the terminate question, HTTP side.
TEST_F(JsonApiAudioWireBytesTest, HttpAnInvalidUtf8TrackTitleIsDeliveredAndTheConnectionSurvives)
{
    FakeWirePlayer *player = addPlayer();
    player->items[0] = trackParams("track_a", INVALID_UTF8_PROBE,
                                   "Ann", "First", "61");

    HttpTestRequest req;
    req.send(httpPlaylistRequest(PLAYER_ID));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_FALSE(req.body().empty());
    EXPECT_TRUE(req.closes().empty());
    EXPECT_TRUE(isPureAscii(req.body())) << req.body();
}

//INVARIANT - the terminate question, WS side. Exactly one message, and the
//session was not closed under us.
TEST_F(JsonApiAudioWireBytesTest, WsAnInvalidUtf8TrackTitleIsDeliveredAndTheConnectionSurvives)
{
    FakeWirePlayer *player = addPlayer();
    player->items[2] = trackParams("track_c", INVALID_UTF8_PROBE,
                                   "Cid", "Third", "183");

    WsTestSession ws;
    ws.send(wsPlaylistRequest(PLAYER_ID));

    ASSERT_EQ(1u, ws.count());
    EXPECT_TRUE(ws.closes().empty());
    EXPECT_TRUE(isPureAscii(ws.lastMessage())) << ws.lastMessage();
}

//INVARIANT - same question on a single shot method, whose payload is a
//std::string straight out of the player rather than a Params.
TEST_F(JsonApiAudioWireBytesTest, AnInvalidUtf8CoverUrlIsDeliveredAndTheConnectionSurvives)
{
    FakeWirePlayer *player = addPlayer();
    player->coverUrl = std::string("http://calaos.fr/") + INVALID_UTF8_PROBE;

    HttpTestRequest req;
    req.send(httpAudioRequest("get_cover_url", Json{{ "id", PLAYER_ID }}));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_FALSE(req.body().empty());
    EXPECT_TRUE(req.closes().empty());
}

//MOVED on the same request: the cover pair used to disappear entirely and the
//client got {}. It is now delivered, replaced.
TEST_F(JsonApiAudioWireBytesTest, AnInvalidUtf8CoverUrlIsNoLongerDropped)
{
    FakeWirePlayer *player = addPlayer();
    player->coverUrl = std::string("http://calaos.fr/") + INVALID_UTF8_PROBE;

    HttpTestRequest req;
    req.send(httpAudioRequest("get_cover_url", Json{{ "id", PLAYER_ID }}));

    EXPECT_EQ(std::string("{\"cover\":\"http://calaos.fr/") + ASCII_FFFD_FFFD_X + "\"}",
              req.body());
}

/*******************************************************************************
 * 7. THE RECURSION - decodeGetPlaylist() -> getNextPlaylistItem(), ONE NETWORK
 *    ROUND TRIP PER TRACK.
 *
 * This is the delicate part of the ticket: the accumulated document is carried
 * ACROSS an async boundary, by a function that calls itself. A migration that
 * carried it by reference would hand a dangling reference to the next stage; a
 * migration that lost it would answer a playlist missing its tail. Neither
 * crashes on the nominal path, and neither is visible on a 3 item playlist that
 * unrolls synchronously - so these cases drive the stages one at a time.
 ******************************************************************************/

TEST_F(JsonApiAudioWireBytesTest, ADeferredPlaylistIsAnsweredWholeAndInOrder)
{
    FakeWirePlayer *player = addPlayer();
    queue.deferred = true;

    WsTestSession ws;
    ws.send(wsPlaylistRequest(PLAYER_ID));

    //Nothing answered yet: only get_playlist_current is in flight.
    EXPECT_EQ(0u, ws.count());
    ASSERT_EQ(1u, queue.count());

    ASSERT_TRUE(queue.fireNext());          //current track
    ASSERT_TRUE(queue.fireNext());          //playlist size

    //One stage per track, each one chained by the answer of the previous one.
    for (int i = 0; i < 3; i++)
    {
        ASSERT_EQ(1u, queue.count()) << "at stage " << i;
        EXPECT_EQ(0u, ws.count()) << "answered before the last track, at stage " << i;
        ASSERT_TRUE(queue.fireNext());
    }

    EXPECT_EQ(0u, queue.count());
    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ(std::vector<int>({ 0, 1, 2 }), player->requestedItems);

    //And the bytes are the ones the synchronous unroll produces: the document
    //survived three async hops without losing or reordering a track.
    const std::string wire = ws.lastMessage();
    EXPECT_TRUE(contains(wire, trackBytes("First",  "Ann", "61",  "track_a", "Alpha"))) << wire;
    EXPECT_TRUE(contains(wire, trackBytes("Second", "Bob", "122", "track_b", "Beta")))  << wire;
    EXPECT_TRUE(contains(wire, trackBytes("Third",  "Cid", "183", "track_c", "Gamma"))) << wire;
    EXPECT_LT(wire.find("Alpha"), wire.find("Beta"));
    EXPECT_LT(wire.find("Beta"), wire.find("Gamma"));
    EXPECT_TRUE(contains(wire, "\"count\":\"3\"")) << wire;
}

//A single track: the tail branch is the one that both appends the last item AND
//answers, so an off by one there hides on a longer list.
TEST_F(JsonApiAudioWireBytesTest, ASingleTrackPlaylistIsWholeOnTheWire)
{
    FakeWirePlayer *player = addPlayer();
    player->items.resize(1);

    HttpTestRequest req;
    req.send(httpPlaylistRequest(PLAYER_ID));

    EXPECT_EQ("{\"count\":\"1\",\"current_track\":\"1\",\"items\":["
              + trackBytes("First", "Ann", "61", "track_a", "Alpha") + "]}",
              req.body());
    EXPECT_EQ(std::vector<int>({ 0 }), player->requestedItems);
}

//count <= 0 short circuits the recursion entirely.
TEST_F(JsonApiAudioWireBytesTest, AnEmptyPlaylistHasAnEmptyItemsArray)
{
    FakeWirePlayer *player = addPlayer();
    player->items.clear();

    HttpTestRequest req;
    req.send(httpPlaylistRequest(PLAYER_ID));

    EXPECT_EQ("{\"count\":\"0\",\"current_track\":\"1\",\"items\":[]}", req.body());
    EXPECT_TRUE(player->requestedItems.empty());
}

/* LIFETIME. The client leaves in the middle of the recursion. The late answers
 * must not touch the dead handler, the recursion must stop, and result_lambda
 * must never fire. Before T3.17a this was a heap-use-after-free; after the
 * migration it is also the place where a dangling Json& would live.
 */
TEST_F(JsonApiAudioWireBytesTest, ClientGoneMidRecursionAnswersNothingAndStops)
{
    FakeWirePlayer *player = addPlayer();
    queue.deferred = true;

    {
        WsTestSession ws;
        ws.send(wsPlaylistRequest(PLAYER_ID));
        ASSERT_TRUE(queue.fireNext());      //current track
        ASSERT_TRUE(queue.fireNext());      //playlist size -> item 0 in flight
        ASSERT_TRUE(queue.fireNext());      //item 0        -> item 1 in flight
        ASSERT_EQ(std::vector<int>({ 0, 1 }), player->requestedItems);
        ASSERT_EQ(1u, queue.count());
        //ws dies with one track still to fetch
    }

    int fired = 0;
    while (queue.fireNext())
        fired++;

    //Item 1 answered to nobody and chained nothing.
    EXPECT_EQ(1, fired);
    EXPECT_EQ(std::vector<int>({ 0, 1 }), player->requestedItems);
}

/* The player IO is deleted through the API while an answer is in flight. The
 * client is still there and MUST get an answer - and that answer must not be a
 * playlist missing its tail. It is the same success:false the entry point
 * answers for an id that is not a player.
 */
TEST_F(JsonApiAudioWireBytesTest, PlayerDeletedMidRecursionAnswersFalseNotATruncatedPlaylist)
{
    FakeWirePlayer *player = addPlayer();
    queue.deferred = true;

    WsTestSession ws;
    ws.send(wsPlaylistRequest(PLAYER_ID));
    ASSERT_TRUE(queue.fireNext());          //current track
    ASSERT_TRUE(queue.fireNext());          //playlist size -> item 0 in flight
    ASSERT_TRUE(queue.fireNext());          //item 0        -> item 1 in flight
    EXPECT_EQ(0u, ws.count());

    //The connection object owns the pending answer and outlives the IO.
    std::function<void()> lateAnswer = queue.takeNext();
    ASSERT_TRUE((bool)lateAnswer);
    ASSERT_TRUE(deleteIO(player));
    player = nullptr;

    lateAnswer();                           //item 1 answers to a dead player
    lateAnswer = std::function<void()>();

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("{\"data\":{\"success\":\"false\"},"
              "\"msg\":\"get_playlist\",\"msg_id\":\"1\"}", ws.lastMessage());
    EXPECT_FALSE(contains(ws.lastMessage(), "items"));
}

/*******************************************************************************
 * 8. THE ID LOOKUP - getAudioPlayer(). It reads the CLIENT document, so its
 *    migration changes the reader, not only the writer. These pin the contract
 *    of jansson_string_get(): a default on an absent member, on a member that
 *    is not a JSON string, and on a root that is not an object.
 ******************************************************************************/

TEST_F(JsonApiAudioWireBytesTest, AnEmptyPlayerIdIsRefusedBeforeThePlayer)
{
    addPlayer();

    HttpTestRequest req;
    req.send(httpAudioRequest("get_time", Json{{ "id", "" }}));

    EXPECT_EQ("{\"error\":\"empty player id\"}", req.body());
}

//An absent id and an empty one are the same thing to this reader.
TEST_F(JsonApiAudioWireBytesTest, AnAbsentIdMemberIsTheSameAsAnEmptyOne)
{
    addPlayer();

    HttpTestRequest req;
    req.send(httpAudioRequest("get_time", Json::object()));

    EXPECT_EQ("{\"error\":\"empty player id\"}", req.body());
}

//A NON STRING id is treated as ABSENT, not converted. `j["id"].get<string>()`
//would throw here; jansson_string_get() and its nlohmann transcription both
//answer the default.
TEST_F(JsonApiAudioWireBytesTest, ANumericIdMemberIsTreatedAsAbsent)
{
    addPlayer();

    HttpTestRequest req;
    req.send(httpAudioRequest("get_time", Json{{ "id", 42 }}));

    EXPECT_EQ("{\"error\":\"empty player id\"}", req.body());
}

TEST_F(JsonApiAudioWireBytesTest, WsANumericIdMemberIsTreatedAsAbsent)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsAudioRequest("get_time", Json{{ "id", 42 }}));

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("{\"data\":{\"error\":\"empty player id\"},"
              "\"msg\":\"audio\",\"msg_id\":\"1\"}", ws.lastMessage());
}

TEST_F(JsonApiAudioWireBytesTest, AnUnknownPlayerIdIsRefusedWithItsHistoricalSpelling)
{
    addPlayer();

    HttpTestRequest req;
    req.send(httpAudioRequest("get_time", Json{{ "id", "e41p_no_such_io" }}));

    //"unkown", sic - the spelling is part of the wire contract.
    EXPECT_EQ("{\"error\":\"unkown player_id\"}", req.body());
}

//An id that exists but is not an AudioPlayer takes the same branch.
TEST_F(JsonApiAudioWireBytesTest, AnIdThatIsNotAPlayerIsRefusedTheSameWay)
{
    HttpTestRequest req;
    req.send(httpAudioRequest("get_time", Json{{ "id", ID_BOOL_IN }}));

    EXPECT_EQ("{\"error\":\"unkown player_id\"}", req.body());
}

/*******************************************************************************
 * 9. THE ITEM ARGUMENT of get_playlist_item - the second thing this perimeter
 *    reads out of the client document.
 ******************************************************************************/

TEST_F(JsonApiAudioWireBytesTest, GetPlaylistItemAnswersTheTrackItselfFlat)
{
    addPlayer();

    HttpTestRequest req;
    req.send(httpAudioRequest("get_playlist_item",
                              Json{{ "id", PLAYER_ID }, { "item", "2" }}));

    EXPECT_EQ(trackBytes("Third", "Cid", "183", "track_c", "Gamma"), req.body());
}

TEST_F(JsonApiAudioWireBytesTest, AMissingItemArgumentIsRefused)
{
    addPlayer();

    HttpTestRequest req;
    req.send(httpAudioRequest("get_playlist_item", Json{{ "id", PLAYER_ID }}));

    EXPECT_EQ("{\"error\":\"wrong item\"}", req.body());
}

//Same contract as the id: a JSON NUMBER is not a JSON string, so the reader
//sees nothing and the gate refuses. A client that sends {"item":2} instead of
//{"item":"2"} is refused today and must stay refused.
TEST_F(JsonApiAudioWireBytesTest, ANumericItemArgumentIsTreatedAsAbsent)
{
    FakeWirePlayer *player = addPlayer();

    HttpTestRequest req;
    req.send(httpAudioRequest("get_playlist_item",
                              Json{{ "id", PLAYER_ID }, { "item", 2 }}));

    EXPECT_EQ("{\"error\":\"wrong item\"}", req.body());
    EXPECT_TRUE(player->requestedItems.empty());
}

TEST_F(JsonApiAudioWireBytesTest, ANonNumericItemArgumentIsRefusedBeforeThePlayer)
{
    FakeWirePlayer *player = addPlayer();

    HttpTestRequest req;
    req.send(httpAudioRequest("get_playlist_item",
                              Json{{ "id", PLAYER_ID }, { "item", "two" }}));

    EXPECT_EQ("{\"error\":\"wrong item\"}", req.body());
    EXPECT_TRUE(player->requestedItems.empty());
}

/*******************************************************************************
 * 10. THE audio_db SIDE - E4.1q's DISPATCHER, PINNED HERE ON PURPOSE.
 *
 * MEASURED, and it is what draws the line of this ticket: get_stats is NOT an
 * `audio` action. audioGetDbStats() is dispatched by processAudioDb()
 * (JsonApiHandlerHttp.cpp:829, JsonApiHandlerWS.cpp:455), together with the
 * fourteen audioDbGet* - so it sits behind E4.1q's dispatcher, exactly like
 * audioDbUnavailable() and processDbResult() do. This ticket therefore leaves
 * the three of them in jansson and says so; see E4.1p.md.
 *
 * These three cases are the guard on that boundary: nothing on the audio_db
 * side may move by one byte because the `audio` side was migrated. They are
 * INVARIANTS on both sides of this ticket, and they become E4.1q's delta cases.
 ******************************************************************************/

//get_stats over the audio_db action, which is the only way to reach it.
TEST_F(JsonApiAudioWireBytesTest, AudioDbGetStatsAnswersTheDatabaseParamsPlusItsAction)
{
    addPlayer();

    HttpTestRequest req;
    req.send(httpAudioDbRequest("get_stats", Json{{ "id", PLAYER_ID }}));

    //Params is a std::map: albums, artists, audio_action, songs.
    EXPECT_EQ("{\"albums\":\"12\",\"artists\":\"7\","
              "\"audio_action\":\"get_stats\",\"songs\":\"134\"}", req.body());
}

//audioDbUnavailable(), reached through audioGetDbStats().
TEST_F(JsonApiAudioWireBytesTest, AudioDbGetStatsWithoutADatabaseIsRefused)
{
    addPlayer(NO_DB_PLAYER, false);

    HttpTestRequest req;
    req.send(httpAudioDbRequest("get_stats", Json{{ "id", NO_DB_PLAYER }}));

    EXPECT_EQ("{\"error\":\"no music database\"}", req.body());
}

/* The same helper, reached through one of the fourteen audioDbGet* this ticket
 * does not touch. The refusal must come back byte for byte identical to the one
 * above - same words, same shape, same emitter. If the two ever differ, the two
 * halves of the audio_db family have drifted apart and a client can tell which
 * one it hit.
 */
TEST_F(JsonApiAudioWireBytesTest, AudioDbGetAlbumsWithoutADatabaseAnswersTheSameBytes)
{
    addPlayer(NO_DB_PLAYER, false);

    HttpTestRequest req;
    req.send(httpAudioDbRequest("get_albums",
                                Json{{ "id", NO_DB_PLAYER },
                                     { "from", "0" }, { "count", "10" }}));

    EXPECT_EQ("{\"error\":\"no music database\"}", req.body());
}

/*******************************************************************************
 * 11. THE WS/HTTP ASYMMETRY, documented by E4.0f and DELIBERATE. The two
 *     transports do not wrap the audio answer the same way, and this ticket
 *     does not harmonise them.
 ******************************************************************************/

TEST_F(JsonApiAudioWireBytesTest, TheWsAnswerIsEnvelopedAndTheHttpOneIsNot)
{
    FakeWirePlayer *player = addPlayer();
    player->overrideSize = true;
    player->playlistSize = 5;

    WsTestSession ws;
    ws.send(wsAudioRequest("get_playlist_size", Json{{ "id", PLAYER_ID }}));
    ASSERT_EQ(1u, ws.count());

    HttpTestRequest req;
    req.send(httpAudioRequest("get_playlist_size", Json{{ "id", PLAYER_ID }}));

    EXPECT_EQ("{\"data\":{\"playlist_size\":\"5\"},"
              "\"msg\":\"audio\",\"msg_id\":\"1\"}", ws.lastMessage());
    EXPECT_EQ("{\"playlist_size\":\"5\"}", req.body());

    //The payload is the SAME object on both sides; only the wrapping differs.
    EXPECT_TRUE(contains(ws.lastMessage(), req.body()));
}

//An audio_action nobody knows is answered by the transport, not by JsonApi, and
//this ticket does not touch that branch.
TEST_F(JsonApiAudioWireBytesTest, AnUnknownAudioActionIsAnsweredByTheTransport)
{
    addPlayer();

    HttpTestRequest req;
    req.send(httpAudioRequest("get_nothing", Json{{ "id", PLAYER_ID }}));

    EXPECT_EQ("{\"error\":\"unkown audio_action\"}", req.body());
}
