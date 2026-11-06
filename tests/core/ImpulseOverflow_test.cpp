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
 * T3.25a §1 - the impulse duration that does not fit in an int.
 *
 * T3.25 closed the TRUNCATED command: {"value":"impulse up "} ends on its
 * separator and decodeSetState() refuses the shape. An out of range number
 * does not end on a separator, so it walks straight through that guard:
 *
 *     {"value":"impulse up 99999999999999999999"}
 *
 * OutputShutter::set_value() erases the prefix and calls Utils::from_string(),
 * which answers false AND publishes the SATURATED value (see the contract
 * table of src/lib/StringUtils.h). The answer is ignored, so ImpulseUp() is
 * handed INT_MAX.
 *
 * ⭐ WHAT THE USER FEELS, AND IT IS THE POINT OF THIS FILE. Up()/Down() arm the
 * end-of-impulse stop only when (impulse_action_time + impulse_time) is below
 * the full travel of the shutter. INT_MAX is not, so NO STOP IS ARMED AT ALL:
 * the impulse degenerates into the complete travel of the shutter, and the
 * server answers {"success":"true"} while it happens. A number nobody can act
 * on turns a nudge into a full open or a full close.
 *
 * THE MEASUREMENT, and why it is a CONTRAST and not a single wait.
 * AnOverflowingImpulseSendsNoShutterOnItsFullTravel drives TWO shutters of the
 * same fixture inside one case and one budget: the first gets a well formed
 * impulse and is SEEN STOPPED inside that budget, the second gets the out of
 * range one. "The second was not seen stopped" alone would be satisfied by a
 * broken oracle, by a loop nobody pumps, or by a host too slow to reach the
 * deadline; the first leg is what proves the budget is long enough to observe
 * a stop that really happens, in the same case, on the same loop, at the same
 * moment. Every wait is wall clock bounded (same discipline as
 * core/ShutterImpulse_test and core/Timer_test).
 *
 * ⚠️ WHAT THIS FILE DOES NOT CLAIM. The int addition itself is NOT undefined
 * behaviour on this tree: T3.34 already computes it as
 * (double)impulse_action_time + (double)impulse_time in all four Up()/Down()
 * of the two shutter classes. The fiche of T3.25a predates that fix. What
 * survived T3.34 is the OTHER half of its §1 - the full travel above - and
 * that half is what is closed here. The measurement is written up in
 * docs/refactoring/T3.25a.md.
 *
 * THE SIX WAYS A TEST OF THIS COULD LIE:
 *  1. a direct call: every case here goes through a real WebSocket session,
 *     because the whole question is whether the API boundary lets the value
 *     through; core/ShutterImpulse_test already covers set_value() directly
 *     and is deliberately left alone;
 *  2. a spelled name: the refusal is read from the transport's own
 *     {"success":...} field, not from a log line;
 *  3. a badly bounded length: the payload is not bounded by a length, it is
 *     rejected because it does not READ as an int - AnImpulseWhoseDigitsRun
 *     OutIntoLettersIsRefused is the case that tells the two apart;
 *  4. a false fixture: time="30" and impulse_time="35" are the real regime,
 *     not the time="0" of core/SetStateGarbage_test which exists precisely so
 *     that NO timer is ever armed. A fixture that arms nothing cannot see a
 *     full travel;
 *  5. presence instead of position: the published command state is compared
 *     whole, never searched for a substring;
 *  6. a haystack excluding what is sought: AWellFormedImpulseStillArmsIts
 *     OwnStop is green in both worlds and reddens any guard that refuses more
 *     than it should.
 *
 * ⭐ AND THE PERMUTATION THAT CHANGES NO VALUE: the two grammars are given
 * DIFFERENT durations (kUpMs / kDownMs, pairwise distinct and distinct from
 * impulse_time), so exchanging the up and the down branch of set_value()
 * cannot leave this file green.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include <chrono>
#include <functional>
#include <limits>

#include "IOFactory.h"
#include "ListeRoom.h"
#include "OutputShutter.h"
#include "OutputShutterSmart.h"
#include "libuvw.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/* ---- loop helpers (same shape as core/ShutterImpulse_test) ------------- */

int elapsedMs(const std::chrono::steady_clock::time_point &t0)
{
    return (int) std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - t0).count();
}

/* libuv arms every deadline from loop->time, its CACHED clock, refreshed only
 * at the top of uv_run(). An idle stretch therefore arms the next deadline in
 * the past by the length of that stretch, which is F-FLAKY-1. Take the origin
 * FIRST, call this, then command: uv__update_time() runs at the top of the
 * iteration, so loop->time >= origin holds by construction. */
void freshenLoopClock()
{
    uvw::Loop::getDefault()->run<uvw::Loop::Mode::NOWAIT>();
}

/* Answers the ms elapsed since t0 at the moment pred() was first SEEN to
 * hold, or -1 within the budget. A LOWER bound on an instant: time stolen
 * after t0 can only make the answer larger, so a busy host cannot falsify a
 * case that asserts "not before". */
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

/* ---- probes ------------------------------------------------------------ */

class PlainProbe: public OutputShutter
{
public:
    explicit PlainProbe(Params &p): OutputShutter(p) {}

    int relayPulses = 0;
    int impulseUpMs = -1;
    int impulseDownMs = -1;

    bool isStopped() { return check_condition_value("stop", true); }

protected:
    void setOutputUp(bool enable) override { if (enable) relayPulses++; }
    void setOutputDown(bool enable) override { if (enable) relayPulses++; }

    void ImpulseUp(int ms) override
    {
        impulseUpMs = ms;
        OutputShutter::ImpulseUp(ms);
    }

    void ImpulseDown(int ms) override
    {
        impulseDownMs = ms;
        OutputShutter::ImpulseDown(ms);
    }
};

class SmartProbe: public OutputShutterSmart
{
public:
    explicit SmartProbe(Params &p): OutputShutterSmart(p) {}

    int relayPulses = 0;
    int impulseUpMs = -1;
    int impulseDownMs = -1;

    bool isStopped() { return check_condition_value("stop", true); }

protected:
    void setOutputUp(bool enable) override { if (enable) relayPulses++; }
    void setOutputDown(bool enable) override { if (enable) relayPulses++; }

    void ImpulseUp(int ms) override
    {
        impulseUpMs = ms;
        OutputShutterSmart::ImpulseUp(ms);
    }

    void ImpulseDown(int ms) override
    {
        impulseDownMs = ms;
        OutputShutterSmart::ImpulseDown(ms);
    }
};

//IOFactory::RegisterClass is first-wins and has no unregister.
void registerProbes()
{
    static bool done = false;
    if (done) return;
    done = true;

    IOFactory::Instance().RegisterClass(
        "T325aPlain", [](Params &p) -> IOBase * { return new PlainProbe(p); });
    IOFactory::Instance().RegisterClass(
        "T325aSmart", [](Params &p) -> IOBase * { return new SmartProbe(p); });
}

/* The payload of the ticket. Twenty digits: it reads as a number all the way
 * to its end, so nothing about its SHAPE can be refused - only its range. */
const char *const kOutOfRange = "99999999999999999999";

/* Distinct, three digits, and none of them equal to impulse_time, so no
 * decimal writing can match by accident and exchanging the two grammars
 * cannot be silent. */
const int kUpMs = 283;
const int kDownMs = 147;
const int kImpulseTimeMs = 35;

/* Full travel of the fixture shutters, in seconds. Big enough that a well
 * formed impulse is far below it (so its stop IS armed) and that reaching it
 * is out of the question inside a case. */
const int kTravelSec = 30;

/* Budget for the contrast: comfortably above (kUpMs + impulse_time) so a stop
 * that really happens is seen, and far below the travel so a shutter left on
 * its full run cannot be seen stopped by waiting. */
const int kContrastBudgetMs = 1200;

}

class ImpulseOverflowTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        registerProbes();
        JsonApiCharacterizationTest::SetUp();

        //Config's IO state cache is process wide and never cleared, and both
        //shutter constructors read it back under "<id>_<type>". Ids are
        //per-case anyway; this is the belt.
        forgetIOState(plainId());
        forgetIOState(plainId() + "_b");
        forgetIOState(smartId());

        loadConfig();
    }

    std::string caseName() const
    {
        return ::testing::UnitTest::GetInstance()->current_test_info()->name();
    }
    std::string plainId() const { return "t325a_plain_" + caseName(); }
    std::string smartId() const { return "t325a_smart_" + caseName(); }

    PlainProbe *makePlain(const std::string &id)
    {
        Params p = {{ "type", "T325aPlain" },
                    { "id", id },
                    { "name", id },
                    { "time", Utils::to_string(kTravelSec) },
                    { "impulse_time", Utils::to_string(kImpulseTimeMs) },
                    { "enabled", "true" },
                    { "visible", "false" }};
        return dynamic_cast<PlainProbe *>(createIO(p));
    }

    SmartProbe *makeSmart(const std::string &id)
    {
        Params p = {{ "type", "T325aSmart" },
                    { "id", id },
                    { "name", id },
                    { "time", Utils::to_string(kTravelSec) },
                    { "time_up", Utils::to_string(kTravelSec) },
                    { "time_down", Utils::to_string(kTravelSec) },
                    { "impulse_time", Utils::to_string(kImpulseTimeMs) },
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
};

/*******************************************************************************
 * The refusal
 ******************************************************************************/

TEST_F(ImpulseOverflowTest, AnOverflowingImpulseUpIsRefusedAtTheApiBoundary)
{
    //RED BEFORE THE FIX: the server answers {"success":"true"} and the shutter
    //is handed INT_MAX.
    PlainProbe *sh = makePlain(plainId());
    ASSERT_NE(nullptr, sh);

    EXPECT_EQ("false", setState(plainId(),
                                std::string("impulse up ") + kOutOfRange))
            << "a duration that does not fit in an int must be refused, not "
               "saturated";
    EXPECT_EQ(-1, sh->impulseUpMs)
            << "the shutter was commanded with a duration nobody can act on";
    EXPECT_EQ(0, sh->relayPulses) << "no relay may have been pulled";
}

TEST_F(ImpulseOverflowTest, AnOverflowingImpulseDownIsRefusedAtTheApiBoundary)
{
    //RED BEFORE THE FIX. The down grammar is a separate branch with its own
    //erase(), so it needs its own case.
    PlainProbe *sh = makePlain(plainId());
    ASSERT_NE(nullptr, sh);

    EXPECT_EQ("false", setState(plainId(),
                                std::string("impulse down ") + kOutOfRange));
    EXPECT_EQ(-1, sh->impulseDownMs);
    EXPECT_EQ(0, sh->relayPulses);
}

TEST_F(ImpulseOverflowTest, TheSmartShutterRefusesTheSameTwoGrammars)
{
    //RED BEFORE THE FIX. OutputShutterSmart carries its own copy of both
    //branches; a guard written once in OutputShutter would leave this open.
    SmartProbe *up = makeSmart(smartId());
    ASSERT_NE(nullptr, up);

    EXPECT_EQ("false", setState(smartId(),
                                std::string("impulse up ") + kOutOfRange));
    EXPECT_EQ(-1, up->impulseUpMs);

    EXPECT_EQ("false", setState(smartId(),
                                std::string("impulse down ") + kOutOfRange));
    EXPECT_EQ(-1, up->impulseDownMs);
    EXPECT_EQ(0, up->relayPulses);
}

TEST_F(ImpulseOverflowTest, AnImpulseWhoseDigitsRunOutIntoLettersIsRefused)
{
    //RED BEFORE THE FIX, and it is what tells a RANGE check from a READ check:
    //"12abc" fits in an int, from_string() reads 12 and answers false, and the
    //12 is acted upon. The rule is "the whole argument reads as an int",
    //nothing narrower.
    PlainProbe *sh = makePlain(plainId());
    ASSERT_NE(nullptr, sh);

    EXPECT_EQ("false", setState(plainId(), "impulse up 12abc"));
    EXPECT_EQ(-1, sh->impulseUpMs)
            << "a partially readable duration was acted upon";
    EXPECT_EQ("", sh->get_command_string());
}

TEST_F(ImpulseOverflowTest, TheSaturatedDurationIsNeverPublishedAsTheState)
{
    //RED BEFORE THE FIX: ImpulseUp() ends with updateCache(), so
    //"impulse up 2147483647" is not only acted upon, it is written into the
    //state cache and served back to every client that asks for the IO.
    PlainProbe *sh = makePlain(plainId());
    ASSERT_NE(nullptr, sh);

    ASSERT_EQ("true", setState(plainId(),
                               "impulse up " + Utils::to_string(kUpMs)));
    ASSERT_EQ("impulse up " + Utils::to_string(kUpMs), sh->get_command_string());

    setState(plainId(), std::string("impulse up ") + kOutOfRange);

    EXPECT_EQ("impulse up " + Utils::to_string(kUpMs), sh->get_command_string())
            << "the refused command overwrote the published state with a "
               "saturated duration";

    Params cached;
    Config::Instance().ReadValueParams(plainId() + "_T325aPlain", cached);
    EXPECT_EQ("impulse up " + Utils::to_string(kUpMs), cached["cmd_state"])
            << "and the same saturated duration reached the state cache";
}

/*******************************************************************************
 * ⭐ The consequence: the shutter goes on its full travel
 ******************************************************************************/

TEST_F(ImpulseOverflowTest, AWellFormedImpulseStillArmsItsOwnStop)
{
    //GREEN BEFORE AND AFTER. The control, and the half that keeps the guard
    //honest: refusing more than it should would redden here first.
    PlainProbe *sh = makePlain(plainId());
    ASSERT_NE(nullptr, sh);

    const auto issued = std::chrono::steady_clock::now();
    freshenLoopClock();
    ASSERT_EQ("true", setState(plainId(),
                               "impulse up " + Utils::to_string(kUpMs)));
    ASSERT_FALSE(sh->isStopped()) << "the shutter never started moving";

    const int stoppedAt = pumpUntilSince(issued, [&]() { return sh->isStopped(); },
                                         kContrastBudgetMs);
    EXPECT_GE(stoppedAt, 0)
            << "a well formed impulse never stopped the shutter";
    EXPECT_GE(stoppedAt, (kImpulseTimeMs + kUpMs) / 2)
            << "the shutter stopped after the bare relay pulse instead of the "
               "duration that was asked for";
}

TEST_F(ImpulseOverflowTest, AnOverflowingImpulseSendsNoShutterOnItsFullTravel)
{
    //⭐ RED BEFORE THE FIX, and this is the case the ticket exists for. Two
    //shutters, one budget, one loop: the first proves the budget really can
    //see a stop, the second is the one under test. Without the first leg,
    //"was not seen stopped" would also be the answer of a loop nobody pumps.
    PlainProbe *control = makePlain(plainId());
    PlainProbe *victim = makePlain(plainId() + "_b");
    ASSERT_NE(nullptr, control);
    ASSERT_NE(nullptr, victim);

    const auto issued = std::chrono::steady_clock::now();
    freshenLoopClock();
    ASSERT_EQ("true", setState(plainId(),
                               "impulse up " + Utils::to_string(kUpMs)));
    setState(plainId() + "_b", std::string("impulse up ") + kOutOfRange);

    ASSERT_GE(pumpUntilSince(issued, [&]() { return control->isStopped(); },
                             kContrastBudgetMs), 0)
            << "the control shutter was never seen stopped: this budget cannot "
               "observe a stop at all, so the case below would prove nothing";

    EXPECT_EQ(0, victim->relayPulses)
            << "the out of range impulse pulled a relay: the shutter left on a "
               "travel nothing is going to stop before its "
            << kTravelSec << " s end";
    EXPECT_GE(pumpUntilSince(issued, [&]() { return victim->isStopped(); },
                             kContrastBudgetMs), 0)
            << "the shutter is still travelling: an out of range duration arms "
               "no stop at all, so the impulse became a full open";
}

TEST_F(ImpulseOverflowTest, ARefusedImpulseLeavesTheShutterWhereItWas)
{
    //RED BEFORE THE FIX. The other half of the same consequence: a shutter
    //that was already moving to a chosen position must not be restarted on a
    //full travel by a command the server is going to refuse.
    PlainProbe *sh = makePlain(plainId());
    ASSERT_NE(nullptr, sh);

    ASSERT_EQ("true", setState(plainId(),
                               "impulse down " + Utils::to_string(kDownMs)));
    ASSERT_EQ(kDownMs, sh->impulseDownMs);
    const int pulsesAfterTheRealCommand = sh->relayPulses;

    EXPECT_EQ("false", setState(plainId(),
                                std::string("impulse down ") + kOutOfRange));
    EXPECT_EQ(kDownMs, sh->impulseDownMs)
            << "the refused command reached ImpulseDown() anyway";
    EXPECT_EQ(pulsesAfterTheRealCommand, sh->relayPulses)
            << "the refused command pulled a relay";
}

/*******************************************************************************
 * What the guard must not cost
 ******************************************************************************/

TEST_F(ImpulseOverflowTest, TheLargestDurationThatFitsIsStillAccepted)
{
    //GREEN BEFORE AND AFTER. The boundary of the rule, stated rather than
    //discovered: INT_MAX written out reads as an int and stays a legal
    //request. Only what does not READ is refused.
    PlainProbe *sh = makePlain(plainId());
    ASSERT_NE(nullptr, sh);

    const std::string biggest =
            Utils::to_string(std::numeric_limits<int>::max());

    EXPECT_EQ("true", setState(plainId(), "impulse up " + biggest));
    EXPECT_EQ(std::numeric_limits<int>::max(), sh->impulseUpMs);
    EXPECT_EQ("impulse up " + biggest, sh->get_command_string());
}

TEST_F(ImpulseOverflowTest, TheOtherShutterCommandsAreUntouched)
{
    //GREEN BEFORE AND AFTER. up / down / stop / toggle / set_state carry no
    //number and must keep working; a guard written above the grammar
    //dispatch instead of inside the two impulse branches would redden here.
    PlainProbe *sh = makePlain(plainId());
    ASSERT_NE(nullptr, sh);

    EXPECT_EQ("true", setState(plainId(), "up"));
    EXPECT_EQ("true", setState(plainId(), "stop"));
    EXPECT_EQ("true", setState(plainId(), "down"));
    EXPECT_EQ("true", setState(plainId(), "stop"));
    EXPECT_EQ("true", setState(plainId(), "toggle"));
    EXPECT_EQ("true", setState(plainId(), "set_state up"));
    EXPECT_EQ("false", setState(plainId(), "no such command"));
}
