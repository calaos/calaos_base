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
#ifndef S_CameraTokens_H
#define S_CameraTokens_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

/* Stands in for cn_user/cn_pass on a camera snapshot URL, which an <img> loads
 * without any way to send a header. Random, so it tells nothing of the
 * password; owned by the websocket session that asked for it and gone with it;
 * opens get_picture and nothing else (JsonApiHandlerHttp::processApi()).
 * Memory only: a restart forgets every token. Callers pass the loop time.
 */
class CameraTokens
{
public:
    static constexpr double Lifetime = 120.0;
    //A client renewing before expiry holds two; the oldest goes beyond that
    static constexpr size_t MaxPerOwner = 4;
    static constexpr size_t MaxTokens = 1024;
    //32 random bytes, in lowercase hexadecimal
    static constexpr size_t TokenLength = 64;

    //Empty when no token can be given (no random bytes, table full)
    static std::string issue(const void *owner, double now);
    static bool accept(const std::string &token, double now);

    static void revoke(const void *owner);
    //Empties the table between tests: no server path calls it
    static void revokeAll();

    static size_t count();

private:
    struct Entry
    {
        const void *owner = nullptr;
        double expires = 0.0;
        //Tokens of one loop iteration share their expiry: this orders them
        uint64_t serial = 0;
    };

    static void purgeExpired(double now);

    //Keyed by the SHA-256 of the token: a lookup compares digests, not the
    //token itself, so its timing tells nothing of a valid one.
    static std::map<std::string, Entry> tokens;
    static uint64_t lastSerial;
};

#endif
