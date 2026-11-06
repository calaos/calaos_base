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
#ifndef S_OriginPolicy_H
#define S_OriginPolicy_H

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

/* Which browser Origins may use the API (docs/08_http_api.md, section Origin).
 * Comparing the Origin to Host alone lets DNS rebinding through: the rebound
 * name is both. Only three origins are trusted: the Host itself behind the
 * TLS front end, where the certificate stops rebinding; an IP of the box; a
 * host of api_allowed_hosts.
 */
namespace OriginPolicy
{

//An Origin, Host or api_allowed_hosts value, normalised: lower case, no
//trailing dot, IP in canonical form without brackets. An Origin always has
//its port, the default one of its scheme when absent; a Host only has the
//port it carries.
struct Endpoint
{
    std::string scheme;
    std::string host;
    std::string port;
};

//Only http and https origins parse; "null", a path or a user part do not.
bool parseOrigin(const std::string &value, Endpoint &out);
bool parseHost(const std::string &value, Endpoint &out);

struct Request
{
    std::string host;           //Host header
    std::string peerIp;         //TCP peer
    std::string forwardedProto; //X-Forwarded-Proto
};

bool accepts(const std::string &origin, const Request &req,
             const std::vector<std::string> &interfaceIps,
             const std::vector<std::string> &allowedHosts);

//Loopback excluded: in an Origin it names the browser's machine, not the box
std::vector<std::string> interfaceAddresses();

std::vector<std::string> allowedHosts();

enum class Verdict
{
    NoOrigin,
    Accepted,
    Refused,
};

//Reads the interfaces (cached) and the config only when the Host rule fails.
Verdict evaluate(const std::unordered_map<std::string, std::string> &headers,
                 const std::string &peerIp);

struct Decision
{
    Verdict verdict = Verdict::NoOrigin;
    bool enforced = false;
    bool logged = false;

    bool refused() const { return verdict == Verdict::Refused && enforced; }

    //Without admin, Access-Control-Allow-Origin keeps echoing any Origin.
    bool allowCors() const
    {
        return verdict == Verdict::Accepted ||
               (verdict == Verdict::Refused && !enforced);
    }
};

//evaluate() under AdminState, logging what is or would be refused. peerIp is
//the TCP peer the rules need, clientIp the one worth logging (X-Forwarded-For).
Decision decide(const std::unordered_map<std::string, std::string> &headers,
                const std::string &peerIp, const std::string &clientIp, bool enforced);

using Clock = std::function<std::chrono::steady_clock::time_point()>;

//For tests: the clock of the hourly reset of the refused-Origin log cap
void setLogClock(Clock clock);

} //namespace OriginPolicy

#endif
