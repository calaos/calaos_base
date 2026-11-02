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
#include "CameraTokens.h"
#include "Utils.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

std::map<std::string, CameraTokens::Entry> CameraTokens::tokens;
uint64_t CameraTokens::lastSerial = 0;

namespace
{

std::string digestOf(const std::string &token)
{
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    if (EVP_Digest(token.data(), token.size(), md, &len, EVP_sha256(), nullptr) != 1)
        return std::string(); //LCOV_EXCL_LINE only reached when OpenSSL has no SHA-256

    return std::string(reinterpret_cast<const char *>(md), len);
}

} //namespace

std::string CameraTokens::issue(const void *owner, double now)
{
    purgeExpired(now);

    size_t owned = 0;
    auto oldest = tokens.end();
    for (auto it = tokens.begin(); it != tokens.end(); ++it)
    {
        if (it->second.owner != owner)
            continue;
        owned++;
        if (oldest == tokens.end() || it->second.serial < oldest->second.serial)
            oldest = it;
    }
    if (owned >= MaxPerOwner)
        tokens.erase(oldest);

    if (tokens.size() >= MaxTokens)
    {
        cWarningDom("network") << "Too many camera tokens, none given";
        return std::string();
    }

    unsigned char raw[TokenLength / 2];
    if (RAND_bytes(raw, sizeof(raw)) != 1)
    {
        //LCOV_EXCL_START only reached when OpenSSL has no entropy
        cErrorDom("network") << "No random bytes for a camera token";
        return std::string();
        //LCOV_EXCL_STOP
    }

    static const char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(TokenLength);
    for (unsigned char c: raw)
    {
        token += hex[c >> 4];
        token += hex[c & 0x0f];
    }

    const std::string key = digestOf(token);
    if (key.empty())
        return std::string(); //LCOV_EXCL_LINE only reached when OpenSSL has no SHA-256

    tokens[key] = Entry{ owner, now + Lifetime, ++lastSerial };

    return token;
}

bool CameraTokens::accept(const std::string &token, double now)
{
    if (token.size() != TokenLength)
        return false;

    const std::string key = digestOf(token);
    if (key.empty())
        return false; //LCOV_EXCL_LINE only reached when OpenSSL has no SHA-256

    auto it = tokens.find(key);
    if (it == tokens.end())
        return false;

    if (it->second.expires <= now)
    {
        tokens.erase(it);
        return false;
    }

    return true;
}

void CameraTokens::revoke(const void *owner)
{
    for (auto it = tokens.begin(); it != tokens.end();)
    {
        if (it->second.owner == owner)
            it = tokens.erase(it);
        else
            ++it;
    }
}

void CameraTokens::revokeAll()
{
    tokens.clear();
}

size_t CameraTokens::count()
{
    return tokens.size();
}

void CameraTokens::purgeExpired(double now)
{
    for (auto it = tokens.begin(); it != tokens.end();)
    {
        if (it->second.expires <= now)
            it = tokens.erase(it);
        else
            ++it;
    }
}
