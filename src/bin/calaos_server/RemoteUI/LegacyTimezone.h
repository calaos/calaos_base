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
#ifndef LEGACYTIMEZONE_H
#define LEGACYTIMEZONE_H

#include <cctype>
#include <cstdio>
#include <string>
#include "json.hpp"

namespace Calaos
{
namespace LegacyTimezone
{

//Installers older than 9d85a26 stored the human offset ("UTC+1", "UTC-9:30")
//straight into *_timezone. The screens hand that string to setenv("TZ"), where
//the sign is inverted: "UTC+1" would show UTC-1. Only "UTC"/"GMT" followed by a
//sign is rewritten; a bare "UTC0", "CET-1CEST,..." or an empty value is
//returned untouched.
inline std::string toPosix(const std::string &tz)
{
    if (tz.size() < 5 || (tz.compare(0, 3, "UTC") != 0 && tz.compare(0, 3, "GMT") != 0))
        return tz;
    const char sign = tz[3];
    if (sign != '+' && sign != '-')
        return tz;

    size_t i = 4;
    int hours = 0, digits = 0;
    while (i < tz.size() && std::isdigit(static_cast<unsigned char>(tz[i])) && digits < 2)
    {
        hours = hours * 10 + (tz[i] - '0');
        ++i; ++digits;
    }
    if (digits == 0)
        return tz;

    int minutes = 0;
    if (i < tz.size())
    {
        if (tz[i] != ':' || tz.size() - i != 3 ||
            !std::isdigit(static_cast<unsigned char>(tz[i + 1])) ||
            !std::isdigit(static_cast<unsigned char>(tz[i + 2])))
            return tz;
        minutes = (tz[i + 1] - '0') * 10 + (tz[i + 2] - '0');
        if (minutes > 59)
            return tz;
    }
    if (hours > 14)
        return tz;

    if (hours == 0 && minutes == 0)
        return "UTC0";

    char name[16], off[16];
    //Name keeps the human sign, the offset after it is the POSIX (inverted) one.
    if (minutes)
    {
        snprintf(name, sizeof(name), "<%c%02d%02d>", sign, hours, minutes);
        snprintf(off, sizeof(off), "%s%d:%02d", sign == '+' ? "-" : "", hours, minutes);
    }
    else
    {
        snprintf(name, sizeof(name), "<%c%02d>", sign, hours);
        snprintf(off, sizeof(off), "%s%d", sign == '+' ? "-" : "", hours);
    }
    return std::string(name) + off;
}

//Rewrites, in a copy, the clock timezone of every widget; the stored pages
//(and so io.xml) stay as the installer wrote them.
inline nlohmann::json normalizePages(nlohmann::json pages)
{
    if (!pages.is_array())
        return pages;
    for (auto &page: pages)
    {
        if (!page.is_object() || !page.contains("widgets") || !page["widgets"].is_array())
            continue;
        for (auto &widget: page["widgets"])
        {
            if (!widget.is_object())
                continue;
            auto it = widget.find("clock_timezone");
            if (it != widget.end() && it->is_string())
                *it = toPosix(it->get<std::string>());
        }
    }
    return pages;
}

}
}

#endif
