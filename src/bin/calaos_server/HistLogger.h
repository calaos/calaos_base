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
    PushToken(string &&t, int hw):
        token(t), hw_type(hw) {}
    PushToken() {}
    string token;
    int hw_type = 0;
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

    //Add a new device token to the db
    void registerPushToken(string token, int push_hw);

    //Get all push token devices
    void getPushTokens(std::function<void(bool success, string errorMsg, const vector<PushToken> &tokens)> callback);

private:
    HistLogger();

    void sqliteWorker();

    string dbname;

    std::atomic_bool done{false};
    std::thread sqliteThread;
    ThreadedQueue<std::shared_ptr<HistWorkerAction>> eventQueue;
};

#endif
