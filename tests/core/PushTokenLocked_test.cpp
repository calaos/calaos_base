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

//The first start after an update finds events.db locked: push_tokens cannot
//gain push_format. Push must keep working with the tokens already there, and
//nothing written about a failure may carry a token.

#include <gtest/gtest.h>

#include <filesystem>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "sqlite_modern_cpp.h"
#pragma GCC diagnostic pop

#include "PushTestUtil.h"

using namespace Calaos;
using namespace PushTest;

namespace
{

//Never produced by anything else in this binary, and not hexadecimal.
const char *const kSecret = "jeton-push-solstice-verglas";

std::unique_ptr<sqlite::database> locker;
TokensAnswer answerWhileLocked;

class LockedAtStartup: public ::testing::Environment
{
public:
    void SetUp() override
    {
        base = makeTestDirs();
        ASSERT_FALSE(base.empty());
        const std::string dbPath = base + "/cache/events.db";
        {
            sqlite::database db(dbPath);
            db << OldTokensSchema;
            db << "INSERT INTO push_tokens (token, hw_type) values ('old-android', 2);";
        }

        locker = std::make_unique<sqlite::database>(dbPath);
        *locker << "BEGIN EXCLUSIVE;";
        //First use of the singleton: its worker opens the database now.
        answerWhileLocked = readTokens();
        *locker << "COMMIT;";
    }

    void TearDown() override
    {
        locker.reset();
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
    }

private:
    std::string base;
};

const auto *env = ::testing::AddGlobalTestEnvironment(new LockedAtStartup);

} //namespace

TEST(PushTokenLockedTest, AReadOnALockedDatabaseIsAnsweredAsAFailure)
{
    EXPECT_TRUE(answerWhileLocked.received) << "the reader never got an answer";
    EXPECT_FALSE(answerWhileLocked.success);
}

TEST(PushTokenLockedTest, TokensAreStillServedWhenTheMigrationCouldNotRun)
{
    const TokensAnswer a = readTokens();
    ASSERT_TRUE(a.received);
    ASSERT_TRUE(a.success);
    ASSERT_NE(nullptr, a.find("old-android"));
    EXPECT_EQ(HistLogger::PUSH_FORMAT_NOTIFICATION, a.find("old-android")->format);
}

TEST(PushTokenLockedTest, RegisteringStillWorksWithoutTheColumn)
{
    HistLogger::Instance().registerPushToken("new-android", HistLogger::PUSH_HW_ANDROID,
                                             HistLogger::PUSH_FORMAT_DATA);
    const TokensAnswer a = readTokens();
    ASSERT_TRUE(a.success);
    ASSERT_NE(nullptr, a.find("new-android"));
    //No column to keep it in until the next start migrates the table.
    EXPECT_EQ(HistLogger::PUSH_FORMAT_NOTIFICATION, a.find("new-android")->format);
}

TEST(PushTokenLockedTest, NoTokenInTheLogsOnSuccessOrFailure)
{
    StdoutCapture capture;

    //A reader holding the database: statements still prepare, the write fails
    //at step time, where sqlite_modern_cpp's error carries the SQL expanded
    //with the bound token.
    *locker << "BEGIN;";
    *locker << "SELECT count(*) FROM push_tokens;" >> [](int) {};
    HistLogger::Instance().registerPushToken(kSecret, HistLogger::PUSH_HW_ANDROID,
                                             HistLogger::PUSH_FORMAT_DATA);
    const TokensAnswer failed = readTokens();
    const bool notStored = failed.received && !failed.find(kSecret);
    *locker << "COMMIT;";

    HistLogger::Instance().registerPushToken(kSecret, HistLogger::PUSH_HW_ANDROID,
                                             HistLogger::PUSH_FORMAT_DATA);
    const TokensAnswer ok = readTokens();

    const std::string log = capture.stop();

    EXPECT_TRUE(notStored) << "the write was expected to fail";
    EXPECT_NE(std::string::npos, log.find("database is locked")) << "no failure was logged";
    EXPECT_TRUE(ok.success);
    EXPECT_NE(nullptr, ok.find(kSecret)) << "the token never reached the database";
    EXPECT_NE(std::string::npos, log.find("history")) << "nothing was captured";
    EXPECT_EQ(std::string::npos, log.find(kSecret)) << log;
}
