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
 * E4.1l - THE BYTES OF AN EVENT, ON THE THREE WIRES IT TRAVELS.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS FILE EXISTS
 * ---------------------------------------------------------------------------
 * CalaosEvent::toJson() is emitted on THREE different wires, and E4.0d covered
 * exactly two of them, semantically:
 *
 *   1. the websocket push        JsonApiHandlerWS::handleEvents()
 *   2. the HTTP poll_listen      JsonApiHandlerHttp::processPolling()
 *   3. THE HISTORY ROW           EventManager::appendEvent(), which dumps the
 *                                event into HistEvent::event_raw and hands it
 *                                to HistLogger - i.e. into sqlite.
 *
 * Wire 3 HAS NO TEST AT ALL on this tree, and the reason is written in
 * CalaosCoreFixture.h: "HistLogger/DataLogger are only reachable for IOs
 * flagged with log_history=true; the minimal config sets neither". Every
 * eventlog suite of the E4.0/T3.17 series seeds HistLogger::appendEvent()
 * DIRECTLY with a hand built HistEvent, so EventManager.cpp's own dump - the
 * only producer of event_raw in production - was never executed by any case.
 * Migrating it under a green suite would have proven nothing; the three
 * HistoryRow cases below are what makes that path EXERCISED and not merely
 * LINKED (EventManager.o is in CORE_SERVER_OBJECTS, which is not the same
 * thing - see FINDINGS.md, F-LINK-1).
 *
 * ---------------------------------------------------------------------------
 * THE ORACLE IS RAW BYTES, ON PURPOSE, AND THAT IS NOT A HARNESS VIOLATION
 * ---------------------------------------------------------------------------
 * JsonApiCharacterization.h forbids comparing serialized JSON text - for the
 * SEMANTIC suites, whose oracle is the 145 goldens. That oracle is BY
 * CONSTRUCTION blind to key order and to escaping, which are precisely the two
 * things this ticket changes. This file is the byte half, exactly like
 * core/JsonApiEmissionBytes_test.cpp (E4.1b), and it reads
 * WsTestSession::lastMessage(), HttpTestRequest::body() and HistEvent
 * ::event_raw RAW - never lastEnvelope(), never bodyJson().
 *
 * ---------------------------------------------------------------------------
 * THREE ORACLES, DELIBERATELY DISJOINT (same discipline as E4.1b)
 * ---------------------------------------------------------------------------
 * So that a counter-mutation names ONE guilty parameter instead of reddening
 * the file:
 *
 *   ORACLE K - ...KeysAreSortedNotInsertionOrdered
 *     ASCII ONLY payload, so no escaping question can reach it. Sensitive to
 *     the key order of the four members of the event object - and of the
 *     envelope around it. jansson emits INSERTION order (event_raw, type,
 *     type_str, data); nlohmann::json is a std::map and emits SORTED order
 *     (data, event_raw, type, type_str). Invariant 1 of the epic - standard
 *     nlohmann::json, never ordered_json - has no other oracle on this path.
 *
 *   ORACLE A - ...IsAsciiOnlyAndEscapesWithLowercaseHex
 *     probe is a VALID character (U+00E9), so no error handler ever looks at
 *     it. Sensitive to ensure_ascii ONLY. It NEVER asserts key order.
 *
 *   ORACLE R - InvalidUtf8...SurvivesAsReplacementChar...
 *     probe is a byte pair that can never become valid UTF-8. Sensitive to
 *     error_handler_t::replace ONLY: it asserts the key is STILL THERE and
 *     carries U+FFFD. It never asserts that the wire is ASCII (U+FFFD parses
 *     back the same whether written \ufffd or as its three UTF-8 bytes) and
 *     never asserts key order.
 *
 * ---------------------------------------------------------------------------
 * WHAT WAS RED WHEN THIS FILE WAS COMMITTED, AND WHY THAT IS THE POINT
 * ---------------------------------------------------------------------------
 * This is the characterization commit of E4.1l and it is RED on the jansson
 * tree, by construction: it spells the TARGET bytes. On the untouched tree
 *   - the three ORACLE K cases fail (jansson insertion order),
 *   - the three ORACLE A cases fail (jansson writes \u00E9, UPPERCASE hex),
 *   - the three ORACLE R cases fail (jansson_from_params() DROPS a pair whose
 *     value is not valid UTF-8: json_string() answers NULL, so the key simply
 *     vanishes from data.data instead of carrying U+FFFD),
 *   - TheEventTypeTravelsAsAJsonStringOnAllThreeWires is GREEN on both sides.
 *     It is the witness: an int that became a JSON number is a contract break
 *     (the golden oracle is type strict, "3" != 3) and that must not become
 *     true as a side effect of the migration.
 *
 * ---------------------------------------------------------------------------
 * ANTI "FIXTURE PAUVRE" (twelve relapses in this series, all found by review)
 * ---------------------------------------------------------------------------
 * A symmetric payload, or one with a single key, would distinguish neither an
 * order from another nor an escaping from another. Therefore:
 *   - the event object's four members are pinned with FOUR DISTINCT values, so
 *     exchanging any two of them in CalaosEvent::toJson() is a failure and not
 *     a shrug;
 *   - the parameters are inserted in the order zone, alias - the REVERSE of
 *     their alphabetical order - and carry two DIFFERENT accented strings;
 *   - the accented probe is carried by ONE parameter only, so a case cannot
 *     confuse "escapes everything" with "escapes the first thing it meets";
 *   - the type used for the order cases (room_changed, 7) and the one used for
 *     the history cases (io_changed, 3) are different, and both numbers and
 *     both strings are asserted by name.
 *
 * Ids and the history database directory are prefixed e41l_. Taken so far:
 * e40_, e40b_ .. e40f_, e41b_, t317a_ .. t317f_, t319_.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "EventManager.h"
#include "HistLogger.h"
#include "IOBase.h"
#include "ListeRoom.h"

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
 * The byte shapes, spelled in escapes so no editor and no locale can rewrite
 * them. The two ASCII forms differ ONLY by the case of the hexadecimal - the
 * whole delta the epic accepted (E4.1.md, invariant 3). Nothing here is case
 * folded: case folding is exactly the defect E4.1a found in the ParamsJson
 * tripwire, which is why it had to be rewritten.
 ******************************************************************************/
const char *const RAW_E_ACUTE   = "\xc3\xa9";
const char *const RAW_A_GRAVE   = "\xc3\xa0";
const char *const ASCII_E_UPPER = "\\u00E9";
const char *const ASCII_E_LOWER = "\\u00e9";
const char *const ASCII_A_LOWER = "\\u00e0";
const char *const ASCII_REPLACEMENT = "\\ufffd";

//0xFF is not a legal lead byte in any position and 0x80 is a continuation byte
//with nothing to continue: this pair can never become valid UTF-8 by accident,
//so whatever the tree does with it, it is never "some other text".
const char *const INVALID_UTF8  = "\xff\x80";

bool contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

bool hasAnyByteAbove7f(const std::string &s)
{
    for (unsigned char c: s)
        if (c >= 0x80)
            return true;
    return false;
}

//Position of a member NAME as it appears on the wire, i.e. of the exact byte
//sequence "<name>": . Answers npos when absent. `from` lets a case skip the
//envelope's own "data" and look inside the event object.
size_t keyPos(const std::string &wire, const char *name, size_t from = 0)
{
    return wire.find(std::string("\"") + name + "\":", from);
}

} //namespace

class EventWireBytesTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        //MUST run before CoreFixture::SetUp() installs the per-case
        //directories. Verbatim in shape from E4.0e / T3.17f / E4.1b: HistLogger
        //is a Meyers singleton whose constructor captures
        //Utils::getCacheFile("events.db") and opens it from a worker thread; a
        //singleton born inside a case would point at a directory TearDown()
        //rm -rf's milliseconds later, and `sqlite::database db(dbname)`
        //(HistLogger.cpp:190) sits OUTSIDE the try of :192 - cantopen is then
        //an uncaught exception in a std::thread, i.e. the loss of the binary.
        //Only the directory template and the id prefix differ, so that no two
        //binaries ever share a database.
        ensureHistLogger();

        JsonApiCharacterizationTest::SetUp();

        //NO DEFENSIVE PUMP. E4.0g made TearDown() drain AFTER the parent
        //teardown, so a case starts on an empty queue. If one ever becomes
        //necessary, a SECOND source of surviving events exists: name it in
        //FINDINGS.md instead of pumping it away.
    }

    static void ensureHistLogger()
    {
        static bool done = false;
        if (done)
            return;
        done = true;

        static char tmpl[] = "/tmp/calaos_e41l_hist_XXXXXX";
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

    //Loop pumping with a deadline: HistLogger answers from its sqlite worker
    //thread through a uvw AsyncHandle, so nothing arrives without pumping. The
    //2s bound stays well under the 30s read timeout HttpClient installs in its
    //constructor.
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

    /* ---------------------------------------------------------------------
     * Wire 1 - the websocket push, raw.
     * ------------------------------------------------------------------ */
    static std::string wsEventWire(int type, Params p)
    {
        WsTestSession ws;
        EventManager::create(type, p);
        pumpEventLoop();
        EXPECT_EQ(1u, ws.count()) << "exactly one push was expected";
        return ws.lastMessage();
    }

    /* ---------------------------------------------------------------------
     * Wire 2 - the HTTP poll_listen answer, raw body.
     *
     * Every HttpTestRequest destructor pumps the loop (harness trap 1), which
     * is why the event is raised and flushed between the two requests and
     * never left to a destructor to deliver.
     * ------------------------------------------------------------------ */
    std::string pollEventWire(int type, Params p)
    {
        std::string uuid;
        {
            HttpTestRequest req;
            req.send(authenticated(Json{{ "action", "poll_listen" },
                                        { "type", "register" }}));
            const Json body = req.bodyJson();
            uuid = body.is_object()? body.value("uuid", std::string())
                                   : std::string();
        }
        EXPECT_FALSE(uuid.empty()) << "poll_listen register answered no uuid";

        EventManager::create(type, p);
        pumpEventLoop();

        HttpTestRequest req;
        req.send(authenticated(Json{{ "action", "poll_listen" },
                                    { "type", "get" },
                                    { "uuid", uuid }}));
        return req.body();
    }

    /* ---------------------------------------------------------------------
     * Wire 3 - the history row, raw, as EventManager wrote it into sqlite.
     *
     * THE ONLY WAY to reach EventManager.cpp's own dump: appendEvent() writes
     * the row only for an EventIOChanged carrying "state", on an IO that
     * exists AND whose log_history param is the string "true". The reference
     * house sets it nowhere, which is why no suite of the series had ever run
     * this code. The write is SYNCHRONOUS, at queue time, not on the idler.
     * ------------------------------------------------------------------ */
    static void enableHistoryOn(const char *ioId)
    {
        IOBase *io = ListeRoom::Instance().get_io(ioId);
        ASSERT_TRUE(io != nullptr) << "no such IO: " << ioId;
        io->set_param("log_history", "true");
        ASSERT_EQ("true", io->get_param("log_history"));
    }

    //The event_raw column of the most recent history row whose io_id matches.
    //Returns "" when no such row exists, so a case can tell "nothing written"
    //from "written wrong".
    static std::string historyRawFor(const std::string &ioId)
    {
        auto answered = std::make_shared<bool>(false);
        auto found = std::make_shared<std::string>();
        HistLogger::Instance().getEvents(0, 200,
                [answered, found, ioId](bool success, string,
                                        const vector<HistEvent> &evs, int, int)
        {
            if (success)
            {
                for (const HistEvent &e: evs)
                {
                    if (e.io_id == ioId)
                    {
                        *found = e.event_raw;
                        break;
                    }
                }
            }
            *answered = true;
        });
        waitUntil([answered]() { return *answered; });
        EXPECT_TRUE(*answered) << "the history database never answered";
        return *found;
    }
};

/*******************************************************************************
 * ORACLE K - key order. ASCII only payloads: no escaping question can reach
 * these three cases, and none of them looks at a non-ASCII byte.
 ******************************************************************************/

TEST_F(EventWireBytesTest, TheWebsocketEventEnvelopeAndObjectAreBothKeySorted)
{
    loadReferenceHouse();

    //room_changed = 7. Parameters inserted zone-then-alias, the reverse of
    //their alphabetical order, so this case also sees a producer that would
    //have kept insertion order inside data.data.
    const std::string wire = wsEventWire(CalaosEvent::EventRoomChanged,
                                         {{ "zone", "kitchen" },
                                          { "alias", "cellar" }});
    ASSERT_FALSE(wire.empty());
    ASSERT_FALSE(hasAnyByteAbove7f(wire)) << "this case must stay ASCII: " << wire;

    //The envelope. The jansson overload writes msg then data; the nlohmann one
    //sorts, so data comes first. There is no msg_id on an unsolicited push.
    const size_t envData = keyPos(wire, "data");
    const size_t envMsg  = keyPos(wire, "msg");
    ASSERT_NE(std::string::npos, envData) << wire;
    ASSERT_NE(std::string::npos, envMsg) << wire;
    EXPECT_LT(envData, envMsg)
            << "the event envelope is not key sorted: " << wire;
    EXPECT_FALSE(contains(wire, "\"msg_id\":"))
            << "an unsolicited push must carry no msg_id: " << wire;

    //The event object itself, looked up AFTER the envelope's own "data":.
    const size_t inner = envData + 1;
    const size_t evData     = keyPos(wire, "data", inner);
    const size_t evEventRaw = keyPos(wire, "event_raw", inner);
    const size_t evType     = keyPos(wire, "type", inner);
    const size_t evTypeStr  = keyPos(wire, "type_str", inner);
    ASSERT_NE(std::string::npos, evData) << wire;
    ASSERT_NE(std::string::npos, evEventRaw) << wire;
    ASSERT_NE(std::string::npos, evType) << wire;
    ASSERT_NE(std::string::npos, evTypeStr) << wire;

    EXPECT_LT(evData, evEventRaw) << "data must precede event_raw: " << wire;
    EXPECT_LT(evEventRaw, evType) << "event_raw must precede type: " << wire;
    EXPECT_LT(evType, evTypeStr) << "type must precede type_str: " << wire;

    //The four members carry four DIFFERENT values, so an exchange of any two
    //of them in CalaosEvent::toJson() is a failure here and not a shrug.
    EXPECT_TRUE(contains(wire, "\"type\":\"7\"")) << wire;
    EXPECT_TRUE(contains(wire, "\"type_str\":\"room_changed\"")) << wire;
    EXPECT_TRUE(contains(wire,
            "\"event_raw\":\"room_changed alias:cellar zone:kitchen\"")) << wire;
    EXPECT_TRUE(contains(wire, "\"alias\":\"cellar\"")) << wire;
    EXPECT_TRUE(contains(wire, "\"zone\":\"kitchen\"")) << wire;
}

TEST_F(EventWireBytesTest, ThePollAnswerAndItsEventObjectsAreBothKeySorted)
{
    loadReferenceHouse();

    const std::string body = pollEventWire(CalaosEvent::EventRoomChanged,
                                           {{ "zone", "kitchen" },
                                            { "alias", "cellar" }});
    ASSERT_FALSE(body.empty());
    ASSERT_FALSE(hasAnyByteAbove7f(body)) << "this case must stay ASCII: " << body;

    //The poll answer's own two members: jansson wrote success then events.
    const size_t rootEvents  = keyPos(body, "events");
    const size_t rootSuccess = keyPos(body, "success");
    ASSERT_NE(std::string::npos, rootEvents) << body;
    ASSERT_NE(std::string::npos, rootSuccess) << body;
    EXPECT_LT(rootEvents, rootSuccess)
            << "the poll answer is not key sorted: " << body;

    //success is the STRING "true", never a JSON boolean. Frozen by E4.0d,
    //re-pinned here on the bytes because this ticket rebuilds the document.
    EXPECT_TRUE(contains(body, "\"success\":\"true\"")) << body;

    const size_t inner = rootEvents + 1;
    const size_t evData     = keyPos(body, "data", inner);
    const size_t evEventRaw = keyPos(body, "event_raw", inner);
    const size_t evType     = keyPos(body, "type", inner);
    const size_t evTypeStr  = keyPos(body, "type_str", inner);
    ASSERT_NE(std::string::npos, evData) << body;
    ASSERT_NE(std::string::npos, evEventRaw) << body;
    ASSERT_NE(std::string::npos, evType) << body;
    ASSERT_NE(std::string::npos, evTypeStr) << body;

    EXPECT_LT(evData, evEventRaw) << "data must precede event_raw: " << body;
    EXPECT_LT(evEventRaw, evType) << "event_raw must precede type: " << body;
    EXPECT_LT(evType, evTypeStr) << "type must precede type_str: " << body;

    EXPECT_TRUE(contains(body, "\"type\":\"7\"")) << body;
    EXPECT_TRUE(contains(body, "\"type_str\":\"room_changed\"")) << body;
    EXPECT_TRUE(contains(body,
            "\"event_raw\":\"room_changed alias:cellar zone:kitchen\"")) << body;
}

TEST_F(EventWireBytesTest, TheHistoryRowOfALoggedEventIsKeySorted)
{
    loadReferenceHouse();

    //io_changed = 3, on the string IO of the house, with history turned on.
    enableHistoryOn(HOUSE_STRING);
    EventManager::create(CalaosEvent::EventIOChanged,
                         {{ "id", HOUSE_STRING }, { "state", "attic" }});

    const std::string raw = historyRawFor(HOUSE_STRING);
    ASSERT_FALSE(raw.empty())
            << "EventManager wrote no history row at all - the path this case "
               "exists to exercise was not taken";
    ASSERT_FALSE(hasAnyByteAbove7f(raw)) << "this case must stay ASCII: " << raw;

    const size_t evData     = keyPos(raw, "data");
    const size_t evEventRaw = keyPos(raw, "event_raw");
    const size_t evType     = keyPos(raw, "type");
    const size_t evTypeStr  = keyPos(raw, "type_str");
    ASSERT_NE(std::string::npos, evData) << raw;
    ASSERT_NE(std::string::npos, evEventRaw) << raw;
    ASSERT_NE(std::string::npos, evType) << raw;
    ASSERT_NE(std::string::npos, evTypeStr) << raw;

    EXPECT_LT(evData, evEventRaw) << "data must precede event_raw: " << raw;
    EXPECT_LT(evEventRaw, evType) << "event_raw must precede type: " << raw;
    EXPECT_LT(evType, evTypeStr) << "type must precede type_str: " << raw;

    //A different type from the two cases above, and both halves asserted.
    EXPECT_TRUE(contains(raw, "\"type\":\"3\"")) << raw;
    EXPECT_TRUE(contains(raw, "\"type_str\":\"io_changed\"")) << raw;
    EXPECT_TRUE(contains(raw, "\"state\":\"attic\"")) << raw;

    //The row is written SYNCHRONOUSLY, at queue time - no pump was run between
    //create() and the read above other than the one historyRawFor() needs for
    //the sqlite answer itself.
    EXPECT_TRUE(contains(raw, std::string("\"id\":\"") + HOUSE_STRING + "\"")) << raw;
}

/*******************************************************************************
 * ORACLE A - escaping. The probe is a VALID character, so no error handler
 * ever looks at it. These three cases NEVER assert key order.
 ******************************************************************************/

TEST_F(EventWireBytesTest, TheWebsocketEventWireIsAsciiOnlyAndEscapesWithLowercaseHex)
{
    loadReferenceHouse();

    //TWO different accented strings, on ONE parameter each, so this case can
    //tell an emitter that escapes everything from one that escapes only what it
    //meets first. The third parameter stays pure ASCII, deliberately.
    const std::string wire = wsEventWire(CalaosEvent::EventRoomChanged,
                                         {{ "zone", std::string("s") + RAW_E_ACUTE + "jour" },
                                          { "alias", std::string("caf") + RAW_A_GRAVE },
                                          { "plain", "cellar" }});
    ASSERT_FALSE(wire.empty());

    EXPECT_FALSE(hasAnyByteAbove7f(wire))
            << "the event wire must stay pure ASCII: " << wire;
    EXPECT_TRUE(contains(wire, ASCII_E_LOWER))
            << "U+00E9 must be escaped with LOWERCASE hex: " << wire;
    EXPECT_TRUE(contains(wire, ASCII_A_LOWER))
            << "U+00E0 must be escaped with LOWERCASE hex: " << wire;
    EXPECT_FALSE(contains(wire, ASCII_E_UPPER))
            << "UPPERCASE hex is jansson's form, not nlohmann's: " << wire;
    EXPECT_FALSE(contains(wire, RAW_E_ACUTE))
            << "raw UTF-8 bytes are form 2 of the tripwire, not form 3: " << wire;
    EXPECT_TRUE(contains(wire, "cellar"))
            << "the ASCII parameter must be untouched: " << wire;
}

TEST_F(EventWireBytesTest, ThePollEventWireIsAsciiOnlyAndEscapesWithLowercaseHex)
{
    loadReferenceHouse();

    const std::string body = pollEventWire(CalaosEvent::EventRoomChanged,
                                           {{ "zone", std::string("s") + RAW_E_ACUTE + "jour" },
                                            { "alias", std::string("caf") + RAW_A_GRAVE },
                                            { "plain", "cellar" }});
    ASSERT_FALSE(body.empty());

    EXPECT_FALSE(hasAnyByteAbove7f(body))
            << "the poll wire must stay pure ASCII: " << body;
    EXPECT_TRUE(contains(body, ASCII_E_LOWER)) << body;
    EXPECT_TRUE(contains(body, ASCII_A_LOWER)) << body;
    EXPECT_FALSE(contains(body, ASCII_E_UPPER)) << body;
    EXPECT_FALSE(contains(body, RAW_E_ACUTE)) << body;
    EXPECT_TRUE(contains(body, "cellar")) << body;
}

TEST_F(EventWireBytesTest, TheHistoryRowIsAsciiOnlyAndEscapesWithLowercaseHex)
{
    loadReferenceHouse();

    enableHistoryOn(HOUSE_BOOL_OUT);
    EventManager::create(CalaosEvent::EventIOChanged,
                         {{ "id", HOUSE_BOOL_OUT },
                          { "state", std::string("s") + RAW_E_ACUTE + "jour" }});

    const std::string raw = historyRawFor(HOUSE_BOOL_OUT);
    ASSERT_FALSE(raw.empty())
            << "EventManager wrote no history row at all";

    EXPECT_FALSE(hasAnyByteAbove7f(raw))
            << "the history row must stay pure ASCII: " << raw;
    EXPECT_TRUE(contains(raw, ASCII_E_LOWER))
            << "U+00E9 must be escaped with LOWERCASE hex: " << raw;
    EXPECT_FALSE(contains(raw, ASCII_E_UPPER))
            << "UPPERCASE hex is jansson's form: " << raw;
    EXPECT_FALSE(contains(raw, RAW_E_ACUTE)) << raw;
}

/*******************************************************************************
 * ORACLE R - the error handler. The probe can never become valid UTF-8. These
 * three cases assert that the pair is STILL THERE and carries U+FFFD; they
 * assert neither ASCII-ness nor key order.
 *
 * On the jansson tree the pair does not survive at all: json_string() answers
 * NULL on invalid UTF-8 and json_object_set_new() then drops the pair in
 * silence (Jansson_Addition.h, jansson_from_params(), and its own comment says
 * so). Turning that silent drop into U+FFFD is the observable behaviour change
 * this ticket carries, and it is the SAME arbitrage as E4.1j / F-LUA-2: both
 * outcomes are garbage, the entry was already broken in both, and the nature of
 * the observable does not change.
 ******************************************************************************/

TEST_F(EventWireBytesTest, InvalidUtf8InAnEventParameterSurvivesAsReplacementCharOverWebsocket)
{
    loadReferenceHouse();

    const std::string wire = wsEventWire(CalaosEvent::EventRoomChanged,
                                         {{ "zone", std::string("head") + INVALID_UTF8 + "tail" },
                                          { "alias", "cellar" }});
    ASSERT_FALSE(wire.empty())
            << "nothing was emitted at all - a bare dump() would have thrown "
               "type_error.316 here, and nothing catches it above the emitter";

    EXPECT_TRUE(contains(wire, "\"zone\":"))
            << "the pair was DROPPED instead of being repaired: " << wire;
    EXPECT_TRUE(contains(wire, ASCII_REPLACEMENT) ||
                contains(wire, "\xef\xbf\xbd"))
            << "the invalid bytes did not become U+FFFD: " << wire;
    EXPECT_FALSE(contains(wire, INVALID_UTF8))
            << "the invalid bytes went out unchanged: " << wire;
    //The rest of the document is intact: this is a repair, not a truncation.
    EXPECT_TRUE(contains(wire, "\"alias\":\"cellar\"")) << wire;
    EXPECT_TRUE(contains(wire, "\"type_str\":\"room_changed\"")) << wire;
}

TEST_F(EventWireBytesTest, InvalidUtf8InAnEventParameterSurvivesAsReplacementCharOverPoll)
{
    loadReferenceHouse();

    const std::string body = pollEventWire(CalaosEvent::EventRoomChanged,
                                           {{ "zone", std::string("head") + INVALID_UTF8 + "tail" },
                                            { "alias", "cellar" }});
    ASSERT_FALSE(body.empty())
            << "nothing was served at all - a bare dump() would have thrown "
               "type_error.316 on a live connection";

    EXPECT_TRUE(contains(body, "\"zone\":"))
            << "the pair was DROPPED instead of being repaired: " << body;
    EXPECT_TRUE(contains(body, ASCII_REPLACEMENT) ||
                contains(body, "\xef\xbf\xbd"))
            << "the invalid bytes did not become U+FFFD: " << body;
    EXPECT_FALSE(contains(body, INVALID_UTF8)) << body;
    EXPECT_TRUE(contains(body, "\"alias\":\"cellar\"")) << body;
}

TEST_F(EventWireBytesTest, InvalidUtf8InALoggedEventStateSurvivesAsReplacementCharInTheHistoryRow)
{
    loadReferenceHouse();

    enableHistoryOn(HOUSE_INT);
    EventManager::create(CalaosEvent::EventIOChanged,
                         {{ "id", HOUSE_INT },
                          { "state", std::string("head") + INVALID_UTF8 + "tail" }});

    const std::string raw = historyRawFor(HOUSE_INT);
    ASSERT_FALSE(raw.empty())
            << "no history row was written - on the jansson tree json_pack() "
               "still succeeds here, so an empty row means something else broke";

    EXPECT_TRUE(contains(raw, "\"state\":"))
            << "the pair was DROPPED instead of being repaired: " << raw;
    EXPECT_TRUE(contains(raw, ASCII_REPLACEMENT) ||
                contains(raw, "\xef\xbf\xbd"))
            << "the invalid bytes did not become U+FFFD: " << raw;
    EXPECT_FALSE(contains(raw, INVALID_UTF8)) << raw;
    EXPECT_TRUE(contains(raw, std::string("\"id\":\"") + HOUSE_INT + "\"")) << raw;
}

/*******************************************************************************
 * THE WITNESS - green before, green after.
 *
 * Everything the event path emits is a JSON STRING, "type" included: it is the
 * enum integer run through Utils::to_string(). The golden oracle of the series
 * is TYPE STRICT ("3" != 3), so an int that became a real JSON number is a
 * contract break, and this is the byte level statement of it on all three
 * wires at once. It must be green on both sides of the migration; if it is
 * ever red, the migration "improved" a value.
 ******************************************************************************/

TEST_F(EventWireBytesTest, TheEventTypeTravelsAsAJsonStringOnAllThreeWires)
{
    loadReferenceHouse();

    const std::string wire = wsEventWire(CalaosEvent::EventRoomChanged,
                                         {{ "zone", "kitchen" }});
    EXPECT_TRUE(contains(wire, "\"type\":\"7\"")) << wire;
    EXPECT_FALSE(contains(wire, "\"type\":7")) << wire;

    const std::string body = pollEventWire(CalaosEvent::EventRoomChanged,
                                           {{ "zone", "kitchen" }});
    EXPECT_TRUE(contains(body, "\"type\":\"7\"")) << body;
    EXPECT_FALSE(contains(body, "\"type\":7")) << body;

    enableHistoryOn(HOUSE_BOOL_IN);
    EventManager::create(CalaosEvent::EventIOChanged,
                         {{ "id", HOUSE_BOOL_IN }, { "state", "true" }});
    const std::string raw = historyRawFor(HOUSE_BOOL_IN);
    ASSERT_FALSE(raw.empty());
    EXPECT_TRUE(contains(raw, "\"type\":\"3\"")) << raw;
    EXPECT_FALSE(contains(raw, "\"type\":3")) << raw;
}
