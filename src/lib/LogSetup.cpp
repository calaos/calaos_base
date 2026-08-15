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
#include "LogSetup.h"

#include <string>
#include <unordered_map>

using namespace std;

static bool calaosLogShuttingDown = false;
static string default_domain;
static std::unordered_map<std::string, Logger *> logger_hash;
static Logger defaultCoutLogger;

Logger *Utils::calaosLogger(const char *domain)
{
    if (calaosLogShuttingDown)
        return &defaultCoutLogger;

    string d = default_domain;

    if (domain)
      d = domain;

    Logger *logger = nullptr;
    auto it = logger_hash.find(d);
    if (it == logger_hash.end())
    {
        logger = new Logger(d);
        logger_hash[d] = logger;
    }
    else
        logger = it->second;

    return logger;
}

void Utils::initLogger(const char *d)
{
    //We are actually shutting down everything, do not allocate memory
    if (calaosLogShuttingDown)
        return;

    default_domain = d;
    logger_hash[default_domain] = new Logger(default_domain);
}

void Utils::freeLoggers()
{
    for (auto &kv: logger_hash)
    {
        delete kv.second;
    }
    logger_hash.clear();

    calaosLogShuttingDown = true;
}
