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

/* T3.14 - repro program for "objects used after their static destructor ran".
 *
 * Driven by StaticLogShutdown_test, never run directly by the test suite.
 *
 * It is the smallest thing that reproduces the whole family of bugs: a
 * translation unit static OF THE PROGRAM whose destructor logs, linked against
 * libcalaos_common. libcalaos_common is a libtool convenience library, so its
 * objects come after this one on the link line, its statics are constructed
 * after this file's and therefore destroyed BEFORE it. When ~ShutdownLogger
 * runs, everything the logger needs has already been destroyed:
 *
 *  - the level cache of Logger.cpp (a function local static built during
 *    main(), hence the very first thing destroyed) looked empty again, so the
 *    whole initialisation block re-ran from the atexit chain and read
 *    local_config.xml through a config path static that was itself already
 *    destroyed. That printed "Parse error ... In file <binary garbage>/
 *    local_config.xml" and locked an already destroyed std::mutex;
 *  - the logger table of LogSetup.cpp had been destroyed too, so
 *    Utils::calaosLogger() looked a domain up in a dead unordered_map. That is
 *    the heap-use-after-free (READ 8) AddressSanitizer reported in
 *    _M_find_before_node().
 *
 * main() deliberately does NOT call Utils::freeLoggers(): that call in
 * calaos_server's main() is the only reason the second bug stayed invisible in
 * production, and every path reaching exit() without it was undefined
 * behaviour. Nothing here may hide it either.
 *
 * With the statics made immortal, the destructor below logs normally and the
 * program prints its two markers and nothing else.
 */

#include "LogSetup.h"

namespace
{

struct ShutdownLogger
{
    ~ShutdownLogger()
    {
        cInfoDom("t3.14") << "T3_14_SHUTDOWN_MARKER";
    }
};

//Constructed before libcalaos_common's own statics, destroyed after them
ShutdownLogger shutdownLogger;

}

int main()
{
    Utils::initLogger("t3.14");

    //Makes the logger build its level cache and resolve the config path while
    //everything is still alive, which is what puts the destruction order of
    //the atexit chain in the state the bug needs.
    cInfoDom("t3.14") << "T3_14_MAIN_MARKER";

    //No Utils::freeLoggers() here, on purpose. See the comment above.
    return 0;
}
