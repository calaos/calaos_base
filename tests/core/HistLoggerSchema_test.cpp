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

//events.db written by servers that had no index on created_at: opening it
//adds the index without touching a row, the reads use it and keep their
//order, and the retention purge deletes exactly what it deleted before.

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <filesystem>
#include <fstream>

#include "PushTestUtil.h"

using namespace Calaos;
using namespace PushTest;

namespace
{

//The schema of the servers before the index, as they created it.
const char *const OldSchema =
    "CREATE TABLE events (id INTEGER PRIMARY KEY UNIQUE NOT NULL, uuid TEXT, "
    "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, event_type INTEGER, io_id TEXT, "
    "io_state TEXT, event_raw TEXT, pic_uid TEXT);"
    "CREATE TABLE push_tokens (id INTEGER PRIMARY KEY UNIQUE NOT NULL, token TEXT, "
    "hw_type INTEGER, created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
    "push_format INTEGER NOT NULL DEFAULT 0);";

std::string base;
std::string dbPath;
std::vector<std::string> rowsBefore, rowsAfter, tokensBefore, tokensAfter;

class Db
{
public:
    Db() { sqlite3_open(dbPath.c_str(), &db); }
    ~Db() { sqlite3_close(db); }

    bool exec(const std::string &sql) { return sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK; }

    //One string per row, NULL told apart from ''.
    std::vector<std::string> rows(const std::string &sql)
    {
        std::vector<std::string> out;
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
            return { std::string("prepare failed: ") + sqlite3_errmsg(db) };
        while (sqlite3_step(st) == SQLITE_ROW)
        {
            std::string r;
            for (int i = 0; i < sqlite3_column_count(st); i++)
            {
                const unsigned char *v = sqlite3_column_text(st, i);
                r += v ? "'" + std::string((const char *)v, sqlite3_column_bytes(st, i)) + "'" : "NULL";
                r += "|";
            }
            out.push_back(r);
        }
        sqlite3_finalize(st);
        return out;
    }

    std::string plan(const std::string &sql)
    {
        std::string p;
        for (const auto &r: rows("EXPLAIN QUERY PLAN " + sql))
            p += r + "\n";
        return p;
    }

private:
    sqlite3 *db = nullptr;
};

const char *const AllRows = "SELECT id, uuid, created_at, event_type, io_id, io_state, event_raw, pic_uid "
                            "FROM events ORDER BY id;";
const char *const AllTokens = "SELECT * FROM push_tokens ORDER BY id;";

struct Page
{
    bool received = false;
    bool success = false;
    int totalCount = -1;
    std::vector<std::string> uuids;
};

Page readPage(int page, int perPage)
{
    auto a = std::make_shared<Page>();
    HistLogger::Instance().getEvents(page, perPage,
                                     [a](bool success, string, const vector<HistEvent> &events, int, int total)
                                     {
        a->received = true;
        a->success = success;
        a->totalCount = total;
        for (const auto &e: events)
            a->uuids.push_back(e.uuid);
    });
    pumpUntil([a]()
              {
        return a->received;
    });
    return *a;
}

void append(const std::string &ioId)
{
    HistEvent ev = HistEvent::create();
    ev.event_type = 3;
    ev.io_id = ioId;
    ev.event_raw = "{}";
    HistLogger::Instance().appendEvent(ev);
}

int count(const std::string &where)
{
    Db db;
    const auto r = db.rows("SELECT count(*) FROM events WHERE " + where + ";");
    return r.size() == 1 ? std::stoi(r[0].substr(1)) : -1;
}

class OldDatabase: public ::testing::Environment
{
public:
    void SetUp() override
    {
        base = makeTestDirs();
        ASSERT_FALSE(base.empty());
        dbPath = base + "/cache/events.db";

        {
            Db db;
            ASSERT_TRUE(db.exec(OldSchema));
            //Three events in the same second, NULLs, a large event_raw, a push
            //with its picture, and events past the 30 days kept by default.
            ASSERT_TRUE(db.exec(
                "INSERT INTO events (uuid, created_at, event_type, io_id, io_state, event_raw, pic_uid) VALUES "
                "('u-tie-1', strftime('%Y-%m-%d %H:%M:00', 'now', '-1 hours'), 3, 'io_a', 'true', '{\"a\":1}', ''),"
                "('u-old-1', datetime('now', '-40 days'), 21, '', '', '{\"pic_uid\":\"pic-old\"}', 'pic-old'),"
                "('u-tie-2', strftime('%Y-%m-%d %H:%M:00', 'now', '-1 hours'), 3, 'io_b', 'false', '{\"b\":2}', ''),"
                "('u-null', datetime('now', '-2 hours'), 3, NULL, NULL, NULL, NULL),"
                "('u-3days', datetime('now', '-3 days'), 3, 'io_c', '21.5', '{\"c\":\"été\"}', ''),"
                "('u-tie-3', strftime('%Y-%m-%d %H:%M:00', 'now', '-1 hours'), 22, 'io_c', '', '{}', ''),"
                "('u-old-2', datetime('now', '-45 days'), 21, '', '', '{\"pic_uid\":\"pic-gone\"}', 'pic-gone'),"
                "('u-recent', datetime('now', '-5 minutes'), 21, '', '', '{\"pic_uid\":\"pic-new\"}', 'pic-new');"));
            ASSERT_TRUE(db.exec("INSERT INTO events (uuid, created_at, event_type, io_id, io_state, event_raw, pic_uid) "
                                "VALUES ('u-large', datetime('now', '-30 minutes'), 3, 'io_d', 'x', '{\"blob\":\"' || "
                                "printf('%.300000c', 'x') || '\"}', '');"));
            ASSERT_TRUE(db.exec("INSERT INTO push_tokens (token, hw_type, push_format) VALUES ('tok-a', 1, 0), ('tok-b', 2, 1);"));
            rowsBefore = db.rows(AllRows);
            tokensBefore = db.rows(AllTokens);
        }

        std::filesystem::create_directories(base + "/cache/push_pictures");
        std::ofstream(base + "/cache/push_pictures/pic-old.jpg") << "jpg";
        std::ofstream(base + "/cache/push_pictures/pic-new.jpg") << "jpg";

        //First use of the singleton: the worker opens and migrates the database.
        ASSERT_TRUE(readPage(0, 100).received);

        Db db;
        rowsAfter = db.rows(AllRows);
        tokensAfter = db.rows(AllTokens);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
    }
};

const auto *env = ::testing::AddGlobalTestEnvironment(new OldDatabase);

} //namespace

TEST(HistLoggerSchemaTest, OpeningAnOldDatabaseAddsTheIndexAndKeepsEveryRow)
{
    ASSERT_EQ(9u, rowsBefore.size());
    EXPECT_EQ(rowsBefore, rowsAfter);
    //The migration only adds the empty device_name and approver of the
    //tokens, and their last registration as the first one
    std::vector<std::string> named;
    for (const std::string &t: tokensBefore)
    {
        const std::string createdAt = t.substr(t.find("'20"), 21);
        named.push_back(t + "''|'0'|" + createdAt + "|");
    }
    EXPECT_EQ(named, tokensAfter);

    Db db;
    const auto idx = db.rows("SELECT name, tbl_name FROM sqlite_master WHERE type = 'index' AND name = 'events_created_at';");
    EXPECT_EQ(std::vector<std::string>{ "'events_created_at'|'events'|" }, idx);
}

TEST(HistLoggerSchemaTest, ThePaginatedReadAndThePurgeUseTheIndex)
{
    Db db;
    const std::string page = db.plan(HistEventSql::ReadPage);
    EXPECT_NE(std::string::npos, page.find("USING INDEX events_created_at")) << page;
    EXPECT_EQ(std::string::npos, page.find("TEMP B-TREE")) << page;

    const std::string purge = db.plan(HistEventSql::Purge);
    EXPECT_NE(std::string::npos, purge.find("INDEX events_created_at (created_at<?)")) << purge;
}

TEST(HistLoggerSchemaTest, PagesKeepTheOrderOfTheUnindexedSort)
{
    Db db;
    //Events of the same second came out oldest first.
    std::vector<std::string> expected;
    for (const auto &r: db.rows("SELECT uuid FROM events NOT INDEXED ORDER BY created_at DESC;"))
        expected.push_back(r.substr(1, r.size() - 3));
    ASSERT_FALSE(expected.empty());

    std::vector<std::string> got;
    for (int p = 0; p * 2 < (int)expected.size(); p++)
    {
        const Page page = readPage(p, 2);
        ASSERT_TRUE(page.received);
        ASSERT_TRUE(page.success);
        EXPECT_EQ((int)expected.size(), page.totalCount);
        got.insert(got.end(), page.uuids.begin(), page.uuids.end());
    }
    EXPECT_EQ(expected, got);
}

TEST(HistLoggerSchemaTest, RetentionIsTheSameAsBefore)
{
    //Reading purges nothing.
    readPage(0, 1);
    EXPECT_EQ(2, count("uuid LIKE 'u-old-%'"));

    //The next event takes out what is older than 30 days, with its picture.
    append("evlog_after_30");
    readPage(0, 1);
    //u-old-2's picture never existed: the failed unlink does not stop the purge.
    EXPECT_EQ(0, count("uuid LIKE 'u-old-%'"));
    EXPECT_FALSE(std::filesystem::exists(base + "/cache/push_pictures/pic-old.jpg"));
    EXPECT_TRUE(std::filesystem::exists(base + "/cache/push_pictures/pic-new.jpg"));
    EXPECT_EQ(1, count("uuid = 'u-3days'"));

    //Lowering history_keep_days applies at the next event.
    Utils::set_config_option("history_keep_days", "2");
    append("evlog_after_2");
    readPage(0, 1);
    EXPECT_EQ(0, count("uuid = 'u-3days'"));
    EXPECT_EQ(1, count("uuid = 'u-recent'"));
    EXPECT_EQ(1, count("io_id = 'evlog_after_2'"));
    EXPECT_TRUE(std::filesystem::exists(base + "/cache/push_pictures/pic-new.jpg"));

    Utils::del_config_option("history_keep_days");
}
