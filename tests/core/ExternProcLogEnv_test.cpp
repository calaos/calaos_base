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

//What a sidecar is told about logging, and what becomes of the priority it
//writes. Logger reads its mode once at static initialisation, so every case
//re-executes this binary as a probe, with its stdout away from any terminal,
//and goes through a real OwCtrl and a stand-in calaos_1wire.

#include <gtest/gtest.h>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "ConfigStore.h"
#include "LogSetup.h"
#include "OWCtrl.h"
#include "libuvw.h"

extern char **environ;

using namespace Calaos;

namespace
{

const char *const kProbeFlag = "--log-env-probe";

//What the stand-in prints. Stdout and stderr reach the relay on two pipes, so
//there is one last line to wait for on each.
const char *const kLastOut = "<6>stand-in-last-stdout-line";
const char *const kLastErr = "<2>[CRI] d (a.cpp:3) stand-in-last-stderr-line";

bool writeTextFile(const std::string &path, const std::string &text)
{
    std::ofstream f(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
    f << text;
    return f.good();
}

bool exists(const std::string &path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

std::string readFile(const std::string &path)
{
    std::ifstream f(path.c_str(), std::ios::in | std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::vector<std::string> linesOf(const std::string &text)
{
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
        lines.push_back(line);
    return lines;
}

bool pumpLoopUntil(const std::function<bool()> &pred, int timeoutMs)
{
    auto loop = uvw::Loop::getDefault();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    for (;;)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();

        if (pred())
            return true;
        if (std::chrono::steady_clock::now() > deadline)
            return false;

        ::usleep(2000);
    }
}

/* Runs inside the re-executed binary. The output is taken at std::cout: the
 * mode itself was read from the real stdout, which stays /dev/null.
 */
int runProbe(const std::string &dir, const std::string &domains)
{
    const std::string cfg = dir + "/config";
    const std::string cache = dir + "/cache";
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(cache.c_str(), 0700);

    Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                             const_cast<char *>(cache.c_str()), true);
    if (!domains.empty())
        Utils::set_config_option("debug_domains", domains);

    const std::string script =
        std::string("#!/bin/sh\n"
                    "env > '") +
        dir + "/env.tmp'\n"
              "mv '" +
        dir + "/env.tmp' '" + dir + "/env.txt'\n"
                                    "printf '%s\\n' '<3>[ERR] d (a.cpp:1) stand-in-error'\n"
                                    "printf '%s\\n' '<4>[WRN] d (a.cpp:2) stand-in-warning'\n"
                                    "printf '%s\\n' 'stand-in-bare-line'\n"
                                    "printf '%s\\n' '<9>stand-in-out-of-range'\n"
                                    "printf '%s\\n' '" +
        kLastOut + "'\n"
                   "printf '%s\\n' '" +
        kLastErr + "' >&2\n";

    if (!writeTextFile(dir + "/calaos_1wire", script) ||
        ::chmod((dir + "/calaos_1wire").c_str(), 0755) != 0 ||
        ::setenv("CALAOS_BIN_PREFIX", dir.c_str(), 1) != 0)
        return 3;

    std::ostringstream sink;
    std::streambuf *saved = std::cout.rdbuf(sink.rdbuf());

    OwCtrl::Instance("");

    bool finished;
    if (domains.empty())
    {
        //A silence has no last line to wait for: the witness says the
        //stand-in runs, then the relay gets the time to read its output
        finished = pumpLoopUntil([&]()
                                 {
            return exists(dir + "/env.txt");
        },
                                 8000);
        pumpLoopUntil([]()
                      {
            return false;
        },
                      300);
    }
    else
    {
        finished = pumpLoopUntil([&]()
                                 {
            const std::string out = sink.str();
            return out.find("stand-in-last-stdout-line") != std::string::npos &&
                   out.find("stand-in-last-stderr-line") != std::string::npos;
        },
                                 8000);
    }

    std::cout.rdbuf(saved);

    if (!writeTextFile(dir + "/out.txt", sink.str()))
        return 4;

    return finished ? 0 : 5;
}

struct Probe
{
    bool ran = false;
    std::vector<std::string> env;
    std::vector<std::string> out;
};

Probe probeWith(const std::vector<std::string> &extraEnv, const std::string &domains = "process:5")
{
    Probe result;

    char tmpl[] = "/tmp/calaos_logenv_XXXXXX";
    const char *d = ::mkdtemp(tmpl);
    if (!d)
        return result;
    const std::string dir = d;

    std::vector<std::string> env;
    for (char **e = environ; *e; e++)
    {
        const std::string var(*e);
        if (var.rfind("TERM=", 0) != 0 &&
            var.rfind("CALAOS_FORCE_COLOR=", 0) != 0 &&
            var.rfind("CALAOS_JOURNAL_PRIORITY=", 0) != 0)
            env.push_back(var);
    }
    env.insert(env.end(), extraEnv.begin(), extraEnv.end());

    std::vector<char *> envp;
    for (std::string &e: env)
        envp.push_back(e.data());
    envp.push_back(nullptr);

    std::string self = "/proc/self/exe";
    std::string flag = kProbeFlag;
    std::string dirArg = dir;
    std::string domainsArg = domains;
    char *argv[] = { self.data(), flag.data(), dirArg.data(), domainsArg.data(), nullptr };

    const pid_t pid = ::fork();
    if (pid < 0)
        return result;

    if (pid == 0)
    {
        const int devnull = ::open("/dev/null", O_WRONLY);
        ::dup2(devnull, STDOUT_FILENO);
        ::execve("/proc/self/exe", argv, envp.data());
        ::_exit(127);
    }

    int status = 0;
    ::waitpid(pid, &status, 0);

    result.ran = WIFEXITED(status) && WEXITSTATUS(status) == 0 && exists(dir + "/env.txt");
    result.env = linesOf(readFile(dir + "/env.txt"));
    result.out = linesOf(readFile(dir + "/out.txt"));

    const std::string cmd = "rm -rf '" + dir + "'";
    if (::system(cmd.c_str()) != 0)
        std::cerr << "warning: could not clean " << dir << std::endl;

    return result;
}

//The value of a variable as the child received it. Every line naming it is
//returned, so that a truncated or doubled name cannot pass.
std::vector<std::string> linesNaming(const std::vector<std::string> &env, const std::string &fragment)
{
    std::vector<std::string> found;
    for (const std::string &line: env)
    {
        if (line.find(fragment) != std::string::npos)
            found.push_back(line);
    }
    return found;
}

using Lines = std::vector<std::string>;

//The server line that relays `text`, whatever the line number of the relay.
std::string relayLine(const std::vector<std::string> &out, const std::string &text)
{
    for (const std::string &line: out)
    {
        if (line.size() >= text.size() &&
            line.compare(line.size() - text.size(), text.size(), text) == 0)
            return line;
    }
    return std::string();
}

bool startsWith(const std::string &line, const std::string &head)
{
    return line.rfind(head, 0) == 0;
}

} // namespace

TEST(ExternProcLogEnvTest, TheSidecarIsToldColorIsOff)
{
    const Probe p = probeWith({});
    ASSERT_TRUE(p.ran);

    EXPECT_EQ(linesNaming(p.env, "FORCE_COLOR"), Lines({ "CALAOS_FORCE_COLOR=0" }));
    EXPECT_EQ(linesNaming(p.env, "JOURNAL_PRIORITY"), Lines({ "CALAOS_JOURNAL_PRIORITY=0" }));
}

TEST(ExternProcLogEnvTest, TheSidecarIsToldColorIsOn)
{
    const Probe p = probeWith({ "CALAOS_FORCE_COLOR=1" });
    ASSERT_TRUE(p.ran);

    EXPECT_EQ(linesNaming(p.env, "FORCE_COLOR"), Lines({ "CALAOS_FORCE_COLOR=1" }));
    EXPECT_EQ(linesNaming(p.env, "JOURNAL_PRIORITY"), Lines({ "CALAOS_JOURNAL_PRIORITY=0" }));
}

TEST(ExternProcLogEnvTest, TheJournalModeReachesTheSidecarWithoutColor)
{
    const Probe p = probeWith({ "CALAOS_JOURNAL_PRIORITY=1", "CALAOS_FORCE_COLOR=1" });
    ASSERT_TRUE(p.ran);

    EXPECT_EQ(linesNaming(p.env, "FORCE_COLOR"), Lines({ "CALAOS_FORCE_COLOR=0" }));
    EXPECT_EQ(linesNaming(p.env, "JOURNAL_PRIORITY"), Lines({ "CALAOS_JOURNAL_PRIORITY=1" }));
}

TEST(ExternProcLogEnvTest, TheRelayCarriesThePriorityTheSidecarWrote)
{
    const Probe p = probeWith({ "CALAOS_JOURNAL_PRIORITY=1" });
    ASSERT_TRUE(p.ran);

    const std::string error = relayLine(p.out, "1wire stdout: [ERR] d (a.cpp:1) stand-in-error");
    EXPECT_TRUE(startsWith(error, "<3>[DBG] process (")) << error;

    const std::string warning = relayLine(p.out, "1wire stdout: [WRN] d (a.cpp:2) stand-in-warning");
    EXPECT_TRUE(startsWith(warning, "<4>[DBG] process (")) << warning;

    const std::string critical = relayLine(p.out, "1wire stderr: [CRI] d (a.cpp:3) stand-in-last-stderr-line");
    EXPECT_TRUE(startsWith(critical, "<2>[DBG] process (")) << critical;
}

TEST(ExternProcLogEnvTest, ALineWithoutAValidPriorityStaysADebugLine)
{
    const Probe p = probeWith({ "CALAOS_JOURNAL_PRIORITY=1" });
    ASSERT_TRUE(p.ran);

    const std::string bare = relayLine(p.out, "1wire stdout: stand-in-bare-line");
    EXPECT_TRUE(startsWith(bare, "<7>[DBG] process (")) << bare;

    const std::string outOfRange = relayLine(p.out, "1wire stdout: <9>stand-in-out-of-range");
    EXPECT_TRUE(startsWith(outOfRange, "<7>[DBG] process (")) << outOfRange;
}

TEST(ExternProcLogEnvTest, OutsideTheJournalModeTheRelayLeavesTheTextAlone)
{
    const Probe p = probeWith({});
    ASSERT_TRUE(p.ran);

    const std::string line = relayLine(p.out, "1wire stdout: <3>[ERR] d (a.cpp:1) stand-in-error");
    EXPECT_TRUE(startsWith(line, "[DBG] process (")) << line;
}

//The priority goes with the line, it does not open the gate of the domain: a
//stock install stays deaf to what its sidecars print.
TEST(ExternProcLogEnvTest, TheJournalModeDoesNotMakeASidecarAudible)
{
    const Probe p = probeWith({ "CALAOS_JOURNAL_PRIORITY=1" }, "");
    ASSERT_TRUE(p.ran);

    for (const std::string &line: p.out)
        EXPECT_EQ(line.find("stand-in-"), std::string::npos) << line;
}

int main(int argc, char **argv)
{
    if (argc == 4 && std::string(argv[1]) == kProbeFlag)
    {
        const int ret = runProbe(argv[2], argv[3]);
        fflush(nullptr);
        _exit(ret);
    }

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
