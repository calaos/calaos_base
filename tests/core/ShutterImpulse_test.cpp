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
 * T3.34 - "impulse down <ms>" must carry the duration the client asked for.
 *
 * Both shutter IOs recognise the prefix with its real length and then drop
 * the WRONG number of characters:
 *
 *     val.compare(0, 13, "impulse down ")   //13 == strlen("impulse down ")
 *     val.erase(0, 11);                     //11 == strlen("impulse up ")
 *
 * so "impulse down 147" reaches Utils::from_string() as "n 147", the
 * extraction fails and (C++11 num_get) writes 0 into the destination:
 * ImpulseDown(0) is called whatever the client asked for.
 *
 * What is pinned here is the OBSERVABLE consequence, never the literal:
 *
 *  1. the duration actually handed to ImpulseDown()/ImpulseUp() - the probe
 *     subclass overrides the two protected virtuals and records the argument
 *     (asserting the *value*, not the fact of the call: the call happens in
 *     both worlds, it is the argument that is wrong);
 *  2. the duration the IO publishes to the API afterwards
 *     (get_command_string() == "impulse down 147"); today it publishes
 *     "impulse down 0", which is the trace of the defect;
 *  3. the stop timer the IO really arms: the shutter must still be moving
 *     well after the impulse_time of the hardware and stop only around the
 *     requested duration. This is the part a user feels - see the timing
 *     comment below.
 *
 * Timing oracle, and why it is the interesting one
 * ------------------------------------------------
 * Down()/Up() arm a one shot stop at (impulse_action_time + impulse_time)
 * ms. With the defect impulse_action_time is 0, so the shutter stops after
 * impulse_time alone - the bare relay pulse of the hardware, a fraction of
 * a second - instead of the requested travel. The test drives the real uvw
 * loop, MEASURES the instant at which the shutter is first seen stopped, and
 * requires that instant to be no earlier than a probe placed halfway between
 * the two candidate deadlines. Every wait is wall clock bounded (same
 * discipline as core/Timer_test) so a regression fails the test instead of
 * hanging make check.
 *
 * ⭐ T3.49: why a MEASURED INSTANT and not "is it stopped at probe ms?".
 * The original form pumped the loop for the probe duration and asserted
 * EXPECT_FALSE(isStopped()). That is an upper bound on an observation, and
 * it went red on correct code six times on loaded hosts. The three things
 * that make the present form immune, each closing a different half:
 *
 *   1. freshenLoopClock() between the origin and the command. libuv arms a
 *      deadline from loop->time, its CACHED clock, refreshed only at the top
 *      of uv_run(). An idle gap - fixture teardown, loadConfig(), a host too
 *      busy to schedule us - therefore arms the stop IN THE PAST by the
 *      length of that gap. Measured with a standalone libuv probe in this
 *      image: a 182 ms one-shot armed after a 120 ms idle gap fires 62 ms
 *      later, inside a 91 ms probe window. THIS is what reddened :297 and
 *      :384, and PlainImpulseDownKeepsMovingAfterAnIdleLoopGap pins it.
 *   2. the origin taken BEFORE that refresh, not between it and the command.
 *      This is what makes 1. an INEQUALITY rather than a small number:
 *      uv__update_time() runs at the TOP of the NOWAIT iteration, so an
 *      origin taken after freshenLoopClock() RETURNS sits later than
 *      loop->time by the whole cost of that iteration - a residual gap of
 *      exactly the same nature as the one being closed. Measured: a NOWAIT
 *      iteration made to cost 95 ms answers 87 for a 182 ms deadline, under
 *      a 91 ms probe, i.e. red. With the origin taken first,
 *      loop->time >= origin holds by construction, hence
 *      deadline >= origin + delay whatever the iteration costs, and the same
 *      probe answers 182. It also puts the arm-to-pump gap inside the
 *      measurement instead of subtracting it.
 *   3. a LOWER bound on the answer. Time stolen after that origin can only
 *      make the answer larger, so no amount of contention can falsify it -
 *      whereas the same probe with a fresh clock and 40 ms of theft burned
 *      in EVERY loop iteration never fires early, which is the measurement
 *      that removes "the scheduler steals time from the pump" from the list
 *      of possible causes.
 *
 * The probe itself is unchanged, and that is the point: the discrimination
 * T3.34 needs - "stops after impulse_time" against "stops after the
 * requested duration" - is exactly as sharp as it was. Widening the margin
 * would have blunted it.
 *
 * Durations
 * ---------
 * All four are non zero (0 is the broken value: a case asking for 0 would be
 * green both ways), pairwise distinct, distinct from the impulse_time of
 * their fixture, and three digits long so that no decimal writing can
 * accidentally match an offset (11 or 13).
 *
 *      OutputShutter        down 147   up 283    impulse_time 35
 *      OutputShutterSmart   down 319   up 421    impulse_time 41
 *
 * The two IOs are covered by two separate fixtures with separate ids and
 * separate durations: a mutation in OutputShutter.cpp must not be able to
 * redden a single OutputShutterSmart case, and the other way round.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <limits>
#include <string>

#include <unistd.h>

#include "CalaosCoreFixture.h"
#include "OutputShutter.h"
#include "OutputShutterSmart.h"
#include "libuvw.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/* ---- loop helpers (same shape as core/Timer_test) --------------------- */

/* Pump the default loop until pred() holds, with a wall clock deadline. */
bool runLoopUntil(const std::function<bool()> &pred, int timeoutMs = 5000)
{
    auto loop = uvw::Loop::getDefault();
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);

    while (!pred())
    {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        loop->run<uvw::Loop::Mode::NOWAIT>();
    }

    return true;
}

int elapsedMs(const std::chrono::steady_clock::time_point &t0)
{
    return (int) std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - t0).count();
}

/* ⭐ Put the loop clock back on the wall clock. See kIdleGapMs below: libuv
 * computes every timer deadline from loop->time, which only advances when the
 * loop runs, so anything armed after an idle stretch is armed in the past by
 * the length of that stretch. One NOWAIT iteration calls uv__update_time()
 * and costs nothing in the usual case.
 *
 * ⚠️ ORDER MATTERS, and it is the whole of what this call buys. Take the
 * measurement origin FIRST, then call this, then arm. uv__update_time() runs
 * at the TOP of the iteration, so an origin taken after this RETURNS is later
 * than loop->time by the cost of the iteration, and the deadline armed from
 * loop->time then lands that much BEFORE origin + delay - the residual gap
 * this call was supposed to close. With the origin taken first the invariant
 * is loop->time >= origin, hence deadline >= origin + delay whatever the
 * iteration costs and whatever the host does in between. That is the property
 * the EXPECT_GE lower bounds of this file rest on. */
void freshenLoopClock()
{
    uvw::Loop::getDefault()->run<uvw::Loop::Mode::NOWAIT>();
}

/* Pump the default loop for a fixed wall clock duration.
 *
 * No iteration cap: `elapsedMs(t0) < ms` alone terminates this loop, and a
 * safety bound that can become the ACTIVE constraint is exactly how T3.40
 * produced false green n.13 (a 200000 iteration cap an idle NOWAIT loop
 * reaches in 80 ms, turning every 650 ms wait into an 80 ms one). No
 * `iterations < ms` floor either: t0 is taken on entry, so the condition
 * holds on entry and the loop always runs at least once, whereas an ms-sized
 * floor would multiply the cost of every wait on a host whose loop iterations
 * are slow - the same failure on the cost axis. */
void pumpLoopFor(int ms)
{
    auto loop = uvw::Loop::getDefault();
    const auto t0 = std::chrono::steady_clock::now();
    int iterations = 0;

    while (elapsedMs(t0) < ms)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        iterations++;
    }

    /* ⚠️ This is a check on the ARGUMENT, not a post-condition on the wait,
     * and selling it as the latter would overstate it: t0 is taken on entry,
     * so for any ms >= 1 the condition holds on entry, the body runs at least
     * once, and the loop exits only once elapsed >= ms. The last two clauses
     * are therefore unreachable from every call site that exists. What IS
     * reachable is a degenerate duration: pumpLoopFor(0) is a no-op wearing
     * the shape of a wait, which is the family false green n.13 belongs to
     * (T3.40, FINDINGS) - a bound that silently turns a wait into nothing.
     * Nothing passes 0 today; this refuses the caller that would. */
    if (ms < 1 || iterations < 1 || elapsedMs(t0) < ms)
        ADD_FAILURE() << "pumpLoopFor(" << ms << ") gave up after "
                      << elapsedMs(t0) << " ms and " << iterations
                      << " iterations: the deadline under test never expired, "
                         "so this case proved nothing";
}

/* ⭐ Pump the default loop until pred() holds; answers the ms elapsed SINCE
 * t0 at the moment pred() was first SEEN to hold, or -1 if it never was
 * within the budget.
 *
 * This is the shape T3.40 arrived at, and the reason it is used here instead
 * of `pumpLoopFor(probe); EXPECT_FALSE(pred())`: an oracle written this way
 * cannot fail because the machine was slow. Time stolen anywhere after t0 -
 * before the command, between the command and the pump, or inside the pump -
 * can only make the answer LARGER, and the case asserts a LOWER bound on it.
 * "The shutter was not observed stopped before probe ms" is a property a busy
 * host cannot falsify; "the shutter is not stopped at probe ms" is a race.
 *
 * ⚠️ A lower bound alone is NOT enough, and that is measured, not argued: with
 * a stale loop clock the deadline itself moves into the past, which makes the
 * answer SMALLER. freshenLoopClock() is what closes that half - but ONLY if
 * t0 is taken BEFORE it, since uv__update_time() runs at the top of that
 * iteration and a t0 taken after it returns is late by the iteration's own
 * cost. Ordered that way the invariant is loop->time >= t0, hence
 * deadline >= t0 + delay by construction. The three go together; any one of
 * them alone leaves the race open.
 *
 * ⚠️ A lower bound says nothing about stopping too LATE. That is exactly what
 * these cases need - the defect T3.34 closed is a shutter that stops after
 * the bare relay pulse - and the budget keeps the other end: never stopping
 * at all answers -1, which the cases reject. */
int pumpUntilSince(const std::chrono::steady_clock::time_point &t0,
                   const std::function<bool()> &pred, int timeoutMs = 5000)
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

/* Timer handles still armed on the default loop, closing/collected ones
 * filtered out (same helper as core/Timer_test and core/KnxIo_test). A
 * one-shot that libuv can never fire shows up here for ever. */
int armedTimerCount()
{
    int count = 0;

    uvw::Loop::getDefault()->walk([&count](uvw::BaseHandle &handle)
    {
        if (handle.type() == uvw::HandleType::TIMER &&
            handle.active() && !handle.closing())
            count++;
    });

    return count;
}

/* ---- probes ----------------------------------------------------------- */

/* OutputShutter with the two impulse entry points instrumented and the
 * relays stubbed out. ImpulseUp/ImpulseDown are protected virtuals of the
 * production class, so the probe records the argument the production
 * set_value() computed and then runs the real implementation. */
class PlainShutterProbe: public OutputShutter
{
public:
    explicit PlainShutterProbe(Params &p): OutputShutter(p) {}

    int impulseDownMs = -1;
    int impulseUpMs = -1;
    int impulseDownCalls = 0;
    int impulseUpCalls = 0;
    bool relayUp = false;
    bool relayDown = false;

    bool isStopped() { return check_condition_value("stop", true); }

protected:
    void setOutputUp(bool enable) override { relayUp = enable; }
    void setOutputDown(bool enable) override { relayDown = enable; }

    void ImpulseUp(int ms) override
    {
        impulseUpCalls++;
        impulseUpMs = ms;
        OutputShutter::ImpulseUp(ms);
    }

    void ImpulseDown(int ms) override
    {
        impulseDownCalls++;
        impulseDownMs = ms;
        OutputShutter::ImpulseDown(ms);
    }
};

class SmartShutterProbe: public OutputShutterSmart
{
public:
    explicit SmartShutterProbe(Params &p): OutputShutterSmart(p) {}

    int impulseDownMs = -1;
    int impulseUpMs = -1;
    int impulseDownCalls = 0;
    int impulseUpCalls = 0;
    bool relayUp = false;
    bool relayDown = false;

    bool isStopped() { return check_condition_value("stop", true); }

protected:
    void setOutputUp(bool enable) override { relayUp = enable; }
    void setOutputDown(bool enable) override { relayDown = enable; }

    void ImpulseUp(int ms) override
    {
        impulseUpCalls++;
        impulseUpMs = ms;
        OutputShutterSmart::ImpulseUp(ms);
    }

    void ImpulseDown(int ms) override
    {
        impulseDownCalls++;
        impulseDownMs = ms;
        OutputShutterSmart::ImpulseDown(ms);
    }
};

/* Durations, see file header. */
const int kPlainDownMs = 147;
const int kPlainUpMs = 283;
const int kPlainImpulseTimeMs = 35;

const int kSmartDownMs = 319;
const int kSmartUpMs = 421;
const int kSmartImpulseTimeMs = 41;

/* ⭐ F-FLAKY-1 (T3.49), made deterministic.
 *
 * libuv does NOT read the clock when a timer is armed: uv_timer_start()
 * computes its deadline from loop->time, the loop's CACHED clock, which is
 * only refreshed by uv__update_time() at the top of each uv_run(). So a
 * stretch of wall clock during which nobody pumps the loop - a fixture
 * teardown, loadConfig(), constructing the probe, or simply a host so busy
 * that this process does not get scheduled - arms every deadline of the case
 * that follows IN THE PAST, by exactly the length of that gap.
 *
 * That is why this suite went red on correct code, and it is measured, not
 * supposed: a standalone libuv probe in this image arms a 182 ms one-shot
 * after a 120 ms idle gap and sees it fire 62 ms later, i.e. well inside a
 * 91 ms probe window. The same probe with a fresh loop clock never fires
 * early, not even with 40 ms of deliberate theft burned inside EVERY loop
 * iteration.
 *
 * kIdleGapMs is chosen strictly between the margin of the probe below (91 ms)
 * and the deadline it watches (182 ms): that is exactly the regime that
 * turned :297 and :384 red on a loaded host. */
const int kIdleGapMs = 120;

/* A budget for the out of range case, not a wait: nothing is expected to
 * happen inside it, and the shutter's own 30 s travel is what stops it. */
const int kOutOfRangeBudgetMs = 200;

/* The instant at which a broken build has already stopped the shutter and a
 * correct one has not: strictly above impulse_time, strictly below the
 * requested duration. */
int stillMovingProbeMs(int requestedMs, int impulseTimeMs)
{
    return (impulseTimeMs + requestedMs) / 2;
}

}

/******************************************************************************
 * OutputShutter (IO/OutputShutter.cpp)
 ******************************************************************************/

class ShutterImpulseTest: public CoreFixture
{
protected:
    Params plainParams(const std::string &id, int timeSec = 30,
                       bool withImpulseTime = true)
    {
        Params p = {
            { "type", "OutputShutter" },
            { "id", id },
            { "name", id },
            { "time", Utils::to_string(timeSec) },
            { "enabled", "true" },
            { "visible", "false" },
        };
        if (withImpulseTime)
            p.Add("impulse_time", Utils::to_string(kPlainImpulseTimeMs));
        return p;
    }
};

//THE defect: the duration the client asked for must reach ImpulseDown().
TEST_F(ShutterImpulseTest, PlainImpulseDownReceivesTheRequestedDuration)
{
    loadConfig();

    Params p = plainParams("t334_plain_down_arg");
    PlainShutterProbe sh(p);

    ASSERT_TRUE(sh.set_value("impulse down " + Utils::to_string(kPlainDownMs)));

    ASSERT_EQ(sh.impulseDownCalls, 1);
    EXPECT_EQ(sh.impulseDownMs, kPlainDownMs)
        << "the requested duration did not survive the prefix removal";
}

//The API visible trace of the same defect.
TEST_F(ShutterImpulseTest, PlainImpulseDownPublishesTheRequestedDuration)
{
    loadConfig();

    Params p = plainParams("t334_plain_down_state");
    PlainShutterProbe sh(p);

    ASSERT_TRUE(sh.set_value("impulse down " + Utils::to_string(kPlainDownMs)));

    EXPECT_EQ(sh.get_command_string(),
              "impulse down " + Utils::to_string(kPlainDownMs));
}

//What the user feels: the shutter must keep moving past the bare relay
//pulse and stop around the requested duration.
TEST_F(ShutterImpulseTest, PlainImpulseDownKeepsMovingUntilTheRequestedDuration)
{
    loadConfig();

    Params p = plainParams("t334_plain_down_timer");
    PlainShutterProbe sh(p);

    const auto issued = std::chrono::steady_clock::now();
    freshenLoopClock();
    ASSERT_TRUE(sh.set_value("impulse down " + Utils::to_string(kPlainDownMs)));
    ASSERT_FALSE(sh.isStopped()) << "shutter never started moving";

    const int stoppedAt = pumpUntilSince(issued, [&]() { return sh.isStopped(); });

    ASSERT_GE(stoppedAt, 0) << "the shutter never stopped";
    EXPECT_GE(stoppedAt, stillMovingProbeMs(kPlainDownMs, kPlainImpulseTimeMs))
        << "the shutter was seen stopped " << stoppedAt << " ms after the "
           "command: it stopped after impulse_time instead of the requested "
           "duration";
}

//The same oracle, after the loop has been left idle for longer than the
//probe margin. Nothing about the shutter changes here: only the loop clock
//is stale, which is the whole of F-FLAKY-1 (see kIdleGapMs above). A case
//that measures a duration must survive this, or every red of this suite is
//going to be blamed on the weather.
TEST_F(ShutterImpulseTest, PlainImpulseDownKeepsMovingAfterAnIdleLoopGap)
{
    loadConfig();

    Params p = plainParams("t349_plain_gap");
    PlainShutterProbe sh(p);

    //Put the loop clock back on the wall clock, then let it go stale by a
    //known amount, so that the gap under test is kIdleGapMs and not whatever
    //the host happened to steal.
    uvw::Loop::getDefault()->run<uvw::Loop::Mode::NOWAIT>();
    ::usleep(kIdleGapMs * 1000);

    const auto issued = std::chrono::steady_clock::now();
    freshenLoopClock();
    ASSERT_TRUE(sh.set_value("impulse down " + Utils::to_string(kPlainDownMs)));
    ASSERT_FALSE(sh.isStopped()) << "shutter never started moving";

    const int stoppedAt = pumpUntilSince(issued, [&]() { return sh.isStopped(); });

    ASSERT_GE(stoppedAt, 0) << "the shutter never stopped";
    EXPECT_GE(stoppedAt, stillMovingProbeMs(kPlainDownMs, kPlainImpulseTimeMs))
        << "the shutter was seen stopped " << stoppedAt << " ms after the "
           "command, while the loop clock was " << kIdleGapMs << " ms stale: "
           "an idle gap must not be able to redden a duration oracle";
}

//The branch that already worked must not regress.
TEST_F(ShutterImpulseTest, PlainImpulseUpReceivesTheRequestedDuration)
{
    loadConfig();

    Params p = plainParams("t334_plain_up_arg");
    PlainShutterProbe sh(p);

    ASSERT_TRUE(sh.set_value("impulse up " + Utils::to_string(kPlainUpMs)));

    ASSERT_EQ(sh.impulseUpCalls, 1);
    EXPECT_EQ(sh.impulseUpMs, kPlainUpMs);
}

TEST_F(ShutterImpulseTest, PlainImpulseUpPublishesTheRequestedDuration)
{
    loadConfig();

    Params p = plainParams("t334_plain_up_state");
    PlainShutterProbe sh(p);

    ASSERT_TRUE(sh.set_value("impulse up " + Utils::to_string(kPlainUpMs)));

    EXPECT_EQ(sh.get_command_string(),
              "impulse up " + Utils::to_string(kPlainUpMs));
}

TEST_F(ShutterImpulseTest, PlainImpulseUpKeepsMovingUntilTheRequestedDuration)
{
    loadConfig();

    Params p = plainParams("t334_plain_up_timer");
    PlainShutterProbe sh(p);

    const auto issued = std::chrono::steady_clock::now();
    freshenLoopClock();
    ASSERT_TRUE(sh.set_value("impulse up " + Utils::to_string(kPlainUpMs)));
    ASSERT_FALSE(sh.isStopped()) << "shutter never started moving";

    const int stoppedAt = pumpUntilSince(issued, [&]() { return sh.isStopped(); });

    ASSERT_GE(stoppedAt, 0) << "the shutter never stopped";
    EXPECT_GE(stoppedAt, stillMovingProbeMs(kPlainUpMs, kPlainImpulseTimeMs))
        << "the shutter was seen stopped " << stoppedAt << " ms after the "
           "command: it stopped after impulse_time instead of the requested "
           "duration";
}

//The carrier case: same duration asked both ways, same duration used.
TEST_F(ShutterImpulseTest, PlainImpulseUpAndDownAgreeOnTheSameDuration)
{
    loadConfig();

    const std::string ms = Utils::to_string(kPlainUpMs);

    Params pu = plainParams("t334_plain_agree_up");
    PlainShutterProbe up(pu);
    ASSERT_TRUE(up.set_value("impulse up " + ms));

    Params pd = plainParams("t334_plain_agree_down");
    PlainShutterProbe down(pd);
    ASSERT_TRUE(down.set_value("impulse down " + ms));

    EXPECT_EQ(down.impulseDownMs, up.impulseUpMs)
        << "up and down disagree on the same requested duration";
    EXPECT_EQ(up.impulseUpMs, kPlainUpMs);
    EXPECT_EQ(down.impulseDownMs, kPlainUpMs);
}

//Shutter with no impulse_time parameter at all (impulse_time == -1, the
//default of a plain relay shutter): the stop deadline is
//(requested - 1) ms and must still be honoured.
TEST_F(ShutterImpulseTest, PlainImpulseDownWithoutImpulseTimeStillHonoursTheDuration)
{
    loadConfig();

    Params p = plainParams("t334_plain_noimp", 30, false);
    PlainShutterProbe sh(p);

    const auto issued = std::chrono::steady_clock::now();
    freshenLoopClock();
    ASSERT_TRUE(sh.set_value("impulse down " + Utils::to_string(kPlainDownMs)));
    EXPECT_EQ(sh.impulseDownMs, kPlainDownMs);
    ASSERT_FALSE(sh.isStopped()) << "shutter never started moving";

    const int stoppedAt = pumpUntilSince(issued, [&]() { return sh.isStopped(); });

    ASSERT_GE(stoppedAt, 0) << "the shutter never stopped";
    EXPECT_GE(stoppedAt, stillMovingProbeMs(kPlainDownMs, 0))
        << "the shutter was seen stopped " << stoppedAt << " ms after the "
           "command: it stopped after impulse_time instead of the requested "
           "duration";
}

/******************************************************************************
 * OutputShutterSmart (IO/OutputShutterSmart.cpp)
 ******************************************************************************/

class SmartShutterImpulseTest: public CoreFixture
{
protected:
    Params smartParams(const std::string &id, int timeSec = 30)
    {
        return Params {
            { "type", "OutputShutterSmart" },
            { "id", id },
            { "name", id },
            { "time", Utils::to_string(timeSec) },
            { "time_up", Utils::to_string(timeSec) },
            { "time_down", Utils::to_string(timeSec) },
            { "impulse_time", Utils::to_string(kSmartImpulseTimeMs) },
            { "enabled", "true" },
            { "visible", "false" },
        };
    }
};

TEST_F(SmartShutterImpulseTest, SmartImpulseDownReceivesTheRequestedDuration)
{
    loadConfig();

    Params p = smartParams("t334_smart_down_arg");
    SmartShutterProbe sh(p);

    ASSERT_TRUE(sh.set_value("impulse down " + Utils::to_string(kSmartDownMs)));

    ASSERT_EQ(sh.impulseDownCalls, 1);
    EXPECT_EQ(sh.impulseDownMs, kSmartDownMs)
        << "the requested duration did not survive the prefix removal";
}

TEST_F(SmartShutterImpulseTest, SmartImpulseDownPublishesTheRequestedDuration)
{
    loadConfig();

    Params p = smartParams("t334_smart_down_state");
    SmartShutterProbe sh(p);

    ASSERT_TRUE(sh.set_value("impulse down " + Utils::to_string(kSmartDownMs)));

    EXPECT_EQ(sh.get_command_string(),
              "impulse down " + Utils::to_string(kSmartDownMs));
}

TEST_F(SmartShutterImpulseTest, SmartImpulseDownKeepsMovingUntilTheRequestedDuration)
{
    loadConfig();

    Params p = smartParams("t334_smart_down_timer");
    SmartShutterProbe sh(p);

    const auto issued = std::chrono::steady_clock::now();
    freshenLoopClock();
    ASSERT_TRUE(sh.set_value("impulse down " + Utils::to_string(kSmartDownMs)));
    ASSERT_FALSE(sh.isStopped()) << "shutter never started moving";

    const int stoppedAt = pumpUntilSince(issued, [&]() { return sh.isStopped(); });

    ASSERT_GE(stoppedAt, 0) << "the shutter never stopped";
    EXPECT_GE(stoppedAt, stillMovingProbeMs(kSmartDownMs, kSmartImpulseTimeMs))
        << "the shutter was seen stopped " << stoppedAt << " ms after the "
           "command: it stopped after impulse_time instead of the requested "
           "duration";
}

TEST_F(SmartShutterImpulseTest, SmartImpulseUpReceivesTheRequestedDuration)
{
    loadConfig();

    Params p = smartParams("t334_smart_up_arg");
    SmartShutterProbe sh(p);

    ASSERT_TRUE(sh.set_value("impulse up " + Utils::to_string(kSmartUpMs)));

    ASSERT_EQ(sh.impulseUpCalls, 1);
    EXPECT_EQ(sh.impulseUpMs, kSmartUpMs);
}

TEST_F(SmartShutterImpulseTest, SmartImpulseUpPublishesTheRequestedDuration)
{
    loadConfig();

    Params p = smartParams("t334_smart_up_state");
    SmartShutterProbe sh(p);

    ASSERT_TRUE(sh.set_value("impulse up " + Utils::to_string(kSmartUpMs)));

    EXPECT_EQ(sh.get_command_string(),
              "impulse up " + Utils::to_string(kSmartUpMs));
}

//The smart shutter only moves up when it is not already fully open, so the
//position is primed first (set_state 100 == fully closed).
TEST_F(SmartShutterImpulseTest, SmartImpulseUpKeepsMovingUntilTheRequestedDuration)
{
    loadConfig();

    Params p = smartParams("t334_smart_up_timer");
    SmartShutterProbe sh(p);

    ASSERT_TRUE(sh.set_value("set_state 100"));

    const auto issued = std::chrono::steady_clock::now();
    freshenLoopClock();
    ASSERT_TRUE(sh.set_value("impulse up " + Utils::to_string(kSmartUpMs)));
    ASSERT_FALSE(sh.isStopped()) << "shutter never started moving";

    const int stoppedAt = pumpUntilSince(issued, [&]() { return sh.isStopped(); });

    ASSERT_GE(stoppedAt, 0) << "the shutter never stopped";
    EXPECT_GE(stoppedAt, stillMovingProbeMs(kSmartUpMs, kSmartImpulseTimeMs))
        << "the shutter was seen stopped " << stoppedAt << " ms after the "
           "command: it stopped after impulse_time instead of the requested "
           "duration";
}

TEST_F(SmartShutterImpulseTest, SmartImpulseUpAndDownAgreeOnTheSameDuration)
{
    loadConfig();

    const std::string ms = Utils::to_string(kSmartUpMs);

    Params pu = smartParams("t334_smart_agree_up");
    SmartShutterProbe up(pu);
    ASSERT_TRUE(up.set_value("impulse up " + ms));

    Params pd = smartParams("t334_smart_agree_down");
    SmartShutterProbe down(pd);
    ASSERT_TRUE(down.set_value("impulse down " + ms));

    EXPECT_EQ(down.impulseDownMs, up.impulseUpMs)
        << "up and down disagree on the same requested duration";
    EXPECT_EQ(up.impulseUpMs, kSmartUpMs);
    EXPECT_EQ(down.impulseDownMs, kSmartUpMs);
}

/******************************************************************************
 * Lifetime: the impulse stop must not outlive the IO
 *
 * Down()/Up() park a stop callback bound to `this` on the event loop. IOBase
 * does not derive from sigc::trackable, so nothing disconnects that binding,
 * and the destructor of the shutter does not cancel it either: an IO deleted
 * before the deadline (ListeRoom::deleteIO(), reachable from the JSON API,
 * or the teardown of a Room) used to leave a dangling `this` armed on the
 * loop. Fixing the duration makes the window as long as the duration the
 * client asked for instead of the bare impulse_time, which is exactly why
 * the guard is part of this ticket.
 *
 * Without a sanitizer the corruption is silent, so what is asserted here is
 * the observable substitute: after the IO is gone, pumping the loop past the
 * deadline must not run the shutter code at all (the probe counts it).
 ******************************************************************************/

namespace
{

class StopCountingShutterProbe: public PlainShutterProbe
{
public:
    explicit StopCountingShutterProbe(Params &p): PlainShutterProbe(p) {}

    static int stopCalls;

protected:
    void Stop() override
    {
        stopCalls++;
        PlainShutterProbe::Stop();
    }
};

int StopCountingShutterProbe::stopCalls = 0;

}

//Own fixture, declared last, so that this case runs after every other one:
//before the guard it does not fail, it KILLS the binary (the stop callback
//dereferences a freed shutter), and gtest would never reach the suites
//declared after it.
class ShutterImpulseLifetimeTest: public ShutterImpulseTest
{
};

TEST_F(ShutterImpulseLifetimeTest, PlainImpulseStopDoesNotOutliveTheDeletedIo)
{
    loadConfig();

    Params p = plainParams("t334_plain_lifetime");
    StopCountingShutterProbe *sh = new StopCountingShutterProbe(p);

    ASSERT_TRUE(sh->set_value("impulse down " + Utils::to_string(kPlainDownMs)));
    ASSERT_FALSE(sh->isStopped());

    StopCountingShutterProbe::stopCalls = 0;
    delete sh;

    //Well past (impulse_time + requested duration)
    pumpLoopFor(kPlainImpulseTimeMs + kPlainDownMs + 400);

    EXPECT_EQ(StopCountingShutterProbe::stopCalls, 0)
        << "the impulse stop callback ran on a destroyed shutter";
}


/******************************************************************************
 * Deadline arithmetic: what the client asks for reaches an int addition
 *
 * (impulse_action_time + impulse_time) is computed in int and handed to
 * Timer::singleShot() divided by 1000. Both ends of the range are reachable
 * from the API, and the shutter used to be shielded from them only because
 * "impulse down" always collapsed to 0:
 *
 *  - impulse_time is -1 when the shutter has no impulse_time parameter (an
 *    ordinary relay shutter), so a zero or negative duration makes the sum
 *    negative. Measured in this image (libuv 1.44.2): singleShot(-0.001)
 *    passes static_cast<uint64_t>(-1.0) == 18446744073709551615 to
 *    uv_timer_start, which clamps the overflowing deadline to (uint64_t)-1.
 *    The timer NEVER fires and stays armed: the shutter runs its full
 *    course, and one libuv handle holding the IO leaks per command.
 *  - Utils::from_string saturates an out-of-range value to INT_MAX, and the
 *    sum used to be computed in int - undefined behaviour, wrapping
 *    negative, which landed on the same never-firing armed handle. The sum
 *    is a double since this ticket; T3.25a then made the IO refuse the
 *    out-of-range spelling outright, so the largest duration that still
 *    READS as an int is what exercises the top of the range from here.
 *
 * The oracle is the loop itself: once the IO is gone and the close callbacks
 * have run, the number of armed timer handles must be back where it started.
 ******************************************************************************/

TEST_F(ShutterImpulseLifetimeTest, PlainZeroLengthImpulseLeavesNoTimerArmedForEver)
{
    loadConfig();

    //Drain first: an impulse stop parked by an earlier case is armed for up
    //to (duration + impulse_time) and would otherwise fire, close, and
    //cancel out the leak this case is looking for.
    pumpLoopFor(800);
    const int before = armedTimerCount();

    {
        //No impulse_time parameter at all: impulse_time == -1
        Params p = plainParams("t334_plain_zero", 30, false);
        PlainShutterProbe sh(p);

        ASSERT_TRUE(sh.set_value("impulse down 0"));
        EXPECT_EQ(sh.impulseDownMs, 0);
        //A shutter that never started moving would make the rest vacuous
        ASSERT_FALSE(sh.isStopped()) << "shutter never started moving";
        EXPECT_TRUE(runLoopUntil([&]() { return sh.isStopped(); }, 2000))
            << "a zero length impulse never stopped the shutter";
    }

    pumpLoopFor(150);
    EXPECT_EQ(armedTimerCount(), before)
        << "an impulse stop handle stayed armed on the loop for ever";
}

TEST_F(ShutterImpulseLifetimeTest, PlainOutOfRangeImpulseLeavesNoTimerArmedForEver)
{
    loadConfig();

    //Drain first: an impulse stop parked by an earlier case is armed for up
    //to (duration + impulse_time) and would otherwise fire, close, and
    //cancel out the leak this case is looking for.
    pumpLoopFor(800);
    const int before = armedTimerCount();

    {
        Params p = plainParams("t334_plain_huge");
        PlainShutterProbe sh(p);

        //T3.25a: a duration that does not READ as an int is refused outright
        //and never reaches the deadline arithmetic at all. The top of the
        //range is still reachable, by asking for it in a spelling that reads.
        EXPECT_FALSE(sh.set_value("impulse down 99999999999999999999"));
        EXPECT_EQ(sh.impulseDownCalls, 0);

        const auto issued = std::chrono::steady_clock::now();
        freshenLoopClock();
        ASSERT_TRUE(sh.set_value(
            "impulse down " +
            Utils::to_string(std::numeric_limits<int>::max())));
        EXPECT_EQ(sh.impulseDownMs, std::numeric_limits<int>::max());
        ASSERT_FALSE(sh.isStopped()) << "shutter never started moving";

        //Asking for 24 days on a 30 s shutter: no early stop is due, the
        //shutter simply travels to its end. Waiting for the real stop would
        //cost that 30 s travel, so this one keeps a budget - but it is still
        //a LOWER bound and not a wall clock check: a host so slow that the
        //budget buys fewer iterations answers -1, never a red. Only a stop
        //that really happened inside 200 ms can redden it.
        EXPECT_EQ(pumpUntilSince(issued, [&]() { return sh.isStopped(); },
                                 kOutOfRangeBudgetMs), -1)
            << "an out of range impulse stopped the shutter early";
    }

    pumpLoopFor(150);
    EXPECT_EQ(armedTimerCount(), before)
        << "an impulse stop handle stayed armed on the loop for ever";
}

TEST_F(SmartShutterImpulseTest, SmartZeroLengthImpulseLeavesNoTimerArmedForEver)
{
    loadConfig();

    //Drain first: an impulse stop parked by an earlier case is armed for up
    //to (duration + impulse_time) and would otherwise fire, close, and
    //cancel out the leak this case is looking for.
    pumpLoopFor(800);
    const int before = armedTimerCount();

    {
        //Drop impulse_time so that readConfig() leaves it at -1
        Params p = smartParams("t334_smart_zero");
        p.Delete("impulse_time");
        SmartShutterProbe sh(p);

        ASSERT_TRUE(sh.set_value("impulse down 0"));
        EXPECT_EQ(sh.impulseDownMs, 0);
        //A shutter that never started moving would make the rest vacuous
        ASSERT_FALSE(sh.isStopped()) << "shutter never started moving";
        EXPECT_TRUE(runLoopUntil([&]() { return sh.isStopped(); }, 2000))
            << "a zero length impulse never stopped the shutter";
    }

    pumpLoopFor(150);
    EXPECT_EQ(armedTimerCount(), before)
        << "an impulse stop handle stayed armed on the loop for ever";
}
