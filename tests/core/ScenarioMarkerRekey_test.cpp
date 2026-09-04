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
 * T3.61 - THE CONTRACT OF E4.6.md §5.3, ON THE SHAPE OF THE PRODUCTION CONFIG
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS FILE IS
 * ---------------------------------------------------------------------------
 * §5.3 states, scenario by scenario, what must happen to the four auto
 * scenarios of configs/raoulh when the IO marker stops being `auto_scenario`:
 *
 *   - "Tout allumer" and "Ambiance TV" (visible="true") stay VISIBLE SCENARIO
 *     BUTTONS: a press still runs button_start -> the step -> step_end;
 *   - "Monter volets matin" and "Descendre volets soir" (visible="false") stay
 *     invisible and KEEP BEING STARTED BY THEIR time_start RULE;
 *   - all four become ABSENT from `autoscenario list` / `get`. That is the
 *     goal of the ticket, not a regression;
 *   - not one byte of io.xml or rules.xml is rewritten (§5.4).
 *
 * The configuration cannot be shipped in the tree, so the fixture below
 * REPRODUCES ITS SHAPE, attribute for attribute, from the real files: the same
 * four IO ids (input_18, input_30, input_42, input_43), the same four markers
 * (scenario_0..scenario_3), the same 18 rules with the same distribution
 * (5 + 5 + 4 + 4 - the two invisible ones own the two time_start rules), the
 * same sixteen machinery IOs, and the same extra non-triggering condition on
 * `intern_vac_mode` that the real time_start rules carry.
 *
 * ---------------------------------------------------------------------------
 * FLIPS
 * ---------------------------------------------------------------------------
 * Cases that pin the state BEFORE the re-key carry
 *
 *      >>> TO FLIP (T3.61) <<<
 *
 * and the sub-ticket that re-keys the marker flips the expected value while
 * keeping the measurement. Cases marked ">>> PROVE, DO NOT FLIP <<<" are the
 * promise of the migration: they must read the same on both sides.
 *
 * ---------------------------------------------------------------------------
 * THE VACUITY TRAP, AND THE FIXTURE THAT CLOSES IT
 * ---------------------------------------------------------------------------
 * "The four are no longer in `autoscenario list`" is an ABSENCE. On an empty
 * list it is true for the wrong reason. Every case asserting it therefore also
 * creates ONE scenario through the API and asserts that scenario IS in the
 * list, in the same call, on the same array. The list is never allowed to be
 * empty when an absence is read off it.
 *
 * ---------------------------------------------------------------------------
 * IDS
 * ---------------------------------------------------------------------------
 * The scenario ids and their machinery ids are the PRODUCTION ones on purpose
 * (that is what is being characterized). The targets they act on are prefixed
 * t361_ and used nowhere else: Config's IO state cache is process wide and
 * never cleared, so a shared id leaks a value between binaries.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "AutoScenario.h"
#include "AutoScenarioDef.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "Rule.h"
#include "Scenario.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char ROOM_NAME_T361[] = "T3.61 maison";
const char ROOM_TYPE_T361[] = "salon";

//The four scenario IOs of configs/raoulh, with their ids and their markers
const char SC0_IO[] = "input_18";   //"Monter volets matin",  visible="false"
const char SC1_IO[] = "input_30";   //"Descendre volets soir", visible="false"
const char SC2_IO[] = "input_42";   //"Tout allumer",          visible="true"
const char SC3_IO[] = "input_43";   //"Ambiance TV",           visible="true"

//What each scenario acts on. Four different types and four different values,
//so two scenarios cannot be confused for one another.
const char TARGET_MATIN[] = "t361_volet_matin";  //string, "up"
const char TARGET_SOIR[] = "t361_volet_soir";    //string, "down"
const char TARGET_LAMPE[] = "t361_lampe";        //bool,   "true"
const char TARGET_AMPLI[] = "t361_ampli";        //int,    "42"
//The extra condition the real time_start rules carry, trigger="false"
const char IO_VAC_MODE[] = "intern_vac_mode";

std::string attr(const std::string &k, const std::string &v)
{
    return " " + k + "=\"" + v + "\"";
}

std::string scenarioIoXml(const std::string &id, const std::string &marker,
                          const std::string &name, bool visible)
{
    return "    <calaos:input" + attr("type", "scenario") + attr("id", id) +
           attr("name", name) + attr("auto_scenario", marker) +
           attr("cycle", "false") + attr("enabled", "true") +
           attr("gui_type", "scenario") + attr("io_type", "inout") +
           /* The production files say log_history="true". It is set to false
            * here and only here: it drives HistLogger, whose sqlite database
            * lives outside the fixture's private directories, and a scenario
            * starting would try to write it. Nothing measured below reads it.
            */
           attr("log_history", "false") +
           attr("visible", visible? "true": "false") + " />\n";
}

std::string machineryIoXml(const std::string &type, const std::string &id,
                           const std::string &marker, const std::string &extra)
{
    return "    <calaos:internal" + attr("type", type) + attr("id", id) +
           attr("name", id) + attr("auto_scenario", marker) +
           attr("enabled", "true") + attr("rw", "true") +
           attr("visible", "false") + extra + " />\n";
}

std::string timerIoXml(const std::string &marker)
{
    const std::string id = marker + "_timer";
    return "    <calaos:input" + attr("type", "InputTimer") + attr("id", id) +
           attr("name", id) + attr("auto_scenario", marker) +
           attr("enabled", "true") + attr("rw", "true") + attr("save", "false") +
           attr("visible", "false") + attr("hour", "0") + attr("min", "0") +
           attr("sec", "0") + attr("msec", "1000") + " />\n";
}

/* An InPlageHoraire that is inside its range whatever the moment: every day,
 * every month, 00:00:00 to 23:59:59. hasChanged() then flips it to true on
 * demand, which is the only way a test can play "the slot has come".
 */
std::string scheduleIoXml(const std::string &marker)
{
    const char *const days[] = { "lundi", "mardi", "mercredi", "jeudi",
                                 "vendredi", "samedi", "dimanche" };
    const std::string id = marker + "_schedule";

    std::string x = "    <calaos:input" + attr("type", "InPlageHoraire") +
                    attr("id", id) + attr("name", id) +
                    attr("auto_scenario", marker) + attr("enabled", "true") +
                    attr("rw", "true") + attr("save", "false") +
                    attr("visible", "false") +
                    attr("months", "111111111111") + ">\n";
    for (const char *day: days)
    {
        x += std::string("      <calaos:") + day + ">\n"
             "        <calaos:plage start_type=\"0\" start_hour=\"0\" start_min=\"0\""
             " start_sec=\"0\" end_type=\"0\" end_hour=\"23\" end_min=\"59\""
             " end_sec=\"59\" />\n"
             "      </calaos:" + day + ">\n";
    }
    x += "    </calaos:input>\n";
    return x;
}

//The machinery of one scenario: three IOs, plus the schedule pair when the
//scenario is driven by a time range.
std::string machineryXml(const std::string &marker, bool scheduled)
{
    std::string x;
    x += machineryIoXml("InternalBool", marker + "_is_active", marker,
                        attr("save", "false"));
    x += machineryIoXml("InternalInt", marker + "_step", marker,
                        attr("save", "false"));
    x += timerIoXml(marker);

    if (!scheduled) return x;

    x += scheduleIoXml(marker);
    x += machineryIoXml("InternalBool", marker + "_is_schedule_enabled", marker,
                        attr("save", "true") + attr("value", "true"));
    return x;
}

std::string conditionXml(const std::string &id, const std::string &val,
                         bool trigger = true)
{
    return std::string("      <calaos:condition type=\"standard\" trigger=\"") +
           (trigger? "true": "false") + "\">\n"
           "        <calaos:input id=\"" + id + "\" oper=\"==\" val=\"" + val + "\" />\n"
           "      </calaos:condition>\n";
}

std::string actionXml(const std::string &id, const std::string &val)
{
    return "      <calaos:action type=\"standard\">\n"
           "        <calaos:output id=\"" + id + "\" val=\"" + val + "\" />\n"
           "      </calaos:action>\n";
}

std::string ruleOpen(const std::string &marker, const std::string &type,
                     const std::string &stepNumber = std::string())
{
    std::string x = "  <calaos:rule" + attr("name", marker + "_" + type) +
                    attr("type", "AutoScenario") +
                    attr("auto_scenario", marker) +
                    attr("auto_scenario_type", type);
    if (!stepNumber.empty()) x += attr("auto_scenario_step", stepNumber);
    return x + ">\n";
}

/* The rules of one scenario, copied from configs/raoulh/rules.xml: the header
 * three, one step, and - for a scheduled scenario - the time_start rule that
 * is what still makes the two invisible ones run at their slot.
 */
std::string scenarioRulesXml(const std::string &marker, const std::string &ioId,
                             const std::string &targetId,
                             const std::string &targetValue, bool scheduled)
{
    const std::string isActive = marker + "_is_active";
    const std::string step = marker + "_step";
    const std::string timer = marker + "_timer";

    std::string x;

    x += ruleOpen(marker, "button_start");
    x += conditionXml(ioId, "true") + conditionXml(isActive, "false");
    x += actionXml(ioId, "false") + actionXml(isActive, "true") +
         actionXml(step, "0") + actionXml(timer, "0") + actionXml(timer, "start");
    x += "  </calaos:rule>\n";

    x += ruleOpen(marker, "button_stop");
    x += conditionXml(ioId, "true") + conditionXml(isActive, "true");
    x += actionXml(ioId, "false") + actionXml(step, "-1") +
         actionXml(timer, "0") + actionXml(timer, "start");
    x += "  </calaos:rule>\n";

    x += ruleOpen(marker, "step_end");
    x += conditionXml(isActive, "true") + conditionXml(step, "-1") +
         conditionXml(timer, "true");
    x += actionXml(isActive, "false");
    x += "  </calaos:rule>\n";

    x += ruleOpen(marker, "step", "0");
    x += conditionXml(isActive, "true") + conditionXml(step, "0") +
         conditionXml(timer, "true");
    x += actionXml(step, "-1") + actionXml(timer, "1000") +
         actionXml(timer, "start") + actionXml(targetId, targetValue);
    x += "  </calaos:rule>\n";

    if (!scheduled) return x;

    x += ruleOpen(marker, "time_start");
    x += conditionXml(isActive, "false") +
         conditionXml(marker + "_is_schedule_enabled", "true") +
         conditionXml(marker + "_schedule", "true") +
         conditionXml(IO_VAC_MODE, "false", false);
    x += actionXml(ioId, "true");
    x += "  </calaos:rule>\n";

    return x;
}

} //namespace

class ScenarioMarkerRekeyTest: public JsonApiCharacterizationTest
{
protected:
    /* The four scenarios of configs/raoulh, their sixteen machinery IOs, their
     * four targets and the vacation-mode flag, in one room.
     */
    static std::string productionShapedIoXml()
    {
        std::string ios;

        ios += internalIoXml("InternalString", TARGET_MATIN, "Volet matin");
        ios += internalIoXml("InternalString", TARGET_SOIR, "Volet soir");
        ios += internalIoXml("InternalBool", TARGET_LAMPE, "Lampe");
        ios += internalIoXml("InternalInt", TARGET_AMPLI, "Ampli");
        ios += internalIoXml("InternalBool", IO_VAC_MODE, "Mode vacances");

        ios += scenarioIoXml(SC0_IO, "scenario_0", "Monter volets matin", false);
        ios += machineryXml("scenario_0", true);
        ios += scenarioIoXml(SC1_IO, "scenario_1", "Descendre volets soir", false);
        ios += machineryXml("scenario_1", true);
        ios += scenarioIoXml(SC2_IO, "scenario_2", "Tout allumer", true);
        ios += machineryXml("scenario_2", false);
        ios += scenarioIoXml(SC3_IO, "scenario_3", "Ambiance TV", true);
        ios += machineryXml("scenario_3", false);

        return ioXmlDocument(roomXml(ROOM_NAME_T361, ROOM_TYPE_T361, ios, 0));
    }

    static std::string productionShapedRulesXml()
    {
        std::string rules;
        rules += scenarioRulesXml("scenario_0", SC0_IO, TARGET_MATIN, "up", true);
        rules += scenarioRulesXml("scenario_1", SC1_IO, TARGET_SOIR, "down", true);
        rules += scenarioRulesXml("scenario_2", SC2_IO, TARGET_LAMPE, "true", false);
        rules += scenarioRulesXml("scenario_3", SC3_IO, TARGET_AMPLI, "42", false);
        return rulesXmlDocument(rules);
    }

    void loadProductionShapedHouse()
    {
        loadConfig(productionShapedIoXml(), productionShapedRulesXml());
    }

    static Scenario *scenarioIo(const std::string &id)
    {
        return dynamic_cast<Scenario *>(ListeRoom::Instance().get_io(id));
    }

    static size_t countOccurrences(const std::string &haystack,
                                   const std::string &needle)
    {
        size_t n = 0;
        for (std::string::size_type p = haystack.find(needle);
             p != std::string::npos;
             p = haystack.find(needle, p + needle.size()))
            n++;
        return n;
    }

    Json wsAutoscenario(WsTestSession &ws, Json data)
    {
        ws.clear();
        ws.send(Json{{ "msg", "autoscenario" }, { "msg_id", "t361" }, { "data", data }});
        EXPECT_EQ(1u, ws.count()) << "autoscenario " << data.value("type", std::string())
                                  << " answered " << ws.count() << " messages";
        if (ws.count() != 1) return Json::object();
        return ws.lastData();
    }

    /* THE ANTI-VACUITY FIXTURE. One scenario created through the API, so that
     * every assertion of the form "the four are not in the list" is read off a
     * list that is NOT empty. Answers its IO id.
     */
    std::string createOneRealScenario(WsTestSession &ws)
    {
        const Json ret = wsAutoscenario(ws, Json{
            { "type", "create" },
            { "name", "Scenario neuf" },
            { "room_name", ROOM_NAME_T361 },
            { "room_type", ROOM_TYPE_T361 },
            { "steps", Json::array({
                Json{{ "pause", "1.5" },
                     { "actions", Json::array({
                         Json{{ "io", TARGET_LAMPE }, { "value", "true" }} }) }} }) },
            { "final_step", Json{{ "actions", Json::array({
                         Json{{ "io", TARGET_AMPLI }, { "value", "7" }} }) }} }});

        return ret.value("id", std::string());
    }

    //Ids present in an `autoscenario list` answer, in order.
    static std::vector<std::string> listedIds(const Json &list)
    {
        std::vector<std::string> ids;
        if (!list["scenarios"].is_array()) return ids;
        for (const Json &sc: list["scenarios"])
            ids.push_back(sc.value("id", std::string()));
        return ids;
    }

    static bool listed(const std::vector<std::string> &ids, const std::string &id)
    {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }
};

/*******************************************************************************
 * SECTION 1 - THE SHAPE, so that everything below is read on the right fixture
 ******************************************************************************/

TEST_F(ScenarioMarkerRekeyTest, TheProductionShapedHouseLoadsItsFourScenariosAndEighteenRules)
{
    /* >>> PROVE, DO NOT FLIP <<<
     * The four IOs, their machinery and their rules load whatever the marker
     * means: `type="scenario"` is what builds a Scenario IO, and a rule of
     * `type="AutoScenario"` is an ordinary rule as far as loading goes.
     */
    loadProductionShapedHouse();

    EXPECT_TRUE(scenarioIo(SC0_IO) != nullptr);
    EXPECT_TRUE(scenarioIo(SC1_IO) != nullptr);
    EXPECT_TRUE(scenarioIo(SC2_IO) != nullptr);
    EXPECT_TRUE(scenarioIo(SC3_IO) != nullptr);

    EXPECT_EQ("false", scenarioIo(SC0_IO)->get_param("visible"));
    EXPECT_EQ("false", scenarioIo(SC1_IO)->get_param("visible"));
    EXPECT_EQ("true", scenarioIo(SC2_IO)->get_param("visible"));
    EXPECT_EQ("true", scenarioIo(SC3_IO)->get_param("visible"));

    //gui_type is unconditional (IO/Scenario.cpp), which is what keeps the MCP
    //scenario tools seeing all four after the migration.
    EXPECT_EQ("scenario", scenarioIo(SC2_IO)->get_param("gui_type"));

    EXPECT_EQ(18, ListeRule::Instance().size())
            << "the fixture no longer has the rule shape of configs/raoulh";
    EXPECT_EQ(18u, ListeRule::Instance().getRuleAutoScenario("scenario_0").size() +
                   ListeRule::Instance().getRuleAutoScenario("scenario_1").size() +
                   ListeRule::Instance().getRuleAutoScenario("scenario_2").size() +
                   ListeRule::Instance().getRuleAutoScenario("scenario_3").size());

    //The two invisible ones are the ones carrying a time_start rule
    EXPECT_EQ(5u, ListeRule::Instance().getRuleAutoScenario("scenario_0").size());
    EXPECT_EQ(5u, ListeRule::Instance().getRuleAutoScenario("scenario_1").size());
    EXPECT_EQ(4u, ListeRule::Instance().getRuleAutoScenario("scenario_2").size());
    EXPECT_EQ(4u, ListeRule::Instance().getRuleAutoScenario("scenario_3").size());
}

/*******************************************************************************
 * SECTION 2 - THE API SURFACE. The four leave it, and a real one stays in it
 ******************************************************************************/

TEST_F(ScenarioMarkerRekeyTest, AutoscenarioListNoLongerCarriesTheFourLegacyScenarios)
{
    /* ✅ FLIPPED, and the flip is the goal of the ticket - §5.3, third line.
     * The four expectations below were EXPECT_TRUE before the re-key and the
     * list had five entries; same measurement, turned around. The case was
     * renamed with it: nothing cites the old name yet.
     *
     * The list is read WITH a freshly created scenario in it, so the absence
     * is never read off an empty array.
     */
    loadProductionShapedHouse();

    WsTestSession ws;
    const std::string fresh = createOneRealScenario(ws);
    ASSERT_FALSE(fresh.empty()) << "the anti-vacuity scenario was not created";

    const std::vector<std::string> ids = listedIds(wsAutoscenario(ws, Json{{ "type", "list" }}));

    EXPECT_TRUE(listed(ids, fresh))
            << "a scenario created through the API must be listed - without this "
               "half, every absence below would pass on an empty list";

    EXPECT_FALSE(listed(ids, SC0_IO));
    EXPECT_FALSE(listed(ids, SC1_IO));
    EXPECT_FALSE(listed(ids, SC2_IO));
    EXPECT_FALSE(listed(ids, SC3_IO));
    EXPECT_EQ(1u, ids.size());

    //and all four IOs are still perfectly there
    for (const char *id: { SC0_IO, SC1_IO, SC2_IO, SC3_IO })
        EXPECT_TRUE(scenarioIo(id) != nullptr) << id;
}

TEST_F(ScenarioMarkerRekeyTest, AutoscenarioGetRefusesTheFourLegacyScenariosAndAnswersOnARealOne)
{
    /* ✅ FLIPPED, `get` half of the same line of §5.3. It used to answer the
     * payload of each of the four; it answers "wrong input" now. The control -
     * `get` on the freshly created scenario - is unchanged and green on both
     * sides, so a `get` that had simply stopped working would not pass here.
     */
    loadProductionShapedHouse();

    WsTestSession ws;
    const std::string fresh = createOneRealScenario(ws);
    ASSERT_FALSE(fresh.empty());

    EXPECT_EQ(fresh, wsAutoscenario(ws, Json{{ "type", "get" }, { "id", fresh }})
                         .value("id", std::string()))
            << "get must answer on a real scenario, on both sides of the re-key";

    for (const char *id: { SC0_IO, SC1_IO, SC2_IO, SC3_IO })
        EXPECT_JSON_EQ(std::string(R"({"error":"wrong input"})"),
                       wsAutoscenario(ws, Json{{ "type", "get" }, { "id", id }}))
                << "autoscenario get " << id;
}

/*******************************************************************************
 * SECTION 3 - THE PROMISE: the four keep behaving exactly as they did
 ******************************************************************************/

TEST_F(ScenarioMarkerRekeyTest, TheTwoVisibleScenariosStillRunTheirChainWhenTheirButtonIsPressed)
{
    /* >>> PROVE, DO NOT FLIP <<<
     * §5.3, first line. Measured where it matters: not "the IO value flipped"
     * - button_start sets it straight back to false, which is what a scenario
     * STARTING looks like - but "the chain ran": _is_active goes true, _step
     * is armed at 0, and the step's own target is reached once the step rule
     * fires.
     *
     * The two are pressed independently and each is checked against its OWN
     * target, so a chain that started the wrong scenario fails.
     */
    loadProductionShapedHouse();

    for (const auto &pair: { std::make_pair(std::string(SC2_IO), std::string("scenario_2")),
                             std::make_pair(std::string(SC3_IO), std::string("scenario_3")) })
    {
        const std::string ioId = pair.first;
        const std::string marker = pair.second;

        Scenario *sc = scenarioIo(ioId);
        ASSERT_TRUE(sc != nullptr) << ioId;
        ASSERT_TRUE(io(marker + "_is_active") != nullptr);
        ASSERT_FALSE(io(marker + "_is_active")->get_value_bool())
                << marker << " was already running before the press";

        EXPECT_TRUE(sc->set_value(true)) << ioId;

        EXPECT_TRUE(io(marker + "_is_active")->get_value_bool())
                << marker << "_button_start did not fire: the scenario button is dead";
        EXPECT_EQ(0, (int)io(marker + "_step")->get_value_double())
                << marker << " did not arm its first step";
        EXPECT_FALSE(sc->get_value_bool())
                << "button_start sets the scenario IO back to false";
    }

    //and the two invisible ones were not started by the two presses
    EXPECT_FALSE(io("scenario_0_is_active")->get_value_bool());
    EXPECT_FALSE(io("scenario_1_is_active")->get_value_bool());
}

TEST_F(ScenarioMarkerRekeyTest, TheTwoInvisibleScenariosAreStillStartedByTheirTimeStartRule)
{
    /* >>> PROVE, DO NOT FLIP <<<
     * §5.3, second line, and the half nothing attested before this ticket:
     * "Monter volets matin" and "Descendre volets soir" have no button in any
     * UI - their ONLY way in is the time_start rule. The slot is played by
     * making the InPlageHoraire recompute itself: its ranges cover every day
     * and every month, so hasChanged() flips it to true and emits, exactly as
     * the periodic poll does at the slot.
     *
     * time_start then sets the scenario IO to true, which cascades into
     * button_start (ListeRule drains the trigger it queued). Both hops are
     * asserted: the scenario is armed AND its step is.
     */
    loadProductionShapedHouse();

    for (const std::string &marker: { std::string("scenario_0"), std::string("scenario_1") })
    {
        IOBase *schedule = io(marker + "_schedule");
        ASSERT_TRUE(schedule != nullptr) << marker << " has no time range";
        ASSERT_FALSE(schedule->get_value_bool()) << "the slot has not come yet";
        ASSERT_TRUE(io(marker + "_is_schedule_enabled")->get_value_bool())
                << "the schedule of " << marker << " is disabled, it can never fire";
        ASSERT_FALSE(io(marker + "_is_active")->get_value_bool());

        //the slot comes
        schedule->hasChanged();

        ASSERT_TRUE(schedule->get_value_bool())
                << "the time range did not enter its slot, the fixture moved";
        EXPECT_TRUE(io(marker + "_is_active")->get_value_bool())
                << marker << "_time_start no longer starts the scenario";
        EXPECT_EQ(0, (int)io(marker + "_step")->get_value_double())
                << marker << " did not arm its first step";
    }

    //and neither of the two visible ones was started by a schedule
    EXPECT_FALSE(io("scenario_2_is_active")->get_value_bool());
    EXPECT_FALSE(io("scenario_3_is_active")->get_value_bool());
}

/*******************************************************************************
 * SECTION 4 - THE FILES. §5.4: nothing on disk is rewritten
 ******************************************************************************/

TEST_F(ScenarioMarkerRekeyTest, TheStartupPassKeepsTheEighteenRulesAndTheirLegacyMarker)
{
    /* >>> PROVE, DO NOT FLIP <<<
     * checkAutoScenario() ends with SaveConfigRule(), so whatever it does to
     * the rules is on disk at the first startup with no user action. It must
     * do nothing at all: the 18 rules of configs/raoulh survive, marker
     * included, and the second startup writes the very same bytes.
     */
    loadProductionShapedHouse();
    ASSERT_EQ(18, ListeRule::Instance().size());

    ListeRoom::Instance().checkAutoScenario();

    EXPECT_EQ(18, ListeRule::Instance().size())
            << "the startup pass destroyed or duplicated rules";

    const std::string rulesFirst = rulesXmlOnDisk();
    const std::string ioFirst = ioXmlOnDisk();
    EXPECT_EQ(18u, countOccurrences(rulesFirst, "type=\"AutoScenario\""));
    EXPECT_EQ(18u, countOccurrences(rulesFirst, "auto_scenario=\"scenario_"));

    //second startup, on the files the first one wrote
    clearCoreState();
    loadConfig(ioFirst, rulesFirst);
    ASSERT_EQ(18, ListeRule::Instance().size());
    ListeRoom::Instance().checkAutoScenario();

    EXPECT_EQ(18, ListeRule::Instance().size());
    EXPECT_EQ(rulesFirst, rulesXmlOnDisk())
            << "the second startup rewrote rules.xml";
    EXPECT_EQ(ioFirst, ioXmlOnDisk())
            << "the second startup rewrote io.xml";
}

TEST_F(ScenarioMarkerRekeyTest, TheStartupPassWritesNoDefinitionIntoIoXmlForTheLegacyScenarios)
{
    /* ✅ FLIPPED, the io.xml half of §5.4 and the measurement that decides the
     * ticket. Before the re-key the four were still auto scenarios, so the
     * first save captured a definition out of their rules and wrote an
     * `autoscenario_uid` and a step list into io.xml for each of them - four
     * counts of 4 below, on a production file nobody asked to rewrite. They
     * are 0 now: no AutoScenario is built, so nothing is captured and nothing
     * is written.
     *
     * The legacy `auto_scenario` params are counted on the same file and read
     * the same on both sides: they are the ones §5.3 leaves in place, orphaned
     * and inert. Four scenario IOs and sixteen machinery IOs carry one.
     */
    loadProductionShapedHouse();
    ASSERT_EQ(0u, countOccurrences(productionShapedIoXml(), "autoscenario_uid"));

    ListeRoom::Instance().checkAutoScenario();

    const std::string ioXml = ioXmlOnDisk();

    EXPECT_EQ(0u, countOccurrences(ioXml, "autoscenario_"))
            << "the startup pass wrote a definition into a file that had none";
    EXPECT_EQ(0u, countOccurrences(ioXml, "as_"))
            << "the startup pass wrote a step param into io.xml";
    EXPECT_EQ(0u, countOccurrences(ioXml, "disabled_missing_io"))
            << "the startup pass stamped a scenario flag onto an ordinary IO";

    EXPECT_EQ(20u, countOccurrences(ioXml, "auto_scenario=\"scenario_"))
            << "the legacy marker must survive on all twenty IOs, untouched";
}
