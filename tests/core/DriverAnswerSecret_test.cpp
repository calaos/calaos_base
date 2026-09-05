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
 * WHAT IS WRITTEN ABOUT AN ANSWER NOBODY COULD READ.
 *
 * ---------------------------------------------------------------------------
 * THE TWO DEFECTS
 * ---------------------------------------------------------------------------
 * 1. UrlDownloader::getResponseHeaders() printed every line of the response
 *    header block. That block is written by the other end: a session cookie, a
 *    challenge with its nonce, an authorization echoed back - and a Location,
 *    which IS a url and went out raw while the url reducer already existed.
 *    The method is public, so a driver reading Content-Type off it republished
 *    the whole block a second time.
 *
 * 2. SynoSurveillanceStation::parseJsonResult() printed the whole answer, at
 *    WARNING, on its refusal path - and login() reads its session id out of
 *    that very answer. WARNING sits under the level a box ships with, so this
 *    one needed nobody to switch anything on.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS GOES THROUGH THE REAL PATH
 * ---------------------------------------------------------------------------
 * Nothing here calls parseJsonResult() or getResponseHeaders() by hand. A real
 * peer on loopback answers the two real requests downloadSnapshot() makes -
 * the api probe, then the login - and the case first asserts that the login
 * request really carried the account and the password, so that a green can
 * never mean "the driver stopped before the line under test".
 *
 * ---------------------------------------------------------------------------
 * WHAT THE MEASURE IS
 * ---------------------------------------------------------------------------
 * Not the presence of a value chosen in advance: a line publishing the head or
 * the tail of the same answer carries the answer and cites no needle. Each
 * case bounds the longest RUN of bytes of the answer that the journal gives
 * back, hunts that run in three shapes (as it is, percent encoded, base64),
 * and the ceiling is re-derived from this tree at every run by the calibration
 * case at the end of the file.
 *
 * WHAT THIS DOES NOT PROVE: no Synology nas, no Hue bridge and no journal of a
 * real install are involved; the five Hue sites and the Web IO request body
 * are closed by reading, not by a case here.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "LogSetup.h"
#include "Logger.h"
#include "Params.h"
#include "StringUtils.h"
#include "SynoSurveillanceStation.h"
#include "UrlDownloader.h"
#include "libuvw.h"

using namespace std::chrono;
using namespace Calaos;

namespace
{

/*
 * FIXTURE, NOT DECORATION.
 *
 * Three needles no other string of this binary can produce by accident, one
 * per site, kept apart from one another so that a leak at one site cannot
 * raise the bound of its neighbours - the calibration case at the end holds
 * them to that. Each sits inside a document shaped like the one the real end
 * sends, so that a mutation quoting "a bit of context" is quoting a secret.
 */
const char *const kSynoSid = "Jv8mQ2xR7pLd4NsT6bWk";
const char *const kCookieSecret = "Zc3fH9yGq5AeUi1oPr0M";
const char *const kRedirectToken = "Sw6tXn4KvB2jDh8LzC5F";

const char *const kSynoUser = "surveillant";
const char *const kSynoPassword = "motdepasse-camera-4417";

//The answer of SYNO.API.Info, which the driver reads without trouble: the step
//BEFORE the one under test, here only to reach the login.
const char *const kApiInfoBody =
    "{\"success\":true,\"data\":{\"SYNO.API.Auth\":"
    "{\"path\":\"auth.cgi\",\"minVersion\":1,\"maxVersion\":6}}}";

/*
 * The login answer. The session id is opened with the camera credentials and
 * travels in the document; the driver refuses this one because the nas reports
 * a failure alongside it, which is the whole point - a driver publishes an
 * answer precisely when it could not read it.
 */
const char *const kLoginBody =
    "{\"error\":{\"code\":403},\"data\":{\"sid\":\"Jv8mQ2xR7pLd4NsT6bWk\"},"
    "\"success\":false}";

//The value of a Set-Cookie the header block used to print verbatim.
const char *const kCookieValue = "id_session=Zc3fH9yGq5AeUi1oPr0M; Path=/; HttpOnly";

//The bearing part of a redirect target: the authority is what may be
//published, everything after it is what must never come back.
const char *const kRedirectTail = "/relocated?ticket=Sw6tXn4KvB2jDh8LzC5F";

//Substituted by the peer with its own authority at send time: the port is
//only known once the socket is bound, and the redirect has to point at it.
const char *const kAuthorityMark = "@@PEER@@";

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

/*
 * HTTP peer on an ephemeral loopback port serving a queue of answers, one per
 * connection, keeping every request it read. One connection per transfer: the
 * transport sets CURLOPT_FORBID_REUSE and every answer closes. ONE peer for
 * the whole exchange on purpose - two peers mean two ports, and a case that
 * tells them apart passes for a reason it did not mean to measure.
 */
class HttpPeer
{
public:
    explicit HttpPeer(std::deque<std::string> answers): pending(std::move(answers))
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
            listen(listenFd, 8) == 0)
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

    std::string authority() const
    {
        return "127.0.0.1:" + std::to_string(port);
    }

    std::vector<std::string> requests() const
    {
        std::lock_guard<std::mutex> lock(mtx);
        return seen;
    }

    int port = 0;

private:
    void run()
    {
        while (!stopRequested)
        {
            struct pollfd p = { listenFd, POLLIN, 0 };
            if (poll(&p, 1, 20) <= 0 || !(p.revents & POLLIN))
                continue;

            const int fd = accept(listenFd, nullptr, nullptr);
            if (fd < 0)
                continue;

            std::string req;
            for (int i = 0; i < 40; i++)
            {
                struct pollfd q = { fd, POLLIN, 0 };
                if (poll(&q, 1, 10) <= 0 || !(q.revents & POLLIN))
                    break;
                char b[2048];
                const ssize_t got = recv(fd, b, sizeof(b), MSG_DONTWAIT);
                if (got <= 0)
                    break;
                req.append(b, static_cast<size_t>(got));
            }

            std::string answer;
            {
                std::lock_guard<std::mutex> lock(mtx);
                seen.push_back(req);
                if (!pending.empty())
                {
                    answer = pending.front();
                    pending.pop_front();
                }
            }

            if (answer.empty())
                answer = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
                         "Connection: close\r\n\r\n";

            const std::string me = authority();
            for (size_t at = answer.find(kAuthorityMark); at != std::string::npos;
                 at = answer.find(kAuthorityMark, at + me.size()))
                answer.replace(at, strlen(kAuthorityMark), me);

            if (send(fd, answer.data(), answer.size(), MSG_NOSIGNAL) < 0)
            {
                close(fd);
                continue;
            }
            close(fd);
        }
    }

    std::deque<std::string> pending;
    std::vector<std::string> seen;
    mutable std::mutex mtx;
    int listenFd = -1;
    std::atomic<bool> stopRequested{false};
    std::thread th;
};

std::string jsonAnswer(const std::string &body)
{
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Connection: close\r\n\r\n" + body;
}

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

void drainLoop(int ms = 200)
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

/*
 * THE LONGEST RUN OF A DOCUMENT THE JOURNAL GIVES BACK.
 *
 * A search for the needle only sees the slice it was spelled for, and a value
 * that reaches the log re-encoded is invisible to a search of the clear: the
 * run is hunted in the three shapes a journal of this tree can carry.
 */
std::string longestEchoRun(const std::string &log, const std::string &text)
{
    std::string best;
    for (size_t i = 0; i < text.size(); i++)
    {
        size_t len = best.size() + 1;
        while (i + len <= text.size() &&
               log.find(text.substr(i, len)) != std::string::npos)
        {
            best = text.substr(i, len);
            len++;
        }
    }
    return best;
}

std::string percentEncoded(const std::string &s)
{
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c: s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xf];
        }
    }
    return out;
}

std::vector<std::string> everyShapeOf(const std::string &text)
{
    std::string copy = text;
    return { text, percentEncoded(text), Utils::Base64_encode(copy) };
}

std::string longestEchoAnyShape(const std::string &log, const std::string &text)
{
    std::string best;
    for (const std::string &shape: everyShapeOf(text))
    {
        const std::string run = longestEchoRun(log, shape);
        if (run.size() > best.size())
            best = run;
    }
    return best;
}

size_t longestEcho(const std::string &log, const std::string &text)
{
    return longestEchoAnyShape(log, text).size();
}

/* One above the overlap this tree really produces, and the suite re-measures
 * that overlap at every run rather than trusting this line: see the
 * calibration case at the end of the file for why a ceiling nobody re-measures
 * drifts in both directions. */
const size_t kMaxEcho = 5;

/*
 * WHAT A BOX PRINTS WITH NOBODY TOUCHING ANYTHING, measured in a child.
 *
 * The severity of the Synology site rests on WARNING being printed with no
 * operator switching anything on, and this process cannot observe it: it
 * raises the level in its own main(), and the Logger fills its domain map once
 * and never re-reads it. The child is forked before that, and before there is
 * an event loop or a transfer to duplicate. The shipped default comes from the
 * fallback of Logger::maxLevelPrintable(), not from the registered default of
 * the debug_level option, which is never consulted until the option is written.
 */
struct DefaultLevelProbe
{
    bool ran = false;
    bool rootWarning = false;
    bool rootError = false;
    bool rootDebug = false;
    bool hueError = false;
    bool urlutilsDebug = false;
    bool hueDebug = false;
};

DefaultLevelProbe &defaultLevelProbe()
{
    static DefaultLevelProbe probe;
    return probe;
}

void measureStockLogLevel()
{
    int fds[2];
    if (::pipe(fds) != 0)
        return;

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close(fds[0]);
        ::close(fds[1]);
        return;
    }

    if (pid == 0)
    {
        ::close(fds[0]);

        unsigned char answer = 0;
        char tmpl[] = "/tmp/calaos_driveranswer_stock_XXXXXX";
        const char *base = ::mkdtemp(tmpl);
        if (base)
        {
            const std::string cfg = std::string(base) + "/config";
            const std::string cache = std::string(base) + "/cache";
            ::mkdir(cfg.c_str(), 0700);
            ::mkdir(cache.c_str(), 0700);

            //Nothing is set afterwards: this is a stock install.
            Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                                     const_cast<char *>(cache.c_str()), true);

            answer = 0x10;
            if (Utils::calaosLogger()->isLevelEnabled(Logger::LOG_LEVEL_WARNING))
                answer |= 0x1;
            if (Utils::calaosLogger()->isLevelEnabled(Logger::LOG_LEVEL_ERROR))
                answer |= 0x2;
            if (Utils::calaosLogger()->isLevelEnabled(Logger::LOG_LEVEL_DEBUG))
                answer |= 0x4;
            if (Utils::calaosLogger("hue")->isLevelEnabled(Logger::LOG_LEVEL_ERROR))
                answer |= 0x8;
            if (Utils::calaosLogger("urlutils")->isLevelEnabled(Logger::LOG_LEVEL_DEBUG))
                answer |= 0x20;
            if (Utils::calaosLogger("hue")->isLevelEnabled(Logger::LOG_LEVEL_DEBUG))
                answer |= 0x40;
        }

        if (::write(fds[1], &answer, 1) != 1)
            answer = 0;
        ::close(fds[1]);
        _exit(0);
    }

    ::close(fds[1]);

    unsigned char answer = 0;
    const ssize_t got = ::read(fds[0], &answer, 1);
    ::close(fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    if (got == 1 && (answer & 0x10))
    {
        DefaultLevelProbe &probe = defaultLevelProbe();
        probe.ran = true;
        probe.rootWarning = (answer & 0x1) != 0;
        probe.rootError = (answer & 0x2) != 0;
        probe.rootDebug = (answer & 0x4) != 0;
        probe.hueError = (answer & 0x8) != 0;
        probe.urlutilsDebug = (answer & 0x20) != 0;
        probe.hueDebug = (answer & 0x40) != 0;
    }
}

/*
 * ONE RUN OF THE REAL SYNOLOGY PATH, shared by the cases that read it.
 *
 * downloadSnapshot() probes the api, then logs in: two requests, two answers
 * chosen here. What is kept is what the shipped code wrote to std::cout on the
 * way, plus what the peer really received.
 */
struct SynoRun
{
    bool ran = false;
    bool called = false;
    std::string payload;
    std::string log;
    std::vector<std::string> requests;
};

SynoRun &synoRun()
{
    static SynoRun run;
    static bool done = false;
    if (done)
        return run;
    done = true;

    HttpPeer peer({ jsonAnswer(kApiInfoBody), jsonAnswer(kLoginBody) });
    if (peer.port <= 0)
        return run;

    const std::string base = "http://" + peer.authority();

    run.log = captureStdout([&]()
    {
        Params p;
        p.Add("id", "io_syno_probe");
        p.Add("name", "entree");
        p.Add("type", "SynoSurveillanceStation");
        p.Add("url", base);
        p.Add("username", kSynoUser);
        p.Add("password", kSynoPassword);
        p.Add("camera_id", "3");

        SynoSurveillanceStation cam(p);
        cam.downloadSnapshot([&](const std::string &data)
        {
            run.payload = data;
            run.called = true;
        });

        runLoopUntil([&]() { return run.called; }, 15000);
        drainLoop();
    });

    run.requests = peer.requests();
    run.ran = true;
    return run;
}

/*
 * ONE TRANSFER WHOSE HEADER BLOCK CARRIES A SESSION COOKIE AND A REDIRECT.
 *
 * libcurl follows the Location, so the accumulated block holds both header
 * sets and getResponseHeaders() walks them all. The redirect points back at
 * the same peer: two connections, one authority, so nothing here can pass by
 * telling two ports apart.
 */
struct HeaderRun
{
    bool ran = false;
    bool completed = false;
    int status = -1;
    std::string log;
    std::string authority;
    std::vector<std::string> requests;
};

HeaderRun &headerRun()
{
    static HeaderRun run;
    static bool done = false;
    if (done)
        return run;
    done = true;

    const std::string redirect =
        std::string("HTTP/1.1 302 Found\r\n"
                    "Set-Cookie: ") + kCookieValue + "\r\n"
        "Location: http://" + kAuthorityMark + kRedirectTail + "\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";

    const std::string image =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: 4\r\n"
        "Connection: close\r\n\r\n"
        "\xff\xd8\xff\xd9";

    HttpPeer peer({ redirect, image });
    if (peer.port <= 0)
        return run;

    run.authority = peer.authority();

    run.log = captureStdout([&]()
    {
        UrlDownloader dl("http://" + peer.authority() + "/snapshot", false);
        dl.m_signalCompleteData.connect([&](const std::string &, int s)
        {
            run.status = s;
            run.completed = true;
        });

        if (!dl.httpGet())
            return;

        runLoopUntil([&]() { return run.completed; }, 15000);
        drainLoop();
    });

    run.requests = peer.requests();
    run.ran = true;
    return run;
}

//Every bounded probe of this suite, so that the ceiling and the fixture are
//held to the same measurement.
struct Probe
{
    const char *label;
    std::string document;
    const std::string *log;
};

std::vector<Probe> probes()
{
    return {
        { "syno login answer", kLoginBody, &synoRun().log },
        { "response header Set-Cookie", kCookieValue, &headerRun().log },
        { "response header Location", kRedirectTail, &headerRun().log },
    };
}

} // namespace

/*
 * THE LEAK ITSELF: THE SESSION ID OF THE LOGIN ANSWER.
 *
 * The driver refuses the answer and says so at WARNING, a level a stock box
 * prints. What it refused is a login answer, so what it published is the
 * session opened with the camera credentials.
 */
TEST(DriverAnswerSecret, TheSynologyLoginAnswerNeverReachesTheLog)
{
    REQUIRE_CURL();

    const SynoRun &run = synoRun();
    ASSERT_TRUE(run.ran) << "the peer never came up, this case measures nothing";
    ASSERT_TRUE(run.called) << "the driver never called back, this case measures nothing";

    //Anti-vacuity: the login step was really reached, with the credentials on
    //the wire, so a green below cannot mean "the driver stopped earlier".
    ASSERT_EQ(2u, run.requests.size())
        << "the driver did not make the api probe and the login, so the line "
           "under test was never reached";
    ASSERT_NE(std::string::npos, run.requests[1].find("method=Login"))
        << "the second request is not the login:\n" << run.requests[1];
    ASSERT_NE(std::string::npos, run.requests[1].find("passwd="))
        << "the login request carried no password, so this is not the path "
           "that opens a session:\n" << run.requests[1];

    //Anti-vacuity: the refusal really printed something.
    ASSERT_NE(std::string::npos, run.log.find("Syno"))
        << "the driver printed nothing at all, so finding no secret in this "
           "log proves nothing:\n" << run.log;

    EXPECT_EQ(std::string::npos, run.log.find(kSynoSid))
        << "the session id of the login answer is in the journal:\n" << run.log;
    EXPECT_EQ(std::string::npos, run.log.find(kLoginBody))
        << "the whole login answer is in the journal:\n" << run.log;
    EXPECT_LT(longestEcho(run.log, kLoginBody), kMaxEcho)
        << "the journal gives back " << longestEcho(run.log, kLoginBody)
        << " consecutive bytes of the login answer (\""
        << longestEchoAnyShape(run.log, kLoginBody) << "\"), which is where the "
           "session id lives:\n" << run.log;
}

/*
 * THE COUNTERWEIGHT OF THE SYNOLOGY SITE. Emptying the line would pass the
 * case above and leave an integrator unable to tell a nas that refuses from a
 * nas that never answered.
 */
TEST(DriverAnswerSecret, TheSynologyRefusalStillNamesTheStepAndTheSize)
{
    REQUIRE_CURL();

    const SynoRun &run = synoRun();
    ASSERT_TRUE(run.ran) << "the peer never came up, this case measures nothing";
    ASSERT_TRUE(run.payload.empty())
        << "the driver accepted the answer, so it never took its refusal path";

    EXPECT_TRUE(someLineHasAll(run.log, { "Syno", std::to_string(strlen(kLoginBody)) }))
        << "no line reports the size of the answer the driver refused ("
        << strlen(kLoginBody) << " bytes), so a journal can no longer tell an "
           "empty answer from one it could not read:\n" << run.log;

    EXPECT_TRUE(someLineHasAll(run.log, { "Syno", "login" }))
        << "no line says the login step failed:\n" << run.log;
}

/*
 * THE HEADER BLOCK: A SESSION COOKIE THE OTHER END SET.
 *
 * The name of the header stays - that is the anti-vacuity here, and half the
 * diagnosis - the value does not.
 */
TEST(DriverAnswerSecret, TheResponseHeaderBlockDoesNotGiveBackASessionCookie)
{
    REQUIRE_CURL();

    const HeaderRun &run = headerRun();
    ASSERT_TRUE(run.ran) << "the peer never came up, this case measures nothing";
    ASSERT_TRUE(run.completed) << "the transfer never completed, this case measures nothing";
    ASSERT_EQ(200, run.status);

    //Anti-vacuity: the header line exists in the journal, so a green means
    //withheld and never "the block was not printed".
    ASSERT_TRUE(someLineHasAll(run.log, { "urlutils", "Set-Cookie" }))
        << "no urlutils line mentions the Set-Cookie the peer sent, so finding "
           "no cookie value in this log proves nothing:\n" << run.log;

    EXPECT_EQ(std::string::npos, run.log.find(kCookieSecret))
        << "the session cookie set by the other end is in the journal:\n" << run.log;
    EXPECT_LT(longestEcho(run.log, kCookieValue), kMaxEcho)
        << "the journal gives back " << longestEcho(run.log, kCookieValue)
        << " consecutive bytes of a Set-Cookie value (\""
        << longestEchoAnyShape(run.log, kCookieValue) << "\"):\n" << run.log;
}

/*
 * THE HEADER BLOCK: A REDIRECT TARGET IS A URL.
 *
 * Everything after the authority of a Location can carry a ticket, and it went
 * out raw while the url reducer already existed one function away.
 */
TEST(DriverAnswerSecret, ARedirectTargetIsReducedLikeAnyOtherUrl)
{
    REQUIRE_CURL();

    const HeaderRun &run = headerRun();
    ASSERT_TRUE(run.ran) << "the peer never came up, this case measures nothing";
    ASSERT_TRUE(run.completed) << "the transfer never completed, this case measures nothing";

    //Anti-vacuity: the redirect was really delivered and really followed.
    ASSERT_EQ(2u, run.requests.size())
        << "the transfer did not follow the redirect, so no Location ever "
           "reached the header block";
    ASSERT_NE(std::string::npos, run.requests[1].find("/relocated"))
        << "the second request did not go to the redirect target:\n" << run.requests[1];

    EXPECT_EQ(std::string::npos, run.log.find(kRedirectToken))
        << "the ticket carried by the redirect target is in the journal:\n" << run.log;
    EXPECT_LT(longestEcho(run.log, kRedirectTail), kMaxEcho)
        << "the journal gives back " << longestEcho(run.log, kRedirectTail)
        << " consecutive bytes of the bearing part of a redirect target (\""
        << longestEchoAnyShape(run.log, kRedirectTail) << "\"):\n" << run.log;
}

/*
 * THE COUNTERWEIGHT OF THE HEADER BLOCK. A block reduced to nothing closes the
 * leak and blinds the only channel that says which content type came back, on
 * which status, and where a redirect went.
 */
TEST(DriverAnswerSecret, TheHeaderBlockStillNamesTheStatusTheTypeAndTheRedirectHost)
{
    REQUIRE_CURL();

    const HeaderRun &run = headerRun();
    ASSERT_TRUE(run.ran) << "the peer never came up, this case measures nothing";
    ASSERT_TRUE(run.completed) << "the transfer never completed, this case measures nothing";

    EXPECT_TRUE(someLineHasAll(run.log, { "urlutils", "302" }))
        << "no urlutils line reports the status of the redirect:\n" << run.log;
    EXPECT_TRUE(someLineHasAll(run.log, { "urlutils", "Content-Type", "image/jpeg" }))
        << "no urlutils line reports the content type, which is what T3.83 "
           "left as the counterweight of the body it withheld:\n" << run.log;
    EXPECT_TRUE(someLineHasAll(run.log, { "urlutils", "Location", run.authority }))
        << "no urlutils line says where the redirect pointed, so a redirection "
           "can no longer be followed at all:\n" << run.log;
}

/*
 * THE LEVEL THE SEVERITY RESTS ON, MEASURED INSTEAD OF ASSERTED.
 *
 * "The answer only leaves once an operator turns DEBUG on" is false for the
 * Synology site and true for the header block, and this process cannot observe
 * either: it raises the level in its own main(). A child that never raised it
 * is asked instead.
 */
TEST(DriverAnswerSecret, AStockInstallPrintsTheSynologyRefusalAndNotTheHeaderBlock)
{
    const DefaultLevelProbe &probe = defaultLevelProbe();

    ASSERT_TRUE(probe.ran)
        << "the stock-level child could not be forked or answered nothing, so "
           "this case measures no level at all";

    EXPECT_TRUE(probe.rootWarning)
        << "the default domain does not print at WARNING on a stock install, "
           "so this binary is not measuring the shipped level";
    EXPECT_TRUE(probe.rootError)
        << "the default domain does not print at ERROR on a stock install, "
           "so this binary is not measuring the shipped level";
    EXPECT_FALSE(probe.rootDebug)
        << "the default domain prints at DEBUG on a stock install";
    EXPECT_TRUE(probe.hueError)
        << "the hue domain does not print at ERROR on a stock install, so this "
           "binary is not measuring the shipped level";
    EXPECT_FALSE(probe.hueDebug)
        << "the hue domain prints at DEBUG on a stock install";
    EXPECT_FALSE(probe.urlutilsDebug)
        << "the urlutils domain prints at DEBUG on a stock install: the whole "
           "response header block left with the journal of every box, with no "
           "operator having switched anything on";
}

/*
 * THE CEILING ITSELF, HELD TO WHAT THIS SUITE MEASURES AT EVERY RUN.
 *
 * A bounded sensor is only as narrow as the number above the noise it was cut
 * for, and that number drifts both ways with nobody watching: a fixture that
 * gains a shared word widens the blind window while every assertion stays
 * green, and a ceiling left wider than the run the tree really produces is
 * blind space nobody asked for. Pinning the equality turns both into a red
 * that says which number to write.
 */
TEST(DriverAnswerSecret, TheCeilingIsHeldToTheOverlapThisSuiteMeasures)
{
    REQUIRE_CURL();

    //A measure that cannot report a run reads as a clean zero everywhere below.
    ASSERT_EQ("bcdef", longestEchoRun("zzbcdefzz", "abcdefg"));
    ASSERT_EQ("", longestEchoRun("zzz", "abc"));
    ASSERT_EQ("bcd", longestEchoAnyShape("zz" + percentEncoded("/bcd/") + "zz", "abcde"));

    std::string worst, worstLabel;
    for (const Probe &p: probes())
    {
        ASSERT_FALSE(p.log->empty())
            << p.label << " has no journal, so the overlap this case exists to "
               "pin was not produced at all";

        const std::string run = longestEchoAnyShape(*p.log, p.document);
        if (run.size() > worst.size())
        {
            worst = run;
            worstLabel = p.label;
        }
    }

    EXPECT_EQ(kMaxEcho, worst.size() + 1)
        << "the ceiling is " << kMaxEcho << " while this tree gives back at "
           "most " << worst.size() << " bytes of a document, at " << worstLabel
        << ", on the run \"" << worst << "\". Everything between the two is a "
           "window this suite cannot see into: either a leak has widened the "
           "overlap, or the fixture has, and the ceiling to write is "
        << (worst.size() + 1) << ".";
}

/*
 * THE FIXTURE, HELD TO THE SAME CEILING.
 *
 * A run two documents share is republished by whichever site leaks first, so
 * it raises the bound of every neighbour that carries it: the red set then
 * names the fixture instead of the site.
 */
TEST(DriverAnswerSecret, NoTwoDocumentsShareARunTheCeilingWouldNotAbsorb)
{
    const std::vector<Probe> all = probes();
    for (size_t i = 0; i < all.size(); i++)
    {
        for (size_t j = i + 1; j < all.size(); j++)
        {
            const std::string run = longestEchoRun(all[i].document, all[j].document);
            EXPECT_LT(run.size(), kMaxEcho)
                << all[i].label << " and " << all[j].label << " share \"" << run
                << "\", " << run.size() << " bytes, which the ceiling of "
                << kMaxEcho << " does not absorb: a leak at either one reddens "
                   "the bound of the other and the red set stops naming a site.";
        }
    }
}

/*
 * Own main instead of gtest_main: DEBUG has to be on before the first log line
 * of the process, the Logger domain map being filled once and never re-read.
 * This is the setting an operator switches on before pasting a journal into a
 * bug report, and it is the only one under which the header block is visible
 * at all.
 */
int main(int argc, char **argv)
{
    //Before anything raises the level, and before there is a loop to duplicate.
    measureStockLogLevel();

    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_driveranswer_cfg_XXXXXX";
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
