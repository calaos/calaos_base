/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
#include "Logger.h"
#include "Utils.h"

static bool logger_isTerm_check()
{
    const char *term = getenv("TERM");
    if (!term) return false;
    string sterm(term);
    if (sterm == "cygwin" ||
        sterm == "linux" ||
        sterm == "rxvt-unicode-256color" ||
        sterm == "rxvt-unicode" ||
        sterm == "rxvt" ||
        sterm == "screen" ||
        sterm == "tmux-256color" ||
        sterm == "xterm" ||
        sterm == "xterm-256color" ||
        sterm == "xterm-termite" ||
        sterm == "xterm-color" ||
        sterm == "Eterm" ||
        sterm == "kterm" ||
        sterm == "aterm" ||
        sterm == "gnome" ||
        sterm == "interix")
    {
        int fdout = STDOUT_FILENO;
        if ((fdout >= 0) && (!::isatty(fdout)))
            return false;
        return true;
    }

    return false;
}

static bool logger_isTerm = logger_isTerm_check();

bool LogStream::isTerminal()
{
    return logger_isTerm;
}

/* Log levels can be configured from calaos_config using:
 * calaos_config set debug_level 5
 * Levels are:
 *      LOG_LEVEL_UNKNOWN   = 0
 *      LOG_LEVEL_CRITICAL  = 1
 *      LOG_LEVEL_ERROR     = 2
 *      LOG_LEVEL_WARNING   = 3
 *      LOG_LEVEL_INFO      = 4
 *      LOG_LEVEL_DEBUG     = 5
 */

static int maxLevelPrintable()
{
    static int max_level_print = -1;

    if (max_level_print > -1)
        return max_level_print;

    string lvl = Utils::get_config_option("debug_level");
    if (Utils::is_of_type<int>(lvl))
        Utils::from_string(lvl, max_level_print);

    if (max_level_print < 0 ||
        max_level_print > Logger::LOG_LEVEL_DEBUG)
        max_level_print = Logger::LOG_LEVEL_INFO;

    return max_level_print;
}

LogStream::~LogStream()
{
    if (logData->level > maxLevelPrintable())
        return;

    //Strip away file path and only keep filename
    logData->file = FileUtils::filename(logData->file);

    switch (logData->level)
    {
    case Logger::LOG_LEVEL_CRITICAL: std::cout << colorPurple() << "[CRI]";
        break;
    case Logger::LOG_LEVEL_DEBUG: std::cout << colorBlue() << "[DBG]";
        break;
    case Logger::LOG_LEVEL_ERROR: std::cout << colorRed() << "[ERR]";
        break;
    case Logger::LOG_LEVEL_INFO: std::cout << colorGreen() << "[INF]";
        break;
    case Logger::LOG_LEVEL_WARNING: std::cout << colorYellow() << "[WRN]";
        break;
    default:
    case Logger::LOG_LEVEL_UNKNOWN: std::cout << colorRed() << colorBold() << "[???]";
        break;
    }

    std::cout << colorCyan() << " " << logData->domain << colorReset() << " ";
    std::cout << colorBold() << "(" << logData->file << ":" << logData->line << ")" << colorReset() << " ";
    std::cout << logData->stream.str() << colorReset() << std::endl;
}
