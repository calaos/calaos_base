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
 * E4.1q - THE BYTES THE MUSIC DATABASE PUTS ON THE WIRE, AND THE FIFTEEN
 *         TWINS THAT MAKE A COPY-PASTE INVISIBLE.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS FILE EXISTS
 * ---------------------------------------------------------------------------
 * E4.1q migrates the music-database half of JsonApi to nlohmann:
 *
 *      processDbResult()        -> the answer shape of FOURTEEN of the fifteen
 *      audioDbUnavailable()     -> the "no music database" refusal of SIXTEEN
 *      audioGetDbStats()        -> audio_db audio_action=get_stats
 *      the 15 audioDbGet*()     -> the other fifteen audio_db actions
 *      processAudioDb()         -> the dispatcher, on BOTH transports
 *
 * and DELETES the transitional overload getAudioPlayer(json_t *, string &) that
 * E4.1p left behind with this ticket's name on it.
 *
 * The regime of proof of the epic, restated because it is the whole reason for
 * this file: the 145 goldens compare PARSED DOCUMENTS. They are, by
 * construction, blind to key order, to the case of a \uXXXX escape and to
 * whether a byte was escaped at all. The 30+ music-database goldens of T3.17c
 * cover STRUCTURE and VALUES on this perimeter and cover them well; NOTHING
 * covers its BYTE dimension until this file exists.
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
 * MEASURED: 45 cases, 16 of them written as ...Today. The migration made
 * FIFTEEN of them fail and NOT ONE of the other thirty, and no other test of
 * the tree moved - the 145 goldens included. Every case now carries either
 * MOVED (it was a ...Today that really moved) or INVARIANT in its comment.
 *
 * ⚠️ The sixteenth, WsAnUnknownAudioDbActionIsEnveloped, was PREDICTED to move
 * and did NOT. Kept with its measurement instead of quietly renamed: that else
 * branch belongs to the transport, not to the dispatcher, and it already
 * emitted through the nlohmann overload of sendJson() before this ticket. A
 * prediction that a case will move is not evidence; the run is.
 *
 * The INVARIANTS held on both sides. If one of them ever moves, a VALUE or a
 * STRUCTURE changed - stop and understand why before touching it.
 *
 * ---------------------------------------------------------------------------
 * THE DELTAS, ON THIS PERIMETER
 * ---------------------------------------------------------------------------
 *   1. KEY ORDER      processDbResult() inserts "total_count" and THEN "items";
 *                     jansson keeps that insertion order, nlohmann sorts, so
 *                     "items" comes first from now on. This is the ONLY key
 *                     order that moves inside a payload of this perimeter:
 *                     every other object here is built from a Params, which is
 *                     a std::map and therefore ALREADY alphabetical - and a
 *                     case says so. The WS envelope moves too (msg,msg_id,data
 *                     -> data,msg,msg_id), as it did for every other migrated
 *                     action.
 *   2. HEX CASE       jansson writes an accent UPPER case, nlohmann lower case.
 *   3. INVALID UTF-8  jansson drops the WHOLE PAIR (json_string() answers NULL,
 *                     json_object_set_new() answers -1 and nobody looks),
 *                     nlohmann keeps it with one U+FFFD per invalid byte. A
 *                     STRUCTURE delta: an ABSENT KEY BECOMES PRESENT.
 *   4. DEL (0x7F)     jansson writes the raw byte, nlohmann escapes it, because
 *                     with ensure_ascii it escapes every codepoint >= 0x7F.
 *   5. EMBEDDED NUL   jansson takes a const char*: the value is TRUNCATED IN
 *                     SILENCE. nlohmann takes a std::string and keeps it whole,
 *                     escaping the NUL.
 *
 * ---------------------------------------------------------------------------
 * WHERE THE POISON COMES FROM ON *THIS* CHAIN - IT IS NOT THE CLIENT
 * ---------------------------------------------------------------------------
 * Everything this perimeter reads out of the request is an id or an integer,
 * and a JSON body cannot carry invalid UTF-8 at all (both parsers refuse it).
 * The bytes come from the OTHER side: album names, artist names, radio titles
 * and MUSIC FOLDER PATHS are what a squeezebox or a roon helper answers, and
 * ultimately what a filesystem and a pile of file tags hold. A filesystem
 * guarantees NOTHING about UTF-8 - a directory created under a latin-1 locale
 * is a legal directory whose name is not legal UTF-8. That is the realistic
 * channel for this chain and the one the fake database below drives, with
 * audioDbGetMusicFolder() singled out because it is the likeliest of the
 * fifteen to meet such a name.
 *
 * Once processDbResult() answers a Json, such a byte would be a std::terminate
 * on a live connection if the emitter did not carry error_handler_t::replace.
 * It does, since E4.1b (JsonApiHandlerHttp.cpp, JsonApiHandlerWS.cpp). The
 * cases named ...IsDeliveredAndTheConnectionSurvives are the proof it holds:
 * they assert a delivered response, a 200, and an EMPTY closes() list, on both
 * sides of the migration.
 *
 * ---------------------------------------------------------------------------
 * THE FIFTEEN TWINS, AND THE "POOR FIXTURE" TRAP (13+ recorded relapses)
 * ---------------------------------------------------------------------------
 * ON THIS PERIMETER THE TRAP HAS A PRECISE NAME: fifteen functions of 39 lines
 * built on the same mould, and two of them answering THE SAME BYTES. Exchange
 * two such twins - make audioDbGetAlbums() call getArtists() and the reverse -
 * and the whole suite comes back GREEN for the wrong reason. That exchange is
 * the counter-mutation E4.1q.md asks for BY NAME.
 *
 * The fake database below is built so it bites: each of the fourteen list
 * getters answers rows whose id AND name carry its own kind, and a total_count
 * whose VALUE AND WIDTH are its own (2, 31, 412, 53, 6, 77, 812, 9, 104, 1105,
 * 12, 133, 14, 1512 - fourteen distinct numbers, widths 1 to 4). Exchanging any
 * two of them therefore moves the text AND the length of the payload.
 * TheFifteenPayloadsArePairwiseDistinctOnTheWire asserts that property
 * directly, so it cannot rot silently.
 *
 * Ids are prefixed e41q_. Taken so far: e40_, e40b_ .. e40f_, e41b_, e41n_,
 * e41o_, e41p_, t317a_ .. t317f_, t319_.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "AudioPlayer.h"
#include "AudioDB.h"
#include "ListeRoom.h"
#include "Utils.h"

#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char *const PLAYER_ID    = "e41q_player";
const char *const NO_DB_PLAYER = "e41q_nodb_player";

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

/* The pending answers of a database, held OUTSIDE the player: a real
 * squeezebox/roon connection object owns the pending callback and outlives the
 * IO. Same shape as JsonApiMusicDb_test / JsonApiAudioWireBytes_test.
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

//What the database was actually asked for, so a swapped identifier is visible
//even where the answer shape is not.
struct DbCall
{
    std::string method;
    int from = -1;
    int nb = -1;
    std::string id;
    std::string itemId;
    std::string search;
};

/* THE FOURTEEN LIST GETTERS PLUS getTrackInfos AND getStats, each answering a
 * payload that is its OWN - see the "poor fixture" note in the file header. The
 * kind drives the row ids, the row names and the total_count, so no two of the
 * fifteen can answer the same bytes.
 */
class FakeMusicDb: public AudioDB
{
public:
    enum Shape
    {
        Normal,     //a leading "count" marker Params, then two rows
        ZeroCount,  //count is "0" - the items array is cleared
        NoCount,    //no count anywhere - no total_count key at all
    };

    FakeMusicDb(Params &p, AnswerQueue &q): AudioDB(p), queue(q) {}

    Shape shape = Normal;
    std::vector<DbCall> calls;

    //When set, it replaces the NAME of the first row of a list answer. This is
    //the poison injection point: it is a value coming back from the player, not
    //from the client.
    std::string firstRowName;
    //Same, for the title getTrackInfos answers.
    std::string trackTitle = "Fake track";

    void getStats(AudioRequest_cb cb, AudioPlayerData = AudioPlayerData()) override
    {
        DbCall c;
        c.method = "getStats";
        calls.push_back(c);

        AudioPlayerData d;
        d.params.Add("albums", "12");
        d.params.Add("artists", "7");
        d.params.Add("songs", "134");
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
        d.params.Add("title", trackTitle);
        d.params.Add("artist", "Fake artist");
        d.params.Add("album", "Fake album");
        d.params.Add("duration", "245");
        queue.answer(cb, d);
    }

    //The total_count each kind answers. Fourteen distinct values, four widths:
    //this is what makes an exchange of two twins move the LENGTH of the payload
    //as well as its text.
    static std::string countOf(const std::string &kind)
    {
        static const std::map<std::string, std::string> counts = {
            { "album",         "2"    },
            { "artistalbum",   "31"   },
            { "yearalbum",     "412"  },
            { "genreartist",   "53"   },
            { "albumtitle",    "6"    },
            { "playlisttrack", "77"   },
            { "artist",        "812"  },
            { "year",          "9"    },
            { "genre",         "104"  },
            { "playlist",      "1105" },
            { "folder",        "12"   },
            { "searchhit",     "133"  },
            { "radio",         "14"   },
            { "radioitem",     "1512" },
        };
        std::map<std::string, std::string>::const_iterator it = counts.find(kind);
        return it == counts.end() ? std::string("0") : it->second;
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
     * shape processDbResult() reads total_count from - and it appends that
     * marker to the items array as well, which is production behaviour and is
     * pinned as such below.
     */
    AudioPlayerData listData(const std::string &kind) const
    {
        AudioPlayerData d;

        if (shape != NoCount)
        {
            Params marker;
            marker.Add("count", shape == ZeroCount ? "0" : countOf(kind));
            d.vparams.push_back(marker);
        }

        for (int i = 1; i <= 2; i++)
        {
            const std::string idx = Utils::to_string(i);
            Params item;
            item.Add("id", kind + "_" + idx);
            item.Add("name", (i == 1 && !firstRowName.empty())
                             ? firstRowName
                             : kind + " number " + idx);
            d.vparams.push_back(item);
        }

        return d;
    }

    AnswerQueue &queue;
};

class FakeMusicPlayer: public AudioPlayer
{
public:
    FakeMusicPlayer(Params &p, AnswerQueue &q, bool withDb):
        AudioPlayer(p),
        db(dbParams, q)
    {
        if (withDb)
            database = &db;
    }

    FakeMusicDb &fakeDb() { return db; }

    bool canPlaylist() override { return true; }
    bool canDatabase() override { return true; }

private:
    Params dbParams;
    FakeMusicDb db;
};

bool contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

bool isPureAscii(const std::string &s)
{
    for (size_t i = 0; i < s.size(); i++)
        if ((unsigned char)s[i] >= 0x80)
            return false;
    return true;
}

//The exact bytes one row takes on the wire. Params is a std::map, so its two
//members come out ALPHABETICALLY under both libraries - that is the invariant,
//and it is asserted as such below.
std::string rowBytes(const std::string &id, const std::string &name)
{
    return "{\"id\":\"" + id + "\",\"name\":\"" + name + "\"}";
}

//The three rows the items array of a Normal answer holds, in order: the count
//marker that production appends, then the two real rows.
std::string itemsOf(const std::string &kind)
{
    const std::string c = FakeMusicDb::countOf(kind);
    return "[{\"count\":\"" + c + "\"},"
           + rowBytes(kind + "_1", kind + " number 1") + ","
           + rowBytes(kind + "_2", kind + " number 2") + "]";
}

} //namespace

class JsonApiMusicDbWireBytesTest: public JsonApiCharacterizationTest
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
    FakeMusicDb *db = nullptr;

    /* Register a fake player in ListeRoom exactly like a config loaded IO:
     * owned by its room (clearCoreState() deletes it) and reachable through
     * ListeRoom::get_io(), which is what getAudioPlayer() looks it up with.
     */
    FakeMusicPlayer *addPlayer(const std::string &id = PLAYER_ID, bool withDb = true)
    {
        Params p;
        p.Add("id", id);
        p.Add("name", "Fake music player");
        p.Add("type", "FakeMusicPlayer");

        FakeMusicPlayer *player = new FakeMusicPlayer(p, queue, withDb);

        firstRoom()->AddIO(player);
        ListeRoom::Instance().addIOHash(player);

        if (withDb)
            db = &player->fakeDb();
        return player;
    }

    //{"msg":"audio_db","msg_id":"1","data":{"id":...,"audio_action":...,...}}
    static Json wsDbRequest(const std::string &action, Json data,
                            const std::string &id = PLAYER_ID)
    {
        data["id"] = id;
        data["audio_action"] = action;
        return Json{{ "msg", "audio_db" }, { "msg_id", "1" }, { "data", data }};
    }

    //HTTP puts everything flat in the request body.
    static Json httpDbRequest(const std::string &action, Json body,
                              const std::string &id = PLAYER_ID)
    {
        body["action"] = "audio_db";
        body["audio_action"] = action;
        body["id"] = id;
        return authenticated(body);
    }

    //The paging arguments every list method reads. from and count are two
    //DIFFERENT numbers on purpose: a method that reads one where it means the
    //other is a key SWAP, and the swap has to be visible.
    static Json PAGE() { return Json{{ "from", "2" }, { "count", "7" }}; }

    //Drives one audio_db action over HTTP and answers the raw response bytes.
    std::string httpWire(const std::string &action, Json body = Json::object(),
                         const std::string &id = PLAYER_ID)
    {
        HttpTestRequest req;
        req.send(httpDbRequest(action, body, id));
        return req.body();
    }
};

/*******************************************************************************
 * 1. THE WHOLE ANSWER OF A LIST METHOD, BYTE FOR BYTE, ON BOTH TRANSPORTS.
 *
 * These two cases carry, in one string: the envelope, the key order of the
 * answer object built by processDbResult(), the array order of the items, the
 * alphabetical order inside each row, the count marker production appends to
 * the array, and the STRING typing of total_count.
 ******************************************************************************/

//MOVED, as announced: processDbResult() assigns total_count and THEN items,
//jansson kept that insertion order and nlohmann SORTS, so items comes first;
//the WS envelope sorts with it (data before msg). Nothing else in this string
//moved - not one row, not one member, not one quote.
TEST_F(JsonApiMusicDbWireBytesTest, WsGetAlbumsWholeAnswerBytes)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsDbRequest("get_album", PAGE()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("{\"data\":{\"items\":" + itemsOf("album") + ",\"total_count\":\"2\"},"
              "\"msg\":\"audio_db\",\"msg_id\":\"1\"}",
              ws.lastMessage());
}

//MOVED: the same two keys, no envelope on this transport.
TEST_F(JsonApiMusicDbWireBytesTest, HttpGetAlbumsWholeAnswerBytes)
{
    addPlayer();

    HttpTestRequest req;
    req.send(httpDbRequest("get_albums", PAGE()));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_EQ("{\"items\":" + itemsOf("album") + ",\"total_count\":\"2\"}",
              req.body());
}

/*******************************************************************************
 * 2. THE FIFTEEN TWINS - THE ANTI "POOR FIXTURE" ARMOUR, AND THE TARGET OF THE
 *    EXCHANGE COUNTER-MUTATION E4.1q.md ASKS FOR BY NAME.
 *
 * Exchanging two of the fifteen bodies is the copy-paste this ticket is most
 * exposed to. It is only VISIBLE if no two of the fifteen answer the same
 * bytes, so that property is asserted here directly rather than assumed.
 ******************************************************************************/

//INVARIANT - and the precondition of every exchange mutation on this file.
TEST_F(JsonApiMusicDbWireBytesTest, TheFifteenPayloadsArePairwiseDistinctOnTheWire)
{
    addPlayer();

    //HTTP spelling of all fifteen audio_db actions, with the arguments each
    //one needs.
    const std::vector<std::pair<std::string, Json>> actions = {
        { "get_albums",          PAGE() },
        { "get_artist_album",    PAGE() },
        { "get_year_albums",     PAGE() },
        { "get_genre_artists",   PAGE() },
        { "get_album_titles",    PAGE() },
        { "get_playlist_titles", PAGE() },
        { "get_artists",         PAGE() },
        { "get_years",           PAGE() },
        { "get_genres",          PAGE() },
        { "get_playlists",       PAGE() },
        { "get_music_folder",    PAGE() },
        { "get_search",          PAGE() },
        { "get_radios",          PAGE() },
        { "get_radio_items",     PAGE() },
        { "get_track_infos",     Json{{ "track_id", "trk_77" }} },
    };

    std::set<std::string> wires;
    for (size_t i = 0; i < actions.size(); i++)
    {
        const std::string wire = httpWire(actions[i].first, actions[i].second);
        EXPECT_FALSE(wire.empty()) << actions[i].first;
        EXPECT_TRUE(wires.insert(wire).second)
            << "two of the fifteen answer the SAME bytes, an exchange between "
               "them would be invisible: " << actions[i].first << " -> " << wire;
    }

    EXPECT_EQ(15u, wires.size());
}

//INVARIANT. The four methods whose only argument is the paging pair are the
//easiest pair to confuse, and get_albums / get_artists are the two the ticket
//sheet names. Their bytes must differ in the total_count AND in every row.
TEST_F(JsonApiMusicDbWireBytesTest, AlbumsAndArtistsAnswerTwoDifferentPayloads)
{
    addPlayer();

    const std::string albums  = httpWire("get_albums",  PAGE());
    const std::string artists = httpWire("get_artists", PAGE());

    EXPECT_EQ("{\"items\":" + itemsOf("album") + ",\"total_count\":\"2\"}", albums);
    EXPECT_EQ("{\"items\":" + itemsOf("artist") + ",\"total_count\":\"812\"}", artists);

    EXPECT_NE(albums, artists);
    //Different lengths, not merely different text: an exchange moves the size
    //of the answer too.
    EXPECT_NE(albums.size(), artists.size());
}

//INVARIANT. Same for the two identifier-taking twins that sit next to each
//other in the dispatcher and read a DIFFERENT member of the request.
TEST_F(JsonApiMusicDbWireBytesTest, YearAlbumsAndGenreArtistsAnswerTwoDifferentPayloads)
{
    addPlayer();

    Json y = PAGE(); y["year"] = "1994";
    Json g = PAGE(); g["genre"] = "gen_4";

    const std::string years  = httpWire("get_year_albums",   y);
    const std::string genres = httpWire("get_genre_artists", g);

    EXPECT_EQ("{\"items\":" + itemsOf("yearalbum") + ",\"total_count\":\"412\"}", years);
    EXPECT_EQ("{\"items\":" + itemsOf("genreartist") + ",\"total_count\":\"53\"}", genres);
    EXPECT_NE(years, genres);
}

/*******************************************************************************
 * 3. THE THREE BRANCHES OF processDbResult(), ON THE BYTES.
 *
 * Fourteen of the fifteen answer through it, so its behaviour IS their response
 * shape. Frozen as it is, oddities included.
 ******************************************************************************/

//MOVED: total_count and items swapped places. The VALUE "0" and the EMPTY array
//are the invariant part - a count of "0" clears the rows but STILL emits
//total_count.
TEST_F(JsonApiMusicDbWireBytesTest, AZeroCountClearsTheItemsAndKeepsTotalCount)
{
    addPlayer();
    db->shape = FakeMusicDb::ZeroCount;

    EXPECT_EQ("{\"items\":[],\"total_count\":\"0\"}",
              httpWire("get_artists", PAGE()));
}

//INVARIANT. No count anywhere means NO total_count KEY AT ALL - absent, never
//null. One key, so no order to move.
TEST_F(JsonApiMusicDbWireBytesTest, NoCountMeansNoTotalCountKeyAndNotANull)
{
    addPlayer();
    db->shape = FakeMusicDb::NoCount;

    const std::string wire = httpWire("get_artists", PAGE());

    EXPECT_EQ("{\"items\":[" + rowBytes("artist_1", "artist number 1") + ","
              + rowBytes("artist_2", "artist number 2") + "]}", wire);
    EXPECT_FALSE(contains(wire, "total_count"));
    EXPECT_FALSE(contains(wire, "null"));
}

//INVARIANT. The count marker Params is appended to the items array as well as
//read for total_count: items[0] of a normal answer is {"count":...} and not a
//row. Production behaviour of every SqueezeboxDB getter, frozen.
TEST_F(JsonApiMusicDbWireBytesTest, TheCountMarkerIsAlsoTheFirstItemOfTheArray)
{
    addPlayer();

    const std::string wire = httpWire("get_genres", PAGE());
    EXPECT_TRUE(contains(wire, "\"items\":[{\"count\":\"104\"},")) << wire;
}

//INVARIANT. A JSON array is ordered in both libraries, and the rows come out in
//the order the database answered them.
TEST_F(JsonApiMusicDbWireBytesTest, TheRowsAreOnTheWireInDatabaseOrder)
{
    addPlayer();

    const std::string wire = httpWire("get_playlists", PAGE());

    const size_t one = wire.find(rowBytes("playlist_1", "playlist number 1"));
    const size_t two = wire.find(rowBytes("playlist_2", "playlist number 2"));

    ASSERT_NE(std::string::npos, one) << wire;
    ASSERT_NE(std::string::npos, two) << wire;
    EXPECT_LT(one, two);
}

//INVARIANT. Params is a std::map: the members of a row come out alphabetically
//under jansson (insertion order IS map order) and under nlohmann (it sorts).
//This is why NO row of this perimeter moves, and it must stay true.
TEST_F(JsonApiMusicDbWireBytesTest, ARowKeepsItsAlphabeticalMemberOrder)
{
    addPlayer();

    const std::string wire = httpWire("get_radios", PAGE());
    EXPECT_TRUE(contains(wire, rowBytes("radio_1", "radio number 1"))) << wire;
    //id before name, and nothing between them.
    EXPECT_FALSE(contains(wire, "\"name\":\"radio number 1\",\"id\""));
}

/*******************************************************************************
 * 4. TYPE STRICTNESS ON THE WIRE.
 *
 * total_count and the count marker are STRINGS and stay strings. An int that
 * became a JSON number would be a contract break the type-strict golden oracle
 * catches (3 != "3") - but only if the value ever reaches a golden, so it is
 * pinned here on the bytes as well, WITH its quotes.
 ******************************************************************************/

//INVARIANT.
TEST_F(JsonApiMusicDbWireBytesTest, TotalCountIsAQuotedString)
{
    addPlayer();

    const std::string wire = httpWire("get_playlists", PAGE());
    EXPECT_TRUE(contains(wire, "\"total_count\":\"1105\"")) << wire;
    EXPECT_FALSE(contains(wire, "\"total_count\":1105"));
}

//INVARIANT. The one method that answers a flat Params and never sees
//processDbResult(): its duration is a string too.
TEST_F(JsonApiMusicDbWireBytesTest, TrackInfosAnswersItsParamsFlatAndAllStrings)
{
    addPlayer();

    EXPECT_EQ("{\"album\":\"Fake album\",\"artist\":\"Fake artist\","
              "\"duration\":\"245\",\"title\":\"Fake track\","
              "\"track_id\":\"trk_77\"}",
              httpWire("get_track_infos", Json{{ "track_id", "trk_77" }}));
}

/*******************************************************************************
 * 5. ESCAPING. The wire has ALWAYS been pure ASCII on this chain (jansson dumps
 *    with JSON_ENSURE_ASCII), and it stays pure ASCII - the invariant
 *    ensure_ascii = true. What changes is the CASE of the hex digits.
 *
 *    This is the perimeter where the change is massively visible in VOLUME:
 *    every album, artist, genre, playlist and folder name is user text.
 ******************************************************************************/

//INVARIANT, and the reason the whole delta is acceptable.
TEST_F(JsonApiMusicDbWireBytesTest, TheWireStaysPureAsciiOnAnAccentedAlbumName)
{
    addPlayer();
    db->firstRowName = std::string("Caf") + RAW_E_ACUTE + " Bleu";

    const std::string wire = httpWire("get_albums", PAGE());
    EXPECT_TRUE(isPureAscii(wire)) << wire;
    EXPECT_FALSE(contains(wire, RAW_E_ACUTE));
}

//MOVED: jansson wrote \u00E9 with UPPER case hex digits, nlohmann writes
//\u00e9. Same length, same position, different case.
TEST_F(JsonApiMusicDbWireBytesTest, HttpAnAccentedAlbumNameIsEscapedLowerCase)
{
    addPlayer();
    db->firstRowName = std::string("Caf") + RAW_E_ACUTE + " Bleu";

    const std::string wire = httpWire("get_albums", PAGE());
    EXPECT_TRUE(contains(wire, std::string("Caf") + ASCII_E_LOWER + " Bleu")) << wire;
    EXPECT_FALSE(contains(wire, std::string("Caf") + ASCII_E_UPPER + " Bleu"));
}

//MOVED: the same, over the other transport, because the two emitters are two
//different functions and one could have been migrated without the other.
TEST_F(JsonApiMusicDbWireBytesTest, WsAnAccentedFolderNameIsEscapedLowerCase)
{
    addPlayer();
    db->firstRowName = std::string("Musique priv") + RAW_E_ACUTE + "e";

    Json f = PAGE(); f["folder_id"] = "fold_5";
    WsTestSession ws;
    ws.send(wsDbRequest("get_music_folder", f));

    ASSERT_EQ(1u, ws.count());
    const std::string wire = ws.lastMessage();
    EXPECT_TRUE(isPureAscii(wire)) << wire;
    EXPECT_TRUE(contains(wire, std::string("Musique priv") + ASCII_E_LOWER + "e")) << wire;
    EXPECT_FALSE(contains(wire, std::string("Musique priv") + ASCII_E_UPPER + "e"));
}

//MOVED: U+007F went out as a RAW byte under JSON_ENSURE_ASCII and is escaped
//from now on - nlohmann escapes every codepoint >= 0x7F. The answer grows by
//five bytes, and Content-Length follows it, which is why that header is
//asserted here on both sides of the migration.
TEST_F(JsonApiMusicDbWireBytesTest, ADelByteInAnAlbumNameIsEscaped)
{
    addPlayer();
    db->firstRowName = std::string("Del") + RAW_DEL + "ta";

    HttpTestRequest req;
    req.send(httpDbRequest("get_albums", PAGE()));

    const std::string wire = req.body();
    EXPECT_TRUE(contains(wire, std::string("Del") + ASCII_DEL + "ta")) << wire;
    EXPECT_FALSE(contains(wire, std::string("Del") + RAW_DEL + "ta"));
    EXPECT_EQ(Utils::to_string(wire.size()), req.header("Content-Length"));
}

//MOVED: json_string() took a const char*, so an embedded NUL TRUNCATED the
//value in silence. nlohmann holds a std::string and keeps it whole.
TEST_F(JsonApiMusicDbWireBytesTest, AnEmbeddedNulNoLongerTruncatesAnAlbumName)
{
    addPlayer();
    db->firstRowName = std::string("Head\0Tail", 9);

    const std::string wire = httpWire("get_albums", PAGE());
    EXPECT_TRUE(contains(wire, std::string("Head") + ASCII_NUL + "Tail")) << wire;
    EXPECT_FALSE(contains(wire, "\"name\":\"Head\""));
}

/*******************************************************************************
 * 6. INVALID UTF-8 COMING BACK FROM THE MUSIC DATABASE.
 *
 * The retournement of this series, third time in a row (E4.1o, E4.1p, here):
 * what jansson DROPPED - the whole key/value pair, silently - is now delivered
 * with one U+FFFD per bad byte. A STRUCTURE delta, and the only one of this
 * ticket: an absent key becomes a present one.
 *
 * The ...IsDeliveredAndTheConnectionSurvives cases are also the proof that
 * error_handler_t::replace is on the emitter: without it, dump() throws
 * type_error.316 with no catch above it and the process TERMINATES. They assert
 * a delivered answer, a 200, and an EMPTY closes() list.
 ******************************************************************************/

//MOVED, and this one is a STRUCTURE delta: json_string() answered NULL on the
//bad bytes, json_object_set_new() answered -1 and NOBODY LOOKED, so the "name"
//member of the poisoned row DISAPPEARED from the answer. It is now present and
//carries two U+FFFD.
TEST_F(JsonApiMusicDbWireBytesTest, AnInvalidUtf8AlbumNameIsNoLongerDropped)
{
    addPlayer();
    db->firstRowName = INVALID_UTF8_PROBE;

    const std::string wire = httpWire("get_albums", PAGE());

    //The pair is delivered: the row is no longer an id-only object.
    EXPECT_TRUE(contains(wire, rowBytes("album_1", ASCII_FFFD_FFFD_X))) << wire;
    EXPECT_FALSE(contains(wire, "{\"id\":\"album_1\"}"));
    //The row that was NOT poisoned is untouched either way.
    EXPECT_TRUE(contains(wire, rowBytes("album_2", "album number 2"))) << wire;
}

//INVARIANT on the delivery, MOVED on the content: whatever the escaping, the
//answer reaches the client, the status is 200 and NOTHING was closed. This is
//the case that would fail - by taking the process down - if a dump() on this
//chain lost error_handler_t::replace.
TEST_F(JsonApiMusicDbWireBytesTest, HttpAnInvalidUtf8AlbumNameIsDeliveredAndTheConnectionSurvives)
{
    addPlayer();
    db->firstRowName = INVALID_UTF8_PROBE;

    HttpTestRequest req;
    req.send(httpDbRequest("get_albums", PAGE()));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_FALSE(req.body().empty());
    EXPECT_TRUE(isPureAscii(req.body())) << req.body();
    EXPECT_TRUE(req.closes().empty());
}

TEST_F(JsonApiMusicDbWireBytesTest, WsAnInvalidUtf8AlbumNameIsDeliveredAndTheConnectionSurvives)
{
    addPlayer();
    db->firstRowName = INVALID_UTF8_PROBE;

    WsTestSession ws;
    ws.send(wsDbRequest("get_album", PAGE()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_TRUE(isPureAscii(ws.lastMessage())) << ws.lastMessage();
    EXPECT_TRUE(ws.closes().empty());
}

/* ⭐ THE CASE E4.1q.md ASKS FOR BY NAME (acceptance 4). A MUSIC FOLDER PATH is
 * the likeliest of the fifteen payloads to hold bytes that are not UTF-8: a
 * filesystem guarantees nothing, and a directory created under a latin-1 locale
 * is a perfectly legal directory. It must traverse audioDbGetMusicFolder()
 * without a terminate, and come out as U+FFFD.
 */
TEST_F(JsonApiMusicDbWireBytesTest, AnInvalidUtf8MusicFolderNameIsDeliveredAndTheConnectionSurvives)
{
    addPlayer();
    db->firstRowName = std::string("Musique") + INVALID_UTF8_PROBE;

    Json f = PAGE(); f["folder_id"] = "fold_5";

    HttpTestRequest req;
    req.send(httpDbRequest("get_music_folder", f));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_FALSE(req.body().empty());
    EXPECT_TRUE(req.closes().empty());
    EXPECT_TRUE(isPureAscii(req.body())) << req.body();
    //The un-poisoned row is delivered either way.
    EXPECT_TRUE(contains(req.body(), rowBytes("folder_2", "folder number 2")))
        << req.body();
}

//MOVED: what the folder name BECOMES. The pair was dropped, it now carries two
//U+FFFD - and the trailing 'x' survives, because replace SUBSTITUTES, it does
//not truncate.
TEST_F(JsonApiMusicDbWireBytesTest, AnInvalidUtf8MusicFolderNameIsNoLongerDropped)
{
    addPlayer();
    db->firstRowName = std::string("Musique") + INVALID_UTF8_PROBE;

    Json f = PAGE(); f["folder_id"] = "fold_5";
    const std::string wire = httpWire("get_music_folder", f);

    EXPECT_TRUE(contains(wire, rowBytes("folder_1",
                                        std::string("Musique") + ASCII_FFFD_FFFD_X))) << wire;
    EXPECT_FALSE(contains(wire, "{\"id\":\"folder_1\"}"));
}

//MOVED. The fifteenth method does not go through processDbResult() at all, so
//its escaping is a SEPARATE emitter path and gets its own case: a track title
//read out of a file tag is exactly as untrustworthy as a folder name. The whole
//"title" pair used to be dropped and the answer had FOUR members; it has five.
TEST_F(JsonApiMusicDbWireBytesTest, AnInvalidUtf8TrackTitleIsNoLongerDropped)
{
    addPlayer();
    db->trackTitle = INVALID_UTF8_PROBE;

    HttpTestRequest req;
    req.send(httpDbRequest("get_track_infos", Json{{ "track_id", "trk_77" }}));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_TRUE(req.closes().empty());
    EXPECT_EQ(std::string("{\"album\":\"Fake album\",\"artist\":\"Fake artist\","
                          "\"duration\":\"245\",\"title\":\"") + ASCII_FFFD_FFFD_X +
              "\",\"track_id\":\"trk_77\"}", req.body());
}

/*******************************************************************************
 * 7. get_stats - NOT AN `audio` ACTION.
 *
 * MEASURED and pinned by E4.1p: audioGetDbStats() is dispatched by
 * processAudioDb(), in the middle of the fourteen twins, so it belongs to this
 * ticket and not to the player one. A request action=audio with
 * audio_action=get_stats is answered "unkown audio_action" - and that is
 * asserted, so nobody re-files it under the wrong dispatcher.
 ******************************************************************************/

//INVARIANT. Params is a std::map: albums, artists, audio_action, songs, on both
//libraries. The audio_action the method adds to the answer is production
//behaviour, frozen.
TEST_F(JsonApiMusicDbWireBytesTest, HttpGetStatsAnswersTheDatabaseParamsPlusItsAction)
{
    addPlayer();

    EXPECT_EQ("{\"albums\":\"12\",\"artists\":\"7\","
              "\"audio_action\":\"get_stats\",\"songs\":\"134\"}",
              httpWire("get_stats"));
}

//MOVED: only the WS envelope, and it moved because it sorts. The payload is
//byte for byte what it was - it is built from a Params, which is a std::map.
TEST_F(JsonApiMusicDbWireBytesTest, WsGetStatsIsEnvelopedAsAnAudioDbAnswer)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsDbRequest("get_stats", Json::object()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("{\"data\":{\"albums\":\"12\",\"artists\":\"7\","
              "\"audio_action\":\"get_stats\",\"songs\":\"134\"},"
              "\"msg\":\"audio_db\",\"msg_id\":\"1\"}",
              ws.lastMessage());
}

//INVARIANT. get_stats is reachable through audio_db and ONLY through audio_db.
TEST_F(JsonApiMusicDbWireBytesTest, GetStatsIsNotReachableThroughTheAudioAction)
{
    addPlayer();

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "audio" },
                                { "audio_action", "get_stats" },
                                { "id", PLAYER_ID }}));

    EXPECT_EQ("{\"error\":\"unkown audio_action\"}", req.body());
}

/*******************************************************************************
 * 8. THE REFUSALS - THE SIXTEEN MEMBERS OF THE FAMILY MUST ANSWER THE SAME
 *    BYTES, AND THAT IS THE ACCEPTANCE TEST FOR DELETING THE TRANSITIONAL
 *    getAudioPlayer(json_t *) OVERLOAD.
 *
 * E4.1p had TWO readers of the request id, one nlohmann and one jansson, both
 * calling audioPlayerById() so they could not drift. E4.1q deletes the jansson
 * one and moves its sixteen callers onto the other. Nothing observable may
 * change - and "nothing observable" is these bytes.
 ******************************************************************************/

//INVARIANT. An id that exists but is not an AudioPlayer. Note the misspelling
//of "unkown player_id": production behaviour, frozen as is.
TEST_F(JsonApiMusicDbWireBytesTest, AnUnknownPlayerIdIsRefusedWithItsHistoricalSpelling)
{
    EXPECT_EQ("{\"error\":\"unkown player_id\"}",
              httpWire("get_artists", PAGE(), ID_BOOL_IN));
}

//INVARIANT. An absent id is a DIFFERENT message from an unknown one.
TEST_F(JsonApiMusicDbWireBytesTest, AnEmptyPlayerIdIsRefusedBeforeThePlayer)
{
    EXPECT_EQ("{\"error\":\"empty player id\"}",
              httpWire("get_artists", PAGE(), ""));
}

//INVARIANT, and the exact property the deleted overload existed to protect:
//the fifteen audioDbGet* and audioGetDbStats resolve the player through ONE
//piece of code, so a client cannot tell which half of the family it hit.
TEST_F(JsonApiMusicDbWireBytesTest, GetStatsAndGetAlbumsRefuseAnUnknownPlayerIdentically)
{
    const std::string stats  = httpWire("get_stats",  Json::object(), ID_BOOL_IN);
    const std::string albums = httpWire("get_albums", PAGE(),         ID_BOOL_IN);
    const std::string track  = httpWire("get_track_infos",
                                        Json{{ "track_id", "trk_77" }}, ID_BOOL_IN);

    EXPECT_EQ("{\"error\":\"unkown player_id\"}", stats);
    EXPECT_EQ(stats, albums);
    EXPECT_EQ(stats, track);
}

TEST_F(JsonApiMusicDbWireBytesTest, GetStatsAndGetAlbumsRefuseAnEmptyPlayerIdentically)
{
    const std::string stats  = httpWire("get_stats",  Json::object(), "");
    const std::string albums = httpWire("get_albums", PAGE(),         "");

    EXPECT_EQ("{\"error\":\"empty player id\"}", stats);
    EXPECT_EQ(stats, albums);
}

//INVARIANT. jansson_string_get() answered its default on a member that is not
//a JSON string; the nlohmann reader must keep that contract, or a numeric id
//would start being accepted.
TEST_F(JsonApiMusicDbWireBytesTest, ANumericIdMemberIsTreatedAsAbsent)
{
    addPlayer();

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "audio_db" },
                                { "audio_action", "get_artists" },
                                { "from", "2" }, { "count", "7" },
                                { "id", 42 }}));

    EXPECT_EQ("{\"error\":\"empty player id\"}", req.body());
}

//INVARIANT. audioDbUnavailable(), reached through audioGetDbStats() and through
//one of the fifteen: byte for byte the same refusal, same emitter.
TEST_F(JsonApiMusicDbWireBytesTest, TheNoDatabaseRefusalIsTheSameBytesForTheWholeFamily)
{
    addPlayer(NO_DB_PLAYER, false);

    const std::string stats  = httpWire("get_stats",  Json::object(), NO_DB_PLAYER);
    const std::string albums = httpWire("get_albums", PAGE(),         NO_DB_PLAYER);
    const std::string track  = httpWire("get_track_infos",
                                        Json{{ "track_id", "trk_77" }}, NO_DB_PLAYER);

    EXPECT_EQ("{\"error\":\"no music database\"}", stats);
    EXPECT_EQ(stats, albums);
    EXPECT_EQ(stats, track);
}

/*******************************************************************************
 * 9. THE from/count GATE - THE ORDER OF THE THREE REFUSALS IS SEMANTIC.
 *
 * T3.19 froze it: the paging gate refuses BEFORE audioDbUnavailable(), so the
 * only requests whose answer changed when the null-database check arrived were
 * the ones that used to kill the process. That ordering is a wire contract and
 * these cases are what holds it.
 ******************************************************************************/

//INVARIANT.
TEST_F(JsonApiMusicDbWireBytesTest, AMissingFromAndCountIsRefusedBeforeTheDatabase)
{
    addPlayer();

    EXPECT_EQ("{\"error\":\"wrong from/count\"}", httpWire("get_albums"));
    EXPECT_TRUE(db->calls.empty());
}

//INVARIANT. A non-numeric from is refused, and the database is never called.
TEST_F(JsonApiMusicDbWireBytesTest, ANonNumericFromIsRefusedBeforeTheDatabase)
{
    addPlayer();

    EXPECT_EQ("{\"error\":\"wrong from/count\"}",
              httpWire("get_albums", Json{{ "from", "start" }, { "count", "7" }}));
    EXPECT_TRUE(db->calls.empty());
}

//INVARIANT, and the ORDER case: a player with NO database and a bad paging pair
//answers the PAGING refusal, not the database one.
TEST_F(JsonApiMusicDbWireBytesTest, ThePagingGateRefusesBeforeTheMissingDatabase)
{
    addPlayer(NO_DB_PLAYER, false);

    EXPECT_EQ("{\"error\":\"wrong from/count\"}",
              httpWire("get_albums", Json::object(), NO_DB_PLAYER));
}

//INVARIANT. get_track_infos is the fifteenth method and has NO paging gate: it
//goes straight to the database with whatever track_id it was given.
TEST_F(JsonApiMusicDbWireBytesTest, TrackInfosHasNoPagingGate)
{
    addPlayer();

    EXPECT_EQ("{\"album\":\"Fake album\",\"artist\":\"Fake artist\","
              "\"duration\":\"245\",\"title\":\"Fake track\","
              "\"track_id\":\"\"}",
              httpWire("get_track_infos"));

    ASSERT_EQ(1u, db->calls.size());
    EXPECT_EQ("getTrackInfos", db->calls[0].method);
}

//INVARIANT. from and count are forwarded to the database in THAT order and are
//two different numbers, so a swap between the two is visible.
TEST_F(JsonApiMusicDbWireBytesTest, FromAndCountReachTheDatabaseInThatOrder)
{
    addPlayer();

    httpWire("get_search", Json{{ "from", "2" }, { "count", "7" },
                                { "search", "nirvana" }});

    ASSERT_EQ(1u, db->calls.size());
    EXPECT_EQ("getSearch", db->calls[0].method);
    EXPECT_EQ(2, db->calls[0].from);
    EXPECT_EQ(7, db->calls[0].nb);
    EXPECT_EQ("nirvana", db->calls[0].id);
}

//INVARIANT. The one method carrying three identifiers - the easiest place to
//paste the wrong member name.
TEST_F(JsonApiMusicDbWireBytesTest, RadioItemsForwardsItsThreeIdentifiers)
{
    addPlayer();

    httpWire("get_radio_items", Json{{ "from", "2" }, { "count", "7" },
                                     { "radio_id", "rad_1" },
                                     { "item_id", "it_2" },
                                     { "search", "jazz" }});

    ASSERT_EQ(1u, db->calls.size());
    EXPECT_EQ("getRadiosItems", db->calls[0].method);
    EXPECT_EQ("rad_1", db->calls[0].id);
    EXPECT_EQ("it_2",  db->calls[0].itemId);
    EXPECT_EQ("jazz",  db->calls[0].search);
}

/*******************************************************************************
 * 10. THE DISPATCHER ITSELF - processAudioDb(), ON BOTH TRANSPORTS.
 *
 * Fifteen branches per transport, and the two spell one of them DIFFERENTLY.
 * That asymmetry is documented by E4.0f and DELIBERATE; this ticket moves the
 * dispatcher to nlohmann and must not harmonise it on the way.
 ******************************************************************************/

//INVARIANT. The SAME method answers "get_albums" over HTTP and "get_album" over
//WS, and each transport rejects the other's spelling.
TEST_F(JsonApiMusicDbWireBytesTest, WsRejectsTheHttpSpellingOfGetAlbums)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsDbRequest("get_albums", PAGE()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_TRUE(contains(ws.lastMessage(), "\"error\":\"unkown audio_action\""))
        << ws.lastMessage();
    EXPECT_TRUE(db->calls.empty());
}

//INVARIANT.
TEST_F(JsonApiMusicDbWireBytesTest, HttpRejectsTheWsSpellingOfGetAlbum)
{
    addPlayer();

    EXPECT_EQ("{\"error\":\"unkown audio_action\"}",
              httpWire("get_album", PAGE()));
    EXPECT_TRUE(db->calls.empty());
}

//INVARIANT. An audio_action nobody knows is answered by the transport, not by
//JsonApi, and the database is never reached.
TEST_F(JsonApiMusicDbWireBytesTest, AnUnknownAudioDbActionIsAnsweredByTheTransport)
{
    addPlayer();

    EXPECT_EQ("{\"error\":\"unkown audio_action\"}",
              httpWire("get_nothing", PAGE()));
    EXPECT_TRUE(db->calls.empty());
}

/* ⚠️ NOT a delta, and it was written as one and MEASURED WRONG - kept with its
 * measurement rather than quietly renamed. This else branch is the transport's
 * own and it ALREADY answered through the nlohmann overload of sendJson()
 * before this ticket (the brace-initialised argument binds to the Json
 * overload, not to the json_t* one), so its envelope was already sorted. It is
 * an INVARIANT, and the fact that it did not move is the proof the dispatcher
 * migration did not touch the branch it does not own.
 */
TEST_F(JsonApiMusicDbWireBytesTest, WsAnUnknownAudioDbActionIsEnveloped)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsDbRequest("get_nothing", PAGE()));

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("{\"data\":{\"error\":\"unkown audio_action\"},"
              "\"msg\":\"audio_db\",\"msg_id\":\"1\"}", ws.lastMessage());
}

//MOVED (the two byte strings). The WS/HTTP asymmetry of E4.0f is the INVARIANT
//part: the payload is the SAME object on both sides, only the wrapping differs,
//and that held before and after.
TEST_F(JsonApiMusicDbWireBytesTest, TheWsAnswerIsEnvelopedAndTheHttpOneIsNot)
{
    addPlayer();

    WsTestSession ws;
    ws.send(wsDbRequest("get_years", PAGE()));
    ASSERT_EQ(1u, ws.count());

    const std::string http = httpWire("get_years", PAGE());

    EXPECT_EQ("{\"data\":{\"items\":" + itemsOf("year") + ",\"total_count\":\"9\"},"
              "\"msg\":\"audio_db\",\"msg_id\":\"1\"}", ws.lastMessage());
    EXPECT_EQ("{\"items\":" + itemsOf("year") + ",\"total_count\":\"9\"}", http);

    EXPECT_TRUE(contains(ws.lastMessage(), http));
}

/*******************************************************************************
 * 11. THE ROUND TRIP IS REAL - THE ANSWER CAN COME BACK LATE, AND THE apiAlive
 *     GUARD OF T3.17c MUST SURVIVE THE MIGRATION UNCHANGED.
 *
 * These are not new lifetime cases - JsonApiMusicDb_test.cpp owns those. They
 * are here because a migration that rebuilt the closures by hand could quietly
 * change WHAT they capture, and the byte assertion on a deferred answer is what
 * proves the document still arrives whole.
 ******************************************************************************/

//MOVED on the key order; INVARIANT on the guard.
TEST_F(JsonApiMusicDbWireBytesTest, ADeferredAnswerReachesALiveClientWhole)
{
    addPlayer();
    queue.deferred = true;

    WsTestSession ws;
    ws.send(wsDbRequest("get_genres", PAGE()));

    EXPECT_EQ(0u, ws.count());
    ASSERT_EQ(1u, queue.count());
    ASSERT_TRUE(queue.fireNext());

    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("{\"data\":{\"items\":" + itemsOf("genre") + ",\"total_count\":\"104\"},"
              "\"msg\":\"audio_db\",\"msg_id\":\"1\"}", ws.lastMessage());
}

//INVARIANT. The client leaves while the answer is in flight: nothing is sent,
//nothing is touched. Without the guard this is a use-after-free.
TEST_F(JsonApiMusicDbWireBytesTest, AClientGoneBeforeTheAnswerGetsNothing)
{
    addPlayer();
    queue.deferred = true;

    {
        WsTestSession ws;
        ws.send(wsDbRequest("get_genres", PAGE()));
        ASSERT_EQ(1u, queue.count());
        //ws dies here, exactly as when the client disconnects
    }

    EXPECT_TRUE(queue.fireNext());
}
