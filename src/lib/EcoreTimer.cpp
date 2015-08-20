/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include <EcoreTimer.h>

static Eina_Bool _calaos_timer_event(void *data)
{
    EcoreTimer *etimer = (EcoreTimer *)data;
    etimer->Tick();

    return EINA_TRUE;
}

EcoreTimer::EcoreTimer(double in, sigc::slot<void, void *> slot, void *d):
    timer(NULL), time(in), timer_data(true), data(d)
{
    //connect the sigc slot
    connection_data = event_signal_data.connect(slot);

    //create the ecore timer
    timer = ecore_timer_add(time, _calaos_timer_event, this);
}

EcoreTimer::EcoreTimer(double in, sigc::slot<void> slot):
    timer(NULL), time(in), timer_data(false)
{
    //connect the sigc slot
    connection = event_signal.connect(slot);

    //create the ecore timer
    timer = ecore_timer_add(time, _calaos_timer_event, this);
}

EcoreTimer::~EcoreTimer()
{
    //disconnect the sigc slot
    if (!timer_data)
        connection.disconnect();
    else
        connection_data.disconnect();

    //delete the ecore timer
    if (timer) ecore_timer_del(timer);
}

void EcoreTimer::Reset()
{
    //reset the ecore timer
    if (timer) ecore_timer_del(timer);
    timer = ecore_timer_add(time, _calaos_timer_event, this);
}

void EcoreTimer::Reset(double in)
{
    time = in;
    Reset();
}

void EcoreTimer::Tick()
{
    if (timer_data)
        event_signal_data.emit(data);
    else
        event_signal.emit();
}

void EcoreTimer::singleShot(double time, sigc::slot<void> slot)
{
    EcoreTimer *timer = new EcoreTimer(time, [=](void *_data)
    {
        EcoreTimer *t = reinterpret_cast<EcoreTimer *>(_data);
        if (t)
        {
            slot();
            delete t;
        }
    }, nullptr);
    timer->data = timer;
}

