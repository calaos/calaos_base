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
#include "HistLogger.h"
#include "sole.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "sqlite_modern_cpp.h"
#pragma GCC diagnostic pop

#define DB_CREATE_SQL "CREATE TABLE IF NOT EXISTS events (" \
    "id INTEGER PRIMARY KEY UNIQUE NOT NULL, " \
    "uuid TEXT, " \
    "datetime DATETIME, " \
    "event_type INTEGER, " \
    "io_id TEXT, " \
    "io_state TEXT, " \
    "event_raw TEXT, " \
    "pic_uid TEXT);"

HistEvent HistEvent::create()
{
    sole::uuid u4 = sole::uuid4();

    HistEvent e;
    e.uuid = u4.str();
    e.datetime = std::chrono::seconds(std::time(nullptr)).count();

    return e;
}

HistLogger::HistLogger()
{
    dbname = Utils::getCacheFile("events.db");
    sqliteThread = std::thread([this]() { sqliteWorker(); });
}

HistLogger::~HistLogger()
{
    done = true;
    eventQueue.invalidate();

    if (sqliteThread.joinable())
        sqliteThread.join();
}

void HistLogger::appendEvent(HistEvent &ev)
{
    eventQueue.push(ev);
}

void HistLogger::sqliteWorker()
{
    cInfoDom("history") << "History logger, setup database to " << dbname;
    sqlite::database db(dbname);

    try
    {
        db << DB_CREATE_SQL;
    }
    catch (exception &e)
    {
        cCriticalDom("history") << "Failed to open and setup database!";
        cCriticalDom("history") << e.what();
    }

    while (!done)
    {
        HistEvent event;
        if (eventQueue.waitPop(event))
        {
            //write the new event to the db
            cDebugDom("history") << "Write to SQLite db: " << event.io_id << " > " << event.io_state;

            try
            {
                db << "INSERT INTO events (uuid, datetime, event_type, io_id, io_state, event_raw, pic_uid) values (?, ?, ?, ?, ?, ?, ?);"
                   << event.uuid
                   << event.datetime
                   << event.event_type
                   << event.io_id
                   << event.io_state
                   << event.event_raw
                   << event.pic_uid;
            }
            catch (exception &e)
            {
                cCriticalDom("history") << "Failed to INSERT into db: " << e.what();
            }
        }
    }

    cDebugDom("history") << "Exiting sqlite thread.";
}

