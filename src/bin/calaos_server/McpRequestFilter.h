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
#ifndef S_McpRequestFilter_H
#define S_McpRequestFilter_H

#include "HttpClient.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace Calaos
{

/* Client identity of the /mcp tunnel.
 *
 * The sidecar listens on a Unix socket, so from its side every peer is local
 * and no equivalent of TransportLimits::isTrustedProxyPeer() can be written
 * there: it has nothing to test. The decision has to be taken here, where the
 * TCP peer is known, and carried to the sidecar in a header the client cannot
 * write - hence the credential, a secret derived from mcp_service_token that
 * never leaves the machine.
 *
 * Stripping the client's own X-Calaos-Client lines needs every request head of
 * the connection, not just the first one, so the tunnel is framed here request
 * by request instead of being spliced blind. Everything that cannot be framed
 * with certainty tears the connection down (obs-folded headers, ambiguous
 * Content-Length/Transfer-Encoding, trailers, upgrades): a security filter that
 * guesses is a security filter that can be walked past.
 *
 * Header-inline and free of uvw so tests link nothing of the server binary.
 */
namespace McpRequestFilter
{

constexpr std::size_t MAX_HEAD_BYTES = 8 * 1024;
constexpr std::size_t MAX_LINE_BYTES = 1024;

//The only client identity the sidecar believes. Value: "<credential> <ip>".
constexpr const char *TRUSTED_HEADER = "X-Calaos-Client";

inline bool iequalsAscii(const std::string &a, const char *b)
{
    std::size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (std::size_t i = 0; i < n; ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

inline std::string trimOws(const std::string &s)
{
    std::string::size_type b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return std::string();
    std::string::size_type e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

inline std::string toLowerAscii(const std::string &s)
{
    std::string r;
    r.reserve(s.size());
    for (char c : s) r.push_back(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

//The identity is pasted into a header line the sidecar splits on a space, so
//anything that is not address-shaped is replaced rather than escaped.
inline std::string safeIdentity(const std::string &id)
{
    if (id.empty() || id.size() > 45)
        return "unknown";
    for (char c : id)
    {
        if (!std::isxdigit(static_cast<unsigned char>(c)) &&
            c != '.' && c != ':' && c != '%')
            return "unknown";
    }
    return id;
}

inline bool parseUnsigned(const std::string &s, unsigned long long &out)
{
    if (s.empty() || s.size() > 19) return false;
    unsigned long long v = 0;
    for (char c : s)
    {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        v = v * 10 + static_cast<unsigned long long>(c - '0');
    }
    out = v;
    return true;
}

inline bool parseChunkSize(const std::string &line, unsigned long long &out)
{
    std::string hex = line.substr(0, line.find(';'));
    hex = trimOws(hex);
    if (hex.empty() || hex.size() > 16) return false;
    unsigned long long v = 0;
    for (char c : hex)
    {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        v = v * 16 + static_cast<unsigned long long>(
                std::isdigit(static_cast<unsigned char>(c))?
                c - '0':
                std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
    }
    out = v;
    return true;
}

/* Rewrites one request head. Returns false when the head must not reach the
 * sidecar at all. `bodyLen` / `chunked` describe the body that follows, so the
 * caller can skip it without ever reading it as headers.
 */
inline bool sanitizeHead(const std::string &head,
                         const std::string &peerIp,
                         const std::string &credential,
                         std::string &out,
                         unsigned long long &bodyLen,
                         bool &chunked)
{
    bodyLen = 0;
    chunked = false;

    std::string kept;
    std::string requestLine;
    std::string lastXff;
    std::string transferEncoding;
    std::string contentLength;
    int contentLengthCount = 0;
    bool first = true;

    std::string::size_type pos = 0;
    for (;;)
    {
        auto eol = head.find("\r\n", pos);
        if (eol == std::string::npos) return false;
        std::string line = head.substr(pos, eol - pos);
        pos = eol + 2;

        if (first)
        {
            requestLine = line;
            first = false;
            continue;
        }
        if (line.empty())
            break;

        //obs-fold: a continuation line would let a dropped header survive
        //through its second line.
        if (line[0] == ' ' || line[0] == '\t') return false;

        auto colon = line.find(':');
        if (colon == std::string::npos || colon == 0) return false;
        std::string name = line.substr(0, colon);
        if (name.find_first_of(" \t") != std::string::npos) return false;
        std::string value = trimOws(line.substr(colon + 1));

        if (iequalsAscii(name, TRUSTED_HEADER))
            continue;

        if (iequalsAscii(name, "x-forwarded-for"))
            lastXff = value;
        else if (iequalsAscii(name, "content-length"))
        {
            contentLength = value;
            contentLengthCount++;
        }
        else if (iequalsAscii(name, "transfer-encoding"))
            transferEncoding = toLowerAscii(value);
        else if (iequalsAscii(name, "upgrade"))
            return false;
        else if (iequalsAscii(name, "connection") &&
                 toLowerAscii(value).find("upgrade") != std::string::npos)
            return false;

        kept += line;
        kept += "\r\n";
    }

    if (contentLengthCount > 1) return false;
    if (!transferEncoding.empty())
    {
        if (contentLengthCount > 0) return false;
        if (transferEncoding == "chunked") chunked = true;
        else if (transferEncoding != "identity") return false;
    }
    else if (contentLengthCount == 1 && !parseUnsigned(contentLength, bodyLen))
        return false;

    const std::string ident =
        safeIdentity(TransportLimits::effectiveClientIp(lastXff, peerIp));

    out = requestLine;
    out += "\r\n";
    out += kept;
    out += TRUSTED_HEADER;
    out += ": ";
    out += credential;
    out += " ";
    out += ident;
    out += "\r\n\r\n";
    return true;
}

/* Streaming filter over the client -> sidecar direction of one tunnel. */
class Filter
{
public:
    Filter(std::string peerIp, std::string credential):
        peer(std::move(peerIp)), cred(std::move(credential))
    {}

    //Appends the bytes to forward to `out`. Returns false when the connection
    //must be torn down.
    bool feed(const std::string &data, std::string &out)
    {
        buf.append(data);

        for (;;)
        {
            switch (state)
            {
            case State::Head:
            {
                auto end = buf.find("\r\n\r\n");
                if (end == std::string::npos)
                    return buf.size() <= MAX_HEAD_BYTES;

                std::string head = buf.substr(0, end + 4);
                buf.erase(0, end + 4);

                std::string rewritten;
                unsigned long long len = 0;
                bool ch = false;
                if (!sanitizeHead(head, peer, cred, rewritten, len, ch))
                    return false;
                out.append(rewritten);

                if (ch)
                    state = State::ChunkSize;
                else if (len > 0)
                {
                    remaining = len;
                    state = State::Body;
                }
                break;
            }

            case State::Body:
            case State::ChunkData:
            {
                std::size_t n = static_cast<std::size_t>(
                    std::min<unsigned long long>(remaining, buf.size()));
                out.append(buf, 0, n);
                buf.erase(0, n);
                remaining -= n;
                if (remaining > 0)
                    return true;
                state = (state == State::Body)? State::Head: State::ChunkSize;
                break;
            }

            case State::ChunkSize:
            {
                auto eol = buf.find("\r\n");
                if (eol == std::string::npos)
                    return buf.size() <= MAX_LINE_BYTES;

                unsigned long long sz = 0;
                if (!parseChunkSize(buf.substr(0, eol), sz))
                    return false;
                out.append(buf, 0, eol + 2);
                buf.erase(0, eol + 2);

                if (sz == 0)
                    state = State::Trailer;
                else
                {
                    remaining = sz + 2; //chunk data plus its CRLF
                    state = State::ChunkData;
                }
                break;
            }

            case State::Trailer:
            {
                auto eol = buf.find("\r\n");
                if (eol == std::string::npos)
                    return buf.size() <= MAX_LINE_BYTES;
                //A trailer field is a header the sidecar's parser may or may
                //not merge; refused instead of reasoned about.
                if (eol != 0) return false;
                out.append(buf, 0, 2);
                buf.erase(0, 2);
                state = State::Head;
                break;
            }
            }

            if (buf.empty())
                return true;
        }
    }

private:
    enum class State { Head, Body, ChunkSize, ChunkData, Trailer };

    std::string peer;
    std::string cred;
    std::string buf;
    State state = State::Head;
    unsigned long long remaining = 0;
};

}

}

#endif
