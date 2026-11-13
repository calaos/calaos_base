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
#include <fstream>
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
inline std::string humanOffsetToPosix(const std::string &tz)
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
        ++i;
        ++digits;
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

//Cursor helpers for repairNamedOffsets().
inline bool skipSignedClock(const std::string &s, size_t &i, bool signRequired)
{
    size_t j = i;
    if (j < s.size() && (s[j] == '+' || s[j] == '-'))
        ++j;
    else if (signRequired)
        return false;
    size_t d = j;
    while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == ':'))
        ++j;
    if (j == d || !std::isdigit(static_cast<unsigned char>(s[d])))
        return false;
    i = j;
    return true;
}

inline bool skipHumanName(const std::string &s, size_t &i)
{
    size_t j = i;
    while (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j])))
        ++j;
    if (j - i < 3)
        return false;
    if (!skipSignedClock(s, j, true))
        return false;
    i = j;
    return true;
}

//Windows installers wrote "UTC+1-1UTC+2,M3.5.0,M10.5.0/3": the Qt abbreviations
//("UTC+1", "UTC+2") used as POSIX names without quotes. POSIX stops a name at the
//first digit or sign, so the screen would read "UTC" and an offset of +1 (two
//hours late). Quoting the names with <> gives the intended zone. A valid POSIX
//string never matches: a signed name must be followed by an offset, which a real
//daylight name or rule cannot start with.
inline std::string repairNamedOffsets(const std::string &tz)
{
    size_t i = 0;
    if (!skipHumanName(tz, i))
        return tz;
    const std::string std1 = tz.substr(0, i);
    size_t o1 = i;
    if (!skipSignedClock(tz, i, false))
        return tz;
    std::string out = "<" + std1 + ">" + tz.substr(o1, i - o1);

    if (i < tz.size() && tz[i] != ',')
    {
        size_t n = i;
        if (!skipHumanName(tz, i))
            return tz;
        out += "<" + tz.substr(n, i - n) + ">";
        size_t o2 = i;
        if (skipSignedClock(tz, i, false))
            out += tz.substr(o2, i - o2);
    }
    if (i < tz.size() && tz[i] != ',')
        return tz;
    return out + tz.substr(i);
}

//Legacy value to a string POSIX can read; valid POSIX and empty pass unchanged.
inline std::string toPosix(const std::string &tz)
{
    const std::string r = humanOffsetToPosix(tz);
    if (r != tz)
        return r;
    return repairNamedOffsets(tz);
}

inline const char *defaultZoneinfoRoot()
{
    return "/usr/share/zoneinfo";
}

//Reads the POSIX rule from the footer of a TZif v2+ file (RFC 8536: "\n<TZ>\n"
//after the v2 data block). Returns "" for a name that leaves the zoneinfo tree,
//a missing or v1 file, or a zone without a rule.
inline std::string posixFromIana(const std::string &iana, const std::string &root = defaultZoneinfoRoot())
{
    if (iana.empty() || iana.size() > 64 || iana[0] == '/' || iana.find("..") != std::string::npos)
        return "";
    for (unsigned char c: iana)
        if (!std::isalnum(c) && c != '/' && c != '_' && c != '-' && c != '+')
            return "";

    std::ifstream f(root + "/" + iana, std::ios::binary);
    if (!f)
        return "";
    char head[5];
    if (!f.read(head, sizeof(head)) || std::string(head, 4) != "TZif" || head[4] < '2')
        return "";

    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 8)
        return "";
    const std::streamoff want = size < 256 ? size : 256;
    std::string tail(static_cast<size_t>(want), '\0');
    f.seekg(size - want);
    if (!f.read(&tail[0], want) || tail.back() != '\n')
        return "";

    tail.pop_back();
    const size_t start = tail.rfind('\n');
    if (start == std::string::npos)
        return "";
    const std::string rule = tail.substr(start + 1);
    for (unsigned char c: rule)
        if (c < 0x20 || c > 0x7e)
            return "";
    return rule;
}

//The installer writes "UTC[+-]N[:MM]" into *_iana for an offset with no IANA
//zone (UTC-15, UTC-4:15, and UTC13 for the west: POSIX makes the sign optional).
//It is already a POSIX string (UTC-15 is 15 h east), so it is relayed as is:
//humanOffsetToPosix would invert it.
inline bool isPosixOffset(const std::string &s)
{
    if (s.size() < 4 || s.compare(0, 3, "UTC") != 0)
        return false;
    const size_t first = (s[3] == '+' || s[3] == '-') ? 4 : 3;
    size_t i = first;
    int hours = 0;
    while (i < s.size() && i < first + 2 && std::isdigit(static_cast<unsigned char>(s[i])))
        hours = hours * 10 + (s[i++] - '0');
    if (i == first || hours > 24)
        return false;
    if (i == s.size())
        return true;
    return s.size() - i == 3 && s[i] == ':' && std::isdigit(static_cast<unsigned char>(s[i + 1])) &&
           std::isdigit(static_cast<unsigned char>(s[i + 2])) && (s[i + 1] - '0') < 6;
}

//POSIX rule for an *_iana value: TZif footer, else a zoneless offset; "" if neither.
inline std::string ianaToPosix(const std::string &iana, const std::string &root = defaultZoneinfoRoot())
{
    const std::string derived = posixFromIana(iana, root);
    if (!derived.empty())
        return derived;
    return isPosixOffset(iana) ? iana : "";
}

//IANA wins when it yields a rule; otherwise the legacy value is repaired.
inline std::string resolve(const std::string &iana, const std::string &legacy,
                           const std::string &root = defaultZoneinfoRoot())
{
    const std::string derived = ianaToPosix(iana, root);
    return derived.empty() ? toPosix(legacy) : derived;
}

//Rewrites, in a copy, the clock timezone of every widget; the stored pages
//(and so io.xml) stay as the installer wrote them.
inline nlohmann::json normalizePages(nlohmann::json pages, const std::string &root = defaultZoneinfoRoot())
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
            auto ianaIt = widget.find("clock_timezone_iana");
            const std::string iana = (ianaIt != widget.end() && ianaIt->is_string()) ? ianaIt->get<std::string>() : "";
            auto it = widget.find("clock_timezone");
            const bool legacyStr = it != widget.end() && it->is_string();
            if (legacyStr || !ianaToPosix(iana, root).empty())
                widget["clock_timezone"] = resolve(iana, legacyStr ? it->get<std::string>() : "", root);
        }
    }
    return pages;
}

//Applies both rewrites to the config message built for a screen.
//screensaverIana is the screen's screensaver_clock_timezone_iana param.
inline void normalizeConfig(nlohmann::json &data, const std::string &screensaverIana = "",
                            const std::string &root = defaultZoneinfoRoot())
{
    auto it = data.find("screensaver_clock_timezone");
    const bool legacyStr = it != data.end() && it->is_string();
    if (legacyStr || !ianaToPosix(screensaverIana, root).empty())
        data["screensaver_clock_timezone"] = resolve(screensaverIana, legacyStr ? it->get<std::string>() : "", root);
    if (data.contains("pages"))
        data["pages"] = normalizePages(data["pages"], root);
}

} //namespace LegacyTimezone
} //namespace Calaos

#endif
