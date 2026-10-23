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
#include "OriginPolicy.h"
#include "HttpClient.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <chrono>
#include <set>

namespace OriginPolicy
{

namespace
{

bool canonicalIp(const std::string &s, std::string &out)
{
    char buf[INET6_ADDRSTRLEN];
    unsigned char addr[sizeof(struct in6_addr)];

    if (inet_pton(AF_INET, s.c_str(), addr) == 1 &&
        inet_ntop(AF_INET, addr, buf, sizeof(buf)))
    {
        out = buf;
        return true;
    }
    if (inet_pton(AF_INET6, s.c_str(), addr) != 1)
        return false;

    //::ffff:a.b.c.d is the IPv4 address, as for TransportLimits::isLoopbackPeer
    const struct in6_addr *a6 = reinterpret_cast<const struct in6_addr *>(addr);
    const bool mapped = IN6_IS_ADDR_V4MAPPED(a6);
    if (!inet_ntop(mapped ? AF_INET : AF_INET6, mapped ? addr + 12 : addr, buf, sizeof(buf)))
        return false; // LCOV_EXCL_LINE: INET6_ADDRSTRLEN always fits a parsed address
    out = buf;
    return true;
}

bool normalisePort(const std::string &s, std::string &out)
{
    if (s.empty())
    {
        out.clear();
        return true;
    }
    if (s.size() > 5 || s.find_first_not_of("0123456789") != std::string::npos)
        return false;
    const int v = std::stoi(s);
    if (v < 1 || v > 65535)
        return false;
    out = std::to_string(v);
    return true;
}

//host[:port] or [v6][:port], already lower case
bool splitHostPort(const std::string &s, std::string &host, std::string &port)
{
    if (s.empty())
        return false;

    std::string h, p;
    if (s[0] == '[')
    {
        const auto close = s.find(']');
        if (close == std::string::npos)
            return false;
        h = s.substr(1, close - 1);
        const std::string after = s.substr(close + 1);
        if (!after.empty())
        {
            if (after[0] != ':')
                return false;
            p = after.substr(1);
        }

        struct in6_addr a;
        if (inet_pton(AF_INET6, h.c_str(), &a) != 1)
            return false;
    }
    else
    {
        const auto colon = s.rfind(':');
        if (colon != std::string::npos)
        {
            //an IPv6 literal without brackets is not a host
            if (s.find(':') != colon)
                return false;
            h = s.substr(0, colon);
            p = s.substr(colon + 1);
        }
        else
        {
            h = s;
        }

        if (!h.empty() && h.back() == '.')
            h.pop_back();
        if (h.empty() ||
            h.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-_") != std::string::npos ||
            h.front() == '.' || h.find("..") != std::string::npos)
            return false;
    }

    if (!normalisePort(p, port))
        return false;

    std::string ip;
    host = canonicalIp(h, ip) ? ip : h;
    return true;
}

bool printable(const std::string &s)
{
    return std::all_of(s.begin(), s.end(), [](unsigned char c)
                       {
        return c > 0x20 && c < 0x7f;
    });
}

std::string header(const std::unordered_map<std::string, std::string> &headers, const char *name)
{
    const auto it = headers.find(name);
    return it != headers.end() ? it->second : std::string();
}

std::string describe(const std::string &origin)
{
    Endpoint o;
    if (parseOrigin(origin, o))
    {
        const bool v6 = o.host.find(':') != std::string::npos;
        return o.scheme + "://" + (v6 ? "[" + o.host + "]" : o.host) + ":" + o.port;
    }
    if (origin == "null")
        return "null";
    return "malformed (" + std::to_string(origin.size()) + " bytes)";
}

//A rebinding page retries in a loop, and anyone on the LAN can send endless
//distinct Origins: one line per key, a bounded set, forgotten every hour.
bool firstTime(const std::string &key)
{
    static std::set<std::string> logged;
    static bool overflowLogged = false;
    static auto since = std::chrono::steady_clock::now();

    const auto now = std::chrono::steady_clock::now();
    if (now - since >= std::chrono::hours(1))
    {
        // LCOV_EXCL_START: takes an hour of wall time
        logged.clear();
        overflowLogged = false;
        since = now;
        // LCOV_EXCL_STOP
    }

    if (logged.count(key))
        return false;
    if (logged.size() >= 64)
    {
        if (!overflowLogged)
            cWarningDom("network") << "Too many distinct Origins to report, "
                                   << "the next ones are not logged for an hour";
        overflowLogged = true;
        return false;
    }
    logged.insert(key);
    return true;
}

bool isBoxIp(const std::string &host, const std::vector<std::string> &interfaceIps)
{
    //The browser's own machine, not the box: the box itself goes through haproxy
    if (TransportLimits::isLoopbackPeer(host))
        return false;
    return std::find(interfaceIps.begin(), interfaceIps.end(), host) != interfaceIps.end();
}

//Behind haproxy the Host carries no port: the page is then on the default one
bool samePort(const Endpoint &o, const Endpoint &h)
{
    if (h.port.empty())
        return o.port == (o.scheme == "https" ? "443" : "80");
    return h.port == o.port;
}

struct Parsed
{
    Endpoint origin;
    Endpoint host;
    bool hostOk = false;
};

//False when the Origin cannot be accepted whatever the rules
bool prepare(const std::string &origin, const Request &req, Parsed &p)
{
    if (!parseOrigin(origin, p.origin))
        return false;
    if (req.host.empty())
        return true;
    p.hostOk = parseHost(req.host, p.host);
    return p.hostOk;
}

//The certificate presented for the Host is what stops a rebound name, so
//the page itself must have come over https too.
bool secureHostRule(const Parsed &p, const Request &req)
{
    const Endpoint &o = p.origin, &h = p.host;
    return TransportLimits::isSecureTransport(req.peerIp, req.forwardedProto) &&
           o.scheme == "https" && p.hostOk && h.host == o.host &&
           (h.port.empty() || h.port == o.port);
}

//Same IP and port as the Host: another service of the box is another origin
bool boxIpRule(const Parsed &p, const std::vector<std::string> &interfaceIps)
{
    return p.hostOk && p.host.host == p.origin.host && samePort(p.origin, p.host) &&
           isBoxIp(p.origin.host, interfaceIps);
}

bool allowedHostRule(const Parsed &p, const std::vector<std::string> &allowedHosts)
{
    for (const auto &entry: allowedHosts)
    {
        std::string v = Utils::str_to_lower(Utils::trim(entry));
        const auto sep = v.find("://");
        if (sep != std::string::npos)
            v.erase(0, sep + 3);
        if (!v.empty() && v.back() == '/')
            v.pop_back();

        Endpoint a;
        std::string ip;
        if (canonicalIp(v, ip))
            a.host = ip;
        else if (!parseHost(v, a))
        {
            if (firstTime("cfg:" + v))
                cWarningDom("network") << "api_allowed_hosts: ignoring unusable entry "
                                       << (printable(v) ? "\"" + v + "\"" : "of " + std::to_string(v.size()) + " bytes");
            continue;
        }

        if (a.host == p.origin.host && (a.port.empty() || a.port == p.origin.port))
            return true;
    }
    return false;
}

//A new DHCP lease or VPN address is picked up by the first request from it,
//a removed one stays accepted for at most CacheLifetime.
class InterfaceCache
{
public:
    static constexpr std::chrono::seconds CacheLifetime{ 30 };
    static constexpr std::chrono::seconds MinRefresh{ 1 };

    const std::vector<std::string> &get(bool missed)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto age = now - stamp;
        if (!loaded || age >= CacheLifetime || (missed && age >= MinRefresh))
        {
            ips = interfaceAddresses();
            stamp = now;
            loaded = true;
        }
        return ips;
    }

private:
    std::vector<std::string> ips;
    std::chrono::steady_clock::time_point stamp;
    bool loaded = false;
};

InterfaceCache interfaceCache;

} //namespace

bool parseOrigin(const std::string &value, Endpoint &out)
{
    if (!printable(value))
        return false;

    const std::string lower = Utils::str_to_lower(value);
    const auto sep = lower.find("://");
    if (sep == std::string::npos)
        return false;

    Endpoint e;
    e.scheme = lower.substr(0, sep);
    if (e.scheme != "http" && e.scheme != "https")
        return false;

    const std::string rest = lower.substr(sep + 3);
    if (rest.find_first_of("/?#@\\") != std::string::npos)
        return false;
    if (!splitHostPort(rest, e.host, e.port))
        return false;

    if (e.port.empty())
        e.port = e.scheme == "https" ? "443" : "80";

    out = e;
    return true;
}

bool parseHost(const std::string &value, Endpoint &out)
{
    if (!printable(value))
        return false;

    Endpoint e;
    if (!splitHostPort(Utils::str_to_lower(value), e.host, e.port))
        return false;

    out = e;
    return true;
}

bool accepts(const std::string &origin, const Request &req,
             const std::vector<std::string> &interfaceIps,
             const std::vector<std::string> &allowedHosts)
{
    Parsed p;
    if (!prepare(origin, req, p))
        return false;
    return secureHostRule(p, req) || boxIpRule(p, interfaceIps) ||
           allowedHostRule(p, allowedHosts);
}

std::vector<std::string> interfaceAddresses()
{
    std::vector<std::string> ips;

    struct ifaddrs *list = nullptr;
    if (getifaddrs(&list) != 0)
    {
        // LCOV_EXCL_START: getifaddrs only fails out of memory or file descriptors
        list = nullptr;
        if (firstTime("getifaddrs"))
            cWarningDom("network") << "Cannot list the interface addresses, "
                                   << "no IP of the box is an accepted Origin";
        // LCOV_EXCL_STOP
    }

    for (struct ifaddrs *i = list; i; i = i->ifa_next)
    {
        if (!i->ifa_addr || (i->ifa_flags & IFF_LOOPBACK))
            continue;

        const int family = i->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6)
            continue;

        char buf[INET6_ADDRSTRLEN];
        const auto *v4 = reinterpret_cast<const struct sockaddr_in *>(i->ifa_addr);
        const auto *v6 = reinterpret_cast<const struct sockaddr_in6 *>(i->ifa_addr);
        const void *a = family == AF_INET ? static_cast<const void *>(&v4->sin_addr) : static_cast<const void *>(&v6->sin6_addr);
        if (inet_ntop(family, a, buf, sizeof(buf)))
            ips.push_back(buf);
    }

    if (list)
        freeifaddrs(list);
    return ips;
}

std::vector<std::string> allowedHosts()
{
    std::vector<std::string> tokens, hosts;
    Utils::split(Utils::get_config_option("api_allowed_hosts"), tokens, ",");
    for (const auto &t: tokens)
    {
        const std::string v = Utils::trim(t);
        if (!v.empty())
            hosts.push_back(v);
    }
    return hosts;
}

Verdict evaluate(const std::unordered_map<std::string, std::string> &headers,
                 const std::string &peerIp)
{
    const auto it = headers.find("origin");
    if (it == headers.end())
        return Verdict::NoOrigin;

    Request req;
    req.host = header(headers, "host");
    req.peerIp = peerIp;
    req.forwardedProto = header(headers, "x-forwarded-proto");

    Parsed p;
    if (!prepare(it->second, req, p))
        return Verdict::Refused;
    if (secureHostRule(p, req))
        return Verdict::Accepted;
    //An IP the cache does not know yet may have just been assigned
    std::string ip;
    const bool mayBeNewIp = p.hostOk && p.host.host == p.origin.host && canonicalIp(p.origin.host, ip);
    const bool ok = boxIpRule(p, interfaceCache.get(false)) || allowedHostRule(p, allowedHosts()) || (mayBeNewIp && boxIpRule(p, interfaceCache.get(true)));
    return ok ? Verdict::Accepted : Verdict::Refused;
}

Decision decide(const std::unordered_map<std::string, std::string> &headers,
                const std::string &peerIp, const std::string &clientIp, bool enforced)
{
    Decision d;
    d.verdict = evaluate(headers, peerIp);
    d.enforced = enforced;

    if (d.verdict != Verdict::Refused)
        return d;

    const std::string what = describe(header(headers, "origin"));
    if (!firstTime((enforced ? "1" : "0") + what))
        return d;

    if (enforced)
        cWarningDom("network") << "Refusing Origin " << what << " from " << clientIp
                               << ": not this box, nor in api_allowed_hosts";
    else
        cWarningDom("network") << "Origin " << what << " from " << clientIp
                               << " will be refused once the admin password is set, "
                               << "unless its host is added to api_allowed_hosts";
    return d;
}

} //namespace OriginPolicy
