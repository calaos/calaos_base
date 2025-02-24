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
#include "Logger.h"
#include "Utils.h"

static bool logger_isTerm_check()
{
    const char *forceColor = getenv("CALAOS_FORCE_COLOR");
    if (forceColor && strcmp(forceColor, "1") == 0)
        return true;

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
        sterm == "interix" ||
        sterm == "screen.xterm-256color")
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
 *
 * To configure log levels per domain, use:
 * calaos_config set debug_domains hifirose:5,network:0
 *
 * To set default log level for all non-specified domains, use:
 * calaos_config set debug_level 3
 *
 */

static int maxLevelPrintable(string domain)
{
    static std::unordered_map<std::string, int> logger_domains;

    if (logger_domains.empty())
    {
        int logger_default_level = Logger::LOG_LEVEL_INFO;
        string lvl = Utils::get_config_option("debug_level");
        if (Utils::is_of_type<int>(lvl))
            Utils::from_string(lvl, logger_default_level);

        if (logger_default_level < Logger::LOG_LEVEL_UNKNOWN ||
            logger_default_level > Logger::LOG_LEVEL_DEBUG)
            logger_default_level = Logger::LOG_LEVEL_INFO;

        //default logger level for all non-specified domains
        logger_domains["default"] = logger_default_level;

        string domains = Utils::get_config_option("debug_domains");
        if (!domains.empty())
        {
            std::vector<std::string> parts;
            Utils::split(domains, parts, ",");

            for (const auto &d: parts)
            {
                std::vector<std::string> dp;
                Utils::split(d, dp, ":");

                if (dp.size() == 2)
                {
                    int l;
                    if (Utils::is_of_type<int>(dp[1]))
                    {
                        Utils::from_string(dp[1], l);
                        if (l >= Logger::LOG_LEVEL_UNKNOWN && l <= Logger::LOG_LEVEL_DEBUG)
                            logger_domains[dp[0]] = l;
                    }
                }
            }
        }

        if (logger_default_level == Logger::LOG_LEVEL_DEBUG)
        {
            for (const auto &d: logger_domains)
                cDebug() << "Logger: Domain: " << d.first << " Level: " << d.second;
        }
    }

    auto it = logger_domains.find(domain);
    if (it != logger_domains.end())
        return it->second;

    return logger_domains["default"];
}

LogStream::~LogStream()
{
    if (logData->level > maxLevelPrintable(logData->domain))
        return;

    //Strip away file path and only keep filename
    logData->file = FileUtils::filename(logData->file);

    std::ostringstream s;

    switch (logData->level)
    {
    case Logger::LOG_LEVEL_CRITICAL: s << colorPurple() << "[CRI]";
        break;
    case Logger::LOG_LEVEL_DEBUG: s << colorBlue() << "[DBG]";
        break;
    case Logger::LOG_LEVEL_ERROR: s << colorRed() << "[ERR]";
        break;
    case Logger::LOG_LEVEL_INFO: s << colorGreen() << "[INF]";
        break;
    case Logger::LOG_LEVEL_WARNING: s << colorYellow() << "[WRN]";
        break;
    default:
    case Logger::LOG_LEVEL_UNKNOWN: s << colorRed() << colorBold() << "[???]";
        break;
    }

    s << colorCyan() << " " << logData->domain << colorReset() << " ";
    s << colorBold() << "(" << logData->file << ":" << logData->line << ")" << colorReset() << " ";
    s << logData->stream.str() << colorReset() << std::endl;

    std::cout << s.str();
    std::cout.flush();
}

bool Logger::isColorEnabled()
{
    return logger_isTerm;
}
