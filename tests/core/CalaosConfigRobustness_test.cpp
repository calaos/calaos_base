/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
 ******************************************************************************/

// T2.4 — Config robustness regressions (CalaosConfig.cpp m1-m6 + state cache).
//
// - m1: a parse error in io.xml/rules.xml used to exit(-1) the whole daemon.
//   Now: log, preserve the corrupt file under backups/corrupt/, walk the
//   backups (Config::BackupFiles() tree) newest to oldest and restore the
//   first one that parses; give up gracefully (empty config) when none works.
//   Every corruption queues an alert message (getConfigAlerts()) that is
//   later sent by mail+push through NotifManager (same mechanism as the
//   battery/connected IO alerts) via Timer::singleShot. The send itself
//   needs a live event loop + notification config, so it is tested here at
//   the queued-message-content level only.
// - m3: config/cache writes go through SaveFile(tmp)+rename(tmp, file) with
//   no unlink() first, so a file always exists (asserted here via the absence
//   of a leftover tmp and content stability on failed save).
// - m4: ~Config() flushes the state cache, so states recorded with
//   save=false are not lost on shutdown (fork based death test).
// - audit finding: the state cache deserialization runs inside try/catch; a
//   JSON-valid but wrongly shaped cache falls back to an empty cache instead
//   of aborting at startup.
// - bonus: a non-UTF8 state value must not make Json::dump() throw away the
//   whole cache on save.

#include "CalaosCoreFixture.h"

#include "CalaosConfig.h"
#include "FileUtils.h"

#include <chrono>
#include <csignal>
#include <ctime>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

using namespace Calaos;

namespace
{

class ConfigRobustnessTest: public CalaosTest::CoreFixture
{
protected:
    static void putFile(const std::string &path, const std::string &content)
    {
        std::ofstream f(path.c_str(), std::ofstream::out | std::ofstream::trunc);
        f << content;
    }

    static std::string getFile(const std::string &path)
    {
        std::ifstream f(path.c_str());
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    std::string ioXmlPath() const { return configDir() + "/" IO_CONFIG; }
    std::string rulesXmlPath() const { return configDir() + "/" RULES_CONFIG; }
    std::string cachePath() const { return cacheDir() + "/iostates.cache"; }
    std::string corruptDir() const { return configDir() + "/backups/corrupt"; }

    static void setMTime(const std::string &path, time_t t)
    {
        struct timeval tv[2];
        tv[0].tv_sec = t; tv[0].tv_usec = 0;
        tv[1].tv_sec = t; tv[1].tv_usec = 0;
        ASSERT_EQ(0, ::utimes(path.c_str(), tv)) << path;
    }

    //Preserved corrupt copies of configName ("<name>.<timestamp>" files)
    std::vector<std::string> corruptCopiesOf(const std::string &configName) const
    {
        std::vector<std::string> found;
        std::error_code ec;
        std::filesystem::directory_iterator it(corruptDir(), ec), end;
        for (;!ec && it != end;it.increment(ec))
        {
            std::string name = it->path().filename().string();
            if (name.rfind(configName + ".", 0) == 0)
                found.push_back(it->path().string());
        }
        return found;
    }
};

/******************************************************************************
 * m1 — corrupt XML must never exit(-1) the process
 ******************************************************************************/

TEST_F(ConfigRobustnessTest, CorruptIoXmlWithoutBackupDoesNotKillTheProcess)
{
    const std::string corrupt = "<?xml version=\"1.0\"?>\n<calaos:ioconfig BROKEN";
    writeConfig(corrupt, CalaosTest::rulesXmlDocument(""));

    size_t alertsBefore = Config::Instance().getConfigAlerts().size();

    //Used to exit(-1) right here, killing the whole test binary
    Config::Instance().LoadConfigIO();

    EXPECT_EQ(0, ListeRoom::Instance().size());
    EXPECT_EQ(nullptr, io(ID_BOOL_IN));

    //The corrupt file was preserved verbatim for inspection
    std::vector<std::string> copies = corruptCopiesOf(IO_CONFIG);
    ASSERT_EQ(1u, copies.size());
    EXPECT_EQ(corrupt, getFile(copies[0]));

    //An alert was queued for the deferred mail/push notification,
    //reporting the empty-config fallback and the preserved path
    const std::vector<std::string> &alerts = Config::Instance().getConfigAlerts();
    ASSERT_EQ(alertsBefore + 1, alerts.size());
    EXPECT_NE(std::string::npos, alerts.back().find(IO_CONFIG));
    EXPECT_NE(std::string::npos, alerts.back().find(copies[0]));
    EXPECT_NE(std::string::npos, alerts.back().find("EMPTY"));
}

TEST_F(ConfigRobustnessTest, CorruptRulesXmlWithoutBackupDoesNotKillTheProcess)
{
    writeConfig(CalaosTest::minimalIoXml(), "<calaos:rules BROKEN");

    Config::Instance().LoadConfigIO();
    //Used to exit(-1) right here
    Config::Instance().LoadConfigRule();

    EXPECT_NE(nullptr, io(ID_BOOL_IN));
    EXPECT_EQ(0, ListeRule::Instance().size());
}

TEST_F(ConfigRobustnessTest, CorruptIoXmlIsRestoredFromBackup)
{
    loadConfig(); //valid minimal config, written to disk and loaded
    Config::Instance().BackupFiles();
    clearCoreState();

    const std::string corrupt = "garbage, definitely not xml <<<";
    putFile(ioXmlPath(), corrupt);

    size_t alertsBefore = Config::Instance().getConfigAlerts().size();
    Config::Instance().LoadConfigIO();

    //Config recovered from the backup: IOs are back...
    EXPECT_NE(nullptr, io(ID_BOOL_IN));
    EXPECT_NE(nullptr, io(ID_STRING));

    //...and the file on disk is parsable again
    pugi::xml_document doc;
    pugi::xml_parse_result parsed = doc.load_file(ioXmlPath().c_str());
    EXPECT_TRUE(parsed) << parsed.description();

    //The corrupt bytes were preserved verbatim before the restore
    std::vector<std::string> copies = corruptCopiesOf(IO_CONFIG);
    ASSERT_EQ(1u, copies.size());
    EXPECT_EQ(corrupt, getFile(copies[0]));

    //Queued alert reports the preserved copy and the restored backup
    const std::vector<std::string> &alerts = Config::Instance().getConfigAlerts();
    ASSERT_EQ(alertsBefore + 1, alerts.size());
    EXPECT_NE(std::string::npos, alerts.back().find(copies[0]));
    EXPECT_NE(std::string::npos, alerts.back().find("restored from backup"));
}

TEST_F(ConfigRobustnessTest, BackupWalkSkipsCorruptNewestAndRestoresOlderOne)
{
    loadConfig();
    std::string good = getFile(ioXmlPath());
    ASSERT_FALSE(good.empty());

    //Two handmade backups: the older one is good, the newer one is corrupt
    //itself. Explicit mtimes make the newest-first ordering deterministic.
    std::string olderDir = configDir() + "/backups/older";
    std::string newerDir = configDir() + "/backups/newer";
    ASSERT_TRUE(FileUtils::mkpath(olderDir));
    ASSERT_TRUE(FileUtils::mkpath(newerDir));
    putFile(olderDir + "/" IO_CONFIG, good);
    putFile(newerDir + "/" IO_CONFIG, "corrupt backup too <<<");
    setMTime(olderDir + "/" IO_CONFIG, ::time(nullptr) - 100);
    setMTime(newerDir + "/" IO_CONFIG, ::time(nullptr) - 50);

    clearCoreState();
    putFile(ioXmlPath(), "live file garbage <<<");

    size_t alertsBefore = Config::Instance().getConfigAlerts().size();
    Config::Instance().LoadConfigIO();

    //The newer (corrupt) backup was skipped, the older good one restored
    EXPECT_NE(nullptr, io(ID_BOOL_IN));
    EXPECT_EQ(good, getFile(ioXmlPath()));

    const std::vector<std::string> &alerts = Config::Instance().getConfigAlerts();
    ASSERT_EQ(alertsBefore + 1, alerts.size());
    EXPECT_NE(std::string::npos, alerts.back().find(olderDir + "/" IO_CONFIG));
}

TEST_F(ConfigRobustnessTest, CorruptRulesXmlIsRestoredFromBackup)
{
    loadConfig();
    Config::Instance().BackupFiles();
    clearCoreState();

    putFile(rulesXmlPath(), "garbage, definitely not xml <<<");
    Config::Instance().LoadConfigIO();
    Config::Instance().LoadConfigRule();

    EXPECT_NE(nullptr, findRule(RULE_NAME));

    pugi::xml_document doc;
    pugi::xml_parse_result parsed = doc.load_file(rulesXmlPath().c_str());
    EXPECT_TRUE(parsed) << parsed.description();
}

/******************************************************************************
 * m2/m3 — save failure handling and atomic replace
 ******************************************************************************/

TEST_F(ConfigRobustnessTest, SaveLeavesNoTemporaryFileBehind)
{
    loadConfig();
    saveConfig();

    EXPECT_TRUE(FileUtils::exists(ioXmlPath()));
    EXPECT_TRUE(FileUtils::exists(rulesXmlPath()));
    //tmp files are renamed over the target, never left behind
    EXPECT_FALSE(FileUtils::exists(ioXmlPath() + "_tmp"));
    EXPECT_FALSE(FileUtils::exists(rulesXmlPath() + "_tmp"));
}

TEST_F(ConfigRobustnessTest, FailedSaveKeepsThePreviousConfigIntact)
{
    if (::geteuid() == 0)
        GTEST_SKIP() << "running as root, read-only directories are not enforced";

    loadConfig();
    saveConfig();
    std::string before = getFile(ioXmlPath());
    ASSERT_FALSE(before.empty());

    //Make the config dir unwritable: SaveFile(tmp) fails, and the previous
    //config file must survive untouched (no unlink before rename)
    ASSERT_EQ(0, ::chmod(configDir().c_str(), 0555));
    Config::Instance().SaveConfigIO();
    ASSERT_EQ(0, ::chmod(configDir().c_str(), 0755));

    EXPECT_EQ(before, getFile(ioXmlPath()));
}

/******************************************************************************
 * State cache robustness
 ******************************************************************************/

TEST_F(ConfigRobustnessTest, UnparsableCacheFallsBackToEmptyCache)
{
    putFile(cachePath(), "this is {{ not json at all");

    Config::Instance().loadStateCache();

    std::string v;
    EXPECT_FALSE(Config::Instance().ReadValueIO("whatever", v));
}

TEST_F(ConfigRobustnessTest, WrongShapeCacheDoesNotAbortAndEndsUpEmpty)
{
    //Valid JSON, but a state value is a number: the deserialization throws.
    //It used to run outside the try/catch and abort the daemon at startup.
    //"a_good" comes first (nlohmann objects iterate sorted) so it is loaded
    //before the throw: the fallback must clear it too, not keep half a cache.
    putFile(cachePath(),
            "{\"iostates\": {\"a_good\": \"1\", \"z_bad\": 42},"
            " \"ioparams\": {\"p\": 12}}");

    Config::Instance().loadStateCache();

    std::string v;
    EXPECT_FALSE(Config::Instance().ReadValueIO("a_good", v));
    EXPECT_FALSE(Config::Instance().ReadValueIO("z_bad", v));
    Params p;
    EXPECT_FALSE(Config::Instance().ReadValueParams("p", p));
}

TEST_F(ConfigRobustnessTest, CacheRoundTripAndNoLeftoverTmp)
{
    Config::Instance().SaveValueIO("t24_io", "t24_value", true);

    Params params;
    params.Add("key", "value");
    Config::Instance().SaveValueParams("t24_params", params, true);

    EXPECT_TRUE(FileUtils::exists(cachePath()));
    EXPECT_FALSE(FileUtils::exists(cachePath() + ".tmp"));

    Config::Instance().loadStateCache();

    std::string v;
    EXPECT_TRUE(Config::Instance().ReadValueIO("t24_io", v));
    EXPECT_EQ("t24_value", v);

    Params p;
    EXPECT_TRUE(Config::Instance().ReadValueParams("t24_params", p));
    EXPECT_EQ("value", p["key"]);
}

TEST_F(ConfigRobustnessTest, NonUtf8StateValueDoesNotLoseTheCache)
{
    //Json::dump() throws on invalid UTF-8 unless told otherwise; a bogus
    //value coming from hardware must not abort or lose the other states
    Config::Instance().SaveValueIO("t24_binary", std::string("\xff\xfe raw"), false);
    Config::Instance().SaveValueIO("t24_sane", "ok", true); //triggers the save

    ASSERT_TRUE(FileUtils::exists(cachePath()));

    Config::Instance().loadStateCache();
    std::string v;
    EXPECT_TRUE(Config::Instance().ReadValueIO("t24_sane", v));
    EXPECT_EQ("ok", v);
}

/******************************************************************************
 * m4 — shutdown flush
 ******************************************************************************/

TEST_F(ConfigRobustnessTest, DestructorFlushesPendingStates)
{
    //fork() based death test: the child records a state with save=false and
    //exits normally; static destruction must flush it to the (shared,
    //inherited) cache path, where the parent can read it back.
    ::testing::FLAGS_gtest_death_test_style = "fast";

    Config::Instance(); //make sure the singleton exists before forking

    EXPECT_EXIT({
        Config::Instance().SaveValueIO("t24_shutdown", "flushed", false);
        exit(0); //runs static destructors, so ~Config()
    }, ::testing::ExitedWithCode(0), "");

    ASSERT_TRUE(FileUtils::exists(cachePath()))
            << "~Config() did not write the state cache";

    Config::Instance().loadStateCache();
    std::string v;
    EXPECT_TRUE(Config::Instance().ReadValueIO("t24_shutdown", v));
    EXPECT_EQ("flushed", v);
}

TEST_F(ConfigRobustnessTest, ABackupWithNoFreeFolderInTheSecondIsRefusedAndWritesNothing)
{
    loadConfig();

    const auto stamp = []()
    {
        const std::time_t t = std::time(nullptr);
        const std::tm tm = *std::localtime(&t);
        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y/%m/%d-%m-%Y_%H-%M-%S");
        return ss.str();
    };

    //a full second to fill the folders in before the stamp moves on
    const std::string start = stamp();
    while (stamp() == start)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

    const std::string base = configDir() + "/backups/" + stamp();
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    ASSERT_FALSE(ec) << ec.message();
    for (int i = 2; i <= 1000; i++)
        std::filesystem::create_directories(base + "-" + std::to_string(i), ec);
    ASSERT_FALSE(ec) << ec.message();

    const std::string result = Config::Instance().BackupFiles();
    ASSERT_EQ(base, configDir() + "/backups/" + stamp()) << "the second moved on while the case ran";

    EXPECT_TRUE(result.empty());
    EXPECT_FALSE(std::filesystem::exists(base + "-1001"));
    EXPECT_TRUE(std::filesystem::is_empty(base));
}

//The sources are 0644 on purpose: the copy must not take after them, nor after the umask
TEST_F(ConfigRobustnessTest, TheBackupCopiesAreReadableOnlyByTheirOwner)
{
    for (const char *name: { "io.xml", "rules.xml", "local_config.xml" })
    {
        const std::string source = configDir() + "/" + name;
        if (!std::filesystem::exists(source))
            std::ofstream(source) << "<?xml version=\"1.0\"?>\n";
        ASSERT_EQ(0, ::chmod(source.c_str(), 0644));
    }

    const mode_t previous = ::umask(0);
    const std::string folder = Config::Instance().BackupFiles();
    ::umask(previous);
    ASSERT_FALSE(folder.empty());

    for (const char *name: { "io.xml", "rules.xml", "local_config.xml" })
    {
        struct stat st;
        const std::string copy = configDir() + "/backups/" + folder + "/" + name;
        ASSERT_EQ(0, ::stat(copy.c_str(), &st)) << copy;
        EXPECT_EQ(0600u, st.st_mode & 07777) << copy;
    }
}

TEST_F(ConfigRobustnessTest, TheCorruptCopyIsReadableOnlyByItsOwner)
{
    writeConfig("<?xml version=\"1.0\"?>\n<calaos:ioconfig BROKEN", CalaosTest::rulesXmlDocument(""));
    ASSERT_EQ(0, ::chmod(ioXmlPath().c_str(), 0644));

    const mode_t previous = ::umask(0);
    Config::Instance().LoadConfigIO();
    ::umask(previous);

    std::vector<std::string> copies = corruptCopiesOf(IO_CONFIG);
    ASSERT_EQ(1u, copies.size());
    struct stat st;
    ASSERT_EQ(0, ::stat(copies[0].c_str(), &st));
    EXPECT_EQ(0600u, st.st_mode & 07777);
}

//Past RLIMIT_FSIZE a write stops short with EFBIG, as on a full disk
TEST_F(ConfigRobustnessTest, ABackupCutShortByTheDiskIsAFailure)
{
    putFile(ioXmlPath(), std::string(64 * 1024, 'x'));

    struct rlimit saved;
    ASSERT_EQ(0, ::getrlimit(RLIMIT_FSIZE, &saved));
    void (*previousHandler)(int) = std::signal(SIGXFSZ, SIG_IGN);
    struct rlimit limited = saved;
    limited.rlim_cur = 4096;
    ASSERT_EQ(0, ::setrlimit(RLIMIT_FSIZE, &limited));
    const std::string folder = Config::Instance().BackupFiles();
    ::setrlimit(RLIMIT_FSIZE, &saved);
    std::signal(SIGXFSZ, previousHandler);

    EXPECT_TRUE(folder.empty());
    std::error_code ec;
    for (const auto &entry: std::filesystem::recursive_directory_iterator(configDir() + "/backups", ec))
        EXPECT_NE(IO_CONFIG, entry.path().filename().string()) << entry.path();
}

/******************************************************************************
 * Restoring a backup never costs the live file
 ******************************************************************************/

class ConfigRestoreTest: public ConfigRobustnessTest
{
protected:
    static constexpr rlim_t kFileSizeLimit = 64 * 1024;

    //Past RLIMIT_FSIZE a write stops short with EFBIG, as on a full disk
    class FullDisk
    {
    public:
        FullDisk()
        {
            ::getrlimit(RLIMIT_FSIZE, &saved);
            previousHandler = std::signal(SIGXFSZ, SIG_IGN);
            struct rlimit limited = saved;
            limited.rlim_cur = kFileSizeLimit;
            ::setrlimit(RLIMIT_FSIZE, &limited);
        }
        ~FullDisk()
        {
            ::setrlimit(RLIMIT_FSIZE, &saved);
            std::signal(SIGXFSZ, previousHandler);
        }

    private:
        struct rlimit saved;
        void (*previousHandler)(int);
    };

    void TearDown() override
    {
        ::umask(savedUmask);
        ConfigRobustnessTest::TearDown();
    }

    //Backup of configName dated secondsAgo, in its own folder
    std::string putBackup(const std::string &folder, const char *configName,
                          const std::string &content, time_t secondsAgo)
    {
        const std::string dir = configDir() + "/backups/" + folder;
        EXPECT_TRUE(FileUtils::mkpath(dir));
        const std::string path = dir + "/" + configName;
        putFile(path, content);
        setMTime(path, ::time(nullptr) - secondsAgo);
        return path;
    }

    //Valid XML too big to get past the FullDisk limit
    static std::string oversized(const std::string &xml)
    {
        return xml + "<!--" + std::string(4 * kFileSizeLimit, 'x') + "-->";
    }

    static void putLive(const std::string &path, const std::string &content, mode_t mode)
    {
        putFile(path, content);
        ASSERT_EQ(0, ::chmod(path.c_str(), mode));
    }

    static mode_t modeOf(const std::string &path)
    {
        struct stat st;
        EXPECT_EQ(0, ::lstat(path.c_str(), &st)) << path;
        return st.st_mode & 07777;
    }

    //The temporary sits next to the live file, whatever its name
    void expectNoTemporaryFor(const char *configName) const
    {
        const std::string name = configName;
        for (const auto &entry: std::filesystem::directory_iterator(configDir()))
        {
            const std::string found = entry.path().filename().string();
            EXPECT_TRUE(found == name || found.find(name) == std::string::npos) << found;
        }
    }

    const mode_t savedUmask = ::umask(0);
};

TEST_F(ConfigRestoreTest, ACopyFailureFallsBackToAnOlderBackup)
{
    loadConfig();
    const std::string good = getFile(ioXmlPath());
    ASSERT_LT(good.size(), kFileSizeLimit);
    putBackup("older", IO_CONFIG, good, 100);
    putBackup("newer", IO_CONFIG, oversized(good), 50);

    clearCoreState();
    putLive(ioXmlPath(), "live file garbage <<<", 0640);
    {
        FullDisk disk;
        Config::Instance().LoadConfigIO();
    }

    EXPECT_NE(nullptr, io(ID_BOOL_IN));
    EXPECT_EQ(good, getFile(ioXmlPath()));
    EXPECT_EQ(0640u, modeOf(ioXmlPath()));
    expectNoTemporaryFor(IO_CONFIG);
}

TEST_F(ConfigRestoreTest, ACopyFailureLeavesTheLiveFileAsItWas)
{
    loadConfig();
    putBackup("only", IO_CONFIG, oversized(getFile(ioXmlPath())), 50);

    clearCoreState();
    const std::string corrupt = "live file garbage <<<";
    putLive(ioXmlPath(), corrupt, 0640);
    {
        FullDisk disk;
        Config::Instance().LoadConfigIO();
    }

    EXPECT_EQ(nullptr, io(ID_BOOL_IN));
    EXPECT_EQ(corrupt, getFile(ioXmlPath()));
    EXPECT_EQ(0640u, modeOf(ioXmlPath()));
    expectNoTemporaryFor(IO_CONFIG);
}

TEST_F(ConfigRestoreTest, ACopyFailureLeavesTheLiveRulesFileAsItWas)
{
    loadConfig();
    putBackup("only", RULES_CONFIG, oversized(getFile(rulesXmlPath())), 50);

    clearCoreState();
    const std::string corrupt = "live file garbage <<<";
    putLive(rulesXmlPath(), corrupt, 0600);
    {
        FullDisk disk;
        Config::Instance().LoadConfigRule();
    }

    EXPECT_EQ(corrupt, getFile(rulesXmlPath()));
    EXPECT_EQ(0600u, modeOf(rulesXmlPath()));
    expectNoTemporaryFor(RULES_CONFIG);
}

TEST_F(ConfigRestoreTest, NoValidBackupLeavesTheLiveFileAsItWas)
{
    loadConfig();
    putBackup("only", IO_CONFIG, "corrupt backup too <<<", 50);

    clearCoreState();
    const std::string corrupt = "live file garbage <<<";
    putLive(ioXmlPath(), corrupt, 0600);
    Config::Instance().LoadConfigIO();

    EXPECT_EQ(nullptr, io(ID_BOOL_IN));
    EXPECT_EQ(corrupt, getFile(ioXmlPath()));
    EXPECT_EQ(0600u, modeOf(ioXmlPath()));
    expectNoTemporaryFor(IO_CONFIG);
}

TEST_F(ConfigRestoreTest, AnUnreadableBackupIsSkipped)
{
    if (::geteuid() == 0)
        GTEST_SKIP() << "root reads a file of mode 0000";

    loadConfig();
    const std::string good = getFile(ioXmlPath());
    putBackup("older", IO_CONFIG, good, 100);
    const std::string newer = putBackup("newer", IO_CONFIG, good, 50);
    ASSERT_EQ(0, ::chmod(newer.c_str(), 0));

    clearCoreState();
    putLive(ioXmlPath(), "live file garbage <<<", 0600);
    Config::Instance().LoadConfigIO();
    ::chmod(newer.c_str(), 0600);

    EXPECT_NE(nullptr, io(ID_BOOL_IN));
    EXPECT_EQ(good, getFile(ioXmlPath()));
}

TEST_F(ConfigRestoreTest, TheRestoredFileKeepsTheModeOfTheLiveFile)
{
    loadConfig();
    const std::string good = getFile(ioXmlPath());
    putBackup("only", IO_CONFIG, good, 50);

    for (mode_t mode: { 0600, 0640, 0644 })
    {
        clearCoreState();
        putLive(ioXmlPath(), "live file garbage <<<", mode);
        Config::Instance().LoadConfigIO();

        EXPECT_EQ(good, getFile(ioXmlPath()));
        EXPECT_EQ(mode, modeOf(ioXmlPath()));
    }
}

//The one way to reach the restore with no live file: a link into a missing
//folder, which the default config cannot be written through either
TEST_F(ConfigRestoreTest, TheRestoredFileIsOwnerOnlyWhenThereWasNoLiveFile)
{
    loadConfig();
    const std::string good = getFile(ioXmlPath());
    putBackup("only", IO_CONFIG, good, 50);

    clearCoreState();
    ASSERT_EQ(0, ::unlink(ioXmlPath().c_str()));
    const std::string missing = configDir() + "/missing/" IO_CONFIG;
    ASSERT_EQ(0, ::symlink(missing.c_str(), ioXmlPath().c_str()));
    Config::Instance().LoadConfigIO();

    EXPECT_NE(nullptr, io(ID_BOOL_IN));
    EXPECT_EQ(good, getFile(ioXmlPath()));
    EXPECT_EQ(0600u, modeOf(ioXmlPath()));
    expectNoTemporaryFor(IO_CONFIG);
}

TEST_F(ConfigRestoreTest, ASymlinkedLiveFileKeepsItsLink)
{
    loadConfig();
    const std::string good = getFile(ioXmlPath());
    putBackup("only", IO_CONFIG, good, 50);

    clearCoreState();
    const std::string persistent = configDir() + "/persistent";
    ASSERT_TRUE(FileUtils::mkpath(persistent));
    const std::string target = persistent + "/" IO_CONFIG;
    putLive(target, "live file garbage <<<", 0640);
    ASSERT_EQ(0, ::unlink(ioXmlPath().c_str()));
    ASSERT_EQ(0, ::symlink(target.c_str(), ioXmlPath().c_str()));
    Config::Instance().LoadConfigIO();

    EXPECT_TRUE(std::filesystem::is_symlink(ioXmlPath()));
    EXPECT_EQ(good, getFile(target));
    EXPECT_EQ(0640u, modeOf(target));
}

TEST_F(ConfigRestoreTest, ARefusedRenameLeavesNoTemporary)
{
    loadConfig();
    putBackup("only", IO_CONFIG, getFile(ioXmlPath()), 50);

    clearCoreState();
    ASSERT_EQ(0, ::unlink(ioXmlPath().c_str()));
    ASSERT_TRUE(FileUtils::mkdir(ioXmlPath()));
    Config::Instance().LoadConfigIO();

    EXPECT_EQ(nullptr, io(ID_BOOL_IN));
    EXPECT_TRUE(FileUtils::isDir(ioXmlPath()));
    expectNoTemporaryFor(IO_CONFIG);
    ::rmdir(ioXmlPath().c_str());
}
}
