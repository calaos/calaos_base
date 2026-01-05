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
#ifndef AUTHFAILUREREASON_H
#define AUTHFAILUREREASON_H

#include <string>

namespace Calaos
{

// Authentication failure reasons for detailed error reporting
// Used by devices to determine if they should re-provision
enum class AuthFailureReason
{
    Success = 0,           // Authentication succeeded
    MissingHeaders,        // Required auth headers not provided (HTTP 400)
    InvalidToken,          // Token not found/invalid - device should re-provision (HTTP 401)
    InvalidTimestamp,      // Timestamp expired or invalid format (HTTP 401)
    InvalidNonce,          // Nonce invalid or already used (HTTP 401)
    InvalidHMAC,           // HMAC signature mismatch (HTTP 403)
    RateLimited            // Too many authentication attempts (HTTP 429)
};

// Get HTTP status code for auth failure reason
inline int authFailureToHttpStatus(AuthFailureReason reason)
{
    switch (reason)
    {
        case AuthFailureReason::Success:
            return 200;
        case AuthFailureReason::MissingHeaders:
            return 400;
        case AuthFailureReason::InvalidToken:
        case AuthFailureReason::InvalidTimestamp:
        case AuthFailureReason::InvalidNonce:
            return 401;
        case AuthFailureReason::InvalidHMAC:
            return 403;
        case AuthFailureReason::RateLimited:
            return 429;
        default:
            return 401;
    }
}

// Get error string for auth failure reason
inline std::string authFailureToString(AuthFailureReason reason)
{
    switch (reason)
    {
        case AuthFailureReason::Success:
            return "success";
        case AuthFailureReason::MissingHeaders:
            return "missing_headers";
        case AuthFailureReason::InvalidToken:
            return "invalid_token";
        case AuthFailureReason::InvalidTimestamp:
            return "invalid_timestamp";
        case AuthFailureReason::InvalidNonce:
            return "invalid_nonce";
        case AuthFailureReason::InvalidHMAC:
            return "invalid_hmac";
        case AuthFailureReason::RateLimited:
            return "rate_limited";
        default:
            return "unknown_error";
    }
}

}

#endif
