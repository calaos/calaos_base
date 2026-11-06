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
#ifndef CALAOS_TEST_ROON_SPAWN_HARNESS_H
#define CALAOS_TEST_ROON_SPAWN_HARNESS_H

/*******************************************************************************
 * T3.28b - WATCHING calaos_roon BE LAUNCHED FOR REAL, from a test.
 *
 * ⭐ WHAT IT DISPROVES. T3.28 justified its two source tripwires by writing
 * that "RoonCtrl::Instance() is a static singleton whose constructor builds an
 * ExternProcServer - which binds a unix socket - and spawns calaos_roon.
 * Nothing in `make check` can construct a RoonCtrl". Measured on this tree,
 * every clause of that sentence is true and the conclusion drawn from it is
 * not:
 *
 *   - Instance() is PUBLIC and static, and Audio/RoonPlayer.$(OBJEXT) is
 *     linked by seventeen test binaries. Nothing hides it.
 *   - Binding a unix socket in /tmp is not a blocker: ExternProcServer's
 *     constructor does exactly that and tests/core/KnxIo_test.cpp already
 *     builds a real KNXCtrl - the same shape, the same ExternProcServer, the
 *     same spawn - on every `make check` run.
 *   - Spawning is not a blocker either. It is an OBSERVATION POINT: what the
 *     sidecar is started with is the only thing T3.28 ever cared about.
 *
 * HOW. Prefix::binDirectoryGet() (src/lib/Prefix.cpp:31-37) answers
 * getenv("CALAOS_BIN_PREFIX") and does NOT cache it, so a test can point
 * `exe` at a directory of its own. We drop a `calaos_roon` there that appends
 * its own argv to a journal and exits; ExternProcServer's ExitEvent handler
 * then arms the 0.1s respawn timer (IO/ExternProc.cpp:187-199) and RoonCtrl
 * relaunches. Pumping the loop for a bounded time therefore yields ONE LINE
 * PER LAUNCH, first launch and respawn alike, spelled by the production code
 * end to end.
 *
 * ⚠️ Bounded on wall clock, never on iterations: a regression must be able to
 * fail a case, never to hang `make check`. Same rule as Timer_test.
 *
 * ---------------------------------------------------------------------------
 * ⚠️ WHY THE SANDBOX IS PROCESS-SCOPED AND NOT PER-CASE - IT IS NOT A
 * CONVENIENCE
 * ---------------------------------------------------------------------------
 * The RoonCtrl singleton caches `exe` in its constructor and is built ONCE per
 * process. A per-case mkdtemp() would therefore hand the SECOND case a fresh,
 * permanently empty journal while the still-running controller kept appending
 * to the first one - a red that says nothing about the code under test. The
 * review of this ticket met exactly that: `--gtest_repeat=2` turned the
 * execution case RED, and a maintainer hunting a flaky suite with that very
 * tool would have believed they had found it.
 *
 * One sandbox per PROCESS, created on first use, matches the lifetime of the
 * thing being observed. Re-entering install() is then a no-op, the journal
 * simply keeps growing, and every line in it is still checked - so a case
 * built on this harness is REPLAYABLE inside one process.
 *
 * ⛔ THAT DOES NOT MAKE THE SUITES REPEAT-SAFE AS A WHOLE. CalaosCoreFixture.h
 * says it in its own header: "core tests must not be run with
 * gtest_repeat/threads in parallel inside a single process", because Config,
 * ListeRoom and friends are process-wide singletons with no reset API. This
 * harness closes ITS OWN contribution to that, nothing more.
 *
 * ---------------------------------------------------------------------------
 * ⚠️ RESIDUE - MEASURED, AND CLOSED BY teardown()
 * ---------------------------------------------------------------------------
 * The binaries that use this harness end on _exit(), like tests/core/KnxIo_test
 * and for the same reason (see the main() of either suite), so NO destructor
 * runs at process exit. Left alone, each run of `make check` therefore leaked,
 * measured: one /tmp sandbox directory (0700, two files), one bound unix socket
 * /tmp/calaos_proc_<uuid>_roon_<pid> that ~ExternProcServer never got to
 * unlink, and one `calaos_roon <defunct>` - the last child spawned, which exits
 * after the loop has stopped being pumped so nobody is left to reap it.
 *
 * ⚠️ The zombie is invisible under a normal shell and NOT under a container
 * started on `sleep infinity`: PID 1 there does not reap orphans, so the
 * <defunct> entries accumulate for the life of the container - which is
 * precisely the build environment this project's agents run in.
 *
 * teardown() closes all three, explicitly, from main() before _exit():
 * waitpid(WNOHANG) in a wall-clock bounded loop, the two files and the
 * directory, and the socket THIS PROCESS bound (its own pid is in the name, so
 * a concurrent `make check -jN` cannot be robbed of its socket).
 ******************************************************************************/

#include <dirent.h>
#include <signal.h>
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
namespace RoonSpawn
{

struct Sandbox
{
    std::string dir;        //empty when the sandbox could not be created
    std::string script;     //<dir>/calaos_roon
    std::string journal;    //<dir>/argv.journal
};

/*
 * Write the stand-in calaos_roon.
 *
 * `printf '%s\n' "$*"` writes ONE line per launch, in a single write() to a
 * file opened O_APPEND, so two launches can never interleave into one line.
 * The journal path is baked in at write time rather than read from the
 * environment: startProcess() hands the child an explicit environment
 * (IO/ExternProc.cpp:260-275) which does not forward CALAOS_BIN_PREFIX or
 * anything of ours.
 */
inline bool writeSpawnRecorder(const std::string &script, const std::string &journal)
{
    {
        std::ofstream f(script.c_str(), std::ios::out | std::ios::trunc);
        if (!f.is_open())
            return false;
        f << "#!/bin/sh\n"
             "printf '%s\\n' \"$*\" >> '" << journal << "'\n"
             "exit 0\n";
        if (!f.good())
            return false;
    }
    return ::chmod(script.c_str(), 0755) == 0;
}

//The one sandbox of this process. Created on first use; `dir` stays empty when
//mkdtemp() fails, so a case FAILS instead of quietly pointing CALAOS_BIN_PREFIX
//at the real install prefix.
inline Sandbox &sandbox()
{
    static Sandbox sb = []() -> Sandbox
    {
        Sandbox s;
        char tmpl[] = "/tmp/calaos_roon_spawn_XXXXXX";
        const char *d = ::mkdtemp(tmpl);
        if (!d)
            return s;

        s.dir = d;
        s.script = s.dir + "/calaos_roon";
        s.journal = s.dir + "/argv.journal";

        if (!writeSpawnRecorder(s.script, s.journal))
            s.dir.clear();

        return s;
    }();

    return sb;
}

//Point the production code at the sandbox. Idempotent: safe to call from every
//case, and from every repetition of the same case.
inline bool install()
{
    const Sandbox &sb = sandbox();
    if (sb.dir.empty())
        return false;

    return ::setenv("CALAOS_BIN_PREFIX", sb.dir.c_str(), 1) == 0;
}

//One entry per launch, in order. A missing journal answers empty, which is a
//failure of the case and not of the reader.
inline std::vector<std::string> journalLines()
{
    std::vector<std::string> lines;
    std::ifstream f(sandbox().journal.c_str());
    std::string line;

    while (std::getline(f, line))
    {
        if (!line.empty())
            lines.push_back(line);
    }

    return lines;
}

//Run the default loop until pred() holds, with a wall clock deadline. Same
//shape as tests/core/Timer_test.cpp's runLoopUntil(); NOWAIT plus a short
//sleep rather than ONCE so that a loop left with no active handle spins
//cheaply until the deadline instead of burning a core.
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
 * ⚠️ WNOHANG AND A WALL CLOCK BUDGET, never a blocking waitpid(): the recorder
 * exits in milliseconds, but a child spawned in the last instant of a case may
 * not have exited yet, and a teardown that BLOCKS on a stuck child would hang
 * `make check` - the one thing this harness must never do. A child still alive
 * when the budget runs out is left to init, exactly as before this function
 * existed.
 *
 * Answers how many were reaped, so a case can assert on it.
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

        //p == 0: children exist but none has exited yet
        if (std::chrono::steady_clock::now() > deadline)
            break;

        ::usleep(2000);
    }

    return reaped;
}

/*
 * Unlink the ExternProcServer sockets THIS PROCESS bound.
 *
 * ~ExternProcServer does it (IO/ExternProc.cpp:114-115) and never runs here,
 * because main() leaves by _exit(). The name carries our pid
 * (/tmp/calaos_proc_<uuid>_<name>_<pid>, IO/ExternProc.cpp:33-36), so matching
 * on the suffix cannot touch a socket belonging to another test binary running
 * in the same `make check -jN`.
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

/*
 * Everything the harness introduced, given back. Call from main(), BEFORE
 * _exit(), and never from a TearDown(): the sandbox outlives the cases on
 * purpose (see the header comment).
 */
inline void teardown()
{
    reapChildren(1000);

    const Sandbox &sb = sandbox();
    if (sb.dir.empty())
        return;

    ::unlink(sb.journal.c_str());
    ::unlink(sb.script.c_str());
    ::rmdir(sb.dir.c_str());

    removeOwnSockets();
}

} //namespace RoonSpawn
} //namespace CalaosTest

#endif //CALAOS_TEST_ROON_SPAWN_HARNESS_H
