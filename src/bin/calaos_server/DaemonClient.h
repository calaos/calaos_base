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
#ifndef S_DaemonClient_H
#define S_DaemonClient_H

#include "Calaos.h"

#include <functional>

namespace Calaos
{

//One answer of calaos-container's versioned API (docs/API.md of calaos-container)
struct DaemonReply
{
    enum class Kind
    {
        Ok,          //envelope with error: false, data holds the payload
        Error,       //envelope with an error code
        Unreachable, //no connection, timeout or an answer that is not an envelope
        Bearer,      //401 that is not bad_session, still there after a token re-read
        TooOld,      //the daemon in memory predates the route
    };

    Kind kind = Kind::Unreachable;
    int status = 0;
    Json data;
    string code;
    string msg;

    //Classifies a raw HTTP answer. statusRoute is true for GET admin/status,
    //where any 404 means an old daemon.
    static DaemonReply parse(int status, const string &body, bool statusRoute);
};

//HTTP client of the calaos-container daemon. Every call goes to a fixed path
//under /api/v2/; nothing of the path comes from a client.
class DaemonClient
{
public:
    static constexpr const char *DefaultUrl = "http://127.0.0.1:8000";
    static constexpr const char *DefaultTokenFile = "/run/calaos/calaos-ct.token";
    static constexpr long DefaultTimeout = 4;
    //update/check runs a synchronous apt-get update on the daemon side
    static constexpr long UpdateCheckTimeout = 120;
    //admin/verify and admin/set hash with argon2id, slow on small boxes: a
    //set that times out here still happens, and the retry reads as an attack
    static constexpr long ArgonTimeout = 30;
    //system/restart waits for systemctl to restart the kiosk container
    static constexpr long RestartTimeout = 30;

    struct Request
    {
        string method = "GET";
        string path;     //relative to /api/v2/
        Json body;       //sent when not null
        string session;  //X-Calaos-Admin-Session, when not empty
        string clientIp; //X-Calaos-Client-IP, when not empty
        long timeout = DefaultTimeout;
    };

    using Callback = std::function<void(const DaemonReply &)>;

    static DaemonClient &Instance();

    //Reads CALAOS_CONTAINER_URL and CALAOS_CONTAINER_TOKEN_FILE, then the
    //token. Not an option of local_config.xml: config put could then send the
    //bearer and the admin password to a third party.
    void configure();

    //True for http://127.0.0.1[:port] and http://[::1][:port] only. base is
    //the URL without a trailing slash.
    static bool parseBaseUrl(const string &url, string &base);

    //Empty before configure() or when CALAOS_CONTAINER_URL was refused: every
    //call is then unreachable.
    const string &baseUrl() const { return base; }

    void call(const Request &request, Callback callback);

    //For the /events client, which talks to the daemon outside call()
    const string &bearer() const { return token; }
    void readToken();

    /* A bearer refused again after a token re-read is logged once, until the
     * daemon accepts it: a broken token would otherwise log at every poll.
     */
    void bearerRefused();
    void bearerAccepted();

private:
    DaemonClient();

    string base;
    string tokenFile;
    string token;
    bool bearerWarned = false;

    void send(const Request &request, Callback callback, bool retried);
};

} //namespace Calaos

#endif
