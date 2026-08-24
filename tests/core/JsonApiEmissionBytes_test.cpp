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
 * (3) was a DECLARED DELTA, not an invariant, and the fix commit of E4.1b
 * FLIPPED IT: the nlohmann emitters now write the third form (pure ASCII,
 * LOWERCASE hex) on both transports. That flip is the deliverable of the
 * ticket, and the case that carried the before picture
 * (Delta_TodayTheNlohmannWireIsRawUtf8...) is now split in two, one per
 * transport: TheNlohmann{Http,Websocket}WireIsAsciiOnlyAndEscapesWith
 * LowercaseHex. Do NOT "repair" them back to raw bytes.
 *
 * ---------------------------------------------------------------------------
 * TWO COMMITS, SAME SPLIT AS T3.17f AND FOR THE SAME REASON
 * ---------------------------------------------------------------------------
 * The three cases above ship in the characterization commit: they are green on
 * an untouched tree. The two cases that feed INVALID bytes END TO END cannot
 * be, because on an unhardened tree they do not fail - they take the binary
 * down (std::terminate out of dump()). They ship WITH the hardening, exactly as
 * JsonApiEventLog_test.cpp did with its lifetime cases.
 *
 * ---------------------------------------------------------------------------
 * THE TWO ORACLES, AND WHAT EACH ONE IS SENSITIVE TO - READ BEFORE EDITING
 * ---------------------------------------------------------------------------
 * They are deliberately DISJOINT, so that a counter-mutation names the guilty
 * parameter instead of reddening everything:
 *
 *   ORACLE A - InvalidUtf8...IsServedAsReplacementChar{OverHttp,OverWebsocket}
 *     sensitive to error_handler_t::replace ONLY. It never asserts that the
 *     wire is ASCII: U+FFFD round trips to the same string whether it was
 *     written backslash-u-f-f-f-d or as the three bytes EF BF BD, so flipping
 *     ensure_ascii leaves it green. Exchange replace -> ignore and the bytes
 *     are DROPPED instead of replaced: red.
 *
 *   ORACLE B - TheNlohmann{Http,Websocket}WireIsAsciiOnly...LowercaseHex
 *     sensitive to ensure_ascii ONLY. Its probe is a VALID character (U+00E9),
 *     so no error handler ever looks at it. Exchange true -> false and the
 *     accent goes out as raw bytes: red.
 *
 * If you make A assert "the wire is ASCII", you destroy that separation and
 * every counter-mutation of this ticket starts reddening both.
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
     * TWO events are seeded, and they are NOT interchangeable either: one
     * carries a VALID accented byte pair (oracle B, escaping) and the other a
     * sequence that can never become valid UTF-8 (oracle A, error handler).
     * Both live in the same table, so every eventlog answer of this file
     * contains both - which is also how the two oracles are shown to be
     * independent: the same response makes exactly one of them red under each
     * counter-mutation.
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

    static const char *poisonIoId()      { return "e41b_poison_io"; }
    static const char *poisonPicUid()    { return "e41b_poison_pic"; }
    //INVALID_UTF8 then a plain 'x': the trailing character is what tells a
    //handler that REPLACED the bad bytes from one that ATE the whole value.
    static std::string poisonIoState()   { return std::string(INVALID_UTF8) + "x"; }
    static const char *poisonRawMark()   { return "e41b_poison_raw"; }
    //What error_handler_t::replace answers for those two bytes, as the UTF-8
    //of U+FFFD twice. Identical once parsed whether the wire escaped it or not,
    //which is exactly why oracle A is blind to ensure_ascii.
    static std::string replacedIoState() { return std::string("\xef\xbf\xbd\xef\xbf\xbd") + "x"; }

    static std::string &escapingUuid()
    {
        static std::string uuid;
        return uuid;
    }

    static std::string &poisonUuid()
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

        //THIS ROW IS WHAT USED TO KILL THE PROCESS. Before the hardening, any
        //eventlog answer carrying it made dump() throw type_error.316 out of an
        //emitter with no try/catch above it - measured on this very binary:
        //"terminate called after throwing an instance of
        //'nlohmann::json_abi_v3_11_3::detail::type_error'", SIGABRT, the whole
        //run lost. Which is why it ships with the fix and not before it.
        HistEvent p = HistEvent::create();
        poisonUuid() = p.uuid;
        p.event_type = CalaosEvent::EventIOChanged;
        p.io_id = poisonIoId();
        p.io_state = poisonIoState();
        p.pic_uid = poisonPicUid();
        p.event_raw = std::string("{\"probe\":\"") + poisonRawMark() + "\"}";
        HistLogger::Instance().appendEvent(p);

        //Wait for the worker to have committed it, otherwise the first case to
        //read sees an empty table. The result is held in a shared_ptr and never
        //captured by reference: a timed out query's callback would still be
        //alive inside the uvw AsyncHandle and would fire during a later pump.
        int seen = 0;
        for (int attempt = 0; attempt < 20 && seen < 2; attempt++)
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

        ASSERT_GE(seen, 2) << "the seeded events never reached the database";
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

    //Locates one seeded event in an eventlog payload by its uuid. Reading by
    //POSITION would make every case depend on the row order sqlite happens to
    //return, and would silently pass if two events were swapped.
    static Json findEvent(const Json &data, const std::string &uuid)
    {
        const Json events = member(data, "events");
        if (!events.is_array())
            return Json();
        for (const auto &e : events)
        {
            if (str(e, "id") == uuid)
                return e;
        }
        return Json();
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
 * 3. ORACLE B - THE ESCAPING. ONE HOUSE, ONE SERVER, ONE ASCII WIRE.
 *
 * BEFORE E4.1b this case was named Delta_TodayTheNlohmannWireIsRawUtf8...: the
 * API served TWO escapings depending on which emitter answered.
 *   - get_home goes through JsonApiHandlerHttp::sendJson(json_t *), dumped with
 *     JSON_ENSURE_ASCII -> the accented IO name of the reference house comes
 *     out as \\u00C9 / \\u00E9, UPPERCASE hex, pure ASCII. UNCHANGED by E4.1b:
 *     zero jansson call is touched by this ticket.
 *   - eventlog goes through JsonApiHandlerHttp::sendJson(const Json &), which
 *     was a BARE dump() -> the accented io_state came out as RAW UTF-8 BYTES.
 *
 * E4.1b removed that asymmetry, and this case is the AFTER picture. The wire
 * stays ASCII-only on both paths; the ONLY residual difference between the two
 * emitters is the CASE of the hexadecimal (\\u00E9 for jansson, \\u00e9 for
 * nlohmann), which is the delta the epic declared and accepted (E4.1.md,
 * invariant 3). Both are read by real JSON parsers on every consumer, so it is
 * invisible to all of them.
 *
 * THIS IS THE ORACLE THE WHOLE SUITE WAS MISSING. Flip ensure_ascii back to
 * false in either emitter and this case goes red - and it is the only one that
 * does. Every assertion is on the RAW response bytes: on the parsed document
 * the three escapings are indistinguishable, which is exactly why nothing
 * covered this dimension before.
 ******************************************************************************/
TEST_F(JsonApiEmissionBytesTest, TheNlohmannHttpWireIsAsciiOnlyAndEscapesWithLowercaseHex)
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

        EXPECT_TRUE(contains(wire, ASCII_E_LOWER))
                << "the nlohmann emitter must escape U+00E9 with LOWERCASE hex";
        EXPECT_FALSE(contains(wire, RAW_E_ACUTE))
                << "ensure_ascii = true means no raw UTF-8 on the wire";
        //Not jansson's form either: the two are one hexadecimal case apart and
        //a case folding comparison would see them as equal. E4.1a found exactly
        //that defect in the ParamsJson tripwire.
        EXPECT_FALSE(contains(wire, ASCII_E_UPPER)) << wire;

        for (unsigned char c : wire)
            ASSERT_LT(c, 0x80u) << "the nlohmann HTTP wire must be ASCII-only";
    }

}

/*******************************************************************************
 * 3bis. ORACLE B, WEBSOCKET HALF.
 *
 * ONE CASE PER TRANSPORT, and that is not cosmetic. The counter-mutation of
 * this ticket is BY EXCHANGE and PER EMITTER: flipping ensure_ascii in ONE of
 * the two emitters must redden ONE case. A single case covering both transports
 * would give the SAME red set for both mutations - and "identical reds from one
 * mutation to the next" is the signature of the _DEPENDENCIES trap, i.e. of a
 * campaign that measured nothing. Four mutations, four distinct singletons.
 ******************************************************************************/
TEST_F(JsonApiEmissionBytesTest, TheNlohmannWebsocketWireIsAsciiOnlyAndEscapesWithLowercaseHex)
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

    EXPECT_TRUE(contains(wire, ASCII_E_LOWER))
            << "the nlohmann WS envelope must escape U+00E9 with LOWERCASE hex";
    EXPECT_FALSE(contains(wire, RAW_E_ACUTE))
            << "ensure_ascii = true means no raw UTF-8 on the wire";
    EXPECT_FALSE(contains(wire, ASCII_E_UPPER)) << wire;

    for (unsigned char c : wire)
        ASSERT_LT(c, 0x80u) << "the nlohmann WS wire must be ASCII-only";
}

/*******************************************************************************
 * 4. ORACLE A - THE ERROR HANDLER, END TO END, ON EACH TRANSPORT SEPARATELY.
 *
 * BEFORE E4.1b these two cases did not fail, THEY KILLED THE BINARY: the
 * poisoned row reaches HistEvent::toJson(), the invalid bytes sit in the tree,
 * the emitter calls a bare dump(), type_error.316 is thrown through an emitter
 * with no try/catch above it and std::terminate ends the process. On a server
 * that is a remote denial of service on a live connection, and the path needs
 * no privilege beyond an ordinary API account:
 *
 *   1. GET /api/?action=set_state&id=<a string IO>&value=%ff%80  - the GET
 *      parameter fallback (JsonApiHandlerHttp.cpp:88) hands percent-decoded
 *      BYTES to Params, no JSON parser anywhere on that path,
 *   2. the IO changes state, EventManager::appendEvent() copies it into
 *      HistEvent::io_state and HistLogger writes the row (log_history),
 *   3. GET /api/?action=eventlog - and the answer is built by the ONE method of
 *      JsonApi.cpp that uses nlohmann.
 *
 * Step 3 is what these two cases drive; the row is seeded directly so the case
 * measures the EMITTER and not the whole chain.
 *
 * TWO CASES AND NOT ONE, on purpose. The counter-mutation asked by the ticket
 * is BY EXCHANGE and PER EMITTER: swap error_handler_t::replace for
 * error_handler_t::ignore in ONE of the two emitters and exactly ONE of these
 * two cases must go red. A single case covering both transports could not tell
 * them apart.
 *
 * NEITHER CASE ASSERTS THAT THE WIRE IS ASCII - see the note in the file
 * header. U+FFFD parses back identically whether it was escaped or not, so
 * these two stay green under an ensure_ascii mutation and oracle B stays green
 * under this one. That separation is the point.
 ******************************************************************************/
TEST_F(JsonApiEmissionBytesTest, InvalidUtf8InTheEventLogIsServedAsReplacementCharOverHttp)
{
    HttpTestRequest req;
    req.send(eventLogRequestBody());

    ASSERT_TRUE(waitUntil([&req]() { return req.count() > 0; }))
            << "eventlog never answered over HTTP";
    ASSERT_EQ(1u, req.count()) << "the answer must be DELIVERED, not swallowed";

    const std::string wire = req.body();

    //The invalid bytes are GONE FROM THE WIRE. Byte level, and invariant to the
    //escaping - this is what oracle A owns.
    EXPECT_FALSE(contains(wire, "\xff")) << "0xFF reached the client";
    EXPECT_FALSE(contains(wire, "\x80")) << "0x80 reached the client";

    //And the answer is still a document, still carrying the poisoned event,
    //with U+FFFD where the bad bytes were and the surviving 'x' after it. A
    //handler that dropped the whole value would pass a "no 0xFF" check.
    const Json body = req.bodyJson();
    const Json ev = findEvent(body, poisonUuid());
    ASSERT_FALSE(ev.is_null()) << "the poisoned event is missing from the answer: " << wire;

    EXPECT_EQ(replacedIoState(), str(ev, "io_state"));
    //Each of the three interchangeable strings on its own key: a swap in the
    //emitter or in HistEvent::toJson() is a failure here, not a shrug.
    EXPECT_EQ(poisonIoId(), str(ev, "io_id"));
    EXPECT_EQ(poisonPicUid(), str(ev, "pic_uid"));

    //Connection still alive: sendJson() answers and does not close.
    EXPECT_TRUE(req.closes().empty()) << "the connection was closed";
}

TEST_F(JsonApiEmissionBytesTest, InvalidUtf8InTheEventLogIsServedAsReplacementCharOverWebsocket)
{
    WsTestSession ws;
    ws.send(wsEventLogRequest());

    ASSERT_TRUE(waitUntil([&ws]() { return ws.count() > 0; }))
            << "eventlog never answered over the websocket";
    ASSERT_EQ(1u, ws.count()) << "the answer must be DELIVERED, not swallowed";

    const std::string wire = ws.lastMessage();

    EXPECT_FALSE(contains(wire, "\xff")) << "0xFF reached the client";
    EXPECT_FALSE(contains(wire, "\x80")) << "0x80 reached the client";

    const Json env = ws.lastEnvelope();
    EXPECT_EQ("eventlog", str(env, "msg"));
    EXPECT_EQ("e41b_msgid", str(env, "msg_id"));

    const Json ev = findEvent(member(env, "data"), poisonUuid());
    ASSERT_FALSE(ev.is_null()) << "the poisoned event is missing from the answer: " << wire;

    EXPECT_EQ(replacedIoState(), str(ev, "io_state"));
    EXPECT_EQ(poisonIoId(), str(ev, "io_id"));
    EXPECT_EQ(poisonPicUid(), str(ev, "pic_uid"));

    //The session was pumped above, so this absence is measured and not assumed.
    EXPECT_TRUE(ws.closes().empty()) << "the connection was closed";
}
