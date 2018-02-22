/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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

#include "libuvw.h"

#define DB_CREATE_SQL "CREATE TABLE IF NOT EXISTS events (" \
    "id INTEGER PRIMARY KEY UNIQUE NOT NULL, " \
    "uuid TEXT, " \
    "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, " \
    "event_type INTEGER, " \
    "io_id TEXT, " \
    "io_state TEXT, " \
    "event_raw TEXT, " \
    "pic_uid TEXT);"

class HistWorkerAction
{
public:
    enum
    {
        WorkerNoAction,
        WorkerAppend,
        WorkerRead,
    };

    int action = WorkerNoAction;
    HistEvent event;
    std::shared_ptr<uvw::AsyncHandle> handle;
    vector<HistEvent> events;
    bool success = true;
    string errMsg;
    int page, per_page, total_count, total_page;
};

HistEvent HistEvent::create()
{
    sole::uuid u4 = sole::uuid4();

    HistEvent e;
    e.uuid = u4.str();

    return e;
}

Json HistEvent::toJson() const
{
    Json j = {
        { "id", uuid },
        { "event_type", Utils::to_string(event_type) },
        { "io_id", io_id },
        { "io_state", io_state },
        { "pic_uid", pic_uid },
        { "created_at", created_at }
    };

    try
    {
        j["event_raw"] = Json::parse(event_raw);
    }
    catch(const std::exception &)
    {
        j["event_raw"] = Json::object();
    }

    return j;
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
    std::shared_ptr<HistWorkerAction> a = std::make_shared<HistWorkerAction>();
    a->action = HistWorkerAction::WorkerAppend;
    a->event = std::move(ev);
    eventQueue.push(a);
}

void HistLogger::getEvents(int page, int per_page,
                           std::function<void(bool success, string errorMsg, const vector<HistEvent> &events, int total_page, int total_count)> callback)
{
    std::shared_ptr<HistWorkerAction> a = std::make_shared<HistWorkerAction>();
    a->action = HistWorkerAction::WorkerRead;
    a->handle = uvw::Loop::getDefault()->resource<uvw::AsyncHandle>();
    a->page = page;
    a->per_page = per_page;

    a->handle->once<uvw::AsyncEvent>([callback, a](const auto &, uvw::AsyncHandle &h)
    {
        callback(a->success, a->errMsg, a->events, a->total_page, a->total_count);
        h.close();
    });

    eventQueue.push(a);
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
        std::shared_ptr<HistWorkerAction> ac;
        if (eventQueue.waitPop(ac))
        {
            if (ac->action == HistWorkerAction::WorkerAppend)
            {
                //write the new event to the db
                cDebugDom("history") << "Write to SQLite db: " << ac->event.io_id << " > " << ac->event.io_state;

                try
                {
                    db << "INSERT INTO events (uuid, event_type, io_id, io_state, event_raw, pic_uid) values (?, ?, ?, ?, ?, ?);"
                       << ac->event.uuid
                       << ac->event.event_type
                       << ac->event.io_id
                       << ac->event.io_state
                       << ac->event.event_raw
                       << ac->event.pic_uid;

                    string numdays = Utils::get_config_option("history_keep_days");
                    if (numdays == "")
                        numdays = "30"; // 30 days are kept by default

                    cDebugDom("history") << "Cleaning events older than " << numdays;

                    string q = "SELECT pic_uid FROM events WHERE created_at <= date('now', '-" + numdays + " days');";
                    db << q >> [&](string pic_uid)
                    {
                        string file = Utils::getCacheFile("push_pictures") + "/" + pic_uid + ".jpg";

                        cDebugDom("history") << "Deleting file: " << file;
                        if (!FileUtils::unlink(file))
                            cWarningDom("history") << "Failed to delete file: " << file;
                    };

                    q = "DELETE FROM events WHERE created_at <= date('now', '-" + numdays + " days');";
                    db << q;
                }
                catch (sqlite::sqlite_exception &e)
                {
                    cCriticalDom("history") << "SQL failed: " << e.get_code() << ": " << e.what()
                                            << " during " << e.get_sql();
                    cCriticalDom("history") << "SQLite error: " << sqlite3_errmsg(db.connection().get());
                }
                catch (exception &e)
                {
                    cCriticalDom("history") << "Failed to INSERT into db: " << e.what();
                }
            }
            else if (ac->action == HistWorkerAction::WorkerRead)
            {
                cDebugDom("history") << "Reading from SQLite db, page:" << ac->page << " per_page:" << ac->per_page;

                try
                {
                    int rowcount = 0;

                    db << "SELECT count(*) FROM events;"
                       >> [&](int count)
                    {
                        rowcount = count;
                    };

                    ac->total_count = rowcount;
                    ac->total_page = rowcount / ac->per_page + ((rowcount % ac->per_page) > 0?1:0);
                    int start = ac->page * ac->per_page;
                    int end = start + ac->per_page;

                    if (ac->page > ac->total_page ||
                        ac->page < 0)
                    {
                        cDebugDom("history") << "page out of range: total_count:" << ac->total_count << " total_page:" << ac->total_page;
                        ac->success = false;
                        ac->errMsg = "page is out of range";
                    }
                    else
                    {
                        string q = "SELECT uuid, created_at, event_type, io_id, io_state, event_raw, pic_uid FROM events ORDER BY created_at DESC LIMIT "
                            + Utils::to_string(start) + ", "
                            + Utils::to_string(end);

                        cDebugDom("history") << q;

                        db << q >> [&](string uuid, string created_at, int event_type, string io_id, string io_state, string event_raw, string pic_uid)
                        {
                            HistEvent ev;
                            ev.uuid = uuid;
                            ev.event_type = event_type;
                            ev.io_id = io_id;
                            ev.io_state = io_state;
                            ev.event_raw = event_raw;
                            ev.pic_uid = pic_uid;
                            ev.created_at = created_at;

                            ac->events.push_back(ev);
                        };
                    }

                    //wakeup the mainloop with the result
                    ac->handle->send();
                }
                catch (sqlite::sqlite_exception &e)
                {
                    cCriticalDom("history") << "SQL failed: " << e.get_code() << ": " << e.what()
                                            << " during " << e.get_sql();
                    cCriticalDom("history") << "SQLite error: " << sqlite3_errmsg(db.connection().get());
                }
                catch (exception &e)
                {
                    cCriticalDom("history") << "Failed to INSERT into db: " << e.what();
                }

            }
        }
    }

    cDebugDom("history") << "Exiting sqlite thread.";
}

