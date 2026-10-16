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
#ifndef HISTLOGGER_H
#define HISTLOGGER_H

#include <thread>
#include "ThreadedQueue.h"
#include "Calaos.h"
#include "Timer.h"

namespace uvw {
//Forward declare classes here to prevent long build time
//because of uvw.hpp being header only
class AsyncHandle;
}

namespace sqlite
{
class database;
}

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
    string created_at;

    //init a new event with filled parameters
    static HistEvent create();

    Json toJson() const;
};

class HistWorkerAction;

class PushToken
{
public:
    PushToken(string &&t, int hw, int fmt = 0):
        token(t),
        hw_type(hw),
        format(fmt)
    {
    }
    PushToken() {}
    string token;
    int hw_type = 0;
    int format = 0;
    string created_at;
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

    //read events back from db async
    void getEvents(int page, int per_page,
                   std::function<void(bool success, string errorMsg, const vector<HistEvent> &events, int total_page, int total_count)> callback);
    void getEvent(string uuid,
                  std::function<void(bool success, string errorMsg, const HistEvent &event)> callback);

    enum {
        PUSH_HW_NONE = 0,
        PUSH_HW_IOS,
        PUSH_HW_ANDROID,
    };

    //What the app registered for. The store apps (3.1.x) send no format and
    //cannot display a data-only FCM message.
    enum
    {
        PUSH_FORMAT_NOTIFICATION = 0,
        PUSH_FORMAT_DATA,
    };

    //Add a new device token to the db, or refresh it and update its format
    void registerPushToken(string token, int push_hw, int push_format = PUSH_FORMAT_NOTIFICATION);

    //Get all push token devices
    void getPushTokens(std::function<void(bool success, string errorMsg, const vector<PushToken> &tokens)> callback);

private:
    HistLogger();

    void sqliteWorker();
    //Drains the queue once the database could not be opened.
    void refuseAll();

    string dbname;

    std::atomic_bool done{false};
    std::thread sqliteThread;
    ThreadedQueue<std::shared_ptr<HistWorkerAction>> eventQueue;
};

//push_tokens storage, run by the history worker thread. Throws sqlite errors,
//except prepare().
namespace PushTokenDb
{
//Creates the table and adds push_format to tables from older servers. False
//when the column is still missing (database locked or read only): tokens are
//then stored and read in the notification format until a later start.
bool prepare(sqlite::database &db);
void save(sqlite::database &db, const PushToken &token, bool hasFormat);
vector<PushToken> load(sqlite::database &db, bool hasFormat);
//Tokens not registered again for 30 days.
void purge(sqlite::database &db);
} //namespace PushTokenDb

#endif
