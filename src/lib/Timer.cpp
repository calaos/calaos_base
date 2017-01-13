/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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

static void _calaos_timer_event(uv_timer_t *req)
{
    Timer *timer = (Timer *)req->data;
    timer->Tick();
}

Timer::Timer(double in, sigc::slot<void, void *> slot, void *d):
    time(in), timer_data(true), data(d)
{
    //connect the sigc slot
    connection_data = event_signal_data.connect(slot);

    //create the libuv timer
    timer.data = this;
    uv_timer_init(uv_default_loop(), &timer);
    uv_timer_start(&timer, _calaos_timer_event, time, time);
}

Timer::Timer(double in, sigc::slot<void> slot):
    time(in), timer_data(false)
{
    //connect the sigc slot
    connection = event_signal.connect(slot);

    //create the libuv timer
    timer.data = this;
    uv_timer_init(uv_default_loop(), &timer);
    uv_timer_start(&timer, _calaos_timer_event, time, time);
}

Timer::~Timer()
{
    //disconnect the sigc slot
    if (!timer_data)
        connection.disconnect();
    else
        connection_data.disconnect();
}

void Timer::Reset()
{
    //reset the ecore timer
    //if (timer) ecore_timer_del(timer);
    //timer = ecore_timer_add(time, _calaos_timer_event, this);
    uv_timer_stop(&timer);
    uv_timer_start(&timer, _calaos_timer_event, time, time);

}

void Timer::Reset(double in)
{
    time = in;
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
    Timer *timer = new Timer(time, [=](void *_data)
    {
      Timer *t = reinterpret_cast<Timer *>(_data);
        if (t)
        {
            slot();
            delete t;
        }
    }, nullptr);
    timer->data = timer;
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
  uv_idle_stop(&idler);
}

void Idler_idler_cb(uv_idle_t* handle)
{
    Idler *o = reinterpret_cast<Idler *>(handle->data);
    if (o)
    {
        o->idlerCallback.emit();
    }
}

void Idler::createIdler()
{
    idler.data = this;
    uv_idle_init(uv_default_loop(), &idler);
    uv_idle_start(&idler, Idler_idler_cb);
}

void Idler::singleIdler(sigc::slot<void> slot)
{
    Idler *o = new Idler();
    o->idlerCallback.connect([=]()
    {
        slot();
        delete o;
    });
}

