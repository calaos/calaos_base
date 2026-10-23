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
#include "DaemonClient.h"
#include "UrlDownloader.h"

#include <cctype>
#include <cstdlib>

namespace Calaos
{

DaemonReply DaemonReply::parse(int status, const string &body, bool statusRoute)
{
    DaemonReply r;
    r.status = status;

    if (status <= 0)
        return r;

    const Json j = Json::parse(body, nullptr, false);
    const bool envelope = j.is_object() && j.contains("error") && j["error"].is_boolean();

    string code;
    if (envelope && j.contains("code") && j["code"].is_string())
        code = j["code"].get<string>();

    //A 401 is a bearer problem unless the daemon names the session: confusing
    //the two would make a stale token ask for the admin password in a loop.
    if (status == 401)
    {
        r.kind = code == "bad_session" ? Kind::Error : Kind::Bearer;
        r.code = code;
        return r;
    }

    //An old daemon answers a plain-text 404 on every /api/v2/ route
    if (status == 404 && (statusRoute || code.empty()))
    {
        r.kind = Kind::TooOld;
        return r;
    }

    if (!envelope)
        return r;

    if (!j["error"].get<bool>())
    {
        r.kind = Kind::Ok;
        r.data = j.value("data", Json());
        return r;
    }

    if (code.empty())
        return r;

    r.kind = Kind::Error;
    r.code = code;
    if (j.contains("msg") && j["msg"].is_string())
        r.msg = j["msg"].get<string>();
    r.data = j.value("data", Json());

    return r;
}

DaemonClient &DaemonClient::Instance()
{
    static DaemonClient inst;
    return inst;
}

DaemonClient::DaemonClient()
{
}

bool DaemonClient::parseBaseUrl(const string &url, string &baseOut)
{
    static const string scheme = "http://";
    if (url.compare(0, scheme.size(), scheme) != 0)
        return false;

    string rest = url.substr(scheme.size());
    if (!rest.empty() && rest.back() == '/')
        rest.pop_back();

    string host, after;
    if (!rest.empty() && rest[0] == '[')
    {
        const string::size_type end = rest.find(']');
        if (end == string::npos)
            return false;
        host = rest.substr(1, end - 1);
        after = rest.substr(end + 1);
    }
    else
    {
        const string::size_type colon = rest.find(':');
        host = rest.substr(0, colon);
        if (colon != string::npos)
            after = rest.substr(colon);
    }

    if (host != "127.0.0.1" && host != "::1")
        return false;

    if (!after.empty())
    {
        const string port = after.substr(1);
        if (after[0] != ':' || port.empty() || port.size() > 5)
            return false;
        for (char c: port)
        {
            if (!isdigit((unsigned char)c))
                return false;
        }
        const long value = std::strtol(port.c_str(), nullptr, 10);
        if (value < 1 || value > 65535)
            return false;
    }

    baseOut = scheme + (host == "::1" ? string("[::1]") : host) + after;
    return true;
}

void DaemonClient::configure()
{
    const char *envUrl = std::getenv("CALAOS_CONTAINER_URL");
    const char *envToken = std::getenv("CALAOS_CONTAINER_TOKEN_FILE");

    const string url = (envUrl && *envUrl) ? string(envUrl) : string(DefaultUrl);

    base.clear();
    if (!parseBaseUrl(url, base))
    {
        //The value is not printed: it is not ours and may carry credentials
        cErrorDom("admin") << "CALAOS_CONTAINER_URL refused, only http://127.0.0.1 and "
                              "http://[::1] are allowed: calaos-container calls are disabled";
        base.clear();
    }

    tokenFile = (envToken && *envToken) ? string(envToken) : string(DefaultTokenFile);
    readToken();
}

void DaemonClient::readToken()
{
    token = Utils::trim(Utils::getFileContent(tokenFile.c_str()));
}

void DaemonClient::call(const Request &request, Callback callback)
{
    if (base.empty())
    {
        callback(DaemonReply());
        return;
    }

    send(request, callback, false);
}

void DaemonClient::send(const Request &request, Callback callback, bool retried)
{
    UrlDownloader *dl = new UrlDownloader(base + "/api/v2/" + request.path, true);

    if (!token.empty())
        dl->setHeader("Authorization", "Bearer " + token);
    if (!request.session.empty())
        dl->setHeader("X-Calaos-Admin-Session", request.session);
    if (!request.clientIp.empty())
        dl->setHeader("X-Calaos-Client-IP", request.clientIp);
    dl->setTimeout(request.timeout);
    //The poll repeats forever and AdminState logs the outcome once
    dl->setQuiet(true);
    //A redirect would carry the bearer and the session away from the loopback
    dl->setFollowRedirects(false);

    const bool statusRoute = request.path == "admin/status";

    //DaemonClient is a never destroyed singleton, `this` cannot dangle
    dl->m_signalCompleteData.connect([this, request, callback, retried, statusRoute](const string &data, int status)
                                     {
        const DaemonReply reply = DaemonReply::parse(status, data, statusRoute);

        if (reply.kind == DaemonReply::Kind::Bearer && !retried)
        {
            cInfoDom("admin") << "calaos-container refused the bearer, reading the token again";
            readToken();
            send(request, callback, true);
            return;
        }

        if (reply.kind == DaemonReply::Kind::Bearer)
            cWarningDom("admin") << "calaos-container still refuses the bearer of " << tokenFile;

        callback(reply);
    });

    bool started;
    if (request.method == "POST")
    {
        dl->setHeader("Content-Type", "application/json");
        started = dl->httpPost(string(), request.body.is_null() ? string("{}") : request.body.dump());
    }
    else
    {
        started = dl->httpGet();
    }

    if (!started)
    {
        //LCOV_EXCL_START only reached when libcurl cannot create a handle
        dl->cancel();
        callback(DaemonReply());
        //LCOV_EXCL_STOP
    }
}

} //namespace Calaos
