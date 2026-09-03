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
 * AutoScenario as a GENERATOR: the rules are destroyed and rebuilt from the
 * definition, never read to learn what the scenario is.
 *
 * Four case names below say the OPPOSITE of what they now assert: they were
 * written against the model this replaces and are kept under their old names so
 * that the flip is nominal. Each carries a "FLIPPED" note.
 *
 * THE FIXTURE IS RICH ON PURPOSE. Three steps and a final one; three different
 * pauses; six action values that look nothing alike; one IO targeted twice with
 * two different values; the IO that vanishes sits in the MIDDLE of the MIDDLE
 * step; cycle and enabled DISAGREE; and the scenario is scheduled, so the two
 * schedule rules exist. Make it poorer and a swapped field stops being visible.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "ActionStd.h"
#include "AutoScenario.h"
#include "AutoScenarioDef.h"
#include "ConditionStd.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "Rule.h"
#include "Scenario.h"

#include <pugixml.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char ROOM_NAME_E46C[] = "E4.6c room";
const char ROOM_TYPE_E46C[] = "salon";

//Ids are e46c_ prefixed: the IO state cache of Config is process wide and never
//cleared, so sharing an id with another binary leaks state between them.
const char IO_LAMP[] = "e46c_lamp";      //bool,   targeted TWICE, two values
const char IO_BLIND[] = "e46c_blind";    //int,    "64"
const char IO_GHOST[] = "e46c_ghost";    //string, VANISHES, middle of step 2
const char IO_VOLUME[] = "e46c_volume";  //int,    "23"
const char IO_BANNER[] = "e46c_banner";  //string, "au lit"
const char IO_SIREN[] = "e46c_siren";    //bool,   "false" in the final step

const char SCENARIO_IO_ID[] = "io_0";
const char SCENARIO_MARKER[] = "scenario_0";

std::string houseIosXml()
{
    std::string ios;
    ios += internalIoXml("InternalBool", IO_LAMP, "Lampe");
    ios += internalIoXml("InternalInt", IO_BLIND, "Volet");
    ios += internalIoXml("InternalString", IO_GHOST, "Fantome");
    ios += internalIoXml("InternalInt", IO_VOLUME, "Volume");
    ios += internalIoXml("InternalString", IO_BANNER, "Banniere");
    ios += internalIoXml("InternalBool", IO_SIREN, "Sirene");
    return ios;
}

} //namespace

class AutoScenarioRulesTest: public JsonApiCharacterizationTest
{
protected:
    void loadRulesHouse()
    {
        loadConfig(ioXmlDocument(roomXml(ROOM_NAME_E46C, ROOM_TYPE_E46C, houseIosXml(), 0)),
                   rulesXmlDocument(std::string()));
    }

    static Scenario *scenarioIo(const std::string &id = SCENARIO_IO_ID)
    {
        return dynamic_cast<Scenario *>(ListeRoom::Instance().get_io(id));
    }

    static AutoScenario *autoScenario(const std::string &id = SCENARIO_IO_ID)
    {
        Scenario *sc = scenarioIo(id);
        return sc? sc->getAutoScenario(): nullptr;
    }

    Json wsAutoscenario(WsTestSession &ws, Json data)
    {
        ws.clear();
        ws.send(Json{{ "msg", "autoscenario" }, { "msg_id", "e46c" }, { "data", data }});
        EXPECT_EQ(1u, ws.count()) << "autoscenario " << data.value("type", std::string())
                                  << " answered " << ws.count() << " messages";
        if (ws.count() != 1) return Json::object();
        return ws.lastData();
    }

    /* THE RICH SCENARIO. cycle "true" is explicit and `disabled` defaults to
     * "true" in create, so cycle and enabled come out DISAGREEING - without
     * that divergence, swapping the two is invisible.
     */
    std::string createRichScenario(WsTestSession &ws)
    {
        const Json ret = wsAutoscenario(ws, Json{
            { "type", "create" },
            { "name", "Coucher" },
            { "room_name", ROOM_NAME_E46C },
            { "room_type", ROOM_TYPE_E46C },
            { "cycle", "true" },
            { "steps", Json::array({
                Json{{ "pause", "1.25" },
                     { "actions", Json::array({
                         Json{{ "io", IO_LAMP }, { "value", "true" }} }) }},
                Json{{ "pause", "3.5" },
                     { "actions", Json::array({
                         Json{{ "io", IO_BLIND }, { "value", "64" }},
                         Json{{ "io", IO_GHOST }, { "value", "bonne nuit" }},
                         Json{{ "io", IO_VOLUME }, { "value", "23" }} }) }},
                Json{{ "pause", "0.75" },
                     { "actions", Json::array({
                         Json{{ "io", IO_BANNER }, { "value", "au lit" }} }) }}
            }) },
            { "final_step", Json{{ "actions", Json::array({
                         Json{{ "io", IO_SIREN }, { "value", "false" }},
                         Json{{ "io", IO_LAMP }, { "value", "false" }} }) }} }});

        return ret.value("id", std::string());
    }

    //The rich scenario, scheduled, saved and reloaded, with the startup pass run
    //on it - the state the server is in on any ordinary boot.
    void loadScheduledScenarioFromDisk()
    {
        loadRulesHouse();
        {
            WsTestSession ws;
            ASSERT_EQ(SCENARIO_IO_ID, createRichScenario(ws));
            wsAutoscenario(ws, Json{{ "type", "add_schedule" }, { "id", SCENARIO_IO_ID }});
        }
        saveConfig();

        const std::string ioXml = ioXmlOnDisk();
        const std::string rulesXml = rulesXmlOnDisk();

        clearCoreState();
        loadConfig(ioXml, rulesXml);
        ListeRoom::Instance().checkAutoScenario();
    }

    /* ---------------------------------------------------------------------
     * Rule inspection. The comparisons are on the SERIALIZED rules, because
     * pointer identity says nothing about content and content is the contract.
     * ------------------------------------------------------------------ */

    //Every rule of the process, serialized, in ListeRule order.
    static std::string serializeAllRules()
    {
        pugi::xml_document doc;
        pugi::xml_node root = doc.append_child("calaos:rules");

        for (int i = 0;i < ListeRule::Instance().size();i++)
        {
            Rule *r = ListeRule::Instance().get_rule(i);
            if (!r) continue;
            pugi::xml_node n = root.append_child("calaos:rule");
            r->SaveToXml(n);
        }

        std::ostringstream ss;
        doc.save(ss);
        return ss.str();
    }

    static std::vector<std::string> ruleShapes()
    {
        std::vector<std::string> out;
        for (int i = 0;i < ListeRule::Instance().size();i++)
        {
            Rule *r = ListeRule::Instance().get_rule(i);
            if (!r) continue;
            out.push_back(r->get_name() + "/" + r->get_param("auto_scenario_type") +
                          "/" + r->get_param(AutoScenarioDef::KEY_UID) +
                          (r->param_exists("auto_scenario_step")?
                               "#" + r->get_param("auto_scenario_step"): std::string()));
        }
        return out;
    }

    static std::vector<Rule *> allRules()
    {
        std::vector<Rule *> out;
        for (int i = 0;i < ListeRule::Instance().size();i++)
            out.push_back(ListeRule::Instance().get_rule(i));
        return out;
    }

    static std::string conditionValueOn(Rule *rule, const std::string &ioId)
    {
        if (!rule) return {};
        for (int i = 0;i < rule->get_size_conds();i++)
        {
            ConditionStd *cond = dynamic_cast<ConditionStd *>(rule->get_condition(i));
            if (!cond || cond->get_size() != 1) continue;
            if (cond->get_input_id(0) != ioId) continue;
            return cond->get_params().get_param(ioId);
        }
        return {};
    }

    static std::string actionValueOn(Rule *rule, const std::string &ioId)
    {
        if (!rule) return {};
        for (int i = 0;i < rule->get_size_actions();i++)
        {
            ActionStd *act = dynamic_cast<ActionStd *>(rule->get_action(i));
            if (!act || act->get_size() != 1) continue;
            if (act->get_output_id(0) != ioId) continue;
            return act->get_params().get_param(ioId);
        }
        return {};
    }

    static bool ruleTargets(Rule *rule, const std::string &ioId)
    {
        if (!rule) return false;
        for (int i = 0;i < rule->get_size_actions();i++)
        {
            ActionStd *act = dynamic_cast<ActionStd *>(rule->get_action(i));
            if (!act || act->get_size() != 1) continue;
            if (act->get_output_id(0) == ioId) return true;
        }
        return false;
    }

    /* ---------------------------------------------------------------------
     * XML surgery, with pugixml: attribute order is the writer's business.
     * ------------------------------------------------------------------ */

    static std::string serialize(const pugi::xml_document &doc)
    {
        std::ostringstream ss;
        doc.save(ss);
        return ss.str();
    }

    static std::vector<pugi::xml_node> ruleNodes(pugi::xml_document &doc)
    {
        std::vector<pugi::xml_node> out;
        for (pugi::xml_node r: doc.child("calaos:rules"))
            out.push_back(r);
        return out;
    }

    //Drop one attribute from every rule element. Answers how many were dropped.
    static int stripRuleAttribute(std::string &xml, const std::string &key)
    {
        pugi::xml_document doc;
        if (!doc.load_buffer(xml.data(), xml.size())) return 0;

        int n = 0;
        for (pugi::xml_node rule: ruleNodes(doc))
            if (rule.remove_attribute(key.c_str())) n++;

        if (n) xml = serialize(doc);
        return n;
    }

    static bool setRuleAttribute(std::string &xml, const std::string &ruleName,
                                 const std::string &key, const std::string &value)
    {
        pugi::xml_document doc;
        if (!doc.load_buffer(xml.data(), xml.size())) return false;

        for (pugi::xml_node rule: ruleNodes(doc))
        {
            if (std::string(rule.attribute("name").value()) != ruleName) continue;
            pugi::xml_attribute a = rule.attribute(key.c_str());
            if (!a) a = rule.append_attribute(key.c_str());
            a.set_value(value.c_str());
            xml = serialize(doc);
            return true;
        }
        return false;
    }

    //Drop every param of the two definition namespaces from the Scenario IO.
    static int stripDefinitionParams(std::string &xml)
    {
        pugi::xml_document doc;
        if (!doc.load_buffer(xml.data(), xml.size())) return 0;

        int n = 0;
        for (pugi::xml_node room: doc.child("calaos:ioconfig").child("calaos:home"))
            for (pugi::xml_node io: room)
            {
                std::vector<std::string> doomed;
                for (pugi::xml_attribute a: io.attributes())
                    if (AutoScenarioDef::isDefinitionParam(a.name()))
                        doomed.push_back(a.name());
                for (const std::string &k: doomed)
                    if (io.remove_attribute(k.c_str())) n++;
            }

        if (n) xml = serialize(doc);
        return n;
    }

    static bool removeIoFromXml(std::string &xml, const std::string &id)
    {
        pugi::xml_document doc;
        if (!doc.load_buffer(xml.data(), xml.size())) return false;

        for (pugi::xml_node room: doc.child("calaos:ioconfig").child("calaos:home"))
            for (pugi::xml_node io: room)
            {
                if (io.attribute("id").value() != id) continue;
                room.remove_child(io);
                xml = serialize(doc);
                return true;
            }
        return false;
    }
};

/*******************************************************************************
 * SECTION 1 - IDEMPOTENCE
 ******************************************************************************/

TEST_F(AutoScenarioRulesTest, TwoConsecutiveRebuildsProduceTheSameRulesByteForByte)
{
    /* EXCHANGE, and it is what makes "identical" mean something: the same
     * comparison is run again after ONE pause has changed, and it must come out
     * different. A rebuild that produced nothing at all would pass the first
     * half and fail the second.
     */
    loadScheduledScenarioFromDisk();

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    //3 steps + button_start + button_stop + step_end + time_start + time_stop
    ASSERT_EQ(8, ListeRule::Instance().size());

    const std::string first = serializeAllRules();
    const std::vector<std::string> firstShapes = ruleShapes();

    ASSERT_TRUE(as->rebuildRules());
    EXPECT_EQ(8, ListeRule::Instance().size()) << "the rebuild left duplicates behind";
    EXPECT_EQ(firstShapes, ruleShapes());
    EXPECT_EQ(first, serializeAllRules()) << "two rebuilds do not agree";

    ASSERT_TRUE(as->rebuildRules());
    EXPECT_EQ(8, ListeRule::Instance().size());
    EXPECT_EQ(first, serializeAllRules()) << "the third rebuild drifted";

    //...and the comparison is not vacuous
    as->setStepPause(1, 9.5);
    EXPECT_NE(first, serializeAllRules()) << "a changed pause did not reach the rules";
    EXPECT_EQ(8, ListeRule::Instance().size());
}

TEST_F(AutoScenarioRulesTest, TheStartupPassIsIdempotentAcrossARebootAndKeepsRulesXmlStable)
{
    loadScheduledScenarioFromDisk();

    saveConfig();
    const std::string rulesFirst = rulesXmlOnDisk();
    const std::string ioXml = ioXmlOnDisk();

    clearCoreState();
    loadConfig(ioXml, rulesFirst);
    ListeRoom::Instance().checkAutoScenario();
    saveConfig();

    EXPECT_EQ(rulesFirst, rulesXmlOnDisk())
            << "a reboot that changed nothing rewrote rules.xml";
    EXPECT_EQ(8, ListeRule::Instance().size());

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    EXPECT_EQ(3u, as->getRuleSteps().size());
    EXPECT_FALSE(as->isBroken());
}

TEST_F(AutoScenarioRulesTest, ARebuildRecognizesTheExistingRulesAndKeepsThem)
{
    /* FLIPPED: nothing is recognized and nothing survives. Every rule is
     * destroyed and regenerated, so the file cannot decide what the scenario is.
     *
     * Liveness tokens, not pointers: the allocator hands the same addresses
     * straight back, so a raw comparison cannot tell a survivor from a fresh
     * object at the same address.
     */
    loadScheduledScenarioFromDisk();

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);

    std::vector<std::weak_ptr<bool>> before;
    for (Rule *r: allRules()) before.push_back(r->aliveToken());
    ASSERT_EQ(8u, before.size());

    ASSERT_TRUE(as->rebuildRules());

    for (const std::weak_ptr<bool> &t: before)
        EXPECT_TRUE(t.expired()) << "a rule survived the rebuild";

    const std::vector<Rule *> after = allRules();
    ASSERT_EQ(8u, after.size());

    std::vector<std::string> names;
    for (Rule *r: after) names.push_back(r->get_name());

    const std::vector<std::string> expected = {
        std::string(SCENARIO_MARKER) + "_button_start",
        std::string(SCENARIO_MARKER) + "_button_stop",
        std::string(SCENARIO_MARKER) + "_step_end",
        std::string(SCENARIO_MARKER) + "_step",
        std::string(SCENARIO_MARKER) + "_step",
        std::string(SCENARIO_MARKER) + "_step",
        std::string(SCENARIO_MARKER) + "_time_start",
        std::string(SCENARIO_MARKER) + "_time_stop" };
    EXPECT_EQ(expected, names) << "the order is the file's, not the generator's";
}

TEST_F(AutoScenarioRulesTest, TheDefinitionOverwritesWhateverTheRuleFileSaid)
{
    /* Two independent edits, on two different rules and two different fields,
     * so a rebuild that healed only conditions or only actions fails here.
     */
    loadRulesHouse();
    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createRichScenario(ws));
    }
    saveConfig();

    const std::string ioXml = ioXmlOnDisk();
    std::string rulesXml = rulesXmlOnDisk();

    const std::string stepIoId = std::string(SCENARIO_MARKER) + "_step";
    const std::string timerIoId = std::string(SCENARIO_MARKER) + "_timer";

    //Anything is legal in the file: the rule is not read, it is overwritten.
    ASSERT_TRUE(setRuleAttribute(rulesXml, std::string(SCENARIO_MARKER) + "_button_start",
                                 "auto_scenario_type", "button_stop"));

    clearCoreState();
    loadConfig(ioXml, rulesXml);
    ASSERT_EQ(6, ListeRule::Instance().size());
    ListeRoom::Instance().checkAutoScenario();

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    ASSERT_EQ(6, ListeRule::Instance().size()) << "the mislabelled rule was duplicated";

    ASSERT_TRUE(as->getRuleStart() != nullptr);
    EXPECT_EQ("0", actionValueOn(as->getRuleStart(), stepIoId));

    ASSERT_EQ(3u, as->getRuleSteps().size());
    EXPECT_EQ("3.5", actionValueOn(as->getRuleSteps()[1], timerIoId));
    EXPECT_EQ("1", conditionValueOn(as->getRuleSteps()[1], stepIoId));
}

TEST_F(AutoScenarioRulesTest, AStepNumberIsWrittenOnceAndItsConditionAgreesWithIt)
{
    /* The step number used to be four things at once and they could drift
     * apart. They are written together now, from the same source.
     */
    loadScheduledScenarioFromDisk();

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);

    const std::string stepIoId = std::string(SCENARIO_MARKER) + "_step";
    const std::vector<Rule *> steps = as->getRuleSteps();
    ASSERT_EQ(3u, steps.size());

    const char *pauses[] = { "1.25", "3.5", "0.75" };
    const std::string timerIoId = std::string(SCENARIO_MARKER) + "_timer";

    for (int i = 0;i < 3;i++)
    {
        EXPECT_EQ(std::to_string(i), steps[i]->get_param("auto_scenario_step")) << i;
        EXPECT_EQ(std::to_string(i), conditionValueOn(steps[i], stepIoId)) << i;
        EXPECT_EQ(pauses[i], actionValueOn(steps[i], timerIoId)) << i;
    }

    //A cycling scenario loops back to 0 instead of handing over to step_end.
    EXPECT_TRUE(as->isCycling());
    EXPECT_EQ("0", actionValueOn(steps[2], stepIoId));
    EXPECT_EQ("1", actionValueOn(steps[0], stepIoId));

    as->setCycling(false);
    ASSERT_TRUE(as->rebuildRules());
    EXPECT_EQ("-1", actionValueOn(as->getRuleSteps()[2], stepIoId));
    EXPECT_EQ(nullptr, as->getRulePlageStop()) << "the cycle rule outlived the cycle flag";
    EXPECT_TRUE(as->getRulePlageStart() != nullptr);
}

/*******************************************************************************
 * SECTION 2 - WHAT MAY BE DESTROYED, AND WHAT MAY NOT
 ******************************************************************************/

TEST_F(AutoScenarioRulesTest, TheOrphanSweepDestroysAnyMarkedRuleNoScenarioAdopted)
{
    /* FLIPPED: the sweep is gone. A rule carrying no generated uid is somebody
     * else's data, whatever it claims about itself.
     *
     * EXCHANGE: two rules identical but for the value of `auto_scenario` - one
     * claiming this very scenario, one claiming another. Both survive.
     */
    loadRulesHouse();
    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createRichScenario(ws));
    }
    saveConfig();

    const std::string ioXml = ioXmlOnDisk();
    std::string rulesXml = rulesXmlOnDisk();

    std::string extra = simpleRuleXml("e46c_claims_the_scenario", IO_LAMP, "==", "true",
                                      IO_SIREN, "true");
    extra += simpleRuleXml("e46c_claims_another", IO_LAMP, "==", "true", IO_SIREN, "false");
    rulesXml.insert(rulesXml.rfind("</calaos:rules>"), extra);
    ASSERT_TRUE(setRuleAttribute(rulesXml, "e46c_claims_the_scenario",
                                 "auto_scenario", SCENARIO_MARKER));
    ASSERT_TRUE(setRuleAttribute(rulesXml, "e46c_claims_another",
                                 "auto_scenario", "scenario_99"));

    clearCoreState();
    loadConfig(ioXml, rulesXml);
    ASSERT_EQ(8, ListeRule::Instance().size());

    ListeRoom::Instance().checkAutoScenario();

    EXPECT_TRUE(findRule("e46c_claims_the_scenario") != nullptr)
            << "a rule claiming the scenario was destroyed";
    EXPECT_TRUE(findRule("e46c_claims_another") != nullptr)
            << "an unrelated marked rule was destroyed";
    //and the destruction is not merely deferred to the save
    saveConfig();
    EXPECT_NE(std::string::npos, rulesXmlOnDisk().find("e46c_claims_the_scenario"));
    EXPECT_NE(std::string::npos, rulesXmlOnDisk().find("e46c_claims_another"));
}

TEST_F(AutoScenarioRulesTest, RulesThatPredateTheDefinitionAreLeftAloneAndNotDuplicated)
{
    /* The shape a configuration written before the generator has: the rules
     * carry the scenario marker and no uid. The generator must stand down
     * entirely - not destroy them, and not build a second set beside them.
     */
    loadRulesHouse();
    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createRichScenario(ws));
    }
    saveConfig();

    const std::string ioXml = ioXmlOnDisk();
    std::string rulesXml = rulesXmlOnDisk();
    ASSERT_EQ(6, stripRuleAttribute(rulesXml, AutoScenarioDef::KEY_UID));

    clearCoreState();
    loadConfig(ioXml, rulesXml);
    ASSERT_EQ(6, ListeRule::Instance().size());
    const std::string before = serializeAllRules();

    ListeRoom::Instance().checkAutoScenario();

    EXPECT_EQ(6, ListeRule::Instance().size()) << "the untouchable rules were duplicated";
    EXPECT_EQ(before, serializeAllRules()) << "the generator rewrote rules it did not write";

    //A second startup does not change its mind either.
    ListeRoom::Instance().checkAutoScenario();
    EXPECT_EQ(6, ListeRule::Instance().size());
    EXPECT_EQ(before, serializeAllRules());
}

TEST_F(AutoScenarioRulesTest, TheStandDownHoldsWhenNEITHERTheRulesNORTheIoCarryAUid)
{
    /* The first-boot shape of an existing installation: no uid on the rules,
     * and no definition on the Scenario IO either. It is the case that catches
     * a stand-down written as "the rule carries a uid other than mine", because
     * on that boot both sides are empty and compare equal. Measured on a
     * production configuration: 125 rules in, 139 out.
     *
     * EXCHANGE against the case above, which strips the uid from the rules
     * only: there the scenario IO still declares one, and that alone hid this.
     */
    loadRulesHouse();
    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createRichScenario(ws));
    }
    saveConfig();

    std::string ioXml = ioXmlOnDisk();
    std::string rulesXml = rulesXmlOnDisk();
    ASSERT_EQ(6, stripRuleAttribute(rulesXml, AutoScenarioDef::KEY_UID));
    ASSERT_GT(stripDefinitionParams(ioXml), 0);
    ASSERT_EQ(std::string::npos, ioXml.find("autoscenario_uid"));

    clearCoreState();
    loadConfig(ioXml, rulesXml);
    ASSERT_EQ(6, ListeRule::Instance().size());
    ASSERT_TRUE(autoScenario() != nullptr) << "the legacy marker no longer builds one";
    const std::string before = serializeAllRules();

    ListeRoom::Instance().checkAutoScenario();

    EXPECT_EQ(6, ListeRule::Instance().size()) << "the untouchable rules were duplicated";
    EXPECT_EQ(before, serializeAllRules());

    /* The save mints a uid into io.xml on its own. The next boot must still
     * stand down, which is why the test is on the rules and not on it.
     */
    saveConfig();
    EXPECT_NE(std::string::npos, ioXmlOnDisk().find("autoscenario_uid"));

    const std::string io2 = ioXmlOnDisk(), rules2 = rulesXmlOnDisk();
    clearCoreState();
    loadConfig(io2, rules2);
    const std::string beforeBoot2 = serializeAllRules();
    ListeRoom::Instance().checkAutoScenario();
    EXPECT_EQ(6, ListeRule::Instance().size()) << "the second boot armed the generator";
    EXPECT_EQ(beforeBoot2, serializeAllRules());
}

TEST_F(AutoScenarioRulesTest, AnAuthoringCallOnRulesThatPredateTheDefinitionLeavesThemUnmarked)
{
    /* FLIPPED: the one way out of the stand-down, and it is what rewriting a
     * scenario has always meant - the whole set is taken over and stamped.
     */
    loadRulesHouse();
    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createRichScenario(ws));
    }
    saveConfig();

    const std::string ioXml = ioXmlOnDisk();
    std::string rulesXml = rulesXmlOnDisk();
    ASSERT_EQ(6, stripRuleAttribute(rulesXml, AutoScenarioDef::KEY_UID));

    clearCoreState();
    loadConfig(ioXml, rulesXml);
    ListeRoom::Instance().checkAutoScenario();
    ASSERT_EQ(6, ListeRule::Instance().size());

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    as->addStep(2.5);

    EXPECT_EQ(7, ListeRule::Instance().size()) << "the old rules were kept as well";
    ASSERT_EQ(4u, as->getRuleSteps().size());
    for (Rule *r: allRules())
        EXPECT_FALSE(r->get_param(AutoScenarioDef::KEY_UID).empty())
                << r->get_name() << " was not taken over";
}

TEST_F(AutoScenarioRulesTest, DeletingAScenarioTakesEveryOneOfItsRulesWithIt)
{
    loadScheduledScenarioFromDisk();

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    ASSERT_EQ(8, ListeRule::Instance().size());

    as->deleteAll();

    EXPECT_EQ(0, ListeRule::Instance().size()) << "the deleted scenario left rules behind";
}

/*******************************************************************************
 * SECTION 3 - NO READ MUTATES
 ******************************************************************************/

TEST_F(AutoScenarioRulesTest, ReadingAScenarioTwiceAnswersTheSameAndChangesNothing)
{
    loadScheduledScenarioFromDisk();

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);

    const std::string rulesBefore = serializeAllRules();

    auto readAll = [&as]()
    {
        std::string acc = as->getCategory();
        acc += "|" + std::to_string(as->getRuleSteps().size());
        acc += "|" + std::string(as->isBroken()? "b": "-");
        acc += "|" + as->getMissingIoDescription();
        for (int i = 0;i < 3;i++)
        {
            acc += "|" + std::to_string(as->getStepPause(i));
            acc += ":" + std::to_string(as->getStepActionCount(i));
            for (int j = 0;j < as->getStepActionCount(i);j++)
                acc += "," + as->getStepAction(i, j).action;
        }
        acc += "|" + std::to_string(as->getEndStepActionCount());
        return acc;
    };

    const std::string first = readAll();
    EXPECT_EQ(first, readAll());
    EXPECT_EQ(first, readAll());
    EXPECT_EQ(rulesBefore, serializeAllRules()) << "a read rewrote the rules";
    EXPECT_EQ(8, ListeRule::Instance().size()) << "a read destroyed a rule";
}

TEST_F(AutoScenarioRulesTest, ARebuildHealsABrokenScenarioByForgettingTheLostStep)
{
    /* FLIPPED: a step rule destroyed by a third party comes BACK - it is
     * regenerated from the definition, and the scenario is whole again.
     *
     * The reads are done in BOTH ORDERS on purpose: the payload emits
     * `category` before `broken`, and reading the category compacts the dead
     * entry away, so the breakage has to be latched to survive that.
     */
    loadScheduledScenarioFromDisk();

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    ASSERT_FALSE(as->isBroken());

    ListeRule::Instance().Remove(as->getRuleSteps()[1]);

    //category first, broken second
    const std::string category = as->getCategory();
    EXPECT_TRUE(as->isBroken()) << "reading the category erased the breakage";
    EXPECT_EQ("", as->getMissingIoDescription()) << "a destroyed rule names no id";

    //broken first, category second: same answers
    EXPECT_TRUE(as->isBroken());
    EXPECT_EQ(category, as->getCategory());
    EXPECT_TRUE(as->isBroken());

    WsTestSession ws;
    const Json a = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});
    const Json b = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});
    EXPECT_EQ("true", a.value("broken", std::string())) << a.dump();
    EXPECT_EQ(a.dump(), b.dump()) << "the second read of the payload differs";

    ASSERT_TRUE(as->rebuildRules());
    EXPECT_FALSE(as->isBroken());
    EXPECT_EQ(3u, as->getRuleSteps().size()) << "the lost step was not rebuilt";
}

TEST_F(AutoScenarioRulesTest, AnActionWhoseIoIsGoneIsRegeneratedAndDisablesItsRuleOnly)
{
    loadRulesHouse();
    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createRichScenario(ws));
    }
    saveConfig();

    std::string ioXml = ioXmlOnDisk();
    const std::string rulesXml = rulesXmlOnDisk();
    ASSERT_TRUE(removeIoFromXml(ioXml, IO_GHOST));

    clearCoreState();
    loadConfig(ioXml, rulesXml);
    ListeRoom::Instance().checkAutoScenario();

    ASSERT_TRUE(io(IO_GHOST) == nullptr);

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    const std::vector<Rule *> steps = as->getRuleSteps();
    ASSERT_EQ(3u, steps.size()) << "the step was dropped instead of being rebuilt";

    //The dead reference is IN the rebuilt rule, and it is what disables it.
    EXPECT_TRUE(ruleTargets(steps[1], IO_GHOST));
    EXPECT_EQ("bonne nuit", actionValueOn(steps[1], IO_GHOST)) << "the value was lost";
    EXPECT_TRUE(steps[1]->isDisabled());
    EXPECT_EQ(IO_GHOST, steps[1]->getMissingIoDescription());

    //and only that one
    EXPECT_FALSE(steps[0]->isDisabled());
    EXPECT_FALSE(steps[2]->isDisabled());
    EXPECT_FALSE(as->getRuleStepEnd()->isDisabled());

    EXPECT_TRUE(as->isBroken());
    EXPECT_EQ(IO_GHOST, as->getMissingIoDescription());
    EXPECT_TRUE(as->isDisabledMissingIo());

    //A second startup does not lose it either: the rebuild writes it back.
    saveConfig();
    EXPECT_NE(std::string::npos, rulesXmlOnDisk().find(IO_GHOST));
    ListeRoom::Instance().checkAutoScenario();
    ASSERT_EQ(3u, autoScenario()->getRuleSteps().size());
    EXPECT_TRUE(ruleTargets(autoScenario()->getRuleSteps()[1], IO_GHOST));
}

TEST_F(AutoScenarioRulesTest, PuttingTheIoBackClearsGateOneButNotTheStickyFlag)
{
    /* The pair `broken` / `disabled_missing_io` DISAGREEING - without a witness
     * where they diverge, swapping them is invisible.
     */
    loadRulesHouse();
    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createRichScenario(ws));
    }
    saveConfig();

    std::string ioXml = ioXmlOnDisk();
    std::string rulesXml = rulesXmlOnDisk();
    ASSERT_TRUE(removeIoFromXml(ioXml, IO_GHOST));

    clearCoreState();
    loadConfig(ioXml, rulesXml);
    ListeRoom::Instance().checkAutoScenario();
    ASSERT_TRUE(autoScenario()->isBroken());
    ASSERT_TRUE(autoScenario()->isDisabledMissingIo());

    /* The IO comes back, and the whole state is written and read again: the
     * flag has to survive that trip, which is the only reason it is persisted.
     */
    ASSERT_TRUE(createInternalIO("InternalString", IO_GHOST, "Fantome") != nullptr);
    saveConfig();
    ioXml = ioXmlOnDisk();
    rulesXml = rulesXmlOnDisk();

    clearCoreState();
    loadConfig(ioXml, rulesXml);
    ListeRoom::Instance().checkAutoScenario();

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    EXPECT_FALSE(as->isBroken());
    EXPECT_EQ("", as->getMissingIoDescription());
    EXPECT_TRUE(as->isDisabledMissingIo()) << "the sticky flag cleared itself";

    std::string err;
    EXPECT_TRUE(as->tryReenable(err));
    EXPECT_EQ("", err);
    EXPECT_FALSE(as->isDisabledMissingIo());
}

/*******************************************************************************
 * SECTION 5 - THE SCHEDULE
 ******************************************************************************/

TEST_F(AutoScenarioRulesTest, AddingAndRemovingTheScheduleRebuildsExactlyTheTwoRules)
{
    loadRulesHouse();
    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createRichScenario(ws));
    }

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    ASSERT_TRUE(as->isCycling());
    ASSERT_EQ(6, ListeRule::Instance().size());
    ASSERT_EQ(nullptr, as->getRulePlageStart());

    as->addSchedule();
    EXPECT_EQ(8, ListeRule::Instance().size());
    EXPECT_TRUE(as->getRulePlageStart() != nullptr);
    EXPECT_TRUE(as->getRulePlageStop() != nullptr);

    //Twice: a second add must not double them.
    as->addSchedule();
    EXPECT_EQ(8, ListeRule::Instance().size()) << "the schedule rules were duplicated";

    as->deleteSchedule();
    EXPECT_EQ(6, ListeRule::Instance().size()) << "a zombie schedule rule was left behind";
    EXPECT_EQ(nullptr, as->getRulePlageStart());
    EXPECT_EQ(nullptr, as->getRulePlageStop());

    //and the steps were not collateral damage
    EXPECT_EQ(3u, as->getRuleSteps().size());
    EXPECT_FALSE(as->isBroken());
}

