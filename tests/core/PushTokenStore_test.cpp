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

//A history database written by a server that predates push_format: its
//tokens must keep the notification format the store apps display.

#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "sqlite_modern_cpp.h"
#pragma GCC diagnostic pop

#include "HistLogger.h"
#include "JsonApi.h"
#include "NotifManager.h"
#include "PushTestUtil.h"
#include "libuvw.h"

using namespace Calaos;

namespace
{

std::string dbPath;
//Read right after the migration, before any case registers anything.
std::vector<PushToken> migrated;

bool pumpUntil(const std::function<bool()> &done)
{
    auto loop = uvw::Loop::getDefault();
    for (int i = 0; i < 400; i++)
    {
        if (done())
            return true;
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done();
}

std::vector<PushToken> storedTokens()
{
    auto done = std::make_shared<bool>(false);
    auto out = std::make_shared<std::vector<PushToken>>();
    HistLogger::Instance().getPushTokens(
        [done, out](bool, string, const vector<PushToken> &tokens)
        {
        *out = tokens;
        *done = true;
    });
    EXPECT_TRUE(pumpUntil([done]()
                          {
        return *done;
    }));
    return *out;
}

const PushToken *find(const std::vector<PushToken> &tokens, const std::string &t)
{
    for (const auto &p: tokens)
        if (p.token == t)
            return &p;
    return nullptr;
}

//HistLogger is a singleton that opens its database once: the old schema has
//to be on disk before anything touches it.
class OldDatabase: public ::testing::Environment
{
public:
    void SetUp() override
    {
        char tmpl[] = "/tmp/calaos_push1_hist_XXXXXX";
        ASSERT_NE(nullptr, ::mkdtemp(tmpl));
        base = tmpl;
        const std::string cfg = base + "/config";
        const std::string cache = base + "/cache";
        ASSERT_EQ(0, ::mkdir(cfg.c_str(), 0700));
        ASSERT_EQ(0, ::mkdir(cache.c_str(), 0700));
        Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                                 const_cast<char *>(cache.c_str()), true);
        //Debug lines are where a token would leak first.
        Utils::set_config_option("debug_level", "5");

        dbPath = cache + "/events.db";
        {
            sqlite::database db(dbPath);
            db << "CREATE TABLE push_tokens (id INTEGER PRIMARY KEY UNIQUE NOT NULL, "
                  "token TEXT, hw_type INTEGER, created_at DATETIME DEFAULT CURRENT_TIMESTAMP);";
            db << "INSERT INTO push_tokens (token, hw_type) values ('old-android', 2);";
            db << "INSERT INTO push_tokens (token, hw_type) values ('old-ios', 1);";
        }

        migrated = storedTokens();
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
    }

private:
    std::string base;
};

const auto *env = ::testing::AddGlobalTestEnvironment(new OldDatabase);

} //namespace

TEST(PushTokenStoreTest, MigrationAddsThePushFormatColumn)
{
    sqlite::database db(dbPath);
    int count = 0;
    db << "SELECT count(*) FROM pragma_table_info('push_tokens') WHERE name = 'push_format';" >> count;
    EXPECT_EQ(1, count);
}

TEST(PushTokenStoreTest, OldTokensKeepTheNotificationFormat)
{
    ASSERT_EQ(2u, migrated.size());
    const PushToken *a = find(migrated, "old-android");
    const PushToken *i = find(migrated, "old-ios");
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, i);
    EXPECT_EQ(HistLogger::PUSH_HW_ANDROID, a->hw_type);
    EXPECT_EQ(HistLogger::PUSH_FORMAT_NOTIFICATION, a->format);
    EXPECT_EQ(HistLogger::PUSH_HW_IOS, i->hw_type);
    EXPECT_EQ(HistLogger::PUSH_FORMAT_NOTIFICATION, i->format);
}

TEST(PushTokenStoreTest, ReRegisteringAnOldTokenUpdatesItsFormat)
{
    HistLogger::Instance().registerPushToken("old-android", HistLogger::PUSH_HW_ANDROID,
                                             HistLogger::PUSH_FORMAT_DATA);
    auto tokens = storedTokens();
    ASSERT_NE(nullptr, find(tokens, "old-android"));
    EXPECT_EQ(HistLogger::PUSH_FORMAT_DATA, find(tokens, "old-android")->format);
    EXPECT_EQ(1, std::count_if(tokens.begin(), tokens.end(),
                               [](const PushToken &t)
                               {
        return t.token == "old-android";
              }))
        << "re-registering must not duplicate the token";

    HistLogger::Instance().registerPushToken("old-android", HistLogger::PUSH_HW_ANDROID);
    tokens = storedTokens();
    ASSERT_NE(nullptr, find(tokens, "old-android"));
    EXPECT_EQ(HistLogger::PUSH_FORMAT_NOTIFICATION, find(tokens, "old-android")->format);
}

TEST(PushTokenStoreTest, NewTokenIsStoredWithItsFormat)
{
    HistLogger::Instance().registerPushToken("new-android", HistLogger::PUSH_HW_ANDROID,
                                             HistLogger::PUSH_FORMAT_DATA);
    const auto tokens = storedTokens();
    ASSERT_NE(nullptr, find(tokens, "new-android"));
    EXPECT_EQ(HistLogger::PUSH_FORMAT_DATA, find(tokens, "new-android")->format);
}

namespace
{

std::string oldDatabase(const std::string &name)
{
    const std::string path = std::filesystem::path(dbPath).parent_path().string() + "/" + name;
    sqlite::database db(path);
    db << PushTest::OldTokensSchema;
    db << "INSERT INTO push_tokens (token, hw_type) values ('ro-android', 2);";
    return path;
}

int formatColumns(sqlite::database &db)
{
    int count = 0;
    db << "SELECT count(*) FROM pragma_table_info('push_tokens') WHERE name = 'push_format';" >> count;
    return count;
}

} //namespace

TEST(PushTokenStoreTest, ASecondOpenOfAMigratedDatabaseChangesNothing)
{
    const std::string path = oldDatabase("second_open.db");
    {
        sqlite::database first(path);
        ASSERT_TRUE(PushTokenDb::prepare(first));
        PushToken t(string("ro-android"), HistLogger::PUSH_HW_ANDROID, HistLogger::PUSH_FORMAT_DATA);
        PushTokenDb::save(first, t, true);
    }

    PushTest::StdoutCapture capture;
    sqlite::database second(path);
    EXPECT_TRUE(PushTokenDb::prepare(second));
    const std::string log = capture.stop();

    EXPECT_EQ(1, formatColumns(second));
    EXPECT_EQ(std::string::npos, log.find("column added")) << log;
    const auto tokens = PushTokenDb::load(second, true);
    ASSERT_EQ(1u, tokens.size());
    EXPECT_EQ(HistLogger::PUSH_FORMAT_DATA, tokens[0].format);
}

TEST(PushTokenStoreTest, ALockOnTheNameColumnOnlyIsRetriedWithTheFormatKept)
{
    const std::string path = std::filesystem::path(dbPath).parent_path().string() + "/name_locked.db";
    {
        sqlite::database db(path);
        db << "CREATE TABLE push_tokens (id INTEGER PRIMARY KEY UNIQUE NOT NULL, token TEXT, hw_type INTEGER, "
              "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, push_format INTEGER NOT NULL DEFAULT 0);";
        db << "INSERT INTO push_tokens (token, hw_type, push_format) values ('cb-data', 2, 1);";
    }

    sqlite::database locker(path);
    locker << "BEGIN IMMEDIATE;";
    sqlite::database db(path);
    bool locked = false, hasName = true;
    EXPECT_TRUE(PushTokenDb::prepare(db, &locked, &hasName));
    EXPECT_TRUE(locked);
    EXPECT_FALSE(hasName);
    const auto whileLocked = PushTokenDb::load(db, true, hasName);
    ASSERT_EQ(1u, whileLocked.size());
    EXPECT_EQ(HistLogger::PUSH_FORMAT_DATA, whileLocked[0].format);
    locker << "COMMIT;";

    EXPECT_TRUE(PushTokenDb::prepare(db, &locked, &hasName));
    EXPECT_FALSE(locked);
    EXPECT_TRUE(hasName);
    const auto after = PushTokenDb::load(db, true, true);
    ASSERT_EQ(1u, after.size());
    EXPECT_EQ("cb-data", after[0].token);
    EXPECT_EQ(HistLogger::PUSH_FORMAT_DATA, after[0].format);
    EXPECT_EQ("", after[0].device_name);
    EXPECT_FALSE(after[0].approver);
}

TEST(PushTokenStoreTest, ATokenInsertedByAnOlderServerAfterMigrationGetsItsRegistrationAtTheNextOpen)
{
    const std::string path = oldDatabase("downgraded.db");
    {
        sqlite::database db(path);
        ASSERT_TRUE(PushTokenDb::prepare(db));
        //The INSERT of a server that knows push_format but not the name columns
        db << "INSERT INTO push_tokens (token, hw_type, push_format, created_at) "
              "values ('downgrade-ios', 1, 1, '2026-01-02 03:04:05');";
    }

    sqlite::database db(path);
    bool hasName = false;
    ASSERT_TRUE(PushTokenDb::prepare(db, nullptr, &hasName));
    ASSERT_TRUE(hasName);
    const auto tokens = PushTokenDb::load(db, true, true);
    const PushToken *t = find(tokens, "downgrade-ios");
    ASSERT_NE(nullptr, t);
    EXPECT_EQ("2026-01-02 03:04:05", t->registered_at);
    const PushToken *ro = find(tokens, "ro-android");
    ASSERT_NE(nullptr, ro);
    EXPECT_FALSE(ro->registered_at.empty());

    //A registration fills a row the backfill could not, and only once
    db << "UPDATE push_tokens SET registered_at = '', created_at = NULL WHERE token = 'downgrade-ios';";
    PushTokenDb::save(db, PushToken(string("downgrade-ios"), 1, 1), true, true);
    const std::string first = find(PushTokenDb::load(db, true, true), "downgrade-ios")->registered_at;
    EXPECT_FALSE(first.empty());
    db << "UPDATE push_tokens SET registered_at = '2020-01-01 00:00:00' WHERE token = 'downgrade-ios';";
    PushTokenDb::save(db, PushToken(string("downgrade-ios"), 1, 1), true, true);
    EXPECT_EQ("2020-01-01 00:00:00", find(PushTokenDb::load(db, true, true), "downgrade-ios")->registered_at);
}

TEST(PushTokenStoreTest, AReadOnlyMigratedDatabaseKeepsItsNames)
{
    const std::string path = oldDatabase("read_only_migrated.db");
    {
        sqlite::database db(path);
        ASSERT_TRUE(PushTokenDb::prepare(db));
        //created_at NULL: the backfill has nothing to copy, it must not retry a write
        db << "INSERT INTO push_tokens (token, hw_type, created_at) values ('null-date', 2, NULL);";
    }
    sqlite::sqlite_config ro;
    ro.flags = sqlite::OpenFlags::READONLY;
    sqlite::database db(path, ro);
    bool locked = true, hasName = false;
    EXPECT_TRUE(PushTokenDb::prepare(db, &locked, &hasName));
    EXPECT_FALSE(locked);
    EXPECT_TRUE(hasName);

    //A row left to fill cannot be written here: the names stay all the same
    {
        sqlite::database rw(path);
        rw << "INSERT INTO push_tokens (token, hw_type, created_at) values ('ro-downgrade', 2, '2026-01-02 03:04:05');";
    }
    sqlite::database again(path, ro);
    PushTest::StdoutCapture capture;
    locked = true;
    hasName = false;
    EXPECT_TRUE(PushTokenDb::prepare(again, &locked, &hasName));
    const std::string log = capture.stop();
    EXPECT_FALSE(locked);
    EXPECT_TRUE(hasName);
    EXPECT_NE(std::string::npos, log.find("registered_at not backfilled")) << log;
}

TEST(PushTokenStoreTest, AReadOnlyOldDatabaseIsStillReadAndWritten)
{
    const std::string path = oldDatabase("read_only.db");
    sqlite::sqlite_config ro;
    ro.flags = sqlite::OpenFlags::READONLY;
    sqlite::database db(path, ro);

    EXPECT_FALSE(PushTokenDb::prepare(db));
    EXPECT_EQ(0, formatColumns(db));
    const auto tokens = PushTokenDb::load(db, false);
    ASSERT_EQ(1u, tokens.size());
    EXPECT_EQ("ro-android", tokens[0].token);
    EXPECT_EQ(HistLogger::PUSH_FORMAT_NOTIFICATION, tokens[0].format);

    sqlite::database rw(path);
    PushTokenDb::save(rw, PushToken(string("ro-new"), HistLogger::PUSH_HW_IOS, HistLogger::PUSH_FORMAT_DATA), false);
    PushTokenDb::save(rw, PushToken(string("ro-android"), HistLogger::PUSH_HW_ANDROID, HistLogger::PUSH_FORMAT_DATA), false);
    EXPECT_EQ(2u, PushTokenDb::load(rw, false).size());
}

TEST(PushTokenStoreTest, AnUnknownPushFormatIsWarnedOncePerProcess)
{
    PushTest::StdoutCapture capture;
    EXPECT_EQ(HistLogger::PUSH_FORMAT_NOTIFICATION, JsonApi::pushFormatFromString("Data"));
    EXPECT_EQ(HistLogger::PUSH_FORMAT_NOTIFICATION, JsonApi::pushFormatFromString("silent"));
    EXPECT_EQ(HistLogger::PUSH_FORMAT_NOTIFICATION, JsonApi::pushFormatFromString(""));
    EXPECT_EQ(HistLogger::PUSH_FORMAT_NOTIFICATION, JsonApi::pushFormatFromString("notification"));
    EXPECT_EQ(HistLogger::PUSH_FORMAT_DATA, JsonApi::pushFormatFromString("data"));
    EXPECT_EQ(HistLogger::PUSH_FORMAT_NOTIFICATION, JsonApi::pushFormatFromString("Data"));
    const std::string log = capture.stop();

    size_t count = 0;
    for (size_t at = log.find("unknown push_format"); at != std::string::npos;
         at = log.find("unknown push_format", at + 1))
        count++;
    EXPECT_EQ(1u, count) << log;
    EXPECT_EQ(std::string::npos, log.find("silent")) << "the value itself is not logged";
}

TEST(PushTokenStoreTest, ASendPostsEveryTokenAndLogsNone)
{
    const char *dataToken = "jeton-data-givre-solstice";
    const char *legacyToken = "jeton-legacy-givre-solstice";
    HistLogger::Instance().registerPushToken(dataToken, HistLogger::PUSH_HW_ANDROID,
                                             HistLogger::PUSH_FORMAT_DATA);
    HistLogger::Instance().registerPushToken(legacyToken, HistLogger::PUSH_HW_ANDROID);

    PushTest::HttpPeer peer;
    NotifManager::setPushServiceUrl(peer.url());

    PushTest::StdoutCapture capture;
    auto sent = std::make_shared<bool>(false);
    NotifManager::Instance().sendPushNotification("Door open", "ev-1", [sent]()
                                                  {
        *sent = true;
    });
    PushTest::pumpUntil([sent]()
                        {
        return *sent;
    });
    const std::string log = capture.stop();

    ASSERT_TRUE(*sent);
    const std::string req = peer.request();
    EXPECT_NE(std::string::npos, req.find("POST /api/push"));
    EXPECT_NE(std::string::npos, req.find(dataToken));
    EXPECT_NE(std::string::npos, req.find(legacyToken));
    EXPECT_NE(std::string::npos, req.find(R"("android":{"priority":"high"})"));
    EXPECT_NE(std::string::npos, log.find("Sending notif to service")) << "nothing was captured";
    EXPECT_EQ(std::string::npos, log.find(dataToken)) << log;
    EXPECT_EQ(std::string::npos, log.find(legacyToken)) << log;
    EXPECT_EQ(std::string::npos, log.find("old-android")) << log;
}
