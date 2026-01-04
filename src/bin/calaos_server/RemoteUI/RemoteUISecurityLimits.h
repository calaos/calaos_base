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
#ifndef REMOTEUISECURITYLIMITS_H
#define REMOTEUISECURITYLIMITS_H

// Security limits for RemoteUI system to prevent DoS attacks
// These limits protect against memory exhaustion from malicious payloads

namespace RemoteUISecurityLimits
{
    // Maximum HTTP request body size (1 MB)
    // Prevents memory exhaustion from huge JSON payloads
    static const size_t MAX_REQUEST_BODY_SIZE = 1024 * 1024;  // 1 MB

    // Maximum number of pages per RemoteUI
    // Prevents memory exhaustion from excessive page definitions
    static const size_t MAX_PAGES_PER_REMOTEUI = 50;

    // Maximum number of widgets per page
    // Prevents memory exhaustion from excessive widget definitions
    static const size_t MAX_WIDGETS_PER_PAGE = 100;

    // Maximum length for string fields (names, MAC addresses, etc.)
    // Prevents memory exhaustion from extremely long strings
    static const size_t MAX_STRING_LENGTH = 255;
}

#endif // REMOTEUISECURITYLIMITS_H
