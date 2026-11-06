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
 * The three grammars of OutputShutterSmart that carry NO number - "up",
 * "down", "toggle" - and the honesty of "set_state <n>".
 *
 * WHAT THESE THREE WORDS MEAN IS A PRODUCT FACT, NOT A CONVENTION PICKED HERE.
 * Four sources, all agreeing: the IO documentation the installer displays
 * (OutputShutterSmart.cpp, actionAdd "up" = "Open the shutter", "down" =
 * "Close the shutter", "toggle" = "Invert shutter state"); the same two labels
 * in calaos_installer (FormActionStd.cpp); the two KNX group addresses a real
 * installation wires, knx_group_up and knx_group_down, driven by
 * setOutputUp()/setOutputDown(); and check_condition_value(), for which a
 * served value of 100 is "closed"/"false" and anything below is "open"/"true".
 * So: "up" opens and drives the served value DOWN to 0, "down" closes and
 * drives it UP to 100. Two installed configurations spend 78 of their 84
 * shutter actions on exactly these three words.
 *
 * WHY A SEPARATE FILE FROM core/ShutterPercentGrammar_test. That one owns the
 * grammars that carry a percentage; its probe and its excursion harness live
 * in its own anonymous namespace. The shape is deliberately the same - the
 * oracle is the one it established and there is no reason to invent another -
 * but the two files stay independent.
 *
 * THE ORACLE, AND WHY IT IS NOT A RESTATEMENT OF THE CODE.
 * A shutter has no readable "I am going up": asserting sens == SHUTTER_UP
 * would only read back the enum the mutated branch has just written. What is
 * watched instead lives outside the logic:
 *
 *   - THE ENERGIZED TERMINAL, counted per side. setOutputUp()/setOutputDown()
 *     are the boundary with the hardware; every real driver reimplements only
 *     those. Which one closes is what an electrician measures, and it decides
 *     which way the motor turns.
 *   - THE POSITION SERVED TO THE APPLICATIONS, sampled WHILE IT MOVES and
 *     pinned as a whole excursion: the extremum on the wrong side of the
 *     starting point, and the resting value.
 *
 * NOTHING HERE ASSERTS A DURATION, AND THAT IS DELIBERATE. Every command
 * without a number runs to a mechanical end, so the far extremum of an
 * excursion does not depend on when the loop was pumped, and the near one is
 * the starting point itself; the terminal counts are read synchronously,
 * before the loop is pumped at all. Load can only make the last tick land
 * LATER, that is closer to the end, so the far bounds are safe in both
 * directions. The only number in milliseconds is kHangGuardMs, which is not a
 * measurement: it is thirteen times the longest nominal travel and exists so a
 * broken build fails instead of spinning for ever. A test that bounded a
 * travel time would be measuring the machine.
 *
 * THE EIGHT WAYS A TEST OF THIS COULD LIE:
 *  1. a direct call: every command is a set_state over a real WS session and
 *     every state read is a real get_state answer;
 *  2. a spelled name: no assertion reads cmd_state to decide a direction -
 *     cmd_state is the word the branch itself wrote, so it would agree with
 *     any permutation. Directions are read from the terminals and from the
 *     served position only;
 *  3. a badly bounded length: nothing is refused for its length - the rule
 *     under test is that the whole argument READS as an int;
 *  4. a false fixture: two seconds of travel each way is a real course, so a
 *     real end timer is armed and a real stop can be observed;
 *  5. presence instead of position: the excursion is pinned by its extrema,
 *     never by "was seen to change";
 *  6. a haystack excluding what is sought: the set_state case that must stay
 *     green pins that a readable percentage still ACTS, so a guard that
 *     refused too much would redden;
 *  7. a permutation that changes no value: exchanging two directions changes
 *     no number, only where it is sent. Every direction case therefore starts
 *     from an ASYMMETRIC position and pins the end it must reach, so no
 *     assertion here can be satisfied by both worlds;
 *  8. a sensor that measures nothing passing for a sensor that finds nothing:
 *     each direction case also pins the terminal count at ONE, so a probe that
 *     had stopped counting would redden rather than agree.
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

int elapsedMs(const std::chrono::steady_clock::time_point &t0)
{
    return (int) std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - t0).count();
}

/* libuv arms every deadline from loop->time, its CACHED clock, refreshed only
 * at the top of uv_run(). An idle stretch therefore arms the next deadline in
 * the past by the length of that stretch. Take the origin FIRST, call this,
 * then command. */
void freshenLoopClock()
{
    uvw::Loop::getDefault()->run<uvw::Loop::Mode::NOWAIT>();
}

/* Pumps until pred() is first seen to hold; answers false only when the hang
 * guard ran out. pred() runs on every turn, which is where the direction cases
 * take their samples of the served position. */
bool pumpUntil(const std::function<bool()> &pred, int guardMs)
{
    auto loop = uvw::Loop::getDefault();
    const auto t0 = std::chrono::steady_clock::now();

    while (!pred())
    {
        if (elapsedMs(t0) > guardMs)
            return false;
        loop->run<uvw::Loop::Mode::NOWAIT>();
    }
    return true;
}

/* Counts the two terminals SEPARATELY: the whole question is WHICH one. */
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
        "T3131Smart", [](Params &p) -> IOBase * { return new SmartProbe(p); });
}

/* Twenty digits: it reads as a number all the way to its end, so nothing about
 * its SHAPE can be refused - only its range. */
const char *const kOutOfRange = "99999999999999999999";

/* Two seconds each way, so 25 / 50 / 75 percent land on 0.5, 1.0 and 1.5 s,
 * all exact in binary. */
const int kTravelSec = 2;

/* Not a measurement: thirteen times the longest travel a case commands. */
const int kHangGuardMs = 20000;

}

class ShutterBareGrammarTest: public JsonApiCharacterizationTest
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
    std::string shutterId() const { return "t3131_" + caseName(); }

    SmartProbe *makeShutter(const std::string &id)
    {
        Params p = {{ "type", "T3131Smart" },
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

    //Puts the shutter at a known percentage without moving it.
    void placeAt(SmartProbe *sh, const std::string &id, int percent)
    {
        ASSERT_EQ("true", setState(id, "set_state " + Utils::to_string(percent)));
        ASSERT_EQ(percent, (int) sh->get_value_double());
        ASSERT_TRUE(sh->isStopped());
    }

    /* Commands `value`, then pumps until the shutter is seen stopped while
     * sampling the position served to the applications on every turn. lo/hi
     * come back as the extrema of the whole excursion, starting point
     * included. */
    bool travel(SmartProbe *sh, const std::string &id, const std::string &value,
                int &lo, int &hi)
    {
        freshenLoopClock();
        if (setState(id, value) != "true")
            return false;

        lo = hi = (int) sh->get_value_double();

        return pumpUntil([&]()
        {
            const int now = (int) sh->get_value_double();
            lo = std::min(lo, now);
            hi = std::max(hi, now);
            return sh->isStopped();
        }, kHangGuardMs);
    }
};

/*******************************************************************************
 * The direction of travel of the three words that carry no number
 ******************************************************************************/

TEST_F(ShutterBareGrammarTest, ABareUpRunsTheShutterToItsOpenEnd)
{
    SmartProbe *sh = makeShutter(shutterId());
    ASSERT_NE(nullptr, sh);
    ASSERT_NO_FATAL_FAILURE(placeAt(sh, shutterId(), 75));

    int lo = 0, hi = 0;
    ASSERT_TRUE(travel(sh, shutterId(), "up", lo, hi))
            << "the shutter never came to rest";

    EXPECT_EQ(75, hi)
            << "the shutter was served at " << hi << " %, past the 75 % it "
               "started from: an \"up\" command closed it";
    EXPECT_EQ(0, lo) << "an \"up\" command did not reach the open end";
    EXPECT_EQ(0, (int) sh->get_value_double())
            << "the resting position is not the end of the excursion";

    EXPECT_EQ(1, sh->upEnergized) << "the up terminal never closed";
    EXPECT_EQ(0, sh->downEnergized)
            << "the down terminal closed on an \"up\" command: the motor turns "
               "the wrong way";

    EXPECT_EQ("stop 0", servedState(shutterId()));
}

TEST_F(ShutterBareGrammarTest, ABareDownRunsTheShutterToItsClosedEnd)
{
    SmartProbe *sh = makeShutter(shutterId());
    ASSERT_NE(nullptr, sh);
    ASSERT_NO_FATAL_FAILURE(placeAt(sh, shutterId(), 25));

    int lo = 0, hi = 0;
    ASSERT_TRUE(travel(sh, shutterId(), "down", lo, hi))
            << "the shutter never came to rest";

    EXPECT_EQ(25, lo)
            << "the shutter was served at " << lo << " %, below the 25 % it "
               "started from: a \"down\" command opened it";
    EXPECT_EQ(100, hi) << "a \"down\" command did not reach the closed end";
    EXPECT_EQ(100, (int) sh->get_value_double())
            << "the resting position is not the end of the excursion";

    EXPECT_EQ(1, sh->downEnergized) << "the down terminal never closed";
    EXPECT_EQ(0, sh->upEnergized)
            << "the up terminal closed on a \"down\" command: the motor turns "
               "the wrong way";

    EXPECT_EQ("stop 100", servedState(shutterId()));
}

TEST_F(ShutterBareGrammarTest, AToggleOnAStoppedShutterReversesTheLastMove)
{
    //Two shutters, one case: a stopped toggle reads the direction of the last
    //completed move, so a single starting point only exercises one side of
    //that choice and an exchange would still find the other one right. The two
    //arming moves are percentage commands on purpose - they are held by
    //core/ShutterPercentGrammar_test, so what this case adds is the toggle.
    SmartProbe *afterUp = makeShutter(shutterId());
    SmartProbe *afterDown = makeShutter(shutterId() + "_b");
    ASSERT_NE(nullptr, afterUp);
    ASSERT_NE(nullptr, afterDown);
    ASSERT_NO_FATAL_FAILURE(placeAt(afterUp, shutterId(), 75));
    ASSERT_NO_FATAL_FAILURE(placeAt(afterDown, shutterId() + "_b", 25));

    int lo = 0, hi = 0;
    ASSERT_TRUE(travel(afterUp, shutterId(), "set 50", lo, hi));
    ASSERT_TRUE(travel(afterDown, shutterId() + "_b", "set 50", lo, hi));

    const int restedUp = (int) afterUp->get_value_double();
    const int restedDown = (int) afterDown->get_value_double();
    const int upTerminals = afterUp->upEnergized;
    const int downTerminals = afterDown->downEnergized;

    ASSERT_TRUE(travel(afterUp, shutterId(), "toggle", lo, hi));
    EXPECT_EQ(restedUp, lo)
            << "the shutter that had just opened went on opening on a toggle";
    //The far end is bounded rather than pinned: a move that STARTS from a
    //mid-travel stop starts from a position that is not a round number of
    //milliseconds, and the end timer is armed on a truncated millisecond, so
    //the last tick can land one percent short. Load can only make it land
    //later, hence more exactly on the end.
    EXPECT_GE(hi, 95) << "a toggle after an opening did not close the shutter";
    EXPECT_EQ(1, afterUp->downEnergized) << "the down terminal never closed";
    EXPECT_EQ(upTerminals, afterUp->upEnergized)
            << "the up terminal closed again: a toggle after an opening turns "
               "the motor the same way";

    ASSERT_TRUE(travel(afterDown, shutterId() + "_b", "toggle", lo, hi));
    EXPECT_EQ(restedDown, hi)
            << "the shutter that had just closed went on closing on a toggle";
    EXPECT_LE(lo, 5) << "a toggle after a closing did not open the shutter";
    EXPECT_EQ(1, afterDown->upEnergized) << "the up terminal never closed";
    EXPECT_EQ(downTerminals, afterDown->downEnergized)
            << "the down terminal closed again: a toggle after a closing turns "
               "the motor the same way";
}

TEST_F(ShutterBareGrammarTest, AToggleWhileMovingStopsAndTheNextOneReversesIt)
{
    //The other half of "invert shutter state": a toggle on a shutter that is
    //running stops it, and the direction it remembers is the one it was
    //running in. Nothing is pumped between the two commands, so the first move
    //is still under way when the first toggle lands.
    SmartProbe *sh = makeShutter(shutterId());
    ASSERT_NE(nullptr, sh);
    ASSERT_NO_FATAL_FAILURE(placeAt(sh, shutterId(), 50));

    freshenLoopClock();
    ASSERT_EQ("true", setState(shutterId(), "down"));
    ASSERT_FALSE(sh->isStopped()) << "the shutter never started";
    ASSERT_EQ(1, sh->downEnergized);

    ASSERT_EQ("true", setState(shutterId(), "toggle"));
    EXPECT_TRUE(sh->isStopped()) << "a toggle on a running shutter did not stop it";
    EXPECT_EQ(0, sh->upEnergized)
            << "stopping closed the up terminal on a shutter with no impulse time";

    const int rested = (int) sh->get_value_double();

    int lo = 0, hi = 0;
    ASSERT_TRUE(travel(sh, shutterId(), "toggle", lo, hi));
    EXPECT_EQ(rested, hi)
            << "the shutter went on closing: the toggle that stopped it "
               "remembered the wrong direction";
    EXPECT_LE(lo, 5) << "a toggle after an interrupted closing did not open "
                        "the shutter";
    EXPECT_EQ(1, sh->upEnergized) << "the up terminal never closed";
    EXPECT_EQ(1, sh->downEnergized) << "the down terminal closed a second time";
}

/*******************************************************************************
 * set_state <n>: answering true without acting
 ******************************************************************************/

TEST_F(ShutterBareGrammarTest, ASetStateWithAPercentagePlacesTheShutterWithoutMoving)
{
    //GREEN BEFORE AND AFTER, and it is the half that tells ACTED from
    //ANSWERED: a guard that refused more than it should would redden here.
    SmartProbe *sh = makeShutter(shutterId());
    ASSERT_NE(nullptr, sh);
    ASSERT_NO_FATAL_FAILURE(placeAt(sh, shutterId(), 25));

    EXPECT_EQ("true", setState(shutterId(), "set_state 80"));
    EXPECT_EQ(80, (int) sh->get_value_double())
            << "the accepted command did not place the shutter";
    EXPECT_EQ("set 80", sh->get_command_string());
    EXPECT_EQ("stop 80", servedState(shutterId()));
    EXPECT_EQ(0, sh->upEnergized + sh->downEnergized)
            << "set_state closed a terminal: it must not start a real move";
}

TEST_F(ShutterBareGrammarTest, ASetStateThatDoesNotReadAsAnIntIsRefusedAndActsOnNothing)
{
    //RED BEFORE THE FIX on every "false" below: the branch answered true and
    //did nothing at all, a third answer where its five neighbours have two.
    //The position and the command state are asserted alongside each refusal
    //precisely because "answered true" and "acted" were indistinguishable.
    SmartProbe *sh = makeShutter(shutterId());
    ASSERT_NE(nullptr, sh);
    ASSERT_NO_FATAL_FAILURE(placeAt(sh, shutterId(), 40));

    //The two forms the IO documentation used to advertise, which this class
    //never handled.
    EXPECT_EQ("false", setState(shutterId(), "set_state true"));
    EXPECT_EQ("false", setState(shutterId(), "set_state false"));
    //Partially readable and out of int range.
    EXPECT_EQ("false", setState(shutterId(), "set_state 40abc"));
    EXPECT_EQ("false", setState(shutterId(), std::string("set_state ") + kOutOfRange));
    //Green on both sides, and by a different mechanism: an argument that is
    //only the separator never reaches set_value(), the transport refuses it.
    EXPECT_EQ("false", setState(shutterId(), "set_state "));

    EXPECT_EQ(40, (int) sh->get_value_double())
            << "a refused set_state moved the shutter";
    EXPECT_EQ("set 40", sh->get_command_string())
            << "a refused set_state published a command state";
    EXPECT_EQ(0, sh->upEnergized + sh->downEnergized)
            << "a refused set_state closed a terminal";
}
