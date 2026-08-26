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
#include "Timer.h"
#include "libuvw.h"

Timer::Timer(double in, sigc::slot<void, void *> slot, void *d):
    time(in * 1000.0),
    timer_data(true),
    data(d)
{
    connection_data = event_signal_data.connect(slot);
    create();
}

Timer::Timer(double in, sigc::slot<void> slot):
    time(in * 1000.0),
    timer_data(false)
{
    connection = event_signal.connect(slot);
    create();
}

void Timer::create()
{
    aliveTag = std::make_shared<bool>(true);

    auto loop = uvw::Loop::getDefault();
    handleTimer = loop->resource<uvw::TimerHandle>();

    handleTimer->on<uvw::TimerEvent>(
        [this, wtag = std::weak_ptr<bool>(aliveTag)](const auto &, auto &)
    {
        //The Timer object may already be destroyed while the uvw handle is
        //still dispatching (close is asynchronous): bail out without
        //touching `this` in that case.
        if (wtag.expired()) return;
        this->Tick();
        //Careful: the slot may have deleted this Timer (delete from its own
        //callback is allowed), so `this` must not be used past this point.
    });

    //T3.56, see the note above Timer::Reset()
    loop->update();

    handleTimer->start(uvw::TimerHandle::Time{time},
                       uvw::TimerHandle::Time{time});
}

Timer::~Timer()
{
    //Invalidate the tag first: any event already queued for dispatch will
    //see an expired weak_ptr and won't touch this object anymore.
    aliveTag.reset();

    handleTimer->stop();
    //close() is asynchronous: uvw keeps the handle alive (self shared_ptr)
    //until the close callback runs from the loop, so the handle itself is
    //never destroyed in the middle of a dispatch.
    handleTimer->close();

    //disconnect the sigc slot
    if (!timer_data)
        connection.disconnect();
    else
        connection_data.disconnect();
}

/* ⭐ T3.56 - refresh the loop's cached clock before every arming.
 *
 * uv_timer_start() does NOT read the clock: it computes its deadline from
 * loop->time, the CACHED value that only uv__update_time() advances - at the
 * top of every uv_run() and again right after epoll_pwait(). A deadline armed
 * while nobody is running the loop is therefore armed in the PAST by however
 * long the loop has been idle.
 *
 * That is not a corner case in calaos_server, it is the STARTUP: the default
 * loop is created at CalaosConfig.cpp:206 and first run at main.cpp:223, with
 * LoadConfigIO()/LoadConfigRule() (main.cpp:150-151) building every IO of the
 * installation in between. At least twelve arming sites of the tree run inside
 * that window - four of which no census had listed: main.cpp:194 (the 0.1 s
 * rule event loop), :195 (the 5 s watchdog), :198 (the 0.1 s "check config
 * once the main loop is started") and IO/Web/WebCtrl.cpp:115 via Reset(). Every
 * one of them was short by the whole startup, measured (T3.56, real 474-IO and
 * 284-IO configurations) at 39-64 ms on a fast x86 host and 134-142 ms with the
 * host slowed 5x. ⇒ the shortest pre-loop delay of the tree,
 * IO/Wago/WagoMap.cpp:46 at 0.1 s, keeps ~40 ms of its 100 on that fast host
 * and NONE of it once the machine is five times slower.
 *
 * ⭐ WHY HERE AND NOT AT THE CALL SITES. Fixing WagoMap and RoonPlayer would
 * have left the other thirteen, including the three in main.cpp itself
 * (:194 the 0.1 s rule event loop, :195 the 5 s watchdog, :198 the 0.1 s
 * "check config once the main loop is started") that no census had listed, and
 * would have left the next one to be written broken again. Here the property
 * is structural: nothing in the tree can arm a libuv deadline without going
 * through these four functions.
 *
 * ⚠️ uv_update_time() is NOT a loop iteration. It reads the clock and writes
 * loop->time, and dispatches nothing - so this is safe to call from inside a
 * constructor running half-way through a configuration load, which running one
 * NOWAIT iteration (the shape the TESTS use, core/ShutterImpulse_test) would
 * NOT be. libuv documents it for exactly this: "can be called manually if you
 * have callbacks that block the event loop for longer periods of time".
 *
 * ⛔ WHAT THIS DOES NOT DO. It makes a delay honest about its LENGTH; it says
 * nothing about what the delay is being used to wait FOR. Audio/RoonPlayer.cpp
 * :243 waits 10 s "for the process to start": with the clock refreshed those
 * 10 s are really 10 s, but they are still counted from the constructor, which
 * runs BEFORE the loop, so the sidecar only gets 10 s minus whatever the rest
 * of the configuration load costs. See docs/refactoring/T3.56.md. */
void Timer::Reset()
{
    uvw::Loop::getDefault()->update();
    handleTimer->again();
}

/* Delegates so there is ONE place the clock is refreshed for both overloads.
 * ⚠️ This one is reached before the loop runs: WebCtrl::Add() is called from
 * the constructor of every web IO (IO/Web/WebDocBase.h:99, webRegisterPolling)
 * and the second such IO onwards takes the Reset branch, IO/Web/WebCtrl.cpp:115. */
void Timer::Reset(double in)
{
    time = in * 1000.0;
    handleTimer->repeat(uvw::TimerHandle::Time{time});
    Reset();
}

void Timer::Tick()
{
    if (timer_data)
        event_signal_data.emit(data);
    else
        event_signal.emit();
}

void Timer::singleShot(double time, sigc::slot<void> slot)
{
    //True one-shot timer (repeat = 0), no Timer object involved at all.
    //The uvw handle keeps itself alive (internal self shared_ptr) until
    //close() completes: destruction happens in the loop's close callback,
    //never in the middle of the timer dispatch.
    auto loop = uvw::Loop::getDefault();
    auto handle = loop->resource<uvw::TimerHandle>();

    handle->on<uvw::TimerEvent>([slot](const auto &, auto &h)
    {
        //Close before running the slot so the handle is always released,
        //whatever the slot does (including starting new timers).
        h.stop();
        h.close();
        slot();
    });

    //T3.56, see the note above Timer::Reset()
    loop->update();

    handle->start(uvw::TimerHandle::Time{static_cast<uint64_t>(time * 1000.0)},
                  uvw::TimerHandle::Time{0});
}

Idler::Idler(sigc::slot<void> slot)
{
    idlerCallback.connect(slot);
    createIdler();
}

Idler::Idler()
{
    createIdler();
}

Idler::~Idler()
{
    //Same lifetime scheme as ~Timer()
    aliveTag.reset();

    handleIdler->stop();
    handleIdler->close();
}

void Idler::createIdler()
{
    aliveTag = std::make_shared<bool>(true);

    auto loop = uvw::Loop::getDefault();
    handleIdler = loop->resource<uvw::IdleHandle>();

    handleIdler->on<uvw::IdleEvent>(
        [this, wtag = std::weak_ptr<bool>(aliveTag)](const auto &, auto &)
    {
        if (wtag.expired()) return;
        idlerCallback.emit();
        //Careful: the slot may have deleted this Idler, `this` must not be
        //used past this point.
    });

    handleIdler->start();
}

void Idler::singleIdler(sigc::slot<void> slot)
{
    //One-shot idler without any Idler object: stop and close the handle
    //before running the slot (an idle handle fires on every loop iteration,
    //stopping first also protects against the slot re-entering the loop).
    //uvw defers the handle destruction to the close callback.
    auto loop = uvw::Loop::getDefault();
    auto handle = loop->resource<uvw::IdleHandle>();

    handle->on<uvw::IdleEvent>([slot](const auto &, auto &h)
    {
        h.stop();
        h.close();
        slot();
    });

    handle->start();
}
