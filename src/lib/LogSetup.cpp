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

/* T3.14 - immortal statics.
 * ==========================
 *
 * Everything below is reachable from the destructor of ANOTHER static object:
 * any class that logs from its destructor runs somewhere in the atexit chain,
 * and nothing orders it after the statics of this translation unit. In
 * calaos_server the chain WagoMapManager -> WagoMap -> ~ExternProcServer (a
 * cDebugDom) does exactly that, and AddressSanitizer caught it as a
 * heap-use-after-free (READ 8) in _M_find_before_node() under
 * Utils::calaosLogger(): the logger table had already been destroyed. It is
 * only ever masked in production because main() calls freeLoggers() before
 * returning; every other path to exit() is undefined behaviour.
 *
 * The tables are therefore allocated once on the heap and never destroyed, so
 * they stay valid for the whole atexit chain. This is a lifetime extension,
 * not a leak: the pointers live in static storage, which LeakSanitizer scans
 * as a root set, so the blocks are still reachable at the leak check and are
 * not reported. It also removes the pre-existing "32 byte leak in
 * Utils::calaosLogger()" reports, which happened precisely because
 * ~logger_hash dropped the last reference to the Logger objects before LSan
 * looked at the heap.
 *
 * The same idiom is applied to the level cache in Logger.cpp and to the config
 * and cache paths in ConfigStore.cpp, which are the other members of this bug
 * family.
 */

static bool calaosLogShuttingDown = false;

static string &defaultDomain()
{
    static string *d = new string();
    return *d;
}

static std::unordered_map<std::string, Logger *> &loggerHash()
{
    static std::unordered_map<std::string, Logger *> *h = new std::unordered_map<std::string, Logger *>();
    return *h;
}

//Fallback logger handed out once freeLoggers() has run. Immortal for the very
//same reason: callers log through the pointer they got from calaosLogger().
static Logger &defaultCoutLogger()
{
    static Logger *l = new Logger();
    return *l;
}

Logger *Utils::calaosLogger(const char *domain)
{
    if (calaosLogShuttingDown)
        return &defaultCoutLogger();

    string d = defaultDomain();

    if (domain)
      d = domain;

    Logger *logger = nullptr;
    auto &hash = loggerHash();
    auto it = hash.find(d);
    if (it == hash.end())
    {
        logger = new Logger(d);
        hash[d] = logger;
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

    defaultDomain() = d;
    loggerHash()[defaultDomain()] = new Logger(defaultDomain());
}

void Utils::freeLoggers()
{
    auto &hash = loggerHash();

    for (auto &kv: hash)
    {
        delete kv.second;
    }
    hash.clear();

    calaosLogShuttingDown = true;
}
