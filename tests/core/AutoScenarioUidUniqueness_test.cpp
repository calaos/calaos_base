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
 * `autoscenario_uid` IS AN io.xml ATTRIBUTE LIKE ANY OTHER, AND TWO SCENARIOS
 * MAY CARRY THE SAME ONE
 * =============================================================================
 *
 * Everything a scenario owns is DERIVED FROM ITS UID: the five machinery IOs
 * are `<uid>_is_active` & co., the generated rules are named `<uid>_step` & co.
 * and are looked up by uid. Two scenarios sharing a uid therefore share one
 * machinery and one rule set, and the loser of that race is whoever the
 * startup pass rebuilds first: the second rebuild destroys the rules the first
 * one had just generated, by uid, and regenerates its own.
 *
 * So the observable loss has TWO halves, and only the second one crashes:
 *
 *   1. BEFORE anything is deleted, one of the two scenarios is already inert.
 *      It is listed, `autoscenario get` answers with its own steps, and its
 *      button does nothing at all - its rules belong to the other one now.
 *   2. `autoscenario delete` on either of them, an ordinary API command in the
 *      default policy, destroys the machinery the survivor still holds.
 *
 * ---------------------------------------------------------------------------
 * WHY THE CONFIGURATION IS EDITED ON DISK
 * ---------------------------------------------------------------------------
 * No API path produces a duplicate: `autoscenario create` allocates through
 * AutoScenarioDef::newUid(), whose counter is pushed past every uid ever read
 * by observeUid(). A duplicate comes from the FILE - a hand edit, a `config
 * put` of an io.xml built elsewhere, or a `set_param` writing the attribute on
 * a live IO. The fixture below therefore does a real disk round trip: build
 * two healthy scenarios, save, rewrite one uid in the saved io.xml, load the
 * files back and run the startup pass. Nothing is assembled in memory.
 *
 * ---------------------------------------------------------------------------
 * WHAT EVERY CASE PAIRS
 * ---------------------------------------------------------------------------
 * Each scenario acts on a target of ITS OWN, and every assertion that one
 * target moved is paired with the other one. A guard that made both scenarios
 * inert would satisfy "they no longer share" and is refused here; a guard that
 * dropped the second scenario at load would satisfy "no duplicate remains" and
 * is refused by BothScenariosOfADuplicatedUidStillLoad.
 *
 * Ids are t3117_ prefixed and used nowhere else in the tree: Config's IO state
 * cache is process wide and never cleared (CalaosCoreFixture.h).
 ******************************************************************************/
#include <gtest/gtest.h>

#include <set>
#include <string>

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

const char T3117_ROOM[] = "T3.117 room";
const char T3117_ROOM_TYPE[] = "salon";

const char TARGET_A[] = "t3117_target_a";
const char TARGET_B[] = "t3117_target_b";

const char SC_A_IO[] = "io_t3117_sca";
const char SC_A_UID[] = "t3117_uid_a";

const char SC_B_IO[] = "io_t3117_scb";
const char SC_B_UID[] = "t3117_uid_b";

} //namespace

class AutoScenarioUidUniquenessTest: public CoreFixture
{
protected:
    void loadHouse()
    {
        loadConfig(ioXmlDocument(roomXml(T3117_ROOM, T3117_ROOM_TYPE,
                                         internalIoXml("InternalBool", TARGET_A, "Target A") +
                                         internalIoXml("InternalBool", TARGET_B, "Target B"),
                                         0)),
                   rulesXmlDocument(std::string()));

        forgetIOState(TARGET_A);
        forgetIOState(TARGET_B);
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

    //The uid as it currently is on the IO, i.e. what io.xml would be saved with
    static std::string uidOf(const std::string &ioId)
    {
        Scenario *sc = scenarioIo(ioId);
        return sc? sc->get_param(AutoScenarioDef::KEY_UID): std::string();
    }

    //Same Params buildAutoscenarioCreate() writes, uid included.
    static AutoScenario *buildScenario(const std::string &ioId, const std::string &uid,
                                       const char *target)
    {
        Params p = {{ "type", "Scenario" },
                    { "id", ioId },
                    { "name", "Scenario " + uid },
                    { "disabled", "false" },
                    { AutoScenarioDef::KEY_UID, uid }};

        Scenario *sc = dynamic_cast<Scenario *>(createIO(p));
        if (!sc || !sc->getAutoScenario()) return nullptr;

        AutoScenario *as = sc->getAutoScenario();
        if (!as->checkScenarioRules()) return nullptr;

        as->addStep(0.0);
        as->addStepAction(0, io(target), "true");
        as->addSchedule();
        as->checkScenarioRules();

        return as;
    }

    /* Two healthy scenarios, saved, then B's uid rewritten to A's IN THE FILE
     * and everything loaded back through the real config path.
     */
    void loadTwoScenariosSharingOneUid()
    {
        loadHouse();

        ASSERT_NE(nullptr, buildScenario(SC_A_IO, SC_A_UID, TARGET_A));
        ASSERT_NE(nullptr, buildScenario(SC_B_IO, SC_B_UID, TARGET_B));

        saveConfig();

        std::string ioXml = ioXmlOnDisk();
        const std::string rulesXml = rulesXmlOnDisk();

        const std::string from = std::string(AutoScenarioDef::KEY_UID) + "=\"" + SC_B_UID + "\"";
        const std::string to = std::string(AutoScenarioDef::KEY_UID) + "=\"" + SC_A_UID + "\"";
        const size_t at = ioXml.find(from);
        ASSERT_NE(std::string::npos, at) << "B's uid is not in the saved io.xml";
        ioXml.replace(at, from.size(), to);

        clearCoreState();
        forgetIOState(TARGET_A);
        forgetIOState(TARGET_B);
        loadConfig(ioXml, rulesXml);
        markAlerts();
        ListeRoom::Instance().checkAutoScenario();
    }

    //The same round trip WITHOUT the edit: the control this file needs so that
    //"the pair survives" is not read off a fixture that never duplicated.
    void loadTwoScenariosWithDistinctUids()
    {
        loadHouse();

        ASSERT_NE(nullptr, buildScenario(SC_A_IO, SC_A_UID, TARGET_A));
        ASSERT_NE(nullptr, buildScenario(SC_B_IO, SC_B_UID, TARGET_B));

        saveConfig();

        const std::string ioXml = ioXmlOnDisk();
        const std::string rulesXml = rulesXmlOnDisk();

        clearCoreState();
        forgetIOState(TARGET_A);
        forgetIOState(TARGET_B);
        loadConfig(ioXml, rulesXml);
        markAlerts();
        ListeRoom::Instance().checkAutoScenario();
    }

    /* Config's alert queue is process wide and is only emptied when it is
     * SENT, which needs an event loop no core test runs: what a case may read
     * is what the startup pass it just ran added, never the whole queue.
     */
    void markAlerts() { alertMark = Config::Instance().getConfigAlerts().size(); }

    std::vector<std::string> alertsSinceMark() const
    {
        const std::vector<std::string> &all = Config::Instance().getConfigAlerts();
        if (all.size() <= alertMark) return std::vector<std::string>();

        return std::vector<std::string>(all.begin() + alertMark, all.end());
    }

    /* Press the button and run whatever step rules the scenario owns. Nothing
     * is short circuited: ioIsActive is set by the generated _button_start rule
     * and by nothing else here, so a scenario whose rules were taken away by
     * its twin never starts and never runs a step.
     */
    static int runScenario(const std::string &ioId)
    {
        Scenario *sc = scenarioIo(ioId);
        AutoScenario *as = sc? sc->getAutoScenario(): nullptr;
        if (!sc || !as) return 0;

        sc->set_value(true);

        if (!as->getIOIsActive() || !as->getIOIsActive()->get_value_bool())
            return 0;

        int ran = 0;
        for (Rule *step: as->getRuleSteps())
            if (step && step->ExecuteActions())
                ran++;

        return ran;
    }

    static bool targetIsSet(const char *id)
    {
        IOBase *o = io(id);
        return o && o->get_value_bool();
    }

    //The IOs a scenario drives, by identity, nulls dropped.
    static std::set<IOBase *> machineryOf(AutoScenario *as)
    {
        std::set<IOBase *> ios;
        if (!as) return ios;

        for (IOBase *held: { (IOBase *)as->getIOIsActive(), (IOBase *)as->getIOStep(),
                             (IOBase *)as->getIOTimer(), (IOBase *)as->getIOTimeRange(),
                             (IOBase *)as->getIOScheduleEnabled() })
            if (held) ios.insert(held);

        return ios;
    }

    /* The machinery a scenario always builds - the three unscheduled ones, or
     * all five when it is scheduled - AND each one is the IO its derived id
     * resolves to. Without the second half a scenario that built nothing would
     * satisfy every "they no longer share" assertion for the wrong reason.
     */
    static ::testing::AssertionResult machineryIsBuilt(AutoScenario *as, bool scheduled)
    {
        if (!as) return ::testing::AssertionFailure() << "no AutoScenario";

        const std::string uid = as->getScenarioId();
        const struct { IOBase *held; const char *suffix; bool always; } wanted[] = {
            { as->getIOIsActive(), "_is_active", true },
            { as->getIOStep(), "_step", true },
            { as->getIOTimer(), "_timer", true },
            { as->getIOTimeRange(), "_schedule", false },
            { as->getIOScheduleEnabled(), "_is_schedule_enabled", false },
        };

        for (const auto &w: wanted)
        {
            if (!w.always && !scheduled)
            {
                if (w.held)
                    return ::testing::AssertionFailure()
                            << uid << w.suffix << " is held by a scenario that has no schedule";
                continue;
            }

            IOBase *resolved = ListeRoom::Instance().findIO(uid + w.suffix);
            if (!resolved)
                return ::testing::AssertionFailure() << uid << w.suffix << " does not exist";
            if (w.held != resolved)
                return ::testing::AssertionFailure()
                        << uid << w.suffix << " is not the IO the scenario holds";
        }

        return ::testing::AssertionSuccess();
    }

    //How many `autoscenario_uid="..."` attributes io.xml carries, and how many
    //DISTINCT values they take.
    struct UidsOnDisk
    {
        size_t count = 0;
        std::set<std::string> distinct;
    };

    size_t alertMark = 0;

    UidsOnDisk uidsOnDisk() const
    {
        UidsOnDisk out;
        const std::string xml = ioXmlOnDisk();
        const std::string needle = std::string(AutoScenarioDef::KEY_UID) + "=\"";

        for (size_t at = xml.find(needle); at != std::string::npos;
             at = xml.find(needle, at + needle.size()))
        {
            const size_t from = at + needle.size();
            const size_t end = xml.find('"', from);
            if (end == std::string::npos) break;

            out.count++;
            out.distinct.insert(xml.substr(from, end - from));
        }

        return out;
    }
};

/* ---------------------------------------------------------------------------
 * THE CONFIGURATION STILL LOADS - the ceiling on how high the guard may go
 * ------------------------------------------------------------------------- */

/* A configuration that ALREADY carries a duplicate must keep both scenarios,
 * with their own steps and their own action. A guard that refused the second
 * IO, or that stopped treating it as a scenario, would make part of the
 * configuration disappear at startup - worse than the defect.
 */
TEST_F(AutoScenarioUidUniquenessTest, BothScenariosOfADuplicatedUidStillLoad)
{
    loadTwoScenariosSharingOneUid();

    Scenario *a = scenarioIo(SC_A_IO);
    Scenario *b = scenarioIo(SC_B_IO);
    ASSERT_NE(nullptr, a) << "A disappeared from the model";
    ASSERT_NE(nullptr, b) << "B disappeared from the model";

    ASSERT_NE(nullptr, a->getAutoScenario()) << "A is no longer an auto scenario";
    ASSERT_NE(nullptr, b->getAutoScenario()) << "B is no longer an auto scenario";

    //Each definition is the one its own IO carries, and they differ
    const Json ja = a->toJson();
    const Json jb = b->toJson();
    ASSERT_EQ(1u, ja["steps"].size()) << ja.dump();
    ASSERT_EQ(1u, jb["steps"].size()) << jb.dump();
    EXPECT_EQ(std::string(TARGET_A), ja["steps"][0]["actions"][0]["io"].get<std::string>());
    EXPECT_EQ(std::string(TARGET_B), jb["steps"][0]["actions"][0]["io"].get<std::string>());

    //and `autoscenario list` names both of them
    JsonApi api;
    const Json list = api.buildAutoscenarioList(Json::object());
    std::set<std::string> listed;
    for (const Json &s: list["scenarios"])
        listed.insert(s.value("id", std::string()));
    EXPECT_EQ(1u, listed.count(SC_A_IO)) << list.dump();
    EXPECT_EQ(1u, listed.count(SC_B_IO)) << list.dump();
}

/* ---------------------------------------------------------------------------
 * WHAT THE USER SEES BEFORE ANYTHING IS DELETED
 * ------------------------------------------------------------------------- */

/* The first half of the loss, and it needs no deletion at all: one of the two
 * is already inert, because the startup rebuild of the second destroyed the
 * rules of the first BY UID and regenerated its own under the same names.
 */
TEST_F(AutoScenarioUidUniquenessTest, EachScenarioOfADuplicatedUidRunsItsOwnAction)
{
    loadTwoScenariosSharingOneUid();

    EXPECT_EQ(1, runScenario(SC_A_IO)) << "A's button ran no step";
    EXPECT_TRUE(targetIsSet(TARGET_A)) << "A's own target did not move";
    EXPECT_FALSE(targetIsSet(TARGET_B)) << "A ran B's action";

    EXPECT_EQ(1, runScenario(SC_B_IO)) << "B's button ran no step";
    EXPECT_TRUE(targetIsSet(TARGET_B)) << "B's own target did not move";
}

/* ---------------------------------------------------------------------------
 * THE MACHINERY
 * ------------------------------------------------------------------------- */

/* The one whose identifier was taken back keeps its five; the other one builds
 * its own three, and the two sets do not intersect.
 * ⚠️ THREE AND NOT FIVE IS THE DECLARED COST: `<uid>_schedule` belongs to the
 * scenario that kept the uid, so the re-keyed one comes back unscheduled. It
 * never had a schedule IO of its own - it was reading its twin's - and the
 * configuration alert says so.
 */
TEST_F(AutoScenarioUidUniquenessTest, EachScenarioOfADuplicatedUidOwnsItsMachinery)
{
    loadTwoScenariosSharingOneUid();

    AutoScenario *a = autoScenario(SC_A_IO);
    AutoScenario *b = autoScenario(SC_B_IO);
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, b);
    ASSERT_NE(a, b);

    ASSERT_TRUE(machineryIsBuilt(a, true));
    ASSERT_TRUE(machineryIsBuilt(b, false));

    //Eight IOs, not five: the two sets must not intersect at all
    const std::set<IOBase *> ma = machineryOf(a);
    const std::set<IOBase *> mb = machineryOf(b);
    ASSERT_EQ(5u, ma.size());
    ASSERT_EQ(3u, mb.size());

    for (IOBase *held: ma)
        EXPECT_EQ(0u, mb.count(held)) << "shared machinery IO "
                                      << held->get_param("id");
}

/* ---------------------------------------------------------------------------
 * THE LOSS - reached by an ordinary API command
 * ------------------------------------------------------------------------- */

/* `autoscenario delete` on B, the real production function, in the default
 * policy. On a duplicated configuration it tears down the machinery A still
 * holds and the rules A still needs; A is left listed, without machinery and
 * without rules, for an operation nobody asked for on A.
 */
TEST_F(AutoScenarioUidUniquenessTest, DeletingOneOfADuplicatedUidLeavesTheOtherRunning)
{
    loadTwoScenariosSharingOneUid();

    AutoScenario *a = autoScenario(SC_A_IO);
    ASSERT_NE(nullptr, a);
    ASSERT_TRUE(machineryIsBuilt(a, true));

    JsonApi api;
    const Json reply = api.buildAutoscenarioDelete(Json{{ "id", SC_B_IO }});
    ASSERT_EQ(std::string(), reply.value("error", std::string())) << reply.dump();

    //B really went away, so what follows is not read on a delete that did nothing
    ASSERT_EQ(nullptr, scenarioIo(SC_B_IO));
    ASSERT_NE(nullptr, autoScenario(SC_A_IO)) << "A must survive the delete of B";

    //A keeps every one of its five IOs...
    EXPECT_TRUE(machineryIsBuilt(a, true));
    //...and it still runs
    EXPECT_EQ(1, runScenario(SC_A_IO)) << "A's button ran no step after B was deleted";
    EXPECT_TRUE(targetIsSet(TARGET_A));
}

/* ---------------------------------------------------------------------------
 * WHAT IS WRITTEN BACK, AND WHAT A SECOND STARTUP DOES WITH IT
 * ------------------------------------------------------------------------- */

TEST_F(AutoScenarioUidUniquenessTest, TheSavedIoXmlCarriesTwoDistinctUids)
{
    loadTwoScenariosSharingOneUid();

    saveConfig();

    const UidsOnDisk uids = uidsOnDisk();
    EXPECT_EQ(2u, uids.count) << "a scenario definition left io.xml";
    EXPECT_EQ(2u, uids.distinct.size()) << "io.xml still carries a duplicated uid";
}

/* Convergence, and it is the runaway this repair could have become: the second
 * startup on the file the first one wrote must change no uid at all.
 */
TEST_F(AutoScenarioUidUniquenessTest, ASecondStartupChangesNoUid)
{
    loadTwoScenariosSharingOneUid();

    const std::string afterFirst[] = { uidOf(SC_A_IO), uidOf(SC_B_IO) };
    ASSERT_FALSE(afterFirst[0].empty());
    ASSERT_FALSE(afterFirst[1].empty());
    ASSERT_NE(afterFirst[0], afterFirst[1]) << "the first startup left a duplicate";

    saveConfig();
    reloadFromDisk();
    ListeRoom::Instance().checkAutoScenario();

    EXPECT_EQ(afterFirst[0], uidOf(SC_A_IO));
    EXPECT_EQ(afterFirst[1], uidOf(SC_B_IO));

    //and both still run, on their own target
    EXPECT_EQ(1, runScenario(SC_A_IO));
    EXPECT_TRUE(targetIsSet(TARGET_A));
    EXPECT_EQ(1, runScenario(SC_B_IO));
    EXPECT_TRUE(targetIsSet(TARGET_B));
}

/* ---------------------------------------------------------------------------
 * THE ALERT
 * ------------------------------------------------------------------------- */

TEST_F(AutoScenarioUidUniquenessTest, TheRepairIsAnnouncedOnTheConfigurationAlertChannel)
{
    loadTwoScenariosSharingOneUid();

    bool named = false;
    for (const std::string &m: alertsSinceMark())
        if (m.find(SC_B_IO) != std::string::npos && m.find(SC_A_UID) != std::string::npos)
            named = true;

    EXPECT_TRUE(named) << "nothing on the alert channel names the duplicate";
}

/* ---------------------------------------------------------------------------
 * THE CONTROL - a healthy configuration is strictly unchanged
 * ------------------------------------------------------------------------- */

/* Without it every assertion above would also be satisfied by a guard that
 * re-keys every scenario at every startup.
 */
TEST_F(AutoScenarioUidUniquenessTest, AConfigurationWithoutADuplicateKeepsItsUids)
{
    loadTwoScenariosWithDistinctUids();

    EXPECT_EQ(std::string(SC_A_UID), uidOf(SC_A_IO));
    EXPECT_EQ(std::string(SC_B_UID), uidOf(SC_B_IO));

    saveConfig();
    const UidsOnDisk uids = uidsOnDisk();
    EXPECT_EQ(2u, uids.count);
    EXPECT_EQ(2u, uids.distinct.size());
    EXPECT_EQ(1u, uids.distinct.count(SC_A_UID));
    EXPECT_EQ(1u, uids.distinct.count(SC_B_UID));

    for (const std::string &m: alertsSinceMark())
        ADD_FAILURE() << "a healthy configuration raised a configuration alert: " << m;
}

/* ---------------------------------------------------------------------------
 * THE CENSUS - where a duplicate can come from, measured rather than read
 * ------------------------------------------------------------------------- */

/* The allocator cannot collide on its own. Every uid a configuration carries
 * is folded into the counter at load (observeUid()), so what `autoscenario
 * create` hands out is past all of them - even a uid nobody wrote in order.
 */
TEST_F(AutoScenarioUidUniquenessTest, TheApiAllocatorNeverHandsOutAUidTheFileAlreadyCarries)
{
    const char TAKEN[] = "as_9999";

    loadHouse();
    ASSERT_NE(nullptr, buildScenario(SC_A_IO, TAKEN, TARGET_A));

    saveConfig();
    reloadFromDisk();
    markAlerts();
    ListeRoom::Instance().checkAutoScenario();
    ASSERT_EQ(std::string(TAKEN), uidOf(SC_A_IO));

    JsonApi api;
    const Json reply = api.buildAutoscenarioCreate(Json{{ "name", "created" },
                                                        { "room_name", T3117_ROOM },
                                                        { "room_type", T3117_ROOM_TYPE }});
    const std::string created = reply.value("id", std::string());
    ASSERT_FALSE(created.empty()) << reply.dump();

    EXPECT_NE(std::string(TAKEN), uidOf(created));
    EXPECT_FALSE(uidOf(created).empty());

    //and the pass had nothing to repair
    for (const std::string &m: alertsSinceMark())
        ADD_FAILURE() << "creating a scenario raised a configuration alert: " << m;
}

/* The one API route that DOES write the attribute, and it is not the scenario
 * verbs: `set_param` takes any (io, param) pair. It is inert until the next
 * start - the AutoScenario is built by the IO constructor - which is exactly
 * why the guard has to live on the startup pass and not on a creation verb.
 */
TEST_F(AutoScenarioUidUniquenessTest, TheGenericSetParamCommandStillWritesTheUidAttribute)
{
    loadTwoScenariosWithDistinctUids();

    JsonApi api;
    const Json reply = api.buildJsonSetParam(Params{{ "id", SC_B_IO },
                                                    { "param", AutoScenarioDef::KEY_UID },
                                                    { "value", SC_A_UID }});
    ASSERT_EQ(std::string(), reply.value("error", std::string())) << reply.dump();
    EXPECT_EQ(std::string(SC_A_UID), uidOf(SC_B_IO));

    //It really is the generic route and not a scenario one: the same command
    //is refused on the id, which is the only key that has a guard of its own.
    const Json refused = api.buildJsonSetParam(Params{{ "id", SC_B_IO },
                                                      { "param", "id" },
                                                      { "value", "io_t3117_other" }});
    EXPECT_NE(std::string(), refused.value("error", std::string())) << refused.dump();
}
