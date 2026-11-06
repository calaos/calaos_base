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
#include "JsonApi.h"
#include "UrlDownloader.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <random>
#include <sstream>

namespace Calaos
{

DaemonReply DaemonReply::parse(int status, const string &body, bool statusRoute)
{
    DaemonReply r;
    r.status = status;

    if (status <= 0)
        return r;

    //A document deeper than a request may be would exhaust the stack of the
    //parser: it reads as an invalid answer
    const Json j = JsonApi::requestNestingWithinLimit(body) ? Json::parse(body, nullptr, false) : Json();
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

bool DaemonClient::isConsoleDir(const string &dir)
{
    if (dir.size() < 2 || dir[0] != '/' || dir.back() == '/')
        return false;

    string::size_type start = 1;
    while (start <= dir.size())
    {
        const string::size_type end = std::min(dir.find('/', start), dir.size());
        const string segment = dir.substr(start, end - start);
        if (segment.empty() || segment == "." || segment == "..")
            return false;
        start = end + 1;
    }
    return true;
}

void DaemonClient::configure()
{
    const char *envUrl = std::getenv("CALAOS_CONTAINER_URL");
    const char *envToken = std::getenv("CALAOS_CONTAINER_TOKEN_FILE");
    const char *envConsole = std::getenv("CALAOS_CONTAINER_CONSOLE_DIR");

    const string url = (envUrl && *envUrl) ? string(envUrl) : string(DefaultUrl);

    base.clear();
    if (!parseBaseUrl(url, base))
    {
        //The value is not printed: it is not ours and may carry credentials
        cErrorDom("admin") << "CALAOS_CONTAINER_URL refused, only http://127.0.0.1 and "
                              "http://[::1] are allowed: calaos-container calls are disabled";
        base.clear();
    }

    consoleDirectory = (envConsole && *envConsole) ? string(envConsole) : string(DefaultConsoleDir);
    if (!isConsoleDir(consoleDirectory))
    {
        cErrorDom("admin") << "CALAOS_CONTAINER_CONSOLE_DIR refused, it must be an absolute path without "
                              "\".\" or \"..\" segments: the console is disabled";
        consoleDirectory.clear();
    }

    tokenFile = (envToken && *envToken) ? string(envToken) : string(DefaultTokenFile);
    readToken();
}

namespace
{

void randomBytes(unsigned char *out, int size)
{
    if (RAND_bytes(out, size) == 1)
        return;
    //LCOV_EXCL_START only reached when OpenSSL has no entropy
    std::random_device rd;
    for (int i = 0; i < size; i++)
        out[i] = (unsigned char)rd();
}
//LCOV_EXCL_STOP

} //namespace

string DaemonClient::webSocketKey()
{
    unsigned char raw[16];
    randomBytes(raw, sizeof(raw));
    return Utils::Base64_encode(raw, sizeof(raw));
}

string DaemonClient::webSocketAccept(const string &key)
{
    const string in = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (EVP_Digest(in.data(), in.size(), digest, &len, EVP_sha1(), nullptr) != 1)
        return string(); //LCOV_EXCL_LINE only reached when OpenSSL has no SHA-1
    return Utils::Base64_encode(digest, (int)len);
}

int DaemonClient::parseHttpHead(const string &head, map<string, string> &headers)
{
    std::istringstream lines(head);
    string line;
    std::getline(lines, line);
    int status = 0;
    {
        std::istringstream first(line);
        string version;
        first >> version >> status;
    }

    while (std::getline(lines, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const string::size_type colon = line.find(':');
        if (colon == string::npos)
            continue;
        headers[Utils::str_to_lower(line.substr(0, colon))] = Utils::trim(line.substr(colon + 1));
    }
    return status;
}

uint32_t DaemonClient::webSocketMask()
{
    uint32_t key = 0;
    while (key == 0)
        randomBytes((unsigned char *)&key, sizeof(key));
    return key;
}

void DaemonClient::readToken()
{
    token = Utils::trim(Utils::getFileContent(tokenFile.c_str()));
}

void DaemonClient::bearerRefused()
{
    if (bearerWarned)
        return;
    bearerWarned = true;
    cWarningDom("admin") << "calaos-container still refuses the bearer of " << tokenFile;
}

void DaemonClient::bearerAccepted()
{
    if (bearerWarned)
        cInfoDom("admin") << "calaos-container accepts the bearer again";
    bearerWarned = false;
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
    //url_stall_timeout (30 s) would cut a slow answer before the longer timeouts
    dl->setStallTimeout(request.timeout + 1);
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
            cDebugDom("admin") << "calaos-container refused the bearer, reading the token again";
            readToken();
            send(request, callback, true);
            return;
        }

        if (reply.kind == DaemonReply::Kind::Bearer)
            bearerRefused();
        else if (reply.kind != DaemonReply::Kind::Unreachable)
            bearerAccepted();

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
