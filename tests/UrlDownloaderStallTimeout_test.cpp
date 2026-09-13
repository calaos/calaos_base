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
 * WHAT AN HTTP TRANSFER DOES WHEN THE PEER ACCEPTS AND THEN SAYS NOTHING.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT
 * ---------------------------------------------------------------------------
 * UrlDownloader::start() set some twenty easy options and not one of them was
 * a delay. libcurl ships no default for any of the three, so a peer that
 * completes the TCP handshake and then never writes a byte kept its easy
 * handle, its curl_slist, its descriptor, its uvw::PollHandle and its caller
 * for ever - and CURLOPT_FORBID_REUSE means one connection per attempt, so a
 * re-reading caller stacks one per cycle. The ordinary failure mode of a cheap
 * IP camera is exactly this: it answers the SYN and freezes.
 *
 * ---------------------------------------------------------------------------
 * WHY THE STALL BOUND AND NOT A TOTAL DURATION
 * ---------------------------------------------------------------------------
 * Twenty call sites of src/ go through this class. Nineteen fetch something
 * finite - a snapshot, a Hue or audio JSON, an InfluxDB write, a push
 * notification, a Web IO file. The twentieth, JsonApiHandlerHttp's camera
 * relay, reads an MJPEG stream that NEVER ENDS BY CONSTRUCTION. A
 * CURLOPT_TIMEOUT would cut that one off mid-stream, which is why the bound
 * here is an absence of progress (CURLOPT_LOW_SPEED_LIMIT/_TIME) and not a
 * duration. DrippingPeerIsNotCutOffByTheStallBound below is the case that
 * says so: it stays alive far longer than its own bound because it keeps
 * moving.
 *
 * AND AN ABSENCE OF PROGRESS IS NOT ENOUGH FOR THAT RELAY EITHER. A camera
 * that only sends an image on motion is silent for as long as the corridor is
 * empty, and the registry bound cuts it off exactly like a frozen one. So the
 * relay asks for no stall bound at all, and the paired cases below - same
 * peer, same empty corridor, one value apart - are what says the exemption
 * works and that a transfer which did not ask for it is still bounded.
 *
 * The floor counts BODY bytes: the delay runs from the connection to the
 * first byte of body, whatever arrives as headers meanwhile. The two header
 * phase cases measure that, and it is what fixes the threshold below which a
 * peer dribbling headers would be cut.
 *
 * ---------------------------------------------------------------------------
 * WHAT MAKES A GREEN MEAN SOMETHING
 * ---------------------------------------------------------------------------
 * "The transfer stopped" is green on a tree where the transfer never started.
 * So the mute peer reports what it saw: whether it accepted a connection at
 * all, how many request bytes it read, and the request text itself - which
 * has to carry the ephemeral port the kernel handed the peer at run time, a
 * number this file cannot know in advance.
 *
 * NOTHING HERE READS A CLOCK FOR ITS VERDICT. A case asserts that a transfer
 * ENDS or that it DID NOT, never that either happened within N seconds: the
 * bound under test is 2 s and the child gives up after 60, a margin no amount
 * of machine load closes.
 * There is no skip guard of any kind in this file - nothing here needs a
 * binary, a network or a service that could be missing.
 *
 * WHAT THIS DOES NOT PROVE: no real camera and no real install are involved,
 * and the two default values are a product decision argued in the ticket, not
 * measured here.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ConfigOptions.h"
#include "ConfigStore.h"
#include "LogSetup.h"
#include "Logger.h"
#include "UrlDownloader.h"
#include "libuvw.h"

using namespace std::chrono;

namespace
{

//Bodies no other string of this binary produces by accident.
const char *const kLivelySlice = "{\"palier\":\"quarante-deux\",\"etat\":\"goutte-a-goutte\"},";
const char *const kPromptBody = "{\"palier\":\"dix-sept\",\"etat\":\"repondu-aussitot\"}";

//The two images of the relay, told apart by name and not by a count of bytes.
const char *const kFrameBefore = "IMAGE-DU-VESTIBULE-AVANT-LE-COULOIR";
const char *const kFrameAfter = "IMAGE-DU-VESTIBULE-APRES-LE-COULOIR";

const size_t kDripSlices = 40;
const size_t kDripSlice = 52;   //one kLivelySlice per send

std::string drippedBody()
{
    std::string s;
    for (size_t i = 0; i < kDripSlices; i++)
        s += kLivelySlice;
    return s;
}

//The path is asked for by name in the request line the peer reads back.
const char *const kPath = "/flux-muet-du-vestibule";

enum class PeerMode
{
    Mute,       //accepts, reads the request, then never writes and never closes
    Drip,       //answers, but slowly and over a span far longer than the bound
    Prompt,     //answers at once and closes: the disarmed twin
    Blackhole,  //listens with a full accept queue, so the handshake never completes
    Relay,      //multipart stream: one frame, an empty corridor, one frame
    HeaderDrip, //status line and headers byte by byte, then the body at once
};

/*
 * The peer, on an ephemeral loopback port. Blackhole fills its own backlog
 * with one pending connection and never accepts: Linux then drops further
 * SYNs instead of refusing them, so the client hangs in connect() rather than
 * getting ECONNREFUSED.
 */
class Peer
{
public:
    Peer(PeerMode m, int corridorMs = 0, int headerPhaseMs = 0):
        mode(m), corridor(corridorMs), headerPhase(headerPhaseMs)
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
            listen(listenFd, 0) == 0)
        {
            socklen_t len = sizeof(addr);
            if (getsockname(listenFd, (struct sockaddr *)&addr, &len) == 0)
                port = ntohs(addr.sin_port);
        }

        if (mode == PeerMode::Blackhole)
        {
            fillBacklog();
            return;
        }

        th = std::thread([this]() { run(); });
        started = true;
    }

    ~Peer()
    {
        stopRequested = true;
        if (started)
            th.join();
        if (heldFd >= 0)
            close(heldFd);
        for (int fd: backlogFds)
            if (fd >= 0)
                close(fd);
        if (listenFd >= 0)
            close(listenFd);
    }

    std::string url() const
    {
        return "http://127.0.0.1:" + std::to_string(port) + kPath;
    }

    std::string request()
    {
        std::lock_guard<std::mutex> lock(mtx);
        return requestText;
    }

    int port = 0;
    std::atomic<int> accepted{0};

private:
    void fillBacklog()
    {
        //Non-blocking: the connect that overflows the queue never completes,
        //which is the point, and must not block this thread.
        for (int i = 0; i < 2; i++)
        {
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0)
                break;
            fcntl(fd, F_SETFL, O_NONBLOCK);
            struct sockaddr_in a;
            memset(&a, 0, sizeof(a));
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            a.sin_port = htons(port);
            connect(fd, (struct sockaddr *)&a, sizeof(a));
            backlogFds.push_back(fd);
        }
        std::this_thread::sleep_for(milliseconds(100));
    }

    void nap(int ms)
    {
        for (int i = 0; i < ms / 10 && !stopRequested; i++)
            std::this_thread::sleep_for(milliseconds(10));
    }

    /* One MJPEG part, then nothing at all for `corridor`, then the other. No
     * Content-Length on the response: the transfer ends when the peer closes,
     * the way a relay ends when the camera hangs up. */
    void sendRelay(int fd)
    {
        const std::string head =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=calaosframe\r\n\r\n";
        send(fd, head.data(), head.size(), MSG_NOSIGNAL);

        sendFrame(fd, kFrameBefore);
        nap(corridor);
        if (stopRequested)
            return;
        sendFrame(fd, kFrameAfter);
    }

    void sendFrame(int fd, const std::string &image)
    {
        const std::string part =
            "--calaosframe\r\nContent-Type: image/jpeg\r\nContent-Length: " +
            std::to_string(image.size()) + "\r\n\r\n" + image + "\r\n";
        send(fd, part.data(), part.size(), MSG_NOSIGNAL);
    }

    //The whole header block spread over `headerPhase`, one byte at a time.
    void sendHeadersByTheByte(int fd, const std::string &head)
    {
        const int per = head.empty()? 0: headerPhase / (int)head.size();
        for (size_t i = 0; i < head.size() && !stopRequested; i++)
        {
            if (send(fd, head.data() + i, 1, MSG_NOSIGNAL) < 0)
                return;
            std::this_thread::sleep_for(milliseconds(per));
        }
    }

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

        accepted++;
        heldFd = fd;

        std::string req;
        for (int i = 0; i < 200 && !stopRequested; i++)
        {
            struct pollfd p = { fd, POLLIN, 0 };
            if (poll(&p, 1, 10) <= 0 || !(p.revents & POLLIN))
            {
                if (!req.empty())
                    break;
                continue;
            }
            char b[2048];
            const ssize_t n = recv(fd, b, sizeof(b), MSG_DONTWAIT);
            if (n <= 0)
                break;
            req.append(b, (size_t)n);
            if (req.find("\r\n\r\n") != std::string::npos)
                break;
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            requestText = req;
        }

        if (mode == PeerMode::Mute)
        {
            //The whole point: the request was read, nothing is answered, and
            //the connection is not closed either.
            while (!stopRequested)
                std::this_thread::sleep_for(milliseconds(20));
            return;
        }

        if (mode == PeerMode::Relay)
        {
            sendRelay(fd);
            close(fd);
            heldFd = -1;
            return;
        }

        const std::string body = (mode == PeerMode::Drip)? drippedBody(): std::string(kPromptBody);
        const std::string head =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Connection: close\r\n\r\n";

        if (mode == PeerMode::Prompt)
        {
            const std::string resp = head + body;
            send(fd, resp.data(), resp.size(), MSG_NOSIGNAL);
            close(fd);
            heldFd = -1;
            return;
        }

        if (mode == PeerMode::HeaderDrip)
        {
            sendHeadersByTheByte(fd, head);
            send(fd, body.data(), body.size(), MSG_NOSIGNAL);
            close(fd);
            heldFd = -1;
            return;
        }

        /* Drip: headers at once, then one body slice every 200 ms for eight
         * seconds - four times the stall bound, at a rate two orders of
         * magnitude above its 1 byte/s floor. The floor is measured on the
         * BODY: a peer that sends its headers and then stalls is caught, so
         * the slices have to be body bytes, not response bytes. */
        send(fd, head.data(), head.size(), MSG_NOSIGNAL);

        size_t at = 0;
        while (at < body.size() && !stopRequested)
        {
            const size_t n = std::min(kDripSlice, body.size() - at);
            if (send(fd, body.data() + at, n, MSG_NOSIGNAL) < 0)
                break;
            at += n;
            for (int i = 0; i < 20 && !stopRequested; i++)
                std::this_thread::sleep_for(milliseconds(10));
        }
        close(fd);
        heldFd = -1;
    }

    PeerMode mode;
    int corridor = 0;
    int headerPhase = 0;
    std::string requestText;
    std::mutex mtx;
    int listenFd = -1;
    int heldFd = -1;
    std::vector<int> backlogFds;
    std::atomic<bool> stopRequested{false};
    bool started = false;
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
 * WHAT THE CHILD SENDS BACK. Key/value head, then the whole journal verbatim -
 * a haystack narrowed before the search answers the question it was narrowed
 * with.
 */
struct Report
{
    bool ran = false;      //the child wrote a report at all
    bool killed = false;   //the parent had to take it down
    int termSignal = 0;

    int accepted = 0;
    int requestBytes = 0;
    int peerPort = 0;

    bool completed = false;
    int status = -1;
    std::string body;
    std::string request;
    std::string connectSetting;
    std::string stallSetting;
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
    const size_t end = padded.find('\n', at);
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
    const size_t end = padded.find('\n', at);
    return padded.substr(at, end - at);
}

struct Exchange
{
    bool completed = false;
    int status = -1;
    std::string body;
};

Exchange runOneTransfer(const std::string &url, int giveUpMs, long stallOverride)
{
    Exchange ex;
    UrlDownloader dl(url, false);
    /* Left alone unless the case asks: the nineteen finite call sites of src/
     * never touch it, so this is the path they take. */
    if (stallOverride != UrlDownloader::defaultStallTimeout)
        dl.setStallTimeout(stallOverride);
    dl.m_signalCompleteData.connect([&](const std::string &d, int s)
    {
        ex.body = d;
        ex.status = s;
        ex.completed = true;
    });

    if (!dl.httpGet())
        return ex;

    runLoopUntil([&]() { return ex.completed; }, giveUpMs);
    drainLoop();
    return ex;
}

struct Scenario
{
    PeerMode mode = PeerMode::Mute;
    const char *connectTimeout = "2";
    const char *stallTimeout = "2";
    int childGiveUpMs = 60000;   //liveness net, never a verdict: 30x the bound
    int parentGiveUpMs = 120000;
    long stallOverride = UrlDownloader::defaultStallTimeout;
    int corridorMs = 0;
    int headerPhaseMs = 0;
};

/*
 * THE CHILD. It always writes a report, even when the transfer never ends: a
 * child taken down by the parent proves nothing about what it saw before.
 */
Report runInChild(const Scenario &sc)
{
    Report rep;

    char tmpl[] = "/tmp/calaos_urlstall_XXXXXX";
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
        Utils::set_config_option("url_connect_timeout", sc.connectTimeout);
        Utils::set_config_option("url_stall_timeout", sc.stallTimeout);

        /* Read back what the store really holds. A refused write would leave
         * the value of the previous child in place, and every case below
         * would measure a bound it did not set. */
        const std::string connectSetting = Utils::get_config_option("url_connect_timeout");
        const std::string stallSetting = Utils::get_config_option("url_stall_timeout");

        Exchange ex;
        int peerPort = 0, accepted = 0;
        std::string request;

        const std::string log = captureStdout([&]()
        {
            Peer peer(sc.mode, sc.corridorMs, sc.headerPhaseMs);
            peerPort = peer.port;
            if (peer.port <= 0)
                return;

            ex = runOneTransfer(peer.url(), sc.childGiveUpMs, sc.stallOverride);
            accepted = peer.accepted.load();
            request = peer.request();
        });

        //Each is one line of the report: their own newlines would look like the
        //end of a field. A multipart body carries plenty.
        for (char &c: request)
            if (c == '\n' || c == '\r')
                c = '|';
        for (char &c: ex.body)
            if (c == '\n' || c == '\r')
                c = '|';

        std::ofstream out(outPath, std::ios::out | std::ios::trunc | std::ios::binary);
        out << "accepted=" << accepted << "\n"
            << "requestBytes=" << request.size() << "\n"
            << "peerPort=" << peerPort << "\n"
            << "completed=" << (ex.completed ? 1 : 0) << "\n"
            << "status=" << ex.status << "\n"
            << "connectSetting=" << connectSetting << "\n"
            << "stallSetting=" << stallSetting << "\n"
            << "request=" << request << "\n"
            << "body=" << ex.body << "\n"
            << "---LOG---\n" << log;
        out.flush();
        out.close();
        _exit(0);
    }

    const auto deadline = steady_clock::now() + milliseconds(sc.parentGiveUpMs);
    int status = 0;
    for (;;)
    {
        const pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid)
            break;
        if (steady_clock::now() > deadline)
        {
            rep.killed = true;
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
            break;
        }
        std::this_thread::sleep_for(milliseconds(20));
    }
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

    rep.accepted = (int)readLong(head, "accepted", 0);
    rep.requestBytes = (int)readLong(head, "requestBytes", 0);
    rep.peerPort = (int)readLong(head, "peerPort", 0);
    rep.completed = readLong(head, "completed", 0) == 1;
    rep.status = (int)readLong(head, "status", -1);
    rep.request = readStr(head, "request");
    rep.connectSetting = readStr(head, "connectSetting");
    rep.stallSetting = readStr(head, "stallSetting");
    rep.body = readStr(head, "body");
    return rep;
}

//Whether SOME single line carries all the needles. The domain is on every
//line of the journal, so looking for each needle anywhere in the whole text
//would be satisfied by two unrelated lines.
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

//One child run per scenario, shared by the cases that read it: three cases
//asking the same question of three different mute peers measure the same
//thing three times and cost three times as long.
const Report &mutePeerRun()
{
    static const Report rep = runInChild(Scenario{ PeerMode::Mute, "2", "2", 60000, 120000 });
    return rep;
}

const Report &promptPeerRun()
{
    static const Report rep = runInChild(Scenario{ PeerMode::Prompt, "2", "2", 20000, 60000 });
    return rep;
}

//Every .cpp/.h under a directory, for the census case below.
void collectSources(const std::string &dir, std::vector<std::string> &out)
{
    DIR *d = ::opendir(dir.c_str());
    if (!d)
        return;
    while (struct dirent *e = ::readdir(d))
    {
        const std::string name = e->d_name;
        if (name == "." || name == "..")
            continue;
        const std::string full = dir + "/" + name;
        struct stat st;
        if (::stat(full.c_str(), &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
        {
            collectSources(full, out);
            continue;
        }
        const size_t dot = name.rfind('.');
        if (dot == std::string::npos)
            continue;
        const std::string ext = name.substr(dot);
        if (ext == ".cpp" || ext == ".h" || ext == ".hpp")
            out.push_back(full);
    }
    ::closedir(d);
}

std::vector<std::string> readLines(const std::string &path)
{
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string l;
    while (std::getline(in, l))
        lines.push_back(l);
    return lines;
}

size_t lastLineWith(const std::vector<std::string> &lines, const std::string &needle, size_t before)
{
    size_t at = std::string::npos;
    for (size_t i = 0; i < before && i < lines.size(); i++)
        if (lines[i].find(needle) != std::string::npos)
            at = i;
    return at;
}

} // namespace

/*
 * THE DEFECT ITSELF. Before the bound, this child was still in its loop when
 * the parent killed it.
 */
TEST(UrlDownloaderStallTimeout, AMutePeerDoesNotHoldTheTransferForEver)
{
    const Report &rep = mutePeerRun();

    ASSERT_FALSE(rep.killed) << "the child had to be taken down: the transfer never ended";
    ASSERT_TRUE(rep.ran) << "the child wrote no report, so nothing below measures anything";
    ASSERT_EQ("2", rep.stallSetting)
        << "the bound this case believes it set is not the one the store holds";
    EXPECT_TRUE(rep.completed)
        << "the caller was never told the transfer was over: it holds an easy handle, a "
           "descriptor and a poll handle on a peer that will never speak";
}

/*
 * PROOF OF TRAVERSAL. A transfer that never started also never hangs, so the
 * case above would be green on a tree where nothing left the machine. The
 * peer says what it saw, and the request has to name the port the kernel gave
 * it at run time - a number this file cannot know in advance.
 */
TEST(UrlDownloaderStallTimeout, TheMutePeerAcceptedTheConnectionAndReadTheRequest)
{
    const Report &rep = mutePeerRun();

    ASSERT_TRUE(rep.ran);
    ASSERT_GT(rep.peerPort, 0);
    EXPECT_EQ(1, rep.accepted)
        << "the peer never accepted a connection: the transfer did not start";
    EXPECT_GT(rep.requestBytes, 0)
        << "the peer accepted but read nothing: no request was ever sent";
    EXPECT_NE(std::string::npos, rep.request.find(std::string("GET ") + kPath))
        << "what the peer read is not the request this case asked for: [" << rep.request << "]";
    EXPECT_NE(std::string::npos,
              rep.request.find("Host: 127.0.0.1:" + std::to_string(rep.peerPort)))
        << "the request does not name the ephemeral port the peer really listened on ("
        << rep.peerPort << "): [" << rep.request << "]";
}

/*
 * WHAT THE CALLER GETS. "Ended" is not "succeeded": the peer sent no status
 * line and no body, and the transport says so in its journal, on a line that
 * carries the run-time port.
 */
TEST(UrlDownloaderStallTimeout, TheMuteTransferEndsWithoutAStatusOrABody)
{
    const Report &rep = mutePeerRun();

    ASSERT_TRUE(rep.ran);
    ASSERT_TRUE(rep.completed);
    ASSERT_GT(rep.peerPort, 0);
    EXPECT_NE(200, rep.status);
    EXPECT_TRUE(rep.body.empty()) << "a peer that sent nothing produced a body: [" << rep.body << "]";
    EXPECT_TRUE(someLineHasAll(rep.log, { "urlutils", "Transfer failed",
                                          std::to_string(rep.peerPort) }))
        << "the transport never named the failed transfer of the peer on port " << rep.peerPort;
}

/*
 * THE DISARMED TWIN. Same class, same call, same loop: only the peer changed.
 * It is what says the fixture runs a real transfer against a real peer, and
 * that the journal captured above is the real one.
 */
TEST(UrlDownloaderStallTimeout, ThePromptPeerIsAnsweredAndItsBodyRead)
{
    const Report &rep = promptPeerRun();

    ASSERT_FALSE(rep.killed);
    ASSERT_TRUE(rep.ran);
    EXPECT_EQ(1, rep.accepted);
    EXPECT_TRUE(rep.completed);
    EXPECT_EQ(200, rep.status);
    EXPECT_EQ(std::string(kPromptBody), rep.body);
    EXPECT_FALSE(someLineHasAll(rep.log, { "urlutils", "Transfer failed" }))
        << "the transport writes that failure line even when nothing failed, so finding it "
           "in the mute run says nothing";
}

/*
 * THE BOUND IS AN ABSENCE OF PROGRESS, NOT A DURATION. This peer answers one
 * chunk per second for eight seconds - four times its own stall bound. A
 * CURLOPT_TIMEOUT set to the same number would cut it off; the MJPEG relay of
 * JsonApiHandlerHttp is the same shape, without the end.
 */
TEST(UrlDownloaderStallTimeout, ADrippingPeerIsNotCutOffByTheStallBound)
{
    const Report rep = runInChild(Scenario{ PeerMode::Drip, "2", "2", 60000, 120000 });

    ASSERT_FALSE(rep.killed);
    ASSERT_TRUE(rep.ran);
    ASSERT_EQ("2", rep.stallSetting);
    ASSERT_EQ(1, rep.accepted) << "the peer never accepted, so this case never dripped anything";
    EXPECT_TRUE(rep.completed);
    EXPECT_EQ(200, rep.status)
        << "a transfer that kept moving was cut off: the bound is a duration, not a stall";
    EXPECT_EQ(drippedBody().size(), rep.body.size())
        << "the body arrived truncated: the transfer was cut while it was still moving";
    EXPECT_EQ(drippedBody(), rep.body);
}

/*
 * THE OTHER HALF OF THE HOLE. A peer whose accept queue is full never
 * completes the handshake - Linux drops the SYN instead of refusing it - so
 * the stall bound above never applies: nothing is established to stall.
 */
TEST(UrlDownloaderStallTimeout, AHandshakeThatNeverCompletesDoesNotHoldTheTransferForEver)
{
    const Report rep = runInChild(Scenario{ PeerMode::Blackhole, "2", "2", 60000, 120000 });

    ASSERT_FALSE(rep.killed) << "the child had to be taken down: the connect never gave up";
    ASSERT_TRUE(rep.ran);
    ASSERT_EQ("2", rep.connectSetting);
    EXPECT_EQ(0, rep.accepted)
        << "the peer accepted the connection, so this case measured the stall bound and not "
           "the connect one";
    EXPECT_TRUE(rep.completed);
    EXPECT_NE(200, rep.status);
}

/*
 * THE FALLBACK IS A BOUND, NOT THE ABSENCE OF ONE. A value libcurl would read
 * as "no limit" is exactly what the defect was; an unusable setting has to
 * land on the default instead. The child is given six times that default.
 */
TEST(UrlDownloaderStallTimeout, AnUnusableSettingFallsBackToTheDefaultAndNotToNoBoundAtAll)
{
    const Report rep = runInChild(Scenario{ PeerMode::Mute, "0", "0", 180000, 300000 });

    ASSERT_FALSE(rep.killed) << "the child had to be taken down: an out-of-range setting left "
                                "the transfer unbounded";
    ASSERT_TRUE(rep.ran);
    ASSERT_EQ("0", rep.stallSetting)
        << "the out-of-range setting never reached the store, so this case measured a usable "
           "bound and not the fallback";
    ASSERT_EQ(1, rep.accepted) << "the peer never accepted, so nothing was bounded here";
    EXPECT_TRUE(rep.completed);
}

/*
 * WHERE THE TWO NUMBERS COME FROM. They are not derived from anything the
 * tree computes: they are the value the tree already gives the SAME failure on
 * the way in - a client that connects and then sends nothing
 * (request_read_timeout). This case is what makes one of the three move
 * without the others.
 */
TEST(UrlDownloaderStallTimeout, TheOutgoingBoundsAreTheInboundOne)
{
    const Calaos::ConfigOption *inbound = Calaos::ConfigOptions::find("request_read_timeout");
    const Calaos::ConfigOption *connect = Calaos::ConfigOptions::find("url_connect_timeout");
    const Calaos::ConfigOption *stall = Calaos::ConfigOptions::find("url_stall_timeout");

    ASSERT_NE(nullptr, inbound);
    ASSERT_NE(nullptr, connect) << "the outgoing connect bound is not a documented option";
    ASSERT_NE(nullptr, stall) << "the outgoing stall bound is not a documented option";

    EXPECT_EQ(inbound->def(), connect->def())
        << "the outgoing connect bound parted from the inbound one it was taken from";
    EXPECT_EQ(inbound->def(), stall->def())
        << "the outgoing stall bound parted from the inbound one it was taken from";
    EXPECT_EQ(inbound->rangeMin(), stall->rangeMin());
    EXPECT_EQ(inbound->rangeMax(), stall->rangeMax());
}


/*
 * THE REGRESSION THIS BOUND BROUGHT, AND ITS ANSWER. A camera that only sends
 * an image on motion is silent for as long as the corridor is empty: one
 * frame, forty-five seconds of nothing, one frame. Under the delivered bound
 * of 30 s that stream is cut while the camera is perfectly healthy, which is
 * what the twin case below measures. Exempted, it survives.
 *
 * The two cases run the SAME peer with the SAME corridor and differ by one
 * value, so what separates them cannot be anything else.
 */
TEST(UrlDownloaderStallTimeout, AnExemptedEndlessRelaySurvivesAnEmptyCorridor)
{
    Scenario sc;
    sc.mode = PeerMode::Relay;
    sc.stallTimeout = "30";        //the value the registry ships
    sc.corridorMs = 45000;
    sc.stallOverride = UrlDownloader::stallTimeoutNone;
    sc.childGiveUpMs = 150000;
    sc.parentGiveUpMs = 300000;
    const Report rep = runInChild(sc);

    ASSERT_FALSE(rep.killed) << "the child had to be taken down: the relay never ended";
    ASSERT_TRUE(rep.ran);
    ASSERT_EQ("30", rep.stallSetting)
        << "the bound this case believes it faced is not the one the store holds";
    ASSERT_EQ(1, rep.accepted) << "the peer never accepted: nothing was relayed here";
    ASSERT_NE(std::string::npos, rep.body.find(kFrameBefore))
        << "the frame sent before the corridor never arrived, so this case says nothing about "
           "what the corridor did: [" << rep.body << "]";
    EXPECT_NE(std::string::npos, rep.body.find(kFrameAfter))
        << "the frame sent after the empty corridor never reached the caller: the relay was cut "
           "off while the camera was merely silent: [" << rep.body << "]";
    EXPECT_FALSE(someLineHasAll(rep.log, { "urlutils", "Transfer failed",
                                           std::to_string(rep.peerPort) }))
        << "the transport recorded a failed transfer for the exempted relay";
}

/*
 * THE NINETEEN ARE STILL BOUNDED. Same peer, same corridor, and the transfer
 * takes the path a call site that asks for nothing takes - which is every
 * other call site of src/. The second frame never arrives.
 *
 * What it also shows, and what is filed on its own: the caller is told the
 * transfer is over with the status line of the headers it did receive, so an
 * abort arrives looking like a short answer. That is why the verdict here is
 * the missing frame and the journal, not the status.
 */
TEST(UrlDownloaderStallTimeout, TheSameCorridorStillCutsOffATransferThatAskedForNoExemption)
{
    Scenario sc;
    sc.mode = PeerMode::Relay;
    sc.stallTimeout = "30";
    sc.corridorMs = 45000;
    sc.childGiveUpMs = 150000;
    sc.parentGiveUpMs = 300000;
    const Report rep = runInChild(sc);

    ASSERT_FALSE(rep.killed) << "the child had to be taken down: the bound did not bite";
    ASSERT_TRUE(rep.ran);
    ASSERT_EQ("30", rep.stallSetting);
    ASSERT_EQ(1, rep.accepted);
    ASSERT_NE(std::string::npos, rep.body.find(kFrameBefore))
        << "nothing arrived at all: this case measured a transfer that never ran, not one that "
           "was cut: [" << rep.body << "]";
    EXPECT_EQ(std::string::npos, rep.body.find(kFrameAfter))
        << "the corridor did not cost this transfer anything: it is not bounded";
    EXPECT_TRUE(rep.completed);
    EXPECT_TRUE(someLineHasAll(rep.log, { "urlutils", "Transfer failed",
                                          std::to_string(rep.peerPort) }))
        << "the transport never named the transfer it cut off on port " << rep.peerPort;
}

/*
 * WHAT THE FLOOR COUNTS. The peer spells its header block out one byte at a
 * time and only then sends its body. Under the bound, the transfer is whole.
 */
TEST(UrlDownloaderStallTimeout, AHeaderBlockDeliveredUnderTheBoundCostsTheTransferNothing)
{
    Scenario sc;
    sc.mode = PeerMode::HeaderDrip;
    sc.stallTimeout = "4";
    sc.headerPhaseMs = 1000;
    sc.childGiveUpMs = 60000;
    const Report rep = runInChild(sc);

    ASSERT_FALSE(rep.killed);
    ASSERT_TRUE(rep.ran);
    ASSERT_EQ("4", rep.stallSetting);
    ASSERT_EQ(1, rep.accepted);
    EXPECT_TRUE(rep.completed);
    EXPECT_EQ(200, rep.status);
    EXPECT_EQ(std::string(kPromptBody), rep.body);
}

/*
 * THE SAME PEER, SLOWER. Twelve seconds of header block under a four second
 * bound: the transfer is cut with not one byte of body, though the connection
 * never stopped moving. The delay therefore runs from the connection to the
 * FIRST BYTE OF BODY - header bytes buy the peer nothing.
 */
TEST(UrlDownloaderStallTimeout, AHeaderBlockLongerThanTheBoundCutsTheTransferBeforeAnyBody)
{
    Scenario sc;
    sc.mode = PeerMode::HeaderDrip;
    sc.stallTimeout = "4";
    sc.headerPhaseMs = 12000;
    sc.childGiveUpMs = 60000;
    const Report rep = runInChild(sc);

    ASSERT_FALSE(rep.killed);
    ASSERT_TRUE(rep.ran);
    ASSERT_EQ("4", rep.stallSetting);
    ASSERT_EQ(1, rep.accepted) << "the peer never accepted, so nothing dribbled here";
    EXPECT_TRUE(rep.completed);
    EXPECT_TRUE(rep.body.empty())
        << "a body arrived, so the header phase was not what this case measured: ["
        << rep.body << "]";
    EXPECT_TRUE(someLineHasAll(rep.log, { "urlutils", "Transfer failed",
                                          std::to_string(rep.peerPort) }))
        << "the transfer was not cut: a header phase longer than the bound costs nothing";
}

/*
 * THE CENSUS, CHECKED INSTEAD OF ASSERTED. This case reads src/ rather than
 * running anything: it is a drift sensor on the claim the exemption rests on -
 * that exactly one place relays data that has no end of its own - and on the
 * wiring of the exemption itself, which no runtime case of this tree reaches
 * (the mjpeg branch of processCamera() has no suite; the one that exists
 * covers the one-shot snapshot branch).
 */
TEST(UrlDownloaderStallTimeout, TheOnlyRelayOfEndlessDataInSrcAsksForNoStallBound)
{
    std::vector<std::string> files;
    collectSources(CALAOS_SRC_DIR, files);
    ASSERT_GT(files.size(), 100u)
        << "only " << files.size() << " sources were read under " << CALAOS_SRC_DIR
        << ": this case searched no tree at all";

    std::vector<std::pair<std::string, size_t>> sites;
    for (const std::string &f: files)
    {
        const std::vector<std::string> lines = readLines(f);
        for (size_t i = 0; i < lines.size(); i++)
            if (lines[i].find("m_signalData.connect") != std::string::npos)
                sites.push_back({ f, i });
    }

    ASSERT_EQ(1u, sites.size())
        << "src/ no longer has exactly one consumer of a live stream (" << sites.size()
        << "): the exemption below was argued for a single site";

    const std::vector<std::string> lines = readLines(sites[0].first);
    const size_t built = lastLineWith(lines, "new UrlDownloader(", sites[0].second);
    ASSERT_NE(std::string::npos, built)
        << "the downloader feeding " << sites[0].first << ":" << (sites[0].second + 1)
        << " is not built above it, so this case cannot tell what was done to it";

    bool exempted = false;
    for (size_t i = built + 1; i < sites[0].second; i++)
        if (lines[i].find("setStallTimeout(") != std::string::npos)
            exempted = true;

    EXPECT_TRUE(exempted)
        << "the downloader built at " << sites[0].first << ":" << (built + 1)
        << " carries a stream with no end of its own, and nothing before its consumer at line "
        << (sites[0].second + 1) << " lifts the stall bound off it";
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_urlstall_cfg_XXXXXX";
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
