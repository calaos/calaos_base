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
    //Wide bound: it only ever elapses when the worker is lost, a loaded host
    //is not that.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline)
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
        EXPECT_TRUE(PushTest::settleHistory()) << "the history worker did not drain";
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

/******************************************************************************
 * push_key
 ******************************************************************************/

namespace
{

const std::string KeyA(64, 'a');
const std::string KeyB(64, 'b');
const std::string KeyC = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

//push_tokens as the servers before push_key left it, one approver enrolled
std::string phaseTwoDatabase(const std::string &name)
{
    const std::string path = std::filesystem::path(dbPath).parent_path().string() + "/" + name;
    sqlite::database db(path);
    db << "CREATE TABLE push_tokens (id INTEGER PRIMARY KEY UNIQUE NOT NULL, token TEXT, hw_type INTEGER, "
          "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, push_format INTEGER NOT NULL DEFAULT 0, "
          "device_name TEXT NOT NULL DEFAULT '', approver INTEGER NOT NULL DEFAULT 0, "
          "registered_at TEXT NOT NULL DEFAULT '');";
    db << "INSERT INTO push_tokens (token, hw_type, push_format, device_name, approver, registered_at) "
          "values ('p2-approver', 2, 1, 'Pixel', 1, '2026-01-02 03:04:05');";
    return path;
}

PushToken withKey(const char *token, const std::string &key, int hw = HistLogger::PUSH_HW_ANDROID)
{
    PushToken t(string(token), hw, HistLogger::PUSH_FORMAT_DATA);
    t.push_key = key;
    return t;
}

std::string keyOf(sqlite::database &db, const char *token)
{
    const auto tokens = PushTokenDb::load(db, true, true, true);
    const PushToken *t = find(tokens, token);
    return t ? t->push_key : std::string("<missing>");
}

} //namespace

TEST(PushTokenStoreTest, MigrationAddsThePushKeyColumnToAPhaseTwoBase)
{
    const std::string path = phaseTwoDatabase("phase_two.db");
    sqlite::database db(path);
    PushTest::StdoutCapture capture;
    bool locked = true, hasName = false, hasKey = false;
    EXPECT_TRUE(PushTokenDb::prepare(db, &locked, &hasName, &hasKey));
    const std::string log = capture.stop();
    EXPECT_FALSE(locked);
    EXPECT_TRUE(hasName);
    EXPECT_TRUE(hasKey);
    EXPECT_NE(std::string::npos, log.find("push_key column added")) << log;

    const auto tokens = PushTokenDb::load(db, true, true, true);
    ASSERT_EQ(1u, tokens.size());
    EXPECT_EQ("", tokens[0].push_key);
    EXPECT_EQ("Pixel", tokens[0].device_name);
    EXPECT_TRUE(tokens[0].approver);

    sqlite::database again(path);
    PushTest::StdoutCapture second;
    EXPECT_TRUE(PushTokenDb::prepare(again, nullptr, nullptr, &hasKey));
    EXPECT_EQ(std::string::npos, second.stop().find("column added"));
    EXPECT_TRUE(hasKey);

    //The singleton's own base, created with the column
    sqlite::database own(dbPath);
    int count = 0;
    own << "SELECT count(*) FROM pragma_table_info('push_tokens') WHERE name = 'push_key';" >> count;
    EXPECT_EQ(1, count);
}

TEST(PushTokenStoreTest, AnApproverTakesAFirstKeyThenKeepsIt)
{
    sqlite::database db(phaseTwoDatabase("frozen_key.db"));
    ASSERT_TRUE(PushTokenDb::prepare(db));

    //Enrolled before keys existed: the first one is taken
    PushTokenDb::save(db, withKey("p2-approver", KeyA), true, true, true);
    EXPECT_EQ(KeyA, keyOf(db, "p2-approver"));
    PushTokenDb::save(db, withKey("p2-approver", KeyB), true, true, true);
    EXPECT_EQ(KeyA, keyOf(db, "p2-approver")) << "frozen once set";
    PushTokenDb::save(db, withKey("p2-approver", ""), true, true, true);
    EXPECT_EQ(KeyA, keyOf(db, "p2-approver"));

    //Freed by approver_removed, it takes a new key again
    PushTokenDb::setApprover(db, "p2-approver", false);
    PushTokenDb::save(db, withKey("p2-approver", KeyB), true, true, true);
    EXPECT_EQ(KeyB, keyOf(db, "p2-approver"));

    //A phone that is not an approver replaces its key, a keyless request leaves it
    PushTokenDb::save(db, withKey("p3-phone", KeyA, HistLogger::PUSH_HW_IOS), true, true, true);
    EXPECT_EQ(KeyA, keyOf(db, "p3-phone"));
    PushTokenDb::save(db, withKey("p3-phone", KeyC, HistLogger::PUSH_HW_IOS), true, true, true);
    EXPECT_EQ(KeyC, keyOf(db, "p3-phone"));
    PushTokenDb::save(db, withKey("p3-phone", "", HistLogger::PUSH_HW_IOS), true, true, true);
    EXPECT_EQ(KeyC, keyOf(db, "p3-phone"));

    //Enrolled with its key: frozen from the start
    PushTokenDb::setApprover(db, "p3-phone", true);
    PushTokenDb::save(db, withKey("p3-phone", KeyB, HistLogger::PUSH_HW_IOS), true, true, true);
    EXPECT_EQ(KeyC, keyOf(db, "p3-phone"));

    //Without the column nothing is written nor read
    PushTokenDb::save(db, withKey("p3-nocolumn", KeyA), true, true, false);
    EXPECT_EQ("", find(PushTokenDb::load(db, true, true, false), "p3-nocolumn")->push_key);
    EXPECT_EQ("", keyOf(db, "p3-nocolumn"));
}

TEST(PushTokenStoreTest, ALockOnThePushKeyColumnOnlyKeepsTheNames)
{
    const std::string path = phaseTwoDatabase("key_locked.db");
    sqlite::database locker(path);
    locker << "BEGIN IMMEDIATE;";
    sqlite::database db(path);
    bool locked = false, hasName = false, hasKey = true;
    PushTest::StdoutCapture capture;
    EXPECT_TRUE(PushTokenDb::prepare(db, &locked, &hasName, &hasKey));
    const std::string log = capture.stop();
    EXPECT_TRUE(locked);
    EXPECT_TRUE(hasName);
    EXPECT_FALSE(hasKey);
    EXPECT_NE(std::string::npos, log.find("cannot add push_key")) << log;
    EXPECT_NE(std::string::npos, log.find("without a push key")) << log;
    const auto whileLocked = PushTokenDb::load(db, true, hasName, hasKey);
    ASSERT_EQ(1u, whileLocked.size());
    EXPECT_EQ("Pixel", whileLocked[0].device_name);
    locker << "COMMIT;";

    EXPECT_TRUE(PushTokenDb::prepare(db, &locked, &hasName, &hasKey));
    EXPECT_FALSE(locked);
    EXPECT_TRUE(hasKey);
}

TEST(PushTokenStoreTest, AReadOnlyPhaseTwoBaseKeepsItsNamesWithoutKeys)
{
    const std::string path = phaseTwoDatabase("read_only_phase_two.db");
    sqlite::sqlite_config ro;
    ro.flags = sqlite::OpenFlags::READONLY;
    sqlite::database db(path, ro);
    bool locked = true, hasName = false, hasKey = true;
    EXPECT_TRUE(PushTokenDb::prepare(db, &locked, &hasName, &hasKey));
    EXPECT_FALSE(locked);
    EXPECT_TRUE(hasName);
    EXPECT_FALSE(hasKey);
    const auto tokens = PushTokenDb::load(db, true, hasName, hasKey);
    ASSERT_EQ(1u, tokens.size());
    EXPECT_EQ("Pixel", tokens[0].device_name);
    EXPECT_EQ("", tokens[0].push_key);
}

TEST(PushTokenStoreTest, AnUnusablePushKeyIsIgnoredAndWarnedOnceWithoutItsValue)
{
    PushTest::StdoutCapture capture;
    EXPECT_EQ(KeyC, JsonApi::pushKeyFromString(KeyC));
    EXPECT_EQ("", JsonApi::pushKeyFromString(""));
    const std::string upper = "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F";
    for (const std::string &bad: { upper, KeyC.substr(2), KeyC + "00", std::string(63, 'a') + "g", std::string(64, ' ') })
        EXPECT_EQ("", JsonApi::pushKeyFromString(bad)) << bad;
    const std::string log = capture.stop();

    size_t count = 0;
    for (size_t at = log.find("push_key is not"); at != std::string::npos; at = log.find("push_key is not", at + 1))
        count++;
    EXPECT_EQ(1u, count) << log;
    EXPECT_EQ(std::string::npos, log.find("0A0B0C")) << "the value itself is not logged";
}

TEST(PushTokenStoreTest, TheWorkerStoresTheKeyAndAPushOfTheCodeLogsNoSecret)
{
    const char *keyedToken = "jeton-cle-givre-solstice";
    const char *keylessToken = "jeton-sans-cle-givre-solstice";
    HistLogger::Instance().registerPushToken(keyedToken, HistLogger::PUSH_HW_ANDROID, HistLogger::PUSH_FORMAT_NOTIFICATION, "Pixel", KeyC);
    HistLogger::Instance().registerPushToken(keylessToken, HistLogger::PUSH_HW_IOS);
    const auto tokens = storedTokens();
    ASSERT_NE(nullptr, find(tokens, keyedToken));
    EXPECT_EQ(KeyC, find(tokens, keyedToken)->push_key);
    EXPECT_EQ("", find(tokens, keylessToken)->push_key);

    NotifManager::AdminCode code;
    code.message = "Calaos admin code 482913: open the web console. Valid 60 s, never share it.";
    code.codeId = "0123456789abcdef";
    code.action = "console_open";
    code.code = "482913";
    code.expires = "2026-10-25T09:13:03Z";

    //Neither token nor key nor code, whatever form the push took
    const std::vector<std::string> secrets = { "482913", KeyC, keyedToken, keylessToken };
    auto send = [&secrets](const char *token, const NotifManager::AdminCode &c, std::string &sealedKey)
    {
        PushTest::HttpPeer peer;
        NotifManager::setPushServiceUrl(peer.url());
        PushTest::StdoutCapture capture;
        auto done = std::make_shared<int>(-1);
        auto key = std::make_shared<std::string>();
        NotifManager::Instance().sendAdminCodeTo(token, c, [done, key](bool ok, const string &sealed)
                                                 {
            *done = ok ? 1 : 0;
            *key = sealed;
        });
        PushTest::pumpUntil([done]()
                            {
            return *done >= 0;
        });
        const std::string log = capture.stop();
        EXPECT_EQ(1, *done) << token;
        sealedKey = *key;
        const std::string req = peer.request();
        const size_t at = req.find("\r\n\r\n");
        const Json body = at == std::string::npos ? Json() : Json::parse(req.substr(at + 4), nullptr, false);
        EXPECT_NE(std::string::npos, log.find("code push to service")) << "nothing was captured: " << log;
        std::vector<std::string> found = secrets;
        Json copy = body;
        const Json data = copy.is_object() ? copy["notifications"][0]["data"] : Json();
        for (const char *k: { "admin_code_nonce", "admin_code_ct", "admin_code_tag" })
            if (data.is_object() && data.contains(k))
                found.push_back(data[k].get<std::string>());
        for (const std::string &secret: found)
            EXPECT_EQ(std::string::npos, log.find(secret)) << secret << " in " << log;
        return body;
    };

    std::string sealedKey;
    const Json sealed = send(keyedToken, code, sealedKey);
    const Json entry = sealed["notifications"][0];
    ASSERT_TRUE(entry.contains("data")) << sealed.dump();
    EXPECT_EQ("482913", PushTest::openAdminCode(KeyC, entry["data"]));
    EXPECT_EQ(std::string::npos, sealed.dump().find("482913")) << sealed.dump();
    EXPECT_FALSE(entry.contains("notification")) << "data only whatever its format";
    EXPECT_EQ(KeyC, sealedKey);

    //A keyless phone, and a keyed one when the daemon gave no code, get the daemon's text
    const Json legacy = send(keylessToken, code, sealedKey);
    EXPECT_EQ(code.message, legacy["notifications"][0]["message"]) << legacy.dump();
    EXPECT_EQ("", sealedKey);
    NotifManager::AdminCode old;
    old.message = code.message;
    old.codeId = code.codeId;
    const Json oldDaemon = send(keyedToken, old, sealedKey);
    EXPECT_EQ(code.message, oldDaemon["notifications"][0]["message"]) << oldDaemon.dump();
    EXPECT_EQ("", sealedKey);
}

TEST(PushTokenStoreTest, ApproverSetFreezesTheKeyTheConfirmCodeWasSealedWith)
{
    sqlite::database db(phaseTwoDatabase("proved_key.db"));
    ASSERT_TRUE(PushTokenDb::prepare(db));

    //KeyA sealed the approver_confirm code the phone read; someone with the
    //token and the household password registers KeyB before approver_set
    PushTokenDb::save(db, withKey("race-phone", KeyA), true, true, true);
    PushTokenDb::save(db, withKey("race-phone", KeyB), true, true, true);
    EXPECT_EQ(KeyB, keyOf(db, "race-phone"));

    PushTokenDb::setApprover(db, "race-phone", true, KeyA, true);
    EXPECT_EQ(KeyA, keyOf(db, "race-phone")) << "the proven key, not the last written";
    EXPECT_TRUE(find(PushTokenDb::load(db, true, true, true), "race-phone")->approver);
    PushTokenDb::save(db, withKey("race-phone", KeyB), true, true, true);
    EXPECT_EQ(KeyA, keyOf(db, "race-phone")) << "then frozen";

    //Without the column, or on a removal, the key is not touched
    PushTokenDb::setApprover(db, "race-phone", false, KeyC, true);
    EXPECT_EQ(KeyA, keyOf(db, "race-phone"));
    EXPECT_FALSE(find(PushTokenDb::load(db, true, true, true), "race-phone")->approver);
    PushTokenDb::setApprover(db, "race-phone", true, KeyC, false);
    EXPECT_EQ(KeyA, keyOf(db, "race-phone"));
    EXPECT_TRUE(find(PushTokenDb::load(db, true, true, true), "race-phone")->approver);
}

TEST(PushTokenStoreTest, ThePurgeNeverDropsAnApprover)
{
    sqlite::database db(phaseTwoDatabase("purge_approver.db"));
    ASSERT_TRUE(PushTokenDb::prepare(db));
    PushTokenDb::save(db, withKey("p2-approver", KeyA), true, true, true);
    PushTokenDb::save(db, withKey("p3-old-phone", KeyB), true, true, true);

    //Neither phone opened the app for 30 days
    db << "UPDATE push_tokens SET created_at = date('now', '-31 days');";
    PushTokenDb::purge(db, true);
    EXPECT_EQ(KeyA, keyOf(db, "p2-approver")) << "kept with its frozen key";
    EXPECT_EQ("<missing>", keyOf(db, "p3-old-phone"));
    PushTokenDb::save(db, withKey("p2-approver", KeyB), true, true, true);
    EXPECT_EQ(KeyA, keyOf(db, "p2-approver")) << "still frozen";

    //Freed, it ages out as any other phone
    PushTokenDb::setApprover(db, "p2-approver", false);
    db << "UPDATE push_tokens SET created_at = date('now', '-31 days');";
    PushTokenDb::purge(db, true);
    EXPECT_EQ("<missing>", keyOf(db, "p2-approver"));

    //A base without the approver column purges by date alone
    sqlite::database old(oldDatabase("purge_old.db"));
    old << "UPDATE push_tokens SET created_at = date('now', '-31 days');";
    PushTokenDb::purge(old);
    EXPECT_EQ(0u, PushTokenDb::load(old, false).size());
}
