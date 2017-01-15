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

Timer::Timer(double in, sigc::slot<void, void *> slot, void *d):
    time(in),
    timer_data(true),
    data(d)
{
    connection_data = event_signal_data.connect(slot);
    create();
}

Timer::Timer(double in, sigc::slot<void> slot):
    time(in),
    timer_data(false)
{
    connection = event_signal.connect(slot);
    create();
}

void Timer::create()
{
    //create the libuv timer
    m_timer = std::make_unique<uv_timer_t>();
    m_timer->data = this;
    uv_timer_init(uv_default_loop(), m_timer.get());

    uv_timer_start(m_timer.get(), [](uv_timer_t* timer)
    {
        auto self = (Timer *)timer->data;
        self->Tick();
    }, time * 1000.0, time * 1000.0);
}

Timer::~Timer()
{
    uv_timer_stop(m_timer.get());

    //Release ownership of smart pointer
    //because it will get out of scope and destroyed
    //in the async uv_close call later in the event loop
    auto handle = m_timer.release();
    uv_close((uv_handle_t *)handle, [](uv_handle_t *h)
    {
        //handle can be safely released now
        free(h);
    });

    //disconnect the sigc slot
    if (!timer_data)
        connection.disconnect();
    else
        connection_data.disconnect();
}

void Timer::Reset()
{
    uv_timer_again(m_timer.get());
}

void Timer::Reset(double in)
{
    time = in;
    uv_timer_set_repeat(m_timer.get(), time * 1000.0);
    uv_timer_again(m_timer.get());
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
    uv_idle_stop(m_idler.get());

    //Release ownership of smart pointer
    //because it will get out of scope and destroyed
    //in the async uv_close call later in the event loop
    auto handle = m_idler.release();
    uv_close((uv_handle_t *)handle, [](uv_handle_t *h)
    {
        //handle can be safely released now
        free(h);
    });
}

void Idler::createIdler()
{
    //create the libuv idler
    m_idler = std::make_unique<uv_idle_t>();
    m_idler->data = this;
    uv_idle_init(uv_default_loop(), m_idler.get());

    uv_idle_start(m_idler.get(), [](uv_idle_t *idle)
    {
        auto self = (Idler *)idle->data;
        self->idlerCallback.emit();
    });
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
