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
 * E4.6a - CHARACTERIZATION of the AutoScenario model, written BEFORE a single
 * line of src/ is touched by the E4.6 rewrite (docs/refactoring/E4.6.md).
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS FILE IS, AND WHAT IT IS NOT
 * ---------------------------------------------------------------------------
 * It records what the code does TODAY. Several of the behaviours below are
 * DEFECTS that E4.6 exists to remove. A characterization case never says "this
 * is right", it says "this is what happens". Every case that pins a defect is
 * marked, in its own comment, with:
 *
 *      >>> TO FLIP (E4.6x, RCn) <<<
 *
 * naming the root cause of E4.6.md §3 it belongs to and the sub-ticket that
 * owns the flip. A later sub-ticket must be able to turn such a case around
 * DELIBERATELY - never to discover it red and "repair" it by weakening the
 * assertion. If you are that sub-ticket: flip the expected value, keep the
 * measurement, and say so in the commit message.
 *
 * COUNTS, so a reader can check them: 19 cases. As shipped by E4.6a, 14
 * carried ">>> TO FLIP <<<" and 5 carried ">>> ✅ PROVE, DO NOT FLIP <<<".
 *
 * ALREADY FLIPPED, and by whom:
 *   - TodayIoXmlCarriesOnlyTheMarkerAndTheDerivedInternalIos -> E4.6b (D2),
 *     2026-09-01. io.xml now carries the definition, exactly as E4.6a
 *     predicted it would. 13 ">>> TO FLIP <<<" left.
 * E4.6b also corrected ONE precondition of loadScenarioWithTwoAmputatedActions()
 * without touching any case: it asserted that the id of an amputated IO was
 * absent from io.xml as a SUBSTRING, which io.xml can now produce legitimately
 * because the definition keeps the id of an action whose IO disappeared (D4).
 * It measures the element instead, and asserts the surviving id positively.
 *
 * The five defects E4.6.md asks E4.6a to pin, and where they live here:
 *
 *   (a) the ORPHAN SWEEP of ListeRoom.cpp:320-330 destroys - and persists the
 *       destruction of - every rule of a scenario whose IO lost its marker.
 *       This is the single thing in the whole epic that can destroy user data
 *       (E4.6.md §5.2: 18 rules of configs/raoulh).      -> section 1
 *   (b) an unmarked scenario IO stays a plain, triggerable IO.  -> section 1
 *   (c) `autoscenario get` on it answers "wrong input".         -> section 1
 *   (d) THE REFERENCE USE CASE: get -> modify verbatim -> reenable answers
 *       success TWICE and restarts an amputated scenario.       -> section 5
 *   (e) RC9: a configuration reloaded without the dead output leaves NO trace
 *       at all - the amputation is invisible.                   -> section 8
 *
 * and (f), which is NOT a defect but an acquired property nothing attested:
 *       `config put` backs the configuration up BEFORE overwriting it, and the
 *       backup is usable to get the lost scenario back.         -> section 9
 *
 * ---------------------------------------------------------------------------
 * SITES, ON master. RECHECK THEM, DO NOT TRUST A QUOTE
 * ---------------------------------------------------------------------------
 * Line numbers quoted here were re-verified on master (aa4821f7). Numbers from
 * the T3.20 review circulate with a ~48 line offset because they were taken in
 * a worktree that adds set_param/del_param overrides to IO/Scenario.cpp. On
 * master that file is 195 lines long and the two escamotage guards are at
 * :158 (standard steps) and :181 (synthetic end step).
 *
 * ---------------------------------------------------------------------------
 * THE FIXTURE IS RICH ON PURPOSE - DO NOT MAKE IT SMALLER
 * ---------------------------------------------------------------------------
 * "Fixture pauvre" - a data set too poor to tell two interchangeable fields
 * apart - is the most frequent defect of this series (7 recurrences, all found
 * by reviewers, never by implementers). So, deliberately:
 *
 *   - THREE standard steps plus the synthetic end step, never one;
 *   - the missing IO sits in the MIDDLE step and in the MIDDLE of that step's
 *     action list, never at either end, so an off-by-one cannot hide;
 *   - a SECOND missing IO in the end step, also in the middle, so the two
 *     escamotage guards (:158 and :181) are covered separately and so
 *     missing_ios is a LIST and its order is observable;
 *   - three DIFFERENT pauses (1.5 / 2.25 / 0.5) so swapping two steps fails;
 *   - action values that look nothing alike ("true", "77", "fantome", "42",
 *     "bonsoir", "false") so swapping two actions fails;
 *   - one IO (LAMP) targeted TWICE with two DIFFERENT values, so a payload
 *     that confuses the id with the value fails.
 *
 * ---------------------------------------------------------------------------
 * PROBES
 * ---------------------------------------------------------------------------
 * E4.0c took "reenable" as its "unknown sub-command" canary; T3.18 then
 * SHIPPED that command and the canary silently became a valid value. Every
 * probe here is therefore chosen OUTSIDE the namespaces E4.6 is going to
 * populate: the future param prefixes are `autoscenario_` and `as_` (E4.6.md
 * D2), so the unknown-param probe below is `e46a_probe_param`, which no
 * sub-ticket can ever turn into a real name.
 *
 * ---------------------------------------------------------------------------
 * EVENT QUEUE - THERE IS EXACTLY ONE PUMP IN THIS FILE, AND IT IS MEASURED
 * ---------------------------------------------------------------------------
 * The harness leaves the queue EMPTY between cases (E4.0g, see the TearDown()
 * comment in JsonApiCharacterization.h). YOU INHERIT AN EMPTY QUEUE.
 *
 * THIS FILE FIRST SHIPPED WITH FOURTEEN PUMPS AND A HEADER CLAIMING TWO WERE
 * MEASURED. THIRTEEN OF THEM ABSORBED NOTHING, and the review found it, which
 * is exactly the recurrence §9.7 of E4.6.md describes: copying a pump you have
 * not measured, with an explanation that is itself false. The thirteen were:
 *   - twelve placed after ListeRoom::checkAutoScenario(), copied from one
 *     another. That pass raises EventScenarioChanged, but NO case of this file
 *     asserts anything about a message while those events are pending;
 *   - one in loadMigrationHouse(), justified by "loading raises one
 *     EventIOAdded per IO" - a claim JsonApiCharacterization.h had ALREADY
 *     corrected in writing: config loading raises NOTHING, EventIOAdded has a
 *     single call site and it is ListeRoom::createIO(), the runtime path.
 * MEASURED as removable: 18/18 green without them, in default order and on
 * five --gtest_shuffle seeds (1, 7, 42, 1234, 99999).
 *
 * The ONE that is left is in
 * AnUnmarkedScenarioIoStillRunsItsRulesWhenTheButtonIsPressed, and it is load
 * bearing because that case asserts an ABSENCE on one branch and a PRESENCE on
 * the other, over the same channel: no io_changed for the scenario IO while it
 * is marked and broken, one after the marker is gone. An absence assertion is
 * worth nothing unless the channel was flushed first - "not delivered" is not
 * "not raised" - so the pump is what turns that half into an oracle.
 * If you ever need another one: measure what it absorbs, or do not add it.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "AutoScenario.h"
#include "CalaosConfig.h"
#include "ConditionStd.h"
#include "EventManager.h"
#include "FileUtils.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "Rule.h"
#include "Scenario.h"

#include <pugixml.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/* Ids are e46a_ prefixed and used nowhere else in the tree: Config's IO state
 * cache is process wide and never cleared (CalaosCoreFixture.h), so sharing an
 * id with another test file leaks state between binaries.
 * The scenario IO id and the marker are NOT prefixed: they are generated by
 * production (get_new_id("io_"), get_new_scenario_id()) and that generation is
 * part of what is characterized here.
 */
const char ROOM_NAME_E46A[] = "E4.6a room";
const char ROOM_TYPE_E46A[] = "salon";

const char IO_LAMP[] = "e46a_lamp";            //bool, targeted TWICE, two values
const char IO_BLIND[] = "e46a_blind";          //int,  "77"
const char IO_GHOST_STEP[] = "e46a_ghost_step"; //string, VANISHES, middle of step 2
const char IO_VOLUME[] = "e46a_volume";        //int,  "42"
const char IO_BANNER[] = "e46a_banner";        //string, "bonsoir"
const char IO_SIREN[] = "e46a_siren";          //bool, "false" in the end step
const char IO_GHOST_END[] = "e46a_ghost_end";  //bool, VANISHES, middle of the end step

//What production hands out for the FIRST scenario of a fresh house
const char SCENARIO_IO_ID[] = "io_0";
const char SCENARIO_MARKER[] = "scenario_0";

//A param name that lives OUTSIDE every namespace E4.6 will ever populate
//(`autoscenario_*` and `as_*`, E4.6.md D2). It cannot become a real name.
const char PROBE_PARAM[] = "e46a_probe_param";
//and a value carrying the three characters D2 will have to percent-encode
const char PROBE_VALUE[] = "a|b=c%d";

std::string houseIosXml()
{
    std::string ios;
    ios += internalIoXml("InternalBool", IO_LAMP, "Lampe");
    ios += internalIoXml("InternalInt", IO_BLIND, "Volet");
    ios += internalIoXml("InternalString", IO_GHOST_STEP, "Fantome d'etape");
    ios += internalIoXml("InternalInt", IO_VOLUME, "Volume");
    ios += internalIoXml("InternalString", IO_BANNER, "Banniere");
    ios += internalIoXml("InternalBool", IO_SIREN, "Sirene");
    ios += internalIoXml("InternalBool", IO_GHOST_END, "Fantome de fin");
    return ios;
}

} //namespace

class AutoScenarioMigrationTest: public JsonApiCharacterizationTest
{
protected:
    void loadMigrationHouse()
    {
        //NO PUMP HERE. Config loading raises no event at all (measured, and
        //already written down in JsonApiCharacterization.h): the pump this
        //function used to end with absorbed nothing.
        loadConfig(ioXmlDocument(roomXml(ROOM_NAME_E46A, ROOM_TYPE_E46A, houseIosXml(), 0)),
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

    static Json wsRequest(const std::string &msg, Json data,
                          const std::string &msgId = "e46a")
    {
        return Json{{ "msg", msg }, { "msg_id", msgId }, { "data", data }};
    }

    //Sends one autoscenario sub-command over WS and answers the "data" member
    //of the single reply.
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
     * of it is load bearing. Three standard steps, a synthetic end step, two
     * IOs that will be made to vanish and both of them in the MIDDLE of their
     * action list, three different pauses, six different values.
     * Answers the id of the Scenario IO, "io_0" on a fresh house.
     */
    std::string createRichScenario(WsTestSession &ws)
    {
        const Json ret = wsAutoscenario(ws, Json{
            { "type", "create" },
            { "name", "Soir\xc3\xa9""e" },       //accented on purpose, see E4.0a
            { "room_name", ROOM_NAME_E46A },
            { "room_type", ROOM_TYPE_E46A },
            { "steps", Json::array({
                Json{{ "step_type", "standard" }, { "step_pause", "1.5" },
                     { "actions", Json::array({
                         Json{{ "id", IO_LAMP }, { "action", "true" }} }) }},
                Json{{ "step_type", "standard" }, { "step_pause", "2.25" },
                     { "actions", Json::array({
                         Json{{ "id", IO_BLIND }, { "action", "77" }},
                         Json{{ "id", IO_GHOST_STEP }, { "action", "fantome" }},
                         Json{{ "id", IO_VOLUME }, { "action", "42" }} }) }},
                Json{{ "step_type", "standard" }, { "step_pause", "0.5" },
                     { "actions", Json::array({
                         Json{{ "id", IO_BANNER }, { "action", "bonsoir" }} }) }},
                Json{{ "step_type", "end" },
                     { "actions", Json::array({
                         Json{{ "id", IO_SIREN }, { "action", "false" }},
                         Json{{ "id", IO_GHOST_END }, { "action", "true" }},
                         Json{{ "id", IO_LAMP }, { "action", "false" }} }) }}
            }) }});

        return ret.value("id", std::string());
    }

    /* ---------------------------------------------------------------------
     * XML surgery. Done with pugixml rather than string replacement: the
     * attribute order of a saved document is an implementation detail of
     * pugixml and a textual patch would break the day it changes.
     * ------------------------------------------------------------------ */

    static std::string serialize(const pugi::xml_document &doc)
    {
        std::ostringstream ss;
        doc.save(ss);
        return ss.str();
    }

    static bool parse(const std::string &xml, pugi::xml_document &doc)
    {
        return doc.load_buffer(xml.data(), xml.size());
    }

    //Every <calaos:*> IO element of an io.xml document, in document order.
    static std::vector<pugi::xml_node> ioNodes(pugi::xml_document &doc)
    {
        std::vector<pugi::xml_node> out;
        for (pugi::xml_node room: doc.child("calaos:ioconfig").child("calaos:home"))
            for (pugi::xml_node io: room)
                out.push_back(io);
        return out;
    }

    //Delete one IO element from io.xml, the way somebody removing a line from
    //the file - or an installer that no longer models it - does.
    static bool removeIoFromXml(std::string &xml, const std::string &id)
    {
        pugi::xml_document doc;
        if (!parse(xml, doc)) return false;

        for (pugi::xml_node io: ioNodes(doc))
        {
            if (io.attribute("id").value() != id) continue;
            io.parent().remove_child(io);
            xml = serialize(doc);
            return true;
        }
        return false;
    }

    /* Remove the `auto_scenario` param FROM THE SCENARIO IO ONLY. This is the
     * migration of E4.6.md §5: the marker is re-keyed, so
     * Scenario::Scenario() (IO/Scenario.cpp:48) no longer builds an
     * AutoScenario for this IO. Everything else - the internal IOs, the rules -
     * is left exactly as it is on disk, which is the whole point: the
     * production files are NOT rewritten by the migration (§5.4).
     */
    static bool unmarkScenarioIoInXml(std::string &xml)
    {
        pugi::xml_document doc;
        if (!parse(xml, doc)) return false;

        for (pugi::xml_node io: ioNodes(doc))
        {
            if (std::string(io.attribute("type").value()) != "scenario") continue;
            if (!io.remove_attribute("auto_scenario")) return false;
            xml = serialize(doc);
            return true;
        }
        return false;
    }

    //Add an arbitrary attribute to the scenario IO of an io.xml document.
    static bool addParamToScenarioIoInXml(std::string &xml,
                                          const std::string &key,
                                          const std::string &value)
    {
        pugi::xml_document doc;
        if (!parse(xml, doc)) return false;

        for (pugi::xml_node io: ioNodes(doc))
        {
            if (std::string(io.attribute("type").value()) != "scenario") continue;
            io.append_attribute(key.c_str()).set_value(value.c_str());
            xml = serialize(doc);
            return true;
        }
        return false;
    }

    //Every <calaos:rule> of a rules.xml document, in document order.
    static std::vector<pugi::xml_node> ruleNodes(pugi::xml_document &doc)
    {
        std::vector<pugi::xml_node> out;
        for (pugi::xml_node r: doc.child("calaos:rules"))
            out.push_back(r);
        return out;
    }

    /* Change ONE literal condition value of ONE step rule, leaving everything
     * else - the ids, the operator, the params - untouched.
     * This is what checkScenarioRules() compares, condition by condition, at
     * AutoScenario.cpp:684-690: a value that differs by one character makes
     * checkCondition() answer false and the rule is `continue`-ed in silence.
     */
    static bool retuneStepCondition(std::string &xml, const std::string &stepNumber,
                                    const std::string &ioIdSuffix,
                                    const std::string &newValue)
    {
        pugi::xml_document doc;
        if (!parse(xml, doc)) return false;

        for (pugi::xml_node rule: ruleNodes(doc))
        {
            if (std::string(rule.attribute("auto_scenario_type").value()) != "step") continue;
            if (std::string(rule.attribute("auto_scenario_step").value()) != stepNumber) continue;

            for (pugi::xml_node cond: rule.children("calaos:condition"))
            {
                for (pugi::xml_node in: cond.children("calaos:input"))
                {
                    const std::string id = in.attribute("id").value();
                    if (id.size() < ioIdSuffix.size()) continue;
                    if (id.compare(id.size() - ioIdSuffix.size(),
                                   ioIdSuffix.size(), ioIdSuffix) != 0) continue;

                    in.attribute("val").set_value(newValue.c_str());
                    xml = serialize(doc);
                    return true;
                }
            }
        }
        return false;
    }

    //Same, on the single action value of a header rule (button_start & co).
    static bool retuneHeaderAction(std::string &xml, const std::string &autoScenarioType,
                                   const std::string &ioIdSuffix,
                                   const std::string &oldValue,
                                   const std::string &newValue)
    {
        pugi::xml_document doc;
        if (!parse(xml, doc)) return false;

        for (pugi::xml_node rule: ruleNodes(doc))
        {
            if (std::string(rule.attribute("auto_scenario_type").value()) != autoScenarioType)
                continue;

            for (pugi::xml_node act: rule.children("calaos:action"))
            {
                for (pugi::xml_node out: act.children("calaos:output"))
                {
                    const std::string id = out.attribute("id").value();
                    if (id.size() < ioIdSuffix.size()) continue;
                    if (id.compare(id.size() - ioIdSuffix.size(),
                                   ioIdSuffix.size(), ioIdSuffix) != 0) continue;
                    if (std::string(out.attribute("val").value()) != oldValue) continue;

                    out.attribute("val").set_value(newValue.c_str());
                    xml = serialize(doc);
                    return true;
                }
            }
        }
        return false;
    }

    /* Drop every <calaos:output id="..."> naming this IO from rules.xml.
     * THIS IS THE calaos_installer ROUND TRIP (E4.6.md §3, RC9): the installer
     * drops an action output whose id does not resolve at LOAD time
     * (projectmanager.cpp:1126-1133, `if (output)` with no else), then
     * regenerates rules.xml in full and uploads it. What comes back to the
     * server is a perfectly coherent file from which the reference has simply
     * disappeared - no server side guard can see it.
     * Answers how many outputs were removed.
     */
    static int dropActionOutputFromRulesXml(std::string &xml, const std::string &id)
    {
        pugi::xml_document doc;
        if (!parse(xml, doc)) return 0;

        int removed = 0;
        for (pugi::xml_node rule: ruleNodes(doc))
        {
            for (pugi::xml_node act: rule.children("calaos:action"))
            {
                std::vector<pugi::xml_node> victims;
                for (pugi::xml_node out: act.children("calaos:output"))
                    if (std::string(out.attribute("id").value()) == id)
                        victims.push_back(out);

                for (pugi::xml_node v: victims)
                {
                    act.remove_child(v);
                    removed++;
                }
            }
        }

        if (removed) xml = serialize(doc);
        return removed;
    }

    /* ---------------------------------------------------------------------
     * Rule inspection
     * ------------------------------------------------------------------ */

    //Ids of the rules carrying the `auto_scenario` param, in ListeRule order.
    static std::vector<std::string> markedRuleNames()
    {
        std::vector<std::string> out;
        for (int i = 0;i < ListeRule::Instance().size();i++)
        {
            Rule *r = ListeRule::Instance().get_rule(i);
            if (r && r->param_exists("auto_scenario"))
                out.push_back(r->get_name() + "/" + r->get_param("auto_scenario_type") +
                              (r->param_exists("auto_scenario_step")?
                                   "#" + r->get_param("auto_scenario_step"): std::string()));
        }
        return out;
    }

    static size_t markedRuleCount() { return markedRuleNames().size(); }

    /* How many "io_changed" events a session received for this IO id.
     * ALWAYS pump before calling it: on the ABSENCE side, a channel that was
     * never flushed answers zero for the wrong reason (E4.0g, "not delivered
     * is not not raised").
     */
    static size_t ioChangedCountFor(const WsTestSession &ws, const std::string &ioId)
    {
        size_t n = 0;
        for (const std::string &m: ws.messages())
        {
            const Json env = Json::parse(m, nullptr, false);
            if (env.is_discarded()) continue;
            if (env.value("msg", std::string()) != "event") continue;
            const Json data = member(env, "data");
            if (str(data, "type_str") != "io_changed") continue;
            if (str(member(data, "data"), "id") == ioId) n++;
        }
        return n;
    }

    /* The value a rule's condition compares `ioId` against, "" when the rule
     * has no such condition. This is numbering (c) of E4.6.md §2.4 - the one
     * checkScenarioRules() REWRITES at :811-829 - as opposed to numbering (b),
     * the `auto_scenario_step` param, which it does not touch.
     */
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

    //Number of <calaos:rule> elements of the rules.xml that is on disk NOW.
    size_t ruleCountOnDisk() const
    {
        pugi::xml_document doc;
        const std::string xml = rulesXmlOnDisk();
        if (!doc.load_buffer(xml.data(), xml.size())) return 0;

        size_t n = 0;
        for (pugi::xml_node r: doc.child("calaos:rules")) { (void)r; n++; }
        return n;
    }

    /* ---------------------------------------------------------------------
     * The two amputated configurations
     * ------------------------------------------------------------------ */

    /* The rich scenario, saved, with the TWO ghost IOs removed from io.xml and
     * everything reloaded. rules.xml is left untouched, so the two dead
     * references survive verbatim (ActionStd::SaveToXml(), E4.2e) - which is
     * what makes the whole "broken" machinery observable at all.
     */
    std::string loadScenarioWithTwoAmputatedActions()
    {
        loadMigrationHouse();

        std::string scenarioId;
        {
            WsTestSession ws;
            scenarioId = createRichScenario(ws);
        }
        EXPECT_EQ(SCENARIO_IO_ID, scenarioId);

        saveConfig();

        std::string ioXml = ioXmlOnDisk();
        const std::string rulesXml = rulesXmlOnDisk();
        EXPECT_TRUE(removeIoFromXml(ioXml, IO_GHOST_STEP));
        EXPECT_TRUE(removeIoFromXml(ioXml, IO_GHOST_END));
        /* E4.6b: "the IO ELEMENT is gone", not "the string is gone". io.xml
         * now carries the definition (D2) and the definition KEEPS the id of
         * an action whose IO disappeared (D4) - which is the whole point, and
         * which the two EXPECT_NE below now assert positively. Before E4.6b
         * these two lines read `ioXml.find(IO_GHOST_*) == npos`; they measured
         * the element through a substring that nothing else could produce, and
         * something else can now.
         */
        EXPECT_EQ(std::string::npos, ioXml.find(std::string("id=\"") + IO_GHOST_STEP + "\""));
        EXPECT_EQ(std::string::npos, ioXml.find(std::string("id=\"") + IO_GHOST_END + "\""));
        EXPECT_NE(std::string::npos, ioXml.find(IO_GHOST_STEP))
                << "the definition in io.xml lost the id of the dead action (D4)";
        EXPECT_NE(std::string::npos, ioXml.find(IO_GHOST_END))
                << "the definition in io.xml lost the id of the dead action (D4)";
        //the rules keep the dead references verbatim: that is the premise
        EXPECT_NE(std::string::npos, rulesXml.find(IO_GHOST_STEP));
        EXPECT_NE(std::string::npos, rulesXml.find(IO_GHOST_END));

        clearCoreState();
        loadConfig(ioXml, rulesXml);
        ListeRoom::Instance().checkAutoScenario();

        return scenarioId;
    }

    //The rich scenario, saved and reloaded untouched. The control the amputated
    //configurations are compared against.
    std::string loadHealthyScenarioFromDisk()
    {
        loadMigrationHouse();

        std::string scenarioId;
        {
            WsTestSession ws;
            scenarioId = createRichScenario(ws);
        }
        EXPECT_EQ(SCENARIO_IO_ID, scenarioId);

        saveConfig();
        const std::string ioXml = ioXmlOnDisk();
        const std::string rulesXml = rulesXmlOnDisk();

        clearCoreState();
        loadConfig(ioXml, rulesXml);
        ListeRoom::Instance().checkAutoScenario();

        return scenarioId;
    }

    /* ---------------------------------------------------------------------
     * Running the scenario for real. Copied in spirit from
     * ScenarioDisabledMissingIo_test: press the button, and if it really
     * started, execute the step rules. Nothing is short circuited - ioIsActive
     * is set by the _button_start rule and by nothing else.
     * Answers the number of step rules that executed.
     * ------------------------------------------------------------------ */
    static int runScenario()
    {
        Scenario *sc = scenarioIo();
        AutoScenario *as = sc? sc->getAutoScenario(): nullptr;
        if (!sc || !as) return 0;

        sc->set_value(true);

        if (!as->getIOIsActive() || !as->getIOIsActive()->get_value_bool())
            return 0; //it did not start: no step rule can pass its conditions

        int ran = 0;
        for (Rule *step: as->getRuleSteps())
            if (step && step->ExecuteActions())
                ran++;

        return ran;
    }

    /* ---------------------------------------------------------------------
     * Backups (D10 level 1)
     * ------------------------------------------------------------------ */

    //Every copy of `name` under <config>/backups, newest first by mtime. Same
    //walk as findBackupsNewestFirst() (CalaosConfig.cpp:55-82), reimplemented
    //here because that one is file static.
    std::vector<std::string> backupsOf(const std::string &name) const
    {
        namespace fs = std::filesystem;
        std::vector<std::pair<fs::file_time_type, std::string>> found;

        std::error_code ec;
        fs::recursive_directory_iterator it(configDir() + "/backups",
                                            fs::directory_options::skip_permission_denied,
                                            ec), end;
        for (;!ec && it != end;it.increment(ec))
        {
            std::error_code fec;
            if (!it->is_regular_file(fec) || fec) continue;
            if (it->path().filename() != name) continue;
            auto t = fs::last_write_time(it->path(), fec);
            if (fec) continue;
            found.emplace_back(t, it->path().string());
        }

        std::sort(found.begin(), found.end(),
                  [](const auto &a, const auto &b) { return a.first > b.first; });

        std::vector<std::string> out;
        for (auto &f: found) out.push_back(std::move(f.second));
        return out;
    }

    static std::string readWholeFile(const std::string &path)
    {
        std::ifstream f(path.c_str());
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
};

/*******************************************************************************
 * SECTION 1 - THE MIGRATION, AND THE ONE THING IN E4.6 THAT CAN DESTROY DATA
 *
 * E4.6 re-keys the marker: `auto_scenario` stops being what makes a Scenario IO
 * an auto scenario. E4.6.md §5.1 establishes that the old data then "keeps
 * working as ordinary rules" - and §5.2 establishes that this promise holds
 * under ONE condition: the orphan sweep of ListeRoom::checkAutoScenario()
 * (ListeRoom.cpp:320-330) must be re-keyed or removed.
 *
 * The three cases below are the net under that condition. They are written
 * BEFORE any src/ change so that the sub-ticket which re-keys the marker cannot
 * discover the destruction after the fact.
 ******************************************************************************/

TEST_F(AutoScenarioMigrationTest, RekeyingTheMarkerMakesTheOrphanSweepDestroyEveryRuleOfTheScenario)
{
    /* >>> TO FLIP (E4.6c, RC1) <<<   defect (a) of E4.6.md §6.
     *
     * ListeRoom.cpp:324, predicate
     *     rule->param_exists("auto_scenario") && !rule->isAutoScenario()
     * Params is an EXACT match (src/lib/Params.cpp:31-37), so on files written
     * by today's server the left half stays TRUE after a re-key, while the
     * right half becomes FALSE because nobody adopts the rules any more.
     * Every rule of the scenario is destroyed, in silence, at the first
     * startup. On configs/raoulh that is 18 rules.
     *
     * THE MEASUREMENT IS AN EXCHANGE, not a neutralization: the SAME two files
     * are loaded twice, once with the marker on the Scenario IO and once
     * without it, and nothing else differs. The control half is what proves the
     * treatment half means something.
     *
     * E4.6c removes the sweep entirely (D1). When it does, this case flips to:
     * the rules SURVIVE the re-key. Flip the expected value, keep the exchange.
     */
    loadMigrationHouse();

    std::string scenarioId;
    {
        WsTestSession ws;
        scenarioId = createRichScenario(ws);
    }
    ASSERT_EQ(SCENARIO_IO_ID, scenarioId);

    saveConfig();
    const std::string ioXmlMarked = ioXmlOnDisk();
    const std::string rulesXml = rulesXmlOnDisk();

    /* --- control: the marker is there, the rules are adopted and survive --- */
    clearCoreState();
    loadConfig(ioXmlMarked, rulesXml);
    ASSERT_TRUE(autoScenario() != nullptr) << "the control needs a live AutoScenario";
    const std::vector<std::string> before = markedRuleNames();
    //3 steps + button_start + button_stop + step_end
    ASSERT_EQ(6u, before.size()) << "unexpected rule shape, the fixture moved";

    ListeRoom::Instance().checkAutoScenario();
    EXPECT_EQ(before, markedRuleNames())
            << "with the marker in place the sweep must keep every rule";

    /* --- treatment: the SAME files, marker removed from the Scenario IO --- */
    std::string ioXmlUnmarked = ioXmlMarked;
    ASSERT_TRUE(unmarkScenarioIoInXml(ioXmlUnmarked));
    //Only the Scenario IO lost it. The internal IOs still carry the param, and
    //so do the rules: that is exactly the state a production file is left in.
    EXPECT_NE(std::string::npos, ioXmlUnmarked.find("auto_scenario=\"" +
                                                    std::string(SCENARIO_MARKER) + "\""))
            << "the internal IOs must keep their marker, only the Scenario IO loses it";

    clearCoreState();
    loadConfig(ioXmlUnmarked, rulesXml);

    ASSERT_TRUE(scenarioIo() != nullptr) << "the IO itself is still there";
    ASSERT_TRUE(autoScenario() == nullptr) << "and it is no longer an auto scenario";
    ASSERT_EQ(6u, markedRuleCount()) << "the six rules loaded fine, they are ordinary rules";

    ListeRoom::Instance().checkAutoScenario();

    EXPECT_EQ(0u, markedRuleCount())
            << "TODAY the sweep destroys all six. E4.6c must make this zero become six.";
}

TEST_F(AutoScenarioMigrationTest, TheOrphanSweepDestructionIsPersistedToRulesXmlAtTheFirstStartup)
{
    /* >>> TO FLIP (E4.6c, RC1) <<<   defect (a), the half that makes it fatal.
     *
     * checkAutoScenario() ends with SaveConfigRule() (ListeRoom.cpp:341), two
     * lines below the sweep. The destruction is therefore not a runtime
     * accident one can recover from by restarting: it is written to disk, at
     * the first startup, with no user action of any kind.
     */
    loadMigrationHouse();

    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createRichScenario(ws));
    }

    saveConfig();
    std::string ioXml = ioXmlOnDisk();
    const std::string rulesXml = rulesXmlOnDisk();
    ASSERT_TRUE(unmarkScenarioIoInXml(ioXml));

    clearCoreState();
    loadConfig(ioXml, rulesXml);
    ASSERT_EQ(6u, ruleCountOnDisk()) << "the six rules are on disk before the startup pass";

    ListeRoom::Instance().checkAutoScenario();

    EXPECT_EQ(0u, ruleCountOnDisk())
            << "TODAY the six rules are erased from rules.xml. E4.6c must keep them.";
    EXPECT_EQ(std::string::npos, rulesXmlOnDisk().find(SCENARIO_MARKER));
}

TEST_F(AutoScenarioMigrationTest, AnUnmarkedScenarioIoStillRunsItsRulesWhenTheButtonIsPressed)
{
    /* ✅ PROVE, DO NOT FLIP.
     * Defect (b) of E4.6.md §6 - and this one is a PROMISE of the migration,
     * not a defect: "the old scenarios keep working because they are ordinary
     * rules" (§5.1, §5.3). IO/Scenario.cpp:80 guards both T3.18 gates with
     * `auto_scenario &&`, so an unmarked IO goes straight to EmitSignalIO()
     * (:95) - which is where the rules engine picks it up.
     *
     * It is measured at the level that matters: not "the value flipped" (it
     * does not stay flipped - `_button_start` immediately sets the scenario IO
     * back to false, which is what a scenario STARTING looks like) but "the
     * chain ran": `<marker>_is_active` goes true and `<marker>_step` goes to 0.
     *
     * EXCHANGE, and it is the whole point: the SAME configuration is loaded
     * twice, marked and unmarked, with the same amputation in it. Marked and
     * broken, the gates refuse and the chain does NOT run; unmarked, the gates
     * do not exist and it does. Asserting only the second half would pass even
     * if set_value() had stopped doing anything at all.
     */
    loadScenarioWithTwoAmputatedActions();

    const std::string isActiveId = std::string(SCENARIO_MARKER) + "_is_active";
    const std::string stepId = std::string(SCENARIO_MARKER) + "_step";

    //--- marked and broken: the button is ACCEPTED and deliberately does nothing
    Scenario *sc = scenarioIo();
    ASSERT_TRUE(sc != nullptr && sc->getAutoScenario() != nullptr);
    ASSERT_TRUE(sc->getAutoScenario()->isBroken());
    ASSERT_TRUE(io(isActiveId) != nullptr);
    {
        WsTestSession refused;
        EXPECT_TRUE(sc->set_value(true)) << "the convention is `accepted, did nothing`";
        EXPECT_FALSE(io(isActiveId)->get_value_bool())
                << "a broken MARKED scenario must not start";

        //ABSENCE, and it is only an oracle because the channel is flushed
        //first: set_value() returns at IO/Scenario.cpp:91, BEFORE
        //EmitSignalIO() and before EventManager::create(), so nothing is even
        //raised. Pump, then count.
        pumpEventLoop();
        EXPECT_EQ(0u, ioChangedCountFor(refused, SCENARIO_IO_ID))
                << "a refused button must not report an io_changed";
    }

    //--- unmarked: the same two files, marker removed from the Scenario IO only
    saveConfig();
    std::string ioXml = ioXmlOnDisk();
    const std::string rulesXml = rulesXmlOnDisk();
    ASSERT_TRUE(unmarkScenarioIoInXml(ioXml));

    clearCoreState();
    loadConfig(ioXml, rulesXml);

    sc = scenarioIo();
    ASSERT_TRUE(sc != nullptr);
    ASSERT_TRUE(sc->getAutoScenario() == nullptr) << "no AutoScenario is built any more";
    EXPECT_EQ("scenario", sc->get_param("gui_type"))
            << "gui_type is unconditional (IO/Scenario.cpp:46), which is why the "
               "MCP tools keep seeing the old scenarios after the migration";
    //the six rules are ordinary rules now, and nothing has swept them yet
    ASSERT_EQ(6u, markedRuleCount());

    WsTestSession ws;
    EXPECT_TRUE(sc->set_value(true));

    ASSERT_TRUE(io(isActiveId) != nullptr);
    EXPECT_TRUE(io(isActiveId)->get_value_bool())
            << "the _button_start rule did not fire: the old scenario stopped working";
    EXPECT_EQ(0, (int)io(stepId)->get_value_double())
            << "the chain armed step 0";
    //`_button_start` sets the scenario IO back to false straight away: a
    //scenario STARTING is exactly what that looks like
    EXPECT_FALSE(sc->get_value_bool());

    /* PRESENCE, on the same channel and with the same counter as the absence
     * asserted above - that pairing is what makes both halves mean something.
     * THE ONLY PUMP OF THIS FILE, with the one above; see the header.
     */
    pumpEventLoop();
    EXPECT_EQ(2u, ioChangedCountFor(ws, SCENARIO_IO_ID))
            << "MEASURED: exactly two - the press (state true) and the immediate "
               "reset by `_button_start` (state false). That is the whole chain "
               "running, on the wire. Among " << ws.count() << " messages.";
}

TEST_F(AutoScenarioMigrationTest, AutoscenarioGetAndListIgnoreAnUnmarkedScenarioIo)
{
    /* ✅ PROVE, DO NOT FLIP.
     * Defect (c) of E4.6.md §6 - also a PROMISE, and the literal wording of the
     * user mandate: "the current ones are no longer considered auto scenarios".
     * buildAutoscenarioGet() tests !sc->getAutoScenario() (JsonApi.cpp:1896),
     * buildAutoscenarioList() walks ListeRoom::getAutoScenarios(), which is fed
     * by AutoScenario's constructor (AutoScenario.cpp:121) and therefore never
     * sees an unmarked IO.
     *
     * EXCHANGE: same house, same IO, marker on then off.
     */
    loadMigrationHouse();

    std::string scenarioId;
    {
        WsTestSession ws;
        scenarioId = createRichScenario(ws);
    }
    ASSERT_EQ(SCENARIO_IO_ID, scenarioId);

    saveConfig();
    const std::string ioXmlMarked = ioXmlOnDisk();
    const std::string rulesXml = rulesXmlOnDisk();

    //--- control: marked, it is listed and readable
    clearCoreState();
    loadConfig(ioXmlMarked, rulesXml);
    {
        WsTestSession ws;
        const Json list = wsAutoscenario(ws, Json{{ "type", "list" }});
        ASSERT_TRUE(list["scenarios"].is_array());
        ASSERT_EQ(1u, list["scenarios"].size());
        EXPECT_EQ(scenarioId, list["scenarios"][0].value("id", std::string()));

        const Json get = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", scenarioId }});
        EXPECT_EQ(scenarioId, get.value("id", std::string()));
    }

    //--- treatment: unmarked, it is gone from the API and only from the API
    std::string ioXmlUnmarked = ioXmlMarked;
    ASSERT_TRUE(unmarkScenarioIoInXml(ioXmlUnmarked));

    clearCoreState();
    loadConfig(ioXmlUnmarked, rulesXml);
    {
        WsTestSession ws;
        const Json list = wsAutoscenario(ws, Json{{ "type", "list" }});
        ASSERT_TRUE(list["scenarios"].is_array());
        EXPECT_EQ(0u, list["scenarios"].size());

        EXPECT_JSON_EQ(std::string(R"({"error":"wrong input"})"),
                       wsAutoscenario(ws, Json{{ "type", "get" }, { "id", scenarioId }}));
    }
    //...and the IO is still perfectly there
    EXPECT_TRUE(scenarioIo() != nullptr);
}

/*******************************************************************************
 * SECTION 2 - RC1: THE MODEL IS RE-GUESSED BY LITERAL PATTERN MATCHING
 *
 * checkScenarioRules() (AutoScenario.cpp:547-832) does not read a scenario, it
 * RECOGNIZES one: for each candidate rule it compares, condition by condition
 * and action by action, the IO POINTER, the operator and the VALUE
 * (checkCondition() :426, checkAction() :444). A rule that does not match is
 * `continue`-ed in silence (:639-716); header rules are then recreated
 * (:737-808) - STEP rules are NOT - and ListeRoom's orphan sweep destroys
 * whatever was not adopted.
 ******************************************************************************/

TEST_F(AutoScenarioMigrationTest, AStepRuleWhoseConditionValueDoesNotMatchIsDroppedAndThenDestroyed)
{
    /* >>> TO FLIP (E4.6c, RC1) <<<
     *
     * One character of one condition value of the MIDDLE step rule is changed,
     * in the file, from "true" to "false" - an EXCHANGE of the two values the
     * field can take, not a neutralization. Every id still resolves, the rule
     * still loads, the rules engine still holds it.
     *
     * checkScenarioRules() refuses to recognize it (:684), never adopts it, and
     * NOTHING recreates a step rule - the recreation block at :737-808 covers
     * button_start/button_stop/step_end/time_start/time_stop only. The orphan
     * sweep then destroys it and SaveConfigRule() persists that.
     *
     * Net result: ONE character in a file silently costs a whole step, for
     * ever. E4.6c regenerates the rules from the definition instead of
     * recognizing them, so the file content stops being able to do this.
     */
    loadMigrationHouse();

    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createRichScenario(ws));
    }

    saveConfig();
    const std::string ioXml = ioXmlOnDisk();
    const std::string rulesXmlPristine = rulesXmlOnDisk();

    //--- control: pristine file, the three steps are recognized
    clearCoreState();
    loadConfig(ioXml, rulesXmlPristine);
    ListeRoom::Instance().checkAutoScenario();
    {
        AutoScenario *as = autoScenario();
        ASSERT_TRUE(as != nullptr);
        ASSERT_EQ(3u, as->getRuleSteps().size());
        EXPECT_EQ(6u, markedRuleCount());
    }

    //--- treatment: the middle step's is_active condition says "false"
    std::string rulesXmlTuned = rulesXmlPristine;
    ASSERT_TRUE(retuneStepCondition(rulesXmlTuned, "1", "_is_active", "false"))
            << "the middle step rule was not found, the fixture moved";
    ASSERT_NE(rulesXmlPristine, rulesXmlTuned);

    clearCoreState();
    loadConfig(ioXml, rulesXmlTuned);
    ASSERT_EQ(6u, markedRuleCount()) << "all six rules loaded, none of them is broken";

    ListeRoom::Instance().checkAutoScenario();

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    EXPECT_EQ(2u, as->getRuleSteps().size())
            << "TODAY the unrecognized step is simply not adopted";
    EXPECT_EQ(5u, markedRuleCount())
            << "TODAY the orphan sweep destroyed it. E4.6c must keep three steps.";
    EXPECT_EQ(5u, ruleCountOnDisk()) << "and the destruction was persisted";

    //And what survived is NOT the middle step: the third one took its place.
    //(see the renumbering case in section 3)
    WsTestSession ws;
    const Json sc = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});
    ASSERT_EQ(3u, sc["steps"].size()) << "two standard steps plus the synthetic end step";
    EXPECT_EQ("1.5", sc["steps"][0].value("step_pause", std::string()));
    EXPECT_EQ("0.5", sc["steps"][1].value("step_pause", std::string()))
            << "the 2.25s step is gone and the 0.5s one slid up: " << sc.dump();
}

TEST_F(AutoScenarioMigrationTest, AHeaderRuleThatFailsTheMatchIsRecreatedAndTheOriginalDestroyed)
{
    /* >>> TO FLIP (E4.6c, RC1) <<<
     *
     * The other half of the same cause. A HEADER rule that fails the literal
     * match is not adopted either - but this one IS recreated (:737-754), so
     * the scenario ends up with a duplicate for an instant, and then the orphan
     * sweep destroys the original.
     *
     * EXCHANGE: the button_start action on the step IO is moved from "0" to
     * "2" - a value the field legitimately takes elsewhere in this very
     * scenario, not a nonsense marker.
     */
    loadMigrationHouse();

    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createRichScenario(ws));
    }

    saveConfig();
    const std::string ioXml = ioXmlOnDisk();
    std::string rulesXml = rulesXmlOnDisk();

    ASSERT_TRUE(retuneHeaderAction(rulesXml, "button_start", "_step", "0", "2"))
            << "the button_start rule was not found, the fixture moved";

    clearCoreState();
    loadConfig(ioXml, rulesXml);

    Rule *original = findRule(std::string(SCENARIO_MARKER) + "_button_start");
    ASSERT_TRUE(original != nullptr);
    ASSERT_EQ(6u, markedRuleCount());

    ListeRoom::Instance().checkAutoScenario();

    //A button_start is still there - but it is a NEW one, built from scratch
    //by the recreation block, and the original has been destroyed.
    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    ASSERT_TRUE(as->getRuleStart() != nullptr);
    EXPECT_NE(original, as->getRuleStart())
            << "TODAY the original is replaced by a rebuilt duplicate";
    EXPECT_EQ(6u, markedRuleCount()) << "the count is unchanged: one created, one destroyed";
    EXPECT_EQ(3u, as->getRuleSteps().size()) << "the steps were not collateral damage";
}

/*******************************************************************************
 * SECTION 3 - RC2: STEP IDENTITY IS POSITIONAL, AND THE END STEP IS SYNTHETIC
 ******************************************************************************/

TEST_F(AutoScenarioMigrationTest, LosingTheMiddleStepRenumbersEveryStepAfterIt)
{
    /* >>> TO FLIP (E4.6b/c, RC2) <<<
     *
     * The step number is FOUR things at once (E4.6.md §2.4): the index in
     * ruleSteps, the `auto_scenario_step` param, the value compared against the
     * _step IO in the rule condition, and the index in the JSON array. None of
     * them is an identity, and checkScenarioRules() rewrites the third one from
     * the first at :811-829.
     *
     * So destroying the MIDDLE step rule does not leave a hole: the third step
     * BECOMES the second, its condition is rewritten from `== 2` to `== 1`, and
     * any client holding "step 2" now points at different actions.
     * E4.6b gives each step an opaque, stable id and the renumbering disappears.
     */
    loadHealthyScenarioFromDisk();

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    std::vector<Rule *> steps = as->getRuleSteps();
    ASSERT_EQ(3u, steps.size());

    //identify the three by their pause, which is the only thing that tells them
    //apart today - and they were chosen to be all different
    EXPECT_EQ("0", steps[0]->get_param("auto_scenario_step"));
    EXPECT_EQ("1", steps[1]->get_param("auto_scenario_step"));
    EXPECT_EQ("2", steps[2]->get_param("auto_scenario_step"));
    Rule *third = steps[2];

    //destroy the MIDDLE one, the way ListeRule does of its own initiative
    ListeRule::Instance().Remove(steps[1]);

    as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    ASSERT_TRUE(as->checkScenarioRules());

    steps = as->getRuleSteps();
    ASSERT_EQ(2u, steps.size());
    EXPECT_EQ(third, steps[1]) << "the third step object is still there...";

    /* ...and TODAY three of the four numberings of E4.6.md §2.4 disagree about
     * what it is. checkScenarioRules() rewrites the CONDITION (:817) and
     * leaves the PARAM alone, so the very rule that persists as
     * auto_scenario_step="2" only fires when the _step IO reads 1.
     * That divergence is the measurement: a mutation that renumbered both
     * consistently, or neither, fails here.
     */
    const std::string stepIoId = std::string(SCENARIO_MARKER) + "_step";
    EXPECT_EQ("2", third->get_param("auto_scenario_step"))
            << "the param is NOT renumbered";
    EXPECT_EQ("1", conditionValueOn(third, stepIoId))
            << "but the condition IS: " << stepIoId;
    //the surviving first step is unambiguous, both numbering agree on it
    EXPECT_EQ("0", steps[0]->get_param("auto_scenario_step"));
    EXPECT_EQ("0", conditionValueOn(steps[0], stepIoId));

    //the payload agrees: what used to be step 2 now answers at index 1
    WsTestSession ws;
    const Json sc = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});
    ASSERT_EQ(3u, sc["steps"].size());
    EXPECT_EQ("0.5", sc["steps"][1].value("step_pause", std::string()));
    ASSERT_EQ(1u, sc["steps"][1]["actions"].size());
    EXPECT_EQ(IO_BANNER, sc["steps"][1]["actions"][0].value("id", std::string()));
    EXPECT_EQ("bonsoir", sc["steps"][1]["actions"][0].value("action", std::string()));
}

TEST_F(AutoScenarioMigrationTest, TheStepsArrayIsOneLongerThanStepsCountBecauseTheEndStepIsSynthetic)
{
    /* >>> TO FLIP (E4.6d, RC2 / symptom #4) <<<
     *
     * `steps_count` is written from getRuleSteps().size() (IO/Scenario.cpp:141)
     * while the array gains a fourth, SYNTHESIZED entry at :171-190. The
     * invariant is therefore len(steps) == steps_count + 1, and E4.0f's own
     * documentation once got it backwards. E4.6d moves the terminal step to a
     * separate `final_step` field and the trap becomes inexpressible.
     *
     * The end step is also the ONLY one with no `step_pause`: pinned here
     * because it is the shape a client uses to tell the two apart.
     */
    loadHealthyScenarioFromDisk();

    WsTestSession ws;
    const Json sc = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});

    ASSERT_TRUE(sc["steps"].is_array());
    EXPECT_EQ("3", sc.value("steps_count", std::string()));
    EXPECT_EQ(4u, sc["steps"].size()) << "TODAY: three real steps plus the synthetic one";
    EXPECT_EQ(sc["steps"].size(), std::stoul(sc.value("steps_count", std::string("0"))) + 1);

    for (size_t i = 0;i < 3;i++)
    {
        EXPECT_EQ("standard", sc["steps"][i].value("step_type", std::string()));
        EXPECT_TRUE(sc["steps"][i].contains("step_pause")) << "step " << i;
    }
    EXPECT_EQ("end", sc["steps"][3].value("step_type", std::string()));
    EXPECT_FALSE(sc["steps"][3].contains("step_pause"))
            << "the synthetic step carries no pause: " << sc["steps"][3].dump();
}

/*******************************************************************************
 * SECTION 4 - RC3: toJson() PUBLISHES RESOLVED POINTERS, SO IT ESCAMOTES
 *
 * ScenarioAction carries an IOBase*, filled from ActionStd::get_output(0),
 * which answers nullptr the moment the id stops resolving. isScenarioInternalIO
 * (nullptr) answers TRUE (AutoScenario.cpp:919-922), so "IO not found" is
 * indistinguishable from "machinery IO, nothing to show". Both guards -
 * IO/Scenario.cpp:158 for the standard steps and :181 for the synthetic end
 * step - then drop the action from the payload.
 * The id is still there (ActionStd::get_output_id()); it is simply never read
 * by the scenario path.
 ******************************************************************************/

TEST_F(AutoScenarioMigrationTest, AStepIsRenderedWithoutTheActionWhoseIoIsGoneAndOnlyMissingIosNamesIt)
{
    /* >>> TO FLIP (E4.6d, RC3) <<<   IO/Scenario.cpp:158
     *
     * The middle step had THREE actions and its middle one lost its IO. The
     * payload answers TWO, in the same order, with no gap, no null, no marker.
     * The dead id appears in exactly ONE place in the whole document:
     * missing_ios. The payload therefore names the missing IO in one field and
     * has erased it from the other - the golden of E4.0c freezes that same
     * inconsistency (e40c_ws_autoscenario_get_broken.json).
     *
     * E4.6d keeps the action and adds "resolved":"false" (D4).
     */
    loadScenarioWithTwoAmputatedActions();

    ASSERT_TRUE(io(IO_GHOST_STEP) == nullptr);
    ASSERT_TRUE(io(IO_GHOST_END) == nullptr);

    WsTestSession ws;
    const Json sc = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});

    ASSERT_EQ(4u, sc["steps"].size());
    const Json middle = sc["steps"][1];
    EXPECT_EQ("2.25", middle.value("step_pause", std::string()))
            << "wrong step, the payload moved: " << sc.dump();

    ASSERT_EQ(2u, middle["actions"].size())
            << "TODAY the amputated step answers 2 of its 3 actions: " << middle.dump();
    EXPECT_EQ(IO_BLIND, middle["actions"][0].value("id", std::string()));
    EXPECT_EQ("77", middle["actions"][0].value("action", std::string()));
    EXPECT_EQ(IO_VOLUME, middle["actions"][1].value("id", std::string()));
    EXPECT_EQ("42", middle["actions"][1].value("action", std::string()));

    //the dead id is nowhere in the steps, only in missing_ios
    for (const Json &step: sc["steps"])
        for (const Json &act: step["actions"])
            EXPECT_NE(IO_GHOST_STEP, act.value("id", std::string()))
                    << "the dead id came back into a step: " << step.dump();

    //E4.2e format, "id_a, id_b", steps first then the header rules
    EXPECT_EQ(std::string(IO_GHOST_STEP) + ", " + IO_GHOST_END,
              sc.value("missing_ios", std::string()));
    EXPECT_EQ("true", sc.value("broken", std::string()));
    EXPECT_EQ("true", sc.value("disabled_missing_io", std::string()));
}

TEST_F(AutoScenarioMigrationTest, TheSyntheticEndStepEscamotesItsDeadActionTheSameWay)
{
    /* >>> TO FLIP (E4.6d, RC3) <<<   IO/Scenario.cpp:181
     *
     * The SECOND guard, in the synthetic end step's own loop. It is a separate
     * site and it needs its own witness: a mutation removing only one of the
     * two would otherwise stay green.
     * The end step had THREE actions, its MIDDLE one is dead, and the two
     * survivors target the SAME IO (LAMP) with a different value than the one
     * step 1 uses on it - so a payload confusing ids with values fails here.
     */
    loadScenarioWithTwoAmputatedActions();

    WsTestSession ws;
    const Json sc = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});

    ASSERT_EQ(4u, sc["steps"].size());
    const Json end = sc["steps"][3];
    ASSERT_EQ("end", end.value("step_type", std::string()));

    ASSERT_EQ(2u, end["actions"].size())
            << "TODAY the end step answers 2 of its 3 actions: " << end.dump();
    EXPECT_EQ(IO_SIREN, end["actions"][0].value("id", std::string()));
    EXPECT_EQ("false", end["actions"][0].value("action", std::string()));
    EXPECT_EQ(IO_LAMP, end["actions"][1].value("id", std::string()));
    EXPECT_EQ("false", end["actions"][1].value("action", std::string()))
            << "LAMP is set to true by step 0 and to false here";

    //and step 0 still says "true" on the same IO
    EXPECT_EQ(IO_LAMP, sc["steps"][0]["actions"][0].value("id", std::string()));
    EXPECT_EQ("true", sc["steps"][0]["actions"][0].value("action", std::string()));
}

/*******************************************************************************
 * SECTION 5 - THE REFERENCE USE CASE, END TO END
 *
 * E4.6.md §3, RC3, "le cas d'usage de reference". Measured by the independent
 * review of T3.20, never frozen anywhere until now. It is the scenario the
 * redesign has to make IMPOSSIBLE BY CONSTRUCTION, not by an added guard.
 ******************************************************************************/

TEST_F(AutoScenarioMigrationTest, ReadingBackAndEchoingThePayloadRestartsAnAmputatedScenarioWithTwoSuccessTrue)
{
    /* >>> TO FLIP (E4.6d, RC3 + RC5) <<<   defect (d) of E4.6.md §6.
     *
     * The user does nothing wrong. They read, and they send back what they
     * read. Four steps, all of them answered by production today:
     *
     *   1. the IO of the middle step is gone -> broken, sticky flag set
     *   2. `get`                             -> the action is ESCAMOTEE
     *   3. that payload, VERBATIM, to `modify` -> {"success":"true"},
     *      isBroken() FALSE, missing_ios EMPTY
     *   4. `reenable`                        -> {"success":"true"}, and the
     *      scenario RUNS AGAIN, permanently amputated
     *
     * Verbatim means verbatim: the echo below is the document `get` answered
     * plus the single key `type`. Nothing else is added - adding "name" or the
     * room, as an existing case in JsonApiScenario_test does, hides half of
     * RC5 behind a workaround no real client would know to apply.
     *
     * The acceptance criterion of E4.6 (§11.3) is the exact reverse of every
     * assertion below: step 3 must answer the SAME document as step 2, `broken`
     * must stay TRUE, and step 4 must REFUSE.
     */
    loadScenarioWithTwoAmputatedActions();

    //--- 1. the state after the amputation
    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    ASSERT_TRUE(as->isBroken());
    ASSERT_TRUE(as->isDisabledMissingIo());

    //the button is refused while it is broken - the control for step 4
    ASSERT_EQ(0, runScenario()) << "a broken scenario must not run";
    EXPECT_FALSE(io(IO_BLIND)->get_value_bool());

    WsTestSession ws;

    //--- 2. get: the payload the UI receives
    Json echo = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});
    ASSERT_EQ(4u, echo["steps"].size());
    ASSERT_EQ(2u, echo["steps"][1]["actions"].size()) << "the amputation, see section 4";
    EXPECT_EQ(std::string(IO_GHOST_STEP) + ", " + IO_GHOST_END,
              echo.value("missing_ios", std::string()));

    //--- 3. the SAME document, plus "type", back into modify
    const Json sentVerbatim = echo;
    echo["type"] = "modify";
    EXPECT_JSON_EQ(std::string(R"({"success":"true"})"), wsAutoscenario(ws, echo));

    as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    EXPECT_FALSE(as->isBroken())
            << "TODAY the round trip whitewashes the scenario. E4.6d must keep it broken.";
    EXPECT_EQ("", as->getMissingIoDescription());

    //the two dead references are gone from the file, for ever
    saveConfig();
    EXPECT_EQ(std::string::npos, rulesXmlOnDisk().find(IO_GHOST_STEP));
    EXPECT_EQ(std::string::npos, rulesXmlOnDisk().find(IO_GHOST_END));

    //RC5, measured in passing: the echo also renamed and hid the scenario,
    //because `get` emits neither name nor visible and `modify` reads both.
    Scenario *sc = scenarioIo();
    ASSERT_TRUE(sc != nullptr);
    EXPECT_NE("Soir\xc3\xa9""e", sc->get_param("name"))
            << "the name survived the echo, which today it must not";
    EXPECT_EQ("false", sc->get_param("visible"));

    /* --- 4. reenable: accepted, and the mechanism is now asserted rather than
     * asserted-about. The review proposed that `modify` had already cleared the
     * flag, which would make tryReenable() take its no-op branch
     * (AutoScenario.cpp:288-295). MEASURED HERE, AND IT IS NOT WHAT HAPPENS:
     * `modify` leaves the flag SET - that is exactly what
     * ScenarioDisabledMissingIo_test::ModifyDoesNotClearTheDisabledFlag pins -
     * so tryReenable() reaches setDisabledMissingIo(false) and really does lift
     * gate 2, only because `isBroken()` has been whitewashed at step 3.
     * The two EXPECTs below are an exchange across the single call, so neither
     * reading can be mistaken for the other any more.
     * Acceptance criterion, unchanged: after E4.6d this command must REFUSE,
     * naming the ids (§11.3, step 4).
     */
    EXPECT_TRUE(as->isDisabledMissingIo())
            << "measured: `modify` does NOT clear gate 2, it is still set here";
    EXPECT_JSON_EQ(std::string(R"({"success":"true"})"),
                   wsAutoscenario(ws, Json{{ "type", "reenable" }, { "id", SCENARIO_IO_ID }}));
    as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    EXPECT_FALSE(as->isDisabledMissingIo())
            << "TODAY reenable really lifts gate 2 on an amputated scenario, "
               "because step 3 whitewashed gate 1. E4.6d must make it REFUSE.";

    //--- and the scenario really runs again, amputated.
    //Put the ghost IO back first: if the action had survived anywhere, this is
    //where it would show. It does not - the reference is gone from the rules.
    ASSERT_TRUE(createInternalIO("InternalString", IO_GHOST_STEP, "Fantome ressuscite") != nullptr);

    EXPECT_EQ(3, runScenario()) << "the amputated scenario is armed again";
    EXPECT_EQ(77, (int)io(IO_BLIND)->get_value_double());
    EXPECT_EQ(42, (int)io(IO_VOLUME)->get_value_double());
    EXPECT_EQ("bonsoir", io(IO_BANNER)->get_value_string());
    EXPECT_EQ("", io(IO_GHOST_STEP)->get_value_string())
            << "the step lost its action for good, and nothing anywhere says so";

    //the document the client sent is still, verbatim, a document production
    //accepted: kept so a reviewer can see what was echoed
    EXPECT_TRUE(sentVerbatim.contains("enabled"));
    EXPECT_FALSE(sentVerbatim.contains("disabled"));
}

/*******************************************************************************
 * SECTION 6 - RC5: THE READ SCHEMA AND THE WRITE SCHEMA ARE NOT THE SAME
 ******************************************************************************/

TEST_F(AutoScenarioMigrationTest, FiveFieldsAreReadByModifyAndNeverEmittedByGet)
{
    /* >>> TO FLIP (E4.6d, RC5) <<<
     *
     * toJson() emits 10 keys (IO/Scenario.cpp:115-193);
     * buildAutoscenarioModify() reads 8 (JsonApi.cpp:2026-2045), FIVE of which
     * `get` never emits: name, visible, disabled, room_name, room_type. And
     * `enabled` (emitted) is the NEGATION of `disabled` (read) under another
     * name. The round trip of this API is not a round trip.
     *
     * MEASURED, not read off the source: each of the five is sent with a value
     * that cannot be mistaken for a default, its effect is observed, and the
     * payload is then checked to see whether `get` gives it back. It does not.
     */
    loadHealthyScenarioFromDisk();

    WsTestSession ws;
    const Json before = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});

    //what `get` emits, exactly - the ten keys
    const std::vector<std::string> emitted = {
        "id", "cycle", "enabled", "schedule", "category",
        "broken", "disabled_missing_io", "missing_ios",
        "steps_count", "steps" };
    for (const std::string &k: emitted)
        EXPECT_TRUE(before.contains(k)) << "get no longer emits " << k;
    EXPECT_EQ(emitted.size(), before.size()) << before.dump();

    //a second room, so room_name/room_type can be observed changing
    ASSERT_TRUE(addRoom("E4.6a annexe", "chambre") != nullptr);

    Json modify = before;
    modify["type"] = "modify";
    modify["name"] = "Nom pose par modify";
    modify["visible"] = "true";
    modify["disabled"] = "false";
    modify["room_name"] = "E4.6a annexe";
    modify["room_type"] = "chambre";
    EXPECT_JSON_EQ(std::string(R"({"success":"true"})"), wsAutoscenario(ws, modify));

    //all five landed
    Scenario *sc = scenarioIo();
    ASSERT_TRUE(sc != nullptr);
    EXPECT_EQ("Nom pose par modify", sc->get_param("name"));
    EXPECT_EQ("true", sc->get_param("visible"));
    EXPECT_EQ("false", sc->get_param("disabled"));
    Room *room = ListeRoom::Instance().getRoomByIO(sc);
    ASSERT_TRUE(room != nullptr);
    EXPECT_EQ("E4.6a annexe", room->get_name());
    EXPECT_EQ("chambre", room->get_type());

    //and none of the five comes back
    const Json after = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});
    for (const char *k: { "name", "visible", "disabled", "room_name", "room_type" })
        EXPECT_FALSE(after.contains(k))
                << "TODAY get is silent about " << k << ". E4.6d must emit it.";
    EXPECT_EQ(emitted.size(), after.size()) << after.dump();

    //`enabled` is the negation of `disabled`, under another name
    EXPECT_EQ("true", after.value("enabled", std::string()));
    EXPECT_EQ("false", sc->get_param("disabled"));
}

/*******************************************************************************
 * SECTION 7 - THE "THIRD PAIR": broken TRUE with missing_ios EMPTY
 *
 * The pair broken/missing_ios agrees in every ordinary case, which is why a
 * mutation neutralizing isDangling() once left FIVE binaries green
 * (FINDINGS.md, "la troisieme paire de la trappe d'E4.0c"). The one state where
 * they DISAGREE is a rule DESTROYED under the scenario: there is no id left to
 * name, because getMissingIoDescription() (:212-247) only collects through
 * ref.get() and that answers null.
 ******************************************************************************/

TEST_F(AutoScenarioMigrationTest, ARuleDestroyedUnderTheScenarioMakesThePayloadSayBrokenWithNoMissingId)
{
    /* >>> TO FLIP (E4.6d, RC1 + RC3) <<<
     *
     * ScenarioDisabledMissingIo_test:432 pins this at the model level and E4.6d
     * DELETES that case, because D4 makes the state unreachable. The witness is
     * repeated here at the PAYLOAD level, which is where a client sees it and
     * where nothing covered it: `broken`:"true" together with `missing_ios`:"".
     *
     * When E4.6 lands, "broken implies missing_ios non empty" holds without
     * exception and this case has to be flipped or removed - deliberately.
     */
    loadHealthyScenarioFromDisk();

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    ASSERT_FALSE(as->isBroken());

    std::vector<Rule *> steps = as->getRuleSteps();
    ASSERT_EQ(3u, steps.size());

    //ListeRule owns the rules and destroys them of its own initiative (E4.2d)
    ListeRule::Instance().Remove(steps[1]);

    WsTestSession ws;
    const Json sc = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});

    EXPECT_EQ("true", sc.value("broken", std::string()))
            << "gate 1 must survive the serialization: " << sc.dump();
    EXPECT_EQ("", sc.value("missing_ios", std::string()))
            << "TODAY there is no id to name. E4.6 makes this state inexpressible.";

    //and the step really is gone from the payload, silently
    EXPECT_EQ("2", sc.value("steps_count", std::string()));
    ASSERT_EQ(3u, sc["steps"].size());
    EXPECT_EQ("0.5", sc["steps"][1].value("step_pause", std::string()));
}

TEST_F(AutoScenarioMigrationTest, ThePayloadTellsTheFourStatesApartAndTwoOfThemDisagree)
{
    /* >>> TO FLIP (E4.6d, RC3/T3.18) <<<  - and this case exists because the
     * REVIEW of E4.6a found the hole it fills.
     *
     * `broken`, `disabled_missing_io` and `missing_ios` agree in the two
     * ordinary states, so a mutation making the FLAG a pure function of the
     * non-emptiness of missing_ios - which destroys exactly the distinction
     * T3.18 exists for - left the first version of this file 0/18 RED.
     * MEASURED by the reviewer, and reproduced here before writing this case.
     *
     * The state that catches it is the third one below, and no other case of
     * this file reached it: `broken`:"false" + `disabled_missing_io`:"true" +
     * `missing_ios`:"" - "repaired, waiting for a manual re-enable", the state
     * the user asked T3.18 for.
     *
     * ⚠️ THE ROUTE MATTERS. `modify` does NOT get you there: it rebuilds the
     * rules from a payload that no longer names the dead IO, so `broken` and
     * the FLAG both end up false (that is the reference use case, section 5).
     * The route that works is the one of
     * ScenarioDisabledMissingIo_test.cpp:971-981: amputate, reload, PUT THE IO
     * BACK, save and reload again. The flag is persisted and sticky, the
     * breakage is recomputed at load and is gone.
     *
     * ⚠️ THIS PROTECTION IS OWNED BY THE TICKET THAT DEMOLISHES IT. The only
     * other witnesses of the same distinction are
     * ScenarioDisabledMissingIo_test::ThePayloadTellsTheFourStatesApart and
     * two cases of JsonApiScenario_test - all three inside E4.6d's rewrite
     * perimeter (§8.2). E4.6d must carry this discrimination over into the new
     * schema BEFORE rewriting them, not after.
     */
    const std::string scenarioId = loadScenarioWithTwoAmputatedActions();
    ASSERT_FALSE(scenarioId.empty());

    WsTestSession ws;

    /* --- state 2: broken and freshly disabled -> true / true / the two ids ---
     * (state 1, healthy, is the control at the end of this case: it needs a
     * scenario that was NEVER broken, and this one has been)
     */
    {
        const Json j = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", scenarioId }});
        EXPECT_EQ("true", j.value("broken", std::string()));
        EXPECT_EQ("true", j.value("disabled_missing_io", std::string()));
        EXPECT_EQ(std::string(IO_GHOST_STEP) + ", " + IO_GHOST_END,
                  j.value("missing_ios", std::string()));
    }

    /* --- state 3: REPAIRED, WAITING FOR A RE-ENABLE -> FALSE / TRUE / "" ----
     * The two boolean keys DISAGREE here, and nowhere else in this direction.
     * Put both IOs back and go through a real save/reload: the breakage is
     * recomputed from the file and is gone, the flag is read back from io.xml
     * and stays.
     */
    ASSERT_TRUE(createInternalIO("InternalString", IO_GHOST_STEP, "Fantome revenu") != nullptr);
    ASSERT_TRUE(createInternalIO("InternalBool", IO_GHOST_END, "Fantome de fin revenu") != nullptr);

    saveConfig();
    ASSERT_NE(std::string::npos, ioXmlOnDisk().find("disabled_missing_io=\"true\""))
            << "the sticky flag is not in io.xml, the reload cannot show it";

    clearCoreState();
    loadConfig(ioXmlOnDisk(), rulesXmlOnDisk());
    ListeRoom::Instance().checkAutoScenario();

    {
        WsTestSession repaired;
        const Json j = wsAutoscenario(repaired, Json{{ "type", "get" }, { "id", scenarioId }});
        EXPECT_EQ("false", j.value("broken", std::string()))
                << "the ids resolve again: " << j.dump();
        EXPECT_EQ("true", j.value("disabled_missing_io", std::string()))
                << "STICKY. Only an explicit reenable clears it - this is the state "
                   "T3.18 exists to make visible, and a payload that derives the flag "
                   "from missing_ios cannot express it.";
        EXPECT_EQ("", j.value("missing_ios", std::string()));

        //and the scenario really is still held: the gate is the flag alone
        AutoScenario *as = autoScenario();
        ASSERT_TRUE(as != nullptr);
        EXPECT_FALSE(as->isBroken());
        EXPECT_TRUE(as->isDisabledMissingIo());
        EXPECT_EQ(0, runScenario()) << "gate 2 alone must still hold it back";
    }

    /* --- state 4: broken with the flag cleared by hand -> TRUE / FALSE / ids -
     * the other disagreement, so neither key can hide behind the other.
     */
    {
        /* ORDER MATTERS, and it was measured the wrong way round first:
         * ListeRoom::deleteIO() runs refreshBrokenScenarios() itself, which
         * re-arms the flag. So break it FIRST, then clear the flag by hand -
         * the same sequence ScenarioDisabledMissingIo_test uses.
         */
        ASSERT_TRUE(deleteIO(io(IO_GHOST_STEP)));
        AutoScenario *as = autoScenario();
        ASSERT_TRUE(as != nullptr);
        ASSERT_TRUE(as->isBroken());
        as->setDisabledMissingIo(false);

        WsTestSession halfBroken;
        const Json j = wsAutoscenario(halfBroken, Json{{ "type", "get" }, { "id", scenarioId }});
        EXPECT_EQ("true", j.value("broken", std::string()));
        EXPECT_EQ("false", j.value("disabled_missing_io", std::string()))
                << "cleared by hand, and nothing has re-flagged it yet: " << j.dump();
        EXPECT_NE("", j.value("missing_ios", std::string()));
    }

    /* --- state 1: healthy -> false / false / "" -----------------------------
     * The control, on a scenario that was never broken. Without it the three
     * states above could all be reached by a payload that always says the same
     * thing.
     */
    {
        clearCoreState();
        loadMigrationHouse();
        WsTestSession healthy;
        ASSERT_EQ(SCENARIO_IO_ID, createRichScenario(healthy));
        const Json j = wsAutoscenario(healthy, Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});
        EXPECT_EQ("false", j.value("broken", std::string()));
        EXPECT_EQ("false", j.value("disabled_missing_io", std::string()));
        EXPECT_EQ("", j.value("missing_ios", std::string()));
    }
}

/*******************************************************************************
 * SECTION 8 - RC9: THE AMPUTATION VECTOR THAT DOES NOT GO THROUGH THE API
 *
 * calaos_installer drops an action output whose id does not resolve at load
 * time (projectmanager.cpp:1126-1133), regenerates io.xml and rules.xml in
 * full, and uploads both. The server then receives a coherent pair of files
 * from which the reference has simply disappeared.
 ******************************************************************************/

TEST_F(AutoScenarioMigrationTest, AnInstallerStyleReloadThatDroppedTheDeadOutputLeavesNoTraceAtAll)
{
    /* >>> TO FLIP (E4.6b/c, RC9) <<<   defect (e) of E4.6.md §6.
     *
     * The EXCHANGE that gives this case its teeth: the SAME amputation is
     * loaded twice, and the only difference is whether rules.xml still names
     * the dead IO.
     *   - reference kept   -> broken, flag set, missing_ios names it (section 4)
     *   - reference dropped -> broken FALSE, flag FALSE, missing_ios EMPTY, one
     *     action fewer, and NOTHING anywhere records that a step was amputated.
     *
     * This is the vector that matters in production, and it is the one no API
     * guard can see. E4.6 closes it by moving the definition out of rules.xml
     * (D1/D2) and regenerating the rules at every load (D10 level 0).
     */
    loadMigrationHouse();

    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createRichScenario(ws));
    }
    saveConfig();

    std::string ioXml = ioXmlOnDisk();
    const std::string rulesKept = rulesXmlOnDisk();
    ASSERT_TRUE(removeIoFromXml(ioXml, IO_GHOST_STEP));

    //--- control: the installer did NOT touch rules.xml
    clearCoreState();
    loadConfig(ioXml, rulesKept);
    ListeRoom::Instance().checkAutoScenario();
    {
        WsTestSession ws;
        const Json sc = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});
        EXPECT_EQ("true", sc.value("broken", std::string()));
        EXPECT_EQ("true", sc.value("disabled_missing_io", std::string()));
        EXPECT_EQ(IO_GHOST_STEP, sc.value("missing_ios", std::string()));
        EXPECT_EQ(2u, sc["steps"][1]["actions"].size());
    }

    //--- treatment: the installer regenerated rules.xml without the dead output
    std::string rulesDropped = rulesKept;
    ASSERT_EQ(1, dropActionOutputFromRulesXml(rulesDropped, IO_GHOST_STEP));
    ASSERT_EQ(std::string::npos, rulesDropped.find(IO_GHOST_STEP));

    clearCoreState();
    loadConfig(ioXml, rulesDropped);
    ListeRoom::Instance().checkAutoScenario();

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr);
    EXPECT_FALSE(as->isBroken())
            << "TODAY the server sees a perfectly healthy scenario";
    EXPECT_FALSE(as->isDisabledMissingIo());
    EXPECT_EQ(3u, as->getRuleSteps().size()) << "the step itself is still there";

    WsTestSession ws;
    const Json sc = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});
    EXPECT_EQ("false", sc.value("broken", std::string()));
    EXPECT_EQ("false", sc.value("disabled_missing_io", std::string()));
    EXPECT_EQ("", sc.value("missing_ios", std::string()));
    //two of three actions, exactly like the amputated case - and nothing at all
    //to tell the two apart
    ASSERT_EQ(2u, sc["steps"][1]["actions"].size()) << sc["steps"][1].dump();
    EXPECT_EQ(IO_BLIND, sc["steps"][1]["actions"][0].value("id", std::string()));
    EXPECT_EQ(IO_VOLUME, sc["steps"][1]["actions"][1].value("id", std::string()));

    //and it runs, amputated, with every gate open
    EXPECT_EQ(3, runScenario());
    EXPECT_EQ(77, (int)io(IO_BLIND)->get_value_double());
    EXPECT_EQ(42, (int)io(IO_VOLUME)->get_value_double());
}

/*******************************************************************************
 * SECTION 9 - D10 LEVEL 1: THE BACKUP BEFORE THE OVERWRITE
 *
 * NOT a defect. An acquired property that nothing attested until now
 * (E4.6.md §4, D10 level 1): JsonApiHandlerHttp.cpp:624 calls
 * Config::BackupFiles() BEFORE writing any received file (:649-655).
 * These two cases are here to be PROVEN and then kept, never flipped.
 ******************************************************************************/

TEST_F(AutoScenarioMigrationTest, ConfigPutBacksUpTheConfigurationBeforeOverwritingIt)
{
    /* ✅ PROVE, DO NOT FLIP - defect-free half of E4.6a, (f) of E4.6.md §6.
     *
     * The ORDER is what is measured, and it is measured by content, not by
     * reading the source: after the put, the file on disk carries the NEW
     * content and the backup carries the OLD one. If BackupFiles() ran after
     * the write, both would carry the new content and this case would fail.
     */
    loadMigrationHouse();

    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createRichScenario(ws));
    }
    saveConfig();

    const std::string ioBefore = ioXmlOnDisk();
    const std::string rulesBefore = rulesXmlOnDisk();
    ASSERT_NE(std::string::npos, ioBefore.find(SCENARIO_MARKER));
    ASSERT_NE(std::string::npos, rulesBefore.find(SCENARIO_MARKER));
    ASSERT_TRUE(backupsOf("io.xml").empty()) << "the case starts with no backup";

    //A destructive upload: an empty house and no rule at all. This is exactly
    //the shape of a configuration an installer would send after losing
    //everything it did not model.
    const std::string ioAfter = ioXmlDocument(roomXml("Vide", "salon", std::string(), 0));
    const std::string rulesAfter = rulesXmlDocument(std::string());

    {
        HttpTestRequest req;
        req.send(authenticated(Json{
            { "action", "config" }, { "type", "put" },
            { "config_files", Json{{ "io.xml", ioAfter }, { "rules.xml", rulesAfter }} }}));

        ASSERT_EQ(1u, req.count());
        EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
        EXPECT_EQ("true", str(req.bodyJson(), "success"));
    }

    //the overwrite happened
    EXPECT_EQ(std::string::npos, ioXmlOnDisk().find(SCENARIO_MARKER));
    EXPECT_EQ(std::string::npos, rulesXmlOnDisk().find(SCENARIO_MARKER));

    //and the backup taken BEFORE it holds what was destroyed
    const std::vector<std::string> ioBackups = backupsOf("io.xml");
    const std::vector<std::string> ruleBackups = backupsOf("rules.xml");
    ASSERT_EQ(1u, ioBackups.size()) << "no backup under " << configDir() << "/backups";
    ASSERT_EQ(1u, ruleBackups.size());
    EXPECT_EQ(ioBefore, readWholeFile(ioBackups[0]))
            << "the backup does not hold the PRE-put io.xml: BackupFiles() ran too late";
    EXPECT_EQ(rulesBefore, readWholeFile(ruleBackups[0]));
}

TEST_F(AutoScenarioMigrationTest, TheBackupLeftByConfigPutIsUsableToGetTheLostScenarioBack)
{
    /* ✅ PROVE, DO NOT FLIP - the other half of (f). A backup nobody can load
     * is not a backup, so the property is proven by USING it: the two files are
     * copied back over the configuration and reloaded through the real
     * Config path, and the scenario comes back whole - three steps, the right
     * pauses, the right actions.
     */
    loadMigrationHouse();

    {
        WsTestSession ws;
        ASSERT_EQ(SCENARIO_IO_ID, createRichScenario(ws));
    }
    saveConfig();

    {
        HttpTestRequest req;
        req.send(authenticated(Json{
            { "action", "config" }, { "type", "put" },
            { "config_files", Json{
                { "io.xml", ioXmlDocument(roomXml("Vide", "salon", std::string(), 0)) },
                { "rules.xml", rulesXmlDocument(std::string()) }} }}));
        ASSERT_EQ(1u, req.count());
        ASSERT_EQ("true", str(req.bodyJson(), "success"));
    }

    const std::vector<std::string> ioBackups = backupsOf("io.xml");
    const std::vector<std::string> ruleBackups = backupsOf("rules.xml");
    ASSERT_EQ(1u, ioBackups.size());
    ASSERT_EQ(1u, ruleBackups.size());

    //restore, the way an operator does
    clearCoreState();
    loadConfig(readWholeFile(ioBackups[0]), readWholeFile(ruleBackups[0]));
    ListeRoom::Instance().checkAutoScenario();

    AutoScenario *as = autoScenario();
    ASSERT_TRUE(as != nullptr) << "the restored configuration has no auto scenario";
    EXPECT_EQ(3u, as->getRuleSteps().size());

    WsTestSession ws;
    const Json sc = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", SCENARIO_IO_ID }});
    ASSERT_EQ(4u, sc["steps"].size());
    EXPECT_EQ("1.5", sc["steps"][0].value("step_pause", std::string()));
    EXPECT_EQ("2.25", sc["steps"][1].value("step_pause", std::string()));
    EXPECT_EQ("0.5", sc["steps"][2].value("step_pause", std::string()));
    ASSERT_EQ(3u, sc["steps"][1]["actions"].size());
    EXPECT_EQ("fantome", sc["steps"][1]["actions"][1].value("action", std::string()));
    EXPECT_EQ("false", sc.value("broken", std::string()));
}

/*******************************************************************************
 * SECTION 10 - WHAT io.xml REALLY CARRIES TODAY, AND WHY PARAMS ARE THE PIVOT
 *
 * E4.6.md D2 / DECISIONS.md 2026-08-24: the definition is going to live in
 * io.xml, carried by PARAMS of the Scenario IO - because calaos_installer
 * preserves params it knows nothing about and loses child nodes. These two
 * cases record the starting point and the property the decision rests on.
 ******************************************************************************/

TEST_F(AutoScenarioMigrationTest, TodayIoXmlCarriesOnlyTheMarkerAndTheDerivedInternalIos)
{
    /* ✅ FLIPPED BY E4.6b (D2), 2026-09-01 - and the flip IS the proof.
     *
     * E4.6a filed this as ">>> TO FLIP (E4.6b, D2) <<<" and predicted, word for
     * word, that "the needles below - 2.25, fantome, bonsoir - are going to
     * appear there as as_s2_pause / as_s2_actions". They do. The assertions
     * below are the SAME MEASUREMENT with the expected value turned around, as
     * the header of this file requires: nothing is weakened, the direction is
     * reversed and the reason is written down.
     *
     * What stays untouched, and it matters more than the flip: the LEGACY
     * marker `auto_scenario` is still there and still what builds the
     * AutoScenario. `autoscenario_uid` lives NEXT TO it, in a namespace of its
     * own. That separation is what keeps the orphan sweep of ListeRoom.cpp:324
     * from finding an unadopted rule (E4.6.md §5.2), and it is why the three
     * internal IOs below still carry the old marker too.
     *
     * The name is kept on purpose: this case is cited by name in E4.6.md §8.3
     * and in the E4.6b brief, and renaming it would break the link between the
     * table and the file.
     */
    loadHealthyScenarioFromDisk();

    const std::string xml = ioXmlOnDisk();

    //the marker, on the Scenario IO
    Scenario *sc = scenarioIo();
    ASSERT_TRUE(sc != nullptr);
    EXPECT_EQ(SCENARIO_MARKER, sc->get_param("auto_scenario"));
    EXPECT_EQ("scenario", sc->get_param("type"));
    EXPECT_EQ("false", sc->get_param("cycle"));
    EXPECT_TRUE(sc->get_params().Exists("disabled"));

    //three internal IOs, ids derived from the marker, all carrying it too
    for (const char *suffix: { "_is_active", "_step", "_timer" })
    {
        const std::string id = std::string(SCENARIO_MARKER) + suffix;
        IOBase *internal = io(id);
        ASSERT_TRUE(internal != nullptr) << "missing internal IO " << id;
        EXPECT_EQ(SCENARIO_MARKER, internal->get_param("auto_scenario"));
        EXPECT_EQ("false", internal->get_param("visible"));
    }
    //not scheduled: no _schedule / _is_schedule_enabled
    EXPECT_TRUE(io(std::string(SCENARIO_MARKER) + "_schedule") == nullptr);
    EXPECT_TRUE(io(std::string(SCENARIO_MARKER) + "_is_schedule_enabled") == nullptr);

    /* THE FLIP. io.xml now carries the DEFINITION: the pauses and the action
     * values are in it, under `as_<step>_pause` / `as_<step>_actions` (D2).
     */
    for (const char *needle: { "fantome", "bonsoir", "2.25",
                               "autoscenario_uid", "autoscenario_steps" })
        EXPECT_NE(std::string::npos, xml.find(needle))
                << "io.xml does not carry " << needle << ": D2 did not land";

    //and the uid is the NEW marker, next to - never instead of - the old one
    EXPECT_TRUE(sc->param_exists("autoscenario_uid"));
    EXPECT_EQ(SCENARIO_MARKER, sc->get_param("auto_scenario"))
            << "the legacy marker was re-keyed: the orphan sweep is now armed";

    /* `auto_scenario_step` is the PROJECTION's numbering (E4.6.md §2.4(b)) and
     * it stays in rules.xml only: the definition addresses its steps by opaque
     * id (D3), never by position.
     */
    EXPECT_EQ(std::string::npos, xml.find("auto_scenario_step"))
            << "io.xml carries the positional step numbering, which D3 removes";

    //rules.xml still carries all of it too - E4.6b does not touch the
    //generator, so the rules are still the thing that runs (that is E4.6c)
    const std::string rules = rulesXmlOnDisk();
    EXPECT_NE(std::string::npos, rules.find("fantome"));
    EXPECT_NE(std::string::npos, rules.find("auto_scenario_step"));
}

TEST_F(AutoScenarioMigrationTest, AnUnknownParamOfTheScenarioIoSurvivesASaveReloadCycle)
{
    /* ✅ PROVE, DO NOT FLIP - the property the io.xml decision rests on
     * (DECISIONS.md, 2026-08-24; E4.6.md D2). The server keeps every attribute
     * it does not model, verbatim, across a full save/reload cycle - which is
     * what makes "carry the definition in params" viable at all.
     *
     * The probe name is deliberately OUTSIDE the namespaces E4.6 will populate
     * (`autoscenario_` and `as_`): E4.0c once used "reenable" as an unknown
     * command probe and T3.18 then shipped that command, turning the canary
     * into a valid value without ever reddening the test.
     * The probe VALUE carries the three characters D2 will have to
     * percent-encode ('|', '=', '%'), so E4.6b inherits a witness that they
     * survive the XML layer untouched today.
     */
    loadHealthyScenarioFromDisk();

    saveConfig();
    std::string ioXml = ioXmlOnDisk();
    const std::string rulesXml = rulesXmlOnDisk();
    ASSERT_TRUE(addParamToScenarioIoInXml(ioXml, PROBE_PARAM, PROBE_VALUE));

    clearCoreState();
    loadConfig(ioXml, rulesXml);

    Scenario *sc = scenarioIo();
    ASSERT_TRUE(sc != nullptr);
    ASSERT_TRUE(sc->getAutoScenario() != nullptr) << "the marker still works";
    EXPECT_EQ(PROBE_VALUE, sc->get_param(PROBE_PARAM))
            << "an unmodelled param was dropped at load";

    //a full startup pass, which ends with SaveConfigIO() (ListeRoom.cpp:340) -
    //the very moment a param nobody reads back would be erased
    ListeRoom::Instance().checkAutoScenario();
    saveConfig();

    EXPECT_NE(std::string::npos, ioXmlOnDisk().find(PROBE_PARAM))
            << "the unmodelled param was erased by the startup save";
    Scenario *reread = scenarioIo();
    ASSERT_TRUE(reread != nullptr);
    EXPECT_EQ(PROBE_VALUE, reread->get_param(PROBE_PARAM));

    //and one more full cycle through the file, to be sure it is the FILE that
    //carries it and not a live object
    const std::string ioAgain = ioXmlOnDisk();
    clearCoreState();
    loadConfig(ioAgain, rulesXmlOnDisk());
    ASSERT_TRUE(scenarioIo() != nullptr);
    EXPECT_EQ(PROBE_VALUE, scenarioIo()->get_param(PROBE_PARAM));
}
