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
#ifndef CALAOS_TEST_EXTERN_PROC_SPAWN_HARNESS_H
#define CALAOS_TEST_EXTERN_PROC_SPAWN_HARNESS_H

/*******************************************************************************
 * Watching a sidecar be launched, ARGUMENT BOUNDARIES INCLUDED.
 *
 * ⭐ WHY THIS IS NOT core/RoonSpawnHarness.h. That recorder writes `"$*"`,
 * which joins argv back together with a space: it cannot tell `--host mon core`
 * (three arguments) from `--host` + `mon core` (two). Every question this
 * harness exists to answer is exactly that distinction, so the recorder here
 * writes one field per argv, separated by US (0x1F) - a byte no configuration
 * of this tree can produce and no shell touches.
 *
 * `$0` is recorded alongside `$@` so that a journal entry is the child's WHOLE
 * argv, argv[0] included, and a case can state the sidecar's own argc without
 * an off-by-one to reason about.
 *
 * ⚠️ ONE LINE PER LAUNCH, WRITTEN ONCE. The record is assembled in a shell
 * variable and emitted by a single printf into a file opened O_APPEND, so two
 * sidecars respawning at the same 100 ms cadence can never interleave into one
 * line. An argument containing a newline WOULD break that invariant; the cases
 * assert their fixtures carry none.
 *
 * ⚠️ Bounded on wall clock, never on iterations: a regression must be able to
 * fail a case, never to hang `make check`. Same rule as Timer_test.
 *
 * ⚠️ ONE SANDBOX PER PROCESS, created on first use, for the reason spelled out
 * at length in core/RoonSpawnHarness.h: the controllers cache `exe` in their
 * constructor and the journals must outlive a single case, or a second case
 * reads an empty journal while the still-running controller appends to the
 * first one - a red that says nothing about the code under test.
 *
 * ⚠️ ONE JOURNAL PER SIDECAR NAME. Several controllers are alive at once here
 * and all of them respawn forever; a shared journal could not attribute a line
 * to a controller. `install("calaos_mqtt")` and `install("calaos_1wire")` drop
 * two recorders in the same directory, each writing to its own file.
 *
 * teardown() gives back what _exit() would not: the spawned children, the
 * sandbox, and the unix sockets ~ExternProcServer never got to unlink.
 ******************************************************************************/

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "libuvw.h"

namespace CalaosTest
{
namespace ExternProcSpawn
{

//The separator the recorder puts between two argv. US, 0x1F.
static const char kArgSep = '\037';

//The one sandbox of this process. `dir` stays empty when mkdtemp() fails, so a
//case FAILS instead of quietly pointing CALAOS_BIN_PREFIX at the real prefix.
inline const std::string &sandboxDir()
{
    static std::string dir = []() -> std::string
    {
        char tmpl[] = "/tmp/calaos_externproc_spawn_XXXXXX";
        const char *d = ::mkdtemp(tmpl);
        return d? std::string(d) : std::string();
    }();

    return dir;
}

inline std::string journalPath(const std::string &binName)
{
    return sandboxDir() + "/" + binName + ".journal";
}

//The names install() has been called with, so teardown() knows what to remove.
inline std::vector<std::string> &installed()
{
    static std::vector<std::string> names;
    return names;
}

/*
 * Drop the stand-in sidecar and point the production code at it.
 *
 * Idempotent: safe to call from every case and every repetition of one. The
 * journal path is baked into the script rather than read from the environment
 * because startProcess() hands the child an explicit environment which
 * forwards nothing of ours.
 */
/*
 * `exitCode` and `holdSeconds` shape a sidecar that FAILS: a non-zero status is
 * what a broker that is switched off leaves behind, and a hold is what
 * separates a child that dies on the spot from one that ran before dying. The
 * record is written BEFORE the hold, so a launch is journalled at the moment it
 * happens whatever the child does next.
 */
inline bool install(const std::string &binName, int exitCode = 0,
                    double holdSeconds = 0.0)
{
    if (sandboxDir().empty())
        return false;

    const std::string script = sandboxDir() + "/" + binName;

    {
        std::ofstream f(script.c_str(), std::ios::out | std::ios::trunc);
        if (!f.is_open())
            return false;
        f << "#!/bin/sh\n"
             "sep=$(printf '\\037')\n"
             "rec=\n"
             "for a in \"$0\" \"$@\"; do rec=\"$rec$sep$a\"; done\n"
             "printf '%s\\n' \"$rec\" >> '" << journalPath(binName) << "'\n";
        if (holdSeconds > 0.0)
            f << "sleep " << holdSeconds << "\n";
        f << "exit " << exitCode << "\n";
        if (!f.good())
            return false;
    }

    if (::chmod(script.c_str(), 0755) != 0)
        return false;

    bool known = false;
    for (const std::string &n: installed())
        known = known || (n == binName);
    if (!known)
        installed().push_back(binName);

    return ::setenv("CALAOS_BIN_PREFIX", sandboxDir().c_str(), 1) == 0;
}

//One entry per launch, in order, each entry the argv the kernel handed the
//child. A missing journal answers empty, which is a failure of the case and
//not of the reader.
inline std::vector<std::vector<std::string>> launches(const std::string &binName)
{
    std::vector<std::vector<std::string>> out;
    std::ifstream f(journalPath(binName).c_str());
    std::string line;

    while (std::getline(f, line))
    {
        if (line.empty())
            continue;

        std::vector<std::string> argv;
        std::string::size_type pos = 0;

        //The record opens with the separator, so the leading empty field is
        //not an argument.
        while (pos < line.size())
        {
            if (line[pos] != kArgSep)
                break;
            const std::string::size_type next = line.find(kArgSep, pos + 1);
            argv.push_back(line.substr(pos + 1,
                                       next == std::string::npos?
                                           std::string::npos : next - pos - 1));
            pos = (next == std::string::npos)? line.size() : next;
        }

        out.push_back(argv);
    }

    return out;
}

//Run the default loop until pred() holds, with a wall clock deadline. Same
//shape as core/RoonSpawnHarness.h's runLoopUntil().
inline bool runLoopUntil(const std::function<bool()> &pred, int timeoutMs)
{
    auto loop = uvw::Loop::getDefault();
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);

    while (!pred())
    {
        if (std::chrono::steady_clock::now() > deadline)
            return false;

        loop->run<uvw::Loop::Mode::NOWAIT>();
        ::usleep(2000);
    }

    return true;
}

/*
 * Reap whatever the spawns left behind.
 *
 * ⚠️ WNOHANG and a wall clock budget, never a blocking waitpid(): a teardown
 * that blocks on a stuck child would hang `make check`, the one thing this
 * harness must never do.
 */
inline int reapChildren(int budgetMs)
{
    int reaped = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(budgetMs);

    for (;;)
    {
        int status = 0;
        const pid_t p = ::waitpid(-1, &status, WNOHANG);

        if (p > 0)
        {
            reaped++;
            continue;
        }

        if (p < 0)
            break;              //ECHILD - nothing of ours is left

        if (std::chrono::steady_clock::now() > deadline)
            break;

        ::usleep(2000);
    }

    return reaped;
}

/*
 * Unlink the ExternProcServer sockets THIS PROCESS bound.
 *
 * ~ExternProcServer does it and never runs here, because main() leaves by
 * _exit(). The name carries our pid, so matching on the suffix cannot touch a
 * socket belonging to another test binary of the same `make check -jN`.
 */
inline int removeOwnSockets()
{
    const std::string suffix = "_" + std::to_string(static_cast<long>(::getpid()));
    const std::string prefix = "calaos_proc_";
    int removed = 0;

    DIR *d = ::opendir("/tmp");
    if (!d)
        return 0;

    for (struct dirent *e = ::readdir(d); e; e = ::readdir(d))
    {
        const std::string name = e->d_name;
        if (name.size() <= prefix.size() + suffix.size())
            continue;
        if (name.compare(0, prefix.size(), prefix) != 0)
            continue;
        if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;

        if (::unlink(("/tmp/" + name).c_str()) == 0)
            removed++;
    }

    ::closedir(d);
    return removed;
}

//Everything the harness introduced, given back. Call from main(), BEFORE
//_exit(), and never from a TearDown(): the sandbox outlives the cases.
inline void teardown()
{
    reapChildren(1000);

    if (sandboxDir().empty())
        return;

    for (const std::string &n: installed())
    {
        ::unlink(journalPath(n).c_str());
        ::unlink((sandboxDir() + "/" + n).c_str());
    }

    ::rmdir(sandboxDir().c_str());

    removeOwnSockets();
}

} //namespace ExternProcSpawn
} //namespace CalaosTest

#endif //CALAOS_TEST_EXTERN_PROC_SPAWN_HARNESS_H
