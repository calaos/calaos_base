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

//Tests for the local_config.xml accessors of Utils: batched merging write,
//atomic and durable replacement, permission preservation and inter-process
//locking.

#include "Utils.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *EMPTY_CONFIG =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
    "<calaos:config xmlns:calaos=\"http://www.calaos.fr\">\n"
    "</calaos:config>\n";

//Every test gets its own directory tree:
//  <base>/config    config dir, holds local_config.xml
//  <base>/cache     cache dir, checked by initConfigOptions()
//  <base>/persist   used by the symlink test
class UtilsConfigTest: public ::testing::Test
{
protected:
    virtual void SetUp()
    {
        char tmpl[] = "/tmp/calaos_cfgtest_XXXXXX";
        char *d = mkdtemp(tmpl);
        ASSERT_NE(d, nullptr);
        base = d;

        confDir = base + "/config";
        cacheDir = base + "/cache";
        persistDir = base + "/persist";

        ASSERT_EQ(::mkdir(confDir.c_str(), 0755), 0);
        ASSERT_EQ(::mkdir(cacheDir.c_str(), 0755), 0);
        ASSERT_EQ(::mkdir(persistDir.c_str(), 0755), 0);
    }

    virtual void TearDown()
    {
        //A test may have made the tree unwritable on purpose
        ::chmod(base.c_str(), 0755);
        ::chmod(confDir.c_str(), 0755);
        ::chmod(persistDir.c_str(), 0755);

        string cmd = "rm -rf '" + base + "'";
        if (::system(cmd.c_str()) != 0)
            std::cerr << "warning: could not clean " << base << std::endl;
    }

    //Point the library at this test directory. initConfigOptions() is the only
    //entry point that resets the cached config/cache paths of the process.
    void initConfig()
    {
        std::vector<char> c(confDir.begin(), confDir.end());
        c.push_back('\0');
        std::vector<char> k(cacheDir.begin(), cacheDir.end());
        k.push_back('\0');

        Utils::initConfigOptions(c.data(), k.data(), true);
    }

    string configFile() const { return confDir + "/local_config.xml"; }

    static void writeFile(const string &path, const string &content)
    {
        std::ofstream f(path.c_str(), std::ofstream::out | std::ofstream::trunc);
        f << content;
        f.close();
    }

    static string readFile(const string &path)
    {
        std::ifstream f(path.c_str());
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    //Write the config file directly, the way another program would
    static void writeConfigFile(const string &path, const string &options)
    {
        writeFile(path,
                  string("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
                         "<calaos:config xmlns:calaos=\"http://www.calaos.fr\">\n") +
                  options +
                  "</calaos:config>\n");
    }

    static string option(const string &key, const string &value)
    {
        return "  <calaos:option name=\"" + key + "\" value=\"" + value + "\" />\n";
    }

    static unsigned int fileMode(const string &path)
    {
        struct stat st;
        if (::stat(path.c_str(), &st) != 0)
            return 0;
        return st.st_mode & 07777;
    }

    string base;
    string confDir;
    string cacheDir;
    string persistDir;
};

//initConfigOptions() creates and seeds a config when none exists, in a single
//write, and the fresh file is not readable by everybody: it holds secrets.
TEST_F(UtilsConfigTest, InitCreatesAndSeedsDefaultConfig)
{
    initConfig();

    ASSERT_TRUE(Utils::fileExists(configFile()));
    EXPECT_EQ(Utils::get_config_option("cn_user"), "user");
    EXPECT_EQ(Utils::get_config_option("cn_pass"), "pass");
    EXPECT_EQ(Utils::get_config_option("latitude"), "48.864715");
    EXPECT_EQ(Utils::get_config_option("notif/battery_mail_enabled"), "true");

    EXPECT_EQ(fileMode(configFile()), 0600u);
}

//Missing notif/* keys are back filled, and an already complete file is left
//byte for byte identical (no useless flash write).
TEST_F(UtilsConfigTest, InitBackFillsMissingKeysOnlyOnce)
{
    writeConfigFile(configFile(), option("cn_user", "someone"));

    initConfig();

    EXPECT_EQ(Utils::get_config_option("cn_user"), "someone");
    EXPECT_EQ(Utils::get_config_option("notif/io_connected_push_enabled"), "true");

    string content = readFile(configFile());

    //Second run: nothing is missing anymore, the file must not be rewritten
    initConfig();
    EXPECT_EQ(readFile(configFile()), content);
}

TEST_F(UtilsConfigTest, SetConfigOptionsCreatesUpdatesAndDeletes)
{
    initConfig();

    Params toSet = { { "foo", "1" }, { "bar", "2" } };
    ASSERT_TRUE(Utils::set_config_options(toSet));

    EXPECT_EQ(Utils::get_config_option("foo"), "1");
    EXPECT_EQ(Utils::get_config_option("bar"), "2");

    Params update = { { "foo", "42" }, { "baz", "3" } };
    ASSERT_TRUE(Utils::set_config_options(update, { "bar" }));

    EXPECT_EQ(Utils::get_config_option("foo"), "42");
    EXPECT_EQ(Utils::get_config_option("baz"), "3");
    EXPECT_EQ(Utils::get_config_option("bar"), "");

    //Deleting an absent key is not an error
    EXPECT_TRUE(Utils::set_config_options(Params(), { "does_not_exist" }));

    //And the untouched keys are still there
    EXPECT_EQ(Utils::get_config_option("cn_user"), "user");

    Params all;
    ASSERT_TRUE(Utils::get_config_options(all));
    EXPECT_TRUE(all.Exists("foo"));
    EXPECT_FALSE(all.Exists("bar"));
}

//The merge guarantee: the document is reloaded under the lock at write time,
//so a key another program added in the meantime survives.
TEST_F(UtilsConfigTest, SetConfigOptionsMergesConcurrentChanges)
{
    initConfig();

    //What the caller read at some earlier point
    Params snapshot;
    ASSERT_TRUE(Utils::get_config_options(snapshot));
    ASSERT_FALSE(snapshot.Exists("written_by_someone_else"));

    //Another program rewrites the whole file behind our back
    writeConfigFile(configFile(),
                    option("written_by_someone_else", "kept") +
                    option("cn_user", "changed_by_someone_else"));

    ASSERT_TRUE(Utils::set_config_options(Params{ { "mine", "ok" } }));

    EXPECT_EQ(Utils::get_config_option("mine"), "ok");
    EXPECT_EQ(Utils::get_config_option("written_by_someone_else"), "kept");
    EXPECT_EQ(Utils::get_config_option("cn_user"), "changed_by_someone_else");
}

//Regression guard: a <calaos:config> root without any child element.
TEST_F(UtilsConfigTest, RoundTripOnEmptyConfigRoot)
{
    writeFile(configFile(), EMPTY_CONFIG);

    Params empty;
    initConfig();

    ASSERT_TRUE(Utils::set_config_options(Params{ { "first", "value" } }));
    EXPECT_EQ(Utils::get_config_option("first"), "value");

    ASSERT_TRUE(Utils::set_config_option("second", "value2"));
    EXPECT_EQ(Utils::get_config_option("second"), "value2");

    ASSERT_TRUE(Utils::del_config_option("first"));
    EXPECT_EQ(Utils::get_config_option("first"), "");

    ASSERT_TRUE(Utils::get_config_options(empty));
    EXPECT_TRUE(empty.Exists("second"));
}

//The file holds smtp_password and mcp_token: rewriting it must never widen its
//mode, whatever the umask is.
TEST_F(UtilsConfigTest, PreservesFileMode)
{
    initConfig();

    ASSERT_EQ(::chmod(configFile().c_str(), 0600), 0);
    ASSERT_TRUE(Utils::set_config_option("smtp_password", "s3cret"));
    EXPECT_EQ(fileMode(configFile()), 0600u);

    ASSERT_TRUE(Utils::set_config_options(Params{ { "mcp_token", "t0ken" } }, { "smtp_password" }));
    EXPECT_EQ(fileMode(configFile()), 0600u);
    EXPECT_EQ(Utils::get_config_option("mcp_token"), "t0ken");

    //A deliberately relaxed mode is preserved too, we only copy the original
    ASSERT_EQ(::chmod(configFile().c_str(), 0640), 0);
    ASSERT_TRUE(Utils::set_config_option("smtp_password", "other"));
    EXPECT_EQ(fileMode(configFile()), 0640u);
}

//local_config.xml is often a symlink to persistent storage on an embedded
//overlay rootfs: the symlink must survive and the real file must be updated.
TEST_F(UtilsConfigTest, FollowsSymlinkedConfigFile)
{
    string realFile = persistDir + "/local_config.xml";
    writeConfigFile(realFile, option("existing", "value"));
    ASSERT_EQ(::symlink(realFile.c_str(), configFile().c_str()), 0);

    initConfig();
    ASSERT_TRUE(Utils::set_config_option("added", "value"));

    struct stat st;
    ASSERT_EQ(::lstat(configFile().c_str(), &st), 0);
    EXPECT_TRUE(S_ISLNK(st.st_mode)) << "the symlink was replaced by a regular file";

    string content = readFile(realFile);
    EXPECT_NE(content.find("\"added\""), string::npos) << "the real file was not updated";
    EXPECT_NE(content.find("\"existing\""), string::npos);

    EXPECT_EQ(Utils::get_config_option("added"), "value");

    //No leftover temporary file
    string cmd = "test -z \"$(ls -A '" + persistDir + "' | grep -v '^local_config.xml$')\"";
    EXPECT_EQ(::system(cmd.c_str()), 0) << "a temporary file was left behind";
}

//Deterministic check of the inter-process lock: while an flock() is held on
//local_config.xml.lock, another process cannot complete a write, and once it
//can it merges with what happened in the meantime.
TEST_F(UtilsConfigTest, WriterBlocksWhileTheLockIsHeld)
{
    initConfig();

    string lockPath = configFile() + ".lock";
    int lockFd = ::open(lockPath.c_str(), O_RDWR | O_CREAT, 0600);
    ASSERT_GE(lockFd, 0);
    ASSERT_EQ(::flock(lockFd, LOCK_EX), 0);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0)
    {
        //Do not keep the parent lock descriptor alive in the child
        ::close(lockFd);
        bool ok = Utils::set_config_option("child_key", "child_value");
        _exit(ok? 0: 1);
    }

    //The child cannot possibly finish while we hold the lock. This assertion
    //cannot flake: with a correct implementation the child is blocked in
    //flock() no matter how the scheduler behaves.
    for (int i = 0;i < 30;i++)
    {
        int status = 0;
        ASSERT_EQ(::waitpid(pid, &status, WNOHANG), 0) << "the writer did not wait for the lock";
        ::usleep(10000);
    }

    //Another program rewrites the file while the child is waiting
    writeConfigFile(configFile(), option("parent_key", "parent_value"));

    ASSERT_EQ(::flock(lockFd, LOCK_UN), 0);
    ::close(lockFd);

    int status = 0;
    ASSERT_EQ(::waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);

    EXPECT_EQ(Utils::get_config_option("parent_key"), "parent_value");
    EXPECT_EQ(Utils::get_config_option("child_key"), "child_value");
}

//Two processes writing different keys at the same time must not lose any of
//them. Both children are released by the same pipe so their writes overlap.
TEST_F(UtilsConfigTest, ConcurrentWritersLoseNoKey)
{
    initConfig();

    const int writers = 2;
    const int keysPerWriter = 25;

    int barrier[2];
    ASSERT_EQ(::pipe(barrier), 0);

    pid_t pids[writers];

    for (int w = 0;w < writers;w++)
    {
        pid_t pid = fork();
        ASSERT_GE(pid, 0);

        if (pid == 0)
        {
            ::close(barrier[1]);

            //Wait for the parent to release everybody at once
            char c = 0;
            if (::read(barrier[0], &c, 1) != 1)
                _exit(2);
            ::close(barrier[0]);

            bool ok = true;
            for (int i = 0;i < keysPerWriter;i++)
            {
                string key = "writer" + std::to_string(w) + "_key" + std::to_string(i);
                if (!Utils::set_config_option(key, "value"))
                    ok = false;
            }

            _exit(ok? 0: 1);
        }

        pids[w] = pid;
    }

    ::close(barrier[0]);
    ASSERT_EQ(::write(barrier[1], "gg", writers), writers);
    ::close(barrier[1]);

    for (int w = 0;w < writers;w++)
    {
        int status = 0;
        ASSERT_EQ(::waitpid(pids[w], &status, 0), pids[w]);
        ASSERT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }

    Params options;
    ASSERT_TRUE(Utils::get_config_options(options));

    for (int w = 0;w < writers;w++)
    {
        for (int i = 0;i < keysPerWriter;i++)
        {
            string key = "writer" + std::to_string(w) + "_key" + std::to_string(i);
            EXPECT_TRUE(options.Exists(key)) << "lost key " << key;
        }
    }

    //And the seeded options survived both writers
    EXPECT_EQ(Utils::get_config_option("cn_user"), "user");
}

//A directory that cannot be written to fails the whole operation and leaves
//the original file intact and parseable.
TEST_F(UtilsConfigTest, FailsCleanlyOnUnwritableDirectory)
{
    initConfig();
    ASSERT_TRUE(Utils::set_config_option("keep", "value"));

    string before = readFile(configFile());

    //Everything readable, nothing creatable in the config directory.
    //The lock file must stay usable so that the failure really happens in the
    //write path and not while taking the lock.
    ASSERT_EQ(::chmod(configFile().c_str(), 0644), 0);
    ASSERT_EQ(::chmod((configFile() + ".lock").c_str(), 0666), 0);
    ASSERT_EQ(::chmod(confDir.c_str(), 0555), 0);
    ASSERT_EQ(::chmod(base.c_str(), 0755), 0);

    //Directory permissions do not apply to root, drop privileges in a child
    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0)
    {
        if (geteuid() == 0)
        {
            if (::setgid(65534) != 0 || ::setuid(65534) != 0)
                _exit(2);
        }

        bool ok = Utils::set_config_option("new_key", "new_value");
        //The write must be refused
        _exit(ok? 1: 0);
    }

    int status = 0;
    ASSERT_EQ(::waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(WIFEXITED(status));

    if (WEXITSTATUS(status) == 2)
    {
        ::chmod(confDir.c_str(), 0755);
        GTEST_SKIP() << "could not drop privileges to test an unwritable directory";
    }

    EXPECT_EQ(WEXITSTATUS(status), 0) << "the write should have failed";

    ASSERT_EQ(::chmod(confDir.c_str(), 0755), 0);

    //Untouched, and still valid
    EXPECT_EQ(readFile(configFile()), before);
    EXPECT_EQ(Utils::get_config_option("keep"), "value");
    EXPECT_EQ(Utils::get_config_option("new_key"), "");

    //No temporary file was left behind
    string cmd = "test -z \"$(ls -A '" + confDir + "' | grep -v '^local_config.xml' )\"";
    EXPECT_EQ(::system(cmd.c_str()), 0) << "a temporary file was left behind";
}
