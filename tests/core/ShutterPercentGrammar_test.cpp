/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
 **  along with Calaos; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/

/******************************************************************************
 * The three grammars of OutputShutterSmart that carry a PERCENTAGE:
 * "set <n>", "up <n>", "down <n>".
 *
 * TWO DEFECTS LIVE HERE, AND THE SECOND ONE IS THE REASON THIS FILE EXISTS.
 *
 * (1) THE SATURATION. Utils::from_string() publishes a SATURATED value on an
 *     out of range argument and answers false (contract table of
 *     src/lib/StringUtils.h); these three branches ignore the answer. The
 *     shutter is then aimed at a position no travel can reach, so Down() arms
 *     an end timer hundreds of days away: the relay is energized and NOTHING
 *     will ever stop it. The saturated number is published on top of that, as
 *     the command state, and written into the state cache that survives a
 *     restart. This is not undefined behaviour - the product is computed in
 *     double - it is an absurd position, held forever.
 *
 * (2) ⭐⭐ THE DIRECTION OF TRAVEL HAD NO SENSOR AT ALL. Measured on master
 *     before a line of this file was written: exchanging the two directions of
 *     these grammars - "up <n>" closes the shutter, "down <n>" opens it,
 *     cmd_state untouched - left `make check` ENTIRELY GREEN, 0 red case.
 *     Closing (1) with a two line guard would have left (2) whole.
 *
 * ⭐ THE ORACLE FOR (2), AND WHY IT IS NOT A RESTATEMENT OF THE CODE.
 * A shutter has no readable "I am going up". Asserting `sens == SHUTTER_UP`
 * would only read back the enum the mutated branch has just written - the
 * defect would set it too. What this file watches instead are the two things
 * that exist OUTSIDE the logic:
 *
 *   - THE ENERGIZED TERMINAL. setOutputUp()/setOutputDown() are the boundary
 *     between Calaos and the hardware; every real driver (Wago, Gpio, KNX,
 *     Mqtt) implements only those. Which of the two closes is what an
 *     electrician measures, and it decides which way the motor turns.
 *   - THE POSITION SERVED TO THE APPLICATIONS, WATCHED WHILE IT MOVES.
 *     get_value_string() is what buildJsonState() hands to every connected
 *     client. The cases sample it on every turn of the loop and pin the whole
 *     EXCURSION: the extremum on the wrong side of the starting point, and
 *     the resting value. A permutation that changes no value cannot survive a
 *     band that is on the other side of where it started.
 *
 * Both are read through a real WebSocket session; no case calls set_value()
 * or any Up()/Down() directly, because the question is what an application
 * obtains.
 *
 * THE SIX WAYS A TEST OF THIS COULD LIE:
 *  1. a direct call: every command here is a set_state over a real WS
 *     session, and every state read is a real get_state answer;
 *  2. a spelled name: the refusals are read from the transport's own
 *     {"success":...} field, never from a log line;
 *  3. a badly bounded length: nothing is refused for its length - the rule is
 *     that the whole argument READS as an int, and
 *     ThePercentGrammarsRefuseWhatDoesNotReadAsAnInt is what tells a range
 *     check from a read check;
 *  4. a false fixture: time_up = time_down = 2 s is a real travel, so a real
 *     end timer is armed and a real stop can be observed. A fixture whose
 *     travel is zero arms nothing and could not see a shutter left running;
 *  5. presence instead of position: the served state is compared whole, and
 *     the movement is pinned by an extremum, not by "was seen to change";
 *  6. a haystack excluding what is sought: TheWholeLegalPercentRangeIsStill
 *     Accepted and TheCommandsWithoutANumberAreUntouched are green on both
 *     sides and redden a guard that refuses more than it should.
 *
 * ⭐ AND THE SEVENTH, THE ONE MEASURED ON THIS TICKET: a test can be blind to
 * a PERMUTATION THAT CHANGES NO VALUE. The two directions produce states of
 * the same shape and the same numbers; only the movement differs. That is why
 * every direction case starts from an ASYMMETRIC position (75 % for an
 * opening, 25 % for a closing) and pins the side of the excursion: swapping
 * the branches sends the shutter to the far end instead, and no assertion
 * here can be satisfied by both worlds.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include <algorithm>
#include <chrono>
#include <functional>

#include "IOFactory.h"
#include "ListeRoom.h"
#include "OutputShutterSmart.h"
#include "libuvw.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/* ---- loop helpers (same shape as core/ImpulseOverflow_test) ------------- */

int elapsedMs(const std::chrono::steady_clock::time_point &t0)
{
    return (int) std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - t0).count();
}

/* libuv arms every deadline from loop->time, its CACHED clock, refreshed only
 * at the top of uv_run(). An idle stretch therefore arms the next deadline in
 * the past by the length of that stretch, which is F-FLAKY-1. Take the origin
 * FIRST, call this, then command. */
void freshenLoopClock()
{
    uvw::Loop::getDefault()->run<uvw::Loop::Mode::NOWAIT>();
}

/* Answers the ms elapsed since t0 at the moment pred() was first SEEN to
 * hold, or -1 within the budget. pred() runs on every turn, which is where
 * the direction cases take their samples of the served position. */
int pumpUntilSince(const std::chrono::steady_clock::time_point &t0,
                   const std::function<bool()> &pred, int timeoutMs)
{
    auto loop = uvw::Loop::getDefault();

    while (!pred())
    {
        if (elapsedMs(t0) > timeoutMs)
            return -1;
        loop->run<uvw::Loop::Mode::NOWAIT>();
    }

    return elapsedMs(t0);
}

/* ---- probe -------------------------------------------------------------- */

/* Counts the two terminals SEPARATELY. core/ImpulseOverflow_test folds them
 * into one counter because it only ever asks "was a relay pulled at all";
 * here the whole point is WHICH one. */
class SmartProbe: public OutputShutterSmart
{
public:
    explicit SmartProbe(Params &p): OutputShutterSmart(p) {}

    int upEnergized = 0;
    int downEnergized = 0;

    bool isStopped() { return check_condition_value("stop", true); }

protected:
    void setOutputUp(bool enable) override { if (enable) upEnergized++; }
    void setOutputDown(bool enable) override { if (enable) downEnergized++; }
};

//IOFactory::RegisterClass is first-wins and has no unregister.
void registerProbe()
{
    static bool done = false;
    if (done) return;
    done = true;

    IOFactory::Instance().RegisterClass(
        "T3123Smart", [](Params &p) -> IOBase * { return new SmartProbe(p); });
}

/* Twenty digits: it reads as a number all the way to its end, so nothing
 * about its SHAPE can be refused - only its range. */
const char *const kOutOfRange = "99999999999999999999";

/* Two seconds of travel each way. 25 / 50 / 75 percent of it land on 0.5,
 * 1.0 and 1.5 s, all exact in binary, so the served percentage is not the
 * truncation of a value sitting a hair under a round number. */
const int kTravelSec = 2;

/* Longest move a case commands is 1000 ms (a "set" across half the travel),
 * and the tick that publishes the position is travel/100 = 20 ms. */
const int kBudgetMs = 3000;

}

class ShutterPercentGrammarTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        registerProbe();
        JsonApiCharacterizationTest::SetUp();

        //Config's IO state cache is process wide and never cleared, and the
        //shutter constructor reads it back under "<id>_<type>".
        forgetIOState(shutterId());
        forgetIOState(shutterId() + "_b");

        loadConfig();
    }

    std::string caseName() const
    {
        return ::testing::UnitTest::GetInstance()->current_test_info()->name();
    }
    std::string shutterId() const { return "t3123_" + caseName(); }

    SmartProbe *makeShutter(const std::string &id)
    {
        Params p = {{ "type", "T3123Smart" },
                    { "id", id },
                    { "name", id },
                    { "time", Utils::to_string(kTravelSec) },
                    { "time_up", Utils::to_string(kTravelSec) },
                    { "time_down", Utils::to_string(kTravelSec) },
                    { "enabled", "true" },
                    { "visible", "false" }};
        return dynamic_cast<SmartProbe *>(createIO(p));
    }

    //One authenticated set_state over the real WS transport; answers the
    //"success" field the server sent back.
    std::string setState(const std::string &id, const std::string &value)
    {
        WsTestSession ws;
        ws.send(Json{{ "msg", "set_state" },
                     { "msg_id", "1" },
                     { "data", {{ "id", id }, { "value", value }} }});

        if (ws.count() != 1u)
            return "<no answer>";
        return ws.lastData().value("success", std::string("<no success key>"));
    }

    //What a connected application obtains for this IO, through the real
    //get_state command and not through the object.
    std::string servedState(const std::string &id)
    {
        WsTestSession ws;
        ws.send(Json{{ "msg", "get_state" },
                     { "msg_id", "2" },
                     { "data", {{ "items", Json::array({ id }) }} }});

        if (ws.count() != 1u)
            return "<no answer>";
        return ws.lastData().value(id, std::string("<no such io>"));
    }

    //Puts the shutter at a known percentage without moving it: this is what
    //set_state <n> is documented for.
    void placeAt(SmartProbe *sh, const std::string &id, int percent)
    {
        ASSERT_EQ("true", setState(id, "set_state " + Utils::to_string(percent)));
        ASSERT_EQ(percent, (int) sh->get_value_double());
        ASSERT_TRUE(sh->isStopped());
    }

    /* Commands `value`, then pumps until the shutter is seen stopped while
     * sampling the position served to the applications on every turn.
     * Answers false when the budget ran out; lo/hi come back as the extrema
     * of the whole excursion, starting point included. */
    bool travel(SmartProbe *sh, const std::string &id, const std::string &value,
                int &lo, int &hi)
    {
        const auto issued = std::chrono::steady_clock::now();
        freshenLoopClock();
        if (setState(id, value) != "true")
            return false;

        lo = hi = (int) sh->get_value_double();

        const int stoppedAt = pumpUntilSince(issued, [&]()
        {
            const int now = (int) sh->get_value_double();
            lo = std::min(lo, now);
            hi = std::max(hi, now);
            return sh->isStopped();
        }, kBudgetMs);

        return stoppedAt >= 0;
    }
};

/*******************************************************************************
 * ⭐⭐ The direction of travel
 *
 * GREEN BEFORE AND AFTER THE FIX, and RED under an exchange of the two
 * directions - which is the whole point: this is the half of the ticket that
 * no guard closes.
 ******************************************************************************/

TEST_F(ShutterPercentGrammarTest, AnUpByPercentOpensTheShutter)
{
    SmartProbe *sh = makeShutter(shutterId());
    ASSERT_NE(nullptr, sh);
    ASSERT_NO_FATAL_FAILURE(placeAt(sh, shutterId(), 75));

    int lo = 0, hi = 0;
    ASSERT_TRUE(travel(sh, shutterId(), "up 25", lo, hi))
            << "the shutter never came to rest inside the budget";

    EXPECT_EQ(75, hi)
            << "the shutter was served at " << hi << " %, past the 75 % it "
               "started from: an \"up\" command closed it";
    EXPECT_GE(lo, 45);
    EXPECT_LE(lo, 55);
    EXPECT_EQ(lo, (int) sh->get_value_double())
            << "the resting position is not the end of the excursion";

    EXPECT_EQ(1, sh->upEnergized) << "the up terminal never closed";
    EXPECT_EQ(0, sh->downEnergized)
            << "the down terminal closed on an \"up\" command: the motor turns "
               "the wrong way";

    EXPECT_EQ("stop " + Utils::to_string((double) lo), servedState(shutterId()));
}

TEST_F(ShutterPercentGrammarTest, ADownByPercentClosesTheShutter)
{
    SmartProbe *sh = makeShutter(shutterId());
    ASSERT_NE(nullptr, sh);
    ASSERT_NO_FATAL_FAILURE(placeAt(sh, shutterId(), 25));

    int lo = 0, hi = 0;
    ASSERT_TRUE(travel(sh, shutterId(), "down 25", lo, hi))
            << "the shutter never came to rest inside the budget";

    EXPECT_EQ(25, lo)
            << "the shutter was served at " << lo << " %, below the 25 % it "
               "started from: a \"down\" command opened it";
    EXPECT_GE(hi, 45);
    EXPECT_LE(hi, 55);
    EXPECT_EQ(hi, (int) sh->get_value_double())
            << "the resting position is not the end of the excursion";

    EXPECT_EQ(1, sh->downEnergized) << "the down terminal never closed";
    EXPECT_EQ(0, sh->upEnergized)
            << "the up terminal closed on a \"down\" command: the motor turns "
               "the wrong way";
}

TEST_F(ShutterPercentGrammarTest, ASetBelowThePositionOpensAndASetAboveItCloses)
{
    //Two shutters, one case: "set" picks its direction by comparing the
    //target with the current position, so a single target only exercises one
    //side of that comparison and an exchange of the two sides would still
    //find one of them right.
    SmartProbe *opening = makeShutter(shutterId());
    SmartProbe *closing = makeShutter(shutterId() + "_b");
    ASSERT_NE(nullptr, opening);
    ASSERT_NE(nullptr, closing);
    ASSERT_NO_FATAL_FAILURE(placeAt(opening, shutterId(), 75));
    ASSERT_NO_FATAL_FAILURE(placeAt(closing, shutterId() + "_b", 25));

    int lo = 0, hi = 0;
    ASSERT_TRUE(travel(opening, shutterId(), "set 25", lo, hi));
    EXPECT_EQ(75, hi) << "a target below the position closed the shutter";
    EXPECT_GE(lo, 20);
    EXPECT_LE(lo, 30);
    EXPECT_EQ(1, opening->upEnergized);
    EXPECT_EQ(0, opening->downEnergized);

    ASSERT_TRUE(travel(closing, shutterId() + "_b", "set 75", lo, hi));
    EXPECT_EQ(25, lo) << "a target above the position opened the shutter";
    EXPECT_GE(hi, 70);
    EXPECT_LE(hi, 80);
    EXPECT_EQ(1, closing->downEnergized);
    EXPECT_EQ(0, closing->upEnergized);
}

/*******************************************************************************
 * The saturation
 ******************************************************************************/

TEST_F(ShutterPercentGrammarTest, AnOutOfRangeSetIsRefusedAtTheApiBoundary)
{
    //RED BEFORE THE FIX: the server answers {"success":"true"} and the
    //shutter is aimed at 2147483647 % of its travel.
    SmartProbe *sh = makeShutter(shutterId());
    ASSERT_NE(nullptr, sh);
    ASSERT_NO_FATAL_FAILURE(placeAt(sh, shutterId(), 25));

    EXPECT_EQ("false", setState(shutterId(), std::string("set ") + kOutOfRange))
            << "a percentage that does not fit in an int must be refused, not "
               "saturated";
    EXPECT_EQ(25, (int) sh->get_value_double())
            << "the refused command moved the shutter";
    EXPECT_EQ(0, sh->upEnergized + sh->downEnergized)
            << "the refused command closed a terminal";
}

TEST_F(ShutterPercentGrammarTest, AnOutOfRangeUpByPercentIsRefused)
{
    //RED BEFORE THE FIX. Its own branch, its own erase(), its own case.
    SmartProbe *sh = makeShutter(shutterId());
    ASSERT_NE(nullptr, sh);
    ASSERT_NO_FATAL_FAILURE(placeAt(sh, shutterId(), 25));

    EXPECT_EQ("false", setState(shutterId(), std::string("up ") + kOutOfRange));
    EXPECT_EQ(25, (int) sh->get_value_double());
    EXPECT_EQ(0, sh->upEnergized + sh->downEnergized);
}

TEST_F(ShutterPercentGrammarTest, AnOutOfRangeDownByPercentIsRefused)
{
    //RED BEFORE THE FIX.
    SmartProbe *sh = makeShutter(shutterId());
    ASSERT_NE(nullptr, sh);
    ASSERT_NO_FATAL_FAILURE(placeAt(sh, shutterId(), 25));

    EXPECT_EQ("false", setState(shutterId(), std::string("down ") + kOutOfRange));
    EXPECT_EQ(25, (int) sh->get_value_double());
    EXPECT_EQ(0, sh->upEnergized + sh->downEnergized);
}

TEST_F(ShutterPercentGrammarTest, TheSaturatedPercentIsNeverPublishedAsTheState)
{
    //RED BEFORE THE FIX, and the fixture is at 100 % ON PURPOSE. The three
    //branches write cmd_state BEFORE they aim the shutter, and Up()/Down()
    //normally overwrite it with a bare "up"/"down" - so the saturated number
    //only SURVIVES on the paths where they return early, the commonest of
    //which is a shutter that has nowhere left to travel. It then goes through
    //updateCache() into the state cache, which outlives a restart, and it is
    //what a rule copying this IO's command state writes onto another shutter
    //(Rules/ActionStd.cpp).
    SmartProbe *sh = makeShutter(shutterId());
    ASSERT_NE(nullptr, sh);
    ASSERT_NO_FATAL_FAILURE(placeAt(sh, shutterId(), 100));
    ASSERT_EQ("set 100", sh->get_command_string());

    setState(shutterId(), std::string("set ") + kOutOfRange);

    EXPECT_EQ("set 100", sh->get_command_string())
            << "the refused command published a saturated percentage as the "
               "command state";

    Params cached;
    Config::Instance().ReadValueParams(shutterId() + "_T3123Smart", cached);
    EXPECT_EQ("set 100", cached["cmd_state"])
            << "and the same saturated percentage reached the state cache, "
               "which outlives a restart";
    //GREEN ON BOTH SIDES, and it is the correction the ticket owed: the
    //saturated number never leaves through the "state" field. That one is
    //get_value_string(), and writePosition() clamps the position to the
    //travel. What escapes is the COMMAND state above, on a channel no client
    //polls - which is why nothing was ever seen.
    EXPECT_EQ("stop 100", servedState(shutterId()));
}

TEST_F(ShutterPercentGrammarTest, AnOutOfRangeDownLeavesNoShutterRunningForEver)
{
    //⭐ RED BEFORE THE FIX, and this is the consequence a user feels. Two
    //shutters, one budget, one loop: the first proves the budget really can
    //see a stop, the second is the one under test. Without the first leg,
    //"was not seen stopped" would also be the answer of a loop nobody pumps.
    //
    //A saturated target sits 42 949 672 s of travel away, so Down() arms its
    //end timer some five hundred days out: the terminal stays closed and
    //nothing in the process will ever open it again.
    SmartProbe *control = makeShutter(shutterId());
    SmartProbe *victim = makeShutter(shutterId() + "_b");
    ASSERT_NE(nullptr, control);
    ASSERT_NE(nullptr, victim);
    ASSERT_NO_FATAL_FAILURE(placeAt(control, shutterId(), 25));
    ASSERT_NO_FATAL_FAILURE(placeAt(victim, shutterId() + "_b", 25));

    const auto issued = std::chrono::steady_clock::now();
    freshenLoopClock();
    ASSERT_EQ("true", setState(shutterId(), "down 25"));
    setState(shutterId() + "_b", std::string("down ") + kOutOfRange);

    ASSERT_GE(pumpUntilSince(issued, [&]() { return control->isStopped(); },
                             kBudgetMs), 0)
            << "the control shutter was never seen stopped: this budget cannot "
               "observe a stop at all, so the case below would prove nothing";

    EXPECT_EQ(0, victim->downEnergized)
            << "the out of range target closed the down terminal: the shutter "
               "is running with no stop armed at all";
    EXPECT_GE(pumpUntilSince(issued, [&]() { return victim->isStopped(); },
                             kBudgetMs), 0)
            << "the shutter is still travelling: a saturated target arms an "
               "end timer hundreds of days away, so the move never ends";
}

/*******************************************************************************
 * What the guard must not cost
 ******************************************************************************/

TEST_F(ShutterPercentGrammarTest, TheWholeLegalPercentRangeIsStillAccepted)
{
    //GREEN BEFORE AND AFTER. The boundary of the rule, stated rather than
    //discovered: only what does not READ as an int is refused.
    SmartProbe *sh = makeShutter(shutterId());
    ASSERT_NE(nullptr, sh);
    ASSERT_NO_FATAL_FAILURE(placeAt(sh, shutterId(), 50));

    EXPECT_EQ("true", setState(shutterId(), "set 0"));
    EXPECT_EQ("true", setState(shutterId(), "stop"));
    EXPECT_EQ("true", setState(shutterId(), "set 100"));
    EXPECT_EQ("true", setState(shutterId(), "stop"));
    EXPECT_EQ("true", setState(shutterId(), "up 5"));
    EXPECT_EQ("true", setState(shutterId(), "stop"));
    EXPECT_EQ("true", setState(shutterId(), "down 5"));
    EXPECT_EQ("true", setState(shutterId(), "stop"));
}

TEST_F(ShutterPercentGrammarTest, ThePercentGrammarsRefuseWhatDoesNotReadAsAnInt)
{
    //RED BEFORE THE FIX, and it is what tells a RANGE check from a READ
    //check: "12abc" fits in an int, from_string() reads 12 and answers false,
    //and the 12 used to be acted upon.
    SmartProbe *sh = makeShutter(shutterId());
    ASSERT_NE(nullptr, sh);
    ASSERT_NO_FATAL_FAILURE(placeAt(sh, shutterId(), 50));

    EXPECT_EQ("false", setState(shutterId(), "set 12abc"));
    EXPECT_EQ("false", setState(shutterId(), "up 5x"));
    EXPECT_EQ("false", setState(shutterId(), "down onze"));
    EXPECT_EQ(50, (int) sh->get_value_double())
            << "a partially readable percentage was acted upon";
    EXPECT_EQ("set 50", sh->get_command_string());
    EXPECT_EQ(0, sh->upEnergized + sh->downEnergized);
}

TEST_F(ShutterPercentGrammarTest, TheCommandsWithoutANumberAreUntouched)
{
    //GREEN BEFORE AND AFTER. up / down / stop / toggle / set_state carry no
    //percentage and must keep working; a guard written above the grammar
    //dispatch instead of inside the three branches would redden here.
    SmartProbe *sh = makeShutter(shutterId());
    ASSERT_NE(nullptr, sh);

    EXPECT_EQ("true", setState(shutterId(), "set_state 50"));
    EXPECT_EQ("true", setState(shutterId(), "down"));
    EXPECT_EQ("true", setState(shutterId(), "stop"));
    EXPECT_EQ("true", setState(shutterId(), "up"));
    EXPECT_EQ("true", setState(shutterId(), "stop"));
    EXPECT_EQ("true", setState(shutterId(), "toggle"));
    EXPECT_EQ("true", setState(shutterId(), "stop"));
    EXPECT_EQ("false", setState(shutterId(), "no such command"));
}
