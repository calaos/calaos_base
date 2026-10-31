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

/* Driven by LogJournalPriority_test: Logger decides once, at static
 * initialisation, from the environment and from what stdout is, so each
 * configuration needs a process of its own.
 */

#include "LogSetup.h"
#include "Logger.h"

int main(int argc, char **argv)
{
    const string mode = argc > 1 ? argv[1] : "";
    const Logger log("prio");

    if (mode == "levels")
    {
        log.LogCritical("f.cpp", "fn", 1) << "critical";
        log.LogError("f.cpp", "fn", 2) << "error";
        log.LogWarning("f.cpp", "fn", 3) << "warning";
        log.LogInfo("f.cpp", "fn", 4) << "info";
        log.LogDebug("f.cpp", "fn", 5) << "debug";
        LogStream("prio", "f.cpp", "fn", 6, Logger::LOG_LEVEL_UNKNOWN) << "unknown";
    }
    else if (mode == "multiline")
    {
        log.LogError("f.cpp", "fn", 1) << "first\nsecond\nthird";
        log.LogWarning("f.cpp", "fn", 2) << "ends with a newline\n";
    }
    else if (mode == "override")
    {
        log.LogDebug("f.cpp", "fn", 1).withJournalPriority(3) << "from a sidecar";
        log.LogDebug("f.cpp", "fn", 2).withJournalPriority(-1) << "no priority given";
    }
    else if (mode == "state")
    {
        cout << "journal=" << Logger::isJournalPriorityEnabled()
             << " color=" << Logger::isColorEnabled() << endl;
    }
    else
        return 2;

    return 0;
}
