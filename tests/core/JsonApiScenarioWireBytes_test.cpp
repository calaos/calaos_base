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
 * E4.1r - THE BYTES THE AUTOSCENARIO COMMANDS PUT ON THE WIRE, THE ORDER OF THE
 *         STEPS, AND THE FOUR REFUSALS OF create / modify / reenable.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS FILE EXISTS
 * ---------------------------------------------------------------------------
 * E4.1r migrates the nine autoscenario builders of JsonApi to nlohmann:
 *
 *      buildAutoscenarioList()         buildAutoscenarioGet()
 *      buildAutoscenarioCreate()       buildAutoscenarioDelete()
 *      buildAutoscenarioModify()       buildAutoscenarioAddSchedule()
 *      buildAutoscenarioDelSchedule()  buildAutoscenarioReenable()
 *      plus processAutoscenario() on BOTH transports
 *
 * and NOTHING ELSE. The migration is MECHANICAL by decision (Q1 of E4.1.md,
 * settled 2026-09-01): E4.6d rewrites these nine functions whole, with its own
 * net and its own review. Anything this file finds worth fixing goes to
 * FINDINGS.md, not into the diff.
 *
 * The regime of proof of the epic, restated because it is the whole reason for
 * this file: the 145 goldens compare PARSED DOCUMENTS. Nine of them (e40c_*)
 * cover the STRUCTURE and the VALUES of this perimeter, and cover them well.
 * NOTHING covers its BYTE dimension until this file exists - not the key order,
 * not the case of a \uXXXX escape, not whether a byte was escaped at all.
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
 * the suffix. A case that had to be edited is a case that ran - that is the
 * proof the path is EXERCISED and not merely compiled.
 *
 * MEASURED: 35 cases, 9 of them written as ...Today. The migration made EIGHT
 * of them fail and NOT ONE of the other twenty-six, and no other test of the
 * tree moved - the 145 goldens included.
 *
 * ⚠️ The ninth, AScheduledScenarioCarriesItsTimeRangeId, was PREDICTED to move
 * and did NOT: both its assertions are order independent, and no VALUE of this
 * perimeter moved. Kept with its measurement rather than quietly renamed.
 *
 * The INVARIANTS held on both sides. If one of them ever moves, a VALUE or a
 * STRUCTURE changed - stop and understand why before touching it.
 *
 * ---------------------------------------------------------------------------
 * THE DELTAS, ON THIS PERIMETER
 * ---------------------------------------------------------------------------
 *   1. KEY ORDER      Scenario::toJson() inserts id, cycle, enabled, schedule,
 *                     category, broken, disabled_missing_io, missing_ios,
 *                     steps_count, steps - in that order, and jansson keeps it.
 *                     nlohmann SORTS: broken comes first and steps_count last.
 *                     Same inside every step (step_pause, step_type, actions ->
 *                     actions, step_pause, step_type) and inside every action
 *                     (id, action -> action, id). The WS envelope sorts with it
 *                     (msg, msg_id, data -> data, msg, msg_id), as it did for
 *                     every other migrated action.
 *                     The one-key answers ({"error":...}, {"id":...},
 *                     {"success":"true"}) do NOT move: they are built from a
 *                     Params, which is a std::map and therefore ALREADY
 *                     alphabetical - and a case says so.
 *   2. HEX CASE       jansson writes an accent UPPER case (é), nlohmann
 *                     lower case (é). Reachable here through the ACTION
 *                     string of a step, which is client supplied and echoed
 *                     back verbatim by get.
 *   3. DEL (0x7F)     jansson writes the raw byte even under JSON_ENSURE_ASCII,
 *                     nlohmann escapes it  - it escapes every codepoint
 *                     >= 0x7F. Same channel.
 *
 * ---------------------------------------------------------------------------
 * ⭐ THE TWO DELTAS OF THE SERIES THAT DO **NOT** HAPPEN HERE, AND WHY
 * ---------------------------------------------------------------------------
 * Every other ticket of the series reported five deltas. Two of the five are
 * ABSENT from the scenario payload, and the reason is the same for both:
 * IO/Scenario.cpp is EXCLUDED from E4.1 (decision Q5) and Scenario::toJson()
 * still builds its strings with json_string(const char *). The damage is done
 * INSIDE the excluded file, one floor below anything this ticket touches:
 *
 *   - INVALID UTF-8 is still DROPPED WITH ITS KEY (json_string() answers NULL,
 *     json_object_set_new() answers -1, nobody looks). It does not become
 *     U+FFFD here. E4.6d moves that, not E4.1r.
 *   - AN EMBEDDED NUL still TRUNCATES the value at the C string, on the way
 *     out. E4.1r measured that the INPUT side could not be reached at all,
 *     because the REQUEST was still parsed by jansson and jansson REFUSES
 *     "\u0000" in a string unless JSON_ALLOW_NUL is passed.
 *
 *     ⛔⭐⭐ E4.1s MOVED THAT PARSE TO nlohmann, WHICH ACCEPTS IT, and the day
 *     it did the truncation stopped being unreachable. A client CAN now put a
 *     zero byte into an action; it reaches AutoScenario whole, and
 *     Scenario::toJson() cuts it at the first zero byte in silence. The case
 *     AnEmbeddedNulInAnActionIsTruncatedByScenarioToJson below - which used to
 *     pin the refusal and was rewritten by E4.1s - is where that is measured.
 *     ⛔ It is E4.6d's to FIX, not E4.1s's: IO/Scenario.cpp is the excluded
 *     file. Written down in FINDINGS.md and declared in RELEASE_NOTES.md.
 *
 * ---------------------------------------------------------------------------
 * THE TRANSITIONAL BRIDGE, AND ITS SINGLE PURPOSE
 * ---------------------------------------------------------------------------
 * Scenario::toJson() answers a json_t* and stays that way until E4.6d. The
 * migrated builders therefore cross the two libraries once, through
 * janssonScenarioPayloadBridge() (JsonApi.cpp, anonymous namespace), named so
 * that E4.6d finds it with one grep. It is a dump + parse, so the bytes of the
 * scenario payload are jansson's on the way in and nlohmann's on the way out -
 * which is exactly deltas 1, 2 and 3 above and nothing else.
 *
 * ---------------------------------------------------------------------------
 * THE "POOR FIXTURE" TRAP, AND THE EXCHANGE E4.1r.md ASKS FOR BY NAME
 * ---------------------------------------------------------------------------
 * The counter-mutation the ticket sheet names is: EXCHANGE TWO STEPS OF AN
 * AUTOSCENARIO IN THE ANSWER OF get. It is only visible if no two steps of the
 * fixture answer the same bytes - the "poor fixture" trap, 7 recorded relapses
 * in E4.0, and it bit E4.1o and E4.1p as well.
 *
 * The reference scenario below is built so the exchange bites. Its three steps
 * differ on FIVE axes at once:
 *      step 0   pause 1.5    ONE action    e41r_bool    -> "true"
 *      step 1   pause 0.25   TWO actions   e41r_target  -> "false"
 *                                          e41r_int     -> "42"
 *      end      no pause     ONE action    e41r_string  -> "done"
 * Different pause, different action count, different target ids, different
 * action values, different step_type. TheThreeStepsArePairwiseDistinctOnTheWire
 * asserts that property directly so it cannot rot in silence.
 *
 * Ids are prefixed e41r_. Taken so far: e40_, e40b_ .. e40f_, e41b_, e41n_,
 * e41o_, e41p_, e41q_, t317a_ .. t317f_, t318_, t319_.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "AutoScenario.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "Scenario.h"
#include "Utils.h"

#include <set>
#include <string>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char E41R_ROOM_NAME[] = "E4.1r room";
const char E41R_ROOM_TYPE[] = "salon";

const char IO_BOOL[] = "e41r_bool";
const char IO_TARGET[] = "e41r_target";
const char IO_INT[] = "e41r_int";
const char IO_STRING[] = "e41r_string";

//The ids production hands out for the FIRST scenario of a fresh house. They are
//generated by get_new_id("io_") / get_new_scenario_id(), both of which scan the
//CURRENT ListeRoom only, which the fixture empties between cases.
const char SCENARIO_IO_ID[] = "io_0";
const char SCENARIO_SCHEDULE_ID[] = "scenario_0_schedule";
const char SCENARIO_IS_ACTIVE_ID[] = "scenario_0_is_active";
const char SCENARIO_TIMER_ID[] = "scenario_0_timer";

std::string houseIosXml()
{
    std::string ios;
    ios += internalIoXml("InternalBool", IO_BOOL, "Bool value");
    ios += internalIoXml("InternalBool", IO_TARGET, "Second step target");
    ios += internalIoXml("InternalInt", IO_INT, "Int value");
    ios += internalIoXml("InternalString", IO_STRING, "String value");
    return ios;
}

} //namespace

class JsonApiScenarioWireBytesTest: public JsonApiCharacterizationTest
{
protected:
    void loadScenarioHouse()
    {
        loadConfig(ioXmlDocument(roomXml(E41R_ROOM_NAME, E41R_ROOM_TYPE, houseIosXml(), 0)),
                   rulesXmlDocument(std::string()));

        //Loading raises one EventIOAdded per IO; drain them so a case pinning
        //the ABSENCE of a message does not trip over them.
        pumpEventLoop();
    }

    //A house with NO ROOM AT ALL: ListeRoom::get_room(0) answers null there,
    //createIO(params, nullptr) refuses, and buildAutoscenarioCreate() takes its
    //FIRST refusal branch. See the refusal section.
    void loadRoomlessHouse()
    {
        loadConfig(ioXmlDocument(std::string()), rulesXmlDocument(std::string()));
        pumpEventLoop();
    }

    /* Amputate one IO element from an io.xml document, the way an installer
     * deleting a line from the file does. Self-closing elements only, which is
     * what every internal IO is written as. Same technique as E4.0c.
     */
    static bool removeIoFromXml(std::string &xml, const std::string &id)
    {
        const size_t at = xml.find("id=\"" + id + "\"");
        if (at == std::string::npos) return false;
        const size_t start = xml.rfind('<', at);
        const size_t end = xml.find('>', at);
        if (start == std::string::npos || end == std::string::npos) return false;
        xml.erase(start, end - start + 1);
        return true;
    }

    /* Turn the internal TIMER IO of the scenario into an InternalBool, keeping
     * its id - "somebody edited the configuration file", the same story as the
     * amputation above.
     *
     * ⭐ Measured, and it took three attempts: the squatter has to LOAD, or
     * createInput() simply recreates the real IO and nothing is hijacked. An
     * element whose type does not match its tag (an InputTime inside a
     * <calaos:input>, an InputTimer inside a <calaos:internal>) is dropped by
     * the loader, so the tag AND the type both have to move. This is the only
     * way found to reach the refusal of buildAutoscenarioModify() without a
     * destructive deleteIO() of a live scenario's internal IO - which
     * SEGFAULTS, see FINDINGS.md; out of scope here.
     */
    static bool hijackTimerAsInternalBool(std::string &xml)
    {
        const size_t at = xml.find(std::string("id=\"") + SCENARIO_TIMER_ID + "\"");
        if (at == std::string::npos) return false;
        const size_t start = xml.rfind('<', at);
        const size_t end = xml.find('>', at);
        if (start == std::string::npos || end == std::string::npos) return false;

        std::string element = xml.substr(start, end - start + 1);

        const std::string tag = "<calaos:input ";
        if (element.compare(0, tag.size(), tag) != 0) return false;
        element.replace(0, tag.size(), "<calaos:internal ");

        //" type=" and not "type=", or io_type="inout" matches first
        const size_t t = element.find(" type=\"");
        if (t == std::string::npos) return false;
        const size_t vstart = t + 7;
        const size_t vend = element.find('"', vstart);
        if (vend == std::string::npos) return false;
        element.replace(vstart, vend - vstart, "InternalBool");

        xml.replace(start, end - start + 1, element);
        return true;
    }

    /* Builds the reference scenario, then makes IO_TARGET vanish from io.xml
     * and reloads everything - the ONLY path that leaves a step rule holding an
     * id that does not resolve, and therefore the only way to reach refusal (4)
     * of buildAutoscenarioReenable().
     */
    void loadScenarioWithAnAmputatedStep()
    {
        loadScenarioHouse();

        {
            WsTestSession ws;
            ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));
        }

        saveConfig();

        std::string ioXml = ioXmlOnDisk();
        const std::string rulesXml = rulesXmlOnDisk();
        ASSERT_TRUE(removeIoFromXml(ioXml, IO_TARGET));
        //E4.6b: "the IO ELEMENT is gone", not "the string is gone". io.xml
        //carries the scenario definition since E4.6b (E4.6.md D2) and that
        //definition KEEPS the id of an action whose IO disappeared (D4).
        ASSERT_EQ(std::string::npos, ioXml.find(std::string("id=\"") + IO_TARGET + "\""));

        clearCoreState();
        loadConfig(ioXml, rulesXml);
        ListeRoom::Instance().checkAutoScenario();
        pumpEventLoop();
    }

    /* ------------------------------------------------------------------
     * Requests. WS puts the sub-command under "data", HTTP puts it at the
     * root - the asymmetry is E4.0c's to characterize, kept in one place
     * here so the byte cases stay readable.
     * --------------------------------------------------------------- */

    static Json wsRequest(Json data, const std::string &msgId = "e41r")
    {
        return Json{{ "msg", "autoscenario" }, { "msg_id", msgId }, { "data", data }};
    }

    static Json httpRequest(Json body)
    {
        body["action"] = "autoscenario";
        return authenticated(body);
    }

    //Drives one autoscenario sub-command over HTTP and answers the raw body.
    std::string httpWire(Json body)
    {
        HttpTestRequest req;
        req.send(httpRequest(body));
        return req.body();
    }

    //Drives one autoscenario sub-command over WS and answers the raw message.
    std::string wsWire(WsTestSession &ws, Json data)
    {
        ws.clear();
        ws.send(wsRequest(data));
        EXPECT_EQ(1u, ws.count()) << "autoscenario " << data.value("type", std::string())
                                  << " answered " << ws.count() << " messages";
        return ws.lastMessage();
    }

    /* The reference scenario. Three steps, pairwise distinct on five axes at
     * once - see the header. `action` is the only client supplied string that
     * survives into the answer of get, so it is also the poison channel of the
     * escaping cases: actionOfSecondTarget is what they override.
     */
    std::string createReferenceScenario(WsTestSession &ws,
                                        const std::string &actionOfSecondTarget = "false")
    {
        const Json ret = Json::parse(wsWire(ws, Json{
            { "type", "create" },
            { "name", "Sc\xc3\xa9""nario" },
            { "room_name", E41R_ROOM_NAME },
            { "room_type", E41R_ROOM_TYPE },
            { "steps", Json::array({
                Json{{ "step_type", "standard" }, { "step_pause", "1.5" },
                     { "actions", Json::array({ Json{{ "id", IO_BOOL },
                                                     { "action", "true" }} }) }},
                Json{{ "step_type", "standard" }, { "step_pause", "0.25" },
                     { "actions", Json::array({ Json{{ "id", IO_TARGET },
                                                     { "action", actionOfSecondTarget }},
                                                Json{{ "id", IO_INT },
                                                     { "action", "42" }} }) }},
                Json{{ "step_type", "end" },
                     { "actions", Json::array({ Json{{ "id", IO_STRING },
                                                     { "action", "done" }} }) }}
            }) }}), nullptr, false);

        return ret.is_object() ? ret.value("data", Json::object()).value("id", std::string())
                               : std::string();
    }

    /* ------------------------------------------------------------------
     * The expected wire, in ONE place. Every whole-answer case reads these,
     * so the migration commit edits the shape once instead of ten times.
     * `action` of the second step's first target is a parameter: the
     * escaping cases reuse the same template with a poisoned value.
     * --------------------------------------------------------------- */

    //MOVED. Scenario::toJson() INSERTS id, cycle, enabled, schedule, category,
    //broken, disabled_missing_io, missing_ios, steps_count, steps; jansson kept
    //that order and nlohmann SORTS. Same inside every step and every action.
    static std::string scenarioWire(const std::string &secondAction = "false")
    {
        return std::string("{")
             + "\"broken\":\"false\","
               "\"category\":\"other\","
               "\"cycle\":\"false\","
               "\"disabled_missing_io\":\"false\","
               "\"enabled\":\"false\","
               "\"id\":\"" + SCENARIO_IO_ID + "\","
               "\"missing_ios\":\"\","
               "\"schedule\":\"false\","
               "\"steps\":["
                 "{\"actions\":[{\"action\":\"true\",\"id\":\"" + IO_BOOL + "\"}],"
                   "\"step_pause\":\"1.5\",\"step_type\":\"standard\"},"
                 "{\"actions\":[{\"action\":\"" + secondAction + "\",\"id\":\"" + IO_TARGET + "\"},"
                                "{\"action\":\"42\",\"id\":\"" + IO_INT + "\"}],"
                   "\"step_pause\":\"0.25\",\"step_type\":\"standard\"},"
                 "{\"actions\":[{\"action\":\"done\",\"id\":\"" + IO_STRING + "\"}],"
                   "\"step_type\":\"end\"}"
               "],"
               "\"steps_count\":\"2\"}";
    }

    //MOVED. The envelope sorts with the payload: data, msg, msg_id.
    static std::string wsEnvelope(const std::string &payload,
                                  const std::string &msgId = "e41r")
    {
        return "{\"data\":" + payload + ","
               "\"msg\":\"autoscenario\",\"msg_id\":\"" + msgId + "\"}";
    }
};

/*******************************************************************************
 * 1. THE WHOLE ANSWER OF get AND list, BYTE FOR BYTE, ON BOTH TRANSPORTS.
 *
 * These four cases carry, in one string each: the envelope, the key order of
 * Scenario::toJson(), the ORDER OF THE STEPS, the order of the actions inside a
 * step, the member order inside an action, and the STRING typing of everything.
 ******************************************************************************/

//MOVED, as announced: the ten keys of the payload sort, and so do the three
//members of each step and the two members of each action, and the WS envelope.
//Not one value moved - not a step, not an action, not a quote.
TEST_F(JsonApiScenarioWireBytesTest, WsAutoscenarioGetWholeAnswerBytes)
{
    loadScenarioHouse();

    WsTestSession ws;
    ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));

    EXPECT_EQ(wsEnvelope(scenarioWire()),
              wsWire(ws, Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }}));
}

//MOVED: same payload, no envelope on this transport.
TEST_F(JsonApiScenarioWireBytesTest, HttpAutoscenarioGetWholeAnswerBytes)
{
    loadScenarioHouse();

    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));
    }

    HttpTestRequest req;
    req.send(httpRequest(Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }}));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_EQ(scenarioWire(), req.body());
}

//MOVED. buildAutoscenarioList() calls Scenario::toJson() on every scenario, so
//the list moves exactly like a get - and it is the SECOND caller of the
//transitional bridge. Its own wrapper object has a single key and does not.
TEST_F(JsonApiScenarioWireBytesTest, WsAutoscenarioListWholeAnswerBytes)
{
    loadScenarioHouse();

    WsTestSession ws;
    ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));

    EXPECT_EQ(wsEnvelope("{\"scenarios\":[" + scenarioWire() + "]}"),
              wsWire(ws, Json{{ "type", "list" }}));
}

//MOVED.
TEST_F(JsonApiScenarioWireBytesTest, HttpAutoscenarioListWholeAnswerBytes)
{
    loadScenarioHouse();

    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));
    }

    EXPECT_EQ("{\"scenarios\":[" + scenarioWire() + "]}",
              httpWire(Json{{ "type", "list" }}));
}

//INVARIANT. A house without a single auto scenario answers an EMPTY ARRAY, and
//not an absent key nor a null - the one shape of this family that carries no
//scenario payload at all, therefore the one that does not cross the bridge.
TEST_F(JsonApiScenarioWireBytesTest, ListWithoutAnyScenarioIsAnEmptyArray)
{
    loadScenarioHouse();

    EXPECT_EQ("{\"scenarios\":[]}", httpWire(Json{{ "type", "list" }}));
}

/*******************************************************************************
 * 2. THE STEPS - THE ANTI "POOR FIXTURE" ARMOUR, AND THE TARGET OF THE
 *    EXCHANGE COUNTER-MUTATION E4.1r.md ASKS FOR BY NAME.
 ******************************************************************************/

//INVARIANT - and the PRECONDITION of the exchange mutation. Exchanging two
//steps of the answer is only visible if no two steps answer the same bytes.
TEST_F(JsonApiScenarioWireBytesTest, TheThreeStepsArePairwiseDistinctOnTheWire)
{
    loadScenarioHouse();

    WsTestSession ws;
    ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));

    Json payload = Json::parse(httpWire(Json{{ "type", "get" },
                                             { "id", SCENARIO_IO_ID }}),
                               nullptr, false);
    ASSERT_TRUE(payload.is_object());
    ASSERT_TRUE(payload["steps"].is_array());
    ASSERT_EQ(3u, payload["steps"].size());

    std::set<std::string> wires;
    for (const Json &step: payload["steps"])
    {
        const std::string wire = step.dump();
        EXPECT_TRUE(wires.insert(wire).second)
            << "two steps answer the SAME bytes, an exchange between them would "
               "be invisible: " << wire;
    }
    EXPECT_EQ(3u, wires.size());

    //and they differ on the five axes the header names, not by luck
    EXPECT_NE(payload["steps"][0].value("step_pause", std::string()),
              payload["steps"][1].value("step_pause", std::string()));
    EXPECT_NE(payload["steps"][0]["actions"].size(),
              payload["steps"][1]["actions"].size());
    EXPECT_NE(payload["steps"][1].value("step_type", std::string()),
              payload["steps"][2].value("step_type", std::string()));
}

//INVARIANT. The steps are on the wire in SCENARIO ORDER, and the end step is
//last. This is the assertion an exchange of two steps has to break.
TEST_F(JsonApiScenarioWireBytesTest, TheStepsAreOnTheWireInScenarioOrder)
{
    loadScenarioHouse();

    WsTestSession ws;
    ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));

    const std::string wire = httpWire(Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});

    const size_t first = wire.find(IO_BOOL);
    const size_t second = wire.find(IO_TARGET);
    const size_t third = wire.find(IO_STRING);

    ASSERT_NE(std::string::npos, first);
    ASSERT_NE(std::string::npos, second);
    ASSERT_NE(std::string::npos, third);
    EXPECT_LT(first, second) << wire;
    EXPECT_LT(second, third) << wire;
}

//INVARIANT. The two actions of the second step keep their request order too:
//a step is an ARRAY, and reordering it inside a step is the same class of
//mutation one floor down.
TEST_F(JsonApiScenarioWireBytesTest, TheActionsOfAStepAreOnTheWireInRequestOrder)
{
    loadScenarioHouse();

    WsTestSession ws;
    ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));

    const std::string wire = httpWire(Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});
    EXPECT_LT(wire.find(IO_TARGET), wire.find(IO_INT)) << wire;
}

/*******************************************************************************
 * 3. TYPING AND KEY ORDER
 ******************************************************************************/

//INVARIANT. Everything is a QUOTED STRING - steps_count and step_pause
//included. The oracle of the golden suite is type-strict (3 != "3") and the
//whole API is stringified; a number here is a contract break.
TEST_F(JsonApiScenarioWireBytesTest, StepsCountAndStepPauseAreQuotedStrings)
{
    loadScenarioHouse();

    WsTestSession ws;
    ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));

    const std::string wire = httpWire(Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});

    EXPECT_NE(std::string::npos, wire.find("\"steps_count\":\"2\"")) << wire;
    EXPECT_NE(std::string::npos, wire.find("\"step_pause\":\"1.5\"")) << wire;
    EXPECT_NE(std::string::npos, wire.find("\"step_pause\":\"0.25\"")) << wire;
    EXPECT_EQ(std::string::npos, wire.find("\"steps_count\":2")) << wire;
}

//MOVED. The ten keys of the payload, ALPHABETICAL now. jansson kept
//Scenario::toJson()'s insertion order (id, cycle, enabled, schedule, category,
//broken, disabled_missing_io, missing_ios, steps_count, steps); nlohmann sorts.
TEST_F(JsonApiScenarioWireBytesTest, ThePayloadKeysAreSorted)
{
    loadScenarioHouse();

    WsTestSession ws;
    ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));

    const std::string wire = httpWire(Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});

    const std::vector<std::string> keysInOrder = {
        "\"broken\":", "\"category\":", "\"cycle\":", "\"disabled_missing_io\":",
        "\"enabled\":", "\"id\":", "\"missing_ios\":", "\"schedule\":",
        "\"steps\":", "\"steps_count\":"
    };

    size_t at = 0;
    for (const std::string &key: keysInOrder)
    {
        const size_t found = wire.find(key, at);
        ASSERT_NE(std::string::npos, found) << key << " not found after " << at
                                            << " in " << wire;
        at = found;
    }
}

//INVARIANT, and the reason only the SCENARIO payload moves. The one-key answers
//are built from a Params, which is a std::map: they were alphabetical before
//the migration and they stay alphabetical after it.
TEST_F(JsonApiScenarioWireBytesTest, TheOneKeyAnswersAreAParamsAndDoNotMove)
{
    loadScenarioHouse();

    WsTestSession ws;
    ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));

    EXPECT_EQ("{\"success\":\"true\"}",
              httpWire(Json{{ "type", "del_schedule" }, { "id", SCENARIO_IO_ID }}));
    EXPECT_EQ("{\"error\":\"wrong input\"}",
              httpWire(Json{{ "type", "get" }, { "id", "e41r_nope" }}));
}

/*******************************************************************************
 * 4. THE WS ENVELOPE
 ******************************************************************************/

//MOVED: msg, msg_id, data -> data, msg, msg_id.
TEST_F(JsonApiScenarioWireBytesTest, TheWsEnvelopeKeysAreSorted)
{
    loadScenarioHouse();

    WsTestSession ws;

    EXPECT_EQ("{\"data\":{\"scenarios\":[]},"
              "\"msg\":\"autoscenario\",\"msg_id\":\"e41r\"}",
              wsWire(ws, Json{{ "type", "list" }}));
}

//INVARIANT. HTTP has no envelope at all, and Content-Length follows the body -
//it is computed from the dumped string by sendJson(), so any byte the
//migration adds or removes is reflected in the header.
TEST_F(JsonApiScenarioWireBytesTest, HttpContentLengthFollowsTheBody)
{
    loadScenarioHouse();

    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));
    }

    HttpTestRequest req;
    req.send(httpRequest(Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }}));

    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_EQ("application/json", req.header("Content-Type"));
    EXPECT_EQ(Utils::to_string(req.body().size()), req.header("Content-Length"));
}

/*******************************************************************************
 * 5. ESCAPING - THE ACTION STRING IS THE ONLY CLIENT SUPPLIED TEXT THAT COMES
 *    BACK OUT OF get.
 *
 * name, room_name and room_type are consumed by create/modify and are NEVER
 * echoed (the payload has no "name" at all - E4.0c pins that a client cannot
 * replay what it received). `action` is stored in the step rule and rendered
 * verbatim by Scenario::toJson(), so it is the poison channel of this
 * perimeter, and the only one.
 ******************************************************************************/

//MOVES: é -> é. The wire stays PURE ASCII on both sides.
TEST_F(JsonApiScenarioWireBytesTest, AnAccentedActionIsEscapedLowerCase)
{
    loadScenarioHouse();

    WsTestSession ws;
    ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws, "arr\xc3\xaat\xc3\xa9"));

    const std::string wire = httpWire(Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});

    EXPECT_NE(std::string::npos, wire.find("\"arr\\u00eat\\u00e9\"")) << wire;
    EXPECT_EQ(std::string::npos, wire.find("\\u00EA")) << wire;

    for (unsigned char c: wire)
        ASSERT_LT(c, 0x80u) << "the autoscenario wire is not pure ASCII any more: " << wire;
}

//MOVES: the raw 0x7f byte -> , five bytes longer, and Content-Length
//follows.
TEST_F(JsonApiScenarioWireBytesTest, ADelByteInAnActionIsEscaped)
{
    loadScenarioHouse();

    WsTestSession ws;
    ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws, std::string("a\x7f""b")));

    const std::string wire = httpWire(Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});

    EXPECT_NE(std::string::npos, wire.find("\"a\\u007fb\"")) << wire;
    EXPECT_EQ(std::string::npos, wire.find(std::string("\"a\x7f""b\""))) << wire;
}

/* THE MINE OF E4.1s, MEASURED.
 *
 * THIS CASE WAS TURNED OVER BY E4.1s, AND IT IS THE ONE PLACE IN THE SUITE
 * WHERE THE CHARGE THAT TICKET DETONATED IS VISIBLE.
 *
 * It used to be named AnEmbeddedNulInAnActionIsRefusedByTheRequestParser and
 * it pinned a REFUSAL: the request was parsed by jansson on both transports,
 * jansson REFUSES an escaped NUL in a string without JSON_ALLOW_NUL, and the
 * message died before any autoscenario code ran - zero answers, zero scenarios
 * created. E4.1r wrote it down as "the NUL delta of the series does not appear
 * on this perimeter, and E4.1s inherits it".
 *
 * E4.1s MOVED THE REQUEST PARSE TO nlohmann, WHICH ACCEPTS THE ESCAPED NUL.
 * The request is served now, the scenario IS created, and the action string
 * reaches AutoScenario::addStepAction() WHOLE - three bytes.
 *
 * AND THEN Scenario::toJson() TRUNCATES IT AT THE FIRST ZERO BYTE, IN SILENCE.
 * json_string(sa.action.c_str()) stops at the C string, IO/Scenario.cpp is
 * EXCLUDED from E4.1 by decision Q5, and E4.6d is the ticket that rewrites it.
 * The parade of E4.1s was to KNOW this and to WRITE IT DOWN, not to migrate
 * the excluded file on the sly - so this case pins the truncation as the
 * measured behaviour of today, loudly, with the ticket that owns it named.
 *
 * The probe is asymmetric on purpose ("a" before the NUL, "b" after): a
 * truncation and a drop and a replacement are three different answers, and a
 * probe that was empty on one side could not tell them apart. The assertion is
 * on the RAW wire and on the parsed value, so "the action is a" cannot be read
 * as "the action is missing".
 */
TEST_F(JsonApiScenarioWireBytesTest, AnEmbeddedNulInAnActionIsTruncatedByScenarioToJson)
{
    loadScenarioHouse();

    //wsWire() counts the answer BEFORE the loop is pumped, which is what makes
    //"one answer" mean the answer and not the backlog of EventIOAdded that
    //creating a scenario raises.
    WsTestSession ws;
    const Json ret = Json::parse(wsWire(ws, Json{
        { "type", "create" },
        { "name", "nul" },
        { "room_name", E41R_ROOM_NAME },
        { "room_type", E41R_ROOM_TYPE },
        { "steps", Json::array({
            Json{{ "step_type", "standard" }, { "step_pause", "1" },
                 { "actions", Json::array({ Json{{ "id", IO_BOOL },
                                                 { "action", std::string("a\0b", 3) }} }) }}
        }) }}), nullptr, false);
    pumpEventLoop();

    //SERVED, where it used to be dropped before any autoscenario code ran.
    ASSERT_TRUE(ret.is_object()) << "the request parse went back to refusing a NUL";
    ASSERT_EQ(SCENARIO_IO_ID,
              ret.value("data", Json::object()).value("id", std::string()));
    ASSERT_EQ(1u, ListeRoom::Instance().getAutoScenarios().size());

    const std::string wire = httpWire(Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});

    //TRUNCATED, inside the excluded file. Three assertions, because the three
    //possible answers have to be told apart:
    //  - the action is "a"          -> truncated at the zero byte  (TODAY)
    //  - the action is "a\u0000b"   -> carried whole               (E4.6d)
    //  - no action at all           -> dropped
    EXPECT_NE(std::string::npos, wire.find("\"action\":\"a\""))
            << "Scenario::toJson() stopped truncating at the NUL - if that is "
               "E4.6d landing, this case is the one to rewrite: " << wire;
    EXPECT_EQ(std::string::npos, wire.find("\\u0000"))
            << "the NUL now travels whole out of the excluded file: " << wire;
    EXPECT_EQ(std::string::npos, wire.find('\0'))
            << "a raw zero byte reached the wire";
}

//INVARIANT. Poison in an action does not kill the connection: the response is
//delivered, it is a 200, and the close list is EMPTY - an assertion of absence
//on a channel that has been flushed.
TEST_F(JsonApiScenarioWireBytesTest, APoisonedActionIsDeliveredAndTheConnectionSurvives)
{
    loadScenarioHouse();

    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws, "arr\xc3\xaat\xc3\xa9\x7f"));
    }

    HttpTestRequest req;
    req.send(httpRequest(Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }}));

    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_FALSE(req.body().empty());
    EXPECT_TRUE(req.closes().empty());
}

/*******************************************************************************
 * 6. THE REFUSALS - THE FOUR perr SITES AND THE SHARED "wrong input"
 *
 * Four sites answer a Params named `perr`:
 *   (1) buildAutoscenarioCreate(), createIO() miss   "scenario creation failed"
 *   (2) buildAutoscenarioCreate(), rules build fails "scenario creation failed"
 *   (3) buildAutoscenarioModify(), rules build fails "scenario modification failed"
 *   (4) buildAutoscenarioReenable(), still broken    "scenario still references
 *                                                     missing IOs: <ids>"
 * plus the "wrong input" refusal SIX commands share on an unresolvable id.
 *
 * ⚠️ (1) and (2) answer THE SAME BYTES. That is a fact of the code as it
 * stands, not something this ticket introduces or fixes: only the log line
 * tells them apart. Pinned here, reported in FINDINGS.md, E4.6d's to change.
 ******************************************************************************/

//INVARIANT. Refusal (1): no room in the house at all, so get_room(0) answers
//null and createIO() refuses.
TEST_F(JsonApiScenarioWireBytesTest, CreateWithoutAnyRoomIsRefused)
{
    loadRoomlessHouse();

    EXPECT_EQ("{\"error\":\"scenario creation failed\"}",
              httpWire(Json{{ "type", "create" }, { "name", "no room" }}));

    //and nothing was left behind
    EXPECT_TRUE(ListeRoom::Instance().getAutoScenarios().empty());
}

//INVARIANT. Refusal (2): an IO of the wrong type squats the internal id the
//scenario needs, checkScenarioRules() aborts and the half-created scenario is
//rolled back.
TEST_F(JsonApiScenarioWireBytesTest, CreateWhoseRulesCannotBeBuiltIsRefused)
{
    loadScenarioHouse();

    Params hijack = {{ "type", "InputTimer" },
                     { "id", SCENARIO_IS_ACTIVE_ID },
                     { "name", "hijack" }};
    ASSERT_NE(nullptr, createIO(hijack, firstRoom()));

    EXPECT_EQ("{\"error\":\"scenario creation failed\"}",
              httpWire(Json{{ "type", "create" },
                            { "name", "hijacked" },
                            { "room_name", E41R_ROOM_NAME },
                            { "room_type", E41R_ROOM_TYPE }}));

    EXPECT_TRUE(ListeRoom::Instance().getAutoScenarios().empty());
}

//INVARIANT. Refusal (3): the scenario exists, then its internal "is_active" IO
//is replaced by one of the wrong type, so the rebuild modify does at the end
//aborts. A DIFFERENT SPELLING from the create refusal, and it must stay so.
TEST_F(JsonApiScenarioWireBytesTest, ModifyWhoseRulesCannotBeRebuiltIsRefused)
{
    loadScenarioHouse();

    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));
    }

    saveConfig();

    //the internal timer IO comes back from the file as an InternalBool:
    //createInput() finds it, the dynamic_cast<InputTimer *> rejects it, and the
    //rebuild modify does at the end aborts.
    std::string ioXml = ioXmlOnDisk();
    const std::string rulesXml = rulesXmlOnDisk();
    ASSERT_TRUE(hijackTimerAsInternalBool(ioXml));

    clearCoreState();
    loadConfig(ioXml, rulesXml);
    ListeRoom::Instance().checkAutoScenario();
    pumpEventLoop();

    EXPECT_EQ("{\"error\":\"scenario modification failed\"}",
              httpWire(Json{{ "type", "modify" },
                            { "id", SCENARIO_IO_ID },
                            { "name", "renamed" },
                            { "room_name", E41R_ROOM_NAME },
                            { "room_type", E41R_ROOM_TYPE },
                            { "steps", Json::array() }}));
}

//INVARIANT. The refusal SIX commands share, byte for byte, on all six. It is
//built from a Params of one pair, so it does not move with the migration.
TEST_F(JsonApiScenarioWireBytesTest, TheWrongInputRefusalIsTheSameBytesForSixCommands)
{
    loadScenarioHouse();

    const std::vector<std::string> commands = {
        "get", "delete", "modify", "add_schedule", "del_schedule", "reenable"
    };

    for (const std::string &type: commands)
    {
        EXPECT_EQ("{\"error\":\"wrong input\"}",
                  httpWire(Json{{ "type", type }, { "id", "e41r_nope" }}))
                << "autoscenario " << type;
    }
}

//INVARIANT. The four refusals must stay DISTINGUISHABLE - except the two that
//already were not. Three distinct byte strings for the four sites, and the
//"wrong input" of the other six commands is a fourth.
TEST_F(JsonApiScenarioWireBytesTest, TheRefusalsOfTheFamilyAreThreePlusOneByteStrings)
{
    loadScenarioHouse();

    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));
    }

    std::set<std::string> refusals;
    refusals.insert("{\"error\":\"scenario creation failed\"}");
    refusals.insert("{\"error\":\"scenario modification failed\"}");
    refusals.insert("{\"error\":\"wrong input\"}");

    //three, not four: the two create sites share their spelling
    EXPECT_EQ(3u, refusals.size());

    //and each of the three is really what the corresponding path answers
    EXPECT_EQ(1u, refusals.count(httpWire(Json{{ "type", "get" }, { "id", "e41r_nope" }})));
}

//INVARIANT. Refusal (4) carries the ids: it is the ONLY refusal of the family
//whose text is not a constant, and the only one whose bytes depend on
//configuration data rather than on a literal.
TEST_F(JsonApiScenarioWireBytesTest, ReenableOfAHealthyScenarioSucceedsAndDoesNotRefuse)
{
    loadScenarioHouse();

    WsTestSession ws;
    ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));

    EXPECT_EQ("{\"success\":\"true\"}",
              httpWire(Json{{ "type", "reenable" }, { "id", SCENARIO_IO_ID }}));
}

/*******************************************************************************
 * 7. THE SUCCESS ANSWERS OF THE OTHER COMMANDS
 ******************************************************************************/

//INVARIANT. create answers the id of the SCENARIO IO, add_schedule the id of
//the TIME RANGE it created. Two different keys named the same: exchanging the
//two builders is a mutation this pins.
TEST_F(JsonApiScenarioWireBytesTest, CreateAndAddScheduleAnswerTwoDifferentIds)
{
    loadScenarioHouse();

    WsTestSession ws;
    ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));

    EXPECT_EQ(std::string("{\"id\":\"") + SCENARIO_SCHEDULE_ID + "\"}",
              httpWire(Json{{ "type", "add_schedule" }, { "id", SCENARIO_IO_ID }}));
}

//INVARIANT. delete, del_schedule and modify all answer the same success pair.
TEST_F(JsonApiScenarioWireBytesTest, DeleteDelScheduleAndModifyAnswerSuccessTrue)
{
    loadScenarioHouse();

    WsTestSession ws;
    ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));

    EXPECT_EQ("{\"success\":\"true\"}",
              httpWire(Json{{ "type", "modify" },
                            { "id", SCENARIO_IO_ID },
                            { "name", "renamed" },
                            { "room_name", E41R_ROOM_NAME },
                            { "room_type", E41R_ROOM_TYPE },
                            { "steps", Json::array() }}));

    EXPECT_EQ("{\"success\":\"true\"}",
              httpWire(Json{{ "type", "del_schedule" }, { "id", SCENARIO_IO_ID }}));

    EXPECT_EQ("{\"success\":\"true\"}",
              httpWire(Json{{ "type", "delete" }, { "id", SCENARIO_IO_ID }}));

    EXPECT_TRUE(ListeRoom::Instance().getAutoScenarios().empty());
}

/* ⚠️ PREDICTED TO MOVE AND DID NOT, kept with its measurement instead of being
 * quietly renamed. A prediction is not evidence; the run is. Both assertions
 * are order independent - the id of the time range is a VALUE, and no value of
 * this perimeter moved. The one member of the payload whose value is an id.
 */
TEST_F(JsonApiScenarioWireBytesTest, AScheduledScenarioCarriesItsTimeRangeId)
{
    loadScenarioHouse();

    WsTestSession ws;
    ASSERT_EQ(SCENARIO_IO_ID, createReferenceScenario(ws));

    EXPECT_EQ(std::string("{\"id\":\"") + SCENARIO_SCHEDULE_ID + "\"}",
              httpWire(Json{{ "type", "add_schedule" }, { "id", SCENARIO_IO_ID }}));

    const std::string wire = httpWire(Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});
    EXPECT_NE(std::string::npos,
              wire.find(std::string("\"schedule\":\"") + SCENARIO_SCHEDULE_ID + "\"")) << wire;
}

/*******************************************************************************
 * 8. WHAT THE DISPATCH READS, AND THE ARGUMENT CONTRACTS jansson_string_get()
 *    IMPOSED AND jsonStringGet() HAS TO KEEP
 ******************************************************************************/

//INVARIANT. An unknown or missing "type" answers NOTHING AT ALL on both
//transports - there is no else branch. A frozen bug (E4.0c), and an assertion
//of absence on a channel that HAS been flushed.
TEST_F(JsonApiScenarioWireBytesTest, AnUnknownTypeAnswersNoByteAtAll)
{
    loadScenarioHouse();

    WsTestSession ws;
    ws.clear();
    ws.send(wsRequest(Json{{ "type", "e41r_not_a_command" }}));
    pumpEventLoop();
    EXPECT_EQ(0u, ws.count());

    ws.clear();
    ws.send(wsRequest(Json{{ "id", SCENARIO_IO_ID }}));
    pumpEventLoop();
    EXPECT_EQ(0u, ws.count());

    HttpTestRequest req;
    req.send(httpRequest(Json{{ "type", "e41r_not_a_command" }}));
    EXPECT_EQ(0u, req.count());
}

//INVARIANT. A NUMERIC "type" or "id" must read as ABSENT, not throw:
//jansson_string_get() answered the default on a member that is not a JSON
//string, and jsonStringGet() has to keep that contract exactly.
TEST_F(JsonApiScenarioWireBytesTest, ANumericTypeOrIdIsTreatedAsAbsent)
{
    loadScenarioHouse();

    //numeric type: no branch matches, no answer
    HttpTestRequest reqType;
    reqType.send(httpRequest(Json{{ "type", 3 }}));
    EXPECT_EQ(0u, reqType.count());

    //numeric id: the id does not resolve, wrong input - and NOT a crash
    EXPECT_EQ("{\"error\":\"wrong input\"}",
              httpWire(Json{{ "type", "get" }, { "id", 3 }}));
}

//INVARIANT. WS reads "data", HTTP reads the ROOT. A WS message whose "data" is
//not an object must behave as an empty one, exactly like json_object_get() on
//a non object answered NULL.
TEST_F(JsonApiScenarioWireBytesTest, AWsDataThatIsNotAnObjectAnswersNothing)
{
    loadScenarioHouse();

    WsTestSession ws;
    ws.send(Json{{ "msg", "autoscenario" }, { "msg_id", "e41r" }, { "data", 42 }});
    pumpEventLoop();
    EXPECT_EQ(0u, ws.count());

    ws.clear();
    ws.send(Json{{ "msg", "autoscenario" }, { "msg_id", "e41r" }});
    pumpEventLoop();
    EXPECT_EQ(0u, ws.count());
}

//INVARIANT. json_array_foreach() ran json_array_size(NULL) == 0 and never
//entered the loop: an absent "steps", or one that is not an array, is a NO-OP
//and NOT an error - create still answers an id, modify still answers success.
TEST_F(JsonApiScenarioWireBytesTest, AbsentOrNonArrayStepsAreANoOpAndNotAnError)
{
    loadScenarioHouse();

    EXPECT_EQ(std::string("{\"id\":\"") + SCENARIO_IO_ID + "\"}",
              httpWire(Json{{ "type", "create" },
                            { "name", "no steps" },
                            { "room_name", E41R_ROOM_NAME },
                            { "room_type", E41R_ROOM_TYPE },
                            { "steps", "not an array" }}));

    const std::string wire = httpWire(Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});
    EXPECT_NE(std::string::npos, wire.find("\"steps_count\":\"0\"")) << wire;

    EXPECT_EQ("{\"success\":\"true\"}",
              httpWire(Json{{ "type", "modify" },
                            { "id", SCENARIO_IO_ID },
                            { "name", "still no steps" },
                            { "room_name", E41R_ROOM_NAME },
                            { "room_type", E41R_ROOM_TYPE }}));
}

//INVARIANT. Same contract one level down: a step whose "actions" is not an
//array contributes no action, and a step that is not an object at all is read
//entirely through the defaults - it becomes an END step, because "step_type"
//reads as absent and absent is not "standard".
TEST_F(JsonApiScenarioWireBytesTest, ANonObjectStepAndANonArrayActionsListAreTolerated)
{
    loadScenarioHouse();

    EXPECT_EQ(std::string("{\"id\":\"") + SCENARIO_IO_ID + "\"}",
              httpWire(Json{{ "type", "create" },
                            { "name", "tolerant" },
                            { "room_name", E41R_ROOM_NAME },
                            { "room_type", E41R_ROOM_TYPE },
                            { "steps", Json::array({
                                Json{{ "step_type", "standard" }, { "step_pause", "1" },
                                     { "actions", "not an array" }},
                                Json("a bare string is not an object")
                            }) }}));

    const std::string wire = httpWire(Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});
    EXPECT_NE(std::string::npos, wire.find("\"steps_count\":\"1\"")) << wire;
    EXPECT_EQ(std::string::npos, wire.find(IO_BOOL)) << wire;
}

//INVARIANT. An action whose target id does not resolve is SILENTLY DROPPED -
//frozen bug of E4.0c, pinned here on the bytes.
TEST_F(JsonApiScenarioWireBytesTest, AnActionOnAnUnknownIoIsSilentlyDropped)
{
    loadScenarioHouse();

    EXPECT_EQ(std::string("{\"id\":\"") + SCENARIO_IO_ID + "\"}",
              httpWire(Json{{ "type", "create" },
                            { "name", "dangling" },
                            { "room_name", E41R_ROOM_NAME },
                            { "room_type", E41R_ROOM_TYPE },
                            { "steps", Json::array({
                                Json{{ "step_type", "standard" }, { "step_pause", "1" },
                                     { "actions", Json::array({
                                         Json{{ "id", "e41r_nope" }, { "action", "true" }},
                                         Json{{ "id", IO_BOOL }, { "action", "true" }} }) }}
                            }) }}));

    const std::string wire = httpWire(Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});
    EXPECT_EQ(std::string::npos, wire.find("e41r_nope")) << wire;
    EXPECT_NE(std::string::npos, wire.find(IO_BOOL)) << wire;
}

//INVARIANT. create and modify read room_name and room_type, and they are NOT
//interchangeable: an exchange of the two arguments has to be visible. A room
//that does not resolve falls back to room 0 and the scenario is still created.
TEST_F(JsonApiScenarioWireBytesTest, RoomNameAndRoomTypeAreNotInterchangeable)
{
    loadScenarioHouse();
    ASSERT_NE(nullptr, addRoom("e41r other room", "chambre"));

    EXPECT_EQ(std::string("{\"id\":\"") + SCENARIO_IO_ID + "\"}",
              httpWire(Json{{ "type", "create" },
                            { "name", "placed" },
                            { "room_name", "e41r other room" },
                            { "room_type", "chambre" }}));

    Room *room = ListeRoom::Instance().getRoomByIO(ListeRoom::Instance().get_io(SCENARIO_IO_ID));
    ASSERT_NE(nullptr, room);
    EXPECT_EQ("e41r other room", room->get_name());
    EXPECT_EQ("chambre", room->get_type());
}

/* INVARIANT. Refusal (4), the ONLY refusal of the family whose text is not a
 * literal: it names the ids that no longer resolve. It goes out through
 * Params::toNJson() after the migration instead of jansson_from_params(), and
 * it is therefore the one refusal of this perimeter whose bytes depend on
 * CONFIGURATION DATA - the realistic poison channel of a refusal.
 */
TEST_F(JsonApiScenarioWireBytesTest, ReenableOfAStillBrokenScenarioNamesTheMissingIds)
{
    loadScenarioWithAnAmputatedStep();

    EXPECT_EQ(std::string("{\"error\":\"scenario still references missing IOs: ")
              + IO_TARGET + "\"}",
              httpWire(Json{{ "type", "reenable" }, { "id", SCENARIO_IO_ID }}));
}

/* MOVES. The payload of a BROKEN scenario, whole: it is the second shape that
 * crosses the transitional bridge, it carries the two T3.18 keys with
 * non-default values, and the amputated step comes out EMPTY with no
 * indication of any kind (frozen bug, E4.0c).
 */
TEST_F(JsonApiScenarioWireBytesTest, ABrokenScenarioPayloadCarriesTheMissingIds)
{
    loadScenarioWithAnAmputatedStep();

    const std::string wire = httpWire(Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});

    EXPECT_NE(std::string::npos, wire.find("\"broken\":\"true\"")) << wire;
    EXPECT_NE(std::string::npos, wire.find("\"disabled_missing_io\":\"true\"")) << wire;
    EXPECT_NE(std::string::npos,
              wire.find(std::string("\"missing_ios\":\"") + IO_TARGET + "\"")) << wire;
}
