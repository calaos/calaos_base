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
#ifndef HMACAUTHENTICATOR_H
#define HMACAUTHENTICATOR_H

#include "Utils.h"
#include "AuthFailureReason.h"
#include "RemoteUIManager.h"
#include <openssl/crypto.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <vector>

using namespace Utils;

namespace Calaos
{

class RemoteUI;

struct WebSocketHeaders
{
    string authorization;
    string auth_timestamp;
    string auth_nonce;
    string auth_hmac;
    string user_agent;
    string origin;
    string device_version;
    string device_hardware_id;

    bool parse(const std::map<string, string> &headers);
    bool isValid() const;
};

class HMACAuthenticator
{
public:
    // Authenticate WebSocket connection with HMAC
    // Returns true on success, false on failure
    // Sets failure_reason to indicate why authentication failed (for error responses)
    static bool authenticateWebSocketConnection(const WebSocketHeaders &headers,
                                              const string &client_ip,
                                              RemoteUI* &authenticated_remote_ui,
                                              AuthFailureReason &failure_reason);

    static bool authenticateHttpRequest(const std::map<string, string> &headers,
                                      const string &client_ip,
                                      RemoteUI* &authenticated_remote_ui);

    static string extractTokenFromBearer(const string &authorization);
    static bool validateTimestamp(const string &timestamp);
    static string generateNonce();

    /* --- Shared helpers -----------------------------------------------
     * Defined inline in the header on purpose: the unit tests exercise
     * them without linking HMACAuthenticator.o, which would drag in
     * RemoteUIManager and the whole server core.
     */

    /* Case-insensitive HTTP header lookup, shared by the WebSocket and
     * HTTP auth paths (was duplicated as two identical lambdas before).
     */
    static string findHeader(const std::map<string, string> &headers, const string &key)
    {
        // Try different case variations
        auto it = headers.find(key);
        if (it != headers.end())
            return it->second;

        string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
        it = headers.find(lower_key);
        if (it != headers.end())
            return it->second;

        return "";
    }

    /* Decode a hex string to raw bytes. Returns false on odd length or any
     * non-hex character. Accepts upper and lower case digits.
     */
    static bool hexDecode(const string &hex, std::vector<unsigned char> &out)
    {
        out.clear();
        if (hex.length() % 2 != 0)
            return false;
        out.reserve(hex.length() / 2);
        for (size_t i = 0; i < hex.length(); i += 2)
        {
            int hi = hexNibble(hex[i]);
            int lo = hexNibble(hex[i + 1]);
            if (hi < 0 || lo < 0)
                return false;
            out.push_back(static_cast<unsigned char>((hi << 4) | lo));
        }
        return true;
    }

    /* Constant-time comparison of a client-supplied hex-encoded MAC against
     * the locally computed raw MAC. The length is guarded first
     * (CRYPTO_memcmp is only constant-time over equal-length buffers, and
     * the MAC length is public knowledge anyway), then the raw bytes are
     * compared with CRYPTO_memcmp so the comparison time does not depend on
     * how many leading bytes of the supplied MAC are correct.
     */
    static bool constantTimeHexEquals(const string &supplied_hex,
                                      const unsigned char *computed, size_t computed_len)
    {
        std::vector<unsigned char> supplied;
        if (!hexDecode(supplied_hex, supplied))
            return false;
        if (computed_len == 0 || supplied.size() != computed_len)
            return false;
        return CRYPTO_memcmp(supplied.data(), computed, computed_len) == 0;
    }

private:
    // Use TIMESTAMP_TOLERANCE_SECONDS from RemoteUIManager.h
    // (No local definition - prevents duplication and inconsistency)

    static int hexNibble(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
};

}

#endif