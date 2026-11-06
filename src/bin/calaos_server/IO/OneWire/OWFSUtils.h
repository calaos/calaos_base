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
#ifndef OWFSUTILS_H
#define OWFSUTILS_H

#include <string>

namespace OWFSUtils
{

//An OWFS root directory entry is a device when it starts with its hex
//family code (e.g. "28.4515A80000"); other entries are OWFS virtual
//folders (alarm, bus.0, settings, ...) and must be skipped.
//Inclusive ranges: the old strict >/< comparisons wrongly dropped devices
//whose family code starts with '0', '9', 'A' or 'F'. Deliberately not
//isxdigit(): OWFS device addresses are uppercase, while virtual folders
//are lowercase ("alarm", "bus.0" start with hex digits 'a'/'b').
inline bool entryIsDevice(const std::string &s)
{
    if (s.empty())
        return false;
    const char c = s[0];
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'F');
}

//Normalize an OWFS directory entry to a device name (strip trailing '/')
inline std::string entryToDeviceName(const std::string &s)
{
    if (!s.empty() && s[s.length() - 1] == '/')
        return s.substr(0, s.length() - 1);
    return s;
}

}

#endif
