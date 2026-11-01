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

//An autodelete UrlDownloader frees itself once its transfer ends, which can
//be after the IO that started it was deleted. Its completion handler must
//then leave the IO alone. A loopback peer holds one request until the IO is
//gone; ASan reports the use after free, and for the Synology chain the next
//request of the chain is a second witness.

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "CalaosCoreFixture.h"
#include "IPCam.h"
#include "ListeRoom.h"
#include "libuvw.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

struct Reply
{
    std::string contentType;
    std::string body;
    std::string status = "200 OK";
};

//Answers each connection with the next reply of its script, and holds the
//connection numbered holdAt until release().
class ScriptedPeer
{
public:
    ScriptedPeer(std::vector<Reply> r, int hold):
        replies(std::move(r)),
        holdAt(hold)
    {
        listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
        int on = 1;
        ::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(listenFd, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
            ::listen(listenFd, 4) == 0)
        {
            socklen_t len = sizeof(addr);
            if (::getsockname(listenFd, (struct sockaddr *)&addr, &len) == 0)
                port = ntohs(addr.sin_port);
        }

        th = std::thread([this]()
                         {
            run();
        });
    }

    ~ScriptedPeer()
    {
        stop = true;
        th.join();
        ::close(listenFd);
    }

    std::string hostPort() const { return "127.0.0.1:" + std::to_string(port); }

    void release() { released = true; }
    bool held() const { return isHeld; }
    int requests() const { return received; }
    int served() const { return answered; }

    int port = 0;

private:
    void run()
    {
        for (int i = 0; !stop; i++)
        {
            int fd = -1;
            while (!stop && fd < 0)
            {
                struct pollfd p = { listenFd, POLLIN, 0 };
                if (::poll(&p, 1, 20) > 0 && (p.revents & POLLIN))
                    fd = ::accept(listenFd, nullptr, nullptr);
            }
            if (fd < 0)
                return;

            readRequest(fd);
            received++;

            if (i == holdAt)
            {
                isHeld = true;
                while (!released && !stop)
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            const Reply &r = replies[std::min<size_t>(i, replies.size() - 1)];
            const std::string resp =
                "HTTP/1.1 " + r.status + "\r\n"
                                         "Content-Type: " +
                r.contentType + "\r\n"
                                "Content-Length: " +
                std::to_string(r.body.size()) + "\r\n"
                                                "Connection: close\r\n\r\n" +
                r.body;
            if (::send(fd, resp.data(), resp.size(), MSG_NOSIGNAL) < 0)
            {
            }
            ::close(fd);
            answered++;
        }
    }

    static void readRequest(int fd)
    {
        std::string req;
        for (int i = 0; i < 200 && req.find("\r\n\r\n") == std::string::npos; i++)
        {
            struct pollfd p = { fd, POLLIN, 0 };
            if (::poll(&p, 1, 10) <= 0 || !(p.revents & POLLIN))
                continue;
            char b[1024];
            const ssize_t n = ::recv(fd, b, sizeof(b), MSG_DONTWAIT);
            if (n <= 0)
                return;
            req.append(b, n);
        }
    }

    std::vector<Reply> replies;
    int holdAt;
    int listenFd = -1;
    std::atomic<bool> stop{ false };
    std::atomic<bool> released{ false };
    std::atomic<bool> isHeld{ false };
    std::atomic<int> received{ 0 };
    std::atomic<int> answered{ 0 };
    std::thread th;
};

bool pumpUntil(const std::function<bool()> &done, int timeoutMs)
{
    auto loop = uvw::Loop::getDefault();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!done())
    {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

void pumpFor(int ms)
{
    pumpUntil([]()
              {
        return false;
    },
              ms);
}

const Reply hueState = { "application/json",
                         R"({"state":{"on":true,"reachable":true,"bri":200,"hue":1000,"sat":100}})" };

Params hueParams(const std::string &id, const ScriptedPeer &peer)
{
    return { { "type", "HueOutputLightRGB" },
             { "id", id },
             { "name", id },
             { "host", peer.hostPort() },
             { "api", "fb5key" },
             { "id_hue", "1" },
             { "enabled", "true" },
             { "visible", "true" } };
}

//getApiInfo(Auth) -> login -> getApiInfo(Camera) -> getSnapshot
std::vector<Reply> synoChain()
{
    return { { "application/json",
               R"({"success":true,"data":{"SYNO.API.Auth":{"path":"auth.cgi"}}})" },
             { "application/json", R"({"success":true,"data":{"sid":"fb5sid"}})" },
             { "application/json",
               R"({"success":true,"data":{"SYNO.SurveillanceStation.Camera":{"path":"entry.cgi"}}})" },
             { "image/jpeg", "FB5JPEG" } };
}

Params synoParams(const std::string &id, const ScriptedPeer &peer)
{
    return { { "type", "SynoSurveillanceStation" },
             { "id", id },
             { "name", id },
             { "url", "http://" + peer.hostPort() + "/" },
             { "username", "fb5" },
             { "password", "fb5pass" },
             { "camera_id", "1" },
             { "enabled", "true" },
             { "visible", "true" } };
}

//The plain IPCam download, on the URL the test gives. Owned by its room.
class UrlCamera: public IPCam
{
public:
    UrlCamera(Params &p, std::string pictureUrl):
        IPCam(p),
        url(std::move(pictureUrl))
    {
    }

    std::string getPictureUrl() override { return url; }

private:
    std::string url;
};

using Frames = std::vector<std::string>;

} //namespace

class InFlightDownloadTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig();
    }

    static UrlCamera *addUrlCamera(const std::string &url)
    {
        Params p = { { "type", "UrlCamera" }, { "id", "fb5_url_camera" }, { "name", "fb5" } };
        UrlCamera *camera = new UrlCamera(p, url);

        firstRoom()->AddIO(camera);
        ListeRoom::Instance().addIOHash(camera);
        return camera;
    }

    static std::string urlOf(const ScriptedPeer &peer)
    {
        return "http://" + peer.hostPort() + "/snapshot";
    }
};

//The witness for the case below: the poll does reach the light.
TEST_F(InFlightDownloadTest, AHueLightTakesTheStateItPolled)
{
    ScriptedPeer peer({ hueState }, -1);
    IOBase *o = createIO(hueParams("fb5_hue_live", peer));
    ASSERT_NE(o, nullptr);
    ASSERT_FALSE(o->get_value_bool());

    ASSERT_TRUE(pumpUntil([&]()
                          {
        return o->get_value_bool();
    },
                          6000));
}

TEST_F(InFlightDownloadTest, ADeletedHueLightIgnoresThePollItHadInFlight)
{
    ScriptedPeer peer({ hueState }, 0);
    IOBase *o = createIO(hueParams("fb5_hue_gone", peer));
    ASSERT_NE(o, nullptr);

    //The polling timer ticks every 2s.
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return peer.held();
    },
                          6000));
    ASSERT_TRUE(deleteIO(o));

    peer.release();
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return peer.served() == 1;
    },
                          3000));
    pumpFor(300);
}

TEST_F(InFlightDownloadTest, ASynologyCameraWalksTheWholeChain)
{
    ScriptedPeer peer(synoChain(), -1);
    IPCam *cam = dynamic_cast<IPCam *>(createIO(synoParams("fb5_syno_live", peer)));
    ASSERT_NE(cam, nullptr);

    auto snapshot = std::make_shared<std::string>();
    auto done = std::make_shared<bool>(false);
    cam->downloadSnapshot([snapshot, done](const string &data)
                          {
        *snapshot = data;
        *done = true;
    });

    ASSERT_TRUE(pumpUntil([done]()
                          {
        return *done;
    },
                          5000));
    EXPECT_EQ(*snapshot, "FB5JPEG");
    EXPECT_EQ(peer.requests(), 4);
}

//The two api probes (hops 0 and 2) answering an error end the chain with an
//empty frame, and the next download starts over from the first probe.
TEST_F(InFlightDownloadTest, ASynologyApiProbeFailureAnswersAnEmptyFrame)
{
    for (int hop: { 0, 2 })
    {
        SCOPED_TRACE("hop " + std::to_string(hop));

        std::vector<Reply> chain = synoChain();
        chain[hop] = { "text/plain", "", "503 Service Unavailable" };

        ScriptedPeer peer(chain, -1);
        IPCam *cam = dynamic_cast<IPCam *>(createIO(synoParams("fb5_syno_probe_" + std::to_string(hop), peer)));
        ASSERT_NE(cam, nullptr);

        auto frame = std::make_shared<std::string>("unset");
        cam->downloadSnapshot([frame](const string &data)
                              {
            *frame = data;
        });

        ASSERT_TRUE(pumpUntil([frame]()
                              {
            return *frame != "unset";
        },
                              5000));
        EXPECT_EQ(*frame, "");
        EXPECT_EQ(peer.requests(), hop + 1);
    }
}

//Each of the four requests of the chain in turn is the one in flight when
//the camera is deleted: nothing of the camera runs once it answers, and the
//chain stops there.
TEST_F(InFlightDownloadTest, ADeletedSynologyCameraStopsItsChainAtEveryHop)
{
    for (int hop = 0; hop < 4; hop++)
    {
        SCOPED_TRACE("hop " + std::to_string(hop));

        ScriptedPeer peer(synoChain(), hop);
        const std::string id = "fb5_syno_gone_" + std::to_string(hop);
        IPCam *cam = dynamic_cast<IPCam *>(createIO(synoParams(id, peer)));
        ASSERT_NE(cam, nullptr);

        auto called = std::make_shared<bool>(false);
        cam->downloadSnapshot([called](const string &)
                              {
            *called = true;
        });

        ASSERT_TRUE(pumpUntil([&]()
                              {
            return peer.held();
        },
                              5000));
        ASSERT_TRUE(deleteIO(cam));

        peer.release();
        ASSERT_TRUE(pumpUntil([&]()
                              {
            return peer.served() == hop + 1;
        },
                              3000));
        pumpFor(500);

        EXPECT_EQ(peer.requests(), hop + 1);
        EXPECT_FALSE(*called);
    }
}

/* Whoever asks while a download is running is answered by that download: an
 * empty frame would show as the broken camera picture on that client. */
TEST_F(InFlightDownloadTest, ClientsOfARunningDownloadShareItsFrame)
{
    ScriptedPeer peer({ { "image/jpeg", "FB5FRAME" } }, 0);
    UrlCamera *cam = addUrlCamera(urlOf(peer));

    Frames frames;
    const auto collect = [&frames](const string &data)
    {
        frames.push_back(data);
    };

    cam->downloadSnapshot(collect);
    cam->downloadSnapshot(collect);
    cam->downloadSnapshot(collect);

    ASSERT_TRUE(pumpUntil([&]()
                          {
        return peer.held();
    },
                          5000));
    cam->downloadSnapshot(collect);
    EXPECT_TRUE(frames.empty());

    peer.release();
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return frames.size() == 4;
    },
                          5000));

    EXPECT_EQ(Frames(4, "FB5FRAME"), frames);
    EXPECT_EQ(peer.requests(), 1);
}

TEST_F(InFlightDownloadTest, AFinishedDownloadIsNotKept)
{
    ScriptedPeer peer({ { "image/jpeg", "FB5ONE" }, { "image/jpeg", "FB5TWO" } }, -1);
    UrlCamera *cam = addUrlCamera(urlOf(peer));

    Frames frames;
    const auto collect = [&frames](const string &data)
    {
        frames.push_back(data);
    };

    cam->downloadSnapshot(collect);
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return frames.size() == 1;
    },
                          5000));

    cam->downloadSnapshot(collect);
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return frames.size() == 2;
    },
                          5000));

    EXPECT_EQ(Frames({ "FB5ONE", "FB5TWO" }), frames);
    EXPECT_EQ(peer.requests(), 2);
}

//Restarting clears the buffer of the downloader: the next waiter of the same
//frame must not read it after that.
TEST_F(InFlightDownloadTest, AWaiterAskingAgainLeavesTheFrameOfTheNextOne)
{
    ScriptedPeer peer({ { "image/jpeg", "FB5ONE" }, { "image/jpeg", "FB5TWO" } }, -1);
    UrlCamera *cam = addUrlCamera(urlOf(peer));

    Frames frames;
    const auto collect = [&frames](const string &data)
    {
        frames.push_back(data);
    };

    cam->downloadSnapshot([&](const string &data)
                          {
        frames.push_back(data);
        cam->downloadSnapshot(collect);
    });
    cam->downloadSnapshot(collect);

    ASSERT_TRUE(pumpUntil([&]()
                          {
        return frames.size() == 3;
    },
                          5000));
    EXPECT_EQ(Frames({ "FB5ONE", "FB5ONE", "FB5TWO" }), frames);
}

//No signal comes out of a download that cannot start: nobody may keep waiting.
TEST_F(InFlightDownloadTest, ACameraWithoutUrlAnswersEveryClient)
{
    UrlCamera *cam = addUrlCamera(std::string());

    Frames frames;
    const auto collect = [&frames](const string &data)
    {
        frames.push_back(data);
    };

    cam->downloadSnapshot(collect);
    cam->downloadSnapshot(collect);

    EXPECT_EQ(Frames(2, std::string()), frames);
}

TEST_F(InFlightDownloadTest, AnErrorStatusAnswersEveryClientWithoutTheHost)
{
    ScriptedPeer peer({ { "text/plain", "FB5ERROR", "503 Service Unavailable" } }, 0);
    UrlCamera *cam = addUrlCamera(urlOf(peer));

    Frames frames;
    const auto collect = [&frames](const string &data)
    {
        frames.push_back(data);
    };

    cam->downloadSnapshot(collect);
    cam->downloadSnapshot(collect);

    std::ostringstream sink;
    std::streambuf *saved = std::cout.rdbuf(sink.rdbuf());
    const bool answered = pumpUntil([&]()
                                    {
        if (peer.held())
            peer.release();
        return frames.size() == 2;
    },
                                    5000);
    std::cout.rdbuf(saved);

    ASSERT_TRUE(answered);
    EXPECT_EQ(Frames(2, std::string()), frames);
    EXPECT_EQ(std::string::npos, sink.str().find("127.0.0.1")) << sink.str();
}

TEST_F(InFlightDownloadTest, AnUnreachableCameraAnswersEveryClient)
{
    int closedPort = 0;
    {
        ScriptedPeer gone({ { "image/jpeg", "FB5" } }, -1);
        closedPort = gone.port;
    }
    UrlCamera *cam = addUrlCamera("http://127.0.0.1:" + std::to_string(closedPort) + "/snapshot");

    Frames frames;
    const auto collect = [&frames](const string &data)
    {
        frames.push_back(data);
    };

    cam->downloadSnapshot(collect);
    cam->downloadSnapshot(collect);

    ASSERT_TRUE(pumpUntil([&]()
                          {
        return frames.size() == 2;
    },
                          5000));
    EXPECT_EQ(Frames(2, std::string()), frames);
}

TEST_F(InFlightDownloadTest, ClientsOfASynologyChainShareIt)
{
    ScriptedPeer peer(synoChain(), 3);
    IPCam *cam = dynamic_cast<IPCam *>(createIO(synoParams("fb5_syno_shared", peer)));
    ASSERT_NE(cam, nullptr);

    Frames frames;
    const auto collect = [&frames](const string &data)
    {
        frames.push_back(data);
    };

    cam->downloadSnapshot(collect);
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return peer.held();
    },
                          5000));
    cam->downloadSnapshot(collect);

    peer.release();
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return frames.size() == 2;
    },
                          5000));

    EXPECT_EQ(Frames(2, "FB5JPEG"), frames);
    EXPECT_EQ(peer.requests(), 4);
}

//The Synology override goes through the shared path too: not a jpeg here, so
//the clients of the width receive the frame as it came.
TEST_F(InFlightDownloadTest, AWidthOnASynologyCameraSharesTheChain)
{
    ScriptedPeer peer(synoChain(), -1);
    IPCam *cam = dynamic_cast<IPCam *>(createIO(synoParams("fb5_syno_width", peer)));
    ASSERT_NE(cam, nullptr);

    Frames frames;
    const auto collect = [&frames](const string &data)
    {
        frames.push_back(data);
    };

    cam->downloadThumbnail(236, collect);
    cam->downloadThumbnail(236, collect);

    ASSERT_TRUE(pumpUntil([&]()
                          {
        return frames.size() == 2;
    },
                          5000));

    EXPECT_EQ(Frames(2, "FB5JPEG"), frames);
    EXPECT_EQ(peer.requests(), 4);
}

TEST_F(InFlightDownloadTest, ADeletedCameraDropsTheThumbnailItWasDownloading)
{
    ScriptedPeer peer({ { "image/jpeg", "FB5FRAME" } }, 0);
    UrlCamera *cam = addUrlCamera(urlOf(peer));

    auto called = std::make_shared<bool>(false);
    cam->downloadThumbnail(236, [called](const string &)
                           {
        *called = true;
    });

    ASSERT_TRUE(pumpUntil([&]()
                          {
        return peer.held();
    },
                          5000));
    ASSERT_TRUE(deleteIO(cam));

    peer.release();
    ASSERT_TRUE(pumpUntil([&]()
                          {
        return peer.served() == 1;
    },
                          3000));
    pumpFor(100);

    EXPECT_FALSE(*called);
}
