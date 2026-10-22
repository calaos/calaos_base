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
#include "HistLogger.h"
#include "sole.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "sqlite_modern_cpp.h"
#pragma GCC diagnostic pop

#include "libuvw.h"

#define DB_CREATE_SQL_EVENTS "CREATE TABLE IF NOT EXISTS events (" \
    "id INTEGER PRIMARY KEY UNIQUE NOT NULL, " \
    "uuid TEXT, " \
    "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, " \
    "event_type INTEGER, " \
    "io_id TEXT, " \
    "io_state TEXT, " \
    "event_raw TEXT, " \
    "pic_uid TEXT);"

//Serves the paginated read and the retention purge. id keeps the order the
//unindexed sort gave to events logged within the same second: oldest first.
#define DB_CREATE_SQL_EVENTS_INDEX "CREATE INDEX IF NOT EXISTS events_created_at " \
                                   "ON events (created_at DESC, id);"

#define DB_CREATE_SQL_TOKENS "CREATE TABLE IF NOT EXISTS push_tokens ("        \
                             "id INTEGER PRIMARY KEY UNIQUE NOT NULL, "        \
                             "token TEXT, "                                    \
                             "hw_type INTEGER, "                               \
                             "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, " \
                             "push_format INTEGER NOT NULL DEFAULT 0);"

bool PushTokenDb::prepare(sqlite::database &db)
{
    try
    {
        db << DB_CREATE_SQL_TOKENS;

        int count = 0;
        db << "SELECT count(*) FROM pragma_table_info('push_tokens') WHERE name = 'push_format';" >> count;
        if (count > 0)
            return true;

        //Tables from older servers: their tokens all belong to apps expecting
        //the notification format, the default.
        db << "ALTER TABLE push_tokens ADD COLUMN push_format INTEGER NOT NULL DEFAULT 0;";
        cInfoDom("history") << "push_tokens: push_format column added";
        return true;
    }
    catch (sqlite::sqlite_exception &e)
    {
        cWarningDom("history") << "push_tokens: cannot add push_format (" << e.get_code() << ": " << e.what()
                               << "), every token is served in the notification format";
        return false;
    }
}

void PushTokenDb::save(sqlite::database &db, const PushToken &token, bool hasFormat)
{
    bool exists = false;
    db << "SELECT id FROM push_tokens WHERE token = ?;" << token.token >> [&](int)
    {
        exists = true;
    };

    if (exists && hasFormat)
        db << "UPDATE push_tokens SET created_at = DATETIME('now'), push_format = ? WHERE token = ?;"
           << token.format << token.token;
    else if (exists)
        db << "UPDATE push_tokens SET created_at = DATETIME('now') WHERE token = ?;" << token.token;
    else if (hasFormat)
        db << "INSERT INTO push_tokens (token, hw_type, push_format) values (?, ?, ?);"
           << token.token << token.hw_type << token.format;
    else
        db << "INSERT INTO push_tokens (token, hw_type) values (?, ?);" << token.token << token.hw_type;
}

vector<PushToken> PushTokenDb::load(sqlite::database &db, bool hasFormat)
{
    vector<PushToken> tokens;
    if (hasFormat)
    {
        db << "SELECT token, hw_type, push_format FROM push_tokens;" >> [&](string token, int hw_type, int push_format)
        {
            tokens.emplace_back(std::move(token), hw_type, push_format);
        };
    }
    else
    {
        db << "SELECT token, hw_type FROM push_tokens;" >> [&](string token, int hw_type)
        {
            tokens.emplace_back(std::move(token), hw_type);
        };
    }
    return tokens;
}

void PushTokenDb::purge(sqlite::database &db)
{
    db << "DELETE FROM push_tokens WHERE created_at <= date('now', '-30 days');";
}

class HistWorkerAction
{
public:
    enum
    {
        WorkerNoAction,
        WorkerAppend,
        WorkerRead,
        WorkerReadSingle,
        WorkerTokenRegister,
        WorkerTokenGetAll,
    };

    int action = WorkerNoAction;
    HistEvent event;
    std::shared_ptr<uvw::AsyncHandle> handle;
    vector<HistEvent> events;
    bool success = true;
    string errMsg;
    int page, per_page, total_count, total_page;
    string uuid;
    PushToken token;
    vector<PushToken> tokens;
};

namespace
{

bool prepareEvents(sqlite::database &db)
{
    try
    {
        db << DB_CREATE_SQL_EVENTS;
        db << DB_CREATE_SQL_EVENTS_INDEX;
        return true;
    }
    catch (const sqlite::sqlite_exception &e)
    {
        cCriticalDom("history") << "Cannot set up the events table: " << e.get_code() << ": " << e.what()
                                << ", retried on the next request";
        return false;
    }
}

//Events dated before the day history_keep_days ago go, with the pictures of
//their push notifications. New events are dated now, so nothing becomes old
//enough until that day changes: one purge per cutoff deletes exactly what a
//purge after every event did.
void purgeEvents(sqlite::database &db, string &lastCutoff)
{
    string numdays = Utils::get_config_option("history_keep_days");
    if (numdays == "")
        numdays = "30";

    //'' when sqlite rejects the modifier: no event is old enough then.
    string cutoff;
    db << "SELECT ifnull(date('now', ?), '');"
       << "-" + numdays + " days" >>
        cutoff;
    if (cutoff == lastCutoff)
        return;

    cDebugDom("history") << "Cleaning events older than " << numdays << " days";

    if (!cutoff.empty())
    {
        db << "SELECT pic_uid FROM events WHERE created_at <= ? AND pic_uid != '';" << cutoff >> [&](string pic_uid)
        {
            string file = Utils::getCacheFile("push_pictures") + "/" + pic_uid + ".jpg";

            cDebugDom("history") << "Deleting file: " << file;
            if (!FileUtils::unlink(file))
                cWarningDom("history") << "Failed to delete file: " << file;
        };

        db << "DELETE FROM events WHERE created_at <= ?;" << cutoff;
    }

    lastCutoff = cutoff;
}

void readPage(sqlite::database &db, HistWorkerAction &ac)
{
    int rowcount = 0;
    db << "SELECT count(*) FROM events;" >> rowcount;

    ac.total_count = rowcount;
    ac.total_page = rowcount / ac.per_page + ((rowcount % ac.per_page) > 0 ? 1 : 0);

    if (ac.page > ac.total_page || ac.page < 0)
    {
        cDebugDom("history") << "page out of range: total_count:" << ac.total_count << " total_page:" << ac.total_page;
        ac.success = false;
        ac.errMsg = "page is out of range";
        return;
    }

    const sqlite_int64 start = sqlite_int64(ac.page) * ac.per_page;
    db << "SELECT uuid, created_at, event_type, io_id, io_state, event_raw, pic_uid FROM events "
          "ORDER BY created_at DESC, id LIMIT ?, ?;"
       << start << sqlite_int64(ac.per_page) >>
        [&](string uuid, string created_at, int event_type, string io_id, string io_state, string event_raw, string pic_uid)
    {
        HistEvent ev;
        ev.uuid = uuid;
        ev.event_type = event_type;
        ev.io_id = io_id;
        ev.io_state = io_state;
        ev.event_raw = event_raw;
        ev.pic_uid = pic_uid;
        ev.created_at = created_at;

        ac.events.push_back(ev);
    };
}

void readSingle(sqlite::database &db, HistWorkerAction &ac)
{
    db << "SELECT uuid, created_at, event_type, io_id, io_state, event_raw, pic_uid FROM events WHERE uuid = ?"
       << ac.uuid >>
        [&](string uuid, string created_at, int event_type, string io_id, string io_state, string event_raw, string pic_uid)
    {
        ac.event.uuid = uuid;
        ac.event.event_type = event_type;
        ac.event.io_id = io_id;
        ac.event.io_state = io_state;
        ac.event.event_raw = event_raw;
        ac.event.pic_uid = pic_uid;
        ac.event.created_at = created_at;
    };

    if (ac.uuid != ac.event.uuid)
    {
        ac.success = false;
        ac.errMsg = "uuid not found";
    }
}

void insertEvent(sqlite::database &db, const HistWorkerAction &ac, string &lastCutoff)
{
    cDebugDom("history") << "Write to SQLite db: " << ac.event.io_id << " > " << ac.event.io_state;

    db << "INSERT INTO events (uuid, event_type, io_id, io_state, event_raw, pic_uid) values (?, ?, ?, ?, ?, ?);"
       << ac.event.uuid
       << ac.event.event_type
       << ac.event.io_id
       << ac.event.io_state
       << ac.event.event_raw
       << ac.event.pic_uid;

    purgeEvents(db, lastCutoff);
}

} //namespace

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

void HistLogger::getEvent(string uuid,
                          std::function<void(bool success, string errorMsg, const HistEvent &event)> callback)
{
    std::shared_ptr<HistWorkerAction> a = std::make_shared<HistWorkerAction>();
    a->action = HistWorkerAction::WorkerReadSingle;
    a->handle = uvw::Loop::getDefault()->resource<uvw::AsyncHandle>();
    a->uuid = uuid;

    a->handle->once<uvw::AsyncEvent>([callback, a](const auto &, uvw::AsyncHandle &h)
    {
        callback(a->success, a->errMsg, a->event);
        h.close();
    });

    eventQueue.push(a);
}

void HistLogger::registerPushToken(string token, int push_hw, int push_format)
{
    std::shared_ptr<HistWorkerAction> a = std::make_shared<HistWorkerAction>();
    a->action = HistWorkerAction::WorkerTokenRegister;
    a->token.token = token;
    a->token.hw_type = push_hw;
    a->token.format = push_format;
    eventQueue.push(a);
}

void HistLogger::getPushTokens(std::function<void(bool success, string errorMsg, const vector<PushToken> &event)> callback)
{
    std::shared_ptr<HistWorkerAction> a = std::make_shared<HistWorkerAction>();
    a->action = HistWorkerAction::WorkerTokenGetAll;
    a->handle = uvw::Loop::getDefault()->resource<uvw::AsyncHandle>();

    a->handle->once<uvw::AsyncEvent>([callback, a](const auto &, uvw::AsyncHandle &h)
    {
        callback(a->success, a->errMsg, a->tokens);
        h.close();
    });

    eventQueue.push(a);
}

void HistLogger::refuseAll()
{
    while (!done)
    {
        std::shared_ptr<HistWorkerAction> ac;
        if (!eventQueue.waitPop(ac))
            continue;

        //Only the readers hold a handle, and they wait on it.
        if (ac->handle)
        {
            ac->success = false;
            ac->errMsg = "history is disabled";
            ac->handle->send();
        }
    }
}

void HistLogger::sqliteWorker()
{
    cInfoDom("history") << "History logger, setup database to " << dbname;

    //An exception out of this thread is std::terminate for the whole server.
    std::unique_ptr<sqlite::database> opened;
    try
    {
        opened = std::make_unique<sqlite::database>(dbname);
    }
    catch (const exception &e)
    {
        cCriticalDom("history") << "Cannot open the history database " << dbname
                                << ", history is disabled: " << e.what();
        refuseAll();
        return;
    }

    sqlite::database &db = *opened;

    bool eventsReady = prepareEvents(db);
    bool pushFormat = PushTokenDb::prepare(db);
    string lastCutoff;

    while (!done)
    {
        std::shared_ptr<HistWorkerAction> ac;
        if (!eventQueue.waitPop(ac))
            continue;

        if (!eventsReady)
            eventsReady = prepareEvents(db);

        //Never e.get_sql(): sqlite_modern_cpp expands it with the bound values,
        //event contents and push tokens included.
        try
        {
            if (ac->action == HistWorkerAction::WorkerAppend)
                insertEvent(db, *ac, lastCutoff);
            else if (ac->action == HistWorkerAction::WorkerRead)
            {
                cDebugDom("history") << "Reading from SQLite db, page:" << ac->page << " per_page:" << ac->per_page;
                readPage(db, *ac);
            }
            else if (ac->action == HistWorkerAction::WorkerReadSingle)
            {
                cDebugDom("history") << "Reading one event from SQLite db";
                readSingle(db, *ac);
            }
            else if (ac->action == HistWorkerAction::WorkerTokenRegister)
            {
                cDebugDom("history") << "Write push token to SQLite db";

                try
                {
                    PushTokenDb::save(db, ac->token, pushFormat);
                    PushTokenDb::purge(db);
                }
                catch (sqlite::sqlite_exception &e)
                {
                    cCriticalDom("history") << "Failed to save a push token: " << e.get_code() << ": " << e.what();
                }
            }
            else if (ac->action == HistWorkerAction::WorkerTokenGetAll)
            {
                cDebugDom("history") << "Reading from SQLite db all tokens";

                try
                {
                    ac->tokens = PushTokenDb::load(db, pushFormat);
                }
                catch (sqlite::sqlite_exception &e)
                {
                    cCriticalDom("history") << "Failed to read the push tokens: " << e.get_code() << ": " << e.what();
                    ac->success = false;
                    ac->errMsg = "push tokens unavailable";
                }

                //A failed purge must not hold back a push.
                if (ac->success)
                {
                    try
                    {
                        PushTokenDb::purge(db);
                    }
                    catch (sqlite::sqlite_exception &e)
                    {
                        cWarningDom("history") << "Failed to purge old push tokens: " << e.get_code() << ": " << e.what();
                    }
                }
            }
        }
        catch (const sqlite::sqlite_exception &e)
        {
            cCriticalDom("history") << "SQL failed: " << e.get_code() << ": " << e.what();
            ac->success = false;
            ac->errMsg = string("history unavailable: ") + e.what();
        }
        catch (const exception &e)
        {
            cCriticalDom("history") << "History request failed: " << e.what();
            ac->success = false;
            ac->errMsg = "history unavailable";
        }

        //Readers wait on their handle: without this send the client never
        //gets an answer.
        if (ac->handle)
        {
            if (!ac->success)
                ac->events.clear();
            ac->handle->send();
        }
    }

    cDebugDom("history") << "Exiting sqlite thread.";
}
