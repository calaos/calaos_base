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
#pragma once

//Shared by the push token tests: pumping the loop while HistLogger's worker
//answers, and reading what the process writes to stdout (the Logger sink).

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <mutex>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "HistLogger.h"
#include "libuvw.h"

namespace PushTest
{

//The phone's side of an encrypted code push, written from docs/08 apart from
//the server's: the code, or empty when the tag does not match.
inline std::string openAdminCode(const std::string &keyHex, const Json &data)
{
    auto bytes = [](const std::string &hex)
    {
        std::string out;
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
            out.push_back((char)std::stoi(hex.substr(i, 2), nullptr, 16));
        return out;
    };
    auto field = [&data](const char *k)
    {
        return data.contains(k) && data[k].is_string() ? data[k].get<std::string>() : std::string();
    };
    auto mac = [](const std::string &key, const std::string &msg)
    {
        unsigned char out[32];
        unsigned int len = 0;
        HMAC(EVP_sha256(), key.data(), (int)key.size(), (const unsigned char *)msg.data(), msg.size(), out, &len);
        return std::string((const char *)out, 32);
    };

    const std::string key = bytes(keyHex), nonce = bytes(field("admin_code_nonce")), ct = bytes(field("admin_code_ct"));
    const std::string tag = bytes(field("admin_code_tag"));
    const std::string id = field("admin_code_id");
    if (key.size() != 32 || nonce.size() != 16 || ct.size() != 6 || tag.size() != 16)
        return std::string();
    const std::string want = mac(key, "calaos-admin-code/mac" + id + nonce + field("admin_code_action") +
                                          field("admin_code_expires") + ct);
    if (CRYPTO_memcmp(want.data(), tag.data(), 16) != 0)
        return std::string();
    const std::string ks = mac(key, "calaos-admin-code/enc" + id + nonce);
    std::string code(6, '\0');
    for (size_t i = 0; i < 6; i++)
        code[i] = (char)(ct[i] ^ ks[i]);
    return code;
}

inline bool pumpUntil(const std::function<bool()> &done)
{
    auto loop = uvw::Loop::getDefault();
    //Long enough for a few statements each waiting out HistLogger's busy
    //timeout on a locked database.
    for (int i = 0; i < 4000; i++)
    {
        if (done())
            return true;
        loop->run<uvw::Loop::Mode::NOWAIT>();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done();
}

struct TokensAnswer
{
    bool received = false;
    bool success = false;
    std::vector<PushToken> tokens;

    const PushToken *find(const std::string &t) const
    {
        for (const auto &p: tokens)
            if (p.token == t)
                return &p;
        return nullptr;
    }
};

inline TokensAnswer readTokens()
{
    auto a = std::make_shared<TokensAnswer>();
    HistLogger::Instance().getPushTokens([a](bool success, string, const vector<PushToken> &tokens)
                                         {
        a->received = true;
        a->success = success;
        a->tokens = tokens;
    });
    pumpUntil([a]()
              {
        return a->received;
    });
    return *a;
}

//At the file descriptor, so the worker thread's lines are caught too.
class StdoutCapture
{
public:
    StdoutCapture()
    {
        char tmpl[] = "/tmp/calaos_push_log_XXXXXX";
        fd = ::mkstemp(tmpl);
        path = tmpl;
        std::cout.flush();
        std::fflush(stdout);
        saved = ::dup(1);
        ::dup2(fd, 1);
    }

    std::string stop()
    {
        if (saved >= 0)
        {
            std::cout.flush();
            std::fflush(stdout);
            ::dup2(saved, 1);
            ::close(saved);
            ::close(fd);
            saved = -1;
        }
        std::ifstream in(path);
        std::stringstream ss;
        ss << in.rdbuf();
        ::unlink(path.c_str());
        return ss.str();
    }

    ~StdoutCapture() { stop(); }

private:
    int fd = -1;
    int saved = -1;
    std::string path;
};

inline std::string makeTestDirs()
{
    char tmpl[] = "/tmp/calaos_push_db_XXXXXX";
    if (!::mkdtemp(tmpl))
        return std::string();
    const std::string base = tmpl;
    const std::string cfg = base + "/config";
    const std::string cache = base + "/cache";
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(cache.c_str(), 0700);
    Utils::initConfigOptions(const_cast<char *>(cfg.c_str()),
                             const_cast<char *>(cache.c_str()), true);
    //Debug lines are where a token would leak first.
    Utils::set_config_option("debug_level", "5");
    return base;
}

//push_tokens as servers before push_format created it.
constexpr const char *OldTokensSchema =
    "CREATE TABLE push_tokens (id INTEGER PRIMARY KEY UNIQUE NOT NULL, "
    "token TEXT, hw_type INTEGER, created_at DATETIME DEFAULT CURRENT_TIMESTAMP);";

//One-shot HTTP peer on loopback standing in for push.calaos.fr: keeps the
//request it got and answers 200.
class HttpPeer
{
public:
    HttpPeer()
    {
        listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
        int on = 1;
        ::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t len = sizeof(addr);
        if (::bind(listenFd, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
            ::listen(listenFd, 1) == 0 &&
            ::getsockname(listenFd, (struct sockaddr *)&addr, &len) == 0)
            port = ntohs(addr.sin_port);
        th = std::thread([this]()
                         {
            run();
        });
    }

    ~HttpPeer()
    {
        stop = true;
        th.join();
        ::close(listenFd);
    }

    std::string url() const { return "http://127.0.0.1:" + std::to_string(port) + "/api/push"; }

    std::string request()
    {
        std::lock_guard<std::mutex> l(m);
        return req;
    }

private:
    void run()
    {
        int fd = -1;
        while (!stop && fd < 0)
        {
            struct pollfd p = { listenFd, POLLIN, 0 };
            if (::poll(&p, 1, 50) > 0)
                fd = ::accept(listenFd, nullptr, nullptr);
        }
        if (fd < 0)
            return;

        std::string got;
        while (!stop)
        {
            const size_t head = got.find("\r\n\r\n");
            if (head != std::string::npos)
            {
                const size_t cl = got.find("Content-Length: ");
                const size_t want = cl == std::string::npos ? 0 : std::stoul(got.substr(cl + 16));
                if (got.size() >= head + 4 + want)
                    break;
            }
            struct pollfd p = { fd, POLLIN, 0 };
            if (::poll(&p, 1, 50) <= 0)
                continue;
            char b[4096];
            const ssize_t n = ::recv(fd, b, sizeof(b), 0);
            if (n <= 0)
                break;
            got.append(b, n);
        }
        {
            std::lock_guard<std::mutex> l(m);
            req = got;
        }
        const std::string resp = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
        ::send(fd, resp.data(), resp.size(), MSG_NOSIGNAL);
        ::close(fd);
    }

    int listenFd = -1;
    int port = 0;
    std::atomic<bool> stop{ false };
    std::thread th;
    std::mutex m;
    std::string req;
};

} //namespace PushTest
