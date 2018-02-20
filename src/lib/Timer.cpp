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
    auto loop = uvw::Loop::getDefault();
    handleTimer = loop->resource<uvw::TimerHandle>();

    handleTimer->on<uvw::TimerEvent>([this](const auto &, auto &)
    {
        this->Tick();
    });

    handleTimer->start(uvw::TimerHandle::Time{time},
                       uvw::TimerHandle::Time{time});
}

Timer::~Timer()
{
    handleTimer->stop();
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
    handleIdler->stop();
    handleIdler->close();
}

void Idler::createIdler()
{
    auto loop = uvw::Loop::getDefault();
    handleIdler = loop->resource<uvw::IdleHandle>();

    handleIdler->on<uvw::IdleEvent>([this](const auto &, auto &)
    {
        idlerCallback.emit();
    });

    handleIdler->start();
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
