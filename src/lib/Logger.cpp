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

#include <cerrno>

/* The variable is set by whoever knows stdout ends in journald. Guessing it from
 * the environment is not possible: JOURNAL_STREAM does not cross podman, and a
 * prefix written anywhere else is noise in the text.
 */
static bool logger_journal_check()
{
    const char *journal = getenv("CALAOS_JOURNAL_PRIORITY");
    return journal && strcmp(journal, "1") == 0 && !::isatty(STDOUT_FILENO);
}

static bool logger_journal = logger_journal_check();

static bool logger_isTerm_check()
{
    if (logger_journal)
        return false;

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

/* T3.14 - immortal static, see the long comment in LogSetup.cpp.
 *
 * This cache used to be a plain function local static. Built during main(), it
 * was therefore the FIRST thing destroyed in the atexit chain; ~_Hashtable
 * resets the element count, so the empty() test below became true again and
 * the whole initialisation block re-ran from a static destructor. It called
 * Utils::get_config_option() on a config path whose own static had already
 * been destroyed, and calaos_server printed a parse error on a corrupted path
 * at every single shutdown. Never destroying the cache removes the re-entry.
 */
static std::unordered_map<std::string, int> &loggerDomains()
{
    static std::unordered_map<std::string, int> *d = new std::unordered_map<std::string, int>();
    return *d;
}

static int maxLevelPrintable(string domain)
{
    std::unordered_map<std::string, int> &logger_domains = loggerDomains();

    if (logger_domains.empty())
    {
        int logger_default_level = Logger::LOG_LEVEL_INFO;
        string lvl = Utils::get_config_option("debug_level", true);
        if (Utils::is_of_type<int>(lvl))
            Utils::from_string(lvl, logger_default_level);

        if (logger_default_level < Logger::LOG_LEVEL_UNKNOWN ||
            logger_default_level > Logger::LOG_LEVEL_DEBUG)
            logger_default_level = Logger::LOG_LEVEL_INFO;

        //default logger level for all non-specified domains
        logger_domains["default"] = logger_default_level;

        string domains = Utils::get_config_option("debug_domains", true);
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
                cout << "Logger: Domain: " << d.first << " Level: " << d.second << endl;
        }
    }

    auto it = logger_domains.find(domain);
    if (it != logger_domains.end())
        return it->second;

    return logger_domains["default"];
}

int Logger::maxLevel() const
{
    return maxLevelPrintable(domainName);
}

static int journalPriority(int level)
{
    switch (level)
    {
    case Logger::LOG_LEVEL_CRITICAL: return 2;
    case Logger::LOG_LEVEL_ERROR: return 3;
    case Logger::LOG_LEVEL_WARNING: return 4;
    case Logger::LOG_LEVEL_DEBUG: return 7;
    default: return 6;
    }
}

LogStream::~LogStream()
{
    if (logData->level > maxLevelPrintable(logData->domain))
        return;

    //Strip away file path and only keep filename
    logData->file = FileUtils::filename(logData->file);

    std::ostringstream s;

    //journald reads the prefix line by line: a line without one falls to info
    string prefix;
    string message = logData->stream.str();
    if (logger_journal)
    {
        const int priority = logData->journalPriority >= 0 ? logData->journalPriority
                                                           : journalPriority(logData->level);
        prefix = "<" + std::to_string(priority) + ">";
        Utils::replace_str(message, "\n", "\n" + prefix);
    }
    s << prefix;

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
    s << message << colorReset() << std::endl;

    //A failed write would otherwise silence every log that follows, and its
    //errno would replace the one a caller is about to report
    const int savedErrno = errno;
    std::cout << s.str();
    std::cout.flush();
    std::cout.clear();
    errno = savedErrno;
}

bool Logger::isColorEnabled()
{
    return logger_isTerm;
}

bool Logger::isJournalPriorityEnabled()
{
    return logger_journal;
}

int Logger::takeJournalPriority(string &line)
{
    if (line.size() < 3 || line[0] != '<' || line[1] < '0' || line[1] > '7' || line[2] != '>')
        return -1;

    const int priority = line[1] - '0';
    line.erase(0, 3);
    return priority;
}
