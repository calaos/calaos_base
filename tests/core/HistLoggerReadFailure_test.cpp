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

//events.db locked while the server reads it (an update, a second process at
//startup): every history read must still be answered, as a failure, or the app
//waits for that answer forever. Nothing logged about the failure may carry
//what the events hold or the uuid a client asked for.

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <filesystem>

#include "PushTestUtil.h"

using namespace Calaos;
using namespace PushTest;

namespace
{

//Never produced by anything else in this binary.
const char *const kSecret = "evlog-contenu-givre-lanterne";
const char *const kAskedUuid = "evlog-uuid-brume-falaise";

std::string base;
std::string dbPath;
std::string seededUuid;

struct EventsAnswer
{
    bool received = false;
    bool success = false;
    std::string error;
    int totalCount = -1;
};

EventsAnswer readPage(int page, int perPage)
{
    auto a = std::make_shared<EventsAnswer>();
    HistLogger::Instance().getEvents(page, perPage,
                                     [a](bool success, string errorMsg, const vector<HistEvent> &, int, int total)
                                     {
        a->received = true;
        a->success = success;
        a->error = errorMsg;
        a->totalCount = total;
    });
    pumpUntil([a]()
              {
        return a->received;
    });
    return *a;
}

EventsAnswer readOne(const std::string &uuid)
{
    auto a = std::make_shared<EventsAnswer>();
    HistLogger::Instance().getEvent(uuid, [a](bool success, string errorMsg, const HistEvent &)
                                    {
        a->received = true;
        a->success = success;
        a->error = errorMsg;
    });
    pumpUntil([a]()
              {
        return a->received;
    });
    return *a;
}

//A second connection holding the write lock: the worker's statements fail
//with SQLITE_BUSY, the server sets no busy timeout.
class Locker
{
public:
    explicit Locker(const std::string &path)
    {
        if (sqlite3_open(path.c_str(), &db) == SQLITE_OK)
            held = sqlite3_exec(db, "BEGIN EXCLUSIVE;", nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    ~Locker()
    {
        if (held)
            sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
        sqlite3_close(db);
    }

    bool held = false;

private:
    sqlite3 *db = nullptr;
};

class HistoryDb: public ::testing::Environment
{
public:
    void SetUp() override
    {
        base = makeTestDirs();
        ASSERT_FALSE(base.empty());
        dbPath = base + "/cache/events.db";

        for (int i = 0; i < 3; i++)
        {
            HistEvent ev = HistEvent::create();
            ev.event_type = 3;
            ev.io_id = "evlog_io_" + std::to_string(i);
            ev.io_state = "true";
            ev.event_raw = "{}";
            if (i == 0)
                seededUuid = ev.uuid;
            HistLogger::Instance().appendEvent(ev);
        }
        ASSERT_EQ(3, readPage(0, 10).totalCount);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
    }
};

const auto *env = ::testing::AddGlobalTestEnvironment(new HistoryDb);

} //namespace

TEST(HistLoggerReadFailureTest, EveryReaderIsAnsweredWhileTheDatabaseIsLocked)
{
    EventsAnswer page, deep, one;
    TokensAnswer tokens;
    {
        Locker lock(dbPath);
        ASSERT_TRUE(lock.held);

        page = readPage(0, 10);
        deep = readPage(2, 1);
        one = readOne(seededUuid);
        tokens = readTokens();
    }

    for (const EventsAnswer *a: { &page, &deep, &one })
    {
        EXPECT_TRUE(a->received) << "a history read was never answered";
        EXPECT_FALSE(a->success);
        EXPECT_NE(std::string::npos, a->error.find("database is locked")) << a->error;
    }
    EXPECT_TRUE(tokens.received);
    EXPECT_FALSE(tokens.success);

    //The worker goes on once the lock is gone.
    const EventsAnswer after = readPage(0, 10);
    EXPECT_TRUE(after.received);
    EXPECT_TRUE(after.success);
    EXPECT_EQ(3, after.totalCount);
    EXPECT_TRUE(readOne(seededUuid).success);
}

TEST(HistLoggerReadFailureTest, AFailureLogsNeitherTheEventNorTheUuidAsked)
{
    StdoutCapture capture;
    EventsAnswer one;
    {
        Locker lock(dbPath);
        ASSERT_TRUE(lock.held);

        HistEvent ev = HistEvent::create();
        ev.event_type = 21;
        ev.event_raw = std::string("{\"message\":\"") + kSecret + "\"}";
        HistLogger::Instance().appendEvent(ev);
        //Answered after the append: the worker runs its queue in order.
        one = readOne(kAskedUuid);
    }
    const std::string log = capture.stop();

    EXPECT_TRUE(one.received);
    EXPECT_NE(std::string::npos, log.find("database is locked")) << "no failure was logged";
    EXPECT_EQ(std::string::npos, log.find(kSecret)) << log;
    EXPECT_EQ(std::string::npos, log.find(kAskedUuid)) << log;
}
