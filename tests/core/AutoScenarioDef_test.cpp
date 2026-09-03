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
 * WHAT IT COVERS
 * ---------------------------------------------------------------------------
 * The first commit of this file was pure characterization, zero line of src/.
 * This is the second: the three cases it filed ">>> TO FLIP (E4.6b, D2) <<<"
 * are turned around here - same measurement, reversed expectation - and the
 * codec cases are added next to them.
 *
 * Sections:
 *   1  the GUARD RAIL. It never flips.
 *   2  the codec, params <-> definition: the byte for byte round trip, the
 *      percent-encoding of '%', '|' and '=', the action whose IO does not
 *      exist, the orphan `as_*` param, and `autoscenario_steps` being the one
 *      authoritative key.
 *   3  the ids: opaque, and never recycled.
 *   4  the same properties END TO END, through io.xml and a real save/reload.
 *
 * ---------------------------------------------------------------------------
 * THE GUARD RAIL
 * ---------------------------------------------------------------------------
 * E4.6b must NOT re-key the marker that builds an AutoScenario. Re-keying it
 * makes ListeRoom::checkAutoScenario() (ListeRoom.cpp:320-330) destroy - and
 * persist the destruction of - every rule carrying `auto_scenario`, which on
 * configs/raoulh is 18 rules (E4.6.md §5.2). That sweep belongs to E4.6c;
 * nothing in E4.6b may arm it. Section 1 is the tripwire, and section 2 adds
 * the static half: `auto_scenario` is in NEITHER namespace this codec owns, so
 * the orphan clean up can never reach it.
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
#include "AutoScenarioDef.h"
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

//An id that names NO IO. It is written into a definition and must come back
//out of it unchanged (D4): a codec that resolves ids would lose it.
const char IO_NOWHERE[] = "e46b_nowhere";

//Split on '|', dropping empties. Written here rather than borrowed so that
//what this file measures does not depend on a general purpose helper.
std::vector<std::string> splitPipe(const std::string &in)
{
    std::vector<std::string> out;
    std::string::size_type start = 0;
    while (start <= in.size())
    {
        const std::string::size_type pos = in.find('|', start);
        if (pos == std::string::npos)
        {
            if (start < in.size()) out.push_back(in.substr(start));
            break;
        }
        if (pos > start) out.push_back(in.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

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
            /* cycle "true" ON PURPOSE, and it is the only reason this key is
             * here: `autoscenario create` defaults `disabled` to "true"
             * (JsonApi.cpp), so the scenario comes out cycle=true /
             * enabled=false and the two keys DISAGREE. E4.0c proved by
             * exchange that swapping `cycle` and `enabled` left 52/52 green
             * because every golden had them equal. Not here.
             */
            { "cycle", "true" },
            { "steps", Json::array({
                Json{{ "pause", "1.25" },
                     { "actions", Json::array({
                         Json{{ "io", IO_LAMP }, { "value", "true" }} }) }},
                Json{{ "pause", "3.5" },
                     { "actions", Json::array({
                         Json{{ "io", IO_BLIND }, { "value", "31" }},
                         Json{{ "io", IO_LABEL }, { "value", SEPARATOR_VALUE }},
                         Json{{ "io", IO_VOLUME }, { "value", "19" }} }) }},
                Json{{ "pause", "0.75" },
                     { "actions", Json::array({
                         Json{{ "io", IO_BANNER }, { "value", "au revoir" }} }) }}
            }) },
            { "final_step", Json{{ "actions", Json::array({
                         Json{{ "io", IO_SIREN }, { "value", "false" }},
                         Json{{ "io", IO_LAMP }, { "value", "false" }} }) }} }});

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

    /* And every rule carrying the marker is CLAIMED by this scenario. The bit
     * Rule used to carry alongside the param is gone - nothing read it - so
     * the claim is measured where it lives: the param, and the lookup that
     * turns it into the scenario's rule list.
     */
    int marked = 0;
    for (int i = 0;i < ListeRule::Instance().size();i++)
    {
        Rule *r = ListeRule::Instance().get_rule(i);
        if (r && r->param_exists("auto_scenario")) marked++;
    }
    EXPECT_GT(marked, 0);
    EXPECT_EQ((size_t)marked,
              ListeRule::Instance().getRuleAutoScenario(SCENARIO_MARKER).size())
            << "a rule carries the marker and yet is not one of this scenario's";
}

/*******************************************************************************
 * SECTION 2 - WHERE THE DEFINITION LIVES TODAY: NOT IN io.xml
 ******************************************************************************/

TEST_F(AutoScenarioDefTest, TheScenarioIoCarriesTheWholeDefinitionInIoXml)
{
    /* ✅ FLIPPED FROM THE CHARACTERIZATION COMMIT (E4.6b, D2).
     *
     * It used to assert that the two namespaces D2 reserves were EMPTY - the
     * definition was nowhere on disk, the model was inferred from rules.xml
     * (E4.6.md §2.1). The same measurement now asserts the opposite, key by
     * key rather than "not empty": a count is not a contract.
     */
    loadRichScenarioFromDisk();

    const std::string xml = ioXmlOnDisk();
    const std::vector<std::string> keys = definitionKeysOf(xml);
    EXPECT_FALSE(keys.empty()) << "io.xml carries no definition param at all";

    Scenario *sc = scenarioIo();
    ASSERT_TRUE(sc != nullptr);

    //the header, one key at a time
    EXPECT_TRUE(sc->param_exists(AutoScenarioDef::KEY_UID));
    EXPECT_EQ(AutoScenarioDef::SCHEMA_VERSION, sc->get_param(AutoScenarioDef::KEY_SCHEMA));
    //cycle and enabled DISAGREE, see createRichScenario()
    EXPECT_EQ("true", sc->get_param(AutoScenarioDef::KEY_CYCLE));
    EXPECT_EQ("false", sc->get_param(AutoScenarioDef::KEY_ENABLED));
    //not scheduled: the key is ABSENT, never present and empty
    EXPECT_FALSE(sc->param_exists(AutoScenarioDef::KEY_SCHEDULE));

    //three steps, named by the authoritative list, and each one has its pair
    const std::string stepList = sc->get_param(AutoScenarioDef::KEY_STEPS);
    const std::vector<std::string> stepIds = splitPipe(stepList);
    ASSERT_EQ(3u, stepIds.size()) << "autoscenario_steps = '" << stepList << "'";

    const char *pauses[] = { "1.25", "3.5", "0.75" };
    for (size_t i = 0;i < stepIds.size();i++)
    {
        EXPECT_EQ(pauses[i], sc->get_param("as_" + stepIds[i] + "_pause"))
                << "step " << i << " (" << stepIds[i] << ")";
        EXPECT_TRUE(sc->param_exists("as_" + stepIds[i] + "_actions"));
    }

    //the final step is a param of its own, not a synthetic tail of the list
    EXPECT_EQ(std::string(IO_SIREN) + "=false|" + IO_LAMP + "=false",
              sc->get_param("as_final_actions"));

    //and the LEGACY marker is untouched, in a namespace of its own
    EXPECT_EQ(SCENARIO_MARKER, sc->get_param("auto_scenario"));
}

TEST_F(AutoScenarioDefTest, ThePausesAndTheActionValuesAreInIoXmlToo)
{
    /* ✅ FLIPPED FROM THE CHARACTERIZATION COMMIT (E4.6b, D2).
     *
     * Same needles, the ones the fixture makes unique. They used to be in
     * rules.xml and NOWHERE else. They are now in io.xml as well - and the
     * separator carrying one arrives PERCENT-ENCODED, which is the only one of
     * the five that is not a plain substring match.
     */
    loadRichScenarioFromDisk();

    const std::string ioXml = ioXmlOnDisk();
    const std::string rulesXml = rulesXmlOnDisk();

    for (const char *needle: { "1.25", "3.5", "0.75", "au revoir" })
    {
        EXPECT_NE(std::string::npos, ioXml.find(needle))
                << "io.xml does not carry " << needle << ": D2 did not land";
        EXPECT_NE(std::string::npos, rulesXml.find(needle))
                << "rules.xml lost " << needle;
    }

    //rules.xml keeps it raw; io.xml carries it encoded and NOT raw
    EXPECT_NE(std::string::npos, rulesXml.find(SEPARATOR_VALUE));
    EXPECT_EQ(std::string::npos, ioXml.find(SEPARATOR_VALUE))
            << "the separators reached io.xml unencoded";
    EXPECT_NE(std::string::npos, ioXml.find("a%7Cb%3Dc%25d%7Ce"))
            << "io.xml does not carry the percent-encoded value";
}

TEST_F(AutoScenarioDefTest, AnOrphanAsParamIsIgnoredAtLoadAndRemovedAtTheNextSave)
{
    /* ✅ FLIPPED FROM THE CHARACTERIZATION COMMIT (E4.6b, D2).
     *
     * `as_s9_actions` names a step `autoscenario_steps` does not list. It used
     * to survive for ever, because nobody owned the namespace. It is now
     * ignored at load and REMOVED at the next save.
     *
     * The EXCHANGE that gives this case its teeth is one line below: a probe
     * OUTSIDE the two owned namespaces goes through the same cycle and MUST
     * survive. The clean up of D2 is the only automatic one of the whole epic
     * and it may only ever reach our own params (E4.6.md D10, requirement 4) -
     * a sweep that tidies up what it does not understand is exactly the
     * mechanism of ListeRoom.cpp:320-330.
     */
    loadRichScenarioFromDisk();

    std::string ioXml = ioXmlOnDisk();
    const std::string rulesXml = rulesXmlOnDisk();
    /* The token is `s_orphan`, NOT `s9`: step ids are minted as "s<digits>"
     * from a process wide counter, so `s9` is a name a real step of another
     * case in this binary can legitimately have taken - the test would then
     * pass or fail depending on the order the cases ran in. `s_orphan` is a
     * valid step id that the allocator can never hand out.
     */
    ASSERT_TRUE(addParamToScenarioIoInXml(ioXml, "as_s_orphan_actions",
                                          std::string(IO_NOWHERE) + "=boo"));
    ASSERT_TRUE(addParamToScenarioIoInXml(ioXml, "e46b_probe_param", "a|b=c%d"));

    clearCoreState();
    loadConfig(ioXml, rulesXml);

    //ignored at LOAD: the definition has three steps, not four
    Scenario *sc = scenarioIo();
    ASSERT_TRUE(sc != nullptr);
    ASSERT_TRUE(sc->getDefinition() != nullptr);
    EXPECT_EQ(3u, sc->getDefinition()->steps.size());

    ListeRoom::Instance().checkAutoScenario();
    saveConfig();

    //removed at SAVE
    EXPECT_EQ(std::string::npos, ioXmlOnDisk().find("as_s_orphan_actions"))
            << "the orphan param survived the save";
    EXPECT_FALSE(scenarioIo()->param_exists("as_s_orphan_actions"));

    //and the probe, one namespace away, is untouched
    EXPECT_NE(std::string::npos, ioXmlOnDisk().find("e46b_probe_param"))
            << "the clean up reached a param that is not ours";
    EXPECT_EQ("a|b=c%d", scenarioIo()->get_param("e46b_probe_param"));
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

/*******************************************************************************
 * SECTION 2 - THE CODEC, params <-> definition
 *
 * These cases do not need a house: they exercise AutoScenarioDef against a
 * Params built by hand, which is the only way to state the round trip as a
 * BYTE FOR BYTE equality (E4.6.md §6, criterion 1) instead of a resemblance.
 *
 * The reference params below diverge on EVERY axis the codec encodes, and two
 * divergences are there for one reason only - E4.0c proved by exchange that
 * `cycle` and `enabled` were never observed in disagreement, so swapping the
 * two keys left 52/52 green:
 *   - cycle "true"  WHILE enabled "false";
 *   - three pauses that are all different, and none of them 0;
 *   - one IO targeted twice with two different values;
 *   - one action id that names no IO at all;
 *   - one value carrying '%', '|' and '=', in the MIDDLE of its list.
 ******************************************************************************/

namespace
{

//The reference params. Written out key by key on purpose: a helper that
//builds them from a definition would be testing itself.
Params referenceDefinitionParams()
{
    Params p;
    p.Add(AutoScenarioDef::KEY_UID, "as_41");
    p.Add(AutoScenarioDef::KEY_SCHEMA, AutoScenarioDef::SCHEMA_VERSION);
    p.Add(AutoScenarioDef::KEY_CYCLE, "true");        //diverges from enabled
    p.Add(AutoScenarioDef::KEY_ENABLED, "false");     //on purpose
    p.Add(AutoScenarioDef::KEY_SCHEDULE, "e46b_range");
    p.Add(AutoScenarioDef::KEY_STEPS, "s3|s1|s2");    //NOT alphabetical
    p.Add("as_s3_pause", "1.25");
    p.Add("as_s3_actions", "e46b_lamp=true");
    p.Add("as_s1_pause", "3.5");
    p.Add("as_s1_actions",
          "e46b_blind=31|e46b_label=a%7Cb%3Dc%25d%7Ce|e46b_nowhere=19");
    p.Add("as_s2_pause", "0.75");
    p.Add("as_s2_actions", "e46b_banner=au revoir");
    p.Add("as_final_actions", "e46b_siren=false|e46b_lamp=off");
    return p;
}

//Every param of the two owned namespaces, rendered as "key=value\n" in
//alphabetical order. Params is a std::map, so this walk is alphabetical and
//independent of the order the keys were added in.
std::string renderDefinitionParams(const Params &p)
{
    std::string out;
    for (Params::const_iterator it = p.cbegin();it != p.cend();++it)
    {
        if (!AutoScenarioDef::isDefinitionParam(it->first)) continue;
        out += it->first + "=" + it->second + "\n";
    }
    return out;
}

} //namespace

TEST_F(AutoScenarioDefTest, ParamsToDefinitionToParamsIsStableByteForByte)
{
    /* ⭐ CRITERION 1 of E4.6.md §6, and the reason this file exists.
     *
     * Load the reference params into a definition, write that definition into
     * an EMPTY Params, and compare the two renderings byte for byte. Not "the
     * same keys", not "the same values once parsed": the same bytes.
     */
    const Params reference = referenceDefinitionParams();

    AutoScenarioDef def;
    ASSERT_TRUE(def.loadFromParams(reference));

    Params written;
    def.saveToParams(written);

    EXPECT_EQ(renderDefinitionParams(reference), renderDefinitionParams(written));

    //and a second turn changes nothing either - the codec is idempotent, not
    //merely reversible once
    AutoScenarioDef again;
    ASSERT_TRUE(again.loadFromParams(written));
    Params third;
    again.saveToParams(third);
    EXPECT_EQ(renderDefinitionParams(written), renderDefinitionParams(third));
}

TEST_F(AutoScenarioDefTest, TheDefinitionReadBackFromTheParamsIsTheOneThatWasWritten)
{
    /* The other half of the round trip: the same equality stated on the MODEL
     * rather than on the bytes. A codec that mangled two fields consistently
     * in both directions would satisfy the byte test alone.
     */
    AutoScenarioDef def;
    ASSERT_TRUE(def.loadFromParams(referenceDefinitionParams()));

    EXPECT_EQ("as_41", def.uid);
    EXPECT_TRUE(def.cycle);
    EXPECT_FALSE(def.enabled);
    EXPECT_EQ("e46b_range", def.scheduleIoId);

    //the ORDER is the one of autoscenario_steps, not the alphabetical one
    ASSERT_EQ(3u, def.steps.size());
    EXPECT_EQ("s3", def.steps[0].stepId);
    EXPECT_EQ("s1", def.steps[1].stepId);
    EXPECT_EQ("s2", def.steps[2].stepId);
    EXPECT_DOUBLE_EQ(1.25, def.steps[0].pause);
    EXPECT_DOUBLE_EQ(3.5, def.steps[1].pause);
    EXPECT_DOUBLE_EQ(0.75, def.steps[2].pause);

    ASSERT_EQ(1u, def.steps[0].actions.size());
    EXPECT_EQ("e46b_lamp", def.steps[0].actions[0].ioId);
    EXPECT_EQ("true", def.steps[0].actions[0].value);

    ASSERT_EQ(3u, def.steps[1].actions.size());
    EXPECT_EQ("e46b_blind", def.steps[1].actions[0].ioId);
    EXPECT_EQ("31", def.steps[1].actions[0].value);
    EXPECT_EQ("e46b_label", def.steps[1].actions[1].ioId);
    EXPECT_EQ(SEPARATOR_VALUE, def.steps[1].actions[1].value);
    EXPECT_EQ(IO_NOWHERE, def.steps[1].actions[2].ioId);
    EXPECT_EQ("19", def.steps[1].actions[2].value);

    //the final step is its own field, never the last element of steps
    ASSERT_EQ(2u, def.finalStep.actions.size());
    EXPECT_EQ("e46b_siren", def.finalStep.actions[0].ioId);
    EXPECT_EQ("false", def.finalStep.actions[0].value);
    EXPECT_EQ("e46b_lamp", def.finalStep.actions[1].ioId);
    EXPECT_EQ("off", def.finalStep.actions[1].value);
}

TEST_F(AutoScenarioDefTest, TheThreeSeparatorsArePercentEncodedAndComeBackByteForByte)
{
    /* ⭐ CRITERION 2 of E4.6.md §6. The encoding is asserted on the EXACT
     * bytes, not just on the round trip: a codec that encoded '|' as "%3D" and
     * '=' as "%7C" would round trip perfectly and be wrong on the wire.
     */
    EXPECT_EQ("%25", AutoScenarioDef::encode("%"));
    EXPECT_EQ("%7C", AutoScenarioDef::encode("|"));
    EXPECT_EQ("%3D", AutoScenarioDef::encode("="));
    EXPECT_EQ("a%7Cb%3Dc%25d%7Ce", AutoScenarioDef::encode(SEPARATOR_VALUE));

    //'%' is encoded FIRST: encoding after the others would double-encode what
    //they produced ("|" -> "%7C" -> "%257C")
    EXPECT_EQ(SEPARATOR_VALUE, AutoScenarioDef::decode(AutoScenarioDef::encode(SEPARATOR_VALUE)));
    EXPECT_EQ("%7C", AutoScenarioDef::decode(AutoScenarioDef::encode("%7C")));

    //every byte a std::string can carry, one at a time, including the ones XML
    //cannot: the codec is not allowed to care
    for (int c = 1;c < 256;c++)
    {
        const std::string one(1, (char)c);
        EXPECT_EQ(one, AutoScenarioDef::decode(AutoScenarioDef::encode(one)))
                << "byte " << c;
    }
    //and the NUL, which std::string carries and const char * does not
    const std::string withNul = std::string("a\0b", 3);
    EXPECT_EQ(withNul, AutoScenarioDef::decode(AutoScenarioDef::encode(withNul)));

    //end to end through one action list, separators in BOTH the id and the
    //value, and the loaded item in the MIDDLE of three
    std::vector<AutoScenarioDefAction> actions;
    AutoScenarioDefAction a1; a1.ioId = "io_1"; a1.value = "plain";
    AutoScenarioDefAction a2; a2.ioId = "we|ird=id%"; a2.value = SEPARATOR_VALUE;
    AutoScenarioDefAction a3; a3.ioId = "io_3"; a3.value = "";
    actions.push_back(a1); actions.push_back(a2); actions.push_back(a3);

    const std::string encoded = AutoScenarioDef::encodeActions(actions);
    EXPECT_EQ("io_1=plain|we%7Cird%3Did%25=a%7Cb%3Dc%25d%7Ce|io_3=", encoded);

    const std::vector<AutoScenarioDefAction> back = AutoScenarioDef::decodeActions(encoded);
    ASSERT_EQ(3u, back.size());
    EXPECT_EQ("io_1", back[0].ioId);       EXPECT_EQ("plain", back[0].value);
    EXPECT_EQ("we|ird=id%", back[1].ioId); EXPECT_EQ(SEPARATOR_VALUE, back[1].value);
    EXPECT_EQ("io_3", back[2].ioId);       EXPECT_EQ("", back[2].value);
}

TEST_F(AutoScenarioDefTest, AnActionWhoseIoDoesNotExistIsKeptByTheCodec)
{
    /* ⭐ CRITERION 3 of E4.6.md §6, at the codec level. `e46b_nowhere` names no
     * IO and never will. The codec NEVER resolves an id, so the action is
     * loaded, kept in the middle of its list, and written back unchanged.
     *
     * This is what makes RC3 inexpressible: toJson() drops such an action
     * today (IO/Scenario.cpp:158, :181) and a client that reads back and
     * echoes loses it for ever.
     */
    AutoScenarioDef def;
    ASSERT_TRUE(def.loadFromParams(referenceDefinitionParams()));

    ASSERT_EQ(3u, def.steps[1].actions.size());
    EXPECT_EQ(IO_NOWHERE, def.steps[1].actions[2].ioId)
            << "the unresolvable action was dropped, or moved";
    EXPECT_TRUE(ListeRoom::Instance().get_io(IO_NOWHERE) == nullptr)
            << "the probe id must name nothing, or this case proves nothing";

    Params written;
    def.saveToParams(written);
    EXPECT_EQ("e46b_blind=31|e46b_label=a%7Cb%3Dc%25d%7Ce|e46b_nowhere=19",
              written.get_param_const("as_s1_actions"));
}

TEST_F(AutoScenarioDefTest, TheStepListIsTheOnlyAuthorityOnOrderAndIdentity)
{
    /* ⭐ CRITERION 5 of E4.6.md §6, PROVED BY EXCHANGE: the same `as_*` params
     * are read twice and only `autoscenario_steps` differs. Nothing else in
     * the input changes - not a value, not a key - so an implementation that
     * took its order from anywhere else (the alphabetical walk of the map, the
     * insertion order, the numeric tail of the id) cannot answer both.
     */
    Params a = referenceDefinitionParams();
    Params b = referenceDefinitionParams();
    b.Add(AutoScenarioDef::KEY_STEPS, "s2|s1");   //reordered AND shortened

    AutoScenarioDef defA, defB;
    ASSERT_TRUE(defA.loadFromParams(a));
    ASSERT_TRUE(defB.loadFromParams(b));

    ASSERT_EQ(3u, defA.steps.size());
    ASSERT_EQ(2u, defB.steps.size()) << "s3 is not listed and must not load";

    EXPECT_EQ("s3", defA.steps[0].stepId);
    EXPECT_EQ("s2", defB.steps[0].stepId);
    EXPECT_DOUBLE_EQ(1.25, defA.steps[0].pause);
    EXPECT_DOUBLE_EQ(0.75, defB.steps[0].pause);
    EXPECT_EQ("e46b_lamp", defA.steps[0].actions[0].ioId);
    EXPECT_EQ("e46b_banner", defB.steps[0].actions[0].ioId);

    //and the params of the step the list dropped are gone at the next save
    Params written;
    defB.saveToParams(written);
    EXPECT_FALSE(written.Exists("as_s3_pause"));
    EXPECT_FALSE(written.Exists("as_s3_actions"));
    EXPECT_EQ("s2|s1", written.get_param_const(AutoScenarioDef::KEY_STEPS));
}

TEST_F(AutoScenarioDefTest, ADuplicatedOrMalformedStepTokenIsRefused)
{
    /* A step id is also a fragment of the param name that carries the step, so
     * it is restricted to [A-Za-z0-9_]. A token outside that alphabet cannot
     * address a param and is refused; so is a repeat, and so is the reserved
     * "final" token, which would otherwise alias as_final_actions.
     */
    Params p = referenceDefinitionParams();
    p.Add(AutoScenarioDef::KEY_STEPS, "s1|s1|bad id|s2|final|s1");

    AutoScenarioDef def;
    ASSERT_TRUE(def.loadFromParams(p));

    ASSERT_EQ(2u, def.steps.size());
    EXPECT_EQ("s1", def.steps[0].stepId);
    EXPECT_EQ("s2", def.steps[1].stepId);
    //the final step is still read from its own key, untouched by the alias
    ASSERT_EQ(2u, def.finalStep.actions.size());
    EXPECT_EQ("e46b_siren", def.finalStep.actions[0].ioId);
}

TEST_F(AutoScenarioDefTest, AParamsWithoutAUidCarriesNoDefinitionAndIsLeftAlone)
{
    /* Two halves of the same rule. A Scenario IO that is not an auto scenario
     * - configs/solanora has eight of them - must come out of a save byte for
     * byte the file it went in as: the codec writes nothing AND removes
     * nothing.
     */
    Params p;
    p.Add("id", "io_9");
    p.Add("type", "scenario");
    p.Add("auto_scenario", "scenario_0");   //the LEGACY marker, on its own
    p.Add("as_s1_actions", "io_1=true");    //and a stray param of our namespace

    AutoScenarioDef def;
    EXPECT_FALSE(def.loadFromParams(p));
    EXPECT_FALSE(def.isDefined());

    def.saveToParams(p);
    EXPECT_EQ(4, p.size());
    EXPECT_EQ("scenario_0", p.get_param_const("auto_scenario"));
    EXPECT_EQ("io_1=true", p.get_param_const("as_s1_actions"));
}

TEST_F(AutoScenarioDefTest, TheLegacyMarkerIsInNeitherOwnedNamespaceAndIsNeverRemoved)
{
    /* ⭐ THE STATIC HALF OF THE GUARD RAIL, and the cheapest one to check.
     *
     * `auto_scenario` is what ListeRoom::checkAutoScenario() reads to decide
     * whether a rule is an orphan to destroy (ListeRoom.cpp:324). The orphan
     * clean up of D2 must never be able to reach it - not the param on the IO,
     * and not, by the same token, anything else the codec does not own.
     */
    EXPECT_FALSE(AutoScenarioDef::isDefinitionParam("auto_scenario"));
    EXPECT_FALSE(AutoScenarioDef::isDefinitionParam("auto_scenario_step"));
    EXPECT_FALSE(AutoScenarioDef::isDefinitionParam("auto_scenario_type"));
    EXPECT_FALSE(AutoScenarioDef::isDefinitionParam("e46b_probe_param"));
    EXPECT_FALSE(AutoScenarioDef::isDefinitionParam("assistant"));
    EXPECT_FALSE(AutoScenarioDef::isDefinitionParam("as_s1_colour"));

    EXPECT_TRUE(AutoScenarioDef::isDefinitionParam("autoscenario_uid"));
    EXPECT_TRUE(AutoScenarioDef::isDefinitionParam("autoscenario_steps"));
    EXPECT_TRUE(AutoScenarioDef::isDefinitionParam("as_s1_pause"));
    EXPECT_TRUE(AutoScenarioDef::isDefinitionParam("as_s1_actions"));
    EXPECT_TRUE(AutoScenarioDef::isDefinitionParam("as_final_actions"));

    //and the clean up, run for real, leaves everything that is not ours
    Params p = referenceDefinitionParams();
    p.Add("auto_scenario", "scenario_0");
    p.Add("as_s9_actions", "io_1=true");   //ours, orphan -> goes
    p.Add("assistant", "keep me");         //not ours -> stays

    AutoScenarioDef def;
    ASSERT_TRUE(def.loadFromParams(p));
    def.saveToParams(p);

    EXPECT_EQ("scenario_0", p.get_param_const("auto_scenario"));
    EXPECT_EQ("keep me", p.get_param_const("assistant"));
    EXPECT_FALSE(p.Exists("as_s9_actions"));
}

/*******************************************************************************
 * SECTION 3 - THE IDS: OPAQUE, AND NEVER RECYCLED (D3)
 ******************************************************************************/

TEST_F(AutoScenarioDefTest, AUidIsNeverHandedOutTwiceEvenAfterItIsObserved)
{
    /* "Never recycled" is not "unique among the live ones": a uid freed by a
     * deletion must never come back, and a uid read from a configuration file
     * the process did not write must not be allocated on top of.
     */
    const std::string first = AutoScenarioDef::newUid();
    const std::string second = AutoScenarioDef::newUid();
    EXPECT_NE(first, second);

    //observing an id already handed out changes nothing
    AutoScenarioDef::observeUid(first);
    const std::string third = AutoScenarioDef::newUid();
    EXPECT_NE(third, first);
    EXPECT_NE(third, second);

    //observing one from FAR ahead - a config written by another process -
    //pushes the counter past it
    AutoScenarioDef::observeUid("as_90001");
    EXPECT_EQ("as_90002", AutoScenarioDef::newUid());

    //a uid that is not of our shape teaches the counter nothing
    AutoScenarioDef::observeUid("scenario_0");
    AutoScenarioDef::observeUid("as_");
    AutoScenarioDef::observeUid("as_x1");
    EXPECT_EQ("as_90003", AutoScenarioDef::newUid());

    //same contract on the step ids, and the two counters are independent
    AutoScenarioDef::observeStepId("s70001");
    EXPECT_EQ("s70002", AutoScenarioDef::newStepId());
    EXPECT_EQ("as_90004", AutoScenarioDef::newUid());
}

/*******************************************************************************
 * SECTION 4 - THE SAME PROPERTIES END TO END, THROUGH io.xml
 ******************************************************************************/

TEST_F(AutoScenarioDefTest, TheDefinitionSurvivesASaveReloadSaveCycleByteForByte)
{
    /* ⭐ CRITERION 1 again, but through the FILE: the definition params of the
     * scenario IO are compared, byte for byte, between two save/reload turns.
     * The pure codec case above proves the transformation; this one proves the
     * whole path - pugixml, Params, the constructor, the capture at save.
     *
     * `cycle` and `enabled` are made to DIVERGE first. E4.0c showed by
     * exchange that they are `false`/`true` everywhere in the goldens, so
     * swapping the two keys stayed green in 52 binaries.
     */
    loadRichScenarioFromDisk();

    const std::string firstXml = ioXmlOnDisk();
    const std::map<std::string, std::string> first = scenarioIoAttributes(firstXml);
    ASSERT_FALSE(first.empty());
    EXPECT_EQ("true", first.at("autoscenario_cycle"));
    EXPECT_EQ("false", first.at("autoscenario_enabled"));

    clearCoreState();
    loadConfig(firstXml, rulesXmlOnDisk());
    ListeRoom::Instance().checkAutoScenario();
    saveConfig();

    const std::map<std::string, std::string> second = scenarioIoAttributes(ioXmlOnDisk());

    //compare ONLY the definition namespaces: the rest of the IO (name, room,
    //state) is not this ticket's contract
    std::string firstDef, secondDef;
    for (const auto &kv: first)
        if (AutoScenarioDef::isDefinitionParam(kv.first))
            firstDef += kv.first + "=" + kv.second + "\n";
    for (const auto &kv: second)
        if (AutoScenarioDef::isDefinitionParam(kv.first))
            secondDef += kv.first + "=" + kv.second + "\n";

    EXPECT_FALSE(firstDef.empty());
    EXPECT_EQ(firstDef, secondDef);
    //the uid is allocated ONCE and reused, never re-minted at every save
    EXPECT_EQ(first.at("autoscenario_uid"), second.at("autoscenario_uid"));
}

TEST_F(AutoScenarioDefTest, AnActionWhoseIoVanishedIsStillNamedByTheDefinitionInIoXml)
{
    /* ⭐ CRITERION 3 end to end, and it is the property RC9 needs.
     *
     * The IO element is removed from io.xml, the way calaos_installer does
     * when it regenerates a project it loaded with an unresolved reference.
     * TODAY the server forgets the id ever existed as soon as rules.xml also
     * loses it. From E4.6b the DEFINITION still names it, on disk, so nothing
     * of the model was lost - E4.6c is what makes the rules follow again
     * (D10 level 0), and this is the half that has to land first.
     */
    loadRichScenarioFromDisk();

    std::string ioXml = ioXmlOnDisk();
    const std::string rulesXml = rulesXmlOnDisk();

    //remove the IO ELEMENT of the value carrying action, by surgery on the
    //element and not on the id string - which the definition now also carries
    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_buffer(ioXml.data(), ioXml.size()));
    bool removed = false;
    for (pugi::xml_node node: ioNodes(doc))
    {
        if (std::string(node.attribute("id").value()) != IO_LABEL) continue;
        node.parent().remove_child(node);
        removed = true;
        break;
    }
    ASSERT_TRUE(removed);
    {
        std::ostringstream ss;
        doc.save(ss);
        ioXml = ss.str();
    }
    ASSERT_EQ(std::string::npos, ioXml.find(std::string("id=\"") + IO_LABEL + "\""));

    clearCoreState();
    loadConfig(ioXml, rulesXml);
    ListeRoom::Instance().checkAutoScenario();
    saveConfig();

    ASSERT_TRUE(io(IO_LABEL) == nullptr) << "the IO is supposed to be gone";

    Scenario *sc = scenarioIo();
    ASSERT_TRUE(sc != nullptr);
    ASSERT_TRUE(sc->getDefinition() != nullptr);
    ASSERT_EQ(3u, sc->getDefinition()->steps.size());

    const std::vector<AutoScenarioDefAction> &acts = sc->getDefinition()->steps[1].actions;
    ASSERT_EQ(3u, acts.size()) << "the dead action was escamoted";
    EXPECT_EQ(IO_LABEL, acts[1].ioId) << "and it is still in the MIDDLE";
    EXPECT_EQ(SEPARATOR_VALUE, acts[1].value) << "with its value intact";

    //and it is on DISK, not only in memory
    EXPECT_NE(std::string::npos, ioXmlOnDisk().find(IO_LABEL));
    EXPECT_NE(std::string::npos, ioXmlOnDisk().find("a%7Cb%3Dc%25d%7Ce"));
}

TEST_F(AutoScenarioDefTest, APlainScenarioIoWithoutTheMarkerGetsNoDefinitionParam)
{
    /* configs/solanora has eight `type="scenario"` IOs and ZERO
     * `auto_scenario` (E4.6.md §2.6). Not one byte of its io.xml may move
     * because of this ticket: no definition, nothing written, nothing removed.
     */
    //A scenario IO written the way the server writes one, and WITHOUT the
    //`auto_scenario` marker: a plain button.
    const std::string plainScenario =
            "    <calaos:input type=\"scenario\" id=\"e46b_plain\" name=\"Bouton\""
            " enabled=\"true\" visible=\"true\" io_type=\"inout\" />\n";

    loadConfig(ioXmlDocument(roomXml(ROOM_NAME_E46B, ROOM_TYPE_E46B,
                                     houseIosXml() + plainScenario, 0)),
               rulesXmlDocument(std::string()));

    Scenario *sc = dynamic_cast<Scenario *>(io("e46b_plain"));
    ASSERT_TRUE(sc != nullptr);
    EXPECT_TRUE(sc->getAutoScenario() == nullptr);

    saveConfig();
    const std::string before = ioXmlOnDisk();
    EXPECT_TRUE(definitionKeysOf(before).empty());

    //a second save changes nothing either
    saveConfig();
    EXPECT_EQ(before, ioXmlOnDisk());
}
