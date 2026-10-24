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
 * Requests are served one at a time; each /api/v2/events stream gets a thread
 * of its own.
 */

#include "json.hpp"

#include <openssl/evp.h>

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
#include <memory>
#include <mutex>
#include <set>
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
        std::string location;
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

    //Closes the listening socket and the streams: a connection is refused from now on
    void stop()
    {
        stopping = true;
        if (worker.joinable())
            worker.join();
        if (listenFd >= 0)
            close(listenFd);
        listenFd = -1;

        dropEvents();
        std::vector<std::thread> threads;
        {
            std::lock_guard<std::mutex> l(streamsMutex);
            threads.swap(streamThreads);
        }
        for (auto &t: threads)
            t.join();
        std::lock_guard<std::mutex> l(streamsMutex);
        streams.clear();
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
        progress = leakyProgress();
        job = leakyJob();
        alerts.clear();
        alertData.clear();
        approvers = nlohmann::json::array();
        pushPayloads.clear();
        useRawHandshake = false;
        rawHandshakeBytes.clear();
        eventsRefusal = 0;
        eventsMuted = false;
        closeAfterSnapshot = false;
        accepted = 0;
        pings = 0;
        pongs = 0;
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

    //What update/status answers; both are leaky on purpose, as on a box
    void setUpdateStatus(const nlohmann::json &p, const nlohmann::json &j)
    {
        std::lock_guard<std::mutex> l(mutex);
        progress = p;
        job = j;
    }

    static nlohmann::json leakyProgress()
    {
        return { { "status", "upgrading" }, { "total_percent", 42 }, { "current_pkg", "calaos-server" }, { "current_step", "unpack" }, { "current_percent", 50 }, { "pkg_index", 1 }, { "pkg_count", 2 }, { "progress", 1 }, { "progress_total", 2 }, { "packages", { { { "name", "calaos-server" }, { "state", "unpack" }, { "percent", 50 } }, { { "name", "linux-image-arm64" }, { "state", "pending" }, { "percent", 0 } } } }, { "message", "Get:1 http://deb.calaos.fr/ /var/cache/apt/archives/x.deb" }, { "error", "upgrade failed: linux-image-arm64" }, { "a_field_added_later", "inventory" } };
    }

    static nlohmann::json leakyJob()
    {
        return { { "id", "3f9c2a7e5b1d4c80" }, { "kind", "upgrade" }, { "package", "calaos-server" }, { "status", "error" }, { "error", "upgrade failed: calaos-server" }, { "reboot_required", false }, { "daemon_restart", false }, { "started_at", "2026-10-22T09:12:03Z" }, { "finished_at", "2026-10-22T09:13:03Z" } };
    }

    /* /api/v2/events. A stream gets the update/status progress, then the
     * pending alerts, then whatever the test pushes. */

    //0: accept the upgrade; another status refuses it with that status
    void refuseEvents(int status)
    {
        std::lock_guard<std::mutex> l(mutex);
        eventsRefusal = status;
    }

    //A muted stream sends nothing and answers no ping
    void muteEvents(bool m) { eventsMuted = m; }

    //Every stream is closed once its snapshot and alerts are sent
    void closeEventsAfterSnapshot(bool c) { closeAfterSnapshot = c; }

    //Answers the upgrade with these bytes instead of a valid 101 and the
    //snapshot; the connection stays open
    void rawHandshake(bool use, const std::string &bytes = std::string())
    {
        std::lock_guard<std::mutex> l(mutex);
        useRawHandshake = use;
        rawHandshakeBytes = bytes;
    }

    //Bytes written as they are on every open stream
    void sendRaw(const std::string &bytes)
    {
        for (auto &s: openStreams())
        {
            std::lock_guard<std::mutex> l(s->wmutex);
            if (s->open)
                ::send(s->fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        }
    }

    void pushEvent(const nlohmann::json &event)
    {
        for (auto &s: openStreams())
            sendFrame(*s, 0x1, event.dump());
    }

    //Persisted until acknowledged, like the daemon's alerts; extra is merged
    //into data (device_name and token_id of the approver alerts)
    void raiseAlert(const std::string &type, const std::string &id, const nlohmann::json &extra = nlohmann::json::object())
    {
        {
            std::lock_guard<std::mutex> l(mutex);
            alerts.push_back({ type, id });
            alertData[id] = extra;
        }
        for (auto &s: openStreams())
            sendAlert(*s, type, id);
    }

    //What GET admin/approvers answers
    void setApprovers(const nlohmann::json &list)
    {
        std::lock_guard<std::mutex> l(mutex);
        approvers = list;
    }

    //What GET admin/push/{codeId} answers, once
    void setPushPayload(const std::string &codeId, const std::string &token, const std::string &message)
    {
        std::lock_guard<std::mutex> l(mutex);
        pushPayloads[codeId] = { { "token", token }, { "message", message } };
    }

    /* What the daemon does at each ping (app/events.go): a pending alert goes
     * out again only on a stream it was never sent on. A copy on the same
     * stream, as after the subscribe race, is a pushEvent(alertEvent()). */
    void replayAlerts()
    {
        for (auto &s: openStreams())
            for (const auto &a: pendingAlerts())
                sendAlert(*s, a.first, a.second);
    }

    std::vector<std::pair<std::string, std::string>> pendingAlerts()
    {
        std::lock_guard<std::mutex> l(mutex);
        return alerts;
    }

    void pingEvents()
    {
        for (auto &s: openStreams())
            sendFrame(*s, 0x9, std::string());
    }

    //Cuts every stream, as a daemon restart does
    void dropEvents()
    {
        for (auto &s: openStreams())
        {
            std::lock_guard<std::mutex> l(s->wmutex);
            ::shutdown(s->fd, SHUT_RDWR);
        }
    }

    size_t eventsAccepted() { return accepted; }
    size_t openEvents() { return openStreams().size(); }
    size_t pingsReceived() { return pings; }
    size_t pongsReceived() { return pongs; }

    static nlohmann::json alertEvent(const std::string &type, const std::string &id, const nlohmann::json &extra = nlohmann::json::object())
    {
        nlohmann::json data = { { "id", id }, { "ts", "2026-10-22T09:14:00Z" } };
        data.update(extra);
        return { { "type", type }, { "data", data } };
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
    struct Stream
    {
        int fd = -1;
        std::atomic<bool> open{ true };
        std::mutex wmutex;
        std::set<std::string> sentAlerts;
    };

    void sendAlert(Stream &s, const std::string &type, const std::string &id)
    {
        {
            std::lock_guard<std::mutex> l(s.wmutex);
            if (!s.sentAlerts.insert(id).second)
                return;
        }
        nlohmann::json extra;
        {
            std::lock_guard<std::mutex> l(mutex);
            const auto it = alertData.find(id);
            extra = it == alertData.end() ? nlohmann::json::object() : it->second;
        }
        sendFrame(s, 0x1, alertEvent(type, id, extra).dump());
    }

    std::mutex streamsMutex;
    std::vector<std::shared_ptr<Stream>> streams;
    std::vector<std::thread> streamThreads;
    std::atomic<int> eventsRefusal{ 0 };
    std::atomic<bool> eventsMuted{ false };
    std::atomic<bool> closeAfterSnapshot{ false };
    std::atomic<size_t> accepted{ 0 };
    std::atomic<size_t> pings{ 0 };
    std::atomic<size_t> pongs{ 0 };
    nlohmann::json progress = leakyProgress();
    nlohmann::json job = leakyJob();
    std::vector<std::pair<std::string, std::string>> alerts;
    std::map<std::string, nlohmann::json> alertData;
    nlohmann::json approvers = nlohmann::json::array();
    std::map<std::string, nlohmann::json> pushPayloads;
    bool useRawHandshake = false;
    std::string rawHandshakeBytes;

    std::vector<std::shared_ptr<Stream>> openStreams()
    {
        std::lock_guard<std::mutex> l(streamsMutex);
        std::vector<std::shared_ptr<Stream>> out;
        for (auto &s: streams)
            if (s->open)
                out.push_back(s);
        return out;
    }

    //Server frames are never masked
    void sendFrame(Stream &s, int opcode, const std::string &payload)
    {
        if (eventsMuted && opcode != 0x8)
            return;
        std::string f;
        f.push_back((char)(0x80 | opcode));
        if (payload.size() < 126)
        {
            f.push_back((char)payload.size());
        }
        else if (payload.size() <= 0xffff)
        {
            f.push_back((char)126);
            f.push_back((char)((payload.size() >> 8) & 0xff));
            f.push_back((char)(payload.size() & 0xff));
        }
        else
        {
            f.push_back((char)127);
            for (int i = 7; i >= 0; i--)
                f.push_back((char)((payload.size() >> (8 * i)) & 0xff));
        }
        f += payload;
        std::lock_guard<std::mutex> l(s.wmutex);
        if (s.open)
            ::send(s.fd, f.data(), f.size(), MSG_NOSIGNAL);
    }

    static std::string base64(const unsigned char *data, size_t len)
    {
        static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (size_t i = 0; i < len; i += 3)
        {
            const unsigned v = (data[i] << 16) | ((i + 1 < len ? data[i + 1] : 0) << 8) | (i + 2 < len ? data[i + 2] : 0);
            out.push_back(t[(v >> 18) & 63]);
            out.push_back(t[(v >> 12) & 63]);
            out.push_back(i + 1 < len ? t[(v >> 6) & 63] : '=');
            out.push_back(i + 2 < len ? t[v & 63] : '=');
        }
        return out;
    }

    //Answers the upgrade; true when fd now belongs to a stream
    bool upgradeEvents(int fd, const Request &req)
    {
        Response refusal;
        {
            std::lock_guard<std::mutex> l(mutex);
            log.push_back(req);
            const auto auth = req.headers.find("authorization");
            if (oldDaemon)
            {
                refusal.status = 404;
                refusal.body = "404 page not found";
            }
            else if (eventsRefusal != 0)
            {
                refusal.status = eventsRefusal;
                refusal.body = err("refused");
            }
            else if (auth == req.headers.end() || auth->second != "Bearer " + bearer || req.path != "/api/v2/events")
            {
                refusal.status = 401;
                refusal.body = err("bad_bearer");
            }
        }

        if (refusal.status != 200)
        {
            reply(fd, refusal);
            return false;
        }

        bool raw = false;
        std::string rawBytes;
        {
            std::lock_guard<std::mutex> l(mutex);
            raw = useRawHandshake;
            rawBytes = rawHandshakeBytes;
        }
        if (raw)
        {
            if (!rawBytes.empty())
                ::send(fd, rawBytes.data(), rawBytes.size(), MSG_NOSIGNAL);
            auto stream = std::make_shared<Stream>();
            stream->fd = fd;
            std::lock_guard<std::mutex> l(streamsMutex);
            streams.push_back(stream);
            streamThreads.emplace_back([this, stream]()
                                       {
                runStream(stream);
            });
            return true;
        }

        const auto keyIt = req.headers.find("sec-websocket-key");
        const std::string in = (keyIt == req.headers.end() ? std::string() : keyIt->second) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int dlen = 0;
        EVP_Digest(in.data(), in.size(), digest, &dlen, EVP_sha1(), nullptr);
        const std::string head = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                 "Sec-WebSocket-Accept: " +
                                 base64(digest, dlen) + "\r\n\r\n";
        ::send(fd, head.data(), head.size(), MSG_NOSIGNAL);

        auto stream = std::make_shared<Stream>();
        stream->fd = fd;

        nlohmann::json snapshot;
        std::vector<std::pair<std::string, std::string>> pending;
        {
            std::lock_guard<std::mutex> l(mutex);
            snapshot = { { "type", "upgrade_progress" }, { "data", progress } };
            pending = alerts;
        }
        sendFrame(*stream, 0x1, snapshot.dump());
        for (const auto &a: pending)
            sendAlert(*stream, a.first, a.second);
        if (closeAfterSnapshot)
            ::shutdown(fd, SHUT_RDWR);

        std::lock_guard<std::mutex> l(streamsMutex);
        streams.push_back(stream);
        streamThreads.emplace_back([this, stream]()
                                   {
            runStream(stream);
        });
        accepted++;
        return true;
    }

    void runStream(std::shared_ptr<Stream> s)
    {
        std::string rx;
        while (!stopping && s->open)
        {
            struct pollfd p = { s->fd, POLLIN, 0 };
            if (poll(&p, 1, 20) <= 0)
                continue;
            char buf[4096];
            const ssize_t n = recv(s->fd, buf, sizeof(buf), 0);
            if (n <= 0)
                break;
            rx.append(buf, (size_t)n);

            while (rx.size() >= 6)
            {
                const int opcode = (unsigned char)rx[0] & 0x0f;
                size_t len = (unsigned char)rx[1] & 0x7f;
                size_t header = 2;
                if (len == 126)
                {
                    if (rx.size() < 8)
                        break;
                    len = ((unsigned char)rx[2] << 8) | (unsigned char)rx[3];
                    header = 4;
                }
                if (rx.size() < header + 4 + len)
                    break;
                std::string payload = rx.substr(header + 4, len);
                for (size_t i = 0; i < len; i++)
                    payload[i] = (char)(payload[i] ^ rx[header + (i % 4)]);
                rx.erase(0, header + 4 + len);

                if (opcode == 0x9)
                {
                    pings++;
                    sendFrame(*s, 0xA, payload);
                }
                else if (opcode == 0xA)
                {
                    pongs++;
                }
                else if (opcode == 0x8)
                {
                    s->open = false;
                }
            }
        }

        std::lock_guard<std::mutex> l(s->wmutex);
        s->open = false;
        ::close(s->fd);
    }

    static void reply(int fd, const Response &res)
    {
        std::string out = "HTTP/1.1 " + std::to_string(res.status) + " X\r\n";
        out += "Content-Type: application/json\r\n";
        out += "Content-Length: " + std::to_string(res.body.size()) + "\r\n";
        if (!res.location.empty())
            out += "Location: " + res.location + "\r\n";
        out += "Connection: close\r\n\r\n" + res.body;
        send(fd, out.data(), out.size(), MSG_NOSIGNAL);
    }

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
            if (!serve(fd))
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

    //True when fd was handed to an events stream
    bool serve(int fd)
    {
        Request req;
        if (!readRequest(fd, req))
            return false;

        if (req.path.compare(0, 14, "/api/v2/events") == 0)
            return upgradeEvents(fd, req);

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

        reply(fd, res);
        return false;
    }

    //The session-only approver and code routes; the code is always right
    Response approverRoute(const Request &req, const nlohmann::json &body, bool hasSession)
    {
        Response res;
        auto field = [&body](const char *k)
        {
            return body.is_object() && body.contains(k) && body[k].is_string() ? body[k].get<std::string>() : std::string();
        };

        if (!hasSession)
        {
            res.status = 401;
            res.body = err("bad_session");
        }
        else if (req.method == "GET" && req.path == "/api/v2/admin/approvers")
            res.body = ok(approvers);
        else if (req.method == "POST" && req.path == "/api/v2/admin/approvers/enroll")
            res.body = ok({ { "approver_id", "a000000000000002" }, { "code_id", "c000000000000001" }, { "expires_in", 60 } });
        else if (req.method == "POST" && req.path == "/api/v2/admin/approvers/confirm")
            res.body = ok({ { "id", "a000000000000002" }, { "device_name", "Pixel 8" }, { "token_id", "9a41f0c2d3e4b5a6" }, { "created_at", "2026-10-23T09:12:03Z" } });
        else if (req.method == "POST" && req.path == "/api/v2/admin/approvers/remove")
            res.body = ok({ { "approver_id", field("approver_id") } });
        else if (req.method == "POST" && req.path == "/api/v2/admin/codes")
            res.body = ok({ { "code_id", "c000000000000002" }, { "approver_id", "a000000000000001" }, { "expires_in", 60 } });
        else if (req.method == "POST" && req.path == "/api/v2/admin/codes/verify")
            res.body = ok({ { "action", "console_open" }, { "expires_in", 30 } });
        else
        {
            res.status = 404;
            res.body = err("not_found");
        }
        return res;
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
        else if (req.method == "GET" && req.path == "/api/v2/update/status")
        {
            res.body = ok({ { "progress", progress }, { "job", job } });
        }
        else if (req.method == "GET" && (req.path == "/api/v2/update/check" || req.path == "/api/v2/update/available"))
        {
            res.body = ok({ { "calaos-server", { { "name", "calaos-server" }, { "version", "4.6.0" }, { "current_version", "4.5.0" } } } });
        }
        else if (req.method == "POST" && (req.path == "/api/v2/update/upgrade-all" || req.path == "/api/v2/update/upgrade"))
        {
            const std::string pkg = body.is_object() && body.contains("package") && body["package"].is_string() ? body["package"].get<std::string>() : std::string();
            if (req.path == "/api/v2/update/upgrade" && pkg == "calaos-held")
            {
                res.status = 409;
                res.body = nlohmann::json{ { "error", true }, { "code", "package_held" }, { "msg", "the package is held (apt-mark hold)" } }.dump();
            }
            else if (req.path == "/api/v2/update/upgrade" && pkg != "calaos-server")
            {
                res.status = 404;
                res.body = err("package_not_found");
            }
            else
            {
                nlohmann::json j = { { "id", "3f9c2a7e5b1d4c80" }, { "kind", pkg.empty() ? "upgrade_all" : "upgrade" }, { "status", "running" }, { "reboot_required", false }, { "daemon_restart", false }, { "started_at", "2026-10-22T09:12:03Z" } };
                if (!pkg.empty())
                    j["package"] = pkg;
                res.status = 202;
                res.body = ok(j);
            }
        }
        else if (req.method == "POST" && (req.path == "/api/v2/system/reboot" || req.path == "/api/v2/system/restart"))
        {
            res.body = ok(nullptr);
        }
        else if (req.method == "GET" && req.path == "/api/v2/system/info")
        {
            res.body = ok({ { "hostname", "calaos" }, { "uptime", 1234 }, { "cpu_usage", 3 }, { "mem_usage", 41 } });
        }
        else if (req.path.compare(0, 23, "/api/v2/admin/approvers") == 0 || req.path.compare(0, 19, "/api/v2/admin/codes") == 0)
        {
            res = approverRoute(req, body, hasSession);
        }
        else if (req.method == "GET" && req.path.compare(0, 19, "/api/v2/admin/push/") == 0)
        {
            const auto it = pushPayloads.find(req.path.substr(19));
            if (hasSession)
            {
                res.status = 403;
                res.body = err("not_allowed");
            }
            else if (it == pushPayloads.end())
            {
                res.status = 404;
                res.body = err("not_found");
            }
            else
            {
                res.body = ok(it->second);
                pushPayloads.erase(it);
            }
        }
        else if (req.method == "POST" && req.path == "/api/v2/admin/alerts/ack")
        {
            int acked = 0;
            if (body.is_object() && body.contains("ids") && body["ids"].is_array())
            {
                for (const auto &id: body["ids"])
                {
                    for (auto it = alerts.begin(); it != alerts.end();)
                    {
                        if (id.is_string() && it->second == id.get<std::string>())
                        {
                            it = alerts.erase(it);
                            acked++;
                        }
                        else
                            ++it;
                    }
                }
            }
            res.body = ok({ { "acked", acked } });
        }
        else
        {
            res.status = 404;
            res.body = err("not_found");
        }

        return res;
    }
};
