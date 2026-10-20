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
#ifndef S_McpLogLevel_H
#define S_McpLogLevel_H

#include <string>

namespace Calaos
{

/* Level of a line printed by the calaos_mcp sidecar.
 *
 * Python logging writes every level to stderr, so the stream a line arrived on
 * says nothing about its severity: the level is read from the line itself
 * ("INFO calaos_mcp.server: ..."). Header-inline and free of uvw so tests link
 * nothing of the server binary.
 */
namespace McpLogLevel
{

enum class Level { Debug, Info, Warning, Error };

//Parses the leading Python level name; CRITICAL folds into Error.
inline bool parse(const std::string &line, Level &out)
{
    static const struct { const char *name; Level level; } known[] = {
        { "CRITICAL", Level::Error },
        { "ERROR", Level::Error },
        { "WARNING", Level::Warning },
        { "INFO", Level::Info },
        { "DEBUG", Level::Debug },
    };

    for (const auto &k : known)
    {
        const std::string name = k.name;
        if (line.compare(0, name.size(), name) != 0 || line.size() == name.size())
            continue;
        const char next = line[name.size()];
        if (next == ' ' || next == ':')
        {
            out = k.level;
            return true;
        }
    }
    return false;
}

/* Lines with no level (traceback frames, a bare print) inherit that of the
 * previous line, but never fall below the stream's floor, so a crash dumped
 * after an INFO line is not filed as INFO.
 */
class Classifier
{
public:
    explicit Classifier(Level minLevel): floor(minLevel), last(minLevel) {}

    Level classify(const std::string &line)
    {
        Level l;
        if (parse(line, l))
            last = l;
        else if (line.compare(0, 9, "Traceback") == 0)
            last = Level::Error;
        else if (last < floor)
            last = floor;
        return last;
    }

private:
    Level floor;
    Level last;
};

}

}

#endif
