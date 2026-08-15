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

void Timer::Reset()
{
    handleTimer->again();
}

void Timer::Reset(double in)
{
    time = in * 1000.0;
    handleTimer->repeat(uvw::TimerHandle::Time{time});
    handleTimer->again();
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
