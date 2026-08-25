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
 * ⭐ T3.49: a wait must say whether it waited. This is the third copy of the
 * helper (core/ShutterImpulse_test, core/IoLifetimeTimer_test), and the same
 * post-condition is on all three. The assertions that FOLLOW it here are not
 * on the false-red axis - they check that a count did NOT grow, so a longer
 * pump can only make them stricter - but a pump that returns having run no
 * iteration would make them vacuously green, which is the axis this closes.
 * No iteration cap and no ms-sized iteration floor, for the reasons spelled
 * out in core/ShutterImpulse_test.cpp. */
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

    if (iterations < 1 || elapsedMs(t0) < ms)
        ADD_FAILURE() << "pumpLoopFor(" << ms << ") gave up after "
                      << elapsedMs(t0) << " ms and " << iterations
                      << " iterations: this wait proved nothing";
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
