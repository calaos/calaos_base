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
 * A fake console socket of calaos-container (docs/API.md, Console): a Unix
 * stream socket answering the WebSocket upgrade of GET /ws with subprotocol
 * calaos-console.v1, on its own thread. What the tunnel writes is recorded;
 * what the test sends goes through a queue the thread drains, so a tunnel that
 * stops reading never blocks the test thread, which pumps the server's loop.
 */

#include <openssl/evp.h>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class FakeConsole
{
public:
    enum class Mode
    {
        Upgrade,    //101 with the subprotocol
        NoProtocol, //101 without Sec-WebSocket-Protocol
        Refuse,     //404, as for a second client
        Silent,     //reads the upgrade, never answers
        Garbage,    //an answer head that never ends
        HangUp,     //closes once the upgrade is read
    };

    struct Frame
    {
        int opcode = 0;
        std::string payload;
        bool fin = true;
        bool masked = false;
    };

    ~FakeConsole() { stop(); }

    bool start(const std::string &socketPath, Mode m = Mode::Upgrade)
    {
        stop();
        path = socketPath;
        mode = m;
        ::unlink(path.c_str());
        listenFd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        struct sockaddr_un a;
        memset(&a, 0, sizeof(a));
        a.sun_family = AF_UNIX;
        strncpy(a.sun_path, path.c_str(), sizeof(a.sun_path) - 1);
        if (listenFd < 0 || bind(listenFd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(listenFd, 4) != 0)
            return false;
        stopping = false;
        worker = std::thread([this]()
                             {
            run();
        });
        return true;
    }

    void stop()
    {
        stopping = true;
        if (worker.joinable())
            worker.join();
        if (listenFd >= 0)
            close(listenFd);
        listenFd = -1;
        if (!path.empty())
            ::unlink(path.c_str());
        std::lock_guard<std::mutex> l(mutex);
        received.clear();
        head.clear();
        out.clear();
        outOff = 0;
        inputBytes = 0;
        accepted = 0;
        upgraded = false;
        eof = false;
        paused = false;
    }

    //A server frame, never masked unless asked
    void send(int opcode, const std::string &payload, bool fin = true, bool masked = false)
    {
        std::string f;
        f.push_back((char)((fin ? 0x80 : 0) | opcode));
        const unsigned char m = masked ? 0x80 : 0;
        if (payload.size() < 126)
        {
            f.push_back((char)(m | payload.size()));
        }
        else if (payload.size() <= 0xffff)
        {
            f.push_back((char)(m | 126));
            f.push_back((char)((payload.size() >> 8) & 0xff));
            f.push_back((char)(payload.size() & 0xff));
        }
        else
        {
            f.push_back((char)(m | 127));
            for (int i = 7; i >= 0; i--)
                f.push_back((char)((payload.size() >> (8 * i)) & 0xff));
        }
        static const unsigned char key[4] = { 1, 2, 3, 4 };
        if (masked)
            f.append((const char *)key, 4);
        for (size_t i = 0; i < payload.size(); i++)
            f.push_back(masked ? (char)(payload[i] ^ key[i % 4]) : payload[i]);
        sendRaw(f);
    }

    void sendClose(uint16_t code, const std::string &reason = "exited")
    {
        std::string p;
        p.push_back((char)(code >> 8));
        p.push_back((char)(code & 0xff));
        p += reason;
        send(0x8, p);
    }

    void sendRaw(const std::string &bytes)
    {
        std::lock_guard<std::mutex> l(mutex);
        out += bytes;
    }

    //Stops reading the tunnel, so that its writes queue up
    void pause(bool p) { paused = p; }

    //Closes the connection without a close frame
    void hangUp() { hangUpNow = true; }

    std::vector<Frame> frames()
    {
        std::lock_guard<std::mutex> l(mutex);
        return received;
    }

    //The payloads of the binary frames, in order
    std::string input()
    {
        std::lock_guard<std::mutex> l(mutex);
        std::string s;
        for (const Frame &f: received)
            if (f.opcode == 0x2)
                s += f.payload;
        return s;
    }

    std::string request()
    {
        std::lock_guard<std::mutex> l(mutex);
        return head;
    }

    size_t pendingOut()
    {
        std::lock_guard<std::mutex> l(mutex);
        return out.size() - outOff;
    }

    size_t inputSize() { return inputBytes; }

    size_t connections() { return accepted; }
    bool isUpgraded() { return upgraded; }
    //The tunnel closed its side
    bool sawEof() { return eof; }

private:
    std::string path;
    Mode mode = Mode::Upgrade;
    int listenFd = -1;
    std::thread worker;
    std::atomic<bool> stopping{ false };
    std::atomic<bool> paused{ false };
    std::atomic<bool> hangUpNow{ false };
    std::atomic<size_t> accepted{ 0 };
    std::atomic<bool> upgraded{ false };
    std::atomic<bool> eof{ false };
    std::mutex mutex;
    std::vector<Frame> received;
    std::string head;
    std::string out;
    size_t outOff = 0;
    std::atomic<size_t> inputBytes{ 0 };

    static std::string base64(const unsigned char *data, size_t len)
    {
        static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string s;
        for (size_t i = 0; i < len; i += 3)
        {
            const unsigned v = (data[i] << 16) | ((i + 1 < len ? data[i + 1] : 0) << 8) | (i + 2 < len ? data[i + 2] : 0);
            s.push_back(t[(v >> 18) & 63]);
            s.push_back(t[(v >> 12) & 63]);
            s.push_back(i + 1 < len ? t[(v >> 6) & 63] : '=');
            s.push_back(i + 2 < len ? t[v & 63] : '=');
        }
        return s;
    }

    static std::string accept(const std::string &key)
    {
        const std::string in = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int dlen = 0;
        EVP_Digest(in.data(), in.size(), digest, &dlen, EVP_sha1(), nullptr);
        return base64(digest, dlen);
    }

    std::string answer(const std::string &req)
    {
        std::string key;
        const size_t k = req.find("Sec-WebSocket-Key: ");
        if (k != std::string::npos)
            key = req.substr(k + 19, req.find("\r\n", k) - k - 19);
        const std::string ok = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                               "Sec-WebSocket-Accept: " +
                               accept(key) + "\r\n";
        switch (mode)
        {
        case Mode::Upgrade: return ok + "Sec-WebSocket-Protocol: calaos-console.v1\r\n\r\n";
        case Mode::NoProtocol: return ok + "\r\n";
        case Mode::Refuse: return "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        case Mode::Garbage: return "HTTP/1.1 101 Switching Protocols\r\n" + std::string(20000, 'x');
        default: return std::string();
        }
    }

    //Masked client frames out of rx, recorded
    void parse(std::string &rx)
    {
        while (rx.size() >= 2)
        {
            const bool fin = (unsigned char)rx[0] & 0x80;
            const int opcode = (unsigned char)rx[0] & 0x0f;
            const bool masked = (unsigned char)rx[1] & 0x80;
            size_t len = (unsigned char)rx[1] & 0x7f;
            size_t header = 2;
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
            const size_t maskLen = masked ? 4 : 0;
            if (rx.size() < header + maskLen + len)
                return;
            Frame f;
            f.opcode = opcode;
            f.fin = fin;
            f.masked = masked;
            f.payload = rx.substr(header + maskLen, len);
            if (masked)
                for (size_t i = 0; i < len; i++)
                    f.payload[i] = (char)(f.payload[i] ^ rx[header + (i % 4)]);
            rx.erase(0, header + maskLen + len);
            if (opcode == 0x2)
                inputBytes += f.payload.size();
            std::lock_guard<std::mutex> l(mutex);
            received.push_back(f);
        }
    }

    void serve(int fd)
    {
        accepted++;
        std::string rx;
        bool haveHead = false;
        while (!stopping)
        {
            if (hangUpNow)
            {
                hangUpNow = false;
                break;
            }

            bool wantOut;
            {
                std::lock_guard<std::mutex> l(mutex);
                wantOut = upgraded && outOff < out.size();
            }
            struct pollfd p = { fd, (short)((paused ? 0 : POLLIN) | (wantOut ? POLLOUT : 0)), 0 };
            if (poll(&p, 1, 5) < 0)
                break;

            if (p.revents & POLLOUT)
            {
                std::lock_guard<std::mutex> l(mutex);
                const ssize_t n = ::send(fd, out.data() + outOff, out.size() - outOff, MSG_DONTWAIT | MSG_NOSIGNAL);
                if (n > 0)
                    outOff += (size_t)n;
                if (outOff > 1024 * 1024 || outOff == out.size())
                {
                    out.erase(0, outOff);
                    outOff = 0;
                }
            }

            if (paused || !(p.revents & (POLLIN | POLLHUP)))
                continue;

            char buf[65536];
            const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
            if (n == 0)
            {
                eof = true;
                break;
            }
            if (n < 0)
                continue;
            rx.append(buf, (size_t)n);

            if (!haveHead)
            {
                const size_t end = rx.find("\r\n\r\n");
                if (end == std::string::npos)
                    continue;
                haveHead = true;
                const std::string req = rx.substr(0, end + 4);
                {
                    std::lock_guard<std::mutex> l(mutex);
                    head = req;
                }
                rx.erase(0, end + 4);
                if (mode == Mode::HangUp)
                    break;
                const std::string a = answer(req);
                ::send(fd, a.data(), a.size(), MSG_NOSIGNAL);
                upgraded = mode == Mode::Upgrade || mode == Mode::NoProtocol;
            }
            parse(rx);
        }
        close(fd);
    }

    void run()
    {
        while (!stopping)
        {
            struct pollfd p = { listenFd, POLLIN, 0 };
            if (poll(&p, 1, 5) <= 0)
                continue;
            const int fd = accept4(listenFd, nullptr, nullptr, SOCK_CLOEXEC);
            if (fd >= 0)
                serve(fd);
        }
    }
};
