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

/* T2.18 — null-guard audit of the createIO()/IOFactory::CreateIO() call
 * sites.
 *
 * T1.11 made the factory return nullptr on an unknown type or a duplicate id,
 * but two call sites still dereferenced the result blindly:
 *
 *  - AutoScenario::createInput() used to mark the returned
 *    pointer, and checkScenarioRules() then built rules over the internal IOs
 *    without checking any of them. A miss (or an existing IO of the wrong
 *    type squatting one of the "<scenario_id>_is_active/_step/_timer" ids,
 *    which makes the dynamic_cast fail) crashed the server.
 *  - JsonApi::buildAutoscenarioCreate() dereferenced both createIO()'s return
 *    and the dynamic_cast<Scenario *> of it, so the same conditions crashed
 *    the server on an API request.
 *
 * ListeRoom::createIO() itself also dereferenced its room argument, which is
 * null whenever the scenario IO is not attached to any room
 * (getRoomByIO() == nullptr in AutoScenario).
 *
 * All of these must end in a logged error (and an {"error": ...} answer on
 * the API path), never in a crash. Every test here segfaulted before the
 * guards were added.
 */

#include "CalaosCoreFixture.h"

#include "AutoScenario.h"
#include "JsonApi.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "Scenario.h"

#include <jansson.h>

using namespace Calaos;
using namespace CalaosTest;

class ScenarioNullGuardTest: public CoreFixture
{
};

/******************************************************************************
 * ListeRoom::createIO() with no room
 ******************************************************************************/

//A null room must fail the creation cleanly instead of crashing on
//room->AddIO(). Nothing may be left registered.
TEST_F(ScenarioNullGuardTest, CreateIoWithoutRoomFailsCleanly)
{
    loadConfig();

    int countBefore = ListeRoom::Instance().get_io_count();

    Params p = { { "type", "InternalBool" },
                 { "id", "io_t218_noroom" },
                 { "name", "noroom" } };
    EXPECT_EQ(ListeRoom::Instance().createIO(p, nullptr), nullptr);

    EXPECT_EQ(ListeRoom::Instance().get_io_count(), countBefore);
    EXPECT_EQ(ListeRoom::Instance().get_io("io_t218_noroom"), nullptr);
}

/******************************************************************************
 * AutoScenario::checkScenarioRules() with an unusable internal IO
 ******************************************************************************/

//An IO of a non-Internal type squatting the "<scenario_id>_is_active" id makes
//createInput() return an IO the dynamic_cast rejects: the rules build must be
//aborted, not built over a null pointer.
TEST_F(ScenarioNullGuardTest, CheckScenarioRulesAbortsWhenInternalIoIsHijacked)
{
    loadConfig();

    Params hijack = { { "type", "InputTimer" },
                      { "id", "sc_t218_is_active" },
                      { "name", "hijack" } };
    ASSERT_NE(createIO(hijack), nullptr);

    Params p = { { "type", "Scenario" },
                 { "id", "io_sc_t218" },
                 { "name", "sc t218" },
                 { "auto_scenario", "sc_t218" } };
    Scenario *sc = dynamic_cast<Scenario *>(createIO(p));
    ASSERT_NE(sc, nullptr);

    AutoScenario *as = sc->getAutoScenario();
    ASSERT_NE(as, nullptr);

    size_t rulesBefore = ListeRule::Instance().size();

    //Crashed on a null ioIsActive before T2.18
    as->checkScenarioRules();

    //Aborted before creating any rule
    EXPECT_EQ(as->getRuleSteps().size(), 0u);
    EXPECT_EQ(as->getRuleStepEnd(), nullptr);
    EXPECT_EQ(ListeRule::Instance().size(), rulesBefore);
}

/******************************************************************************
 * JsonApi::buildAutoscenarioCreate()
 ******************************************************************************/

//Same squatted id, but through the API: the request must be answered with an
//error, and the half-created scenario must be rolled back entirely.
TEST_F(ScenarioNullGuardTest, ApiScenarioCreateAnswersErrorInsteadOfCrashing)
{
    loadConfig();

    //The first auto scenario created gets the id "scenario_0"
    Params hijack = { { "type", "InputTimer" },
                      { "id", "scenario_0_is_active" },
                      { "name", "hijack" } };
    ASSERT_NE(createIO(hijack), nullptr);

    int countBefore = ListeRoom::Instance().get_io_count();
    size_t rulesBefore = ListeRule::Instance().size();

    JsonApi api;
    //E4.1r: buildAutoscenarioCreate() reads and answers a Json now. No
    //assertion of this case changed, only the type carrying them.
    const Json jdata = Json{{ "name", "T218 scenario" }};

    //Crashed before T2.18
    const Json jret = api.buildAutoscenarioCreate(jdata);

    ASSERT_TRUE(jret.is_object());
    ASSERT_TRUE(jret.contains("error"));
    EXPECT_NE("", jret.value("error", std::string()));

    //The scenario IO and the internal IOs created before the failure must all
    //be gone, only the squatter remains
    EXPECT_EQ(ListeRoom::Instance().get_io_count(), countBefore);
    EXPECT_EQ(ListeRule::Instance().size(), rulesBefore);
    EXPECT_TRUE(ListeRoom::Instance().getAutoScenarios().empty());
}

//The nominal API creation must keep working exactly as before.
TEST_F(ScenarioNullGuardTest, ApiScenarioCreateStillWorks)
{
    loadConfig();

    JsonApi api;
    //The room is part of the payload now: `create` refuses a document that
    //does not name one instead of quietly dropping the scenario into room 0.
    const Json jdata = Json{{ "name", "T218 scenario" },
                            { "room_name", ROOM_NAME },
                            { "room_type", ROOM_TYPE }};

    const Json jret = api.buildAutoscenarioCreate(jdata);

    ASSERT_TRUE(jret.is_object());
    EXPECT_FALSE(jret.contains("error"));
    ASSERT_TRUE(jret.contains("id"));

    IOBase *io = ListeRoom::Instance().get_io(jret.value("id", std::string()));
    ASSERT_NE(io, nullptr);
    EXPECT_NE(dynamic_cast<Scenario *>(io), nullptr);
    EXPECT_FALSE(ListeRoom::Instance().getAutoScenarios().empty());
}
