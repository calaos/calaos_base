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
 * An AutoScenario remembers the five IOs that drive it. Destroying one of them
 * while the scenario lives used to leave the member pointing at freed memory,
 * and the memory was READ BACK inside the very same call: deleteIO() ends with
 * ListeRoom::refreshBrokenScenarios(), which calls stopBrokenRun() on every
 * scenario it finds broken, and stopBrokenRun() reads is_active and writes
 * step and timer. The whole invariant this file pins is therefore:
 *
 *      no AutoScenario names an IO that has been destroyed.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS A MODEL CASE HERE AND WHAT IS NOT
 * ---------------------------------------------------------------------------
 * No JSON command deletes a machinery IO by itself, so most cases below reach
 * the destruction through ListeRoom::deleteIO() directly and are MODEL cases,
 * named ...ThroughTheModel. They are still the cases that matter: the same
 * five members are read by a pass that any IO deletion runs.
 *
 * SharedUidTest is NOT a model case, and it is the door this invariant used to
 * be reachable through: `autoscenario_uid` is a plain io.xml attribute, two
 * scenarios could carry one, and createInput() then handed the second one the
 * IOs of the first instead of building its own - from where `autoscenario
 * delete` on either of them, an ordinary API command, destroyed the machinery
 * of the other. The startup pass now gives the second one an identifier of its
 * own (ListeRoom::rekeyDuplicateAutoScenarioUids()), so the two cases below
 * pin that the door is SHUT: they still edit the configuration on disk and
 * reload it, the same story the amputation helpers of the neighbouring suites
 * tell, and they assert the machinery is no longer shared and that deleting
 * one leaves the other whole. What the pair becomes is the subject of
 * core/AutoScenarioUidUniqueness_test.
 *
 * ---------------------------------------------------------------------------
 * WHY THE ASSERTIONS READ POINTERS AND NOT A CRASH
 * ---------------------------------------------------------------------------
 * Dereferencing freed memory is not required to crash, and a case that only
 * asserts "we got here" would be green on both sides. Each case asserts the
 * member is null AFTERWARDS, and asserts BEFOREHAND that it was non-null and
 * really was the IO about to die - otherwise a fixture that never built the
 * machinery would pass for the wrong reason. Under --enable-asan the same
 * cases turn the read itself into a heap-use-after-free report.
 *
 * Ids are t357_ prefixed and used nowhere else in the tree: Config's IO state
 * cache is process wide and never cleared (CalaosCoreFixture.h).
 ******************************************************************************/
#include <gtest/gtest.h>

#include "CalaosCoreFixture.h"

#include "ListeRoom.h"
#include "ListeRule.h"
#include "Room.h"
#include "IO/Scenario.h"
#include "JsonApi.h"

#include "AutoScenario.h"
#include "AutoScenarioDef.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char T357_ROOM[] = "T3.57 room";
const char T357_ROOM_TYPE[] = "salon";

const char TARGET_1[] = "t357_first";
const char TARGET_2[] = "t357_second";

const char SC_A_IO[] = "io_t357_sca";
const char SC_A_UID[] = "t357_sca";

const char SC_B_IO[] = "io_t357_scb";
const char SC_B_UID[] = "t357_scb";

std::string machineryId(const std::string &uid, const std::string &suffix)
{
    return uid + suffix;
}

} //namespace

class ScenarioInternalIoLifetimeTest: public CoreFixture
{
protected:
    void loadHouse()
    {
        loadConfig(ioXmlDocument(roomXml(T357_ROOM, T357_ROOM_TYPE,
                                         internalIoXml("InternalBool", TARGET_1, "First") +
                                         internalIoXml("InternalBool", TARGET_2, "Second"),
                                         0)),
                   rulesXmlDocument(std::string()));

        forgetIOState(TARGET_1);
        forgetIOState(TARGET_2);
    }

    static Scenario *scenarioIo(const std::string &ioId)
    {
        return dynamic_cast<Scenario *>(io(ioId));
    }

    static AutoScenario *autoScenario(const std::string &ioId)
    {
        Scenario *sc = scenarioIo(ioId);
        return sc? sc->getAutoScenario(): nullptr;
    }

    //Same Params buildAutoscenarioCreate() writes, uid included.
    static AutoScenario *createScenario(const std::string &ioId, const std::string &uid)
    {
        Params p = {{ "type", "Scenario" },
                    { "id", ioId },
                    { "name", "Scenario " + uid },
                    { "disabled", "false" },
                    { AutoScenarioDef::KEY_UID, uid }};

        Scenario *sc = dynamic_cast<Scenario *>(createIO(p));
        if (!sc || !sc->getAutoScenario()) return nullptr;
        if (!sc->getAutoScenario()->checkScenarioRules()) return nullptr;

        return sc->getAutoScenario();
    }

    //One step, one action, and a schedule: the schedule is what brings the
    //fourth and fifth machinery IOs into existence.
    AutoScenario *buildScenario(const std::string &ioId, const std::string &uid,
                                const char *target)
    {
        AutoScenario *as = createScenario(ioId, uid);
        if (!as) return nullptr;

        as->addStep(0.0);
        as->addStepAction(0, io(target), "true");
        as->addSchedule();
        as->checkScenarioRules();

        return as;
    }

    /* The five machinery members, read through the accessors. Order matters
     * only for the messages.
     */
    struct Machinery
    {
        IOBase *isActive = nullptr;
        IOBase *step = nullptr;
        IOBase *timer = nullptr;
        IOBase *timeRange = nullptr;
        IOBase *scheduleEnabled = nullptr;
    };

    static Machinery machineryOf(AutoScenario *as)
    {
        Machinery m;
        if (!as) return m;

        m.isActive = as->getIOIsActive();
        m.step = as->getIOStep();
        m.timer = as->getIOTimer();
        m.timeRange = as->getIOTimeRange();
        m.scheduleEnabled = as->getIOScheduleEnabled();

        return m;
    }

    /* The fixture check every case runs first: the five members exist AND each
     * one is the IO its derived id resolves to. Without it a scenario that
     * never built its machinery would satisfy every "is null afterwards"
     * assertion below for the wrong reason.
     */
    static ::testing::AssertionResult machineryIsBuilt(AutoScenario *as,
                                                       const std::string &uid)
    {
        if (!as) return ::testing::AssertionFailure() << "no AutoScenario";

        const Machinery m = machineryOf(as);
        const struct { IOBase *held; const char *suffix; } wanted[] = {
            { m.isActive, "_is_active" },
            { m.step, "_step" },
            { m.timer, "_timer" },
            { m.timeRange, "_schedule" },
            { m.scheduleEnabled, "_is_schedule_enabled" },
        };

        for (const auto &w: wanted)
        {
            IOBase *resolved = ListeRoom::Instance().findIO(machineryId(uid, w.suffix));
            if (!resolved)
                return ::testing::AssertionFailure()
                        << uid << w.suffix << " does not exist";
            if (w.held != resolved)
                return ::testing::AssertionFailure()
                        << uid << w.suffix << " is not the IO the scenario holds";
        }

        return ::testing::AssertionSuccess();
    }

    //Destroy one machinery IO exactly the way the JSON API destroys any IO:
    //default policy, so refreshBrokenScenarios() runs on the way out.
    static bool destroyMachineryIo(const std::string &uid, const char *suffix)
    {
        IOBase *target = ListeRoom::Instance().findIO(machineryId(uid, suffix));
        if (!target) return false;

        return ListeRoom::Instance().deleteIO(target);
    }
};

/* ---------------------------------------------------------------------------
 * The fixture itself, before anything is destroyed
 * ------------------------------------------------------------------------- */

TEST_F(ScenarioInternalIoLifetimeTest, AScenarioWithAScheduleHoldsItsFiveMachineryIos)
{
    loadHouse();

    AutoScenario *as = buildScenario(SC_A_IO, SC_A_UID, TARGET_1);
    ASSERT_NE(nullptr, as);
    EXPECT_TRUE(machineryIsBuilt(as, SC_A_UID));
}

/* ---------------------------------------------------------------------------
 * MODEL CASES - destruction through ListeRoom::deleteIO()
 * ------------------------------------------------------------------------- */

//The case FINDINGS.md reported: is_active destroyed under a live scenario.
TEST_F(ScenarioInternalIoLifetimeTest,
       DestroyingIsActiveLeavesNoPointerToItThroughTheModel)
{
    loadHouse();

    AutoScenario *as = buildScenario(SC_A_IO, SC_A_UID, TARGET_1);
    ASSERT_NE(nullptr, as);
    ASSERT_TRUE(machineryIsBuilt(as, SC_A_UID));

    ASSERT_TRUE(destroyMachineryIo(SC_A_UID, "_is_active"));

    //It really went away, so the member cannot be "still valid"
    EXPECT_EQ(nullptr, ListeRoom::Instance().findIO(machineryId(SC_A_UID, "_is_active")));
    EXPECT_EQ(nullptr, as->getIOIsActive());

    //and only that one: a sweep that clears everything would pass the line
    //above while breaking the scenario
    EXPECT_NE(nullptr, as->getIOStep());
    EXPECT_NE(nullptr, as->getIOTimer());
    EXPECT_NE(nullptr, as->getIOTimeRange());
    EXPECT_NE(nullptr, as->getIOScheduleEnabled());

    //The owner is not a back-pointer: the AutoScenario cannot outlive it, and
    //~AutoScenario dereferences it.
    EXPECT_EQ(scenarioIo(SC_A_IO), as->getIOScenario());
}

//Each of the five, one case body, one at a time: position and not presence.
TEST_F(ScenarioInternalIoLifetimeTest,
       DestroyingAnyOfTheFiveClearsThatOneAndNoOtherThroughTheModel)
{
    const char *const suffixes[] = { "_is_active", "_step", "_timer",
                                     "_schedule", "_is_schedule_enabled" };

    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++)
    {
        clearCoreState();
        loadHouse();

        AutoScenario *as = buildScenario(SC_A_IO, SC_A_UID, TARGET_1);
        ASSERT_NE(nullptr, as) << suffixes[i];
        ASSERT_TRUE(machineryIsBuilt(as, SC_A_UID)) << suffixes[i];

        ASSERT_TRUE(destroyMachineryIo(SC_A_UID, suffixes[i])) << suffixes[i];

        const Machinery m = machineryOf(as);
        IOBase *const held[] = { m.isActive, m.step, m.timer,
                                 m.timeRange, m.scheduleEnabled };

        for (size_t j = 0; j < sizeof(held) / sizeof(held[0]); j++)
        {
            if (i == j)
                EXPECT_EQ(nullptr, held[j]) << "destroyed " << suffixes[i];
            else
                EXPECT_NE(nullptr, held[j]) << "destroyed " << suffixes[i]
                                            << ", collateral on " << suffixes[j];
        }
    }
}

/* The read is a WRITE here. stopBrokenRun() returns early on a scenario that is
 * not running, so a scenario left stopped never gets past the first member.
 * Started first, the pass reads is_active and then writes step and timer.
 */
TEST_F(ScenarioInternalIoLifetimeTest,
       DestroyingIsActiveWhileTheScenarioRunsStopsItWithoutTouchingTheDeadIoThroughTheModel)
{
    loadHouse();

    AutoScenario *as = buildScenario(SC_A_IO, SC_A_UID, TARGET_1);
    ASSERT_NE(nullptr, as);
    ASSERT_TRUE(machineryIsBuilt(as, SC_A_UID));

    Scenario *sc = scenarioIo(SC_A_IO);
    ASSERT_NE(nullptr, sc);
    sc->set_value(true);
    ASSERT_TRUE(as->getIOIsActive()->get_value_bool()) << "the scenario did not start";

    ASSERT_TRUE(destroyMachineryIo(SC_A_UID, "_is_active"));

    EXPECT_EQ(nullptr, as->getIOIsActive());
    //the pass ran and did what it could with what is left
    EXPECT_TRUE(as->isDisabledMissingIo());
}

//~Room destroys its IOs without going through deleteIO(); the same invariant
//has to hold there, which is why the clearing cannot live in deleteIO().
TEST_F(ScenarioInternalIoLifetimeTest,
       DestroyingTheRoomLeavesNoPointerToItsIosThroughTheModel)
{
    loadHouse();

    AutoScenario *as = buildScenario(SC_A_IO, SC_A_UID, TARGET_1);
    ASSERT_NE(nullptr, as);
    ASSERT_TRUE(machineryIsBuilt(as, SC_A_UID));

    /* A second room, and the scenario IO moved into it: `autoscenario modify`
     * moves the Scenario IO alone and leaves the machinery where it was, so
     * the two really can end up in different rooms.
     */
    Room *other = addRoom("T3.57 other room", "chambre");
    ASSERT_NE(nullptr, other);

    Room *home = ListeRoom::Instance().getRoomByIO(scenarioIo(SC_A_IO));
    ASSERT_NE(nullptr, home);
    ASSERT_NE(home, other);
    ASSERT_EQ(home, as->getRoomContainer());

    home->RemoveIOFromRoom(scenarioIo(SC_A_IO));
    other->AddIO(scenarioIo(SC_A_IO));

    int homeIndex = -1;
    for (int i = 0; i < ListeRoom::Instance().size(); i++)
        if (ListeRoom::Instance().get_room(i) == home) homeIndex = i;
    ASSERT_NE(-1, homeIndex);

    ListeRoom::Instance().Remove(homeIndex);

    //the scenario survived its machinery, which is the whole point
    ASSERT_NE(nullptr, autoScenario(SC_A_IO));
    EXPECT_EQ(nullptr, as->getIOIsActive());
    EXPECT_EQ(nullptr, as->getIOStep());
    EXPECT_EQ(nullptr, as->getIOTimer());
    EXPECT_EQ(nullptr, as->getIOTimeRange());
    EXPECT_EQ(nullptr, as->getIOScheduleEnabled());
    //the sixth back-pointer, the one addSchedule() would build into
    EXPECT_EQ(nullptr, as->getRoomContainer());
}

/* ---------------------------------------------------------------------------
 * THE DOOR - reached by an ordinary API command
 * ------------------------------------------------------------------------- */

/* Rewrite scenario B's uid to A's in the saved io.xml and load everything back.
 * The IO ids are derived from the uid, so B's createInput() would find A's IOs
 * already there and adopt them - which is what the startup pass prevents by
 * moving B onto an identifier of its own before anything is derived.
 */
class SharedUidTest: public ScenarioInternalIoLifetimeTest
{
protected:
    void loadTwoScenariosSharingOneUid()
    {
        loadHouse();

        ASSERT_NE(nullptr, buildScenario(SC_A_IO, SC_A_UID, TARGET_1));
        ASSERT_NE(nullptr, buildScenario(SC_B_IO, SC_B_UID, TARGET_2));

        saveConfig();

        std::string ioXml = ioXmlOnDisk();
        const std::string rulesXml = rulesXmlOnDisk();

        const std::string from = std::string(AutoScenarioDef::KEY_UID) + "=\"" + SC_B_UID + "\"";
        const std::string to = std::string(AutoScenarioDef::KEY_UID) + "=\"" + SC_A_UID + "\"";
        const size_t at = ioXml.find(from);
        ASSERT_NE(std::string::npos, at) << "B's uid is not in the saved io.xml";
        ioXml.replace(at, from.size(), to);

        clearCoreState();
        loadConfig(ioXml, rulesXml);
        ListeRoom::Instance().checkAutoScenario();
    }
};

//The precondition of the door, measured rather than assumed - and it no longer
//holds: the two build machineries of their own.
TEST_F(SharedUidTest, TwoScenariosSharingAUidDoNotShareTheirMachineryIos)
{
    loadTwoScenariosSharingOneUid();

    AutoScenario *a = autoScenario(SC_A_IO);
    AutoScenario *b = autoScenario(SC_B_IO);
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, b);
    ASSERT_NE(a, b);

    //A kept the identifier, so its five are exactly the ones it always had
    ASSERT_TRUE(machineryIsBuilt(a, SC_A_UID));

    //and B, moved onto one of its own, holds three IOs that are none of them
    ASSERT_NE(nullptr, b->getIOIsActive());
    ASSERT_NE(nullptr, b->getIOStep());
    ASSERT_NE(nullptr, b->getIOTimer());
    EXPECT_NE(a->getIOIsActive(), b->getIOIsActive());
    EXPECT_NE(a->getIOStep(), b->getIOStep());
    EXPECT_NE(a->getIOTimer(), b->getIOTimer());
}

/* And the door itself, shut: `autoscenario delete` on B tears down B's
 * machinery and ends on deleteIO(B), whose detection pass walks A - which now
 * holds nothing B could take with it.
 */
TEST_F(SharedUidTest, DeletingOneOfTwoScenariosSharingAUidLeavesTheOtherWhole)
{
    loadTwoScenariosSharingOneUid();

    AutoScenario *a = autoScenario(SC_A_IO);
    ASSERT_NE(nullptr, a);
    ASSERT_TRUE(machineryIsBuilt(a, SC_A_UID));

    JsonApi api;
    const Json reply = api.buildAutoscenarioDelete(Json{{ "id", SC_B_IO }});
    ASSERT_EQ(std::string(), reply.value("error", std::string())) << reply.dump();

    ASSERT_EQ(nullptr, scenarioIo(SC_B_IO));
    ASSERT_NE(nullptr, autoScenario(SC_A_IO)) << "A must survive the delete of B";

    /* Not "the member is null" any more but "the member is the live IO its own
     * id resolves to": a stale pointer would be non-null and would not compare
     * equal to what findIO() answers.
     */
    EXPECT_TRUE(machineryIsBuilt(a, SC_A_UID));

    //A pass over what is left must be answerable, and it is what the server
    //runs at every startup
    ListeRoom::Instance().checkAutoScenario();
    EXPECT_NE(nullptr, autoScenario(SC_A_IO));
    EXPECT_TRUE(machineryIsBuilt(a, SC_A_UID));
}

/* ---------------------------------------------------------------------------
 * NON REGRESSION - loading a configuration must be untouched
 * ------------------------------------------------------------------------- */

/* Nothing is destroyed on this path, so nothing may be forgotten either: the
 * five members have to come back non null and resolvable after a save/reload
 * plus the startup pass, twice in a row.
 */
TEST_F(ScenarioInternalIoLifetimeTest, ALoadedConfigurationKeepsEveryMachineryPointer)
{
    loadHouse();

    ASSERT_NE(nullptr, buildScenario(SC_A_IO, SC_A_UID, TARGET_1));
    ASSERT_NE(nullptr, buildScenario(SC_B_IO, SC_B_UID, TARGET_2));

    for (int cycle = 0; cycle < 2; cycle++)
    {
        saveConfig();
        reloadFromDisk();
        ListeRoom::Instance().checkAutoScenario();

        EXPECT_TRUE(machineryIsBuilt(autoScenario(SC_A_IO), SC_A_UID)) << "cycle " << cycle;
        EXPECT_TRUE(machineryIsBuilt(autoScenario(SC_B_IO), SC_B_UID)) << "cycle " << cycle;
    }
}

/* The generator destroys its own machinery all the time - deleteAll(),
 * deleteSchedule(), the stale schedule flag of rebuildRules(). Those paths must
 * keep behaving exactly as they did, including rebuilding what they dropped.
 */
TEST_F(ScenarioInternalIoLifetimeTest, TheGeneratorStillDropsAndRebuildsItsOwnSchedule)
{
    loadHouse();

    AutoScenario *as = buildScenario(SC_A_IO, SC_A_UID, TARGET_1);
    ASSERT_NE(nullptr, as);
    ASSERT_TRUE(machineryIsBuilt(as, SC_A_UID));

    as->deleteSchedule();
    EXPECT_EQ(nullptr, as->getIOTimeRange());
    EXPECT_EQ(nullptr, as->getIOScheduleEnabled());
    EXPECT_EQ(nullptr, ListeRoom::Instance().findIO(machineryId(SC_A_UID, "_schedule")));
    EXPECT_EQ(nullptr, ListeRoom::Instance().findIO(machineryId(SC_A_UID,
                                                                "_is_schedule_enabled")));
    //the three that do not belong to the schedule are untouched
    EXPECT_NE(nullptr, as->getIOIsActive());
    EXPECT_NE(nullptr, as->getIOStep());
    EXPECT_NE(nullptr, as->getIOTimer());

    as->addSchedule();
    EXPECT_TRUE(machineryIsBuilt(as, SC_A_UID));
}

//deleteAll() drops the five and leaves the scenario IO alone, as
//buildAutoscenarioDelete() needs it to.
TEST_F(ScenarioInternalIoLifetimeTest, DeleteAllDropsTheFiveAndKeepsTheScenarioIo)
{
    loadHouse();

    AutoScenario *as = buildScenario(SC_A_IO, SC_A_UID, TARGET_1);
    ASSERT_NE(nullptr, as);
    ASSERT_TRUE(machineryIsBuilt(as, SC_A_UID));

    as->deleteAll();

    EXPECT_EQ(nullptr, as->getIOIsActive());
    EXPECT_EQ(nullptr, as->getIOStep());
    EXPECT_EQ(nullptr, as->getIOTimer());
    EXPECT_EQ(nullptr, as->getIOTimeRange());
    EXPECT_EQ(nullptr, as->getIOScheduleEnabled());
    EXPECT_EQ(scenarioIo(SC_A_IO), as->getIOScenario());
}
