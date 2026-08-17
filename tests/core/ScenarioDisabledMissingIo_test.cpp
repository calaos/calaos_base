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
 * T3.18 - a scenario whose step lost its IO is DISABLED, and stays disabled
 * until someone re-enables it by hand (user decision, DECISIONS.md
 * "Scenario ampute").
 *
 * ---------------------------------------------------------------------------
 * COMMIT 1 OF 2 - what is in this file RIGHT NOW, and why only that
 * ---------------------------------------------------------------------------
 * T3.18 CHANGES A BEHAVIOUR, so most of its tests cannot be green before the
 * fix: they assert the new behaviour by construction. Putting them here would
 * mean committing a red suite, which is worse than useless.
 *
 * What CAN be green before the fix - and is - are the assumptions T3.18 RESTS
 * ON. Each of the three cases below pins a pre-existing fact that the fix does
 * not create but absolutely relies on; every one of them is exactly as
 * meaningful after the fix as before it, and each one is a real trap:
 *
 *   1. ModifyDoesNotClearAnUnknownScenarioParam
 *      The persisted flag T3.18 adds lives in the Params of the Scenario IO,
 *      the very same Params `autoscenario modify` rewrites. It survives only
 *      because buildAutoscenarioModify() applies four NAMED keys instead of
 *      replacing the params in bulk. That is a fact of implementation, not a
 *      designed guarantee, and the first rewrite of that function would reopen
 *      the hole in silence - a `modify` restarting a broken scenario.
 *
 *   2. TheDisabledParamIsTheSchedulingChoiceAndIsRewrittenByModify
 *      Why the fix must NOT reuse the existing `disabled` param: it is a USER
 *      choice ("do not run this on its schedule"), exposed as "enabled" in the
 *      payload, and REWRITTEN by every modify. Recycling it would let the first
 *      modify of any client put a broken scenario back to work. Pinned here so
 *      the two params are seen doing different things.
 *
 *   3. RemovingAndReAddingAScheduleLeavesNoZombieRule
 *      T3.18 makes "disable the rules" the DEFAULT of the IO detach path and
 *      leaves "destroy them" explicit at the teardown sites. This case pins
 *      what those sites must keep doing: the schedule IOs have deterministic
 *      ids and are recreated identically, so their rules have to die with them
 *      or the next build duplicates them and nothing collects the duplicates.
 *      Without it, the new default is a time bomb.
 *
 * Commit 2 adds the fix and the twelve cases that describe it: the two gates,
 * the persisted sticky flag, the save/reload cycles, the manual re-enable and
 * its refusal, the clean stop, and the four states of the payload.
 ******************************************************************************/

#include "CalaosCoreFixture.h"

#include "AutoScenario.h"
#include "JsonApi.h"
#include "Scenario.h"

#include <string>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/* Every id is t318_ prefixed and used nowhere else in the tree: Config's IO
 * state cache is process wide and never cleared (CalaosCoreFixture.h), so
 * sharing an id with another test binary leaks values across binaries.
 */
const char T318_ROOM[] = "T3.18 room";
const char T318_ROOM_TYPE[] = "salon";

const char TARGET_1[] = "t318_first";
const char TARGET_2[] = "t318_missing";   //the one that vanishes, in commit 2
const char TARGET_3[] = "t318_third";

const char SC_ID[] = "t318_sc";
const char SC_IO[] = "io_t318_sc";

//The param T3.18 adds. Spelled out here so that commit 2 cannot rename it
//without this file noticing.
const char FLAG[] = "disabled_missing_io";

/* buildAutoscenarioModify() is protected. Nothing is overridden: the round trip
 * cases drive the real production function, not a copy of it.
 */
class ApiProbe: public JsonApi
{
public:
    using JsonApi::buildAutoscenarioModify;
};

} //namespace

class ScenarioDisabledMissingIoTest: public CoreFixture
{
protected:
    void loadHouse()
    {
        loadConfig(ioXmlDocument(roomXml(T318_ROOM, T318_ROOM_TYPE,
                                         internalIoXml("InternalBool", TARGET_1, "First") +
                                         internalIoXml("InternalBool", TARGET_2, "Missing") +
                                         internalIoXml("InternalBool", TARGET_3, "Third"),
                                         0)),
                   rulesXmlDocument(std::string()));

        //A value left in Config's process wide state cache by another case of
        //this same binary would otherwise come back on the next load
        forgetIOState(TARGET_1);
        forgetIOState(TARGET_2);
        forgetIOState(TARGET_3);
    }

    static Scenario *scenarioIo(const std::string &id = SC_IO)
    {
        return dynamic_cast<Scenario *>(io(id));
    }

    static AutoScenario *autoScenario(const std::string &id = SC_IO)
    {
        Scenario *sc = scenarioIo(id);
        return sc? sc->getAutoScenario(): nullptr;
    }

    //A scenario IO with its machinery rules, no step yet
    static AutoScenario *createScenario(const std::string &ioId, const std::string &scId)
    {
        //`disabled` is defaulted to "true" exactly like buildAutoscenarioCreate()
        //does (JsonApi.cpp), so the fixture carries the SAME user setting the
        //API produces - and so that `disabled` and `disabled_missing_io`, which
        //share this Params and mean opposite things, can be observed in
        //disagreement rather than both sitting at their default.
        Params p = {{ "type", "Scenario" },
                    { "id", ioId },
                    { "name", "Scenario " + scId },
                    { "disabled", "true" },
                    { "auto_scenario", scId }};

        Scenario *sc = dynamic_cast<Scenario *>(createIO(p));
        if (!sc || !sc->getAutoScenario()) return nullptr;
        if (!sc->getAutoScenario()->checkScenarioRules()) return nullptr;

        return sc->getAutoScenario();
    }

    /* The scenario every case works on: three steps, one action each, the
     * SECOND one on the IO that is going to disappear in commit 2. Three steps
     * and not two so that the amputation loses a step in the MIDDLE: losing the
     * last one would be indistinguishable from a shorter scenario.
     */
    AutoScenario *buildReferenceScenario()
    {
        AutoScenario *as = createScenario(SC_IO, SC_ID);
        if (!as) return nullptr;

        as->addStep(0.0);
        as->addStepAction(0, io(TARGET_1), "true");
        as->addStep(0.0);
        as->addStepAction(1, io(TARGET_2), "true");
        as->addStep(0.0);
        as->addStepAction(2, io(TARGET_3), "true");

        //What the server does at every startup: renumber and re-chain
        as->checkScenarioRules();

        return as;
    }

    //The `autoscenario modify` a client sends back. `steps` is empty on
    //purpose: this is the round trip, not a step edit.
    static json_t *modifyRequest(const std::string &name, const std::string &cycle,
                                 const std::string &disabled)
    {
        json_t *jreq = json_object();
        json_object_set_new(jreq, "id", json_string(SC_IO));
        json_object_set_new(jreq, "name", json_string(name.c_str()));
        json_object_set_new(jreq, "cycle", json_string(cycle.c_str()));
        json_object_set_new(jreq, "disabled", json_string(disabled.c_str()));
        json_object_set_new(jreq, "room_name", json_string(T318_ROOM));
        json_object_set_new(jreq, "room_type", json_string(T318_ROOM_TYPE));
        json_object_set_new(jreq, "steps", json_array());
        return jreq;
    }

    //The flag as it really is in io.xml on disk
    bool flagIsOnDisk() const
    {
        return ioXmlOnDisk().find(std::string(FLAG) + "=\"true\"") != std::string::npos;
    }
};

/*******************************************************************************
 * 1. The guard rail: `autoscenario modify` must leave an unknown param alone
 ******************************************************************************/

TEST_F(ScenarioDisabledMissingIoTest, ModifyDoesNotClearAnUnknownScenarioParam)
{
    /* THE guard rail of this ticket, and the reason it is in the first commit:
     * it is true today and it MUST STILL BE TRUE once the flag exists.
     * buildAutoscenarioModify() does not replace the params of the Scenario IO
     * in bulk, it compares and applies four named keys (name, visible, cycle,
     * disabled) plus the room. An unknown param therefore survives - a FACT OF
     * IMPLEMENTATION, not a designed guarantee.
     */
    loadHouse();
    ASSERT_NE(buildReferenceScenario(), nullptr);

    Scenario *sc = scenarioIo();
    ASSERT_NE(sc, nullptr);
    sc->set_param(FLAG, "true");
    ASSERT_EQ("true", sc->get_param(FLAG));

    json_t *jreq = modifyRequest("Renamed by the client", "true", "false");
    ApiProbe api;
    json_t *jret = api.buildAutoscenarioModify(jreq);
    ASSERT_NE(jret, nullptr);
    EXPECT_EQ("true", jansson_string_get(jret, "success"));
    json_decref(jret);
    json_decref(jreq);

    sc = scenarioIo();
    ASSERT_NE(sc, nullptr);

    //what modify DOES own is rewritten...
    EXPECT_EQ("Renamed by the client", sc->get_param("name"));
    EXPECT_EQ("false", sc->get_param("disabled"));
    EXPECT_TRUE(sc->getAutoScenario()->isCycling());

    //...and what it does not own is left alone. This is the one place where
    //`disabled` and `disabled_missing_io` - same Params, opposite meanings -
    //are observed being treated DIFFERENTLY.
    EXPECT_EQ("true", sc->get_param(FLAG))
            << "autoscenario modify cleared an unknown param of the Scenario IO";

    //and it survives the SaveConfigIO() modify does right after
    EXPECT_TRUE(flagIsOnDisk());
}

/*******************************************************************************
 * 2. Why the existing `disabled` param cannot be recycled
 ******************************************************************************/

TEST_F(ScenarioDisabledMissingIoTest, TheDisabledParamIsTheSchedulingChoiceAndIsRewrittenByModify)
{
    /* `disabled` is a USER choice: its only effect is to switch the scheduling
     * off (ioScheduleEnabled), the scenario stays startable from the button. It
     * is exposed as "enabled" - NEGATED - in the payload, and it is REWRITTEN
     * by every modify. That is precisely why T3.18 must not reuse it: the first
     * `autoscenario modify` any client sends would put a broken scenario back
     * to work.
     */
    loadHouse();
    AutoScenario *as = buildReferenceScenario();
    ASSERT_NE(as, nullptr);
    ASSERT_TRUE(as->isDisabled());

    //the payload emits the NEGATION, under another name
    json_t *j = scenarioIo()->toJson();
    ASSERT_NE(j, nullptr);
    EXPECT_EQ("false", jansson_string_get(j, "enabled"));
    json_decref(j);

    //a modify flips it, without the client ever naming "enabled"
    json_t *jreq = modifyRequest("Scenario t318_sc", "false", "false");
    ApiProbe api;
    json_t *jret = api.buildAutoscenarioModify(jreq);
    ASSERT_NE(jret, nullptr);
    json_decref(jret);
    json_decref(jreq);

    as = autoScenario();
    ASSERT_NE(as, nullptr);
    EXPECT_FALSE(as->isDisabled()) << "modify does not own `disabled` any more";

    j = scenarioIo()->toJson();
    ASSERT_NE(j, nullptr);
    EXPECT_EQ("true", jansson_string_get(j, "enabled"));
    json_decref(j);
}

/*******************************************************************************
 * 3. The teardown sites must keep destroying their rules
 ******************************************************************************/

TEST_F(ScenarioDisabledMissingIoTest, RemovingAndReAddingAScheduleLeavesNoZombieRule)
{
    /* This is what protects the risky half of commit 2: "disable" becomes the
     * DEFAULT of the IO detach path, and "destroy" has to stay explicit at the
     * teardown sites. Those sites destroy IOs whose ids are DETERMINISTIC and
     * recreated identically right after: kept disabled, their rules would be
     * duplicated by the next build and nothing would ever collect the
     * duplicates (Rule::setAutoScenario(false) does not exist, so the
     * auto_scenario sweep of checkAutoScenario() misses them).
     */
    loadHouse();
    AutoScenario *as = buildReferenceScenario();
    ASSERT_NE(as, nullptr);
    as->setCycling(true);

    auto countRules = [](const std::string &suffix)
    {
        int n = 0;
        for (int i = 0; i < ListeRule::Instance().size(); i++)
        {
            if (ListeRule::Instance().get_rule(i)->get_name() == std::string(SC_ID) + suffix)
                n++;
        }
        return n;
    };

    as->addSchedule();
    ASSERT_NE(as->getIOTimeRange(), nullptr);
    ASSERT_EQ(1, countRules("_time_start"));
    ASSERT_EQ(1, countRules("_time_stop"));

    as->deleteSchedule();
    EXPECT_EQ(nullptr, as->getIOTimeRange());
    EXPECT_EQ(0, countRules("_time_start")) << "a zombie schedule rule was left behind";
    EXPECT_EQ(0, countRules("_time_stop")) << "a zombie schedule rule was left behind";

    as->addSchedule();
    EXPECT_EQ(1, countRules("_time_start")) << "the schedule rules were duplicated";
    EXPECT_EQ(1, countRules("_time_stop")) << "the schedule rules were duplicated";

    //the same for a full delete: the rules of the scenario must go with it
    const int before = ListeRule::Instance().size();
    as->deleteAll();
    EXPECT_LT(ListeRule::Instance().size(), before);
}
