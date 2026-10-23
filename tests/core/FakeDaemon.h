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
#pragma once

/*
 * A fake calaos-container, on a real loopback socket, answering the admin
 * routes of its docs/API.md. It runs on its own thread: the server under test
 * runs on the uv loop of the test thread, which keeps pumping while it waits.
 * Requests are served one at a time.
 */

#include "json.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class FakeDaemon
{
public:
    struct Request
    {
        std::string method;
        std::string path;
        std::string body;
        std::map<std::string, std::string> headers; //lowercase names
    };

    struct Response
    {
        int status = 200;
        std::string body;
        int delayMs = 0;
    };

    //Returns true when it produced the answer, false to let the model answer
    using Override = std::function<bool(const Request &, Response &)>;

    static constexpr const char *Password = "correct horse battery";
    static constexpr const char *Session = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

    ~FakeDaemon() { stop(); }

    bool start(int port)
    {
        stop();
        //CLOEXEC: the spawned fake sidecar must not keep the port listening
        listenFd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (listenFd < 0)
            return false;
        int one = 1;
        setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(port);
        if (bind(listenFd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(listenFd, 16) != 0)
        {
            fprintf(stderr, "FakeDaemon: cannot listen on %d: %s\n", port, strerror(errno));
            close(listenFd);
            listenFd = -1;
            return false;
        }

        stopping = false;
        worker = std::thread([this]()
                             {
            run();
        });
        return true;
    }

    //Closes the listening socket: a connection is refused from now on
    void stop()
    {
        stopping = true;
        if (worker.joinable())
            worker.join();
        if (listenFd >= 0)
            close(listenFd);
        listenFd = -1;
    }

    //Model back to a reachable daemon of the current API, no admin defined
    void reset(const std::string &expectedBearer)
    {
        std::lock_guard<std::mutex> l(mutex);
        bearer = expectedBearer;
        adminSet = false;
        oldDaemon = false;
        password = Password;
        override = nullptr;
        log.clear();
    }

    void setAdminSet(bool set)
    {
        std::lock_guard<std::mutex> l(mutex);
        adminSet = set;
    }
    void setOldDaemon(bool old)
    {
        std::lock_guard<std::mutex> l(mutex);
        oldDaemon = old;
    }
    void setBearer(const std::string &b)
    {
        std::lock_guard<std::mutex> l(mutex);
        bearer = b;
    }
    void setOverride(Override o)
    {
        std::lock_guard<std::mutex> l(mutex);
        override = o;
    }
    bool isAdminSet()
    {
        std::lock_guard<std::mutex> l(mutex);
        return adminSet;
    }

    std::vector<Request> requests()
    {
        std::lock_guard<std::mutex> l(mutex);
        return log;
    }

    size_t count(const std::string &path)
    {
        std::lock_guard<std::mutex> l(mutex);
        size_t n = 0;
        for (const Request &r: log)
            if (r.path == path)
                n++;
        return n;
    }

    Request last(const std::string &path)
    {
        std::lock_guard<std::mutex> l(mutex);
        for (auto it = log.rbegin(); it != log.rend(); ++it)
            if (it->path == path)
                return *it;
        return Request();
    }

    void clearLog()
    {
        std::lock_guard<std::mutex> l(mutex);
        log.clear();
    }

    static std::string ok(const nlohmann::json &data)
    {
        return nlohmann::json{ { "error", false }, { "msg", "ok" }, { "data", data } }.dump();
    }

    static std::string err(const std::string &code, const nlohmann::json &data = nlohmann::json())
    {
        nlohmann::json j = { { "error", true }, { "code", code }, { "msg", "daemon says " + code } };
        if (!data.is_null())
            j["data"] = data;
        return j.dump();
    }

private:
    int listenFd = -1;
    std::atomic<bool> stopping{ false };
    std::thread worker;

    std::mutex mutex;
    std::string bearer;
    bool adminSet = false;
    bool oldDaemon = false;
    std::string password = Password;
    Override override;
    std::vector<Request> log;

    void run()
    {
        while (!stopping)
        {
            struct pollfd p = { listenFd, POLLIN, 0 };
            if (poll(&p, 1, 20) <= 0)
                continue;
            const int fd = accept4(listenFd, nullptr, nullptr, SOCK_CLOEXEC);
            if (fd < 0)
                continue;
            serve(fd);
            close(fd);
        }
    }

    bool readRequest(int fd, Request &req)
    {
        std::string raw;
        size_t headerEnd = std::string::npos;
        size_t contentLength = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (std::chrono::steady_clock::now() < deadline && !stopping)
        {
            struct pollfd p = { fd, POLLIN, 0 };
            if (poll(&p, 1, 20) <= 0)
                continue;
            char buf[4096];
            const ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0)
                return false;
            raw.append(buf, (size_t)n);

            if (headerEnd == std::string::npos)
            {
                headerEnd = raw.find("\r\n\r\n");
                if (headerEnd == std::string::npos)
                    continue;
                parseHead(raw.substr(0, headerEnd), req);
                const auto it = req.headers.find("content-length");
                if (it != req.headers.end())
                    contentLength = std::stoul(it->second);
            }
            if (raw.size() >= headerEnd + 4 + contentLength)
            {
                req.body = raw.substr(headerEnd + 4, contentLength);
                return true;
            }
        }
        return false;
    }

    static void parseHead(const std::string &head, Request &req)
    {
        size_t pos = head.find("\r\n");
        const std::string line = head.substr(0, pos);
        const size_t sp1 = line.find(' ');
        const size_t sp2 = line.find(' ', sp1 + 1);
        req.method = line.substr(0, sp1);
        req.path = line.substr(sp1 + 1, sp2 - sp1 - 1);

        while (pos != std::string::npos)
        {
            const size_t start = pos + 2;
            pos = head.find("\r\n", start);
            const std::string h = head.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
            const size_t colon = h.find(':');
            if (colon == std::string::npos)
                continue;
            std::string name = h.substr(0, colon);
            for (char &c: name)
                c = (char)tolower((unsigned char)c);
            std::string value = h.substr(colon + 1);
            value.erase(0, value.find_first_not_of(' '));
            req.headers[name] = value;
        }
    }

    void serve(int fd)
    {
        Request req;
        if (!readRequest(fd, req))
            return;

        Response res;
        {
            std::lock_guard<std::mutex> l(mutex);
            log.push_back(req);
            if (!override || !override(req, res))
                res = model(req);
        }

        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(res.delayMs);
        while (std::chrono::steady_clock::now() < until && !stopping)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        std::string out = "HTTP/1.1 " + std::to_string(res.status) + " X\r\n"
                                                                     "Content-Type: application/json\r\n"
                                                                     "Content-Length: " +
                          std::to_string(res.body.size()) + "\r\n"
                                                            "Connection: close\r\n\r\n" +
                          res.body;
        send(fd, out.data(), out.size(), MSG_NOSIGNAL);
    }

    //Called with the mutex held
    Response model(const Request &req)
    {
        Response res;

        if (req.path == "/api/push")
        {
            res.body = "{}";
            return res;
        }

        if (oldDaemon)
        {
            res.status = 404;
            res.body = "404 page not found";
            return res;
        }

        const auto auth = req.headers.find("authorization");
        if (auth == req.headers.end() || auth->second != "Bearer " + bearer)
        {
            res.status = 401;
            res.body = err("bad_bearer");
            return res;
        }

        const auto sessionIt = req.headers.find("x-calaos-admin-session");
        const bool hasSession = sessionIt != req.headers.end();
        if (hasSession && sessionIt->second != Session)
        {
            res.status = 401;
            res.body = err("bad_session");
            return res;
        }

        const nlohmann::json body = nlohmann::json::parse(req.body, nullptr, false);
        const std::string pass = body.is_object() && body.contains("password") && body["password"].is_string() ? body["password"].get<std::string>() : std::string();

        if (req.method == "GET" && req.path == "/api/v2/admin/status")
        {
            res.body = ok({ { "admin_set", adminSet } });
        }
        else if (req.method == "POST" && req.path == "/api/v2/admin/verify")
        {
            if (!adminSet)
            {
                res.status = 409;
                res.body = err("admin_not_set");
            }
            else if (pass != password)
            {
                res.status = 403;
                res.body = err("bad_password");
            }
            else
            {
                res.body = ok({ { "session", Session }, { "expires_in", 900 } });
            }
        }
        else if (req.method == "POST" && req.path == "/api/v2/admin/set")
        {
            if (hasSession && !adminSet)
            {
                res.status = 409;
                res.body = err("admin_not_set");
            }
            else if (!hasSession && adminSet)
            {
                res.status = 409;
                res.body = err("admin_already_set");
            }
            else if (pass.size() < 10)
            {
                res.status = 400;
                res.body = err("password_too_short", { { "min_length", 10 } });
            }
            else
            {
                adminSet = true;
                password = pass;
                res.body = ok({ { "mode", hasSession ? "change" : "bootstrap" } });
            }
        }
        else
        {
            res.status = 404;
            res.body = err("not_found");
        }

        return res;
    }
};
