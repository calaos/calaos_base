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
 * WHAT THE HTTP TRANSPORT WRITES ABOUT THE ADDRESS IT CALLS.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT
 * ---------------------------------------------------------------------------
 * Every URL this transport logged went through a masker that replaced the
 * value of ten parameter names and returned untouched as soon as the URL had
 * no query string at all. A Hue bridge carries its API key as a PATH segment
 * (http://<bridge>/api/<key>/lights/<id>), so nothing was ever masked, and
 * the constructor publishes the URL at INFO - the level a box ships with.
 * The key left with the journal every two seconds, on a stock install.
 *
 * A list of names is the failure mode the sibling tickets refused: it is
 * wrong the moment somebody looks somewhere else. So the transport publishes
 * what can never be a secret - scheme, host, port - and gives the rest as a
 * SHAPE (how many path segments, how many bytes, how many query parameters)
 * plus a tag that is stable inside one journal.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS GOES THROUGH A REAL TRANSFER
 * ---------------------------------------------------------------------------
 * Nothing here calls the reducer. A real UrlDownloader is built on a URL
 * shaped like the one HueOutputLightRGB builds, a real HTTP peer on loopback
 * answers it, and what is asserted is what the shipped code wrote to
 * std::cout on the way - constructor line, start line, failure line. Putting
 * the old masker back on any of those lines turns these cases red.
 *
 * ---------------------------------------------------------------------------
 * WHAT THE SENSOR LOOKS FOR, AND WHAT IT DELIBERATELY DOES NOT
 * ---------------------------------------------------------------------------
 * Not a name, and not a length. Looking for "the API key" only sees the
 * slice it was spelled for: a line publishing the first or the last bytes of
 * the same URL carries the secret and cites no needle, and cutting at 24
 * bytes instead of 64 walks under an assertion written for the whole value.
 * So the log is asked how much of the secret-bearing part of the URL it
 * gives back, in the longest RUN, and the case fixes a ceiling.
 *
 * And it is asked twice: once on the bytes as they appear in the URL, once
 * on their percent-DECODED form. A password written mot%20de%20passe in the
 * userinfo is invisible to a search for the plain value, which is how a tail
 * of URL walked past the guard of a sibling ticket.
 *
 * ---------------------------------------------------------------------------
 * THE HALF THAT MUST NOT DIE WITH THE SECRET
 * ---------------------------------------------------------------------------
 * An operator reads this line to know WHICH device was called and WHETHER
 * the call got through. Both are asserted, on the success path and on the
 * failure path - and two endpoints of the SAME host with paths of the same
 * length and the same segment count must still render as two different
 * lines, or a journal of a bridge with twelve lights says nothing at all.
 *
 * WHAT THIS DOES NOT PROVE: no real Hue bridge, no real camera and no
 * journal of a real install are involved; the response HEADER block is still
 * published line by line by getResponseHeaders(); and the drivers that
 * republish a whole response body are outside this transport.
 *
 * The level the severity rests on is measured in a forked child: this
 * process raises the level in its own main() and can no longer see the one a
 * box ships with.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
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
#include "StringUtils.h"
#include "UrlDownloader.h"
#include "libuvw.h"

using namespace std::chrono;

namespace
{

/*
 * FIXTURE, NOT DECORATION.
 *
 * Three secrets in the three shapes a URL can carry one, all in values no
 * other string of this binary can produce by accident. The path key is the
 * Hue one - the shape the ten-name masker never even looked at. The query
 * pair uses names that were absent from that list. The userinfo password is
 * percent-encoded, so a search for its plain form does not see it.
 *
 * Every case asserts the URL it just built really carries them before
 * concluding anything about the journal.
 */
const char *const kPathApiKey = "cle-api-pont-7d41e9b2ac635f08";
const char *const kQueryToken = "jeton-requete-6b0f52d8e1a94c37";
const char *const kUserinfoPassword = "mot%20de%20passe%20operateur";

const char *const kResponseBody = "{\"state\":{\"on\":true,\"bri\":180}}";

std::string huePath(const std::string &lightId)
{
    //The expression HueOutputLightRGB builds on its two-second timer, minus
    //the host: "/api/" + <api param of io.xml> + "/lights/" + <id_hue>.
    return std::string("/api/") + kPathApiKey + "/lights/" + lightId;
}

std::string queryString()
{
    return std::string("token=") + kQueryToken + "&api_key=" + kPathApiKey;
}

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

//HTTP peer on an ephemeral loopback port, serving one connection after
//another until it is destroyed. answers=false accepts and closes without
//writing anything, which is how the failure branch of the transport is
//reached without depending on a port nobody happens to be listening on.
//It has to outlive several transfers: two calls a journal must tell apart
//have to reach the SAME host and port, or the ephemeral port alone would be
//doing the telling.
class HttpPeer
{
public:
    explicit HttpPeer(bool answers = true): respond(answers)
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

    std::string authority() const
    {
        return "127.0.0.1:" + std::to_string(port);
    }

    int port = 0;

private:
    void run()
    {
        while (!stopRequested)
        {
            struct pollfd p = { listenFd, POLLIN, 0 };
            if (poll(&p, 1, 50) <= 0 || !(p.revents & POLLIN))
                continue;

            const int fd = accept(listenFd, nullptr, nullptr);
            if (fd < 0)
                continue;

            serve(fd);
            close(fd);
        }
    }

    void serve(int fd)
    {
        for (int i = 0; i < 20; i++)
        {
            struct pollfd p = { fd, POLLIN, 0 };
            if (poll(&p, 1, 10) <= 0 || !(p.revents & POLLIN))
                break;
            char b[1024];
            if (recv(fd, b, sizeof(b), MSG_DONTWAIT) <= 0)
                break;
        }

        if (!respond)
            return;

        const std::string bodyStr = kResponseBody;
        const std::string resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(bodyStr.size()) + "\r\n"
            "Connection: close\r\n\r\n" + bodyStr;
        send(fd, resp.data(), resp.size(), MSG_NOSIGNAL);
    }

    bool respond = true;
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

std::string lineContaining(const std::string &log, const std::string &needle)
{
    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line))
    {
        if (line.find(needle) != std::string::npos)
            return line;
    }
    return std::string();
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

//Longest run of `secret` the log gives back, wherever that run was cut.
size_t longestRun(const std::string &log, const std::string &secret)
{
    size_t best = 0;
    for (size_t i = 0; i < secret.size(); i++)
    {
        size_t len = best + 1;
        while (i + len <= secret.size() &&
               log.find(secret.substr(i, len)) != std::string::npos)
        {
            best = len;
            len++;
        }
    }
    return best;
}

//Both forms, because the same bytes reach a journal percent-encoded or not.
size_t longestEcho(const std::string &log, const std::string &secret)
{
    const std::string decoded = Utils::url_decode(secret);
    size_t best = longestRun(log, secret);
    if (decoded != secret)
        best = std::max(best, longestRun(log, decoded));
    return best;
}

/*
 * Above the incidental overlap between the secret-bearing part of the URL and
 * what the transport legitimately publishes (a byte count and a port share
 * digits with it), and far below any excerpt of a credential worth having.
 */
const size_t kMaxUrlEcho = 8;

struct DefaultLevelProbe
{
    bool ran = false;
    bool infoPrinted = false;
};

DefaultLevelProbe &defaultLevelProbe()
{
    static DefaultLevelProbe probe;
    return probe;
}

/*
 * WHAT A BOX PRINTS WITH NOBODY TOUCHING ANYTHING, measured in a child.
 *
 * The Logger fills its domain map once and never re-reads it, so a process
 * that has raised the level can no longer observe the default. The child
 * never raises it. The default of a new box comes from the fallback of
 * Logger::maxLevelPrintable(), not from the registered default of the
 * debug_level option, which is never consulted when the option has never
 * been written.
 */
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
        char tmpl[] = "/tmp/calaos_urldlurl_stock_XXXXXX";
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
    }
}

//Result of one real transfer: the URL that was really used, what the consumer
//got, and what the shipped code wrote to std::cout while getting it.
struct Exchange
{
    bool completed = false;
    std::string url;
    int status = -1;
    std::string log;
};

//host:port of a url built by runGet().
std::string authorityOf(const std::string &url)
{
    const size_t at = url.find('@');
    const size_t pathStart = url.find('/', at);
    return url.substr(at + 1, pathStart - at - 1);
}

Exchange runGetOn(HttpPeer &peer, const std::string &lightId)
{
    Exchange ex;
    if (peer.port <= 0)
        return ex;

    ex.url = "http://oper:" + std::string(kUserinfoPassword) + "@" +
             peer.authority() + huePath(lightId) + "?" + queryString();

    ex.log = captureStdout([&]()
    {
        UrlDownloader dl(ex.url, false);
        dl.m_signalComplete.connect([&](int s)
        {
            ex.status = s;
            ex.completed = true;
        });

        if (!dl.httpGet())
            return;

        runLoopUntil([&]() { return ex.completed; }, 15000);
        drainLoop();
    });

    return ex;
}

Exchange runGet(const std::string &lightId, bool peerAnswers)
{
    HttpPeer peer(peerAnswers);
    return runGetOn(peer, lightId);
}

//The constructor line, with the loopback authority blanked out: the peers of
//two runs sit on two ephemeral ports, and telling two lines apart by their
//port number would prove nothing about what the transport renders of the URL.
std::string constructorLine(const std::string &log, const std::string &authority)
{
    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line))
    {
        const size_t at = line.find("UrlDownloader: ");
        if (at == std::string::npos)
            continue;

        std::string out = line.substr(at);
        const size_t host = out.find(authority);
        if (host != std::string::npos)
            out.replace(host, authority.size(), "<peer>");
        return out;
    }
    return std::string();
}

} // namespace

/*
 * THE LEAK ITSELF, IN THE SHAPE THE MASKER NEVER LOOKED AT.
 */
TEST(UrlDownloaderLogUrl, TheApiKeyOfThePathNeverReachesTheLog)
{
    REQUIRE_CURL();

    const Exchange ex = runGet("3", true);

    ASSERT_TRUE(ex.completed) << "the transfer never completed, this case measures nothing";
    ASSERT_EQ(200, ex.status);

    //Fixture check: a green below must mean withheld, not absent.
    ASSERT_NE(std::string::npos, ex.url.find(kPathApiKey))
        << "the url under test carries no api key at all: " << ex.url;
    ASSERT_NE(std::string::npos, ex.url.find(kQueryToken))
        << "the url under test carries no query token at all: " << ex.url;
    ASSERT_NE(std::string::npos, ex.url.find(kUserinfoPassword))
        << "the url under test carries no userinfo password at all: " << ex.url;

    //Anti-vacuity: the domain is not mute at this level in this process.
    ASSERT_NE(std::string::npos, ex.log.find("urlutils"))
        << "nothing of the urlutils domain was printed, so finding no secret "
           "in this log proves nothing";

    EXPECT_EQ(std::string::npos, ex.log.find(kPathApiKey))
        << "the api key of the url path is in the journal:\n" << ex.log;
    EXPECT_EQ(std::string::npos, ex.log.find(kQueryToken))
        << "the token of the url query is in the journal:\n" << ex.log;
    EXPECT_EQ(std::string::npos, ex.log.find(Utils::url_decode(kUserinfoPassword)))
        << "the userinfo password is in the journal, percent-decoded:\n" << ex.log;
}

/*
 * THE SAME, BOUNDED INSTEAD OF NAMED. The three assertions above look for
 * values chosen in advance; this one looks at how much of the credential
 * part of the url the journal gives back, whatever slice of it was taken.
 */
TEST(UrlDownloaderLogUrl, NoRunOfTheCredentialPartOfTheUrlSurvivesInTheLog)
{
    REQUIRE_CURL();

    const Exchange ex = runGet("3", true);

    ASSERT_TRUE(ex.completed) << "the transfer never completed, this case measures nothing";
    ASSERT_NE(std::string::npos, ex.log.find("urlutils"))
        << "nothing of the urlutils domain was printed, so finding no secret "
           "in this log proves nothing";

    const size_t at = ex.url.find('@');
    const size_t pathStart = ex.url.find('/', at);
    ASSERT_NE(std::string::npos, at);
    ASSERT_NE(std::string::npos, pathStart);

    const std::string userinfo = ex.url.substr(std::string("http://").size(),
                                               at - std::string("http://").size());
    const std::string pathAndQuery = ex.url.substr(pathStart);

    ASSERT_NE(std::string::npos, userinfo.find(kUserinfoPassword));
    ASSERT_NE(std::string::npos, pathAndQuery.find(kPathApiKey));

    const size_t echoUserinfo = longestEcho(ex.log, userinfo);
    const size_t echoPath = longestEcho(ex.log, pathAndQuery);

    EXPECT_LT(echoUserinfo, kMaxUrlEcho)
        << "the journal gives back " << echoUserinfo
        << " consecutive bytes of the userinfo of the url:\n" << ex.log;
    EXPECT_LT(echoPath, kMaxUrlEcho)
        << "the journal gives back " << echoPath
        << " consecutive bytes of the path and query of the url, where the "
           "bridge api key lives:\n" << ex.log;
}

/*
 * THE FAILURE PATH. "Transfer failed for <url>" is a WARNING, printed on a
 * stock install, and it republishes the same url on the branch nobody
 * exercised. An unreachable bridge is the common case, not the rare one.
 */
TEST(UrlDownloaderLogUrl, TheFailureLineDoesNotRepublishTheUrlEither)
{
    REQUIRE_CURL();

    const Exchange ex = runGet("3", false);

    ASSERT_TRUE(ex.completed) << "the transfer never completed, this case measures nothing";
    ASSERT_NE(200, ex.status) << "the peer answered, so the failure branch was never taken";

    //The failure LINE and not the whole journal: the constructor line is in
    //this log too, and a case that failed because of it would say nothing
    //about the branch it is here for.
    const std::string failure = lineContaining(ex.log, "Transfer failed");

    //Anti-vacuity: the line was really printed.
    ASSERT_FALSE(failure.empty())
        << "no failure line was printed, this case measures nothing:\n" << ex.log;

    const size_t at = ex.url.find('@');
    const size_t pathStart = ex.url.find('/', at);
    const std::string userinfo = ex.url.substr(std::string("http://").size(),
                                               at - std::string("http://").size());
    const std::string pathAndQuery = ex.url.substr(pathStart);

    EXPECT_EQ(std::string::npos, failure.find(kPathApiKey))
        << "the api key is on the failure line:\n" << failure;
    EXPECT_LT(longestEcho(failure, userinfo), kMaxUrlEcho)
        << "the failure line gives back " << longestEcho(failure, userinfo)
        << " consecutive bytes of the userinfo:\n" << failure;
    EXPECT_LT(longestEcho(failure, pathAndQuery), kMaxUrlEcho)
        << "the failure line gives back " << longestEcho(failure, pathAndQuery)
        << " consecutive bytes of the path and query:\n" << failure;
}

/*
 * THE COUNTERWEIGHT. Publishing nothing would pass every case above and
 * leave an operator unable to say which device was called or whether the
 * call got through. Both halves, on both paths.
 */
TEST(UrlDownloaderLogUrl, TheLogStillSaysWhichDeviceAndWhetherItAnswered)
{
    REQUIRE_CURL();

    const Exchange ok = runGet("3", true);
    ASSERT_TRUE(ok.completed) << "the transfer never completed, this case measures nothing";
    ASSERT_EQ(200, ok.status);

    const std::string hostPort = authorityOf(ok.url);
    ASSERT_EQ(0u, hostPort.find("127.0.0.1:"));

    EXPECT_TRUE(someLineHasAll(ok.log, {"urlutils", hostPort}))
        << "no urlutils line names the host and port that were called, so a "
           "journal can no longer say which device answered:\n" << ok.log;
    EXPECT_TRUE(someLineHasAll(ok.log, {"urlutils", "200"}))
        << "no urlutils line reports the http status:\n" << ok.log;

    const Exchange ko = runGet("3", false);
    ASSERT_TRUE(ko.completed) << "the failed transfer never completed, this case measures nothing";

    EXPECT_TRUE(someLineHasAll(ko.log, {"urlutils", "Transfer failed", hostPort.substr(0, 9)}))
        << "the failure line no longer names the host that could not be "
           "reached:\n" << ko.log;
}

/*
 * THE OTHER HALF OF THE DIAGNOSIS, AND THE ONE A REDUCTION KILLS FIRST.
 *
 * A bridge with twelve lights is polled on twelve urls that share a host, a
 * segment count and a byte count. If the reduced form collapses them into
 * one line, the journal of the only driver this defect was found on becomes
 * unreadable. Two paths of the SAME length must still render differently.
 */
TEST(UrlDownloaderLogUrl, TwoEndpointsOfTheSameHostAreStillTellableApart)
{
    REQUIRE_CURL();

    //One peer for both calls: same host, same port, so nothing but what the
    //transport renders of the PATH can tell the two lines apart.
    HttpPeer peer(true);
    const Exchange three = runGetOn(peer, "3");
    const Exchange seven = runGetOn(peer, "7");

    ASSERT_TRUE(three.completed && seven.completed)
        << "a transfer never completed, this case measures nothing";
    ASSERT_EQ(huePath("3").size(), huePath("7").size())
        << "the two paths do not have the same length, so telling them apart "
           "would prove nothing about the reduced form";

    ASSERT_EQ(authorityOf(three.url), authorityOf(seven.url))
        << "the two calls did not reach the same host and port, so any "
           "difference below could be the port alone";

    const std::string a = constructorLine(three.log, authorityOf(three.url));
    const std::string b = constructorLine(seven.log, authorityOf(seven.url));

    ASSERT_FALSE(a.empty()) << "no constructor line at all:\n" << three.log;
    ASSERT_FALSE(b.empty()) << "no constructor line at all:\n" << seven.log;
    ASSERT_EQ(std::string::npos, a.find("127.0.0.1"))
        << "the peer authority was not blanked out, so any difference below "
           "could be nothing but the ephemeral port: " << a;

    EXPECT_NE(a, b)
        << "two different endpoints of the same host produce the same journal "
           "line, so a reader cannot tell which one was called:\n"
        << a << "\n" << b;
}

/*
 * THE LEVEL THIS TICKET RESTS ON, MEASURED INSTEAD OF ASSERTED.
 *
 * The constructor line is a cInfoDom. "It leaves on a stock install" is the
 * whole of the severity and this process cannot observe it: it raises the
 * level in its own main(). Lower the shipped fallback of the Logger and this
 * goes red.
 */
TEST(UrlDownloaderLogUrl, AStockInstallPrintsTheUrlutilsInfoLines)
{
    const DefaultLevelProbe &probe = defaultLevelProbe();

    ASSERT_TRUE(probe.ran)
        << "the stock-level child could not be forked or answered nothing, so "
           "this case measures no level at all";

    EXPECT_TRUE(probe.infoPrinted)
        << "the urlutils domain does not print at INFO on a stock install, so "
           "this binary is not measuring the shipped level and the severity of "
           "this ticket cannot be read from it";
}

/*
 * Own main instead of gtest_main: the level has to be raised before the first
 * log line of the process, the Logger domain map being filled once and never
 * re-read.
 */
int main(int argc, char **argv)
{
    //Before anything raises the level, and before there is a loop to duplicate.
    measureStockLogLevel();

    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_urldlurl_cfg_XXXXXX";
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
