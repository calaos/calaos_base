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

/* What the journal says when an HTTP client drops its connection: a real
 * HttpServer on loopback, a client that resets its socket, and the log the
 * shipped code writes on the way. */

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "HttpServer.h"
#include "Utils.h"
#include "libuvw.h"

using namespace std::chrono;

namespace
{

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

void drainLoop(int ms)
{
    auto loop = uvw::Loop::getDefault();
    auto deadline = steady_clock::now() + milliseconds(ms);
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

int &serverPort()
{
    static int p = 0;
    return p;
}

//Half a request, then a reset: what a browser tab or a sleeping phone does.
std::string logOfAResetConnection()
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return std::string();

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(serverPort());

    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0)
    {
        close(fd);
        return std::string();
    }

    return captureStdout([fd]()
    {
        const std::string partial = "GET /index.html HTTP/1.1\r\nHost: 127.0.0.1\r\n";
        if (send(fd, partial.data(), partial.size(), MSG_NOSIGNAL) < 0)
            return;
        drainLoop(100);

        //A zero linger turns close() into an RST.
        struct linger lg = { 1, 0 };
        setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
        close(fd);
        drainLoop(300);
    });
}

} // namespace

TEST(HttpClientDropLog, PeerDisconnectsAreToldApartFromRealFaults)
{
    for (int code: { UV_ECONNRESET, UV_EPIPE, UV_ETIMEDOUT, UV_ECONNABORTED,
                     UV_ENOTCONN, UV_ECANCELED })
        EXPECT_TRUE(Calaos::isPeerDisconnect(code)) << uv_err_name(code);

    for (int code: { UV_EACCES, UV_EBADF, UV_EINVAL, UV_ENOBUFS, UV_ENOMEM })
        EXPECT_FALSE(Calaos::isPeerDisconnect(code)) << uv_err_name(code);
}

TEST(HttpClientDropLog, AClientResetIsLoggedAtDebugWithItsCauseAndPeer)
{
    const std::string log = logOfAResetConnection();
    ASSERT_NE(std::string::npos, log.find("network"))
        << "nothing of the network domain was printed, so this case measures nothing";

    EXPECT_EQ(std::string::npos, log.find("[CRI]"))
        << "a client going away was logged as critical:\n" << log;
    EXPECT_EQ(std::string::npos, log.find("Error sending data"))
        << "the error happened on read, not on send:\n" << log;

    const std::string line = lineContaining(log, "ECONNRESET");
    ASSERT_FALSE(line.empty()) << "the cause of the drop is not in the log:\n" << log;
    EXPECT_NE(std::string::npos, line.find("[DBG]")) << line;
    EXPECT_NE(std::string::npos, line.find("127.0.0.1")) << line;
}

//Own main: debug_level has to be raised before the first log line of the
//process, the Logger domain map is filled once.
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    char tmpl[] = "/tmp/calaos_httpdrop_cfg_XXXXXX";
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
        Utils::set_config_option("listen_address", "127.0.0.1");
    }

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
