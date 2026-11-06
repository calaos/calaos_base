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

/*******************************************************************************
 * WHAT AN HTTP DOWNLOAD DOES WHEN THE LOOP REFUSES TO WATCH ITS SOCKET.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT
 * ---------------------------------------------------------------------------
 * uvw::Loop::resource<R>() runs `ptr = ptr->init() ? ptr : nullptr`: it hands
 * back NOTHING when the underlying uv_*_init refuses. The tree calls it in 45
 * places outside uvw/ and none looks at the result. Forty-four of them cannot
 * be hit - measured, see the ticket - because their initializer opens no
 * descriptor, or (ProcessHandle) does not initialize anything at all. The
 * forty-fifth is the socket callback of the libcurl-multi transport, and
 * uv_poll_init_socket() DOES refuse: EEXIST on a descriptor the loop already
 * watches, EPERM on a descriptor that is not pollable, EBADF on one that is
 * already gone. The handle was dereferenced four lines below, so any HTTP
 * download - camera snapshot, outgoing notification, MCP sidecar - could take
 * the whole server down with it.
 *
 * ---------------------------------------------------------------------------
 * HOW THE REFUSAL IS PRODUCED, AND WHY IT IS NOT A MOCK
 * ---------------------------------------------------------------------------
 * Nothing here calls the socket callback, and nothing here fakes libcurl or
 * uvw. A real HTTP peer listens on a loopback port, a real UrlDownloader runs
 * a real GET through libcurl-multi driven by the real libuv loop. The single
 * thing replaced is the C entry point uv_poll_init_socket(), interposed by
 * this executable: libuv is a shared object, UrlDownloader.o is not, so the
 * definition below is the one the shipped code calls, and it forwards to
 * libuv verbatim while disarmed. Arming it makes the shipped resource<>()
 * return null exactly the way an EEXIST would.
 *
 * ---------------------------------------------------------------------------
 * WHY EACH CASE RUNS IN A FORKED CHILD
 * ---------------------------------------------------------------------------
 * The unguarded form does not fail an assertion, it kills the process. A
 * child makes that observable AS a verdict (the parent reads the signal that
 * killed it) instead of taking the rest of the suite down. What the child
 * sends back is written to a file the parent created, never to a pipe it
 * waits on afterwards.
 *
 * ---------------------------------------------------------------------------
 * WHAT MAKES A GREEN MEAN SOMETHING
 * ---------------------------------------------------------------------------
 * A case that only checked "the download fails cleanly" would be green on
 * code that never reaches the faulty line at all. So every case carries its
 * own proof of traversal:
 *  - the interposer counts the calls it served and the refusals it returned,
 *    and a case that refused nothing is red;
 *  - the refusal line of the journal has to name the descriptor LIBCURL
 *    chose, a number this file never picks and cannot know in advance, on the
 *    same line as its domain;
 *  - the disarmed twin of each case completes with the body the peer really
 *    sent, which is what says the fixture goes through uv_poll_init_socket()
 *    at all.
 *
 * WHAT THIS DOES NOT PROVE: no descriptor is really exhausted here, no camera
 * and no real install are involved, and the forty-four other resource<>()
 * sites are argued in the ticket, not measured by this file.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <uv.h>

#include <dlfcn.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ConfigStore.h"
#include "LogSetup.h"
#include "Logger.h"
#include "UrlDownloader.h"
#include "libuvw.h"

using namespace std::chrono;

namespace
{

/*
 * THE INTERPOSER'S STATE. Every case runs in its own child, so these never
 * cross a case boundary.
 */
std::atomic<bool> gRefusePollInit{false};
std::atomic<int> gPollInitCalls{0};
std::atomic<int> gPollInitRefusals{0};
std::atomic<long> gLastRefusedSocket{-1};

} // namespace

/*
 * The executable's definition wins over libuv.so for every caller linked into
 * it, UrlDownloader.o included. Disarmed it is libuv's own function, byte for
 * byte: the fixture has to behave exactly like production when nothing is
 * being injected, or the armed run measures something else.
 */
extern "C" int uv_poll_init_socket(uv_loop_t *loop, uv_poll_t *handle, uv_os_sock_t socket)
{
    static int (*real)(uv_loop_t *, uv_poll_t *, uv_os_sock_t) = nullptr;
    if (!real)
    {
        void *sym = dlsym(RTLD_NEXT, "uv_poll_init_socket");
        if (!sym)
        {
            void *lib = dlopen("libuv.so.1", RTLD_LAZY);
            if (lib)
                sym = dlsym(lib, "uv_poll_init_socket");
        }
        real = reinterpret_cast<int (*)(uv_loop_t *, uv_poll_t *, uv_os_sock_t)>(sym);
    }

    gPollInitCalls++;

    if (gRefusePollInit.load())
    {
        gPollInitRefusals++;
        gLastRefusedSocket.store(static_cast<long>(socket));
        return UV_EEXIST;
    }

    if (!real)
        return UV_ENOSYS;
    return real(loop, handle, socket);
}

namespace
{

//A body no other string of this binary produces by accident, and the second
//peer answers something else so a case cannot confuse the two transfers.
const char *const kFirstBody = "{\"porte\":\"vestibule-quatorze\",\"etat\":\"ouverte\"}";
const char *const kSecondBody = "{\"porte\":\"cellier-trente-et-un\",\"etat\":\"fermee\"}";

bool haveCurl()
{
    const char *path = getenv("PATH");
    if (!path)
        return false;

    std::string p(path), dir;
    size_t start = 0;
    while (start <= p.size())
    {
        size_t end = p.find(':', start);
        if (end == std::string::npos)
            end = p.size();
        dir = p.substr(start, end - start);
        if (!dir.empty() && access((dir + "/curl").c_str(), X_OK) == 0)
            return true;
        start = end + 1;
    }
    return false;
}

#define REQUIRE_CURL() do { if (!haveCurl()) GTEST_SKIP() << "no curl binary in PATH"; } while (0)

//One-shot HTTP peer on an ephemeral loopback port: reads the request, answers
//a complete fixed response, closes. Same pattern as UrlDownloader_test.
class HttpPeer
{
public:
    explicit HttpPeer(std::string responseBody): body(std::move(responseBody))
    {
        listenFd = socket(AF_INET, SOCK_STREAM, 0);
        int on = 1;
        setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(listenFd, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
            listen(listenFd, 1) == 0)
        {
            socklen_t len = sizeof(addr);
            if (getsockname(listenFd, (struct sockaddr *)&addr, &len) == 0)
                port = ntohs(addr.sin_port);
        }

        th = std::thread([this]() { run(); });
    }

    ~HttpPeer()
    {
        stopRequested = true;
        th.join();
        if (listenFd >= 0)
            close(listenFd);
    }

    std::string url() const
    {
        return "http://127.0.0.1:" + std::to_string(port) + "/etat";
    }

    int port = 0;

private:
    void run()
    {
        int fd = -1;
        while (!stopRequested)
        {
            struct pollfd p = { listenFd, POLLIN, 0 };
            if (poll(&p, 1, 50) > 0 && (p.revents & POLLIN))
            {
                fd = accept(listenFd, nullptr, nullptr);
                break;
            }
        }
        if (fd < 0)
            return;

        for (int i = 0; i < 20; i++)
        {
            struct pollfd p = { fd, POLLIN, 0 };
            if (poll(&p, 1, 10) <= 0 || !(p.revents & POLLIN))
                break;
            char b[1024];
            if (recv(fd, b, sizeof(b), MSG_DONTWAIT) <= 0)
                break;
        }

        const std::string resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Connection: close\r\n\r\n" + body;

        if (send(fd, resp.data(), resp.size(), MSG_NOSIGNAL) < 0)
        {
            close(fd);
            return;
        }
        close(fd);
    }

    std::string body;
    int listenFd = -1;
    std::atomic<bool> stopRequested{false};
    std::thread th;
};

bool runLoopUntil(const std::function<bool()> &pred, int timeoutMs)
{
    auto loop = uvw::Loop::getDefault();
    auto deadline = steady_clock::now() + milliseconds(timeoutMs);
    while (!pred())
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        if (steady_clock::now() > deadline)
            return false;
        std::this_thread::sleep_for(milliseconds(1));
    }
    return true;
}

void drainLoop(int ms = 300)
{
    auto loop = uvw::Loop::getDefault();
    auto deadline = steady_clock::now() + milliseconds(ms);
    while (steady_clock::now() < deadline)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(milliseconds(1));
    }
}

std::string captureStdout(const std::function<void()> &fn)
{
    std::ostringstream sink;
    std::streambuf *saved = std::cout.rdbuf(sink.rdbuf());
    fn();
    std::cout.rdbuf(saved);
    return sink.str();
}

/*
 * WHAT THE CHILD SENDS BACK. Key/value head, then the journal verbatim - the
 * whole journal, not the lines a filter kept: a haystack narrowed before the
 * search answers the question it was narrowed with.
 */
struct Report
{
    bool ran = false;          //the child wrote something at all
    bool exitedNormally = false;
    int termSignal = 0;
    int exitCode = -1;

    int pollInitCalls = 0;
    int pollInitRefusals = 0;
    long refusedSocket = -1;

    bool firstCompleted = false;
    int firstStatus = -1;
    std::string firstBody;

    bool secondCompleted = false;
    int secondStatus = -1;
    std::string secondBody;

    int firstPort = 0;
    int secondPort = 0;

    std::string log;
};

long readLong(const std::string &head, const std::string &key, long fallback)
{
    const std::string needle = "\n" + key + "=";
    const std::string padded = "\n" + head;
    size_t at = padded.find(needle);
    if (at == std::string::npos)
        return fallback;
    at += needle.size();
    size_t end = padded.find('\n', at);
    return strtol(padded.substr(at, end - at).c_str(), nullptr, 10);
}

std::string readStr(const std::string &head, const std::string &key)
{
    const std::string needle = "\n" + key + "=";
    const std::string padded = "\n" + head;
    size_t at = padded.find(needle);
    if (at == std::string::npos)
        return {};
    at += needle.size();
    size_t end = padded.find('\n', at);
    return padded.substr(at, end - at);
}

//One exchange, run to completion or to the bound. Returns what the consumer saw.
struct Exchange
{
    bool completed = false;
    int status = -1;
    std::string body;
};

Exchange runOneTransfer(const std::string &url)
{
    Exchange ex;
    UrlDownloader dl(url, false);
    dl.m_signalCompleteData.connect([&](const std::string &d, int s)
    {
        ex.body = d;
        ex.status = s;
        ex.completed = true;
    });

    if (!dl.httpGet())
        return ex;

    runLoopUntil([&]() { return ex.completed; }, 15000);
    drainLoop();
    return ex;
}

/*
 * THE CHILD. `arm` says whether uv_poll_init_socket refuses during the first
 * transfer; `secondTransfer` says whether a second, never-refused transfer
 * follows it on the same (singleton) libcurl-multi handle.
 */
Report runInChild(bool arm, bool secondTransfer)
{
    Report rep;

    char tmpl[] = "/tmp/calaos_pollguard_XXXXXX";
    const int outFd = ::mkstemp(tmpl);
    if (outFd < 0)
        return rep;
    const std::string outPath = tmpl;
    ::close(outFd);

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        ::unlink(outPath.c_str());
        return rep;
    }

    if (pid == 0)
    {
        Exchange first, second;
        int firstPort = 0, secondPort = 0;

        const std::string log = captureStdout([&]()
        {
            HttpPeer peer(kFirstBody);
            firstPort = peer.port;
            if (peer.port <= 0)
                return;

            gRefusePollInit.store(arm);
            first = runOneTransfer(peer.url());
            gRefusePollInit.store(false);

            if (secondTransfer)
            {
                HttpPeer peer2(kSecondBody);
                secondPort = peer2.port;
                if (peer2.port > 0)
                    second = runOneTransfer(peer2.url());
            }
        });

        std::ofstream out(outPath, std::ios::out | std::ios::trunc | std::ios::binary);
        out << "calls=" << gPollInitCalls.load() << "\n"
            << "refusals=" << gPollInitRefusals.load() << "\n"
            << "refusedSocket=" << gLastRefusedSocket.load() << "\n"
            << "firstCompleted=" << (first.completed ? 1 : 0) << "\n"
            << "firstStatus=" << first.status << "\n"
            << "firstBody=" << first.body << "\n"
            << "secondCompleted=" << (second.completed ? 1 : 0) << "\n"
            << "secondStatus=" << second.status << "\n"
            << "secondBody=" << second.body << "\n"
            << "firstPort=" << firstPort << "\n"
            << "secondPort=" << secondPort << "\n"
            << "---LOG---\n" << log;
        out.flush();
        out.close();
        _exit(0);
    }

    int status = 0;
    ::waitpid(pid, &status, 0);
    rep.exitedNormally = WIFEXITED(status);
    rep.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    rep.termSignal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;

    std::ifstream in(outPath, std::ios::in | std::ios::binary);
    std::ostringstream all;
    all << in.rdbuf();
    in.close();
    ::unlink(outPath.c_str());

    const std::string text = all.str();
    const std::string sep = "---LOG---\n";
    const size_t at = text.find(sep);
    if (at == std::string::npos)
        return rep;

    rep.ran = true;
    const std::string head = text.substr(0, at);
    rep.log = text.substr(at + sep.size());

    rep.pollInitCalls = static_cast<int>(readLong(head, "calls", 0));
    rep.pollInitRefusals = static_cast<int>(readLong(head, "refusals", 0));
    rep.refusedSocket = readLong(head, "refusedSocket", -1);
    rep.firstCompleted = readLong(head, "firstCompleted", 0) == 1;
    rep.firstStatus = static_cast<int>(readLong(head, "firstStatus", -1));
    rep.firstBody = readStr(head, "firstBody");
    rep.secondCompleted = readLong(head, "secondCompleted", 0) == 1;
    rep.secondStatus = static_cast<int>(readLong(head, "secondStatus", -1));
    rep.secondBody = readStr(head, "secondBody");
    rep.firstPort = static_cast<int>(readLong(head, "firstPort", 0));
    rep.secondPort = static_cast<int>(readLong(head, "secondPort", 0));
    return rep;
}

//Whether SOME single line carries all the needles. The domain appears on
//every line of the journal, so looking for each needle anywhere in the whole
//text would be satisfied by two unrelated lines.
bool someLineHasAll(const std::string &log, const std::vector<std::string> &needles)
{
    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line))
    {
        bool all = true;
        for (const std::string &n: needles)
        {
            if (line.find(n) == std::string::npos)
            {
                all = false;
                break;
            }
        }
        if (all)
            return true;
    }
    return false;
}

} // namespace

/*
 * THE DEFECT ITSELF: the process must come back alive from a socket the loop
 * would not watch.
 */
TEST(UrlDownloaderPollGuard, AnHttpTransferSurvivesALoopThatRefusesToWatchItsSocket)
{
    REQUIRE_CURL();

    const Report rep = runInChild(true, false);

    ASSERT_EQ(0, rep.termSignal)
        << "the download took the process down with signal " << rep.termSignal
        << " (" << strsignal(rep.termSignal) << "): resource<PollHandle>() handed back "
           "nothing and the handle was used anyway";
    ASSERT_TRUE(rep.ran) << "the child survived but wrote no report, so nothing below "
                            "measures anything";
    EXPECT_TRUE(rep.exitedNormally);
    EXPECT_EQ(0, rep.exitCode);
}

/*
 * PROOF OF TRAVERSAL, ONE. A refusal that nothing served is a sensor that
 * measures nothing: the case above would be green on code that never reaches
 * the socket callback.
 */
TEST(UrlDownloaderPollGuard, TheRefusalIsReallyServedOnTheTransferPath)
{
    REQUIRE_CURL();

    const Report rep = runInChild(true, false);

    ASSERT_TRUE(rep.ran);
    EXPECT_GT(rep.pollInitCalls, 0)
        << "uv_poll_init_socket was never called: the transfer never reached the "
           "socket callback and the case above proves nothing";
    EXPECT_GT(rep.pollInitRefusals, 0)
        << "the interposer served calls but refused none";
    EXPECT_GE(rep.refusedSocket, 0)
        << "no descriptor was recorded, so the refusal did not come from a real socket";
}

/*
 * PROOF OF TRAVERSAL, TWO. The journal has to name the descriptor LIBCURL
 * chose - a number this file never picks - on the line that says the refusal.
 * A message asserted by its own wording alone would be a name known in
 * advance, satisfied by any line the guard might print for any reason.
 */
TEST(UrlDownloaderPollGuard, TheRefusalNamesTheDescriptorLibcurlHandedOver)
{
    REQUIRE_CURL();

    const Report rep = runInChild(true, false);

    ASSERT_TRUE(rep.ran);
    ASSERT_GE(rep.refusedSocket, 0);
    ASSERT_FALSE(rep.log.empty()) << "nothing was captured, so no line can be looked for";

    /* [ERR] urlutils (UrlDownloader.cpp:N) is a line class only the guard
     * produces here, and the descriptor is read between its delimiters: a
     * bare number matches the source line of the journal entry itself, the
     * port of the peer, or a byte count. */
    EXPECT_TRUE(someLineHasAll(rep.log, { "[ERR]", "urlutils", "(UrlDownloader.cpp:",
                                          "(fd=" + std::to_string(rep.refusedSocket) + ")" }))
        << "no error line of the transport names the descriptor " << rep.refusedSocket
        << " the loop refused";
}

/*
 * THE DISARMED TWIN. Same peer, same object, same call: the only thing that
 * changed is whether the interposer refuses. It is what says the fixture goes
 * through uv_poll_init_socket() at all - and that the captured journal is the
 * real one, since it carries the port the peer was given at run time.
 */
TEST(UrlDownloaderPollGuard, TheSameTransferCompletesWhenTheLoopAcceptsTheSocket)
{
    REQUIRE_CURL();

    const Report rep = runInChild(false, false);

    ASSERT_TRUE(rep.ran);
    ASSERT_EQ(0, rep.termSignal);
    EXPECT_GT(rep.pollInitCalls, 0)
        << "the accepted transfer never went through uv_poll_init_socket, so arming it "
           "injects the failure somewhere the transfer does not pass";
    EXPECT_EQ(0, rep.pollInitRefusals);
    EXPECT_TRUE(rep.firstCompleted);
    EXPECT_EQ(200, rep.firstStatus);
    EXPECT_EQ(std::string(kFirstBody), rep.firstBody);
    ASSERT_GT(rep.firstPort, 0);
    EXPECT_TRUE(someLineHasAll(rep.log, { "urlutils", std::to_string(rep.firstPort) }))
        << "the captured journal never mentions the port the peer really listened on";
    EXPECT_FALSE(someLineHasAll(rep.log, { "[ERR]", "urlutils", "(UrlDownloader.cpp:" }))
        << "the transport already writes that error line when nothing is refused, so "
           "finding it in the refused run says nothing";
}

/*
 * THE REFUSED TRANSFER IS ENDED, NOT LEFT HANGING. No event will ever reach a
 * socket the loop does not watch, so the caller has to be told - and with a
 * status that is not the 200 its disarmed twin gets.
 */
TEST(UrlDownloaderPollGuard, TheRefusedTransferIsReportedBackToItsCaller)
{
    REQUIRE_CURL();

    const Report rep = runInChild(true, false);

    ASSERT_TRUE(rep.ran);
    ASSERT_GT(rep.pollInitRefusals, 0);
    EXPECT_TRUE(rep.firstCompleted)
        << "the caller was never told: the transfer hangs for ever on a socket nobody watches";
    EXPECT_NE(200, rep.firstStatus);
    EXPECT_NE(std::string(kFirstBody), rep.firstBody);
}

/*
 * ONE REFUSAL IS NOT A DEAD TRANSPORT. Telling libcurl about the failure by
 * returning non-zero from the socket callback sets multi->dead, and every
 * later download of the process fails with it - a server that survives the
 * crash but downloads nothing again. The second peer answers a different body
 * so a green cannot be the first answer arriving twice.
 */
TEST(UrlDownloaderPollGuard, ARefusedSocketDoesNotKillTheDownloadsThatFollowIt)
{
    REQUIRE_CURL();

    const Report rep = runInChild(true, true);

    ASSERT_TRUE(rep.ran);
    ASSERT_EQ(0, rep.termSignal);
    ASSERT_GT(rep.pollInitRefusals, 0)
        << "nothing was refused, so this case never saw the transport in the state it is about";
    ASSERT_GT(rep.secondPort, 0);
    EXPECT_TRUE(rep.secondCompleted)
        << "the download that followed the refused one never completed";
    EXPECT_EQ(200, rep.secondStatus);
    EXPECT_EQ(std::string(kSecondBody), rep.secondBody);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_pollguard_cfg_XXXXXX";
    const char *base = ::mkdtemp(tmpl);
    if (base)
    {
        const std::string cfg = std::string(base) + "/config";
        const std::string cache = std::string(base) + "/cache";
        ::mkdir(cfg.c_str(), 0700);
        ::mkdir(cache.c_str(), 0700);

        Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                                 const_cast<char *>(cache.c_str()), true);
        Utils::set_config_option("debug_level", "5");
    }

    return RUN_ALL_TESTS();
}
