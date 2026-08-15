// T2.10: UrlDownloader lifecycle regressions. These tests pin down the
// *observable* contract of the class so the transport backend can be swapped
// (T2.5 plans a libcurl-multi rewrite) without changing behavior:
//  - a transfer against a local HTTP server completes with the body and the
//    http status code;
//  - cancel() really interrupts the transfer (the server sees the connection
//    drop) and no callback ever fires afterwards;
//  - deleting a non-autodelete downloader mid-transfer is safe and also stops
//    the transfer;
//  - cancelling an autodelete downloader lets it free itself safely;
//  - the internally accumulated response body is bounded while streaming
//    consumers still receive every byte;
//  - no temp file and no fd is leaked across complete/cancel/destroy cycles.
// Nothing here asserts on subprocess details (pid, argv, curl itself): only
// API semantics and what a peer can observe on the wire.

#include "UrlDownloader.h"
#include "libuvw.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <glob.h>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace std::chrono;

namespace
{

//The downloader (currently) spawns the curl binary: without it every test
//would fail for the wrong reason. Checked without fork/exec so libuv's
//SIGCHLD handling is never raced.
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

//Pump the default uvw loop until pred() is true or the timeout elapsed.
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

//Pump the loop for a fixed time so deferred work (close callbacks, the
//autodelete Idler) gets a chance to run.
void drainLoop(int ms = 150)
{
    auto loop = uvw::Loop::getDefault();
    auto deadline = steady_clock::now() + milliseconds(ms);
    while (steady_clock::now() < deadline)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(milliseconds(1));
    }
}

/* Minimal local HTTP peer on an ephemeral port, running in its own thread
 * with plain sockets (same pattern as TcpSocket_test).
 * - fixedBody mode: serves a complete response with Content-Length and
 *   closes: the normal completion path.
 * - stream mode (totalBytes > 0, or 0 for endless): serves headers without
 *   Content-Length then pushes chunks until totalBytes were sent (then EOF)
 *   or until the client disconnects, which it records: that observation is
 *   what "the transfer was really interrupted" means for the tests. */
class StreamServer
{
public:
    StreamServer(std::string fixedBody, size_t totalBytes)
        : body(std::move(fixedBody)), total(totalBytes)
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

    ~StreamServer()
    {
        stopRequested = true;
        th.join();
        if (listenFd >= 0)
            close(listenFd);
    }

    std::string url() const
    {
        return "http://127.0.0.1:" + std::to_string(port) + "/";
    }

    int port = 0;
    std::atomic<bool> clientDisconnected{false};
    std::atomic<size_t> bytesSent{0};

private:
    void run()
    {
        //Accept, interruptible
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

        std::string headers;
        if (!body.empty())
            headers = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                      std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        else
            headers = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                      "Connection: close\r\n\r\n";

        if (send(fd, headers.data(), headers.size(), MSG_NOSIGNAL) < 0)
        {
            clientDisconnected = true;
            close(fd);
            return;
        }

        if (!body.empty())
        {
            //Fixed response fully sent: EOF ends the transfer
            close(fd);
            return;
        }

        //Streaming mode
        const std::string chunk(4096, 'x');
        size_t sent = 0;
        while (!stopRequested && (total == 0 || sent < total))
        {
            struct pollfd p = { fd, POLLOUT | POLLIN, 0 };
            if (poll(&p, 1, 100) < 0)
                break;
            if (p.revents & (POLLERR | POLLHUP))
            {
                clientDisconnected = true;
                break;
            }
            if (p.revents & POLLIN)
            {
                char b[512];
                ssize_t r = recv(fd, b, sizeof(b), MSG_DONTWAIT);
                if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
                {
                    clientDisconnected = true;
                    break;
                }
                //request bytes (r > 0): ignore
            }
            if (p.revents & POLLOUT)
            {
                size_t want = chunk.size();
                if (total > 0 && total - sent < want)
                    want = total - sent;
                ssize_t w = send(fd, chunk.data(), want, MSG_NOSIGNAL | MSG_DONTWAIT);
                if (w < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        continue;
                    clientDisconnected = true;
                    break;
                }
                sent += (size_t)w;
                bytesSent = sent;
            }
            std::this_thread::sleep_for(milliseconds(1));
        }

        close(fd);
    }

    std::string body;
    size_t total;
    int listenFd = -1;
    std::atomic<bool> stopRequested{false};
    std::thread th;
};

size_t countHeaderTmpFiles()
{
    //UrlDownloader keeps the response headers in /tmp/calaos_dlheader_*.tmp
    //files, unlinked on destruction. Only this test binary creates them.
    glob_t g;
    size_t n = 0;
    if (glob("/tmp/calaos_dlheader_*.tmp", 0, nullptr, &g) == 0)
        n = g.gl_pathc;
    globfree(&g);
    return n;
}

size_t countOpenFds()
{
    size_t n = 0;
    DIR *d = opendir("/proc/self/fd");
    if (!d)
        return 0;
    while (readdir(d))
        n++;
    closedir(d);
    return n;
}

} // namespace

TEST(UrlDownloaderLifecycle, CompletesWithBodyAndStatus)
{
    REQUIRE_CURL();

    StreamServer srv("hello calaos", 0);
    ASSERT_GT(srv.port, 0);

    UrlDownloader dl(srv.url(), false);
    std::string gotBody;
    int gotStatus = -1;
    bool done = false;
    dl.m_signalCompleteData.connect([&](const std::string &d, int s)
    {
        gotBody = d;
        gotStatus = s;
        done = true;
    });

    ASSERT_TRUE(dl.httpGet());
    EXPECT_TRUE(dl.isRunning());
    ASSERT_TRUE(runLoopUntil([&]() { return done; }, 15000)) << "download never completed";

    EXPECT_EQ(gotBody, "hello calaos");
    EXPECT_EQ(gotStatus, 200);
    EXPECT_FALSE(dl.isRunning());

    drainLoop();
}

TEST(UrlDownloaderLifecycle, CancelInterruptsTransferAndSilencesCallbacks)
{
    REQUIRE_CURL();

    StreamServer srv("", 0); //endless stream

    auto *dl = new UrlDownloader(srv.url(), false);
    int dataEvents = 0;
    int completeEvents = 0;
    dl->m_signalData.connect([&](int, const char *) { dataEvents++; });
    dl->m_signalComplete.connect([&](int) { completeEvents++; });

    ASSERT_TRUE(dl->httpGet());
    ASSERT_TRUE(runLoopUntil([&]() { return dataEvents > 0; }, 15000))
            << "stream never delivered data";

    dl->cancel();
    EXPECT_FALSE(dl->isRunning());
    EXPECT_TRUE(dl->isCancelled());
    const int dataAtCancel = dataEvents;

    //The transfer must be *really* interrupted: the server observes its
    //client going away, it does not keep feeding a sink forever
    EXPECT_TRUE(runLoopUntil([&]() { return srv.clientDisconnected.load(); }, 15000))
            << "cancel() did not abort the transfer, the server still has a client";

    //And nothing may fire after cancel()
    drainLoop(300);
    EXPECT_EQ(dataEvents, dataAtCancel) << "data callback fired after cancel()";
    EXPECT_EQ(completeEvents, 0) << "completion callback fired after cancel()";

    //A cancelled downloader is inert: no restart
    EXPECT_FALSE(dl->httpGet());

    //...and safe to delete
    delete dl;
    drainLoop();
}

TEST(UrlDownloaderLifecycle, DestroyMidTransferIsSafeAndStopsTransfer)
{
    REQUIRE_CURL();

    StreamServer srv("", 0); //endless stream

    auto *dl = new UrlDownloader(srv.url(), false);
    int dataEvents = 0;
    dl->m_signalData.connect([&](int, const char *) { dataEvents++; });

    ASSERT_TRUE(dl->httpGet());
    ASSERT_TRUE(runLoopUntil([&]() { return dataEvents > 0; }, 15000))
            << "stream never delivered data";

    //Deleting the object mid-transfer must be safe (regression: the stdio
    //pipe used to stay open and called back into freed memory) and must stop
    //the transfer
    delete dl;

    EXPECT_TRUE(runLoopUntil([&]() { return srv.clientDisconnected.load(); }, 15000))
            << "deleting the downloader did not stop the transfer";
    drainLoop(300); //any late callback into freed memory shows up under ASan here
}

TEST(UrlDownloaderLifecycle, AutodeleteCancelFreesItselfSafely)
{
    REQUIRE_CURL();

    StreamServer srv("", 0); //endless stream

    //Autodelete object: it must never be deleted from outside, cancel() is
    //the only external control and has to end in a clean self-destruction
    //(regression: external delete raced the completion Idler)
    auto *dl = new UrlDownloader(srv.url(), true);
    int dataEvents = 0;
    dl->m_signalData.connect([&](int, const char *) { dataEvents++; });

    ASSERT_TRUE(dl->httpGet());
    ASSERT_TRUE(runLoopUntil([&]() { return dataEvents > 0; }, 15000))
            << "stream never delivered data";

    dl->cancel();
    dl = nullptr; //the object frees itself, it must not be touched anymore

    EXPECT_TRUE(runLoopUntil([&]() { return srv.clientDisconnected.load(); }, 15000))
            << "cancel() did not abort the transfer";
    drainLoop(300); //self-destruction runs here; double free / UAF shows up under ASan
}

TEST(UrlDownloaderLifecycle, AccumulatedBufferIsBoundedWhileStreamStaysComplete)
{
    REQUIRE_CURL();

    const size_t cap = 64 * 1024;
    const size_t streamTotal = 300 * 1024;

    StreamServer srv("", streamTotal);

    UrlDownloader dl(srv.url(), false);
    dl.bufferMaxSizeSet(cap);
    EXPECT_EQ(dl.bufferMaxSizeGet(), cap);

    size_t streamedBytes = 0;
    std::string finalBody;
    int gotStatus = -1;
    bool done = false;
    dl.m_signalData.connect([&](int size, const char *) { streamedBytes += (size_t)size; });
    dl.m_signalCompleteData.connect([&](const std::string &d, int s)
    {
        finalBody = d;
        gotStatus = s;
        done = true;
    });

    ASSERT_TRUE(dl.httpGet());
    ASSERT_TRUE(runLoopUntil([&]() { return done; }, 30000)) << "stream never completed";

    EXPECT_EQ(gotStatus, 200);
    //Live consumers received the whole stream...
    EXPECT_EQ(streamedBytes, streamTotal);
    //...but the internal accumulation stopped at the bound (regression: an
    //endless MJPEG stream grew the RAM without limit)
    EXPECT_EQ(finalBody.size(), cap);

    drainLoop();
}

TEST(UrlDownloaderLifecycle, NoTempFileNorFdLeakAcrossLifecycles)
{
    REQUIRE_CURL();

    //Warm everything up once (loop internals, epoll, signal pipe) so the
    //baseline below is stable
    {
        StreamServer srv("warmup", 0);
        UrlDownloader dl(srv.url(), false);
        bool done = false;
        dl.m_signalComplete.connect([&](int) { done = true; });
        ASSERT_TRUE(dl.httpGet());
        ASSERT_TRUE(runLoopUntil([&]() { return done; }, 15000));
    }
    drainLoop(300);

    const size_t tmpBaseline = countHeaderTmpFiles();
    const size_t fdBaseline = countOpenFds();

    for (int i = 0; i < 5; i++)
    {
        //One completed + destroyed
        {
            StreamServer srv("cycle", 0);
            UrlDownloader dl(srv.url(), false);
            bool done = false;
            dl.m_signalComplete.connect([&](int) { done = true; });
            ASSERT_TRUE(dl.httpGet());
            ASSERT_TRUE(runLoopUntil([&]() { return done; }, 15000));
        }

        //One cancelled mid-transfer (autodelete: frees itself)
        {
            StreamServer srv("", 0);
            auto *dl = new UrlDownloader(srv.url(), true);
            int dataEvents = 0;
            dl->m_signalData.connect([&](int, const char *) { dataEvents++; });
            ASSERT_TRUE(dl->httpGet());
            ASSERT_TRUE(runLoopUntil([&]() { return dataEvents > 0; }, 15000));
            dl->cancel();
            ASSERT_TRUE(runLoopUntil([&]() { return srv.clientDisconnected.load(); }, 15000));
            drainLoop(200);
        }

        //One deleted mid-transfer
        {
            StreamServer srv("", 0);
            auto *dl = new UrlDownloader(srv.url(), false);
            int dataEvents = 0;
            dl->m_signalData.connect([&](int, const char *) { dataEvents++; });
            ASSERT_TRUE(dl->httpGet());
            ASSERT_TRUE(runLoopUntil([&]() { return dataEvents > 0; }, 15000));
            delete dl;
            ASSERT_TRUE(runLoopUntil([&]() { return srv.clientDisconnected.load(); }, 15000));
            drainLoop(200);
        }
    }

    drainLoop(400);

    EXPECT_EQ(countHeaderTmpFiles(), tmpBaseline)
            << "header temp files leaked across downloader lifecycles";
    EXPECT_LE(countOpenFds(), fdBaseline)
            << "fds leaked across downloader lifecycles";
}

TEST(UrlDownloaderLifecycle, CancelBeforeStartMakesObjectInert)
{
    //No network, no curl needed: pure API contract
    UrlDownloader dl("http://127.0.0.1:1/", false);
    dl.cancel();
    EXPECT_TRUE(dl.isCancelled());
    EXPECT_FALSE(dl.isRunning());
    EXPECT_FALSE(dl.httpGet()) << "a cancelled downloader must not restart";
    drainLoop(50);
}

// ---------------------------------------------------------------------------
// # T2.17 — TLS verified by default (setInsecure() opt-in) + credential-masked
// URL logging. The masking helper moved from IPCam (server binary) into
// src/lib (Utils::maskUrlCredentials) so UrlDownloader can mask every URL it
// logs; IPCam::maskUrlCredentials delegates to it (contract pinned by
// IPCamUrl_test). The loopback peer of the tests above is plain HTTP: the TLS
// default change must not affect it.

#include "StringUtils.h"

TEST(UrlDownloaderTls, VerifiedByDefaultInsecureIsOptIn)
{
    UrlDownloader dl("https://example.invalid/", false);
    EXPECT_FALSE(dl.isInsecure()) << "TLS verification must be the default";
    dl.setInsecure();
    EXPECT_TRUE(dl.isInsecure());
}

TEST(UrlDownloaderTls, PlainHttpUnaffectedByTlsDefaults)
{
    REQUIRE_CURL();

    //Verified-by-default downloader over plain HTTP: completes as before
    {
        StreamServer srv("verified default", 0);
        UrlDownloader dl(srv.url(), false);
        std::string body;
        bool done = false;
        dl.m_signalCompleteData.connect([&](const std::string &d, int s)
        {
            body = d;
            done = (s == 200);
        });
        ASSERT_TRUE(dl.httpGet());
        ASSERT_TRUE(runLoopUntil([&]() { return done; }, 15000));
        EXPECT_EQ(body, "verified default");
    }

    //setInsecure() over plain HTTP: no-op, still completes
    {
        StreamServer srv("insecure optin", 0);
        UrlDownloader dl(srv.url(), false);
        dl.setInsecure();
        std::string body;
        bool done = false;
        dl.m_signalCompleteData.connect([&](const std::string &d, int s)
        {
            body = d;
            done = (s == 200);
        });
        ASSERT_TRUE(dl.httpGet());
        ASSERT_TRUE(runLoopUntil([&]() { return done; }, 15000));
        EXPECT_EQ(body, "insecure optin");
    }

    drainLoop();
}

TEST(MaskUrlCredentialsLib, HelperLivesInLibAndMasksCredentials)
{
    //Userinfo password: user kept, password masked
    EXPECT_EQ("http://admin:*****@10.0.0.1:80/img.cgi?q=30",
              Utils::maskUrlCredentials("http://admin:s3cret@10.0.0.1:80/img.cgi?q=30"));

    //Credential-bearing query parameters, case-insensitive keys
    EXPECT_EQ("http://cam:88/x?cmd=snap&usr=*****&pwd=*****",
              Utils::maskUrlCredentials("http://cam:88/x?cmd=snap&usr=admin&pwd=s3cret"));
    EXPECT_EQ("https://nas:5001/snap.cgi?id=3&_sid=*****&profileType=1",
              Utils::maskUrlCredentials("https://nas:5001/snap.cgi?id=3&_sid=AbC123&profileType=1"));
    EXPECT_EQ("http://cam/x?PWD=*****&Usr=*****",
              Utils::maskUrlCredentials("http://cam/x?PWD=a&Usr=b"));

    //No credentials: untouched (host:port colon is not a password)
    const std::string plain = "http://10.0.0.2:8080/Jpeg/CamImg.jpg?res=640x480";
    EXPECT_EQ(plain, Utils::maskUrlCredentials(plain));
    EXPECT_EQ("", Utils::maskUrlCredentials(""));
    EXPECT_EQ("not a url", Utils::maskUrlCredentials("not a url"));
}
