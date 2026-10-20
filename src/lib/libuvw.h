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

#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace Calaos
{

/* Whether a configured listen address must be bound with the IPv6 form of
 * uvw's bind().
 *
 * Asking is not optional. bind() is templated on the family and defaults to
 * IPv4; uv_ip4_addr() zeroes its output BEFORE reporting that it could not
 * parse the literal, and uvw drops that return code. An IPv6 literal bound
 * with the default template therefore binds 0.0.0.0 without a word: it WIDENS
 * to every interface a listen the operator wrote to narrow to one.
 */
inline bool isIpv6Literal(const std::string &ip)
{
    struct in6_addr a;
    return inet_pton(AF_INET6, ip.c_str(), &a) == 1;
}

/* inet_pton, not inet_aton: the shorthand forms the latter accepts ("127.1",
 * "0x7f.0.0.1") would bind something other than what is written.
 */
inline bool isIpv4Literal(const std::string &ip)
{
    struct in_addr a;
    return inet_pton(AF_INET, ip.c_str(), &a) == 1;
}

//One phrase for both fallbacks and both servers, so a log can be searched for it.
static const char kWidenedListen[] = "listening on 0.0.0.0 (every interface) instead";

/* The address to actually bind, and - through refused - the configured value
 * when it is not usable at all.
 *
 * Widening rather than refusing to start is a product choice: a typo in a
 * configuration file must not turn a home automation box into a brick. It is
 * only defensible while it is said out loud, which is what refused is for.
 */
inline std::string listenAddressOrWildcard(const std::string &configured,
                                           std::string &refused)
{
    if (configured.empty() || isIpv4Literal(configured) || isIpv6Literal(configured))
        return configured.empty()? std::string("0.0.0.0"): configured;

    refused = configured;
    return "0.0.0.0";
}

/* Binds handle to ip:port and answers whether it landed there.
 *
 * The listener has to exist BEFORE bind(): uvw publishes the failure the
 * instant it happens, so an owner subscribing afterwards never hears it. And
 * an unheard failure is not a refusal - listen() and recv() do not check, they
 * let libuv auto-bind the handle on the wildcard address AND AN EPHEMERAL
 * PORT. A server on a port nobody chose is worse than one bound too wide:
 * its own operator cannot find it either.
 */
template<typename H, typename... Opts>
bool bindListenAddress(H &handle, const std::string &ip, unsigned int port,
                       Opts &&...opts)
{
    bool bound = true;
    auto conn = handle.template on<uvw::ErrorEvent>(
                    [&bound](const uvw::ErrorEvent &, H &) { bound = false; });

    if (isIpv6Literal(ip))
        handle.template bind<uvw::IPv6>(ip, port, std::forward<Opts>(opts)...);
    else
        handle.template bind<uvw::IPv4>(ip, port, std::forward<Opts>(opts)...);

    handle.erase(conn);
    return bound;
}

/* What a TCP peer that goes away looks like from this side: reset or timed out
 * on read, EPIPE on write, ENOTCONN on shutdown, ECANCELED for the writes the
 * close drops. Browsers, phones and panels do it all day; it is not a fault.
 */
inline bool isPeerDisconnect(int uvError)
{
    switch (uvError)
    {
    case UV_ECONNRESET:
    case UV_EPIPE:
    case UV_ETIMEDOUT:
    case UV_ECONNABORTED:
    case UV_ENOTCONN:
    case UV_ECANCELED:
        return true;
    default:
        return false;
    }
}

static const char kMappedPrefix[] = "::ffff:";

/* An IPv4-mapped literal reduced to its dotted quad, everything else returned
 * as it stands.
 *
 * The unmapping is not cosmetic: a dual-stack listen hands back
 * `::ffff:192.0.2.7` where an IPv4 listen says `192.0.2.7`, so without it the
 * identity of a correspondent - a per-client bucket, a configured Wago host -
 * would depend on which address the server was told to bind. inet_ntop is the
 * only writer of these bytes and it renders a mapped address in the dotted
 * form, so the textual test is exact.
 */
inline std::string unmappedLiteral(const std::string &ip)
{
    const std::string mapped(kMappedPrefix);
    if (ip.compare(0, mapped.size(), mapped) == 0 &&
        ip.find(':', mapped.size()) == std::string::npos)
        return ip.substr(mapped.size());
    return ip;
}

/* The inverse, and it is needed on the way out: an AF_INET6 descriptor refuses
 * a sockaddr_in with ENETUNREACH, so a dotted quad has to travel mapped.
 */
inline std::string mappedLiteral(const std::string &ip)
{
    if (isIpv4Literal(ip))
        return kMappedPrefix + ip;
    return ip;
}

/* The peer of an accepted TCP handle, in the one spelling the rest of the tree
 * compares against: a bare literal, no brackets, no port, no zone, and an
 * IPv4-mapped peer given as its dotted quad. Empty means no connected peer.
 */
inline std::string tcpPeerAddress(const uvw::TcpHandle &handle)
{
    const std::string v6 = handle.peer<uvw::IPv6>().ip;
    if (!v6.empty())
        return unmappedLiteral(v6);

    return handle.peer<uvw::IPv4>().ip;
}

/* Answers on the family the socket is bound to, not on the family the
 * destination literal happens to read in.
 *
 * send() is templated like bind() and defaults to IPv4, and uv_ip4_addr()
 * zeroes its output before reporting it could not read an IPv6 literal - so a
 * reply leaves an IPv6 socket as a sockaddr_in and is refused. That costs more
 * than the one datagram: the refusal is published on the handle, and an owner
 * that stops the handle on ErrorEvent stops receiving anything at all.
 *
 * The family is read from the handle rather than remembered beside it, which
 * only works because sock<IPv6>() is empty on an AF_INET descriptor.
 */
template<typename H>
void sendDatagram(H &handle, const std::string &ip, unsigned int port,
                  char *data, unsigned int len)
{
    if (handle.template sock<uvw::IPv6>().ip.empty())
        handle.template send<uvw::IPv4>(ip, port, data, len);
    else
        handle.template send<uvw::IPv6>(mappedLiteral(ip), port, data, len);
}

/* The same, for a payload the caller cannot keep alive itself.
 *
 * The overload above LENDS the buffer: uvw's char* send() neither copies it nor
 * owns it - its deleter does nothing - and libuv reads it only at the next turn
 * of the loop as soon as another send is already queued. Lending it anything
 * shorter-lived than the request puts freed memory on the wire, to whoever
 * asked. This one hands uvw a buffer uvw deletes when the request ends, which
 * happens on SendEvent, on ErrorEvent, and on a send refused outright.
 */
template<typename H>
void sendDatagram(H &handle, const std::string &ip, unsigned int port,
                  const std::string &payload)
{
    std::unique_ptr<char[]> data(new char[payload.size()]);
    std::memcpy(data.get(), payload.data(), payload.size());

    if (handle.template sock<uvw::IPv6>().ip.empty())
        handle.template send<uvw::IPv4>(ip, port, std::move(data),
                                        payload.size());
    else
        handle.template send<uvw::IPv6>(mappedLiteral(ip), port, std::move(data),
                                        payload.size());
}

}

#endif
