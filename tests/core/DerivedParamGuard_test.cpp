/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
 **  along with Calaos; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/

/******************************************************************************
 * set_param and del_param write any key on any IO, and the params of a
 * scenario DEFINITION are not a client's to write: AutoScenarioDef derives the
 * whole set and re-emits it at every save.
 *
 * WHERE THE WRITE IS NOT ERASED, which is what makes this more than a lie in
 * the answer document. Scenario::SaveToXml() re-derives the definition params
 * from the live definition - but AutoScenarioDef::saveToParams() returns at
 * once when the definition carries no uid, so a PLAIN Scenario IO (a scenario
 * variable, a legitimate IO of the tree) writes the forged keys straight to
 * io.xml. At the next start it loads as an auto scenario it never was, and
 * with a uid the client chose: either a fresh one, and nothing anywhere
 * repairs it, or one already taken, and the whole installation now has two
 * scenarios claiming the key everything a scenario owns is derived from.
 *
 * THE EIGHT WAYS A TEST OF THIS COULD LIE, and what is done about each:
 *  1. a direct call to IOBase::set_param() or to buildJsonSetParam() would
 *     measure the model and not the API - every case here goes over a real WS
 *     session or a real HTTP request, and both are exercised;
 *  2. spelling: TheKeyIsTheOneTheApiItselfPublishes reads the key back from
 *     get_io and sends THAT string, so a guard keyed on another spelling of
 *     the same param cannot pass. get_io publishes a fixed projection and the
 *     uid is the only definition key in it, which that case asserts rather
 *     than assumes;
 *  3. a length bound: nothing here is bounded by a length, and the forged
 *     uids are of three different lengths;
 *  4. a false fixture: the loss cases assert AFTER saveConfig() +
 *     reloadFromDisk(), on what came back from the disk, never on the answer
 *     alone - and TheSameCycleStillCarriesAnOrdinaryWrite plays the identical
 *     cycle for a param that must go through, so "the definition is still
 *     absent" cannot be the answer of a cycle that reloads nothing;
 *  5. presence instead of position: answers are compared as whole documents
 *     (EXPECT_JSON_EQ), never by "does it contain the word error", and the
 *     duplicate case COUNTS the occurrences in io.xml rather than finding one;
 *  6. a haystack that excludes what is looked for: the controls write the
 *     legacy marker `auto_scenario`, a key of the shape `as_something`, the
 *     deliberately forgeable `disabled_missing_io` and an arbitrary key of the
 *     client's own - all four must still go through, so a guard that refused
 *     anything looking like a scenario param reddens this file;
 *  7. a permutation that changes no value: the refusal has to be the one the
 *     neighbouring guards already give, so TheRefusalIsTheOneTheStructural
 *     GuardAlreadyGives compares the two answers to each other;
 *  8. a sensor that measures nothing reads like a sensor that finds nothing:
 *     every refusal case is paired with a write of its own that must succeed
 *     on the same session, so a fixture where no message reaches the handler
 *     at all cannot be mistaken for a guard that holds.
 *
 * ⚠️ WHAT THIS FILE DOES NOT MEASURE, deliberately: on a scenario that IS an
 * auto scenario the forged value is re-derived by the next save, so the disk
 * can say nothing about it. The oracle there is what the API PUBLISHES between
 * the write and that save, and that is what ForgingTheUidOfALiveScenarioIs
 * RefusedBeforeAnyoneReadsIt asserts.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "ListeRoom.h"
#include "AutoScenarioDef.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char ROOM_NAME[] = "T3137 room";
const char ROOM_TYPE[] = "salon";

const char IO_BOOL[] = "t3137_bool";
const char IO_TARGET[] = "t3137_target";

//A Scenario IO carrying no definition at all - eight of these live in the
//production configuration the series measures against.
const char IO_PLAIN_SCENARIO[] = "t3137_plain_scenario";

//The uid production hands out to the first scenario of a fresh house.
const char FIRST_UID[] = "as_0";

const char REFUSED[] = R"({"error":"param refused"})";
const char ACCEPTED[] = R"({"success":"true"})";

std::string houseIosXml()
{
    return internalIoXml("InternalBool", IO_BOOL, "Bool value")
         + internalIoXml("InternalBool", IO_TARGET, "Second target");
}

size_t occurrences(const std::string &haystack, const std::string &needle)
{
    size_t n = 0;
    for (std::string::size_type at = haystack.find(needle);
         at != std::string::npos;
         at = haystack.find(needle, at + needle.size()))
        n++;
    return n;
}

} //namespace

class DerivedParamGuardTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        JsonApiCharacterizationTest::SetUp();
        AutoScenarioDef::resetIdAllocatorsForTests();

        loadConfig(ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE, houseIosXml(), 0)),
                   rulesXmlDocument(std::string()));

        //Loading raises one EventIOAdded per IO; they would otherwise be
        //delivered in the middle of a case counting messages.
        pumpEventLoop();
    }

    static Json wsRequest(const std::string &msg, Json data)
    {
        return Json{{ "msg", msg }, { "msg_id", "t3137" }, { "data", data }};
    }

    Json wsSetParam(const std::string &id, const std::string &param,
                    const std::string &value)
    {
        WsTestSession ws;
        ws.send(wsRequest("set_param", Json{{ "id", id }, { "param", param },
                                            { "value", value }}));
        EXPECT_EQ(1u, ws.count()) << "set_param answered " << ws.count()
                                  << " messages";
        if (ws.count() != 1u) return Json{{ "error", "<no answer>" }};
        return ws.lastData();
    }

    Json wsDelParam(const std::string &id, const std::string &param)
    {
        WsTestSession ws;
        ws.send(wsRequest("del_param", Json{{ "id", id }, { "param", param }}));
        EXPECT_EQ(1u, ws.count()) << "del_param answered " << ws.count()
                                  << " messages";
        if (ws.count() != 1u) return Json{{ "error", "<no answer>" }};
        return ws.lastData();
    }

    Json wsGetIo(const std::string &id)
    {
        WsTestSession ws;
        ws.send(wsRequest("get_io", Json{{ "items", Json::array({ id }) }}));
        if (ws.count() != 1u) return Json::object();
        const Json payload = ws.lastData();
        const Json::const_iterator it = payload.find(id);
        if (it == payload.cend() || !it->is_object()) return Json::object();
        return *it;
    }

    Json wsAutoscenario(Json data)
    {
        WsTestSession ws;
        ws.send(wsRequest("autoscenario", data));
        EXPECT_EQ(1u, ws.count()) << "autoscenario answered " << ws.count()
                                  << " messages";
        if (ws.count() != 1u) return Json::object();
        return ws.lastData();
    }

    //The scenario every live-scenario case works on. Answers the id of its
    //Scenario IO.
    std::string createScenario()
    {
        const Json ret = wsAutoscenario(Json{
            { "type", "create" },
            { "name", "Soir" },
            { "room_name", ROOM_NAME },
            { "room_type", ROOM_TYPE },
            { "steps", Json::array({
                Json{{ "pause", "1.5" },
                     { "actions", Json::array({ Json{{ "io", IO_BOOL }, { "value", "true" }} }) }},
                Json{{ "pause", "0" },
                     { "actions", Json::array({ Json{{ "io", IO_TARGET }, { "value", "true" }} }) }}
            }) }});
        return ret.value("id", std::string());
    }

    //A Scenario IO with no definition: what a hand written scenario variable
    //looks like, and the one case where a forged definition param reaches the
    //disk untouched.
    void createPlainScenario()
    {
        Params p = {{ "type", "Scenario" }, { "id", IO_PLAIN_SCENARIO },
                    { "name", "Plain scenario" }};
        ASSERT_TRUE(createIO(p, firstRoom()) != nullptr);
        pumpEventLoop();
    }

    /* What the installation looks like at the NEXT START. Nothing else in this
     * file is allowed to answer for it. */
    void restartFromDisk()
    {
        saveConfig();
        reloadFromDisk();
    }

    //`autoscenario get` answers "wrong input" for an IO that is not an auto
    //scenario, and a payload carrying an id for one that is. That is the API's
    //own answer to "is this thing a scenario the generator owns?".
    bool apiSeesAnAutoScenario(const std::string &id)
    {
        const Json ret = wsAutoscenario(Json{{ "type", "get" }, { "id", id }});
        return ret.contains("id");
    }
};

/*******************************************************************************
 * The loss, played to the disk and back
 ******************************************************************************/

TEST_F(DerivedParamGuardTest, AForgedUidTurnsAPlainScenarioIntoAnAutoScenario)
{
    //RED BEFORE THE FIX, and the red is what came back from the disk: a
    //scenario variable the user wrote by hand comes back owned by the
    //generator, which destroys and rewrites the rules of what it owns.
    createPlainScenario();
    ASSERT_FALSE(apiSeesAnAutoScenario(IO_PLAIN_SCENARIO));

    wsSetParam(IO_PLAIN_SCENARIO, AutoScenarioDef::KEY_UID, "as_4242");
    restartFromDisk();

    EXPECT_FALSE(apiSeesAnAutoScenario(IO_PLAIN_SCENARIO))
            << "a plain scenario came back from the disk as an auto scenario, "
               "with a uid a client chose";
}

TEST_F(DerivedParamGuardTest, TheIoXmlWrittenBackCarriesNoForgedDefinition)
{
    //RED BEFORE THE FIX. The oracle is the FILE, one step earlier than the
    //case above: a guard that answered the client and let SaveConfigIO() write
    //the forged key would lose the same scenario.
    createPlainScenario();

    wsSetParam(IO_PLAIN_SCENARIO, AutoScenarioDef::KEY_UID, "as_4242");
    wsSetParam(IO_PLAIN_SCENARIO, AutoScenarioDef::KEY_STEPS, "s7");
    saveConfig();

    const std::string xml = ioXmlOnDisk();
    EXPECT_EQ(0u, occurrences(xml, "as_4242"))
            << "the forged uid reached io.xml";
    EXPECT_EQ(0u, occurrences(xml, std::string(AutoScenarioDef::KEY_STEPS) + "=\"s7\""))
            << "the forged step list reached io.xml";
}

TEST_F(DerivedParamGuardTest, TheUidOfALiveScenarioCannotBeHandedToASecondIo)
{
    //RED BEFORE THE FIX, and this is the ticket's own defect: two IOs claiming
    //the key everything a scenario owns is derived from. Counted, not found -
    //one occurrence is the legitimate one.
    const std::string scenarioIo = createScenario();
    ASSERT_FALSE(scenarioIo.empty());
    createPlainScenario();

    //The ATTRIBUTE, not the bare value: the machinery ids are derived from the
    //uid and the legacy marker holds it too, so the value alone is in the file
    //four times on a sound configuration.
    const std::string attr = std::string(AutoScenarioDef::KEY_UID) + "=\""
                             + FIRST_UID + "\"";
    saveConfig();
    ASSERT_EQ(1u, occurrences(ioXmlOnDisk(), attr))
            << "the fixture does not carry the uid it is about to duplicate";

    wsSetParam(IO_PLAIN_SCENARIO, AutoScenarioDef::KEY_UID, FIRST_UID);
    saveConfig();

    EXPECT_EQ(1u, occurrences(ioXmlOnDisk(), attr))
            << "io.xml now carries the same scenario uid twice";
}

TEST_F(DerivedParamGuardTest, ForgingTheUidOfALiveScenarioIsRefusedBeforeAnyoneReadsIt)
{
    /* RED BEFORE THE FIX. On a scenario that IS an auto scenario the next save
     * re-derives the params, so the disk cannot be the oracle - what the API
     * PUBLISHES between the write and that save can. A client reading get_io
     * was handed a uid that names nothing while the machinery of the real one
     * kept running under the old key. */
    const std::string scenarioIo = createScenario();
    ASSERT_FALSE(scenarioIo.empty());
    ASSERT_EQ(FIRST_UID, wsGetIo(scenarioIo).value(AutoScenarioDef::KEY_UID,
                                                   std::string()));

    wsSetParam(scenarioIo, AutoScenarioDef::KEY_UID, "as_999999");

    EXPECT_EQ(FIRST_UID, wsGetIo(scenarioIo).value(AutoScenarioDef::KEY_UID,
                                                   std::string()))
            << "the API publishes a scenario uid a client wrote";
    EXPECT_TRUE(io(std::string(FIRST_UID) + "_step") != nullptr)
            << "and the machinery still answers to the old one, so the two "
               "identities have parted";
}

TEST_F(DerivedParamGuardTest, DeletingTheStepListOfALiveScenarioIsRefused)
{
    //RED BEFORE THE FIX. `autoscenario_steps` is the one authoritative key:
    //a step it does not name is not loaded, whatever `as_*` params exist.
    const std::string scenarioIo = createScenario();
    ASSERT_FALSE(scenarioIo.empty());
    //The definition lives in AutoScenarioDef until a save writes it into the
    //params: without this the key is simply absent and the case would be
    //asserting on an IO that never carried a step list.
    saveConfig();
    ASSERT_TRUE(io(scenarioIo) != nullptr);
    ASSERT_FALSE(io(scenarioIo)->get_param(AutoScenarioDef::KEY_STEPS).empty());

    EXPECT_JSON_EQ(std::string(REFUSED),
                   wsDelParam(scenarioIo, AutoScenarioDef::KEY_STEPS));
    EXPECT_FALSE(io(scenarioIo)->get_param(AutoScenarioDef::KEY_STEPS).empty())
            << "the step list is gone, and a step it does not name is not "
               "loaded whatever `as_*` params exist";

    //The same verb still deletes an ordinary param on the same IO, so an
    //unreachable handler cannot read as a guard that holds.
    EXPECT_JSON_EQ(std::string(ACCEPTED), wsSetParam(scenarioIo, "unit", "x"));
    EXPECT_JSON_EQ(std::string(ACCEPTED), wsDelParam(scenarioIo, "unit"));
}

TEST_F(DerivedParamGuardTest, TheSameCycleStillCarriesAnOrdinaryWrite)
{
    /* GREEN BEFORE AND AFTER, and it is what makes the cases above worth
     * reading: the same save+reload cycle, on a param that MUST go through. If
     * this one stopped seeing the write, "the forged key is not there" would
     * be the answer of a cycle that reloads nothing. */
    createPlainScenario();

    ASSERT_JSON_EQ(std::string(ACCEPTED),
                   wsSetParam(IO_PLAIN_SCENARIO, "unit", "kWh"));
    restartFromDisk();

    EXPECT_EQ("kWh", wsGetIo(IO_PLAIN_SCENARIO).value("unit", std::string()));
    EXPECT_NE(0u, occurrences(ioXmlOnDisk(), "kWh"))
            << "the disk cycle is not carrying writes at all";
}

/*******************************************************************************
 * The two transports
 ******************************************************************************/

TEST_F(DerivedParamGuardTest, TheWsTransportRefusesToWriteADefinitionParam)
{
    //RED BEFORE THE FIX: the server answers {"success":"true"}.
    createPlainScenario();
    EXPECT_JSON_EQ(std::string(REFUSED),
                   wsSetParam(IO_PLAIN_SCENARIO, AutoScenarioDef::KEY_UID, "as_1"));

    //The eighth way to lie: a session that reaches no handler at all answers
    //nothing, and "no answer" is not "refused".
    EXPECT_JSON_EQ(std::string(ACCEPTED),
                   wsSetParam(IO_PLAIN_SCENARIO, "io_style", "custom"));
}

TEST_F(DerivedParamGuardTest, TheHttpTransportRefusesToWriteADefinitionParam)
{
    //RED BEFORE THE FIX. HTTP carries no scope layer at all, so a guard put in
    //one handler rather than in the shared builder would leave this half open.
    createPlainScenario();
    {
        HttpTestRequest req;
        req.send(authenticated(Json{{ "action", "set_param" },
                                    { "id", IO_PLAIN_SCENARIO },
                                    { "param", AutoScenarioDef::KEY_UID },
                                    { "value", "as_1" }}));
        ASSERT_EQ(1u, req.count());
        EXPECT_JSON_EQ(std::string(REFUSED), req.body());
    }
    {
        HttpTestRequest req;
        req.send(authenticated(Json{{ "action", "set_param" },
                                    { "id", IO_PLAIN_SCENARIO },
                                    { "param", "io_style" },
                                    { "value", "custom" }}));
        ASSERT_EQ(1u, req.count());
        EXPECT_JSON_EQ(std::string(ACCEPTED), req.body());
    }
}

TEST_F(DerivedParamGuardTest, TheHttpTransportRefusesToDeleteADefinitionParam)
{
    //RED BEFORE THE FIX, del side.
    const std::string scenarioIo = createScenario();
    ASSERT_FALSE(scenarioIo.empty());

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "del_param" },
                                { "id", scenarioIo },
                                { "param", AutoScenarioDef::KEY_UID }}));
    ASSERT_EQ(1u, req.count());
    EXPECT_JSON_EQ(std::string(REFUSED), req.body());
}

/*******************************************************************************
 * The whole namespace, and the key as the API itself spells it
 ******************************************************************************/

TEST_F(DerivedParamGuardTest, EveryKeyOfTheDefinitionIsRefusedOnBothVerbs)
{
    /* RED BEFORE THE FIX, twelve times. The defect is the CLASS, not the uid:
     * a fix keyed on one name would leave the schema, the step list and the
     * per step params writable, and the step list alone costs every step. */
    const std::string scenarioIo = createScenario();
    ASSERT_FALSE(scenarioIo.empty());

    const std::vector<std::string> keys = {
        AutoScenarioDef::KEY_UID, AutoScenarioDef::KEY_SCHEMA,
        AutoScenarioDef::KEY_CYCLE, AutoScenarioDef::KEY_ENABLED,
        AutoScenarioDef::KEY_SCHEDULE, AutoScenarioDef::KEY_STEPS,
    };
    for (const std::string &key: keys)
    {
        EXPECT_JSON_EQ(std::string(REFUSED), wsSetParam(scenarioIo, key, "x"))
                << "set_param " << key;
        EXPECT_JSON_EQ(std::string(REFUSED), wsDelParam(scenarioIo, key))
                << "del_param " << key;
    }
}

TEST_F(DerivedParamGuardTest, ThePerStepParamsAreRefusedAndSoIsTheFinalStep)
{
    //RED BEFORE THE FIX. The step namespace is the other half of the
    //definition, and it is matched on the SHAPE of the key, not on a list.
    const std::string scenarioIo = createScenario();
    ASSERT_FALSE(scenarioIo.empty());

    for (const char *key: { "as_s0_pause", "as_s0_actions", "as_final_actions" })
    {
        EXPECT_JSON_EQ(std::string(REFUSED), wsSetParam(scenarioIo, key, "x"))
                << "set_param " << key;
        EXPECT_JSON_EQ(std::string(REFUSED), wsDelParam(scenarioIo, key))
                << "del_param " << key;
    }
}

TEST_F(DerivedParamGuardTest, TheKeyIsTheOneTheApiItselfPublishes)
{
    /* The second way to lie: a guard that agreed with this file on how the key
     * is spelled and with nothing else. Here the key is READ BACK from the
     * document get_io publishes and sent as it came, so the two spellings
     * cannot drift apart without reddening. */
    const std::string scenarioIo = createScenario();
    ASSERT_FALSE(scenarioIo.empty());
    saveConfig();   //see DeletingTheStepListOfALiveScenarioIsRefused

    const Json published = wsGetIo(scenarioIo);
    std::vector<std::string> definitionKeys;
    for (Json::const_iterator it = published.cbegin();it != published.cend();++it)
        if (AutoScenarioDef::isDefinitionParam(it.key()))
            definitionKeys.push_back(it.key());

    /* ONE, and the count is asserted rather than assumed: get_io publishes a
     * fixed projection (JsonApi::ioProjectionParams()) and the uid is the only
     * definition key in it. The rest of the definition is in the params and
     * simply not published, which is why the other cases read the model.
     */
    ASSERT_EQ(1u, definitionKeys.size()) << published.dump();
    ASSERT_EQ(std::string(AutoScenarioDef::KEY_UID), definitionKeys[0]);

    for (const std::string &key: definitionKeys)
        EXPECT_JSON_EQ(std::string(REFUSED), wsSetParam(scenarioIo, key, "x"))
                << "the API publishes " << key << " and accepts it back";
}

/*******************************************************************************
 * What must still go through - the half that decides whether this breaks a
 * legitimate client
 ******************************************************************************/

TEST_F(DerivedParamGuardTest, TheLegacyScenarioMarkerIsStillWritable)
{
    /* GREEN BEFORE AND AFTER. `auto_scenario` is in NEITHER owned namespace and
     * nothing may touch it: it is what keeps the orphan sweep of ListeRoom from
     * destroying the rules of an existing scenario. A guard written as "the key
     * looks like a scenario key" reddens here. */
    createPlainScenario();
    EXPECT_JSON_EQ(std::string(ACCEPTED),
                   wsSetParam(IO_PLAIN_SCENARIO, "auto_scenario", "as_0"));
    EXPECT_JSON_EQ(std::string(ACCEPTED),
                   wsSetParam(IO_PLAIN_SCENARIO, "auto_scenario_type", "3"));
    EXPECT_JSON_EQ(std::string(ACCEPTED),
                   wsDelParam(IO_PLAIN_SCENARIO, "auto_scenario"));
}

TEST_F(DerivedParamGuardTest, AKeyThatMerelyStartsWithAsIsStillWritable)
{
    /* GREEN BEFORE AND AFTER, and this is where a name list and a shape part
     * company: the step namespace is "as_<step id>_pause" / "_actions", not
     * everything starting with "as_". A loose prefix would refuse a user key
     * and there is no telling which one an installation carries. */
    createPlainScenario();
    for (const char *key: { "as_", "as_built", "as_s0_paused", "assistant",
                            "autoscenario" })
    {
        EXPECT_JSON_EQ(std::string(ACCEPTED),
                       wsSetParam(IO_PLAIN_SCENARIO, key, "x"))
                << "refused the ordinary key " << key;
    }
}

TEST_F(DerivedParamGuardTest, TheForgeableGateOfT318IsStillWritable)
{
    /* GREEN BEFORE AND AFTER. `disabled_missing_io` is DELIBERATELY forgeable -
     * the arbitration of T3.18 rests on the other gate being live and derived,
     * not on this one being unreachable. Closing it here would change an
     * arbitration this ticket has no mandate over. */
    const std::string scenarioIo = createScenario();
    ASSERT_FALSE(scenarioIo.empty());

    EXPECT_JSON_EQ(std::string(ACCEPTED),
                   wsSetParam(scenarioIo, "disabled_missing_io", "true"));
    EXPECT_JSON_EQ(std::string(ACCEPTED),
                   wsSetParam(scenarioIo, "disabled", "true"));
    EXPECT_JSON_EQ(std::string(ACCEPTED),
                   wsSetParam(scenarioIo, "cycle", "true"));
    EXPECT_JSON_EQ(std::string(ACCEPTED),
                   wsSetParam(scenarioIo, "visible", "false"));
}

TEST_F(DerivedParamGuardTest, AClientKeyOfItsOwnIsStillWritable)
{
    /* GREEN BEFORE AND AFTER. No known client writes a param through this API
     * at all, but the installer's property editor lets a user create any key
     * he likes and a Lua script chooses its own: there is no source anywhere
     * that could enumerate what is writable, which is why this guard refuses a
     * derived set rather than publishing a white list. */
    for (const char *key: { "t3137_probe", "unit", "io_style", "log_history" })
    {
        EXPECT_JSON_EQ(std::string(ACCEPTED), wsSetParam(IO_BOOL, key, "x"))
                << "refused the ordinary key " << key;
        EXPECT_JSON_EQ(std::string(ACCEPTED), wsDelParam(IO_BOOL, key))
                << "refused to delete the ordinary key " << key;
    }
}

TEST_F(DerivedParamGuardTest, TheServerStillRekeysADuplicateItselfAtLoad)
{
    /* GREEN BEFORE AND AFTER, and it is the reason the refusal is at the API
     * and not in IOBase: the repair of a duplicate uid WRITES the uid through
     * the model, and a guard down there would have frozen the one caller that
     * is allowed to. Two plain scenarios written into io.xml by hand, the way
     * an edited file or a config put brings them in. */
    const std::string ios =
        houseIosXml()
        + "    <calaos:output type=\"Scenario\" id=\"t3137_sc_a\" name=\"A\" "
          "autoscenario_uid=\"as_50\" autoscenario_steps=\"\" />\n"
          "    <calaos:output type=\"Scenario\" id=\"t3137_sc_b\" name=\"B\" "
          "autoscenario_uid=\"as_50\" autoscenario_steps=\"\" />\n";

    loadConfig(ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE, ios, 0)),
               rulesXmlDocument(std::string()));
    //The startup pass, which no core test runs on its own.
    ListeRoom::Instance().checkAutoScenario();
    pumpEventLoop();

    const std::string a = wsGetIo("t3137_sc_a").value(AutoScenarioDef::KEY_UID,
                                                      std::string());
    const std::string b = wsGetIo("t3137_sc_b").value(AutoScenarioDef::KEY_UID,
                                                      std::string());
    ASSERT_FALSE(a.empty());
    ASSERT_FALSE(b.empty());
    EXPECT_NE(a, b) << "the duplicate was not re-keyed at load";
}

/*******************************************************************************
 * The permutation that changes no value
 ******************************************************************************/

TEST_F(DerivedParamGuardTest, TheRefusalIsTheOneTheStructuralGuardAlreadyGives)
{
    /* The seventh way to lie: every literal of this file would still be
     * satisfied if the API grew a second spelling for "no". A refused
     * definition param and a refused structural param have to answer the same
     * document, because a client tells them apart by nothing else. */
    const std::string scenarioIo = createScenario();
    ASSERT_FALSE(scenarioIo.empty());

    EXPECT_JSON_EQ(wsDelParam(scenarioIo, "type").dump(),
                   wsSetParam(scenarioIo, AutoScenarioDef::KEY_UID, "as_7"));
    EXPECT_JSON_EQ(wsSetParam(scenarioIo, "id", "somethingelse").dump(),
                   wsDelParam(scenarioIo, AutoScenarioDef::KEY_STEPS));
}

TEST_F(DerivedParamGuardTest, ARefusedWriteAnnouncesNothingToTheOtherSessions)
{
    /* A refusal that still raised EventIOChanged would tell every listening
     * client that a param it cannot read back has changed - the silent no-op
     * this whole family of guards exists to remove. */
    const std::string scenarioIo = createScenario();
    ASSERT_FALSE(scenarioIo.empty());

    WsTestSession listener;
    //Creating the scenario queued its own events; they are delivered to every
    //live session on the next pump, so they have to be drained AFTER the
    //listener exists and not before.
    pumpEventLoop();
    listener.clear();

    wsSetParam(scenarioIo, AutoScenarioDef::KEY_UID, "as_31337");
    pumpEventLoop();
    EXPECT_EQ(0u, listener.count()) << listener.lastMessage();

    //And the same listener does see an accepted one, so counting zero is not
    //what this fixture answers to everything.
    wsSetParam(scenarioIo, "unit", "kWh");
    pumpEventLoop();
    EXPECT_NE(0u, listener.count());
}
