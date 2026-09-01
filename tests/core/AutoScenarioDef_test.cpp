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
 * E4.6b - the AutoScenario DEFINITION and its params codec (E4.6.md D1..D4).
 *
 * ---------------------------------------------------------------------------
 * COMMIT 1 OF 3 - CHARACTERIZATION, ZERO LINE OF src/
 * ---------------------------------------------------------------------------
 * This first version records what the tree does TODAY, before AutoScenarioDef
 * exists. Every case that pins a defect this ticket removes carries
 *
 *      >>> TO FLIP (E4.6b, Dn) <<<
 *
 * and every case that must survive the ticket carries
 *
 *      >>> PROVE, DO NOT FLIP <<<
 *
 * The guard rail of the whole epic lives here too: E4.6b must NOT re-key the
 * marker that builds an AutoScenario. Re-keying it makes
 * ListeRoom::checkAutoScenario() (ListeRoom.cpp:320-330) destroy - and persist
 * the destruction of - every rule carrying `auto_scenario`, which on
 * configs/raoulh is 18 rules (E4.6.md §5.2). That sweep belongs to E4.6c;
 * nothing in E4.6b may arm it.
 *
 * ---------------------------------------------------------------------------
 * THE FIXTURE IS RICH ON PURPOSE - DO NOT MAKE IT SMALLER
 * ---------------------------------------------------------------------------
 * "Fixture pauvre" is the most frequent defect of this series (7 recurrences,
 * all found by reviewers). A codec round trip is the sharpest possible case:
 * two values that look alike make an exchange mutation invisible. So the
 * scenario below differs on EVERY axis the codec encodes:
 *
 *   - THREE standard steps plus the final one, never one;
 *   - three DIFFERENT pauses (1.25 / 3.5 / 0.75) so swapping two steps fails;
 *   - action values that look nothing alike, and one of them carries the three
 *     characters D2 has to percent-encode: '|', '=' and '%';
 *   - one IO targeted TWICE with two DIFFERENT values, so confusing the id
 *     with the value fails;
 *   - an action in the MIDDLE of a list, never at an end, so an off-by-one
 *     cannot hide;
 *   - a final step with its own, different, actions.
 *
 * PROBES: `e46b_` prefixed ids, used nowhere else in the tree (Config's IO
 * state cache is process wide and never cleared, CalaosCoreFixture.h).
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "AutoScenario.h"
#include "CalaosConfig.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "Rule.h"
#include "Scenario.h"

#include <pugixml.hpp>

#include <string>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char ROOM_NAME_E46B[] = "E4.6b room";
const char ROOM_TYPE_E46B[] = "salon";

const char IO_LAMP[] = "e46b_lamp";        //bool, targeted TWICE, two values
const char IO_BLIND[] = "e46b_blind";      //int, "31"
const char IO_LABEL[] = "e46b_label";      //string, THE separator carrying value
const char IO_VOLUME[] = "e46b_volume";    //int, "19"
const char IO_BANNER[] = "e46b_banner";    //string, "au revoir"
const char IO_SIREN[] = "e46b_siren";      //bool, "false", final step only

//The three characters D2 has to percent-encode, in one user supplied value,
//with the '=' NOT first and the '|' NOT last: a codec that only handles the
//leading or trailing case fails here.
const char SEPARATOR_VALUE[] = "a|b=c%d|e";

//What production hands out for the FIRST scenario of a fresh house
const char SCENARIO_IO_ID[] = "io_0";
const char SCENARIO_MARKER[] = "scenario_0";

std::string houseIosXml()
{
    std::string ios;
    ios += internalIoXml("InternalBool", IO_LAMP, "Lampe");
    ios += internalIoXml("InternalInt", IO_BLIND, "Volet");
    ios += internalIoXml("InternalString", IO_LABEL, "Etiquette");
    ios += internalIoXml("InternalInt", IO_VOLUME, "Volume");
    ios += internalIoXml("InternalString", IO_BANNER, "Banniere");
    ios += internalIoXml("InternalBool", IO_SIREN, "Sirene");
    return ios;
}

} //namespace

class AutoScenarioDefTest: public JsonApiCharacterizationTest
{
protected:
    void loadDefHouse()
    {
        //NO PUMP. Config loading raises no event at all (measured in E4.6a,
        //and written down in JsonApiCharacterization.h).
        loadConfig(ioXmlDocument(roomXml(ROOM_NAME_E46B, ROOM_TYPE_E46B, houseIosXml(), 0)),
                   rulesXmlDocument(std::string()));
    }

    static Scenario *scenarioIo(const std::string &id = SCENARIO_IO_ID)
    {
        return dynamic_cast<Scenario *>(ListeRoom::Instance().get_io(id));
    }

    static Json wsRequest(const std::string &msg, Json data,
                          const std::string &msgId = "e46b")
    {
        return Json{{ "msg", msg }, { "msg_id", msgId }, { "data", data }};
    }

    Json wsAutoscenario(WsTestSession &ws, Json data)
    {
        ws.clear();
        ws.send(wsRequest("autoscenario", data));
        EXPECT_EQ(1u, ws.count()) << "autoscenario " << data.value("type", std::string())
                                  << " answered " << ws.count() << " messages";
        if (ws.count() != 1) return Json::object();
        return ws.lastData();
    }

    /* THE RICH SCENARIO. Read the header before touching it: every asymmetry
     * is load bearing. Answers the id of the Scenario IO, "io_0" on a fresh
     * house.
     */
    std::string createRichScenario(WsTestSession &ws)
    {
        const Json ret = wsAutoscenario(ws, Json{
            { "type", "create" },
            { "name", "Coucher" },
            { "room_name", ROOM_NAME_E46B },
            { "room_type", ROOM_TYPE_E46B },
            { "steps", Json::array({
                Json{{ "step_type", "standard" }, { "step_pause", "1.25" },
                     { "actions", Json::array({
                         Json{{ "id", IO_LAMP }, { "action", "true" }} }) }},
                Json{{ "step_type", "standard" }, { "step_pause", "3.5" },
                     { "actions", Json::array({
                         Json{{ "id", IO_BLIND }, { "action", "31" }},
                         Json{{ "id", IO_LABEL }, { "action", SEPARATOR_VALUE }},
                         Json{{ "id", IO_VOLUME }, { "action", "19" }} }) }},
                Json{{ "step_type", "standard" }, { "step_pause", "0.75" },
                     { "actions", Json::array({
                         Json{{ "id", IO_BANNER }, { "action", "au revoir" }} }) }},
                Json{{ "step_type", "end" },
                     { "actions", Json::array({
                         Json{{ "id", IO_SIREN }, { "action", "false" }},
                         Json{{ "id", IO_LAMP }, { "action", "false" }} }) }}
            }) }});

        return ret.value("id", std::string());
    }

    //Create the rich scenario on a fresh house, save, reload from disk, and run
    //the startup pass the server runs (which ends with SaveConfigIO()).
    void loadRichScenarioFromDisk()
    {
        loadDefHouse();
        {
            WsTestSession ws;
            EXPECT_EQ(SCENARIO_IO_ID, createRichScenario(ws));
        }
        saveConfig();

        const std::string ioXml = ioXmlOnDisk();
        const std::string rulesXml = rulesXmlOnDisk();

        clearCoreState();
        loadConfig(ioXml, rulesXml);
        ListeRoom::Instance().checkAutoScenario();
    }

    /* ---------------------------------------------------------------------
     * XML inspection. pugixml rather than string search: attribute order is
     * an implementation detail of the writer.
     * ------------------------------------------------------------------ */

    static std::vector<pugi::xml_node> ioNodes(pugi::xml_document &doc)
    {
        std::vector<pugi::xml_node> out;
        for (pugi::xml_node room: doc.child("calaos:ioconfig").child("calaos:home"))
            for (pugi::xml_node io: room)
                out.push_back(io);
        return out;
    }

    //Every attribute of the scenario IO element of an io.xml document, as a
    //key -> value map, straight off the FILE and not off the live object.
    static std::map<std::string, std::string> scenarioIoAttributes(const std::string &xml)
    {
        std::map<std::string, std::string> out;
        pugi::xml_document doc;
        if (!doc.load_buffer(xml.data(), xml.size())) return out;

        for (pugi::xml_node io: ioNodes(doc))
        {
            if (std::string(io.attribute("type").value()) != "scenario") continue;
            for (pugi::xml_attribute a: io.attributes())
                out[a.name()] = a.value();
            return out;
        }
        return out;
    }

    //Keys of the definition namespaces D2 reserves: `autoscenario_*` and
    //`as_*`. NOTE the legacy marker `auto_scenario` is in NEITHER of them -
    //that separation is what keeps the orphan sweep of E4.6c disarmed.
    static std::vector<std::string> definitionKeysOf(const std::string &xml)
    {
        std::vector<std::string> out;
        for (const auto &kv: scenarioIoAttributes(xml))
        {
            if (kv.first.compare(0, 13, "autoscenario_") == 0 ||
                kv.first.compare(0, 3, "as_") == 0)
                out.push_back(kv.first);
        }
        return out;
    }

    static bool addParamToScenarioIoInXml(std::string &xml,
                                          const std::string &key,
                                          const std::string &value)
    {
        pugi::xml_document doc;
        if (!doc.load_buffer(xml.data(), xml.size())) return false;

        for (pugi::xml_node io: ioNodes(doc))
        {
            if (std::string(io.attribute("type").value()) != "scenario") continue;
            io.append_attribute(key.c_str()).set_value(value.c_str());
            std::ostringstream ss;
            doc.save(ss);
            xml = ss.str();
            return true;
        }
        return false;
    }
};

/*******************************************************************************
 * SECTION 1 - THE GUARD RAIL. It has to be first, and it never flips.
 ******************************************************************************/

TEST_F(AutoScenarioDefTest, TheMarkerThatBuildsAnAutoScenarioIsStillAutoScenario)
{
    /* >>> PROVE, DO NOT FLIP <<<
     *
     * IO/Scenario.cpp:48 tests get_param("auto_scenario") != "". THAT test is
     * what makes ListeRoom::checkAutoScenario() adopt the rules of the
     * scenario, and an adopted rule is one the orphan sweep of
     * ListeRoom.cpp:324 does NOT destroy.
     *
     * E4.6b introduces `autoscenario_uid` NEXT TO the legacy marker; it must
     * not replace it. This case is the tripwire: if a future E4.6b commit
     * re-keys the constructor, the second half goes red here BEFORE the 18
     * rules of configs/raoulh are destroyed in production.
     */
    loadRichScenarioFromDisk();

    Scenario *sc = scenarioIo();
    ASSERT_TRUE(sc != nullptr);
    EXPECT_EQ(SCENARIO_MARKER, sc->get_param("auto_scenario"));
    ASSERT_TRUE(sc->getAutoScenario() != nullptr)
            << "the legacy marker no longer builds an AutoScenario - the orphan "
               "sweep of ListeRoom.cpp:324 is now armed on every marked rule";

    //and every rule of the scenario is adopted, so the sweep skips them all
    int marked = 0, adopted = 0;
    for (int i = 0;i < ListeRule::Instance().size();i++)
    {
        Rule *r = ListeRule::Instance().get_rule(i);
        if (!r || !r->param_exists("auto_scenario")) continue;
        marked++;
        if (r->isAutoScenario()) adopted++;
    }
    EXPECT_GT(marked, 0);
    EXPECT_EQ(marked, adopted)
            << marked - adopted << " marked rule(s) are no longer adopted: "
               "ListeRoom::checkAutoScenario() would destroy them and persist it";
}

/*******************************************************************************
 * SECTION 2 - WHERE THE DEFINITION LIVES TODAY: NOT IN io.xml
 ******************************************************************************/

TEST_F(AutoScenarioDefTest, TodayTheScenarioIoCarriesNoDefinitionParamAtAll)
{
    /* >>> TO FLIP (E4.6b, D2) <<<
     *
     * Today io.xml carries the MARKER and nothing else of the model: no uid,
     * no step list, no pause, no action. E4.6.md §2.1. After E4.6b the two
     * namespaces D2 reserves - `autoscenario_*` and `as_*` - are populated,
     * and this case flips.
     */
    loadRichScenarioFromDisk();

    const std::string xml = ioXmlOnDisk();
    EXPECT_TRUE(definitionKeysOf(xml).empty())
            << "io.xml already carries definition params";

    Scenario *sc = scenarioIo();
    ASSERT_TRUE(sc != nullptr);
    EXPECT_FALSE(sc->param_exists("autoscenario_uid"));
    EXPECT_FALSE(sc->param_exists("autoscenario_steps"));
}

TEST_F(AutoScenarioDefTest, TodayThePausesAndTheActionValuesLiveOnlyInRulesXml)
{
    /* >>> TO FLIP (E4.6b, D2) <<<
     *
     * The needles are the ones the fixture makes unique: the three pauses, the
     * separator carrying value, and the banner. They are in rules.xml and
     * nowhere else - the definition is INFERRED from the rules (RC1).
     */
    loadRichScenarioFromDisk();

    const std::string ioXml = ioXmlOnDisk();
    const std::string rulesXml = rulesXmlOnDisk();

    for (const char *needle: { "1.25", "3.5", "0.75", "au revoir", SEPARATOR_VALUE })
    {
        EXPECT_EQ(std::string::npos, ioXml.find(needle))
                << "io.xml already carries " << needle << ", the model moved";
        EXPECT_NE(std::string::npos, rulesXml.find(needle))
                << "rules.xml lost " << needle;
    }
}

TEST_F(AutoScenarioDefTest, TodayNothingOwnsTheAsNamespaceSoAnOrphanParamSurvivesForEver)
{
    /* >>> TO FLIP (E4.6b, D2) <<<
     *
     * `as_s9_actions` names a step that does not exist. Today nobody reads the
     * namespace, so the param survives a full save/reload/save cycle verbatim
     * (which is exactly the property the io.xml decision rests on, E4.6a's
     * AnUnknownParamOfTheScenarioIoSurvivesASaveReloadCycle).
     * After E4.6b `autoscenario_steps` is AUTHORITATIVE: an `as_*` param it
     * does not name is ignored at load and REMOVED at save.
     */
    loadRichScenarioFromDisk();

    std::string ioXml = ioXmlOnDisk();
    const std::string rulesXml = rulesXmlOnDisk();
    ASSERT_TRUE(addParamToScenarioIoInXml(ioXml, "as_s9_actions", "e46b_ghost=boo"));

    clearCoreState();
    loadConfig(ioXml, rulesXml);
    ListeRoom::Instance().checkAutoScenario();
    saveConfig();

    EXPECT_NE(std::string::npos, ioXmlOnDisk().find("as_s9_actions"))
            << "somebody now owns the as_ namespace";
    Scenario *sc = scenarioIo();
    ASSERT_TRUE(sc != nullptr);
    EXPECT_EQ("e46b_ghost=boo", sc->get_param("as_s9_actions"));
}

TEST_F(AutoScenarioDefTest, TodayAnActionValueCarryingTheSeparatorsIsStoredRawInRulesXml)
{
    /* >>> PROVE, DO NOT FLIP <<<
     *
     * The premise of the percent-encoding of D2: an action value is arbitrary
     * user text and it really can carry '|', '=' and '%'. rules.xml stores it
     * raw in an XML attribute (pugixml escapes what XML needs and nothing
     * else), and it comes back byte for byte through the rule.
     * E4.6b must keep that true END TO END: what the codec encodes on the way
     * into io.xml it decodes on the way out, so this observation is unchanged.
     */
    loadRichScenarioFromDisk();

    Scenario *sc = scenarioIo();
    ASSERT_TRUE(sc != nullptr);
    AutoScenario *as = sc->getAutoScenario();
    ASSERT_TRUE(as != nullptr);

    //step 2, action 2 (the middle one of three) is the separator carrying one
    ASSERT_EQ(3, as->getStepActionCount(1));
    const ScenarioAction sa = as->getStepAction(1, 1);
    ASSERT_TRUE(sa.io != nullptr);
    EXPECT_EQ(IO_LABEL, sa.io->get_param("id"));
    EXPECT_EQ(SEPARATOR_VALUE, sa.action);

    //and the raw bytes really are in the file
    EXPECT_NE(std::string::npos, rulesXmlOnDisk().find(SEPARATOR_VALUE));
}
