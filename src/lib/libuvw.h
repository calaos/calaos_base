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
#ifndef UVW_H
#define UVW_H

//Ignore shadow warnings for external libs

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "uvw/src/uvw.hpp"
#pragma GCC diagnostic pop

#include <arpa/inet.h>

#include <string>

namespace Calaos
{

/* The peer address of an accepted TCP handle, in the single spelling the rest
 * of the tree compares against: a bare literal, no brackets, no port, no zone,
 * and an IPv4-mapped peer given as its dotted quad.
 *
 * The unmapping is not cosmetic: a dual-stack listen hands back
 * `::ffff:192.0.2.7` for the client an IPv4 listen calls `192.0.2.7`, so
 * without it the identity of every per-client bucket, and every localhost test
 * of the tree, would depend on which address the server was told to bind. Only
 * inet_ntop writes these bytes and it renders a mapped address in the dotted
 * form, so the textual test is exact; the second condition rejects anything
 * else beginning the same way.
 *
 * An empty answer means the handle has no connected peer.
 */
/* Whether a configured listen address must be bound with the IPv6 form of
 * uvw's bind().
 *
 * ⚠️ Asking is not optional. bind() is templated on the family and defaults to
 * IPv4; uv_ip4_addr() zeroes its output BEFORE reporting that it could not
 * parse the literal, and uvw drops that return code. Binding an IPv6 literal
 * with the default template therefore binds 0.0.0.0 without a word - it WIDENS
 * to every interface a listen the operator wrote to narrow to one.
 */
inline bool isIpv6Literal(const std::string &ip)
{
    struct in6_addr a;
    return inet_pton(AF_INET6, ip.c_str(), &a) == 1;
}

inline std::string tcpPeerAddress(const uvw::TcpHandle &handle)
{
    const std::string v6 = handle.peer<uvw::IPv6>().ip;
    if (!v6.empty())
    {
        static const std::string mapped("::ffff:");
        if (v6.compare(0, mapped.size(), mapped) == 0 &&
            v6.find(':', mapped.size()) == std::string::npos)
            return v6.substr(mapped.size());
        return v6;
    }

    return handle.peer<uvw::IPv4>().ip;
}

}

#endif
