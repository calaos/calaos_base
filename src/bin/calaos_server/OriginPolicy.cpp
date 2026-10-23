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
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
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
    if (inet_pton(AF_INET6, s.c_str(), addr) == 1 &&
        inet_ntop(AF_INET6, addr, buf, sizeof(buf)))
    {
        out = buf;
        return true;
    }
    return false;
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

//A rebinding page retries in a loop: one line per origin is enough.
bool firstTime(const std::string &key)
{
    static std::set<std::string> logged;
    if (logged.size() >= 64)
        logged.clear();
    return logged.insert(key).second;
}

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
    Endpoint o;
    if (!parseOrigin(origin, o))
        return false;

    Endpoint h;
    const bool hasHost = !req.host.empty();
    const bool hostOk = hasHost && parseHost(req.host, h);
    if (hasHost && !hostOk)
        return false;

    const bool portOk = !hostOk || h.port.empty() || h.port == o.port;

    //The certificate presented for the Host is what stops a rebound name,
    //so the page itself must have come over https too.
    if (TransportLimits::isSecureTransport(req.peerIp, req.forwardedProto) &&
        o.scheme == "https" && hostOk && h.host == o.host && portOk)
        return true;

    if (portOk)
    {
        for (const auto &ip: interfaceIps)
        {
            std::string c;
            if (canonicalIp(ip, c) && c == o.host)
                return true;
        }
    }

    for (const auto &entry: allowedHosts)
    {
        std::string v = Utils::str_to_lower(Utils::trim(entry));
        const auto sep = v.find("://");
        if (sep != std::string::npos)
            v.erase(0, sep + 3);
        if (!v.empty() && v.back() == '/')
            v.pop_back();

        Endpoint a;
        if (!parseHost(v, a))
            continue;
        if (a.host == o.host && (a.port.empty() || a.port == o.port))
            return true;
    }

    return false;
}

std::vector<std::string> interfaceAddresses()
{
    std::vector<std::string> ips;

    struct ifaddrs *list = nullptr;
    if (getifaddrs(&list) != 0)
    {
        if (firstTime("getifaddrs"))
            cWarningDom("network") << "Cannot list the interface addresses, "
                                   << "no IP of the box is an accepted Origin";
        return ips;
    }

    for (struct ifaddrs *i = list; i; i = i->ifa_next)
    {
        if (!i->ifa_addr)
            continue;

        char buf[INET6_ADDRSTRLEN];
        const void *a = nullptr;
        if (i->ifa_addr->sa_family == AF_INET)
            a = &reinterpret_cast<struct sockaddr_in *>(i->ifa_addr)->sin_addr;
        else if (i->ifa_addr->sa_family == AF_INET6)
            a = &reinterpret_cast<struct sockaddr_in6 *>(i->ifa_addr)->sin6_addr;
        else
            continue;

        if (inet_ntop(i->ifa_addr->sa_family, a, buf, sizeof(buf)))
            ips.push_back(buf);
    }

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

    return accepts(it->second, req, interfaceAddresses(), allowedHosts()) ?
           Verdict::Accepted : Verdict::Refused;
}

Decision decide(const std::unordered_map<std::string, std::string> &headers,
                const std::string &peerIp, bool enforced)
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
        cWarningDom("network") << "Refusing Origin " << what << " from " << peerIp
                               << ": not this box, nor in api_allowed_hosts";
    else
        cWarningDom("network") << "Origin " << what << " from " << peerIp
                               << " will be refused once the admin password is set, "
                               << "unless its host is added to api_allowed_hosts";
    return d;
}

} //namespace OriginPolicy
