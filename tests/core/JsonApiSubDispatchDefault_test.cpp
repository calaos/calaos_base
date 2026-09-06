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

/*
 * WHAT A SUB-DISPATCH WITHOUT A DEFAULT BRANCH COSTS, MEASURED ON A REAL
 * SOCKET.
 *
 * Every case here goes through a real HttpServer bound on loopback: a real
 * connection, a real request, a real answer read back from the wire. Nothing
 * calls processApi() directly, because the thing being measured is not what
 * the dispatch returns - it returns nothing - but what happens to the
 * CONNECTION when it does.
 *
 * THE MECHANISM, AND IT IS THE WHOLE TICKET.
 *   - On HTTP the answer is what releases the socket: sendJson() sets
 *     Connection: Close, and HttpClient force-closes half a second after the
 *     last byte is written. Nothing else closes an idle connection.
 *   - requestReadTimeout() does not save it: it covers the delay BEFORE the
 *     first request is parsed, and a request that reaches a sub-dispatch is
 *     long parsed.
 *   - So a silent branch leaves an established connection with no answer and no
 *     timer, holding one slot of maxConnectionsPerIp() until the peer gives up.
 *
 * THE IDENTITY TRICK. The per-source cap is keyed on the address the trusted
 * proxy hop named, and loopback is a trusted hop, so an X-Forwarded-For line
 * gives a case its own bucket. That is what lets the cap be measured with a
 * small ceiling without any other case of this file borrowing a slot from it.
 */

#include <gtest/gtest.h>

#include "HttpClient.h"
#include "HttpServer.h"
#include "IPCam.h"
#include "JsonApi.h"
#include "ListeRoom.h"
#include "McpServerManager.h"
#include "Room.h"
#include "Utils.h"
#include "json.hpp"

#include "libuvw.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Calaos;
using std::chrono::steady_clock;
using std::chrono::milliseconds;
using std::chrono::seconds;

namespace
{

const char *const kAdminUser = "t359-admin";
const char *const kAdminPass = "t359-admin-password";
const char *const kCameraId = "t359_camera";

//Small enough for the hoarding case to be quick, and reached only by the
//X-Forwarded-For identity that case invents for itself.
const std::size_t kCapForTests = 3;

//The bucket of the hoarding case, and nothing else in this file uses it.
const char *const kHoarderIp = "203.0.113.7";

int &serverPort()
{
    static int p = 0;
    return p;
}

void drainLoop(int ms)
{
    auto loop = uvw::Loop::getDefault();
    const auto deadline = steady_clock::now() + milliseconds(ms);
    while (steady_clock::now() < deadline)
    {
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(milliseconds(1));
    }
}

int pickFreePort()
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return 0;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;

    int port = 0;
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) == 0)
    {
        socklen_t len = sizeof(a);
        if (getsockname(fd, (struct sockaddr *)&a, &len) == 0)
            port = ntohs(a.sin_port);
    }
    close(fd);
    return port;
}

int connectLoopback()
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(serverPort());

    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

std::string httpPost(const std::string &body, const std::string &forwardedFor = std::string())
{
    std::ostringstream req;
    req << "POST /api.php HTTP/1.1\r\n"
        << "Host: 127.0.0.1:" << serverPort() << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n";
    if (!forwardedFor.empty())
        req << "X-Forwarded-For: " << forwardedFor << "\r\n";
    req << "\r\n" << body;
    return req.str();
}

Json authenticated(Json request)
{
    request["cn_user"] = kAdminUser;
    request["cn_pass"] = kAdminPass;
    return request;
}

/*
 * One connection, opened and kept, so a case can observe what the server does
 * with it AFTER the answer - or does not do, when there is no answer.
 */
class HeldConnection
{
public:
    ~HeldConnection() { hangUp(); }

    bool open() { fd = connectLoopback(); return fd >= 0; }

    bool write(const std::string &raw)
    {
        return fd >= 0 && send(fd, raw.data(), raw.size(), MSG_NOSIGNAL) >= 0;
    }

    //Reads until the headers are complete or the delay runs out, pumping the
    //server (which lives on this very thread) as it waits.
    std::string readResponse(int ms = 3000)
    {
        auto loop = uvw::Loop::getDefault();
        const auto deadline = steady_clock::now() + milliseconds(ms);
        while (steady_clock::now() < deadline)
        {
            loop->run<uvw::Loop::Mode::NOWAIT>();

            char buf[8192];
            const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
            if (n > 0)
                rx.append(buf, (std::size_t)n);

            if (rx.find("\r\n\r\n") != std::string::npos)
                break;

            std::this_thread::sleep_for(milliseconds(1));
        }
        return rx;
    }

    void hangUp()
    {
        if (fd >= 0)
            close(fd);
        fd = -1;
    }

private:
    int fd = -1;
    std::string rx;
};

std::string exchange(const std::string &raw, int ms = 3000)
{
    HeldConnection c;
    if (!c.open() || !c.write(raw))
        return std::string();

    const std::string response = c.readResponse(ms);
    c.hangUp();
    drainLoop(80);
    return response;
}

std::string statusLine(const std::string &response)
{
    const std::size_t eol = response.find("\r\n");
    return eol == std::string::npos? std::string(): response.substr(0, eol);
}

std::string headerValue(const std::string &response, const std::string &key)
{
    const std::size_t end = response.find("\r\n\r\n");
    if (end == std::string::npos)
        return std::string();

    const std::string head = Utils::str_to_lower(response.substr(0, end));
    const std::string needle = "\r\n" + Utils::str_to_lower(key) + ":";

    const std::size_t at = head.find(needle);
    if (at == std::string::npos)
        return std::string();

    const std::size_t from = at + needle.size();
    const std::size_t eol = head.find("\r\n", from);
    std::string v = head.substr(from, eol - from);

    const std::size_t b = v.find_first_not_of(" \t");
    return b == std::string::npos? std::string(): v.substr(b);
}

std::string body(const std::string &response)
{
    const std::size_t sep = response.find("\r\n\r\n");
    return sep == std::string::npos? std::string(): response.substr(sep + 4);
}

std::string errorOf(const std::string &response)
{
    const Json j = Json::parse(body(response), nullptr, false);
    if (!j.is_object())
        return std::string();
    const auto it = j.find("error");
    return it == j.end() || !it->is_string()? std::string(): it->get<std::string>();
}

/*
 * A real websocket client, server-to-client frames only (they carry no mask).
 */
class WsClient
{
public:
    ~WsClient()
    {
        if (fd >= 0)
            close(fd);
        drainLoop(30);
    }

    bool open()
    {
        fd = connectLoopback();
        if (fd < 0)
            return false;

        std::ostringstream req;
        req << "GET /api HTTP/1.1\r\n"
            << "Host: 127.0.0.1:" << serverPort() << "\r\n"
            << "Connection: Upgrade\r\n"
            << "Upgrade: websocket\r\n"
            << "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\n"
            << "Sec-WebSocket-Version: 13\r\n\r\n";

        const std::string h = req.str();
        if (send(fd, h.data(), h.size(), MSG_NOSIGNAL) < 0)
            return false;

        const auto deadline = steady_clock::now() + seconds(5);
        while (steady_clock::now() < deadline)
        {
            pump();
            if (handshake.find("\r\n\r\n") != std::string::npos)
                break;
        }
        return handshake.find("101") != std::string::npos;
    }

    bool login()
    {
        const Json env = Json::parse(ask(Json{
            { "msg", "login" }, { "msg_id", "login" },
            { "data", {{ "cn_user", kAdminUser }, { "cn_pass", kAdminPass }} }}),
            nullptr, false);

        return env.is_object() && env.contains("data") &&
               env["data"].is_object() && env["data"].value("success", "") == "true";
    }

    //The payload of the first frame that comes back, or "" when the server
    //said nothing before the delay ran out. An empty answer is a measurement
    //here, not a failure of the client.
    std::string ask(const Json &request, int ms = 1500)
    {
        const std::string frame = maskedTextFrame(request.dump());
        if (send(fd, frame.data(), frame.size(), MSG_NOSIGNAL) < 0)
            return std::string();

        const auto deadline = steady_clock::now() + milliseconds(ms);
        while (steady_clock::now() < deadline)
        {
            pump();
            if (!frames.empty())
            {
                const std::string out = frames.front();
                frames.erase(frames.begin());
                return out;
            }
        }
        return std::string();
    }

private:
    static std::string maskedTextFrame(const std::string &payload)
    {
        static const unsigned char mask[4] = { 0x37, 0xfa, 0x21, 0x3d };

        std::string f;
        f.push_back((char)0x81);
        if (payload.size() < 126)
        {
            f.push_back((char)(0x80 | (payload.size() & 0x7f)));
        }
        else
        {
            f.push_back((char)(0x80 | 126));
            f.push_back((char)((payload.size() >> 8) & 0xff));
            f.push_back((char)(payload.size() & 0xff));
        }
        for (int i = 0; i < 4; i++)
            f.push_back((char)mask[i]);
        for (std::size_t i = 0; i < payload.size(); i++)
            f.push_back((char)(payload[i] ^ mask[i % 4]));
        return f;
    }

    void pump()
    {
        auto loop = uvw::Loop::getDefault();
        loop->run<uvw::Loop::Mode::NOWAIT>();

        char buf[8192];
        const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0)
        {
            if (handshake.find("\r\n\r\n") == std::string::npos)
            {
                handshake.append(buf, (std::size_t)n);
                const std::size_t end = handshake.find("\r\n\r\n");
                if (end != std::string::npos)
                {
                    rx.append(handshake, end + 4, std::string::npos);
                    handshake.erase(end + 4);
                }
            }
            else
            {
                rx.append(buf, (std::size_t)n);
            }
            decode();
        }
        std::this_thread::sleep_for(milliseconds(1));
    }

    void decode()
    {
        while (rx.size() >= 2)
        {
            const unsigned char b1 = (unsigned char)rx[1];
            std::size_t len = b1 & 0x7f;
            std::size_t header = 2;

            if (len == 126)
            {
                if (rx.size() < 4)
                    return;
                len = ((unsigned char)rx[2] << 8) | (unsigned char)rx[3];
                header = 4;
            }
            else if (len == 127)
            {
                if (rx.size() < 10)
                    return;
                len = 0;
                for (int i = 2; i < 10; i++)
                    len = (len << 8) | (unsigned char)rx[i];
                header = 10;
            }

            if (rx.size() < header + len)
                return;

            if (((unsigned char)rx[0] & 0x0f) == 0x1)
                frames.push_back(rx.substr(header, len));
            rx.erase(0, header + len);
        }
    }

    int fd = -1;
    std::string handshake;
    std::string rx;
    std::vector<std::string> frames;
};

std::string wsError(const std::string &envelope)
{
    const Json env = Json::parse(envelope, nullptr, false);
    if (!env.is_object() || !env.contains("data") || !env["data"].is_object())
        return std::string();
    return env["data"].value("error", "");
}

class JsonApiSubDispatchDefaultTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        LoginThrottle::clear();
        drainLoop(20);
    }

    void TearDown() override { drainLoop(50); }
};

} // namespace

/*******************************************************************************
 * camera - HTTP, the site the ticket was opened on
 ******************************************************************************/

TEST_F(JsonApiSubDispatchDefaultTest, AKnownCameraWithAnUnknownTypeAnswersAnErrorAndReleasesTheSocket)
{
    const std::string r = exchange(httpPost(authenticated(Json{
        { "action", "camera" },
        { "id", kCameraId },
        { "type", "t359_not_a_type" }}).dump()));

    EXPECT_EQ("HTTP/1.0 200 OK", statusLine(r));
    EXPECT_EQ("unknown camera type", errorOf(r)) << r;
    //The header is the release: nothing else closes an idle connection.
    EXPECT_EQ("close", headerValue(r, "Connection")) << r;
}

TEST_F(JsonApiSubDispatchDefaultTest, ACameraWithoutATypeAnswersTheSameError)
{
    const std::string r = exchange(httpPost(authenticated(Json{
        { "action", "camera" },
        { "id", kCameraId }}).dump()));

    EXPECT_EQ("unknown camera type", errorOf(r)) << r;
    EXPECT_EQ("close", headerValue(r, "Connection")) << r;
}

TEST_F(JsonApiSubDispatchDefaultTest, AnUnknownCameraIdKeepsItsOwnMisspelledRefusal)
{
    /* The contrast. The two refusals must not collapse into one: an id that
     * does not exist is answered before the sub-dispatch is reached, and its
     * frozen typo is the only thing that tells the two apart on the wire.
     */
    const std::string r = exchange(httpPost(authenticated(Json{
        { "action", "camera" },
        { "id", "t359_no_such_camera" },
        { "type", "t359_not_a_type" }}).dump()));

    EXPECT_EQ("unkown camera id", errorOf(r)) << r;
}

/*******************************************************************************
 * settings - websocket, the second site of the same family
 ******************************************************************************/

TEST_F(JsonApiSubDispatchDefaultTest, AnUnknownSettingsActionAnswersAnError)
{
    WsClient ws;
    ASSERT_TRUE(ws.open());
    ASSERT_TRUE(ws.login());

    EXPECT_EQ("unknown settings action",
              wsError(ws.ask(Json{{ "msg", "settings" }, { "msg_id", "1" },
                                  { "data", {{ "action", "t359_not_an_action" }} }})));
}

TEST_F(JsonApiSubDispatchDefaultTest, ASettingsMessageWithoutAnActionAnswersTheSameError)
{
    WsClient ws;
    ASSERT_TRUE(ws.open());
    ASSERT_TRUE(ws.login());

    EXPECT_EQ("unknown settings action",
              wsError(ws.ask(Json{{ "msg", "settings" }, { "msg_id", "1" }})));
}

TEST_F(JsonApiSubDispatchDefaultTest, SettingsArgumentsAtTheRootReachTheDefaultBranch)
{
    /* The WS/HTTP asymmetry, now visible instead of silent: arguments written
     * at the root of a websocket message are not seen, so a change_cred spelled
     * that way is an unknown action - and the credentials do not move.
     */
    WsClient ws;
    ASSERT_TRUE(ws.open());
    ASSERT_TRUE(ws.login());

    EXPECT_EQ("unknown settings action",
              wsError(ws.ask(Json{{ "msg", "settings" }, { "msg_id", "1" },
                                  { "action", "change_cred" },
                                  { "old_user", kAdminUser }, { "old_pw", kAdminPass },
                                  { "new_user", "t359_new" }, { "new_pw", "t359_new_pw" }})));

    EXPECT_TRUE(JsonApi::checkCredentials(kAdminUser, kAdminPass));
}

/*******************************************************************************
 * ⭐ THE DESCRIPTOR - what the silence actually costs the server
 ******************************************************************************/

TEST_F(JsonApiSubDispatchDefaultTest, ThePerSourceCapIsWhatBoundsAHoardingClient)
{
    /* The two ceilings a hoarded connection runs into, pinned so the numbers
     * quoted about this defect stay tied to the code: one client can hold
     * DefaultMaxConnectionsPerIp connections, and DefaultMaxConnections is what
     * two such clients would take from everybody else.
     */
    EXPECT_EQ(50u, (unsigned)TransportLimits::DefaultMaxConnectionsPerIp);
    EXPECT_EQ(100u, (unsigned)TransportLimits::DefaultMaxConnections);

    //And the override main() installs is live, otherwise the case below would
    //be measuring the shipped 50 with three connections.
    EXPECT_EQ(kCapForTests, TransportLimits::maxConnectionsPerIp());
}

TEST_F(JsonApiSubDispatchDefaultTest, TheAnswerReturnsThePerSourceSlotSoTheNextRequestIsServed)
{
    /* ⭐ THE MEASUREMENT OF THE TICKET.
     *
     * kCapForTests connections are opened from one source, each sends the
     * request that used to be answered by nothing, and none of them hangs up.
     * Two things must hold, and neither holds without a default branch: every
     * one of them is answered, and the slot each holds comes back - so the
     * next request from that same source is served instead of refused with a
     * 429.
     */
    drainLoop(200);

    std::vector<HeldConnection *> held;
    for (std::size_t i = 0; i < kCapForTests; i++)
    {
        HeldConnection *c = new HeldConnection();
        held.push_back(c);

        ASSERT_TRUE(c->open());
        ASSERT_TRUE(c->write(httpPost(authenticated(Json{
            { "action", "camera" },
            { "id", kCameraId },
            { "type", "t359_not_a_type" }}).dump(), kHoarderIp)));
    }

    for (std::size_t i = 0; i < held.size(); i++)
    {
        const std::string r = held[i]->readResponse();
        EXPECT_EQ("unknown camera type", errorOf(r))
                << "connection " << i << " was never answered: " << r;
    }

    //Long enough for the 500ms close timer of every answered connection.
    drainLoop(1500);

    HeldConnection probe;
    ASSERT_TRUE(probe.open());
    ASSERT_TRUE(probe.write(httpPost(authenticated(Json{{ "action", "get_home" }}).dump(),
                                     kHoarderIp)));
    const std::string r = probe.readResponse();

    EXPECT_EQ("HTTP/1.0 200 OK", statusLine(r))
            << "the slots were never returned, the source is capped out: " << r;
    probe.hangUp();

    for (std::size_t i = 0; i < held.size(); i++)
        delete held[i];
    drainLoop(200);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_t359_XXXXXX";
    const char *base = mkdtemp(tmpl);
    if (!base)
    {
        std::cerr << "could not create a temporary config directory" << std::endl;
        return 1;
    }

    const std::string cfg = std::string(base) + "/config";
    const std::string cache = std::string(base) + "/cache";
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(cache.c_str(), 0700);

    Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                             const_cast<char *>(cache.c_str()), true);
    //Loopback only: this binds a real listening socket.
    Utils::set_config_option("listen_address", "127.0.0.1");
    //checkCredentials() prefers cn_user/cn_pass when both are set.
    Utils::set_config_option("calaos_user", kAdminUser);
    Utils::set_config_option("calaos_password", kAdminPass);
    Utils::set_config_option("cn_user", kAdminUser);
    Utils::set_config_option("cn_pass", kAdminPass);
    //Before the first read: the accessor caches its value for the process.
    Utils::set_config_option("max_connections_per_ip", Utils::to_string((int)kCapForTests));

    //start() stat()s calaos_mcp under the bin prefix and returns silently when
    //it is not there, so no sidecar is ever spawned and make check stays
    //hermetic.
    ::setenv("CALAOS_BIN_PREFIX", "/nonexistent/t359/bin", 1);
    McpServerManager::Instance().start();

    /* A camera reachable through ListeRoom::get_io(), which is what
     * processCamera() looks it up with. The unknown-type branch never calls it,
     * but it has to EXIST: an unknown id is refused before the sub-dispatch,
     * and that refusal is a different answer.
     */
    Params camParams;
    camParams.Add("id", kCameraId);
    camParams.Add("name", "T359 camera");
    camParams.Add("type", "IPCam");
    IPCam *camera = new IPCam(camParams);

    Room *room = new Room("t359", "misc");
    ListeRoom::Instance().Add(room);
    room->AddIO(camera);
    ListeRoom::Instance().addIOHash(camera);

    serverPort() = pickFreePort();
    if (serverPort() <= 0)
    {
        std::cerr << "could not reserve a loopback port" << std::endl;
        return 1;
    }

    HttpServer::Instance(serverPort());
    drainLoop(50);

    return RUN_ALL_TESTS();
}
