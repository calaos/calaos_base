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
 * loop and checks the shutter is STILL moving at a point in time the broken
 * code has long passed, then that it does stop. Every wait is wall clock
 * bounded (same discipline as core/Timer_test) so a regression fails the
 * test instead of hanging make check.
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
#include <string>

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

/* Pump the default loop for a fixed wall clock duration. */
void pumpLoopFor(int ms)
{
    auto loop = uvw::Loop::getDefault();
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(ms);

    while (std::chrono::steady_clock::now() < deadline)
        loop->run<uvw::Loop::Mode::NOWAIT>();
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

    ASSERT_TRUE(sh.set_value("impulse down " + Utils::to_string(kPlainDownMs)));
    ASSERT_FALSE(sh.isStopped()) << "shutter never started moving";

    pumpLoopFor(stillMovingProbeMs(kPlainDownMs, kPlainImpulseTimeMs));
    EXPECT_FALSE(sh.isStopped())
        << "the shutter stopped after impulse_time instead of the "
           "requested duration";

    EXPECT_TRUE(runLoopUntil([&]() { return sh.isStopped(); }))
        << "the shutter never stopped";
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

    ASSERT_TRUE(sh.set_value("impulse up " + Utils::to_string(kPlainUpMs)));
    ASSERT_FALSE(sh.isStopped()) << "shutter never started moving";

    pumpLoopFor(stillMovingProbeMs(kPlainUpMs, kPlainImpulseTimeMs));
    EXPECT_FALSE(sh.isStopped());

    EXPECT_TRUE(runLoopUntil([&]() { return sh.isStopped(); }));
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

    ASSERT_TRUE(sh.set_value("impulse down " + Utils::to_string(kPlainDownMs)));
    EXPECT_EQ(sh.impulseDownMs, kPlainDownMs);
    ASSERT_FALSE(sh.isStopped());

    pumpLoopFor(stillMovingProbeMs(kPlainDownMs, 0));
    EXPECT_FALSE(sh.isStopped());

    EXPECT_TRUE(runLoopUntil([&]() { return sh.isStopped(); }));
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

    ASSERT_TRUE(sh.set_value("impulse down " + Utils::to_string(kSmartDownMs)));
    ASSERT_FALSE(sh.isStopped()) << "shutter never started moving";

    pumpLoopFor(stillMovingProbeMs(kSmartDownMs, kSmartImpulseTimeMs));
    EXPECT_FALSE(sh.isStopped())
        << "the shutter stopped after impulse_time instead of the "
           "requested duration";

    EXPECT_TRUE(runLoopUntil([&]() { return sh.isStopped(); }))
        << "the shutter never stopped";
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
    ASSERT_TRUE(sh.set_value("impulse up " + Utils::to_string(kSmartUpMs)));
    ASSERT_FALSE(sh.isStopped()) << "shutter never started moving";

    pumpLoopFor(stillMovingProbeMs(kSmartUpMs, kSmartImpulseTimeMs));
    EXPECT_FALSE(sh.isStopped());

    EXPECT_TRUE(runLoopUntil([&]() { return sh.isStopped(); }));
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
