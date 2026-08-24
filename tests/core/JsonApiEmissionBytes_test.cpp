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
 * E4.1b - THE BYTES THE API ACTUALLY PUTS ON THE WIRE.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS FILE EXISTS AT ALL
 * ---------------------------------------------------------------------------
 * The 145 goldens of the E4.0 series compare PARSED DOCUMENTS, never bytes -
 * a contract chosen deliberately by E4.0a, because a byte exact oracle would
 * have failed wholesale at the jansson -> nlohmann cut for a reason already
 * accepted (key order). The corollary was measured by the E4.1k review and is
 * brutal: NO golden, and NO case of the whole suite, rougit on a change of
 * ESCAPING. Flipping ensure_ascii from true to false left `make check` GREEN.
 *
 * So the entire escaping dimension of the E4.1 migration was, until this file,
 * covered by nothing. Every assertion below is therefore on the RAW BYTES of
 * the response - `HttpTestRequest::body()` and `WsTestSession::lastMessage()`,
 * never `bodyJson()` / `lastEnvelope()`. A semantic oracle is BY CONSTRUCTION
 * blind to what this file measures; that blindness is the hole.
 *
 * ---------------------------------------------------------------------------
 * THE PATH THIS FILE DRIVES, AND WHY IT IS THE RIGHT ONE
 * ---------------------------------------------------------------------------
 * `eventlog` is TODAY the only API action whose payload is built with nlohmann
 * on both transports: JsonApi::buildJsonEventLog() is the single method of
 * JsonApi.h taking a std::function<void(Json &)>, and it hands its document to
 *
 *      JsonApiHandlerHttp::processEventLog()  -> sendJson(const Json &)
 *      JsonApiHandlerWS::processEventLog()    -> sendJson(msg, const Json &, id)
 *
 * i.e. to the two nlohmann emitters E4.1b hardens. Everything else still goes
 * through the jansson emitters, which is why the same request family lets one
 * case put the two escapings SIDE BY SIDE on one server and one house.
 *
 * The payload of that document comes from HistEvent::toJson()
 * (HistLogger.cpp:82-103), which copies `io_id`, `io_state` and `pic_uid`
 * STRAIGHT out of sqlite into the tree, with no UTF-8 validation anywhere on
 * the way in. That is the injection surface, and it is not hypothetical:
 * EventManager::appendEvent() (EventManager.cpp:93) writes
 * `e.io_state = ev.getParam()["state"]`, and for a string IO that state is
 * whatever `set_state` was handed - including, over HTTP, the GET parameter
 * fallback of JsonApiHandlerHttp.cpp:88 (`jsonParam = paramsGET`), which is
 * percent-decoded bytes and NOT a JSON string. jansson refuses invalid UTF-8
 * at json_loads(), nlohmann refuses it at Json::parse(); NOTHING refuses it on
 * that path, because that path never goes through a JSON parser.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS PINNED HERE BEFORE ANY LINE OF src/ MOVES
 * ---------------------------------------------------------------------------
 * This is the characterization half. It is green on an untouched tree and it
 * says three things:
 *
 *   1. nlohmann ACCEPTS invalid UTF-8 into the tree (jansson would have
 *      dropped the pair at construction) and only complains from dump(), by
 *      throwing type_error.316. The exception code is pinned exactly.
 *   2. dump(-1, ' ', true, error_handler_t::replace) on that SAME tree answers
 *      instead of throwing, and writes U+FFFD. This is the cause being
 *      treated, not the symptom; it is what E4.1b installs on the emitters.
 *   3. The API serves TWO DIFFERENT ESCAPINGS TODAY, on the same connection
 *      family: the jansson answers are pure ASCII (JSON_ENSURE_ASCII,
 *      UPPERCASE hex) and the nlohmann answers are raw UTF-8 bytes.
 *
 * (3) is a DECLARED DELTA, not an invariant: the fix commit of E4.1b flips it
 * to the third form (pure ASCII, LOWERCASE hex) on both transports, and THAT
 * FLIP IS THE DELIVERABLE. The case is named Delta_ for that reason. Do not
 * "repair" it back.
 *
 * ---------------------------------------------------------------------------
 * TWO COMMITS, SAME SPLIT AS T3.17f AND FOR THE SAME REASON
 * ---------------------------------------------------------------------------
 * Everything in this commit is green on an untouched tree. The cases that feed
 * INVALID bytes end to end cannot be: on an unhardened tree they do not fail,
 * they take the binary down (std::terminate out of dump()). They ship with the
 * hardening, in the fix commit, exactly as JsonApiEventLog_test.cpp did with
 * its lifetime cases.
 *
 * Ids and the history database directory are prefixed e41b_ - Config's IO state
 * cache is process wide and never cleared, and the sqlite database must never
 * be shared with another binary. Taken so far: e40_, e40b_, e40c_, e40d_,
 * e40e_, e40f_, t317a_ .. t317f_, t319_.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "EventManager.h"
#include "HistLogger.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/*******************************************************************************
 * The three byte shapes this file talks about, spelled once, in escapes, so
 * that no editor and no locale can silently rewrite them.
 *
 *   RAW_E_ACUTE   the two UTF-8 bytes of U+00E9 (e acute)
 *   ASCII_E_UPPER what jansson writes for it (JSON_ENSURE_ASCII, UPPERCASE)
 *   ASCII_E_LOWER what nlohmann writes for it with ensure_ascii = true
 *
 * The two ASCII forms differ ONLY by the case of the hexadecimal, which is the
 * whole delta E4.1 accepted (E4.1.md, invariant 3). A comparison that lowercases
 * before matching cannot see it - that is precisely the defect E4.1a found in
 * the ParamsJson tripwire, so nothing here is case folded.
 ******************************************************************************/
const char *const RAW_E_ACUTE     = "\xc3\xa9";
const char *const ASCII_E_UPPER   = "\\u00E9";
const char *const ASCII_E_LOWER   = "\\u00e9";

//A byte sequence that CANNOT become valid UTF-8 by accident: 0xFF is not a
//legal lead byte in any position, and 0x80 is a continuation byte with nothing
//to continue. Whatever the tree does with it, it is never "some other text".
const char *const INVALID_UTF8    = "\xff\x80";

bool contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

//Counts occurrences, so a case can say "exactly twice" instead of "somewhere".
size_t occurrences(const std::string &haystack, const std::string &needle)
{
    size_t n = 0;
    for (size_t p = haystack.find(needle); p != std::string::npos;
         p = haystack.find(needle, p + needle.size()))
        n++;
    return n;
}

} //namespace

class JsonApiEmissionBytesTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        //MUST run before CoreFixture::SetUp() installs the per-case directories:
        //HistLogger is a Meyers singleton whose constructor captures
        //Utils::getCacheFile("events.db") and opens it from a worker thread. A
        //singleton born inside a case would point at a directory TearDown()
        //rm -rf's milliseconds later, and `sqlite::database db(dbname)`
        //(HistLogger.cpp:190) sits OUTSIDE the try of :192 - cantopen is then an
        //uncaught exception in a std::thread, i.e. the loss of the binary.
        //Solution reused verbatim from E4.0e / T3.17f, only the directory
        //template and the id prefix differ so no two binaries share a database.
        ensureHistLogger();

        JsonApiCharacterizationTest::SetUp();

        seedEventLog();

        //NO DEFENSIVE PUMP HERE. E4.0g made TearDown() drain after the parent
        //teardown, so a case starts on an empty queue (contract documented on
        //JsonApiCharacterizationTest::TearDown()). Measured, not assumed: this
        //binary is green under --gtest_shuffle. If a drain ever becomes
        //necessary, a SECOND source of surviving events exists - name it in
        //FINDINGS.md instead of pumping it away.
    }

    static void ensureHistLogger()
    {
        static bool done = false;
        if (done)
            return;
        done = true;

        static char tmpl[] = "/tmp/calaos_e41b_hist_XXXXXX";
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
     * HistLogger queues the query for its sqlite worker thread and wakes the
     * main loop back up through a uvw AsyncHandle. Nothing arrives without
     * pumping, and an assertion of ABSENCE placed before the pump would be an
     * oracle mort (E4.0g: "non livre n'est pas non leve").
     *
     * The 2s bound stays well under the 30s read timeout HttpClient installs in
     * its constructor, so an HttpTestRequest can be held alive across one wait.
     **************************************************************************/
    static bool waitUntil(const std::function<bool()> &done)
    {
        for (int i = 0; i < 400; i++)
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
     * ANTI "FIXTURE PAUVRE" (seven relapses in the E4.0 series, every single one
     * found by a reviewer and never by an implementer): the three string fields
     * HistEvent::toJson() copies verbatim out of sqlite - io_id, io_state,
     * pic_uid - are interchangeable as far as the emitter is concerned. Each one
     * therefore gets a DIFFERENT value here, and the cases assert each one by
     * name, so swapping any two in production is a failure and not a shrug. The
     * accented probe is carried by io_state ALONE: if it were in all three, a
     * case could not tell an emitter that escapes everything from one that
     * escapes only what it happens to touch first.
     *
     * event_raw carries its own distinct marker for the same reason, and stays
     * VALID JSON on purpose - HistEvent::toJson() PARSES it back
     * (HistLogger.cpp:93-100), so invalid bytes placed there would be swallowed
     * by that Json::parse() and never reach the emitter. The injection surface
     * is the three plain strings, not the nested document.
     *
     * Seeded once per process: the database is opened once and never emptied, so
     * a per-case seed would make the counters depend on the shuffle order.
     **************************************************************************/
    static const char *escapingIoId()    { return "e41b_escaping_io"; }
    static const char *escapingPicUid()  { return "e41b_escaping_pic"; }
    //"caf<e acute>-e41b": the accent is the probe, the suffix makes the value
    //unmistakable in a diff and impossible to confuse with anything the house
    //or another binary could produce.
    static std::string escapingIoState() { return std::string("caf") + RAW_E_ACUTE + "-e41b"; }
    static const char *escapingRawMark() { return "e41b_escaping_raw"; }

    static std::string &escapingUuid()
    {
        static std::string uuid;
        return uuid;
    }

    static void seedEventLog()
    {
        static bool done = false;
        if (done)
            return;
        done = true;

        HistEvent e = HistEvent::create();
        escapingUuid() = e.uuid;
        e.event_type = CalaosEvent::EventIOChanged;
        e.io_id = escapingIoId();
        e.io_state = escapingIoState();
        e.pic_uid = escapingPicUid();
        e.event_raw = std::string("{\"probe\":\"") + escapingRawMark() + "\"}";
        HistLogger::Instance().appendEvent(e);

        //Wait for the worker to have committed it, otherwise the first case to
        //read sees an empty table. The result is held in a shared_ptr and never
        //captured by reference: a timed out query's callback would still be
        //alive inside the uvw AsyncHandle and would fire during a later pump.
        int seen = 0;
        for (int attempt = 0; attempt < 20 && seen < 1; attempt++)
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

        ASSERT_GE(seen, 1) << "the seeded event never reached the database";
    }

    //The eventlog request, on either transport. per_page is explicit and non
    //zero: an absent or non numeric one used to divide by zero in the sqlite
    //worker (E4.0e, fixed by T3.19), and this file is not the place to
    //re-characterize that refusal.
    static Json eventLogRequestBody()
    {
        return authenticated({{ "action", "eventlog" },
                              { "page", "0" },
                              { "per_page", "50" }});
    }

    static Json wsEventLogRequest()
    {
        return Json{{ "msg", "eventlog" },
                    { "msg_id", "e41b_msgid" },
                    { "data", {{ "page", "0" }, { "per_page", "50" }}}};
    }
};

/*******************************************************************************
 * 1. THE CAUSE, AT THE LIBRARY SEAM
 *
 * HistEvent::toJson() is the exact constructor the two nlohmann emitters are
 * fed from. It takes std::string members and puts them in a Json without ever
 * looking at them, so the invalid bytes are IN THE TREE - this is the half of
 * the E4.0 UTF-8 trap that is counter-intuitive, and both columns of
 * E4.0.md:355 were wrong before E4.0e corrected them. jansson does the opposite
 * (refuses at construction, drops the pair silently, then dumps a truncated
 * document with a 200); nlohmann accepts and detonates from dump().
 *
 * GREEN BEFORE AND AFTER E4.1b's fix, on purpose: E4.1b does not change what
 * dump() does by default, it changes which dump() the emitters call. This case
 * is the reference point that makes the next one meaningful.
 ******************************************************************************/
TEST_F(JsonApiEmissionBytesTest, NlohmannAcceptsInvalidUtf8IntoTheTreeAndOnlyBareDumpThrows316)
{
    HistEvent e = HistEvent::create();
    e.event_type = CalaosEvent::EventIOChanged;
    e.io_id = "e41b_cause_io";
    e.io_state = std::string(INVALID_UTF8) + "x";
    e.pic_uid = "e41b_cause_pic";
    e.event_raw = "{\"probe\":\"e41b_cause_raw\"}";

    const Json j = e.toJson();

    //IN THE TREE, byte for byte. Nothing validated it on the way in.
    ASSERT_TRUE(j.contains("io_state"));
    EXPECT_EQ(std::string(INVALID_UTF8) + "x", j["io_state"].get<std::string>());

    //The three interchangeable strings landed on the three keys they belong to.
    EXPECT_EQ("e41b_cause_io", j["io_id"].get<std::string>());
    EXPECT_EQ("e41b_cause_pic", j["pic_uid"].get<std::string>());

    //A BARE dump() - what JsonApiHandlerHttp::sendJson(const Json &) and
    //JsonApiHandlerWS::sendJson(msg, const Json &, id) call today - throws.
    //Not "returns an error", not "drops the key": THROWS, out of an emitter
    //that has no try/catch above it, on a live connection.
    bool threw = false;
    int code = 0;
    try
    {
        (void) j.dump();
    }
    catch (const Json::type_error &te)
    {
        threw = true;
        code = te.id;
    }

    EXPECT_TRUE(threw) << "a bare dump() of a tree holding invalid UTF-8 must throw";
    EXPECT_EQ(316, code) << "the exception E4.0 pinned is type_error.316, nothing else";
}

/*******************************************************************************
 * 2. THE CURE, AT THE SAME SEAM
 *
 * error_handler_t::replace on the SAME tree. This is invariant 2 of the epic
 * (user decision, 2026-08-17): the handler treats the CAUSE, where a try/catch
 * would only catch the symptom and still owe an answer.
 *
 * Note the byte shape of the replacement WITH ensure_ascii = true: nlohmann
 * writes the six ASCII characters backslash-u-f-f-f-d (json.hpp:18538-18545),
 * never the
 * three raw bytes EF BF BD. Both are U+FFFD once parsed; only one keeps the
 * wire ASCII-only, and that is the one invariant 3 asks for.
 *
 * GREEN BEFORE AND AFTER, like the case above.
 ******************************************************************************/
TEST_F(JsonApiEmissionBytesTest, TheReplaceHandlerAnswersWhereTheBareDumpThrows)
{
    HistEvent e = HistEvent::create();
    e.event_type = CalaosEvent::EventIOChanged;
    e.io_id = "e41b_cure_io";
    e.io_state = std::string(INVALID_UTF8) + "x";
    e.pic_uid = "e41b_cure_pic";
    e.event_raw = "{\"probe\":\"e41b_cure_raw\"}";

    const Json j = e.toJson();

    std::string wire;
    bool threw = false;
    try
    {
        wire = j.dump(-1, ' ', true, Json::error_handler_t::replace);
    }
    catch (const std::exception &)
    {
        threw = true;
    }

    ASSERT_FALSE(threw) << "the replace handler must answer, never throw";

    //Two invalid bytes, two replacement characters: nlohmann re-reads the byte
    //after resetting the decoder, so 0xFF and 0x80 are rejected independently.
    EXPECT_EQ(2u, occurrences(wire, "\\ufffd"))
            << "each invalid byte must produce its own U+FFFD: " << wire;

    //The surviving text is still there - a handler that ate the whole value
    //would pass a "contains U+FFFD" check and be useless.
    EXPECT_TRUE(contains(wire, "x")) << wire;

    //And the invalid bytes are GONE from the wire.
    EXPECT_FALSE(contains(wire, "\xff")) << "0xFF survived onto the wire";
    EXPECT_FALSE(contains(wire, "\x80")) << "0x80 survived onto the wire";

    //ensure_ascii = true means ASCII-ONLY. Not "mostly ASCII".
    for (unsigned char c : wire)
        ASSERT_LT(c, 0x80u) << "non ASCII byte on an ensure_ascii wire: " << wire;
}

/*******************************************************************************
 * 3. DELTA - THE API SERVES TWO DIFFERENT ESCAPINGS TODAY
 *
 * ONE house, ONE server, TWO answers:
 *   - get_home goes through JsonApiHandlerHttp::sendJson(json_t *), which dumps
 *     with JSON_ENSURE_ASCII -> the accented IO name of the reference house
 *     comes out as \\u00C9 / \\u00E9, UPPERCASE hex, pure ASCII.
 *   - eventlog goes through JsonApiHandlerHttp::sendJson(const Json &), a bare
 *     nlohmann dump() -> the accented io_state comes out as RAW UTF-8 BYTES.
 *
 * That asymmetry is what E4.1b removes; this case is the BEFORE picture and it
 * is a DECLARED DELTA, not an invariant. The fix commit flips the nlohmann half
 * to \\u00e9 (lowercase hex, still pure ASCII) on both transports. DO NOT
 * "repair" this case back to raw bytes - the flip is the deliverable.
 *
 * Every assertion is on the RAW response bytes. On the parsed document the two
 * forms are indistinguishable, which is exactly why the escaping dimension had
 * no coverage at all before this file.
 ******************************************************************************/
TEST_F(JsonApiEmissionBytesTest, Delta_TodayTheNlohmannWireIsRawUtf8WhileTheJanssonWireIsAscii)
{
    //--- jansson side: get_home, HTTP -------------------------------------
    {
        //The house is NOT loaded by the fixture's SetUp(): every case of this
        //harness asks for it explicitly. HOUSE_ACCENTED carries the accented
        //name this half of the case reads.
        loadReferenceHouse();

        HttpTestRequest req;
        req.send(authenticated({{ "action", "get_home" }}));

        ASSERT_EQ(1u, req.count()) << "get_home did not answer";
        const std::string wire = req.body();

        //"<E acute>clairage caf<e acute>" is the name of HOUSE_ACCENTED.
        EXPECT_TRUE(contains(wire, ASCII_E_UPPER))
                << "jansson must escape U+00E9 with UPPERCASE hex";
        EXPECT_FALSE(contains(wire, RAW_E_ACUTE))
                << "jansson answers are ASCII-only (JSON_ENSURE_ASCII)";

        for (unsigned char c : wire)
            ASSERT_LT(c, 0x80u) << "the jansson wire must be ASCII-only";
    }

    //--- nlohmann side: eventlog, HTTP ------------------------------------
    {
        HttpTestRequest req;
        req.send(eventLogRequestBody());

        ASSERT_TRUE(waitUntil([&req]() { return req.count() > 0; }))
                << "eventlog never answered over HTTP";
        ASSERT_EQ(1u, req.count());
        const std::string wire = req.body();

        //The seeded event really is in there, and each of the three
        //interchangeable strings is on its own key.
        ASSERT_TRUE(contains(wire, escapingUuid())) << wire;
        ASSERT_TRUE(contains(wire, escapingIoId())) << wire;
        ASSERT_TRUE(contains(wire, escapingPicUid())) << wire;
        ASSERT_TRUE(contains(wire, escapingRawMark())) << wire;

        EXPECT_TRUE(contains(wire, RAW_E_ACUTE))
                << "TODAY the nlohmann emitter writes raw UTF-8 bytes";
        EXPECT_FALSE(contains(wire, ASCII_E_LOWER)) << wire;
        EXPECT_FALSE(contains(wire, ASCII_E_UPPER)) << wire;
    }

    //--- nlohmann side: eventlog, WEBSOCKET -------------------------------
    {
        WsTestSession ws;
        ws.send(wsEventLogRequest());

        ASSERT_TRUE(waitUntil([&ws]() { return ws.count() > 0; }))
                << "eventlog never answered over the websocket";
        ASSERT_EQ(1u, ws.count());
        const std::string wire = ws.lastMessage();

        ASSERT_TRUE(contains(wire, escapingUuid())) << wire;
        ASSERT_TRUE(contains(wire, escapingIoId())) << wire;
        ASSERT_TRUE(contains(wire, escapingPicUid())) << wire;
        ASSERT_TRUE(contains(wire, escapingRawMark())) << wire;

        EXPECT_TRUE(contains(wire, RAW_E_ACUTE))
                << "TODAY the nlohmann WS envelope writes raw UTF-8 bytes";
        EXPECT_FALSE(contains(wire, ASCII_E_LOWER)) << wire;
        EXPECT_FALSE(contains(wire, ASCII_E_UPPER)) << wire;
    }
}
