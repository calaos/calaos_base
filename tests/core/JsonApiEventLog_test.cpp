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
 * T3.17f - eventlog: the last unguarded asynchronous chain of JsonApi.cpp.
 *
 * JsonApi::buildJsonEventLog() is the twenty sixth and last std::function method
 * of JsonApi.h, and the only one taking a std::function<void(Json &)> where the
 * twenty five others take a std::function<void(json_t *)> result_lambda. That
 * lone signature is why every previous inventory of the T3.17 series walked past
 * it; the T3.17e audit found it, stopped, and ticketed it rather than half fix
 * it (T3.17f.md).
 *
 * ---------------------------------------------------------------------------
 * WHAT IS BEING GUARDED, AND WHY THE COMPILER NEVER SAID A WORD ABOUT IT
 * ---------------------------------------------------------------------------
 * The two callbacks of buildJsonEventLog() do NOT odr-use `this`: their bodies
 * only name `callback`, `page`, `perPage`, `errorMsg`, `event`/`events`. So `[=]`
 * captures no `this`, and the -Wdeprecated implicit-capture warning that the
 * series used as its danger detector says NOTHING here.
 *
 * It is the SAME false negative T3.17c measured on audioDbGetTrackInfos, and the
 * corrected criterion of T3.17.md (section "Correction du critere de diagnostic
 * de la serie") applies verbatim: the danger is ANY freed object reachable from
 * the closure, `this` OR a std::function captured by value that holds it. Here
 * `callback` IS that std::function. It is:
 *
 *   - JsonApiHandlerWS::processEventLog()  (JsonApiHandlerWS.cpp:493-499)
 *         buildJsonEventLog(jsonReq, [=](Json &j) { sendJson("eventlog", j, client_id); });
 *     a [=] lambda that DOES capture the handler's `this` (sendJson is a member),
 *   - JsonApiHandlerHttp::processEventLog() (JsonApiHandlerHttp.cpp:877)
 *         buildJsonEventLog(jsonParam, [this](Json &j) { sendJson(j); });
 *     the same thing spelled explicitly.
 *
 * So the callback that JsonApi hands to HistLogger holds the handler, and
 * HistLogger stores it in a uvw::AsyncHandle OWNED BY THE LOOP
 * (HistLogger.cpp:133-141 and :151-158) while a sqlite worker THREAD does the
 * query. The client can disconnect in that window; ~HttpClient()
 * (HttpClient.cpp:162) deletes the handler, JsonApi base sub-object included;
 * the async handle then fires on a freed object.
 *
 * ---------------------------------------------------------------------------
 * THE SECOND UAF OF THE SERIES IS STRUCTURALLY ABSENT HERE
 * ---------------------------------------------------------------------------
 * T3.17a reproduced a second, independent use-after-free on the playlist chain:
 * a raw AudioPlayer* carried across the round trip and dereferenced afterwards.
 * T3.17b, T3.17c and T3.17d each MEASURED it absent on their own sites and
 * applied nothing, because re-resolving by id where the pointer is never read
 * turns complete answers into errors.
 *
 * On eventlog there is no pointer to measure at all: buildJsonEventLog() takes a
 * Params and a callback, never calls getAudioPlayer(), never casts an IOBase,
 * and never touches ListeRoom. Its whole payload comes from the sqlite rows the
 * worker thread put in HistWorkerAction. That is not a symmetry argument, and it
 * is pinned by measurement all the same:
 * AnswerIsCompleteAfterEveryIoIsDeleted below deletes the entire reference house
 * between the request and the answer and requires the answer to arrive intact.
 *
 * ---------------------------------------------------------------------------
 * NO LEAK TO PLUG EITHER
 * ---------------------------------------------------------------------------
 * T3.17a had to add explicit json_decref() in its guarded branches because the
 * playlist chain carries RAW json_t*. buildJsonEventLog() is the one method of
 * JsonApi.cpp that never touches jansson: everything is nlohmann (Json), it is
 * built AFTER the guard point, and it owns itself. A bare `return` in the
 * guarded branch leaks nothing. Measured with detect_leaks=1.
 *
 * ---------------------------------------------------------------------------
 * FROZEN BUG - NOT EXERCISED HERE ON PURPOSE (FIXED SINCE, BY T3.19)
 * ---------------------------------------------------------------------------
 * eventlog with per_page="0", or with ANY non empty non numeric per_page,
 * DIVIDED BY ZERO in the sqlite worker thread (HistLogger.cpp:268,
 * `rowcount / ac->per_page`). Utils::from_string() writes 0 into its destination
 * on a failed non empty extraction, which overwrote the perPage=100 default of
 * JsonApi.cpp on the very next line. An ABSENT or EMPTY per_page is harmless.
 * Found and documented by E4.0e; the SIGFPE would have taken this whole binary
 * down, so every case below sends an explicit, non zero, numeric per_page.
 * T3.19 now refuses per_page <= 0 in buildJsonEventLog() before HistLogger is
 * called; the cases of that guard live in core/JsonApiInputGuards_test. Every
 * case below is unchanged and still sends a valid per_page - do not "improve"
 * that away, it is what keeps this file a characterization of the paginated
 * path rather than of the refusal.
 *
 * ---------------------------------------------------------------------------
 * TWO COMMITS
 * ---------------------------------------------------------------------------
 * Everything above the LIFETIME banner is the characterization commit: green on
 * an unguarded tree, no line of src/ touched. Everything below it ships WITH the
 * guard, because on an unguarded tree those cases do not fail, they take the
 * process down (measured: SIGSEGV, exit 139, in the WS and HTTP cases). Same
 * split as T3.17b, T3.17c and T3.17d.
 *
 * Ids and goldens are prefixed t317f_ - Config's IO state cache is process wide
 * and never cleared, so prefixes may not be shared between binaries.
 * Taken: e40_, e40b_, e40c_, e40d_, e40e_, e40f_, t317a_, t317b_, t317c_,
 * t317d_, t317e_.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "EventManager.h"
#include "HistLogger.h"
#include "ListeRoom.h"
#include "JsonApi.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

class JsonApiEventLogTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        //MUST run before CoreFixture::SetUp() installs the per-case directories.
        ensureHistLogger();

        JsonApiCharacterizationTest::SetUp();

        /* NO DRAIN HERE. This SetUp() used to end with a pumpEventLoop(),
         * described as a mandatory workaround for the harness defect ticketed
         * E4.0g: the fixture pumped BEFORE CoreFixture::TearDown() destroyed
         * the rooms, so the EventIODeleted that ~Room() raises per IO
         * (Room.cpp:77) survived into the next case.
         *
         * E4.0g landed and moved that pump AFTER the parent teardown, so the
         * backlog is gone at the source and this binary inherits an EMPTY
         * queue - see the contract on JsonApiCharacterizationTest::TearDown().
         * This file was written while E4.0g was still in review, which is why
         * it carried the workaround at all; it was the SIXTH of its kind, and
         * measurement is what retired it: green over --gtest_shuffle seeds
         * 7, 42, 101 and 20260815 without it.
         *
         * If a drain ever becomes necessary here again, a SECOND source of
         * events is surviving TearDown() - name it in FINDINGS.md rather than
         * pumping it away.
         *
         * The eventlog answer is located by its "msg" member below rather than
         * by position, so a stray event could not be mistaken for it either.
         */
    }

    /***************************************************************************
     * BRINGING HistLogger INTO THE HARNESS - the real cost of this ticket.
     *
     * HistLogger is a Meyers singleton whose CONSTRUCTOR captures
     * Utils::getCacheFile("events.db") and spawns a worker thread that opens
     * that path with sqlite. CoreFixture gives every case a private
     * <tmp>/calaos_coretest_XXXXXX tree and rm -rf's it in TearDown(), so a
     * singleton born inside a case points at a directory deleted milliseconds
     * later. The failure is not a bad answer, it is the loss of the binary:
     * `sqlite::database db(dbname)` (HistLogger.cpp:190) sits OUTSIDE the try of
     * :192, so cantopen is an uncaught exception in a std::thread, i.e.
     * std::terminate.
     *
     * So the singleton is built HERE, once, against a directory that belongs to
     * this test file and lives as long as the process. The round trip is not
     * cosmetic: it is what PROVES the worker opened the database before the
     * first case replaces the cache path under it.
     *
     * This solution is E4.0e's, reused deliberately rather than reinvented (its
     * JsonApiSession_test.cpp::ensureHistLogger()); only the directory template
     * and the id prefix differ, so the two binaries cannot share a database.
     **************************************************************************/
    static void ensureHistLogger()
    {
        static bool done = false;
        if (done)
            return;
        done = true;

        static char tmpl[] = "/tmp/calaos_t317f_hist_XXXXXX";
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

    /***************************************************************************
     * Loop pumping with a deadline. eventlog is asynchronous end to end:
     * HistLogger pushes the query to its sqlite worker thread and wakes the main
     * loop back up through a uvw AsyncHandle. Nothing arrives without pumping.
     *
     * The 2s bound is deliberate and well under the 30s read timeout Timer that
     * HttpClient's constructor installs (trap 2 of the harness header), so an
     * HttpTestRequest can safely be held alive across one of these waits.
     **************************************************************************/
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
     * The event log fixture.
     *
     * The events table must be NON EMPTY and hold a row count that is not a
     * multiple of per_page, otherwise total_page and total_count carry the same
     * value everywhere and SWAPPING THE TWO KEYS IN PRODUCTION WOULD LEAVE THE
     * SUITE GREEN - the exact failure mode E4.0b and E4.0c were caught on. Five
     * rows read two at a time give total_count=5, total_page=3, page=0,
     * per_page=2: four distinct values for four keys.
     *
     * Seeded once per process: the sqlite database is opened once and never
     * emptied, so a per-case seed would make the counters depend on the shuffle
     * order.
     *
     * created_at is generated by sqlite (CURRENT_TIMESTAMP, one second
     * resolution), so the event objects are NOT golden material; the cases
     * assert their shape and let the goldens cover the deterministic error
     * payloads. The seeded uuids are remembered so that "id" and "created_at",
     * the two non deterministic strings of the object, cannot be swapped
     * unnoticed (E4.0e review finding R1).
     **************************************************************************/
    static int seededEventCount() { return 5; }

    static std::vector<std::string> &seededUuids()
    {
        static std::vector<std::string> uuids;
        return uuids;
    }

    static bool isSeededUuid(const std::string &s)
    {
        const auto &u = seededUuids();
        return std::find(u.begin(), u.end(), s) != u.end();
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
            e.io_id = "t317f_logged_io_" + Utils::to_string(i);
            e.io_state = (i % 2)? "true": "false";
            e.pic_uid = "t317f_pic_" + Utils::to_string(i);
            //event_raw is stored as TEXT but HistEvent::toJson() PARSES it back
            //(HistLogger.cpp:93-100), so it is a nested object on the wire.
            e.event_raw = "{\"type_str\":\"io_changed\",\"data\":{\"id\":\"" +
                          e.io_id + "\"}}";
            HistLogger::Instance().appendEvent(e);
        }

        //Wait for the worker to have committed all of them, otherwise the first
        //case to read sees a partial table. The result is held in a shared_ptr,
        //never captured by reference: a timed out query's callback would still
        //be alive inside the uvw AsyncHandle and would fire during a later pump.
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

    /***************************************************************************
     * Finding the answer among the messages.
     *
     * A WS session is also an event sink, and the cases that delete IOs raise
     * EventIODeleted into the very same stream. The eventlog answer is therefore
     * located by its "msg" member, never by position or by count.
     **************************************************************************/
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

    //Sends an eventlog request over WS and pumps until the async answer lands.
    static Json wsEventLog(WsTestSession &ws, Json data)
    {
        ws.clear();
        ws.send(Json{{ "msg", "eventlog" }, { "msg_id", "t317f" },
                     { "data", data }});
        waitUntil([&ws]() { return hasEventLogAnswer(ws); });
        return wsEventLogAnswer(ws);
    }

    //The request a bare JsonApi method takes: HTTP and WS both end up handing
    //buildJsonEventLog() a flat Params.
    static Params bareRequest(Json data)
    {
        Params p;
        for (auto it = data.begin(); it != data.end(); ++it)
            p.Add(it.key(), it.value().get<std::string>());
        return p;
    }

    /***************************************************************************
     * Ordering sentinel for the lifetime cases.
     *
     * Those cases destroy the object BEFORE the answer comes back, so there is
     * nothing left to observe the delivery with. The sqlite worker consumes its
     * queue strictly in order (HistLogger::sqliteWorker(), one waitPop per
     * iteration), so a request pushed AFTER the one in flight can only be
     * answered after it. Waiting on this sentinel therefore proves the in flight
     * async has already been delivered - which is exactly the moment the
     * unguarded tree touches freed memory.
     **************************************************************************/
    static void drainPendingAnswers()
    {
        auto done = std::make_shared<bool>(false);
        HistLogger::Instance().getEvents(0, 100,
                [done](bool, string, const vector<HistEvent> &, int, int)
        {
            *done = true;
        });

        ASSERT_TRUE(waitUntil([done]() { return *done; }))
                << "the sqlite worker never answered the sentinel query, the "
                   "in flight callback cannot be assumed delivered";

        //A couple more turns so the AsyncHandle close callbacks also run.
        pumpEventLoop(4);
    }
};

/*******************************************************************************
 * THE PAGINATION PATH - JsonApi.cpp, HistLogger::getEvents() callback
 ******************************************************************************/

TEST_F(JsonApiEventLogTest, PaginatedAnswerCarriesFourRealIntegersAndTheEvents)
{
    //The four counters are JSON NUMBERS, not stringified numbers - the single
    //exception in the whole API (JsonApiHome_test.cpp:135 says so). The oracle
    //is type strict, so "3" != 3.
    //Five rows read two at a time give four DISTINCT values for the four keys,
    //so swapping any two of them fails here.
    seedEventLog();

    WsTestSession ws;
    const Json data = wsEventLog(ws, Json{{ "page", "0" }, { "per_page", "2" }});

    ASSERT_TRUE(data.is_object()) << "eventlog did not answer";
    EXPECT_JSON_EQ(Json(seededEventCount()), member(data, "total_count"));
    EXPECT_JSON_EQ(Json(3), member(data, "total_page"));
    EXPECT_JSON_EQ(Json(0), member(data, "page"));
    EXPECT_JSON_EQ(Json(2), member(data, "per_page"));

    EXPECT_TRUE(member(data, "total_count").is_number_integer());
    EXPECT_TRUE(member(data, "total_page").is_number_integer());
    EXPECT_TRUE(member(data, "page").is_number_integer());
    EXPECT_TRUE(member(data, "per_page").is_number_integer());

    ASSERT_TRUE(member(data, "events").is_array());
    EXPECT_EQ(2u, member(data, "events").size());
    EXPECT_EQ(5u, data.size()) << "the document has exactly five members";
}

TEST_F(JsonApiEventLogTest, TheSecondPageEchoesItsOwnCoordinatesAndCarriesTheSecondSlice)
{
    /* REVIEW FINDING R1 OF T3.17f: EVERY OTHER CASE OF THIS FILE ASKS FOR PAGE
     * ZERO. `page="9"` leaves through the out-of-range error and never reaches
     * the document builder, so nothing here actually PAGINATED, and three
     * mutations survived the suite: pinning the echoed `page` to 0, pinning the
     * echoed `per_page` to 2, and forcing the sqlite offset
     * `int start = ac->page * ac->per_page` (HistLogger.cpp:271) to 0. This one
     * case kills all three, which is why its coordinates are chosen the way
     * they are:
     *   - page 1 with per_page 3 over five rows gives total_count=5,
     *     total_page=2, page=1, per_page=3: FOUR DISTINCT VALUES again, so no
     *     two counters are interchangeable and neither echo can be pinned to a
     *     constant that another case already uses,
     *   - per_page is 3, not 2, so an echo hard-wired to 2 fails here even
     *     though every other case would accept it,
     *   - the slice is checked BY PROVENANCE against page 0's: with the offset
     *     forced to zero, page 1 returns page 0's own rows, so the union of the
     *     two slices collapses from five distinct uuids to three.
     * The last row count is 2, not 3, which is the tail of the table - a fourth
     * thing an offset of zero cannot produce.
     */
    seedEventLog();

    WsTestSession wsFirst;
    const Json first = wsEventLog(wsFirst, Json{{ "page", "0" },
                                                { "per_page", "3" }});
    ASSERT_TRUE(first.is_object()) << "page 0 did not answer";
    ASSERT_EQ(3u, member(first, "events").size());

    WsTestSession wsSecond;
    const Json second = wsEventLog(wsSecond, Json{{ "page", "1" },
                                                  { "per_page", "3" }});
    ASSERT_TRUE(second.is_object()) << "page 1 did not answer";

    //The echoed coordinates are the ones that were ASKED FOR.
    EXPECT_JSON_EQ(Json(1), member(second, "page"));
    EXPECT_JSON_EQ(Json(3), member(second, "per_page"));
    EXPECT_JSON_EQ(Json(2), member(second, "total_page"));
    EXPECT_JSON_EQ(Json(seededEventCount()), member(second, "total_count"));
    EXPECT_TRUE(member(second, "page").is_number_integer());
    EXPECT_TRUE(member(second, "per_page").is_number_integer());

    //The tail of the table: five rows, three skipped, two left.
    ASSERT_TRUE(member(second, "events").is_array());
    ASSERT_EQ(2u, member(second, "events").size())
            << "page 1 did not return the tail of the table";

    //Provenance: the two slices are disjoint and together they are the whole
    //seeded table, every id being one of the uuids this file generated.
    std::set<std::string> ids;
    for (const Json &page: { first, second })
    {
        for (const Json &ev: member(page, "events"))
        {
            const std::string id = str(ev, "id");
            EXPECT_TRUE(isSeededUuid(id)) << "unknown event id " << id;
            ids.insert(id);
        }
    }
    EXPECT_EQ((size_t)seededEventCount(), ids.size())
            << "the two pages overlap, the offset is not being applied";
}

TEST_F(JsonApiEventLogTest, EventObjectsCarrySevenKeysAndANestedEventRaw)
{
    //HistEvent::toJson() (HistLogger.cpp:82-103) renames uuid to "id",
    //stringifies event_type, and PARSES event_raw back into a nested object -
    //the only nested document of this payload.
    seedEventLog();

    WsTestSession ws;
    const Json data = wsEventLog(ws, Json{{ "page", "0" }, { "per_page", "5" }});

    ASSERT_TRUE(member(data, "events").is_array());
    ASSERT_FALSE(member(data, "events").empty());

    const Json ev = member(data, "events")[0];
    ASSERT_TRUE(ev.is_object());
    EXPECT_EQ(7u, ev.size());
    for (const char *k: { "id", "event_type", "io_id", "io_state", "pic_uid",
                          "created_at", "event_raw" })
        EXPECT_TRUE(ev.find(k) != ev.end()) << "missing event key " << k;

    //Everything is a string except event_raw, which is a parsed object.
    EXPECT_TRUE(member(ev, "id").is_string());
    EXPECT_TRUE(member(ev, "event_type").is_string());
    EXPECT_TRUE(member(ev, "io_id").is_string());
    EXPECT_TRUE(member(ev, "io_state").is_string());
    EXPECT_TRUE(member(ev, "pic_uid").is_string());
    EXPECT_TRUE(member(ev, "created_at").is_string());
    EXPECT_TRUE(member(ev, "event_raw").is_object())
            << "event_raw must be a nested object, not the stored text";

    /* EVERY STRING KEY IS PINNED BY PROVENANCE, NOT BY is_string(). Type alone
     * leaves same-typed keys INTERCHANGEABLE: the six string members of this
     * object could be permuted and a suite that only checked their types would
     * stay green. That is the failure mode E4.0b, E4.0c and E4.0e were each
     * caught on (E4.0e review finding R1), and it is why each assertion below
     * says WHERE the value comes from, with the counter-proof on the key it
     * could plausibly be swapped with.
     *
     * "id" is the uuid the caller generated, "created_at" is the sqlite
     * CURRENT_TIMESTAMP, "io_state" is the seeded "true"/"false" - three
     * disjoint value spaces, checked in both directions.
     */
    EXPECT_TRUE(isSeededUuid(str(ev, "id")))
            << "\"id\" does not carry a seeded uuid but " << str(ev, "id");
    EXPECT_FALSE(isSeededUuid(str(ev, "created_at")))
            << "\"created_at\" carries a uuid, the two values are swapped";

    /* REVIEW FINDING R1 OF T3.17f, same shape as E4.0e's: "io_state" was the
     * one key of this object left on is_string() alone, so swapping its value
     * with "created_at"'s in HistEvent::toJson() kept the suite green. The seed
     * already gives it a value no timestamp can have.
     */
    const std::string state = str(ev, "io_state");
    EXPECT_TRUE(state == "true" || state == "false")
            << "\"io_state\" carries " << state << ", not the seeded boolean";
    EXPECT_NE("true", str(ev, "created_at"));
    EXPECT_NE("false", str(ev, "created_at"))
            << "\"created_at\" carries the io_state, the two values are swapped";

    EXPECT_EQ(Utils::to_string((int)CalaosEvent::EventIOChanged),
              str(ev, "event_type"));
    EXPECT_EQ(0u, str(ev, "io_id").find("t317f_logged_io_"));
    EXPECT_EQ(0u, str(ev, "pic_uid").find("t317f_pic_"));
    EXPECT_EQ(str(ev, "io_id"),
              str(member(member(ev, "event_raw"), "data"), "id"));
}

TEST_F(JsonApiEventLogTest, PageOutOfRangeAnswersAnErrorAndNothingElse)
{
    seedEventLog();

    WsTestSession ws;
    const Json data = wsEventLog(ws, Json{{ "page", "9" }, { "per_page", "2" }});

    EXPECT_JSON_GOLDEN("t317f_ws_eventlog_page_out_of_range", data);
    EXPECT_EQ("page is out of range", str(data, "error"));
    //The error payload REPLACES the whole document: no counters, no events.
    EXPECT_TRUE(member(data, "total_count").is_null());
    EXPECT_TRUE(member(data, "events").is_null());
}

TEST_F(JsonApiEventLogTest, ALiveClientStillGetsItsAnswerAfterTheRoundTrip)
{
    /* THE INVARIANT THE GUARD MUST NOT BREAK. T3.17 warns about it in its first
     * paragraph: a token checked at the wrong place, or a bare return added on
     * a path where the object is still alive, makes the answer VANISH. Nothing
     * crashes and nothing is logged; the client waits forever.
     *
     * This case holds the session alive across the ENTIRE round trip, pumps the
     * loop far more than the answer needs, and requires the answer to be there.
     * It is green before the guard and must stay green after it.
     */
    seedEventLog();

    WsTestSession ws;
    ws.send(Json{{ "msg", "eventlog" }, { "msg_id", "t317f" },
                 { "data", Json{{ "page", "0" }, { "per_page", "2" }} }});

    //Nothing can have arrived yet: the answer needs a loop turn.
    EXPECT_FALSE(hasEventLogAnswer(ws))
            << "the answer was synchronous, the round trip is not being exercised";

    ASSERT_TRUE(waitUntil([&ws]() { return hasEventLogAnswer(ws); }));
    pumpEventLoop(8);

    const Json data = wsEventLogAnswer(ws);
    ASSERT_TRUE(data.is_object());
    EXPECT_JSON_EQ(Json(seededEventCount()), member(data, "total_count"));
    EXPECT_EQ(2u, member(data, "events").size());
}

/*******************************************************************************
 * THE UUID PATH - JsonApi.cpp, HistLogger::getEvent() callback
 *
 * A second, entirely separate callback, short circuiting the paging: it answers
 * ONE event document with no counters at all. Two callbacks means two guards -
 * the rule of the series is one check per stage, never one at the top (T3.17.md,
 * "alive.expired() en premiere instruction de chaque etage").
 ******************************************************************************/

TEST_F(JsonApiEventLogTest, UuidPathAnswersTheSingleEventWithoutCounters)
{
    seedEventLog();
    ASSERT_FALSE(seededUuids().empty());
    const std::string uuid = seededUuids()[2];

    WsTestSession ws;
    const Json data = wsEventLog(ws, Json{{ "uuid", uuid }});

    ASSERT_TRUE(data.is_object()) << "the uuid path did not answer";
    //HistEvent::toJson() of that one row, at the ROOT - not wrapped in a list.
    EXPECT_EQ(7u, data.size());
    EXPECT_EQ(uuid, str(data, "id"));
    EXPECT_EQ("t317f_logged_io_2", str(data, "io_id"));
    EXPECT_TRUE(member(data, "event_raw").is_object());

    //None of the pagination keys exist on this path.
    for (const char *k: { "total_count", "total_page", "page", "per_page",
                          "events" })
        EXPECT_TRUE(member(data, k).is_null()) << "unexpected key " << k;
}

TEST_F(JsonApiEventLogTest, UuidPathWithAnUnknownUuidAnswersAnError)
{
    //The uuid branch short circuits the paging entirely, so it is the one
    //eventlog request that is safe without per_page (see the SIGFPE note above).
    WsTestSession ws;
    const Json data = wsEventLog(ws, Json{{ "uuid", "t317f-no-such-uuid" }});

    EXPECT_JSON_GOLDEN("t317f_ws_eventlog_uuid_not_found", data);
    EXPECT_EQ("uuid not found", str(data, "error"));
}

TEST_F(JsonApiEventLogTest, AnEmptyUuidFallsBackToThePaginatedPath)
{
    //jParam["uuid"] != "" is the whole discriminator, so an EMPTY uuid is not
    //the uuid path at all - it is a paginated request, and it needs a per_page.
    seedEventLog();

    WsTestSession ws;
    const Json data = wsEventLog(ws, Json{{ "uuid", "" }, { "page", "0" },
                                          { "per_page", "2" }});

    EXPECT_JSON_EQ(Json(seededEventCount()), member(data, "total_count"));
    EXPECT_TRUE(member(data, "events").is_array());
}

/*******************************************************************************
 * BOTH TRANSPORTS
 *
 * The guard goes in JsonApi.cpp and nowhere else, and covers HTTP and WS by the
 * same transitive mechanism T3.17e validated by measurement: both handlers
 * derive from JsonApi (single non virtual base, one owner `JsonApi *jsonApi`,
 * one destruction at HttpClient.cpp:162). These two cases are what makes that
 * claim observable rather than asserted: the HTTP transport reaches the very
 * same two callbacks, and both of its paths are exercised.
 ******************************************************************************/

TEST_F(JsonApiEventLogTest, HttpPaginatedAnswerMatchesTheWebsocketOne)
{
    seedEventLog();

    WsTestSession ws;
    const Json wsData = wsEventLog(ws, Json{{ "page", "0" }, { "per_page", "2" }});

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "eventlog" },
                                { "page", "0" }, { "per_page", "2" }}));
    ASSERT_TRUE(waitUntil([&req]() { return req.count() > 0; }));

    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());

    const Json body = req.bodyJson();
    EXPECT_JSON_EQ(member(wsData, "total_count"), member(body, "total_count"));
    EXPECT_JSON_EQ(member(wsData, "total_page"), member(body, "total_page"));
    EXPECT_JSON_EQ(member(wsData, "page"), member(body, "page"));
    EXPECT_JSON_EQ(member(wsData, "per_page"), member(body, "per_page"));
    EXPECT_EQ(2u, member(body, "events").size());
}

TEST_F(JsonApiEventLogTest, HttpUuidPathAnswersTheSingleEvent)
{
    seedEventLog();
    const std::string uuid = seededUuids()[0];

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "eventlog" }, { "uuid", uuid }}));
    ASSERT_TRUE(waitUntil([&req]() { return req.count() > 0; }));

    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());

    const Json body = req.bodyJson();
    EXPECT_EQ(uuid, str(body, "id"));
    EXPECT_EQ("t317f_logged_io_0", str(body, "io_id"));
    EXPECT_EQ(7u, body.size());
}

TEST_F(JsonApiEventLogTest, HttpUnknownUuidAnswersTheSameErrorAsWebsocket)
{
    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "eventlog" },
                                { "uuid", "t317f-no-such-uuid" }}));
    ASSERT_TRUE(waitUntil([&req]() { return req.count() > 0; }));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_JSON_GOLDEN("t317f_http_eventlog_uuid_not_found", req.body());
}

/*******************************************************************************
 * THE "SECOND UAF" OF THE SERIES, MEASURED ABSENT
 *
 * Protocol of T3.17b/c/d: destroy the IO side between the request and the
 * answer, fire the callback, and see whether anything is dereferenced. Here the
 * whole reference house goes away - eight IOs, three rooms - and the answer must
 * still arrive COMPLETE. It does, because the payload comes from sqlite rows and
 * buildJsonEventLog() holds no IO pointer to begin with.
 *
 * Applying T3.17a's re-resolution-by-id here would be code with no referent at
 * all, and turning this into an error would break the T3.17 invariant that
 * response shapes do not change. Nothing is applied.
 ******************************************************************************/

TEST_F(JsonApiEventLogTest, AnswerIsCompleteAfterEveryIoIsDeleted)
{
    seedEventLog();
    loadReferenceHouse();

    WsTestSession ws;
    ws.send(Json{{ "msg", "eventlog" }, { "msg_id", "t317f" },
                 { "data", Json{{ "page", "0" }, { "per_page", "2" }} }});
    ASSERT_FALSE(hasEventLogAnswer(ws)) << "the answer was not deferred";

    //Delete every IO of the house the way the JSON API does, while the query is
    //in flight. This also floods the session with EventIODeleted, which is why
    //the answer is located by its "msg" member and not by position.
    for (const char *id: { HOUSE_BOOL_IN, HOUSE_BOOL_OUT, HOUSE_INT,
                           HOUSE_STRING, HOUSE_ACCENTED, HOUSE_CAMERA_PTZ,
                           HOUSE_CAMERA_PLAIN, HOUSE_PLAYER })
    {
        IOBase *target = io(id);
        if (target)
        {
            EXPECT_TRUE(deleteIO(target)) << "could not delete " << id;
        }
    }
    ASSERT_EQ(nullptr, io(HOUSE_PLAYER));

    ASSERT_TRUE(waitUntil([&ws]() { return hasEventLogAnswer(ws); }))
            << "the answer never arrived once the IOs were gone";

    const Json data = wsEventLogAnswer(ws);
    ASSERT_TRUE(data.is_object());
    EXPECT_JSON_EQ(Json(seededEventCount()), member(data, "total_count"));
    EXPECT_EQ(2u, member(data, "events").size())
            << "the answer was mutilated by the deletion of the IOs";
}

/*******************************************************************************
 * LIFETIME - WHAT T3.17f ACTUALLY FIXES. ADDED WITH THE GUARD, NOT BEFORE.
 *
 * The round trip is real: the request goes into HistLogger's queue, a sqlite
 * worker THREAD runs the query, and the answer comes back through a
 * uvw::AsyncHandle owned by the loop. In between, the client can disconnect -
 * and ~HttpClient() (HttpClient.cpp:162) deletes the handler, JsonApi base
 * sub-object included. The late answer then calls `callback`, which is the
 * handler's own lambda holding the handler's `this`, so sendJson() dereferences
 * a freed object.
 *
 * These cases live in the SECOND commit of T3.17f, unlike everything above,
 * because on an unguarded tree the two transport ones do not fail, they take the
 * process down: SIGSEGV, exit 139 - the same thing a reviewer hit while
 * mutating E4.0e, and the same split T3.17b, T3.17c and T3.17d had to make. A
 * characterization commit has to be green.
 *
 * The cases come in two shapes, exactly as in T3.17c:
 *   - four on a BARE JsonApi (ApiGoneBefore*), whose callback sets a flag. They
 *     observe the guard directly - the answer must NOT fire - and they FAIL
 *     rather than crash on an unguarded tree, which makes them a regression net
 *     rather than an ASan-only net.
 *   - four through a real WS session / HTTP request, which is where the
 *     use-after-free actually lives. Three assert nothing beyond "it ran", ASan
 *     being the oracle there; the fourth is the per-object control.
 *
 * THE FLAG IS A shared_ptr, NEVER A STACK LOCAL CAPTURED BY REFERENCE, and that
 * is the same rule seedEventLog() states above for the same reason. The guard is
 * precisely what these cases exist to test, so it cannot be assumed to hold: if
 * it ever regressed AND the delivery slipped past the sentinel, a captured `&`
 * would write through a dangling pointer into a dead stack frame - undefined
 * behaviour instead of the clean red this net is supposed to produce. The
 * callback keeps the flag alive by owning a share of it.
 *
 * NO -Wdeprecated WARNING POINTS AT EITHER CALLBACK, and that is the whole
 * lesson of T3.17c restated: neither body odr-uses `this`, so `[=]` captures no
 * `this` and the compiler is silent. The freed object travels inside the
 * captured std::function. "No implicit this capture" is not "no use-after-free".
 ******************************************************************************/

TEST_F(JsonApiEventLogTest, ApiGoneBeforePaginatedAnswer)
{
    seedEventLog();

    auto answered = std::make_shared<bool>(false);
    {
        JsonApi api;
        api.buildJsonEventLog(bareRequest(Json{{ "page", "0" },
                                               { "per_page", "2" }}),
                              [answered](Json &) { *answered = true; });
        //api dies here, exactly as when the client disconnects
    }

    drainPendingAnswers();
    EXPECT_FALSE(*answered)
            << "the pagination answer was handed to a destroyed JsonApi";
}

TEST_F(JsonApiEventLogTest, ApiGoneBeforeUuidAnswer)
{
    //The second callback, on the other branch. One guard per stage: a single
    //check at the top of buildJsonEventLog() would pass this case and still
    //leave both callbacks nude, since the object is alive when they are armed.
    seedEventLog();
    ASSERT_FALSE(seededUuids().empty());

    auto answered = std::make_shared<bool>(false);
    {
        JsonApi api;
        api.buildJsonEventLog(bareRequest(Json{{ "uuid", seededUuids()[1] }}),
                              [answered](Json &) { *answered = true; });
    }

    drainPendingAnswers();
    EXPECT_FALSE(*answered)
            << "the uuid answer was handed to a destroyed JsonApi";
}

TEST_F(JsonApiEventLogTest, ApiGoneBeforeUuidErrorAnswer)
{
    //The error branch of the uuid callback runs BEFORE the nominal one and
    //returns on its own, so a guard placed after the `if (!success)` block
    //would leave the error path unprotected. An unknown uuid takes that path.
    auto answered = std::make_shared<bool>(false);
    {
        JsonApi api;
        api.buildJsonEventLog(bareRequest(Json{{ "uuid", "t317f-gone-uuid" }}),
                              [answered](Json &) { *answered = true; });
    }

    drainPendingAnswers();
    EXPECT_FALSE(*answered)
            << "the \"uuid not found\" error was handed to a destroyed JsonApi";
}

TEST_F(JsonApiEventLogTest, ApiGoneBeforePageOutOfRangeAnswer)
{
    //Same argument on the pagination callback: its error branch also returns on
    //its own, so it needs the guard to be the FIRST statement of the body.
    seedEventLog();

    auto answered = std::make_shared<bool>(false);
    {
        JsonApi api;
        api.buildJsonEventLog(bareRequest(Json{{ "page", "9" },
                                               { "per_page", "2" }}),
                              [answered](Json &) { *answered = true; });
    }

    drainPendingAnswers();
    EXPECT_FALSE(*answered)
            << "the \"page is out of range\" error was handed to a destroyed "
               "JsonApi";
}

/* The real thing on the websocket transport: a client that leaves while its
 * answer is in flight. Without the guard this is the heap-use-after-free quoted
 * in the commit message - and, without ASan, a plain SIGSEGV.
 */
TEST_F(JsonApiEventLogTest, WsClientGoneBeforePaginatedAnswerIsIgnored)
{
    seedEventLog();

    {
        WsTestSession ws;
        ws.send(Json{{ "msg", "eventlog" }, { "msg_id", "t317f" },
                     { "data", Json{{ "page", "0" }, { "per_page", "2" }} }});
        ASSERT_FALSE(hasEventLogAnswer(ws))
                << "the answer was synchronous, nothing is in flight";
        //ws dies here, exactly as when the client disconnects
    }

    //The database answer arrives afterwards and must touch nothing.
    drainPendingAnswers();
}

TEST_F(JsonApiEventLogTest, WsClientGoneBeforeUuidAnswerIsIgnored)
{
    seedEventLog();

    {
        WsTestSession ws;
        ws.send(Json{{ "msg", "eventlog" }, { "msg_id", "t317f" },
                     { "data", Json{{ "uuid", seededUuids()[3] }} }});
        ASSERT_FALSE(hasEventLogAnswer(ws));
    }

    drainPendingAnswers();
}

/* The HTTP twin, JsonApiHandlerHttp.cpp:877. Not edited by this ticket, and
 * that is the point: it is covered by the very same guard, because both
 * handlers are one object with one destruction. This case is what makes the
 * claim measurable instead of asserted.
 *
 * ~HttpTestRequest() destroys the handler and THEN pumps the loop twice
 * (harness trap 1), so on an unguarded tree the use-after-free happens inside
 * the destructor itself.
 */
TEST_F(JsonApiEventLogTest, HttpClientGoneBeforeAnswerIsIgnored)
{
    seedEventLog();

    {
        HttpTestRequest req;
        req.send(authenticated(Json{{ "action", "eventlog" },
                                    { "page", "0" }, { "per_page", "2" }}));
        ASSERT_EQ(0u, req.count())
                << "the answer was synchronous, nothing is in flight";
    }

    drainPendingAnswers();
}

/* The guard is per object, not global: one client leaving must not swallow
 * another client's answer. Without this control, a guard on a shared or static
 * token would pass every case above and silence the whole server.
 */
TEST_F(JsonApiEventLogTest, ASecondClientLeavingDoesNotSwallowTheFirstAnswer)
{
    seedEventLog();

    WsTestSession survivor;
    survivor.send(Json{{ "msg", "eventlog" }, { "msg_id", "t317f" },
                       { "data", Json{{ "page", "0" }, { "per_page", "2" }} }});

    {
        WsTestSession leaving;
        leaving.send(Json{{ "msg", "eventlog" }, { "msg_id", "t317f" },
                          { "data", Json{{ "page", "0" }, { "per_page", "2" }} }});
    }

    ASSERT_TRUE(waitUntil([&survivor]() { return hasEventLogAnswer(survivor); }))
            << "the surviving client never got its answer";

    const Json data = wsEventLogAnswer(survivor);
    ASSERT_TRUE(data.is_object());
    EXPECT_JSON_EQ(Json(seededEventCount()), member(data, "total_count"));
    EXPECT_EQ(2u, member(data, "events").size());
}
