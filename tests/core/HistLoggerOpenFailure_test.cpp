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

//The history database is opened by HistLogger's worker thread. When sqlite
//cannot open it (cache directory gone or read only, full disk), the server
//must carry on without history, and the readers waiting on the worker must
//still get an answer. HistLogger is a process singleton, so each case runs in
//a forked child; an exception escaping the worker thread aborts that child.

#include <gtest/gtest.h>

#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "HistLogger.h"
#include "NotifManager.h"
#include "libuvw.h"

using namespace Calaos;

namespace
{

enum class CacheState
{
    Missing,
    ReadOnly
};

struct Answer
{
    bool received = false;
    bool success = true;
    string error;
};

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

std::shared_ptr<Answer> readEvents()
{
    auto answer = std::make_shared<Answer>();
    HistLogger::Instance().getEvents(0, 10,
                                     [answer](bool success, string errorMsg, const vector<HistEvent> &, int, int)
                                     {
        answer->received = true;
        answer->success = success;
        answer->error = errorMsg;
    });
    pumpUntil([answer]()
              {
        return answer->received;
    });
    return answer;
}

//Writes one line per answer: '1' when received, then '1' when refused.
[[noreturn]] void runChild(int fd, const std::string &base, CacheState state)
{
    const std::string cfg = base + "/config";
    const std::string cache = base + "/cache";
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(cache.c_str(), 0700);
    Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                             const_cast<char *>(cache.c_str()), true);

    //initConfigOptions() refuses a cache path it cannot write to, so the
    //directory is taken away only once the path is set.
    if (state == CacheState::Missing)
        std::filesystem::remove_all(cache);
    else
        ::chmod(cache.c_str(), 0500);

    HistEvent ev = HistEvent::create();
    ev.io_id = "fb5_dropped";
    HistLogger::Instance().appendEvent(ev);
    HistLogger::Instance().registerPushToken("fb5_token", HistLogger::PUSH_HW_ANDROID);

    std::string report;
    for (int i = 0; i < 2; i++)
    {
        auto answer = readEvents();
        report += answer->received ? '1' : '0';
        report += (!answer->success && !answer->error.empty()) ? '1' : '0';
    }

    auto tokens = std::make_shared<Answer>();
    HistLogger::Instance().getPushTokens(
        [tokens](bool success, string errorMsg, const vector<PushToken> &)
        {
        tokens->received = true;
        tokens->success = success;
        tokens->error = errorMsg;
    });
    pumpUntil([tokens]()
              {
        return tokens->received;
    });
    report += tokens->received ? '1' : '0';
    report += !tokens->success ? '1' : '0';

    auto single = std::make_shared<Answer>();
    HistLogger::Instance().getEvent(ev.uuid,
                                    [single](bool success, string errorMsg, const HistEvent &)
                                    {
        single->received = true;
        single->success = success;
        single->error = errorMsg;
    });
    pumpUntil([single]()
              {
        return single->received;
    });
    report += single->received ? '1' : '0';
    report += !single->success ? '1' : '0';

    //A push with no readable token list is dropped: nothing is posted.
    auto pushed = std::make_shared<bool>(false);
    NotifManager::Instance().sendPushNotification("fb5", "", [pushed]() { *pushed = true; });
    auto after = std::make_shared<Answer>();
    HistLogger::Instance().getPushTokens(
        [after](bool, string, const vector<PushToken> &)
        {
        after->received = true;
    });
    pumpUntil([after]()
              {
        return after->received;
    });
    for (int i = 0; i < 10; i++)
        uvw::Loop::getDefault()->run<uvw::Loop::Mode::NOWAIT>();
    report += after->received ? '1' : '0';
    report += !*pushed ? '1' : '0';

    if (::write(fd, report.data(), report.size()) != (ssize_t)report.size())
        _exit(3);

    //exit(), not _exit(): ~HistLogger() joins the worker.
    std::exit(0);
}

struct ChildResult
{
    bool exited = false;
    int code = -1;
    int signal = 0;
    std::string report;
};

ChildResult runInChild(CacheState state)
{
    ChildResult r;

    char tmpl[] = "/tmp/calaos_fb5_hist_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (!base)
        return r;

    int fds[2];
    if (::pipe(fds) != 0)
        return r;

    //Otherwise the child flushes gtest's buffered output a second time.
    std::fflush(nullptr);
    const pid_t pid = ::fork();
    if (pid == 0)
    {
        ::close(fds[0]);
        runChild(fds[1], base, state);
    }
    ::close(fds[1]);

    for (;;)
    {
        struct pollfd pfd = { fds[0], POLLIN, 0 };
        if (::poll(&pfd, 1, 30000) <= 0)
        {
            ::kill(pid, SIGKILL);
            break;
        }
        char buf[16];
        const ssize_t n = ::read(fds[0], buf, sizeof(buf));
        if (n <= 0)
            break;
        r.report.append(buf, n);
    }
    ::close(fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    r.exited = WIFEXITED(status);
    r.code = r.exited ? WEXITSTATUS(status) : -1;
    r.signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;

    const std::string cache = std::string(base) + "/cache";
    ::chmod(cache.c_str(), 0700);
    std::error_code ec;
    std::filesystem::remove_all(base, ec);

    return r;
}

} //namespace

TEST(HistLoggerOpenFailureTest, AMissingCacheDirectoryDisablesHistoryAndTheServerGoesOn)
{
    const ChildResult r = runInChild(CacheState::Missing);

    ASSERT_TRUE(r.exited) << "the child died on signal " << r.signal;
    EXPECT_EQ(r.code, 0);
    //Two event reads, the push tokens and a single event: each answered, each
    //refused. Then a push: dropped.
    EXPECT_EQ(r.report, "1111111111");
}

TEST(HistLoggerOpenFailureTest, AReadOnlyCacheDirectoryDisablesHistoryAndTheServerGoesOn)
{
    if (::geteuid() == 0)
        GTEST_SKIP() << "root writes into a read only directory";

    const ChildResult r = runInChild(CacheState::ReadOnly);

    ASSERT_TRUE(r.exited) << "the child died on signal " << r.signal;
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.report, "1111111111");
}
