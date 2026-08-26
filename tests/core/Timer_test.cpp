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
 * T2.1 — Timer lifetime regression tests.
 *
 * Timer::singleShot used to build a *repeating* uvw timer whose callback did
 * `slot(); delete timer;` — a delete executed in the middle of the uvw
 * dispatch, with the deleted object's lambda still on the stack (same shape
 * for Idler::singleIdler). The rework makes singleShot a true one-shot
 * (repeat = 0) whose handle is closed before the slot runs, destruction
 * being deferred by uvw to the loop's close callback. Regular Timer objects
 * additionally hold an "alive" tag only weakly captured by the uvw lambda,
 * which makes both delete-from-inside-the-callback (InputTimer::TimerDone
 * pattern) and destruction with a pending callback safe.
 *
 * Timer only depends on src/lib, so unlike the other core tests this binary
 * can (and does) run the default uvw loop itself, in a bounded way: every
 * wait has a wall-clock deadline so a regression can make a test fail but
 * never hang `make check`. Use-after-free / double-free regressions are
 * caught by ASan (the whole test suite runs under it).
 ******************************************************************************/

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <thread>

#include "Timer.h"
#include "libuvw.h"

namespace
{

/* Run the default loop until pred() is true, with a wall-clock deadline so
 * that a broken implementation fails the test instead of hanging it. */
bool runLoopUntil(const std::function<bool()> &pred, int timeoutMs = 2000)
{
    auto loop = uvw::Loop::getDefault();
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);

    while (!pred())
    {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        loop->run<uvw::Loop::Mode::ONCE>();
    }

    return true;
}

int elapsedMs(const std::chrono::steady_clock::time_point &t0)
{
    return (int) std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - t0).count();
}

/* Keep pumping the loop for a fixed wall-clock duration: processes close
 * callbacks and would let any spurious extra tick (repeating-timer
 * regression) fire and be counted.
 *
 * ⭐ T3.49: this is the third copy of the helper (core/ShutterImpulse_test,
 * core/IoLifetimeTimer_test) and it now counts its iterations and reports
 * them on failure, like the other two. The assertions that FOLLOW it here are
 * not on the false-red axis - they check that a count did NOT grow, so a
 * longer pump can only make them stricter - what would hurt them is a pump
 * that returned having exercised nothing, which is why the guard below
 * refuses a degenerate duration. No iteration cap and no ms-sized iteration
 * floor, for the reasons spelled out in core/ShutterImpulse_test.cpp. */
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

    /* A check on the ARGUMENT, not a post-condition on the wait: t0 is taken
     * on entry, so for any ms >= 1 the body runs at least once and the loop
     * exits only once elapsed >= ms - the last two clauses are unreachable
     * from every call site that exists. pumpLoopFor(0) is what is reachable,
     * and it is a no-op wearing the shape of a wait. See the same note in
     * core/ShutterImpulse_test.cpp. */
    if (ms < 1 || iterations < 1 || elapsedMs(t0) < ms)
        ADD_FAILURE() << "pumpLoopFor(" << ms << ") gave up after "
                      << elapsedMs(t0) << " ms and " << iterations
                      << " iterations: this wait proved nothing";
}

/* ⭐ T3.56 - answer the ms elapsed SINCE t0 at the moment pred() was first
 * SEEN to hold, or -1 if it never was within the budget.
 *
 * Same shape as core/ShutterImpulse_test's pumpUntilSince, and here for the
 * same reason: the cases below assert a LOWER bound on when a timer fired,
 * and time stolen anywhere after t0 can only make that answer LARGER. A busy
 * host cannot falsify "the timer was not observed fired before D ms"; it can
 * trivially falsify "the timer has not fired at D ms".
 *
 * ⚠️ t0 must be taken BEFORE the timer is armed, never after. The whole point
 * of these cases is where the deadline lands relative to the arming instant. */
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

/* ⭐ T3.56 - FABRICATE the idle stretch this whole suite is about.
 *
 * Nothing pumps the loop during these ms, which is exactly what happens
 * between uv_loop_init() and the first loop->run(): calaos_server creates the
 * loop at CalaosConfig.cpp:206 (the Timer(60.0) of the state cache) and does
 * not run it until main.cpp:223, with LoadConfigIO()/LoadConfigRule() in
 * between. Every timer armed by an IO constructor in that window is armed off
 * a loop clock frozen at line 206.
 *
 * ⚠️ A case that pumps the loop throughout can NEVER see this defect - the
 * pump is what refreshes the clock. The idle stretch is the fixture. */
void goIdleFor(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

/* Number of timer handles still armed on the default loop (same helper as
 * LanHue_test): closed-but-not-yet-collected handles are filtered out. */
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

/* All live (not yet collected) handles of a given type, closing or not:
 * once the loop ran the close callbacks, a properly released handle must
 * not be walked anymore — this detects leaked self-referencing handles. */
int liveHandleCount(uvw::HandleType type)
{
    int count = 0;

    uvw::Loop::getDefault()->walk([&count, type](uvw::BaseHandle &handle)
    {
        if (handle.type() == type)
            count++;
    });

    return count;
}

}

/******************************************************************************
 * Timer::singleShot
 ******************************************************************************/

TEST(TimerSingleShot, FiresExactlyOnceThenReleasesHandle)
{
    int count = 0;

    Timer::singleShot(0.01, [&count]() { count++; });

    ASSERT_TRUE(runLoopUntil([&]() { return count >= 1; }));

    //A repeating-timer regression would tick again here
    pumpLoopFor(60);
    EXPECT_EQ(count, 1);

    //Handle must be fully gone (closed AND collected), not just disarmed
    EXPECT_EQ(armedTimerCount(), 0);
    EXPECT_EQ(liveHandleCount(uvw::HandleType::TIMER), 0);
}

TEST(TimerSingleShot, ChainedFromInsideCallback)
{
    //Reconnection-style pattern used by call sites (e.g. AVReceiver):
    //the slot of a singleShot schedules another singleShot.
    int count = 0;

    Timer::singleShot(0.005, [&count]()
    {
        Timer::singleShot(0.005, [&count]() { count++; });
    });

    ASSERT_TRUE(runLoopUntil([&]() { return count >= 1; }));

    pumpLoopFor(40);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(liveHandleCount(uvw::HandleType::TIMER), 0);
}

/******************************************************************************
 * Timer object lifetime
 ******************************************************************************/

TEST(TimerLifetime, RepeatingTimerStillRepeats)
{
    int count = 0;
    Timer t(0.005, sigc::slot<void>([&count]() { count++; }));

    ASSERT_TRUE(runLoopUntil([&]() { return count >= 3; }));
    EXPECT_GE(count, 3);

    //t goes out of scope here, next test checks nothing lingers
}

TEST(TimerLifetime, DeleteFromOwnCallback)
{
    //InputTimer::TimerDone does exactly this: the slot deletes the Timer
    //whose uvw lambda is currently on the stack. ASan catches a regression.
    int count = 0;
    Timer *t = nullptr;

    t = new Timer(0.01, sigc::slot<void>([&]()
    {
        count++;
        delete t;
        t = nullptr;
    }));

    ASSERT_TRUE(runLoopUntil([&]() { return count >= 1; }));

    pumpLoopFor(60);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(t, nullptr);
    EXPECT_EQ(armedTimerCount(), 0);
}

TEST(TimerLifetime, DestroyedBeforeFiringNeverFires)
{
    int count = 0;
    bool guardDone = false;

    Timer *t = new Timer(0.01, sigc::slot<void>([&count]() { count++; }));
    delete t;

    //Guard timer keeps the loop busy well past the deleted timer's due time
    Timer::singleShot(0.05, [&guardDone]() { guardDone = true; });

    ASSERT_TRUE(runLoopUntil([&]() { return guardDone; }));
    EXPECT_EQ(count, 0);
    EXPECT_EQ(armedTimerCount(), 0);
}

TEST(TimerLifetime, PeerCallbackDeletesTimerDueInSameIteration)
{
    //Two timers due in the same loop iteration; the first one's slot
    //deletes the second while the loop is dispatching the timer batch.
    //The deleted timer must not fire nor be touched (ASan).
    int countA = 0, countB = 0;
    bool guardDone = false;

    //Same-deadline timers fire in creation order: create the deleting
    //timer first so it runs before its victim in the same batch.
    Timer *a = nullptr;
    Timer *b = nullptr;
    a = new Timer(0.01, sigc::slot<void>([&]()
    {
        countA++;
        delete b;
        b = nullptr;
        delete a;
        a = nullptr;
    }));
    b = new Timer(0.01, sigc::slot<void>([&countB]() { countB++; }));

    Timer::singleShot(0.05, [&guardDone]() { guardDone = true; });

    ASSERT_TRUE(runLoopUntil([&]() { return guardDone; }));
    EXPECT_EQ(countA, 1);
    EXPECT_EQ(countB, 0);
    EXPECT_EQ(armedTimerCount(), 0);
}

/******************************************************************************
 * Idler
 ******************************************************************************/

TEST(IdlerLifetime, SingleIdlerRunsExactlyOnce)
{
    int count = 0;

    Idler::singleIdler([&count]() { count++; });

    ASSERT_TRUE(runLoopUntil([&]() { return count >= 1; }));

    pumpLoopFor(30);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(liveHandleCount(uvw::HandleType::IDLE), 0);
}

TEST(IdlerLifetime, DeleteFromOwnCallback)
{
    //Old singleIdler shape, written manually: the idler deletes itself from
    //its own callback. Must be safe now (weak alive tag).
    int count = 0;
    Idler *o = new Idler();

    o->idlerCallback.connect(sigc::slot<void>([&]()
    {
        count++;
        delete o;
        o = nullptr;
    }));

    ASSERT_TRUE(runLoopUntil([&]() { return count >= 1; }));

    pumpLoopFor(30);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(o, nullptr);
    EXPECT_EQ(liveHandleCount(uvw::HandleType::IDLE), 0);
}

/******************************************************************************
 * ⭐ T3.56 — a timer armed while the loop is IDLE must still wait its delay.
 *
 * ---------------------------------------------------------------------------
 * THE MECHANISM, AND WHY IT IS A PRODUCT DEFECT AND NOT A TEST ARTEFACT
 * ---------------------------------------------------------------------------
 * uv_timer_start() does NOT read the clock. It computes its deadline from
 * loop->time, the CACHED clock, which only uv__update_time() advances — at the
 * top of every uv_run() and again right after epoll_pwait(). So a timer armed
 * during a stretch in which nobody runs the loop is armed in the PAST by the
 * length of that stretch. T3.49 measured this on the test side; the same
 * arming code is what every IO constructor of the server uses.
 *
 * calaos_server creates the default loop at CalaosConfig.cpp:206 and does not
 * run it until main.cpp:223. LoadConfigIO()/LoadConfigRule() (main.cpp:150-151)
 * sit in between and build every IO of the installation. Every Timer an IO
 * constructor arms in that window is therefore SHORTER than it says by the
 * duration of the configuration load, and any delay smaller than that load
 * fires on the very first loop iteration — IO/Wago/WagoMap.cpp:46 asks for
 * 0.1 s, the shortest pre-loop delay of the tree.
 *
 * ---------------------------------------------------------------------------
 * ⛔ THE FIXTURE TRAP THAT COST THIS SUITE ITS FIRST RUN — READ THIS BEFORE
 *    TOUCHING ANY OF THE FOUR CASES
 * ---------------------------------------------------------------------------
 * The first version of these cases was GREEN on the unfixed tree — 819 ms per
 * case, the timer waiting its full 200 ms after a 600 ms idle stretch — while
 * a standalone libuv probe on the very same image reproduced the defect in
 * three lines. The oracle was not measuring what it said.
 *
 * uvw::Loop::getDefault() (uvw/src/uvw/loop.hpp) caches the wrapper in a
 * std::weak_ptr. When nothing holds a strong reference — and nothing does
 * while no handle is alive — the shared_ptr it just returned is destroyed at
 * the end of the full expression, ~Loop() calls uv_loop_close(), and
 * uv_loop_close() ends with `if (loop == default_loop_ptr) default_loop_ptr =
 * NULL;`. The NEXT uv_default_loop() therefore runs uv_loop_init() again on
 * the same static uv_loop_t — and uv_loop_init() calls uv__update_time().
 *
 * ⇒ ⭐ IN A BINARY WITH NO LIVE HANDLE, EVERY `uvw::Loop::getDefault()->x()`
 * SILENTLY RE-INITIALISES THE DEFAULT LOOP AND HANDS BACK A FRESH CLOCK.
 * Measured on this image: two temporaries either side of a 600 ms sleep, same
 * raw loop pointer, uv_now() advanced by 600.
 *
 * calaos_server is NOT in that shape and this is what makes the defect real
 * there: Config's own state-cache Timer (CalaosConfig.cpp:206) holds a live
 * uvw handle — hence a strong reference to the Loop — from before the
 * configuration load until the process ends, so the loop is never recreated
 * and its clock never refreshed until main.cpp:223.
 *
 * ⇒ EVERY CASE BELOW PINS THE LOOP (`auto loop = uvw::Loop::getDefault();`
 * held for the whole case) AND THEN ASSERTS THAT THE FIXTURE REALLY PRODUCED
 * A STALE CLOCK. Without the pin the fixture evaporates; without the assertion
 * nobody notices.
 *
 * ---------------------------------------------------------------------------
 * WHAT THESE CASES ARE, AND WHAT THEY ARE NOT
 * ---------------------------------------------------------------------------
 * They are a LOWER bound on when a timer fires, measured from the instant it
 * was armed, with an idle stretch deliberately placed before the arming. The
 * lower bound is the falsifiable half: time stolen after t0 can only make the
 * answer larger, so a slow host cannot redden them; a deadline that moved into
 * the past can only make it smaller, and that is the defect.
 *
 * ⚠️ kIdleGapMs is deliberately THREE TIMES kArmedDelayMs. An idle stretch
 * shorter than the armed delay proves nothing at all: the deadline would still
 * land in the future and every case would pass on broken code. The gap has to
 * be comfortably LONGER than what is being armed.
 *
 * ⚠️ kCoarseClockSlackMs is not padding for a busy host — the lower bound does
 * not need any. It answers ONE thing: uv__update_time() reads
 * CLOCK_MONOTONIC_COARSE while these cases measure with CLOCK_MONOTONIC, so a
 * freshly updated loop->time can legitimately sit up to one kernel tick behind
 * the origin. 25 ms is far above any CONFIG_HZ in use and still 8x below the
 * 200 ms being asserted, so it can never hide the defect: broken, these cases
 * fire at ~0 ms, not at 180.
 ******************************************************************************/

namespace
{
/* Three times the armed delay: see the ⚠️ above. */
constexpr int kIdleGapMs = 600;
constexpr int kArmedDelayMs = 200;
constexpr int kCoarseClockSlackMs = 25;

/* Upper bound for the NOMINAL case only, and generous on purpose: it is not
 * there to time the loop, it is there so a "fix" that made every wait wildly
 * longer — or that armed nothing at all — cannot pass as an improvement. */
constexpr int kNominalCeilingMs = 2000;

/* ⭐ How far behind the wall clock the loop's CACHED clock has fallen.
 *
 * This is the quantity the whole suite is about, and measuring it is what
 * turns "we slept, so the clock must be stale" into something checked. It is
 * also the guard against the fixture trap documented above: a re-initialised
 * loop answers ~0 here and the case says so instead of passing. */
int loopClockStalenessMs(const std::shared_ptr<uvw::Loop> &loop)
{
    const uint64_t wall = uv_hrtime() / 1000000ULL;
    const uint64_t cached = (uint64_t) loop->now().count();

    return wall > cached ? (int)(wall - cached) : 0;
}
}

TEST(TimerStaleLoopClock, RepeatingTimerArmedAfterAnIdleStretchStillWaitsItsFullDelay)
{
    //⚠️ THE PIN, see the block comment. Held for the whole case.
    auto loop = uvw::Loop::getDefault();

    //Start from a CURRENT loop clock so the staleness under test is exactly
    //kIdleGapMs and nothing left over from the cases above.
    loop->run<uvw::Loop::Mode::NOWAIT>();

    goIdleFor(kIdleGapMs);

    ASSERT_GE(loopClockStalenessMs(loop), kIdleGapMs / 2)
        << "the fixture did not produce a stale loop clock: after " << kIdleGapMs
        << " ms with nobody running the loop, loop->time is still current. "
           "Something refreshed it — the usual culprit is a "
           "uvw::Loop::getDefault() TEMPORARY re-initialising the default loop "
           "(see the block comment). This case proves nothing in that state.";

    int count = 0;
    int firedAt = -1;

    {
        //⚠️ t0 BEFORE the arming, always. What is under test is where the
        //deadline lands relative to the instant the Timer was constructed.
        const auto t0 = std::chrono::steady_clock::now();
        Timer t(kArmedDelayMs / 1000.0, sigc::slot<void>([&count]() { count++; }));

        firedAt = pumpUntilSince(t0, [&count]() { return count >= 1; });
    }

    ASSERT_GE(firedAt, 0)
        << "the timer never fired at all within the budget: this case can say "
           "nothing about when it fired";

    EXPECT_GE(firedAt, kArmedDelayMs - kCoarseClockSlackMs)
        << "a Timer asked for " << kArmedDelayMs << " ms fired after "
        << firedAt << " ms, having been armed while the loop had been idle for "
        << kIdleGapMs << " ms.\n"
        << "uv_timer_start() computed its deadline from the CACHED loop clock, "
           "which nothing refreshed during that idle stretch, so the deadline "
           "was already in the past when the timer was armed.\n"
        << "In calaos_server that idle stretch is the configuration load "
           "(main.cpp:150-151) and this is what makes IO/Wago/WagoMap.cpp:46 "
           "(0.1 s) fire on the first loop iteration.";

    //Give the closed handle its close callback back before the next case.
    pumpLoopFor(20);
}

TEST(TimerStaleLoopClock, SingleShotArmedAfterAnIdleStretchStillWaitsItsFullDelay)
{
    //Separate case, not a parameter of the one above: Timer::singleShot() arms
    //its own anonymous handle through a DIFFERENT code path (Timer.cpp), and a
    //remedy applied to one and not the other has to be visible. The two cases
    //are what makes the counter-mutation sets distinct.
    auto loop = uvw::Loop::getDefault();

    loop->run<uvw::Loop::Mode::NOWAIT>();

    goIdleFor(kIdleGapMs);

    ASSERT_GE(loopClockStalenessMs(loop), kIdleGapMs / 2)
        << "the fixture did not produce a stale loop clock; see the case above";

    int count = 0;
    const auto t0 = std::chrono::steady_clock::now();

    Timer::singleShot(kArmedDelayMs / 1000.0, [&count]() { count++; });

    const int firedAt = pumpUntilSince(t0, [&count]() { return count >= 1; });

    ASSERT_GE(firedAt, 0)
        << "the one-shot never fired at all within the budget";

    EXPECT_GE(firedAt, kArmedDelayMs - kCoarseClockSlackMs)
        << "Timer::singleShot(" << (kArmedDelayMs / 1000.0) << ") fired after "
        << firedAt << " ms, having been armed while the loop had been idle for "
        << kIdleGapMs << " ms.\n"
        << "This is the shape Audio/RoonPlayer.cpp:243 uses to \"wait for the "
           "process to start\": a delay armed off a frozen loop clock is "
           "amputated by the length of the idle stretch that preceded it.";

    pumpLoopFor(20);
}

/* ⭐ THE THIRD ARMING PATH, and the one no census had listed.
 *
 * Timer::Reset() re-arms through uv_timer_again(), which reads the SAME cached
 * loop clock as uv_timer_start(). It is not a loop-only path: WebCtrl::Add()
 * runs from the constructor of every web IO (IO/Web/WebDocBase.h:99,
 * webRegisterPolling()) and from the second such IO onwards it takes the
 * timer->Reset(frequency) branch, IO/Web/WebCtrl.cpp:115 — inside the
 * configuration load, with the loop stopped.
 *
 * The Timer is armed here on a CURRENT clock with a delay it cannot reach
 * during the idle stretch, so the only thing this case can be measuring is
 * where Reset() put the new deadline. */
TEST(TimerStaleLoopClock, ResetAfterAnIdleStretchStillWaitsItsFullDelay)
{
    auto loop = uvw::Loop::getDefault();

    loop->run<uvw::Loop::Mode::NOWAIT>();

    int count = 0;
    int firedAt = -1;

    {
        //10 s: far beyond the idle stretch AND beyond the budget below, so it
        //cannot fire on its own and be mistaken for the Reset() under test.
        Timer t(10.0, sigc::slot<void>([&count]() { count++; }));

        pumpLoopFor(20);
        ASSERT_EQ(count, 0) << "the 10 s timer fired before Reset() was even "
                               "called: this case is not measuring Reset()";

        goIdleFor(kIdleGapMs);

        ASSERT_GE(loopClockStalenessMs(loop), kIdleGapMs / 2)
            << "the fixture did not produce a stale loop clock; see the first "
               "case of this suite";

        const auto t0 = std::chrono::steady_clock::now();
        t.Reset(kArmedDelayMs / 1000.0);

        firedAt = pumpUntilSince(t0, [&count]() { return count >= 1; });
    }

    ASSERT_GE(firedAt, 0)
        << "the timer never fired after Reset() within the budget";

    EXPECT_GE(firedAt, kArmedDelayMs - kCoarseClockSlackMs)
        << "Timer::Reset(" << (kArmedDelayMs / 1000.0) << ") fired after "
        << firedAt << " ms, having been called while the loop had been idle "
           "for " << kIdleGapMs << " ms.\n"
        << "uv_timer_again() reads the same cached loop clock as "
           "uv_timer_start(), so a Reset() issued from an IO constructor — "
           "IO/Web/WebCtrl.cpp:115 — lands its deadline in the past too.";

    pumpLoopFor(20);
}

/* ⭐ THE SYMMETRIC CONTROL, and it is not decoration.
 *
 * A remedy that stopped distinguishing anything — one that armed every timer
 * far into the future, or that turned the wait into a handle nobody starts —
 * would make the three cases above green while being strictly worse than the
 * defect. This case pins the NOMINAL behaviour from both ends on a loop clock
 * that is already current: the delay is still honoured, and it is still only
 * the delay. It is expected green before and after the remedy; its job is to
 * be the case a bad remedy breaks. */
TEST(TimerStaleLoopClock, OnACurrentLoopClockTheDelayIsHonouredAndNothingMore)
{
    auto loop = uvw::Loop::getDefault();

    //No idle stretch here: this is the case that must NOT move.
    loop->run<uvw::Loop::Mode::NOWAIT>();

    ASSERT_LE(loopClockStalenessMs(loop), kCoarseClockSlackMs)
        << "the loop clock was not current at the start of the control case, "
           "so this case is not the control it claims to be";

    int count = 0;
    int firedAt = -1;

    {
        const auto t0 = std::chrono::steady_clock::now();
        Timer t(kArmedDelayMs / 1000.0, sigc::slot<void>([&count]() { count++; }));

        firedAt = pumpUntilSince(t0, [&count]() { return count >= 1; });
    }

    ASSERT_GE(firedAt, 0) << "the timer never fired on a current loop clock";

    EXPECT_GE(firedAt, kArmedDelayMs - kCoarseClockSlackMs)
        << "a Timer asked for " << kArmedDelayMs << " ms fired after "
        << firedAt << " ms on a loop clock that was already current";

    EXPECT_LE(firedAt, kNominalCeilingMs)
        << "a Timer asked for " << kArmedDelayMs << " ms fired after "
        << firedAt << " ms on a loop clock that was already current: whatever "
           "closes the stale-clock hole must not push ordinary waits out.";

    pumpLoopFor(20);
}
