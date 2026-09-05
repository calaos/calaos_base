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
 * WHAT THE HTTP TRANSPORT WRITES ABOUT THE ANSWERS IT CARRIES.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT
 * ---------------------------------------------------------------------------
 * UrlDownloader::completeCb() streamed the whole response body whenever the
 * transfer was not going to a file. Every HTTP driver of the tree goes
 * through that one line: the amplifier registration answer that carries the
 * device token, the influxdb authorizations listing, the surveillance-station
 * login answer that carries the session id, and whatever a Lua script or a
 * Web IO points at. The transport cannot know: it holds a std::string, not
 * fields - so the fix publishes what is never a secret (status, size)
 * instead of guessing what to hide.
 *
 * The same file already masks URL credentials everywhere and had never
 * touched the body. Protecting one channel had stood in for the other.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS GOES THROUGH A REAL TRANSFER
 * ---------------------------------------------------------------------------
 * Nothing here calls completeCb(). A real HTTP peer on loopback answers a
 * real POST with a body that carries the needle, the libuv loop is pumped
 * until the consumer callback fires, and what is asserted is what the shipped
 * code wrote to std::cout on the way. Putting the old line back in
 * src/lib/UrlDownloader.cpp turns these cases red.
 *
 * The needle is asserted to reach the CONSUMER verbatim before anything is
 * said about the journal: a green must mean "withheld from the log", never
 * "the body never arrived".
 *
 * ---------------------------------------------------------------------------
 * THE HALF THAT MUST NOT DIE WITH THE SECRET
 * ---------------------------------------------------------------------------
 * The body is what a developer reads to understand why a driver fails to
 * decode an answer. Deleting the line would close the leak and blind that.
 * What replaces it has to say that an answer arrived, with which status and
 * how many bytes - a size nobody compares to anything is not a measurement,
 * so the case compares it to the body the peer really sent.
 *
 * WHAT THIS DOES NOT PROVE: no real amplifier, no real camera, no influxdb
 * and no journal of a real install are involved, and the response HEADER
 * block is still published line by line one level above this one.
 *
 * The level the severity rests on is measured in a forked child: this process
 * raises the level in its own main() and can no longer see the one a box
 * ships with.
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
#include <cstdlib>
#include <cctype>
#include <cstring>
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
 * FIXTURE, NOT DECORATION.
 *
 * The needle is a value no other string of this binary can produce by
 * accident, and it sits at the head of the body so that a mutation quoting
 * "just a bit of context" of it is really quoting the secret. The body is
 * shaped after the answer AVRRose::registerDevice() reads: the token is a
 * value inside the document, never a header and never a URL.
 *
 * The token ends in no hexadecimal tail. The journal publishes identifiers of
 * its own in that alphabet - an object address, a url fingerprint - so a
 * hexadecimal needle can be matched by bytes the journal drew at random, which
 * turns the ceiling into a lottery no run of the suite reproduces.
 */
const char *const kDeviceToken = "jeton-appareil-quinzieme-solstice";
const char *const kResponseBody =
    "{\"deviceRoseToken\":\"jeton-appareil-quinzieme-solstice\","
    "\"result\":\"ok\",\"modelName\":\"RS520\"}";

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
        return "http://127.0.0.1:" + std::to_string(port) + "/device_connected";
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

        //Drain whatever the client sends before answering, so the POST body
        //never sits in the socket when the response is written.
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

//Whether SOME single line carries all the needles. "urlutils" appears on
//every line of the domain, so looking for a token anywhere in the log would
//be satisfied by a line that has nothing to do with the answer.
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
 * THE LONGEST RUN OF THE ANSWER THE JOURNAL ECHOES BACK.
 *
 * Looking for the token is looking for a value chosen in advance, and a value
 * search only sees the slice it was spelled for: a line that publishes the
 * first or the last bytes of the same answer carries the answer and cites no
 * needle. What must never reach a journal is any RUN of the body, wherever it
 * was cut - so the log is asked how much of the body it gives back, and the
 * case fixes a ceiling instead of naming a secret.
 */
//The run itself and not its length: a failure that cites the bytes says
//whether what came back is a secret or a word the transport is entitled to.
std::string longestBodyEchoRun(const std::string &log, const std::string &body)
{
    std::string best;
    for (size_t i = 0; i < body.size(); i++)
    {
        size_t len = best.size() + 1;
        while (i + len <= body.size() &&
               log.find(body.substr(i, len)) != std::string::npos)
        {
            best = body.substr(i, len);
            len++;
        }
    }
    return best;
}

//The longest run of the journal's own alphabet a needle carries: an address
//and a fingerprint are printed in it, so such a run can be matched by bytes
//nobody chose.
std::string longestHexRun(const std::string &s)
{
    std::string best, cur;
    for (const char c: s)
    {
        if (::isxdigit(static_cast<unsigned char>(c)))
        {
            cur += c;
            if (cur.size() > best.size())
                best = cur;
        }
        else
            cur.clear();
    }
    return best;
}

size_t longestBodyEcho(const std::string &log, const std::string &body)
{
    return longestBodyEchoRun(log, body).size();
}

//Above the incidental overlap between the answer and what the transport
//legitimately prints (the host and the path of the URL share words with it),
//and far below any excerpt worth publishing. The number is not a decision:
//the ceiling case below re-measures that overlap at every run and pins the
//equality, so a ceiling wider than the tree produces is a red.
const size_t kMaxBodyEcho = 4;

/*
 * WHAT A BOX PRINTS WITH NOBODY TOUCHING ANYTHING, measured in a child.
 *
 * The Logger fills its domain map once and never re-reads it: a process that
 * has raised the level can no longer observe the default. The child never
 * raises it, so it sees the shipped one - and it has to be forked before this
 * process has an event loop or a transfer to duplicate. The default of a new
 * box comes from the fallback of maxLevelPrintable(), not from the registered
 * default of the debug_level option, which is never consulted when the option
 * has not been written.
 */
struct DefaultLevelProbe
{
    bool ran = false;
    bool infoPrinted = false;
    bool debugPrinted = false;
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
        char tmpl[] = "/tmp/calaos_urldlsecret_stock_XXXXXX";
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

            answer = 0x4;
            if (Utils::calaosLogger("urlutils")->isLevelEnabled(Logger::LOG_LEVEL_INFO))
                answer |= 0x1;
            if (Utils::calaosLogger("urlutils")->isLevelEnabled(Logger::LOG_LEVEL_DEBUG))
                answer |= 0x2;
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

    if (got == 1 && (answer & 0x4))
    {
        defaultLevelProbe().ran = true;
        defaultLevelProbe().infoPrinted = (answer & 0x1) != 0;
        defaultLevelProbe().debugPrinted = (answer & 0x2) != 0;
    }
}

//Result of one real transfer: what the consumer got, and what the shipped
//code wrote to std::cout while getting it.
struct Exchange
{
    bool completed = false;
    std::string bodySeen;
    int status = -1;
    std::string log;
};

//connectData=false reproduces the callers that only ever connect
//m_signalComplete (NotifManager, DataLogger writes): the body is accumulated
//and logged all the same, nobody asked for it.
Exchange runPost(bool connectData)
{
    Exchange ex;
    HttpPeer peer(kResponseBody);
    if (peer.port <= 0)
        return ex;

    ex.log = captureStdout([&]()
    {
        //The transport call AVRRose::postRequest() makes, minus the TLS: same
        //object, same setHeader, same httpPost, same completion signals.
        UrlDownloader dl(peer.url(), false);
        dl.setHeader("Content-Type", "application/json;charset=utf-8");

        if (connectData)
        {
            dl.m_signalCompleteData.connect([&](const std::string &d, int s)
            {
                ex.bodySeen = d;
                ex.status = s;
                ex.completed = true;
            });
        }
        else
        {
            dl.m_signalComplete.connect([&](int s)
            {
                ex.status = s;
                ex.completed = true;
            });
        }

        if (!dl.httpPost({}, "{\"deviceName\":\"calaos\"}"))
            return;

        runLoopUntil([&]() { return ex.completed; }, 15000);
        drainLoop();
    });

    return ex;
}

} // namespace

/*
 * THE LEAK ITSELF. The needle reaches the consumer and must not reach the
 * journal, neither alone nor inside the document it came in.
 */
TEST(UrlDownloaderLogSecret, TheResponseBodyNeverReachesTheLog)
{
    REQUIRE_CURL();

    const Exchange ex = runPost(true);

    ASSERT_TRUE(ex.completed) << "the transfer never completed, this case measures nothing";
    ASSERT_EQ(200, ex.status);

    //Fixture check: a green below must mean withheld, not absent.
    ASSERT_NE(std::string::npos, ex.bodySeen.find(kDeviceToken))
        << "the consumer did not receive the token, so the body under test "
           "carries no secret at all";

    //Anti-vacuity: the domain is not mute at this level in this process.
    ASSERT_NE(std::string::npos, ex.log.find("urlutils"))
        << "nothing of the urlutils domain was printed, so finding no secret "
           "in this log proves nothing";

    EXPECT_EQ(std::string::npos, ex.log.find(kDeviceToken))
        << "the device token of the response body is in the journal:\n" << ex.log;
    EXPECT_EQ(std::string::npos, ex.log.find(kResponseBody))
        << "the whole response body is in the journal:\n" << ex.log;
    EXPECT_LT(longestBodyEcho(ex.log, kResponseBody), kMaxBodyEcho)
        << "the journal gives back " << longestBodyEcho(ex.log, kResponseBody)
        << " consecutive bytes of the response body, which no line of this "
           "transport has any business publishing:\n" << ex.log;
}

/*
 * The same line runs for a caller that never asked for the body. Whether a
 * consumer is interested changes nothing to what the transport publishes.
 */
TEST(UrlDownloaderLogSecret, ABodyNoConsumerAskedForIsNotDumpedEither)
{
    REQUIRE_CURL();

    const Exchange ex = runPost(false);

    ASSERT_TRUE(ex.completed) << "the transfer never completed, this case measures nothing";
    ASSERT_EQ(200, ex.status);
    ASSERT_NE(std::string::npos, ex.log.find("urlutils"))
        << "nothing of the urlutils domain was printed, so finding no secret "
           "in this log proves nothing";

    EXPECT_EQ(std::string::npos, ex.log.find(kDeviceToken))
        << "the device token is in the journal of a transfer whose caller only "
           "connected m_signalComplete:\n" << ex.log;
    EXPECT_LT(longestBodyEcho(ex.log, kResponseBody), kMaxBodyEcho)
        << "the journal gives back " << longestBodyEcho(ex.log, kResponseBody)
        << " consecutive bytes of a body no consumer asked for:\n" << ex.log;
}

/*
 * THE COUNTERWEIGHT. Emptying the line would pass the two cases above and
 * leave a developer unable to tell an empty answer from a truncated one.
 * The size is compared to what the peer really sent.
 */
TEST(UrlDownloaderLogSecret, TheLogStillGivesTheStatusAndTheBodySize)
{
    REQUIRE_CURL();

    const Exchange ex = runPost(true);

    ASSERT_TRUE(ex.completed) << "the transfer never completed, this case measures nothing";
    ASSERT_EQ(std::string(kResponseBody).size(), ex.bodySeen.size());

    EXPECT_TRUE(someLineHasAll(ex.log, {"urlutils", "200"}))
        << "no urlutils line reports the http status code:\n" << ex.log;

    const std::string expected = std::to_string(ex.bodySeen.size());
    EXPECT_TRUE(someLineHasAll(ex.log, {"urlutils", expected, "bytes"}))
        << "no urlutils line reports the size of the response body (" << expected
        << " bytes), so a journal can no longer tell an empty answer from a "
           "truncated one:\n" << ex.log;
}

/*
 * THE LEVEL THIS TICKET RESTS ON, MEASURED INSTEAD OF ASSERTED.
 *
 * "The body only leaves once an operator turns DEBUG on" is the whole of the
 * severity, and this process cannot observe it: it raises the level in its
 * own main(). A child that never raised it is asked instead. Lower the
 * shipped fallback of the Logger and this goes red.
 */
TEST(UrlDownloaderLogSecret, AStockInstallDoesNotPrintTheUrlutilsDebugLines)
{
    const DefaultLevelProbe &probe = defaultLevelProbe();

    ASSERT_TRUE(probe.ran)
        << "the stock-level child could not be forked or answered nothing, so "
           "this case measures no level at all";

    EXPECT_TRUE(probe.infoPrinted)
        << "the urlutils domain does not even print at INFO on a stock "
           "install, so this binary is not measuring the shipped level";
    EXPECT_FALSE(probe.debugPrinted)
        << "the urlutils domain prints at DEBUG on a stock install: the "
           "response body left with the journal of every box, with no operator "
           "having switched anything on";
}

/*
 * THE CEILING ITSELF, HELD TO WHAT THIS SUITE MEASURES AT EVERY RUN.
 *
 * A bounded sensor is only as narrow as the number above the noise it was cut
 * for, and that number drifts both ways with nobody watching: a fixture that
 * gains a word shared with the transport lines widens the blind window while
 * every assertion above stays green, and a ceiling left wider than the run
 * this tree really produces is blind space nobody asked for. Pinning the
 * equality turns both into a red that says which number to write.
 *
 * No companion case holds the fixture apart here, and the reason is worth
 * knowing: the ceiling covers ONE document. The two others of the same
 * exchange - the posted body and the path - share "{"device" and "device"
 * with the answer BY THE SHAPE OF THE REAL API (POST /device_connected,
 * {"deviceName":...} answered with a deviceRoseToken), so holding them apart
 * would mean an unfaithful fixture. The consequence is named instead: were
 * the transport ever to republish the request body, the bound above would
 * redden on eight bytes that are not the answer.
 */
TEST(UrlDownloaderLogSecret, TheCeilingIsHeldToTheOverlapThisSuiteMeasures)
{
    REQUIRE_CURL();

    //A measure that cannot report a run reads as a clean zero everywhere else.
    ASSERT_EQ("bcdef", longestBodyEchoRun("zzbcdefzz", "abcdefg"));
    ASSERT_EQ("", longestBodyEchoRun("zzz", "abc"));

    std::string worst, worstLabel;
    const auto keep = [&worst, &worstLabel](const std::string &run,
                                            const std::string &label)
    {
        if (run.size() > worst.size())
        {
            worst = run;
            worstLabel = label;
        }
    };

    const struct { const char *label; bool connectData; } probes[] = {
        { "a caller that asked for the body", true },
        { "a caller that only connected m_signalComplete", false },
    };

    for (const auto &p: probes)
    {
        const Exchange ex = runPost(p.connectData);
        ASSERT_TRUE(ex.completed)
            << "the transfer of " << p.label << " never completed, so the "
               "overlap this case exists to pin was not produced at all";
        ASSERT_NE(std::string::npos, ex.log.find("urlutils"))
            << "nothing of the urlutils domain was printed for " << p.label
            << ", so the overlap measured here is not the one the cases above "
               "are bounded against";
        keep(longestBodyEchoRun(ex.log, kResponseBody), p.label);
    }

    EXPECT_EQ(kMaxBodyEcho, worst.size() + 1)
        << "the ceiling is " << kMaxBodyEcho << " while this tree gives back at "
           "most " << worst.size() << " bytes of the response body, for "
        << worstLabel << ", on the run \"" << worst << "\". Everything between "
           "the two is a window this suite cannot see into: either a leak has "
           "widened the overlap, or the fixture has, and the ceiling to write "
           "is " << (worst.size() + 1) << ".";
}

/*
 * THE ALPHABET THE JOURNAL DRAWS IN, HELD OUT OF THE FIXTURE.
 *
 * The equality above is only reproducible if the bytes nobody chose cannot
 * lengthen a run: an object address and a url fingerprint are printed in
 * hexadecimal, so a body carrying a hexadecimal run as long as the measured
 * overlap makes the ceiling a lottery, and its red an intermittent one nobody
 * can reproduce.
 */
TEST(UrlDownloaderLogSecret, TheResponseBodyDoesNotCarryTheJournalsAlphabetThatFar)
{
    const std::string run = longestHexRun(kResponseBody);
    EXPECT_LT(run.size(), kMaxBodyEcho)
        << "the response body carries \"" << run << "\", " << run.size()
        << " bytes of the alphabet the journal draws its own identifiers in, "
           "which the ceiling of " << kMaxBodyEcho << " does not absorb: a "
           "draw can match them and the equality above becomes a lottery.";
}

/*
 * Own main instead of gtest_main: DEBUG has to be on before the first log
 * line of the process, the Logger domain map being filled once and never
 * re-read. This is the setting an operator switches on before pasting a
 * journal into a bug report, which is the whole scenario.
 */
int main(int argc, char **argv)
{
    //Before anything raises the level, and before there is a loop to duplicate.
    measureStockLogLevel();

    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_urldlsecret_cfg_XXXXXX";
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
