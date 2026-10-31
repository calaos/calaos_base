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
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/

//The bytes Logger writes for each level, per destination of stdout. Compared
//whole: journald takes the priority from the first bytes of each line, and a
//terminal shows every byte a test by substring would let through.

#include <gtest/gtest.h>

#include "Logger.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

using std::string;
using std::vector;

namespace
{

const char *const VALID_CONFIG =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
    "<calaos:config xmlns:calaos=\"http://www.calaos.fr\">\n"
    "<calaos:option name=\"debug_domains\" value=\"prio:5\" />\n"
    "</calaos:config>\n";

const char *const PLAIN_LEVELS =
    "[CRI] prio (f.cpp:1) critical\n"
    "[ERR] prio (f.cpp:2) error\n"
    "[WRN] prio (f.cpp:3) warning\n"
    "[INF] prio (f.cpp:4) info\n"
    "[DBG] prio (f.cpp:5) debug\n"
    "[???] prio (f.cpp:6) unknown\n";

const char *const JOURNAL_LEVELS =
    "<2>[CRI] prio (f.cpp:1) critical\n"
    "<3>[ERR] prio (f.cpp:2) error\n"
    "<4>[WRN] prio (f.cpp:3) warning\n"
    "<6>[INF] prio (f.cpp:4) info\n"
    "<7>[DBG] prio (f.cpp:5) debug\n"
    "<6>[???] prio (f.cpp:6) unknown\n";

//A terminal turns \n into \r\n
const char *const COLOR_LEVELS =
    "\033[35m[CRI]\033[36m prio\033[0m \033[1m(f.cpp:1)\033[0m critical\033[0m\r\n"
    "\033[31m[ERR]\033[36m prio\033[0m \033[1m(f.cpp:2)\033[0m error\033[0m\r\n"
    "\033[33m[WRN]\033[36m prio\033[0m \033[1m(f.cpp:3)\033[0m warning\033[0m\r\n"
    "\033[32m[INF]\033[36m prio\033[0m \033[1m(f.cpp:4)\033[0m info\033[0m\r\n"
    "\033[34m[DBG]\033[36m prio\033[0m \033[1m(f.cpp:5)\033[0m debug\033[0m\r\n"
    "\033[31m\033[1m[???]\033[36m prio\033[0m \033[1m(f.cpp:6)\033[0m unknown\033[0m\r\n";

enum class Destination { Pipe, File, Terminal };

}

class LogJournalPriorityTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        char tmpl[] = "/tmp/calaos_logprio_XXXXXX";
        const char *d = ::mkdtemp(tmpl);
        ASSERT_NE(d, nullptr);
        base = d;

        confDir = base + "/config";
        ASSERT_EQ(::mkdir(confDir.c_str(), 0755), 0);

        std::ofstream f((confDir + "/local_config.xml").c_str());
        f << VALID_CONFIG;
        f.close();

        helper = findHelper();
        ASSERT_FALSE(helper.empty()) << "LogJournalPriority_helper was not built";
    }

    void TearDown() override
    {
        const string cmd = "rm -rf '" + base + "'";
        if (::system(cmd.c_str()) != 0)
            std::cerr << "warning: could not clean " << base << std::endl;
    }

    static string findHelper()
    {
        const char *builddir = getenv("abs_top_builddir");

        if (builddir)
        {
            const string p = string(builddir) + "/tests/LogJournalPriority_helper";
            if (::access(p.c_str(), X_OK) == 0)
                return p;
        }

        const string local = "./LogJournalPriority_helper";
        if (::access(local.c_str(), X_OK) == 0)
            return local;

        return string();
    }

    //Whatever the caller's shell has set must not decide a case
    vector<string> helperEnvironment(const vector<string> &extra)
    {
        vector<string> env;
        for (char **e = environ; *e; e++)
        {
            const string var(*e);
            if (var.rfind("TERM=", 0) != 0 &&
                var.rfind("CALAOS_FORCE_COLOR=", 0) != 0 &&
                var.rfind("CALAOS_JOURNAL_PRIORITY=", 0) != 0 &&
                var.rfind("CALAOS_CONFIG=", 0) != 0 &&
                var.rfind("HOME=", 0) != 0)
                env.push_back(var);
        }

        env.push_back("CALAOS_CONFIG=" + confDir);
        env.push_back("HOME=" + base);
        env.insert(env.end(), extra.begin(), extra.end());
        return env;
    }

    //fd is what the helper gets as stdout; readFd, if any, is drained here
    string runOn(int fd, int readFd, const string &mode, const vector<string> &extraEnv)
    {
        vector<string> env = helperEnvironment(extraEnv);
        vector<char *> envp;
        for (string &e: env)
            envp.push_back(e.data());
        envp.push_back(nullptr);

        string mutableMode = mode;
        char *argv[] = { helper.data(), mutableMode.data(), nullptr };

        const pid_t pid = ::fork();
        if (pid < 0)
        {
            ADD_FAILURE() << "fork failed";
            return string();
        }

        if (pid == 0)
        {
            ::dup2(fd, STDOUT_FILENO);
            ::dup2(fd, STDERR_FILENO);
            ::execve(helper.c_str(), argv, envp.data());
            ::_exit(127);
        }

        ::close(fd);

        string out;
        if (readFd >= 0)
            out = drain(readFd);

        int status = 0;
        ::waitpid(pid, &status, 0);
        EXPECT_TRUE(WIFEXITED(status)) << "helper did not exit normally";
        EXPECT_EQ(WEXITSTATUS(status), 0) << "helper failed for mode " << mode;
        return out;
    }

    //A hung helper must fail the case, not `make check`. The end of the
    //output is 0 on a pipe and EIO on the master side of a terminal.
    static string drain(int fd)
    {
        string out;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);

        while (std::chrono::steady_clock::now() < deadline)
        {
            struct pollfd p = { fd, POLLIN, 0 };
            if (::poll(&p, 1, 200) <= 0)
                continue;

            char buf[4096];
            const ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n <= 0)
                return out;
            out.append(buf, static_cast<size_t>(n));
        }

        ADD_FAILURE() << "the helper did not close its stdout in time";
        return out;
    }

    string runOnPipe(const string &mode, const vector<string> &extraEnv)
    {
        int fds[2];
        if (::pipe(fds) != 0)
        {
            ADD_FAILURE() << "pipe failed";
            return string();
        }

        string out = runOn(fds[1], fds[0], mode, extraEnv);
        ::close(fds[0]);
        return out;
    }

    string runOnFile(const string &mode, const vector<string> &extraEnv)
    {
        const string path = base + "/stdout.txt";
        const int fd = ::open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (fd < 0)
        {
            ADD_FAILURE() << "could not create " << path;
            return string();
        }

        runOn(fd, -1, mode, extraEnv);

        std::ifstream f(path.c_str());
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    string runOnTerminal(const string &mode, const vector<string> &extraEnv)
    {
        const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
        if (master < 0 || ::grantpt(master) != 0 || ::unlockpt(master) != 0)
        {
            ADD_FAILURE() << "no pseudo terminal available";
            return string();
        }

        const int slave = ::open(::ptsname(master), O_RDWR | O_NOCTTY);
        if (slave < 0)
        {
            ADD_FAILURE() << "could not open the terminal side";
            ::close(master);
            return string();
        }

        string out = runOn(slave, master, mode, extraEnv);
        ::close(master);
        return out;
    }

    string run(Destination where, const string &mode, const vector<string> &extraEnv)
    {
        switch (where)
        {
        case Destination::Pipe: return runOnPipe(mode, extraEnv);
        case Destination::File: return runOnFile(mode, extraEnv);
        case Destination::Terminal: return runOnTerminal(mode, extraEnv);
        }
        return string();
    }

    string base, confDir, helper;
};

TEST_F(LogJournalPriorityTest, EachLevelStartsWithItsPriorityWhenTheVariableIsSet)
{
    EXPECT_EQ(runOnPipe("levels", { "CALAOS_JOURNAL_PRIORITY=1" }), JOURNAL_LEVELS);
}

TEST_F(LogJournalPriorityTest, EveryLineOfAMessageCarriesThePriority)
{
    EXPECT_EQ(runOnPipe("multiline", { "CALAOS_JOURNAL_PRIORITY=1" }),
              "<3>[ERR] prio (f.cpp:1) first\n"
              "<3>second\n"
              "<3>third\n"
              "<4>[WRN] prio (f.cpp:2) ends with a newline\n"
              "<4>\n");
}

TEST_F(LogJournalPriorityTest, AnExplicitPriorityReplacesTheOneOfTheLevel)
{
    EXPECT_EQ(runOnPipe("override", { "CALAOS_JOURNAL_PRIORITY=1" }),
              "<3>[DBG] prio (f.cpp:1) from a sidecar\n"
              "<7>[DBG] prio (f.cpp:2) no priority given\n");
}

TEST_F(LogJournalPriorityTest, AnExplicitPriorityWritesNothingWithoutTheVariable)
{
    EXPECT_EQ(runOnPipe("override", {}),
              "[DBG] prio (f.cpp:1) from a sidecar\n"
              "[DBG] prio (f.cpp:2) no priority given\n");
}

//TERM alone must not bring ANSI into the journal either
TEST_F(LogJournalPriorityTest, TheJournalModeWinsOverForcedColor)
{
    EXPECT_EQ(runOnPipe("levels", { "CALAOS_JOURNAL_PRIORITY=1", "CALAOS_FORCE_COLOR=1", "TERM=xterm" }),
              JOURNAL_LEVELS);
    EXPECT_EQ(runOnPipe("state", { "CALAOS_JOURNAL_PRIORITY=1", "CALAOS_FORCE_COLOR=1" }),
              "journal=1 color=0\n");
}

TEST_F(LogJournalPriorityTest, ForcedColorIsUnchangedWithoutTheVariable)
{
    string expected = COLOR_LEVELS;
    for (size_t pos = expected.find("\r\n"); pos != string::npos; pos = expected.find("\r\n", pos))
        expected.erase(pos, 1);

    EXPECT_EQ(runOnPipe("levels", { "CALAOS_FORCE_COLOR=1" }), expected);
}

TEST_F(LogJournalPriorityTest, NoPrefixWithoutTheExactValue)
{
    for (const Destination where: { Destination::Pipe, Destination::File })
    {
        EXPECT_EQ(run(where, "levels", {}), PLAIN_LEVELS);
        EXPECT_EQ(run(where, "levels", { "CALAOS_JOURNAL_PRIORITY=0" }), PLAIN_LEVELS);
        EXPECT_EQ(run(where, "levels", { "CALAOS_JOURNAL_PRIORITY=" }), PLAIN_LEVELS);
        EXPECT_EQ(run(where, "levels", { "CALAOS_JOURNAL_PRIORITY=yes" }), PLAIN_LEVELS);
        EXPECT_EQ(run(where, "multiline", {}),
                  "[ERR] prio (f.cpp:1) first\nsecond\nthird\n"
                  "[WRN] prio (f.cpp:2) ends with a newline\n\n");
    }
}

TEST_F(LogJournalPriorityTest, ATerminalKeepsItsColorsAndShowsNoPrefix)
{
    EXPECT_EQ(runOnTerminal("levels", { "TERM=xterm" }), COLOR_LEVELS);
    EXPECT_EQ(runOnTerminal("levels", { "TERM=xterm", "CALAOS_JOURNAL_PRIORITY=1" }), COLOR_LEVELS);
    EXPECT_EQ(runOnTerminal("state", { "TERM=xterm", "CALAOS_JOURNAL_PRIORITY=1" }),
              "journal=0 color=1\r\n");
}

TEST_F(LogJournalPriorityTest, ATerminalWithoutColorsShowsNoPrefixEither)
{
    string expected = PLAIN_LEVELS;
    for (size_t pos = expected.find('\n'); pos != string::npos; pos = expected.find('\n', pos + 2))
        expected.insert(pos, "\r");

    EXPECT_EQ(runOnTerminal("levels", { "TERM=dumb", "CALAOS_JOURNAL_PRIORITY=1" }), expected);
}

TEST(LogJournalPriorityParse, TakesALeadingPriority)
{
    for (int n = 0; n <= 7; n++)
    {
        string line = "<" + std::to_string(n) + ">text";
        EXPECT_EQ(Logger::takeJournalPriority(line), n);
        EXPECT_EQ(line, "text");
    }

    string alone = "<3>";
    EXPECT_EQ(Logger::takeJournalPriority(alone), 3);
    EXPECT_EQ(alone, "");
}

TEST(LogJournalPriorityParse, LeavesEverythingElseUntouched)
{
    for (const char *text: { "", "<", "<3", "<8>text", "<a>text", "<33>text", " <3>text", "text <3>" })
    {
        string line = text;
        EXPECT_EQ(Logger::takeJournalPriority(line), -1) << text;
        EXPECT_EQ(line, text);
    }
}
