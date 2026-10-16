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

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "HistLogger.h"
#include "libuvw.h"

namespace PushTest
{

inline bool pumpUntil(const std::function<bool()> &done)
{
    auto loop = uvw::Loop::getDefault();
    for (int i = 0; i < 400; i++)
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
    pumpUntil([a]() { return a->received; });
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

} //namespace PushTest
