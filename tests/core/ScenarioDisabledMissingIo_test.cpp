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
 * until someone re-enables it by hand.
 *
 * USER DECISION, taken against the initial scoping and then hardened
 * (docs/refactoring/DECISIONS.md, "Scenario ampute"):
 *
 *   "On desactive le scenario, on le flag avec un nouveau parametre dans la
 *    config pour que ca survive a un reboot, et un user doit corriger le
 *    scenario manuellement en le reactivant. Si un IO disparait c'est un
 *    probleme, on ne peut pas le resoudre sans intervention manuelle, et un IO
 *    dans Calaos ne se supprime pas comme ca."
 *
 * So the disabling is STICKY: it does NOT clear itself when the IO comes back.
 * That is what most of this file is about, and it is the one property that
 * cannot be read off the code - it only shows up across real save/reload
 * cycles, which is why they are played for real here.
 *
 * ---------------------------------------------------------------------------
 * Two commits, and what went where
 * ---------------------------------------------------------------------------
 * T3.18 CHANGES a behaviour, so most of what is below cannot be green before
 * the fix: it asserts the new behaviour by construction. The first commit
 * therefore carried only the three cases that were green against an UNTOUCHED
 * src/, because they pin the pre-existing facts the fix RESTS ON - the four
 * named keys of buildAutoscenarioModify(), the meaning of the existing
 * `disabled` param, and the teardown sites that must keep destroying their
 * rules. All three are still here, unchanged in substance; the modify one was
 * renamed from ModifyDoesNotClearAnUnknownScenarioParam now that the param has
 * a name. Everything else came with the fix, in the second commit.
 *
 * ---------------------------------------------------------------------------
 * The two gates
 * ---------------------------------------------------------------------------
 *      the scenario starts  <=>  !isBroken()  AND  !disabled_missing_io
 *
 *  - gate 1, isBroken(): LIVE, derived from Rule::isDisabled() over the rules
 *    of the scenario (E4.2e) and from a step rule destroyed under us (E4.2f).
 *    Stored nowhere, so no client can forge it.
 *  - gate 2, disabled_missing_io: a PERSISTED param of the Scenario IO, sticky,
 *    cleared only by AutoScenario::tryReenable().
 * Neither is redundant: gate 1 alone would clear itself as soon as the IO came
 * back (refused by the arbitration), gate 2 alone would be forgeable, since
 * set_param/del_param accept any (io, param) pair without a whitelist.
 * BothGatesRefuseOnTheirOwn exercises the two directions.
 *
 * ---------------------------------------------------------------------------
 * How a scenario is run here, and why it is not cheating
 * ---------------------------------------------------------------------------
 * A scenario is paced by its InputTimer, which is a libuv object; no libuv loop
 * runs in the core tests (CalaosCoreFixture.h), so the chain never advances on
 * its own. runScenario() below therefore does two things, and only those two:
 *   - it presses the button for real - Scenario::set_value(true), the entry
 *     point of both start paths, the button and the schedule,
 *   - and, ONLY IF the scenario really started (ioIsActive became true, which
 *     is the _button_start rule's doing and nothing else), it runs the actions
 *     of the step rules in order.
 * Nothing is short circuited: a scenario that does not start runs zero step,
 * because every step rule needs `ioIsActive == true` to pass its conditions and
 * the timer is never started either. That is exactly the difference this ticket
 * is about - DISABLED (nothing runs) versus AMPUTATED (the surviving steps run
 * a sequence the user never wrote).
 ******************************************************************************/

#include "CalaosCoreFixture.h"

#include "AutoScenario.h"
#include "JsonApi.h"
#include "Scenario.h"

#include <algorithm>
#include <string>
#include <vector>

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
const char TARGET_2[] = "t318_missing";   //the one that vanishes
const char TARGET_3[] = "t318_third";

const char SC_ID[] = "t318_sc";
const char SC_IO[] = "io_t318_sc";

//The second scenario, used as the action of a step of the first one: the only
//"hot" deletion an authenticated client can actually reach today
const char SC_B_ID[] = "t318_scb";
const char SC_B_IO[] = "io_t318_scb";

/* buildAutoscenarioModify() is protected. Nothing else of JsonApi is needed and
 * nothing is overridden: the round trip test drives the real production
 * function, not a copy of it.
 */
class ApiProbe: public JsonApi
{
public:
    using JsonApi::buildAutoscenarioModify;
    using JsonApi::buildAutoscenarioReenable;
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
     * SECOND one on the IO that is going to disappear. Three steps and not two
     * so that the amputation loses a step in the MIDDLE: losing the last one
     * would be indistinguishable from a shorter scenario.
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

    /* Amputate one IO element from an io.xml document, the way an installer
     * deleting a line from the file does. Self-closing elements only, which is
     * what every internal IO is written as. Same helper as E4.0c.
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

    /* Save everything, make `missingId` vanish from io.xml, and load it all
     * back followed by checkAutoScenario() - i.e. exactly what a reboot does
     * after somebody removed an IO from the configuration. This is the ONLY
     * path an installer really takes, and the one E4.2e's detection runs on.
     */
    void saveAmputateAndReload(const std::string &missingId)
    {
        saveConfig();

        std::string ioXml = ioXmlOnDisk();
        const std::string rulesXml = rulesXmlOnDisk();

        ASSERT_TRUE(removeIoFromXml(ioXml, missingId))
                << "the IO to amputate was not found in the saved io.xml";
        //the rules keep the dead reference verbatim (ActionStd::SaveToXml(),
        //E4.2e): that is what makes the whole ticket possible
        ASSERT_NE(std::string::npos, rulesXml.find(missingId));

        clearCoreState();
        loadConfig(ioXml, rulesXml);
        ListeRoom::Instance().checkAutoScenario();
    }

    //Full save/reload cycle of what is currently in memory, plus the startup
    //pass. No amputation: this is a plain reboot.
    void saveAndReload()
    {
        saveConfig();
        reloadFromDisk();
        ListeRoom::Instance().checkAutoScenario();
    }

    /* Press the button and, if the scenario really started, run its steps.
     * See the header: nothing is short circuited, ioIsActive is set by the
     * _button_start rule and by nothing else here.
     * Answers the number of step rules that executed their actions.
     */
    static int runScenario(const std::string &ioId = SC_IO)
    {
        Scenario *sc = scenarioIo(ioId);
        AutoScenario *as = sc? sc->getAutoScenario(): nullptr;
        if (!sc || !as) return 0;

        sc->set_value(true);

        if (!as->getIOIsActive() || !as->getIOIsActive()->get_value_bool())
            return 0; //it did not start: no step rule can ever pass its conditions

        int ran = 0;
        for (Rule *step: as->getRuleSteps())
        {
            if (step && step->ExecuteActions())
                ran++;
        }

        return ran;
    }

    static bool targetIsSet(const char *id)
    {
        IOBase *o = io(id);
        return o && o->get_value_bool();
    }

    //The `disabled_missing_io` attribute as it really is in io.xml on disk
    bool flagIsOnDisk() const
    {
        return ioXmlOnDisk().find("disabled_missing_io=\"true\"") != std::string::npos;
    }

    static Json toJsonOf(const std::string &ioId = SC_IO)
    {
        Scenario *sc = scenarioIo(ioId);
        if (!sc) return Json::object();

        return sc->toJson();
    }
};

/*******************************************************************************
 * The control: a healthy scenario is strictly unchanged
 ******************************************************************************/

TEST_F(ScenarioDisabledMissingIoTest, AHealthyScenarioRunsAllItsStepsAndCarriesNoFlag)
{
    loadHouse();
    AutoScenario *as = buildReferenceScenario();
    ASSERT_NE(as, nullptr);
    ASSERT_EQ(3u, as->getRuleSteps().size());

    EXPECT_FALSE(as->isBroken());
    EXPECT_FALSE(as->isDisabledMissingIo());
    EXPECT_EQ("", as->getMissingIoDescription());

    EXPECT_EQ(3, runScenario());
    EXPECT_TRUE(targetIsSet(TARGET_1));
    EXPECT_TRUE(targetIsSet(TARGET_2));
    EXPECT_TRUE(targetIsSet(TARGET_3));

    /* The healthy io.xml must be EXACTLY the one it always was: the flag is
     * ABSENT, never disabled_missing_io="false". A leftover "false" would leak
     * into every configuration that was repaired once.
     */
    saveConfig();
    EXPECT_EQ(std::string::npos, ioXmlOnDisk().find("disabled_missing_io"))
            << "a healthy scenario must not carry the param at all";
}

/*******************************************************************************
 * The decision itself: disabled, not amputated
 ******************************************************************************/

TEST_F(ScenarioDisabledMissingIoTest, LosingTheIoOfAStepDisablesTheWholeScenario)
{
    loadHouse();
    ASSERT_NE(buildReferenceScenario(), nullptr);

    saveAmputateAndReload(TARGET_2);

    AutoScenario *as = autoScenario();
    ASSERT_NE(as, nullptr);
    ASSERT_EQ(nullptr, io(TARGET_2)) << "the IO really has to be gone";

    //(a) the step rule EXISTS STILL - it was not destroyed
    const std::vector<Rule *> steps = as->getRuleSteps();
    ASSERT_EQ(3u, steps.size()) << "the step rule was destroyed instead of disabled";
    ASSERT_NE(steps[1], nullptr);

    //(b) and it is disabled, naming the right id
    EXPECT_TRUE(steps[1]->isDisabled());
    EXPECT_EQ(TARGET_2, steps[1]->getMissingIoDescription());
    //the two others are untouched: only the guilty rule is disabled
    EXPECT_FALSE(steps[0]->isDisabled());
    EXPECT_FALSE(steps[2]->isDisabled());

    //(c) gate 1
    EXPECT_TRUE(as->isBroken());
    EXPECT_EQ(TARGET_2, as->getMissingIoDescription());

    //(d) gate 2, set by the startup detection pass
    EXPECT_TRUE(as->isDisabledMissingIo());
    EXPECT_EQ("true", scenarioIo()->get_param("disabled_missing_io"));

    /* (e) THE CENTRAL ASSERTION, the one that tells DISABLED from AMPUTATED.
     * Amputated, the scenario would start and run steps 1 and 3 - a sequence
     * the user never wrote. Disabled, it runs NOTHING.
     */
    EXPECT_EQ(0, runScenario()) << "the scenario ran a shortened sequence";
    EXPECT_FALSE(as->getIOIsActive()->get_value_bool()) << "the scenario started";
    EXPECT_FALSE(targetIsSet(TARGET_1)) << "step 1 ran: the scenario is amputated, not disabled";
    EXPECT_FALSE(targetIsSet(TARGET_3)) << "step 3 ran: the scenario is amputated, not disabled";

    //(f) and stopping still works: cutting set_value(false) would make an
    //already running scenario impossible to stop (ruleStop goes through it)
    EXPECT_TRUE(scenarioIo()->set_value(false));
}

TEST_F(ScenarioDisabledMissingIoTest, DeletingAScenarioUsedAsAStepActionDisablesTheOtherOne)
{
    /* The HOT path, and the only deletion of an IO a client can really reach
     * today: `autoscenario delete` on a scenario that another scenario uses as
     * a step action. Same outcome as the load path, through
     * ListeRoom::deleteIO() -> RemoveRule(Disable) -> refreshBrokenScenarios().
     */
    loadHouse();

    AutoScenario *asB = createScenario(SC_B_IO, SC_B_ID);
    ASSERT_NE(asB, nullptr);

    AutoScenario *as = createScenario(SC_IO, SC_ID);
    ASSERT_NE(as, nullptr);
    as->addStep(0.0);
    as->addStepAction(0, io(TARGET_1), "true");
    as->addStep(0.0);
    as->addStepAction(1, io(SC_B_IO), "true");   //step 2 starts scenario B
    as->addStep(0.0);
    as->addStepAction(2, io(TARGET_3), "true");
    as->checkScenarioRules();
    ASSERT_EQ(3u, as->getRuleSteps().size());
    ASSERT_FALSE(as->isBroken());

    const int rulesBefore = ListeRule::Instance().size();

    //`autoscenario delete` on B, verbatim: deleteAll() then deleteIO()
    asB->deleteAll();
    ASSERT_TRUE(ListeRoom::Instance().deleteIO(io(SC_B_IO)));

    //A is still whole: its step rule was kept, only disabled
    as = autoScenario();
    ASSERT_NE(as, nullptr);
    ASSERT_EQ(3u, as->getRuleSteps().size()) << "A lost a step to B's deletion";
    EXPECT_TRUE(as->getRuleSteps()[1]->isDisabled());
    EXPECT_EQ(SC_B_IO, as->getMissingIoDescription());
    EXPECT_TRUE(as->isBroken());
    EXPECT_TRUE(as->isDisabledMissingIo());

    //B's own rules did go away (Destroy at the teardown sites), A's did not
    EXPECT_LT(ListeRule::Instance().size(), rulesBefore);

    EXPECT_EQ(0, runScenario());
    EXPECT_FALSE(targetIsSet(TARGET_1));
    EXPECT_FALSE(targetIsSet(TARGET_3));
}

TEST_F(ScenarioDisabledMissingIoTest, ARuleDestroyedUnderTheScenarioBreaksItWithNoMissingId)
{
    /* The OTHER half of gate 1: a rule that was registered here and has since
     * been DESTROYED, as opposed to one that survives holding a dead reference.
     * The definition still declares three steps, so the scenario knows one of
     * its rules is missing without being able to name any id for it - that is
     * the third reason of isBroken(), and the only one that names nothing.
     *
     * >>> AND THIS IS THE THIRD PAIR OF THE PAYLOAD. <<<
     * disabled_missing_io is observed disagreeing with broken and with
     * missing_ios elsewhere in this file, but broken and missing_ios agreed in
     * every other case - so neutralising either isDangling() call of isBroken()
     * left all five binaries green. Here broken is TRUE while missing_ios is
     * EMPTY: a destroyed rule leaves no id behind, there is nothing to name,
     * and the scenario is broken all the same.
     */
    loadHouse();
    AutoScenario *as = buildReferenceScenario();
    ASSERT_NE(as, nullptr);
    ASSERT_EQ(3u, as->getRuleSteps().size());
    ASSERT_FALSE(as->isBroken());

    //--- a STEP rule destroyed under us ------------------------------------
    Rule *step = as->getRuleSteps()[1];
    ASSERT_NE(step, nullptr);
    ListeRule::Instance().Remove(step);

    /* THE PAYLOAD IS ASKED FIRST, ON PURPOSE. Serializing a scenario must not
     * be able to wipe the evidence that it is broken - it used to, because a
     * key emitted earlier ran a read accessor that compacted the dead entry
     * away. No accessor mutates any more, and this order is what keeps saying
     * so.
     */
    const Json j = toJsonOf();
    EXPECT_EQ("true", j.value("broken", std::string()))
            << "reading the payload erased gate 1 before it was asked";
    EXPECT_EQ("", j.value("missing_ios", std::string()))
            << "broken and missing_ios must be observed DISAGREEING somewhere";

    //and it is still true afterwards, with the entry now compacted away: the
    //step count alone cannot tell, only the latch can
    EXPECT_TRUE(as->isBroken()) << "a destroyed step rule does not break the scenario";
    //nothing to name: the rule that held the ids is gone with it
    EXPECT_EQ("", as->getMissingIoDescription());
    EXPECT_EQ(2u, as->getRuleSteps().size());

    //and the scenario does not start, on gate 1 ALONE - nothing ran the
    //detection pass here, so the persisted flag is not set
    EXPECT_FALSE(as->isDisabledMissingIo());
    EXPECT_EQ(0, runScenario());
    EXPECT_FALSE(targetIsSet(TARGET_1));

    //--- and a MACHINERY rule destroyed under us ---------------------------
    /* The same thing on a HEADER rule. Owning no rule at all is the normal
     * state before the first build and must not read as a breakage, which is
     * why the count is compared against what the definition calls for rather
     * than against zero.
     */
    clearCoreState();
    loadHouse();
    as = buildReferenceScenario();
    ASSERT_NE(as, nullptr);
    ASSERT_FALSE(as->isBroken());

    Rule *stepEnd = as->getRuleStepEnd();
    ASSERT_NE(stepEnd, nullptr);
    ListeRule::Instance().Remove(stepEnd);

    EXPECT_EQ(nullptr, as->getRuleStepEnd());
    EXPECT_TRUE(as->isBroken()) << "a destroyed step_end rule does not break the scenario";
    EXPECT_EQ("", as->getMissingIoDescription());
    EXPECT_EQ("true", toJsonOf().value("broken", std::string()));
    EXPECT_EQ(0, runScenario());
}

/*******************************************************************************
 * Persistence and stickiness - the heart of the user decision
 ******************************************************************************/

TEST_F(ScenarioDisabledMissingIoTest, FlagSurvivesAStartupSaveReloadCycle)
{
    /* THE test of the arbitration. checkAutoScenario() ends with
     * SaveConfigIO(): a flag that was not read back in the AutoScenario
     * constructor would be wiped from disk at the first startup, silently and
     * with no user action. And once the IO is back, the flag must STILL be
     * there - "la desactivation est collante".
     */
    loadHouse();
    ASSERT_NE(buildReferenceScenario(), nullptr);

    saveAmputateAndReload(TARGET_2);
    ASSERT_TRUE(autoScenario()->isDisabledMissingIo());

    //--- first cycle: the IO is still missing -------------------------------
    saveConfig();
    EXPECT_TRUE(flagIsOnDisk()) << "the flag was not persisted";
    const std::string rulesAfterFirstSave = rulesXmlOnDisk();

    saveAndReload();

    AutoScenario *as = autoScenario();
    ASSERT_NE(as, nullptr);
    EXPECT_TRUE(as->isDisabledMissingIo()) << "the startup SaveConfigIO() erased the flag";
    EXPECT_TRUE(as->isBroken());
    ASSERT_EQ(3u, as->getRuleSteps().size()) << "a step was lost across the cycle";

    //the three steps kept their original numbers: nothing was renumbered,
    //because nothing disappeared
    for (int i = 0; i < 3; i++)
        EXPECT_EQ(std::to_string(i), as->getRuleSteps()[i]->get_param("auto_scenario_step"));

    saveConfig();
    EXPECT_EQ(rulesAfterFirstSave, rulesXmlOnDisk())
            << "rules.xml is not byte for byte identical across the cycle";
    EXPECT_TRUE(flagIsOnDisk());

    //--- second cycle: THE IO IS BACK --------------------------------------
    Params p = {{ "type", "InternalBool" }, { "id", TARGET_2 }, { "name", "Missing" }};
    ASSERT_NE(createIO(p, firstRoom()), nullptr);

    saveAndReload();

    as = autoScenario();
    ASSERT_NE(as, nullptr);

    //gate 1 is gone - the reference resolves again, nothing is missing
    EXPECT_FALSE(as->isBroken()) << "the reload should have cleared the missing io";
    EXPECT_EQ("", as->getMissingIoDescription());

    //GATE 2 IS STILL THERE. This is the whole decision: the disabling does not
    //clear itself, it waits for a human.
    EXPECT_TRUE(as->isDisabledMissingIo())
            << "the disabling cleared itself when the IO came back";
    saveConfig();
    EXPECT_TRUE(flagIsOnDisk());

    //and the scenario still does not run
    EXPECT_EQ(0, runScenario());
    EXPECT_FALSE(targetIsSet(TARGET_1));
    EXPECT_FALSE(targetIsSet(TARGET_2));
    EXPECT_FALSE(targetIsSet(TARGET_3));
}

/*******************************************************************************
 * The manual re-enable
 ******************************************************************************/

TEST_F(ScenarioDisabledMissingIoTest, ReenableIsRefusedWhileTheIoIsStillMissing)
{
    loadHouse();
    ASSERT_NE(buildReferenceScenario(), nullptr);
    saveAmputateAndReload(TARGET_2);

    AutoScenario *as = autoScenario();
    ASSERT_NE(as, nullptr);
    ASSERT_TRUE(as->isDisabledMissingIo());

    std::string err;
    EXPECT_FALSE(as->tryReenable(err));

    //the refusal NAMES what to repair: a bare failure would leave the user with
    //nothing to act on, which is the silence this ticket removes
    EXPECT_NE(std::string::npos, err.find(TARGET_2)) << err;
    EXPECT_EQ("scenario still references missing IOs: " + std::string(TARGET_2), err);

    //and the flag is left exactly where it was - a refused re-enable changes
    //nothing at all
    EXPECT_TRUE(as->isDisabledMissingIo());
    EXPECT_EQ(0, runScenario());
}

TEST_F(ScenarioDisabledMissingIoTest, ReenableClearsTheFlagOnceTheIoIsBackAndTheScenarioRunsAgain)
{
    loadHouse();
    ASSERT_NE(buildReferenceScenario(), nullptr);
    saveAmputateAndReload(TARGET_2);

    //repair for real: the IO comes back and everything is reloaded
    Params p = {{ "type", "InternalBool" }, { "id", TARGET_2 }, { "name", "Missing" }};
    ASSERT_NE(createIO(p, firstRoom()), nullptr);
    saveAndReload();

    AutoScenario *as = autoScenario();
    ASSERT_NE(as, nullptr);
    ASSERT_FALSE(as->isBroken());
    ASSERT_TRUE(as->isDisabledMissingIo());
    ASSERT_EQ(0, runScenario()) << "it must not run before the re-enable";

    std::string err;
    EXPECT_TRUE(as->tryReenable(err));
    EXPECT_EQ("", err);
    EXPECT_FALSE(as->isDisabledMissingIo());

    //the param is REMOVED, not set to "false"
    EXPECT_FALSE(scenarioIo()->param_exists("disabled_missing_io"));
    saveConfig();
    EXPECT_EQ(std::string::npos, ioXmlOnDisk().find("disabled_missing_io"));

    //and the three steps run again, in their original order
    EXPECT_EQ(3, runScenario());
    EXPECT_TRUE(targetIsSet(TARGET_1));
    EXPECT_TRUE(targetIsSet(TARGET_2));
    EXPECT_TRUE(targetIsSet(TARGET_3));
}

TEST_F(ScenarioDisabledMissingIoTest, ReenableCommandRefusesWithTheIdsAndSucceedsOnceRepaired)
{
    /* The same two answers, through the API command T3.18 ships:
     * `autoscenario reenable`. It exists precisely because set_param CANNOT
     * refuse - re-enabling a still broken scenario through set_param would
     * answer success and change nothing, i.e. the silent no-op this ticket
     * removes, moved one level up.
     */
    loadHouse();
    ASSERT_NE(buildReferenceScenario(), nullptr);
    saveAmputateAndReload(TARGET_2);

    ApiProbe api;

    //E4.1r: buildAutoscenarioReenable() answers a Json and reads one. Not one
    //assertion of this case changed - what it observes is the refusal and the
    //sticky flag, not the shape of the document.
    const Json jreq = Json{{ "id", SC_IO }};

    const Json jrefused = api.buildAutoscenarioReenable(jreq);
    const std::string refused = jrefused.value("error", std::string());

    EXPECT_EQ("scenario still references missing IOs: " + std::string(TARGET_2), refused);
    EXPECT_TRUE(autoScenario()->isDisabledMissingIo());

    //repair, then ask again
    Params p = {{ "type", "InternalBool" }, { "id", TARGET_2 }, { "name", "Missing" }};
    ASSERT_NE(createIO(p, firstRoom()), nullptr);
    saveAndReload();
    ASSERT_TRUE(autoScenario()->isDisabledMissingIo());

    const Json jok = api.buildAutoscenarioReenable(jreq);

    EXPECT_EQ("true", jok.value("success", std::string()));
    EXPECT_FALSE(autoScenario()->isDisabledMissingIo());
    EXPECT_EQ(3, runScenario());

    //an unknown id is a plain wrong input, like every other autoscenario command
    const Json jerr = api.buildAutoscenarioReenable(Json{{ "id", "t318_nope" }});
    EXPECT_EQ("wrong input", jerr.value("error", std::string()));
}

/*******************************************************************************
 * The two gates, in both directions
 ******************************************************************************/

TEST_F(ScenarioDisabledMissingIoTest, BothGatesRefuseOnTheirOwn)
{
    loadHouse();
    ASSERT_NE(buildReferenceScenario(), nullptr);
    saveAmputateAndReload(TARGET_2);

    AutoScenario *as = autoScenario();
    ASSERT_NE(as, nullptr);
    ASSERT_TRUE(as->isBroken());
    ASSERT_TRUE(as->isDisabledMissingIo());

    /* GATE 1 alone. Any authenticated client can del_param the flag
     * (buildJsonDelParam has no whitelist), which is exactly what is done here
     * by hand. It buys nothing: the scenario is still broken and gate 1 - which
     * is derived, never stored, and therefore not forgeable - keeps refusing.
     */
    as->setDisabledMissingIo(false);
    ASSERT_FALSE(as->isDisabledMissingIo());
    ASSERT_TRUE(as->isBroken());

    EXPECT_EQ(0, runScenario()) << "gate 1 let a broken scenario start";
    EXPECT_FALSE(targetIsSet(TARGET_1));

    /* GATE 2 alone. A perfectly healthy scenario, flagged by hand: it must
     * refuse too, otherwise the persisted flag would do nothing on its own and
     * the whole stickiness would rest on gate 1 - which clears itself.
     */
    clearCoreState();
    loadHouse();
    AutoScenario *healthy = buildReferenceScenario();
    ASSERT_NE(healthy, nullptr);
    ASSERT_FALSE(healthy->isBroken());

    healthy->setDisabledMissingIo(true);
    EXPECT_EQ(0, runScenario()) << "gate 2 let a flagged scenario start";
    EXPECT_FALSE(targetIsSet(TARGET_1));
    EXPECT_FALSE(targetIsSet(TARGET_3));

    //and clearing it gives the scenario straight back
    healthy->setDisabledMissingIo(false);
    EXPECT_EQ(3, runScenario());
    EXPECT_TRUE(targetIsSet(TARGET_1));
    EXPECT_TRUE(targetIsSet(TARGET_3));
}

/*******************************************************************************
 * The clean stop
 ******************************************************************************/

TEST_F(ScenarioDisabledMissingIoTest, AScenarioBrokenWhileItRunsIsBroughtToACleanStop)
{
    /* Without this, the defect E4.2e leaves behind comes straight back: a
     * scenario stuck "in progress" for ever. ioIsActive would stay true, so
     * ruleStepEnd (which needs ioStep == -1) would never come and
     * _button_start (which needs ioIsActive == false) could never restart it.
     */
    loadHouse();

    AutoScenario *asB = createScenario(SC_B_IO, SC_B_ID);
    ASSERT_NE(asB, nullptr);

    AutoScenario *as = createScenario(SC_IO, SC_ID);
    ASSERT_NE(as, nullptr);
    as->addStep(0.0);
    as->addStepAction(0, io(TARGET_1), "true");
    as->addStep(0.0);
    as->addStepAction(1, io(SC_B_IO), "true");
    as->checkScenarioRules();

    //start it for real, and leave it running
    scenarioIo()->set_value(true);
    ASSERT_TRUE(as->getIOIsActive()->get_value_bool()) << "the scenario did not start";

    //B disappears WHILE A is running
    asB->deleteAll();
    ASSERT_TRUE(ListeRoom::Instance().deleteIO(io(SC_B_IO)));

    as = autoScenario();
    ASSERT_NE(as, nullptr);
    ASSERT_TRUE(as->isDisabledMissingIo());

    EXPECT_FALSE(as->getIOIsActive()->get_value_bool()) << "still stuck in progress";
    EXPECT_EQ(-1, (int)as->getIOStep()->get_value_double());
}

/*******************************************************************************
 * Resistance to the client round trip
 ******************************************************************************/

TEST_F(ScenarioDisabledMissingIoTest, ModifyDoesNotClearTheDisabledFlag)
{
    /* THE guard rail of this ticket. buildAutoscenarioModify() does not replace
     * the params of the Scenario IO in bulk: it compares and applies four named
     * keys only (name, visible, cycle, disabled) plus the room. An unknown param
     * therefore survives - which is a FACT OF IMPLEMENTATION, not a designed
     * guarantee. Pinned here so that the first rewrite of that function cannot
     * reopen the hole in silence: without this test, a `modify` restarting a
     * broken scenario would go unnoticed until it damaged something.
     */
    loadHouse();
    ASSERT_NE(buildReferenceScenario(), nullptr);
    saveAmputateAndReload(TARGET_2);
    ASSERT_TRUE(autoScenario()->isDisabledMissingIo());

    //the modify a client sends back: it also flips the schedule choice, so the
    //case shows the two params being treated DIFFERENTLY and not just both
    //ignored. `enabled` is the only name that choice has on the wire now.
    ASSERT_EQ("true", scenarioIo()->get_param("disabled"));

    //E4.1r: same request, as a document. No assertion changed.
    const Json jreq = Json{{ "id", SC_IO },
                           { "name", "Renamed by the client" },
                           { "cycle", "true" },
                           { "enabled", "true" },
                           { "room_name", T318_ROOM },
                           { "room_type", T318_ROOM_TYPE },
                           { "steps", Json::array() }};

    ApiProbe api;
    const Json jret = api.buildAutoscenarioModify(jreq);
    EXPECT_EQ("true", jret.value("success", std::string()));

    Scenario *sc = scenarioIo();
    ASSERT_NE(sc, nullptr);

    //what modify DOES own is rewritten...
    EXPECT_EQ("Renamed by the client", sc->get_param("name"));
    EXPECT_EQ("false", sc->get_param("disabled"));
    EXPECT_TRUE(sc->getAutoScenario()->isCycling());

    //...and what it does not own is left alone. `disabled` and
    //`disabled_missing_io` live in the same Params and mean opposite things:
    //this is the one place where they are observed DISAGREEING.
    EXPECT_EQ("true", sc->get_param("disabled_missing_io"))
            << "autoscenario modify cleared the sticky flag";
    EXPECT_TRUE(sc->getAutoScenario()->isDisabledMissingIo());

    //and it survives the save modify does right after
    EXPECT_TRUE(flagIsOnDisk());
}

TEST_F(ScenarioDisabledMissingIoTest, TheDisabledParamIsTheSchedulingChoiceAndIsRewrittenByModify)
{
    /* Why the fix does NOT recycle the existing `disabled` param, spelled out
     * rather than argued. It is a USER choice: its only effect is to switch the
     * scheduling off, the scenario stays startable from the button. It is
     * exposed as "enabled" - NEGATED - in the payload, and it is REWRITTEN by
     * every modify. Recycling it would let the first `autoscenario modify` any
     * client sends put a broken scenario back to work.
     * Green before the fix as well (first commit): this is a pre-existing fact
     * the whole design of the sticky flag depends on.
     */
    loadHouse();
    AutoScenario *as = buildReferenceScenario();
    ASSERT_NE(as, nullptr);
    ASSERT_TRUE(as->isDisabled());

    //the payload emits the NEGATION, under another name
    Json j = toJsonOf();
    EXPECT_EQ("false", j.value("enabled", std::string()));
    //and the two params are NOT the same thing
    EXPECT_EQ("false", j.value("disabled_missing_io", std::string()));

    //E4.1r: same request, as a document. No assertion changed.
    const Json jreq = Json{{ "id", SC_IO },
                           { "name", "Scenario t318_sc" },
                           { "cycle", "false" },
                           { "enabled", "true" },
                           { "room_name", T318_ROOM },
                           { "room_type", T318_ROOM_TYPE },
                           { "steps", Json::array() }};

    ApiProbe api;
    api.buildAutoscenarioModify(jreq);

    as = autoScenario();
    ASSERT_NE(as, nullptr);
    EXPECT_FALSE(as->isDisabled()) << "modify does not own `disabled` any more";
    EXPECT_EQ("true", toJsonOf().value("enabled", std::string()));
}

/*******************************************************************************
 * Non regression of the teardown paths (the eight explicit Destroy sites)
 ******************************************************************************/

TEST_F(ScenarioDisabledMissingIoTest, RemovingAndReAddingAScheduleLeavesNoZombieRule)
{
    /* This is what protects the "Destroy is explicit at the teardown sites"
     * half of the change. Those sites destroy IOs whose ids are DETERMINISTIC
     * and recreated identically right after: kept disabled, their rules would be
     * duplicated by the next build and nothing would ever collect the
     * duplicates, and nothing anywhere collects them.
     * Without this case the Disable default is a time bomb.
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
    EXPECT_EQ(0, countRules("_time_start")) << "a disabled zombie was left behind";
    EXPECT_EQ(0, countRules("_time_stop")) << "a disabled zombie was left behind";

    as->addSchedule();
    EXPECT_EQ(1, countRules("_time_start")) << "the schedule rules were duplicated";
    EXPECT_EQ(1, countRules("_time_stop")) << "the schedule rules were duplicated";

    //and none of that flagged the scenario: nothing broke, it was a teardown
    EXPECT_FALSE(as->isBroken());
    EXPECT_FALSE(as->isDisabledMissingIo());

    //the same for a full delete: it must not leave the OTHER scenario's rules
    //behind either
    const int before = ListeRule::Instance().size();
    as->deleteAll();
    EXPECT_LT(ListeRule::Instance().size(), before);
}

/*******************************************************************************
 * The payload
 ******************************************************************************/

TEST_F(ScenarioDisabledMissingIoTest, ThePayloadTellsTheFourStatesApart)
{
    /* The four states of the decision, and why THREE keys are needed and not
     * one. broken and disabled_missing_io diverge on purpose: "repaired,
     * waiting for a manual re-enable" (false/true) is the state the user asked
     * for, and with a single key it is indistinguishable from "healthy".
     * The values are asserted key by key so that SWAPPING the two names in
     * IO/Scenario.cpp fails here, with the names in the message.
     */
    loadHouse();
    ASSERT_NE(buildReferenceScenario(), nullptr);

    //--- healthy: false / false / "" ---------------------------------------
    Json j = toJsonOf();
    ASSERT_TRUE(j.is_object());
    EXPECT_EQ("false", j.value("broken", std::string()));
    EXPECT_EQ("false", j.value("disabled_missing_io", std::string()));
    EXPECT_EQ("", j.value("missing_ios", std::string()));

    //--- broken and freshly disabled: true / true / the id ------------------
    saveAmputateAndReload(TARGET_2);
    j = toJsonOf();
    EXPECT_EQ("true", j.value("broken", std::string()));
    EXPECT_EQ("true", j.value("disabled_missing_io", std::string()));
    EXPECT_EQ(TARGET_2, j.value("missing_ios", std::string()));

    //--- repaired, waiting for a re-enable: FALSE / TRUE / "" ---------------
    //THE state the decision exists for, and the only one where the two boolean
    //keys DISAGREE
    Params p = {{ "type", "InternalBool" }, { "id", TARGET_2 }, { "name", "Missing" }};
    ASSERT_NE(createIO(p, firstRoom()), nullptr);
    saveAndReload();

    j = toJsonOf();
    EXPECT_EQ("false", j.value("broken", std::string()));
    EXPECT_EQ("true", j.value("disabled_missing_io", std::string()));
    EXPECT_EQ("", j.value("missing_ios", std::string()));

    //--- broken with the flag cleared by hand: TRUE / FALSE / the id --------
    //the other disagreement, so neither key can hide behind the other
    clearCoreState();
    loadHouse();
    ASSERT_NE(buildReferenceScenario(), nullptr);
    saveAmputateAndReload(TARGET_2);
    autoScenario()->setDisabledMissingIo(false);

    j = toJsonOf();
    EXPECT_EQ("true", j.value("broken", std::string()));
    EXPECT_EQ("false", j.value("disabled_missing_io", std::string()));
    EXPECT_EQ(TARGET_2, j.value("missing_ios", std::string()));

    //the key that must NOT move: "enabled" still means the user's schedule
    //choice, and nothing else
    EXPECT_EQ("false", j.value("enabled", std::string()));
}

/*******************************************************************************
 * The startup alert: which disabled rules it announces as steps of a scenario
 ******************************************************************************/

/* Config::LoadConfigRule() reports the rules it had to disable, and names the
 * SCENARIO instead of the rule for those that belong to one. Two disabled rules
 * are reloaded here, and they are not the same thing at all:
 *
 *  - the step rule of a live scenario, generated by the projection, carrying
 *    both `auto_scenario` and `autoscenario_uid`;
 *  - a rule of the world before the definition existed: `auto_scenario` alone,
 *    no uid, and no scenario claims it any more since the generator only owns
 *    the rules it wrote.
 *
 * The predicate used to be `param_exists("auto_scenario")`, which called BOTH
 * of them a step of a scenario. E4.6f keys it on the uid: the leftover is
 * reported, as a plain rule, and no longer sends the user looking for a
 * scenario that does not exist.
 */
TEST_F(ScenarioDisabledMissingIoTest, TheStartupAlertCallsAStepOfAScenarioOnlyARuleTheProjectionWrote)
{
    loadHouse();
    ASSERT_NE(buildReferenceScenario(), nullptr);
    saveConfig();

    std::string ioXml = ioXmlOnDisk();
    std::string rulesXml = rulesXmlOnDisk();
    ASSERT_TRUE(removeIoFromXml(ioXml, TARGET_2));

    //the generated rules really carry the uid: without it both rules would be
    //legacy shaped and the two branches would be indistinguishable
    const std::string scUid = scenarioIo()->get_param("autoscenario_uid");
    ASSERT_FALSE(scUid.empty());
    ASSERT_NE(std::string::npos, rulesXml.find("autoscenario_uid=\"" + scUid + "\""))
            << rulesXml;

    const std::string legacy =
            std::string("  <calaos:rule name=\"t318_legacy_rule\" type=\"AutoScenario\"") +
            " auto_scenario=\"t318_old_scenario\" auto_scenario_type=\"step\">\n"
            "    <calaos:condition type=\"standard\" trigger=\"true\">\n"
            "      <calaos:input id=\"" + TARGET_1 + "\" oper=\"==\" val=\"true\" />\n"
            "    </calaos:condition>\n"
            "    <calaos:action type=\"standard\">\n"
            "      <calaos:output id=\"" + TARGET_2 + "\" val=\"true\" />\n"
            "    </calaos:action>\n"
            "  </calaos:rule>\n";

    const size_t close = rulesXml.rfind("</calaos:rules>");
    ASSERT_NE(std::string::npos, close);
    rulesXml.insert(close, legacy);

    clearCoreState();
    const size_t alertsBefore = Config::Instance().getConfigAlerts().size();
    loadConfig(ioXml, rulesXml);

    //Config is a process wide singleton and the queue is only emptied when the
    //notification is really sent: read the entry this load appended, not the
    //whole queue.
    const std::vector<std::string> &alerts = Config::Instance().getConfigAlerts();
    ASSERT_EQ(alertsBefore + 1u, alerts.size());
    const std::string report = alerts.back();

    //the live scenario is named, by the uid its definition is filed under
    EXPECT_NE(std::string::npos,
              report.find("- step of scenario '" + scUid +
                          "' (rule 't318_sc_step')")) << report;

    //the leftover is still reported - nothing is hidden - but as what it is
    EXPECT_NE(std::string::npos,
              report.find("- rule 't318_legacy_rule': missing IO(s) t318_missing"))
            << report;
    EXPECT_EQ(std::string::npos,
              report.find("step of scenario 't318_old_scenario'")) << report;

    //the paragraph that tells the user a scenario stays disabled until it is
    //re-enabled by hand is emitted as soon as one line claimed a scenario
    EXPECT_NE(std::string::npos, report.find("A SCENARIO is among them")) << report;
}

/* The other half of the same alert: when nothing that got disabled belongs to a
 * scenario, the report must not say one is among them. The paragraph it guards
 * tells the user a scenario stays dead until it is re-enabled by hand, which is
 * false of a plain rule - that one comes back on its own.
 */
TEST_F(ScenarioDisabledMissingIoTest, TheScenarioParagraphIsAbsentWhenNoDisabledRuleBelongsToAScenario)
{
    const size_t alertsBefore = Config::Instance().getConfigAlerts().size();

    loadConfig(ioXmlDocument(roomXml(T318_ROOM, T318_ROOM_TYPE,
                                     internalIoXml("InternalBool", TARGET_1, "First"),
                                     0)),
               rulesXmlDocument(simpleRuleXml("t318_plain_rule", TARGET_1, "==", "true",
                                              "t318_never_existed", "true")));

    const std::vector<std::string> &alerts = Config::Instance().getConfigAlerts();
    ASSERT_EQ(alertsBefore + 1u, alerts.size());
    const std::string report = alerts.back();

    EXPECT_NE(std::string::npos,
              report.find("- rule 't318_plain_rule': missing IO(s) t318_never_existed"))
            << report;
    EXPECT_EQ(std::string::npos, report.find("step of scenario")) << report;
    EXPECT_EQ(std::string::npos, report.find("A SCENARIO is among them")) << report;
}
