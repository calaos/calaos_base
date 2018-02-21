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
#ifndef HISTLOGGER_H
#define HISTLOGGER_H

#include <thread>
#include "ThreadedQueue.h"
#include "Calaos.h"
#include "Timer.h"

using namespace Calaos;

class HistEvent
{
public:
    int64_t id;
    string uuid;
    int event_type;
    string io_id;
    string io_state;
    string event_raw;
    string pic_uid;

    //init a new event with filled parameters
    static HistEvent create();
};

class HistLogger
{
public:
    static HistLogger &Instance()
    {
        static HistLogger inst;
        return inst;
    }
    ~HistLogger();

    void appendEvent(HistEvent &ev);

private:
    HistLogger();

    void sqliteWorker();

    string dbname;

    std::atomic_bool done{false};
    std::thread sqliteThread;
    ThreadedQueue<HistEvent> eventQueue;
};

#endif
